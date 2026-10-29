/* The replacement graphics on the graphics card (see gpu_hd.h). */
#include "gpu_hd.h"

#include "../../runtime/mods/textures.h"

static SDL_GPUDevice *device;
static SDL_GPUBuffer *buffer;
static SDL_GPUTransferBuffer *upload;
static uint32_t upload_size;
static uint32_t uploaded;                       /* how many replacements the card has */
static uint32_t texel_end;                      /* how many words of texels it has, after the table */
static uint32_t table_capacity, texel_capacity; /* the buffer's room: table entries, texel words */
/* For each replacement: where its texels are in the buffer (in words), and
 * which version of its pixels the card has (for ones that change). */
static uint32_t *places, *versions;
static uint32_t places_capacity;
static SDL_GPUTransferBuffer *changed_upload;
static uint32_t changed_upload_size;

/* A still picture's texels are followed by smaller copies, each half the one before, down to a texel
 * (raster3d.hlsl hd_texel reads the one with about a texel to a pixel): read at full size, a texture
 * drawn much smaller (a mod's model far away) flickered at its cut-outs and edges. A 2D sheet in its
 * own colours has them too (compose.h kd_hd reads them while a copy's texel comes from one tile): one
 * larger than shown lost its lines. Not for pictures that change, indexed sheets, nor bitmaps. */
#define SMALLER 0x8u /* in a table entry's info: the smaller copies follow its texels */

static int has_smaller(uint32_t id, const KhdaysTextureReplacement *r)
{
    if (r->info & KHDAYS_REPLACEMENT_NO_MIPS)
        return 0;
    const uint32_t kind = r->info & (KHDAYS_REPLACEMENT_INDEXED | KHDAYS_REPLACEMENT_TILES | KHDAYS_REPLACEMENT_BITMAP);
    return (kind == 0 || kind == KHDAYS_REPLACEMENT_TILES) && khdays_textures_version((int)id) == 0 &&
           (r->width > 1 || r->height > 1);
}

/* The words a replacement takes on the card: its texels, and the smaller copies of a still picture. */
static uint32_t words_of(uint32_t id, const KhdaysTextureReplacement *r)
{
    uint32_t w = (uint32_t)r->width, h = (uint32_t)r->height, words = w * h;
    if (!has_smaller(id, r)) {
        return words;
    }
    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        words += w * h;
    }
    return words;
}

/* The smaller copies of `rgba` (w x h, 8-bit RGBA, red first) one after the other into `out`:
 * each texel the four under it, their colours weighted by their alpha (a clear texel does not
 * darken its neighbours). */
static void make_smaller(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out)
{
    const uint8_t *from = rgba;
    while (w > 1 || h > 1) {
        const uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        for (uint32_t y = 0; y < nh; ++y) {
            for (uint32_t x = 0; x < nw; ++x) {
                uint32_t c[3] = {0, 0, 0}, plain[3] = {0, 0, 0}, a = 0;
                uint8_t *o = out + ((size_t)y * nw + x) * 4u;
                for (uint32_t k = 0; k < 4; ++k) {
                    const uint32_t sx = SDL_min(x * 2u + (k & 1u), w - 1u), sy = SDL_min(y * 2u + (k >> 1), h - 1u);
                    const uint8_t *t = from + ((size_t)sy * w + sx) * 4u;
                    for (int i = 0; i < 3; ++i) {
                        c[i] += (uint32_t)t[i] * t[3];
                        plain[i] += t[i];
                    }
                    a += t[3];
                }
                for (int i = 0; i < 3; ++i) {
                    o[i] = (uint8_t)(a > 0 ? (c[i] + a / 2u) / a : (plain[i] + 2u) / 4u);
                }
                o[3] = (uint8_t)((a + 2u) / 4u);
            }
        }
        from = out;
        out += (size_t)nw * nh * 4u;
        w = nw;
        h = nh;
    }
}

static SDL_GPUBuffer *make_buffer(void)
{
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    info.size = (table_capacity * 4u + texel_capacity) * 4u;
    return SDL_CreateGPUBuffer(device, &info);
}

int khdays_gpu_hd_open(SDL_GPUDevice *d)
{
    if (buffer != NULL && device == d) {
        return 1;
    }
    khdays_gpu_hd_close();
    device = d;
    table_capacity = 64;
    texel_capacity = 1u << 16;
    buffer = make_buffer();
    uploaded = texel_end = 0;
    if (buffer == NULL) {
        khdays_gpu_hd_close();
        return 0;
    }
    return 1;
}

void khdays_gpu_hd_close(void)
{
    if (device == NULL) {
        return;
    }
    if (buffer != NULL)
        SDL_ReleaseGPUBuffer(device, buffer);
    if (upload != NULL)
        SDL_ReleaseGPUTransferBuffer(device, upload);
    if (changed_upload != NULL)
        SDL_ReleaseGPUTransferBuffer(device, changed_upload);
    buffer = NULL;
    upload = NULL;
    changed_upload = NULL;
    upload_size = changed_upload_size = uploaded = texel_end = table_capacity = texel_capacity = 0;
    device = NULL;
}

SDL_GPUBuffer *khdays_gpu_hd_buffer(void)
{
    return buffer;
}

/* Uploads again, in place, the replacements whose pixels changed since the
 * card got them. */
static int sync_changed(SDL_GPUCommandBuffer *cmd)
{
    uint32_t bytes = 0, at = 0;
    uint8_t *mapped;
    SDL_GPUCopyPass *copy;
    for (uint32_t id = 0; id < uploaded; ++id) {
        const uint32_t version = khdays_textures_version((int)id);
        if (version != 0 && version != versions[id]) {
            const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
            bytes += (uint32_t)(r->width * r->height) * 4u;
        }
    }
    if (bytes == 0) {
        return 1;
    }
    if (bytes > changed_upload_size) {
        SDL_GPUTransferBufferCreateInfo info;
        if (changed_upload != NULL)
            SDL_ReleaseGPUTransferBuffer(device, changed_upload);
        SDL_zero(info);
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = bytes;
        changed_upload = SDL_CreateGPUTransferBuffer(device, &info);
        changed_upload_size = changed_upload != NULL ? bytes : 0;
        if (changed_upload == NULL) {
            return 0;
        }
    }
    mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, changed_upload, true);
    if (mapped == NULL) {
        return 0;
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    for (uint32_t id = 0; id < uploaded; ++id) {
        const uint32_t version = khdays_textures_version((int)id);
        if (version != 0 && version != versions[id]) {
            const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
            const uint32_t n = (uint32_t)(r->width * r->height) * 4u;
            SDL_GPUTransferBufferLocation src;
            SDL_GPUBufferRegion dst;
            if (n > bytes - at) {
                continue; /* changed by the game's thread after the sizes were added up: the next time */
            }
            khdays_textures_read_begin((int)id);
            versions[id] = khdays_textures_version((int)id);
            SDL_memcpy(mapped + at, r->rgba, n);
            khdays_textures_read_end((int)id);
            src.transfer_buffer = changed_upload;
            src.offset = at;
            dst.buffer = buffer;
            dst.offset = places[id] * 4u;
            dst.size = n;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
            at += n;
        }
    }
    SDL_UnmapGPUTransferBuffer(device, changed_upload);
    SDL_EndGPUCopyPass(copy);
    return 1;
}

int khdays_gpu_hd_sync(SDL_GPUCommandBuffer *cmd)
{
    const uint32_t count = (uint32_t)khdays_textures_replacements();
    uint32_t end = texel_end, first, words = 0, size, at = 0, texel;
    uint8_t *mapped;
    if (device == NULL || buffer == NULL) {
        return 0;
    }
    if (count <= uploaded) {
        return sync_changed(cmd);
    }
    if (count > places_capacity) {
        uint32_t n = places_capacity ? places_capacity : 64u;
        while (n < count)
            n *= 2;
        uint32_t *more_places = (uint32_t *)SDL_realloc(places, n * sizeof(uint32_t)), *more_versions;
        if (more_places == NULL) {
            return 0; /* keep the old tables and try again next frame */
        }
        places = more_places;
        more_versions = (uint32_t *)SDL_realloc(versions, n * sizeof(uint32_t));
        if (more_versions == NULL) {
            return 0;
        }
        versions = more_versions;
        places_capacity = n;
    }
    for (uint32_t id = uploaded; id < count; ++id) {
        end += words_of(id, khdays_textures_replacement((int)id));
    }
    if (count > table_capacity || end > texel_capacity) {
        /* make the buffer anew, bigger, and upload everything again (the texels after the larger table) */
        SDL_ReleaseGPUBuffer(device, buffer);
        while (table_capacity < count)
            table_capacity *= 2;
        while (texel_capacity < end)
            texel_capacity *= 2;
        buffer = make_buffer();
        uploaded = texel_end = 0;
        if (buffer == NULL) {
            return 0;
        }
    }
    first = uploaded;
    for (uint32_t id = first; id < count; ++id) {
        words += words_of(id, khdays_textures_replacement((int)id));
    }
    size = (count - first) * 16u + words * 4u;
    if (size > upload_size) {
        SDL_GPUTransferBufferCreateInfo info;
        if (upload != NULL)
            SDL_ReleaseGPUTransferBuffer(device, upload);
        SDL_zero(info);
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = size;
        upload = SDL_CreateGPUTransferBuffer(device, &info);
        upload_size = upload != NULL ? size : 0;
        if (upload == NULL) {
            return 0;
        }
    }
    mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, upload, true);
    if (mapped == NULL) {
        return 0;
    }
    texel = table_capacity * 4u + texel_end;
    for (uint32_t id = first; id < count; ++id) {
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        const uint32_t entry[4] = {texel, (uint32_t)r->width, (uint32_t)r->height,
                                   r->info | (has_smaller(id, r) ? SMALLER : 0u)};
        places[id] = texel;
        SDL_memcpy(mapped + at, entry, sizeof(entry));
        at += 16u;
        texel += words_of(id, r);
    }
    for (uint32_t id = first; id < count; ++id) {
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        const uint32_t bytes = (uint32_t)(r->width * r->height) * 4u;
        const uint32_t smaller = words_of(id, r) * 4u - bytes;
        khdays_textures_read_begin((int)id);
        versions[id] = khdays_textures_version((int)id);
        SDL_memcpy(mapped + at, r->rgba, bytes);
        if (smaller > 0) {
            /* made in memory of the program's (the mapped buffer may be slow to read back) */
            uint8_t *copies = (uint8_t *)SDL_malloc(smaller);
            if (copies != NULL) {
                make_smaller((const uint8_t *)r->rgba, (uint32_t)r->width, (uint32_t)r->height, copies);
                SDL_memcpy(mapped + at + bytes, copies, smaller);
                SDL_free(copies);
            } else {
                SDL_memset(mapped + at + bytes, 0, smaller);
            }
        }
        khdays_textures_read_end((int)id);
        at += bytes + smaller;
    }
    SDL_UnmapGPUTransferBuffer(device, upload);
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;
        src.transfer_buffer = upload;
        src.offset = 0;
        dst.buffer = buffer;
        dst.offset = first * 16u;
        dst.size = (count - first) * 16u;
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        if (words > 0) {
            src.offset = dst.size;
            dst.offset = (table_capacity * 4u + texel_end) * 4u;
            dst.size = words * 4u;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        }
        SDL_EndGPUCopyPass(copy);
    }
    uploaded = count;
    texel_end += words;
    return sync_changed(cmd);
}
