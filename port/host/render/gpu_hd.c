/* The replacement graphics on the graphics card (see gpu_hd.h). */
#include "gpu_hd.h"

#include "../../runtime/core/session_log.h"
#include "../../runtime/mods/textures.h"

static SDL_GPUDevice *device;
static SDL_GPUBuffer *buffer;
static SDL_GPUTransferBuffer *upload;
static uint32_t upload_size;
static uint32_t uploaded;                       /* how many replacements the card has */
static uint32_t texel_end;                      /* how many words of texels it has, after the table */
static uint32_t table_capacity, texel_capacity; /* the buffer's room: table entries, texel words */
static int white_pending;                       /* the first texel (WHITE) not on the card yet */
static int refused, left_out_said;              /* the card refused a larger buffer; the full said */
/* For each replacement: where its texels are in the buffer (in words; LEFT_OUT: none, past the most the
 * buffer holds), and which version of its pixels the card has (for ones that change). */
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

/* The most the buffer holds: the shaders read it by byte addresses of 32 bits (hd.Load), and SDL's buffer
 * sizes are 32 bits. Pictures past it are left out: their table entry is the first texel, white, so they
 * show the game's colours untextured instead of another picture's. */
#define MOST_BYTES 0xfffff000ull
#define LEFT_OUT 0u
#define WHITE 0xffffffffu

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

/* A buffer of `table` entries and `texels` words (at most MOST_BYTES together). */
static SDL_GPUBuffer *make_buffer(uint32_t table, uint32_t texels)
{
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                 SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = (Uint32)(((uint64_t)table * 4u + texels) * 4u);
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
    buffer = make_buffer(table_capacity, texel_capacity);
    uploaded = 0;
    texel_end = 1; /* WHITE */
    white_pending = 1;
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
    white_pending = refused = left_out_said = 0;
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
        if (version != 0 && version != versions[id] && places[id] != LEFT_OUT) {
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
        if (version != 0 && version != versions[id] && places[id] != LEFT_OUT) {
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

/* A larger buffer for `count` replacements and `need` words of texels (as much of them as MOST_BYTES
 * allows): what the card has is copied over on the card, its texels after the larger table (`rewrite`: all
 * the table's entries are to be uploaded again, their places moved). 0 if the card refused it. */
static int grow(SDL_GPUCopyPass *copy, uint32_t count, uint64_t need, int *rewrite)
{
    uint32_t table = table_capacity;
    uint64_t texels = texel_capacity, most;
    SDL_GPUBuffer *grown;
    while (table < count)
        table *= 2;
    while (texels < need)
        texels *= 2;
    most = MOST_BYTES / 4u - (uint64_t)table * 4u;
    if (texels > most)
        texels = most;
    if (texels < texel_end) {
        return 0; /* (a table of hundreds of millions of entries) */
    }
    if (table == table_capacity && (texels == texel_capacity || refused)) {
        return 1; /* as large as it can be: the new pictures that do not fit are left out */
    }
    grown = make_buffer(table, (uint32_t)texels);
    khdays_log("hd: the card's replacement memory grown: %u pictures, %llu MB of them, room for %llu MB%s", count,
               (unsigned long long)need * 4u >> 20, ((unsigned long long)table * 4u + texels) * 4u >> 20,
               grown == NULL ? " -- refused by the card" : "");
    if (grown == NULL) {
        refused = 1; /* (not asked again for more texels) */
        return 0;
    }
    if (texel_end > 0 && !white_pending) { /* (a buffer just made has nothing on it yet) */
        SDL_GPUBufferLocation from, to;
        from.buffer = buffer;
        from.offset = table_capacity * 16u;
        to.buffer = grown;
        to.offset = table * 16u;
        SDL_CopyGPUBufferToBuffer(copy, &from, &to, texel_end * 4u, false);
    }
    for (uint32_t id = 0; id < uploaded; ++id) {
        if (places[id] != LEFT_OUT) {
            places[id] += (table - table_capacity) * 4u;
        }
    }
    SDL_ReleaseGPUBuffer(device, buffer); /* (released once the card is done with it) */
    buffer = grown;
    *rewrite = table != table_capacity;
    table_capacity = table;
    texel_capacity = (uint32_t)texels;
    return 1;
}

int khdays_gpu_hd_sync(SDL_GPUCommandBuffer *cmd)
{
    const uint32_t count = (uint32_t)khdays_textures_replacements();
    uint64_t need = texel_end;
    uint32_t first, entries_from, words = 0, size, at = 0, texel;
    int rewrite = 0;
    uint8_t *mapped;
    SDL_GPUCopyPass *copy;
    if (device == NULL || buffer == NULL) {
        return 0;
    }
    if (count <= uploaded && !white_pending) {
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
        need += words_of(id, khdays_textures_replacement((int)id));
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    if ((count > table_capacity || need > texel_capacity) && !grow(copy, count, need, &rewrite) &&
        count > table_capacity) {
        SDL_EndGPUCopyPass(copy);
        return 0; /* no room for their entries: try again next frame */
    }
    /* the places of the new ones, while they fit; the others left out */
    first = uploaded;
    texel = table_capacity * 4u + texel_end;
    for (uint32_t id = first; id < count; ++id) {
        const uint32_t n = words_of(id, khdays_textures_replacement((int)id));
        if ((uint64_t)texel_end + words + n <= texel_capacity) {
            places[id] = texel;
            texel += n;
            words += n;
        } else {
            places[id] = LEFT_OUT;
            if (!left_out_said) {
                left_out_said = 1;
                khdays_log("hd: the card's replacement memory is full (%llu MB): pictures past it are drawn "
                           "untextured",
                           ((unsigned long long)table_capacity * 4u + texel_capacity) * 4u >> 20);
            }
        }
    }
    entries_from = rewrite ? 0u : first;
    size = (count - entries_from) * 16u + words * 4u + (white_pending ? 4u : 0u);
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
            SDL_EndGPUCopyPass(copy);
            return 0;
        }
    }
    mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, upload, true);
    if (mapped == NULL) {
        SDL_EndGPUCopyPass(copy);
        return 0;
    }
    for (uint32_t id = entries_from; id < count; ++id) {
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        const int out = places[id] == LEFT_OUT;
        const uint32_t entry[4] = {out ? table_capacity * 4u : places[id], out ? 1u : (uint32_t)r->width,
                                   out ? 1u : (uint32_t)r->height,
                                   out ? 0u : r->info | (has_smaller(id, r) ? SMALLER : 0u)};
        SDL_memcpy(mapped + at, entry, sizeof(entry));
        at += 16u;
    }
    for (uint32_t id = first; id < count; ++id) {
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        const uint32_t bytes = (uint32_t)(r->width * r->height) * 4u;
        const uint32_t smaller = words_of(id, r) * 4u - bytes;
        if (places[id] == LEFT_OUT) {
            continue;
        }
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
    if (white_pending) {
        const uint32_t white = WHITE;
        SDL_memcpy(mapped + at, &white, 4);
    }
    SDL_UnmapGPUTransferBuffer(device, upload);
    {
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;
        src.transfer_buffer = upload;
        src.offset = 0;
        dst.buffer = buffer;
        dst.offset = entries_from * 16u;
        dst.size = (count - entries_from) * 16u;
        if (dst.size > 0)
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        src.offset += dst.size;
        if (words > 0) {
            dst.offset = (table_capacity * 4u + texel_end) * 4u;
            dst.size = words * 4u;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
            src.offset += dst.size;
        }
        if (white_pending) {
            dst.offset = table_capacity * 16u;
            dst.size = 4u;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
            white_pending = 0;
        }
    }
    SDL_EndGPUCopyPass(copy);
    uploaded = count;
    texel_end += words;
    return sync_changed(cmd);
}
