/* The replacement graphics on the graphics card (see gpu_hd.h). */
#include "gpu_hd.h"

#include "../../runtime/core/session_log.h"
#include "../../runtime/mods/textures.h"

#include <stdlib.h>

static SDL_GPUDevice *device;
static SDL_GPUBuffer *buffer;
static SDL_GPUTransferBuffer *upload;
static uint32_t upload_size;
static uint32_t known;                          /* how many replacements the card's table has entries for */
static uint32_t texel_end;                      /* the texel words ever used (holes below it, none above) */
static uint32_t table_capacity, texel_capacity; /* the buffer's room: table entries, texel words */
static int white_pending;                       /* the first texel (WHITE) not on the card yet */
static int refused, full_said, table_said;      /* the card refused a larger buffer; the full said */
static int at_most;                             /* the buffer is as large as MOST_BYTES lets it be */
static uint32_t now = 1;                        /* the picture being drawn (khdays_gpu_hd_next_frame) */
static uint32_t pinned_out;                     /* pictures kept on the card that did not fit yet */
static uint64_t let_go;                         /* on-demand pictures let go since the full was said */

/* For each replacement: where its texels are, in words from the texels' start (NOWHERE: not on the card --
 * an on-demand one not drawn lately, or one that did not fit), the version of its pixels the card has (for
 * ones that change), and the picture that last drew it (on-demand ones). */
static uint32_t *places, *versions, *drawn;
static uint32_t places_capacity;
static SDL_GPUTransferBuffer *changed_upload;
static uint32_t changed_upload_size;

/* The texels' free stretches below texel_end, in order (pictures let go). */
typedef struct Hole {
    uint32_t at, words;
} Hole;
static Hole *holes;
static uint32_t nholes, holes_room;

/* A still picture's texels are followed by smaller copies, each half the one before, down to a texel
 * (raster3d.hlsl hd_texel reads the one with about a texel to a pixel): read at full size, a texture
 * drawn much smaller (a mod's model far away) flickered at its cut-outs and edges. A 2D sheet in its
 * own colours has them too (compose.h kd_hd reads them while a copy's texel comes from one tile): one
 * larger than shown lost its lines. Not for pictures that change, indexed sheets, nor bitmaps. */
#define SMALLER 0x8u /* in a table entry's info: the smaller copies follow its texels */

/* The most the buffer holds: the shaders read it by byte addresses of 32 bits (hd.Load), and SDL's buffer
 * sizes are 32 bits. Past it the 3D's pictures (KHDAYS_REPLACEMENT_ON_DEMAND) not drawn in this picture
 * leave the card, the least lately drawn first, and come back when drawn again; a picture with no room even
 * so is the first texel, white -- the game's colours untextured, not another picture's -- until it fits. */
#define MOST_BYTES 0xfffff000ull
static uint64_t most_bytes = MOST_BYTES; /* (lowered by the tests: khdays_gpu_hd_set_most) */
#define NOWHERE 0xffffffffu
#define QUEUED 0xfffffffeu /* (a place being found for it in this upload) */
#define WHITE 0xffffffffu

static int on_demand(const KhdaysTextureReplacement *r)
{
    return (r->info & KHDAYS_REPLACEMENT_ON_DEMAND) != 0;
}

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
    texel_capacity = most_bytes / 4u - 64u * 4u < (1u << 16) ? (uint32_t)(most_bytes / 4u - 64u * 4u) : 1u << 16;
    buffer = make_buffer(table_capacity, texel_capacity);
    known = 0;
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
    upload_size = changed_upload_size = known = texel_end = table_capacity = texel_capacity = 0;
    white_pending = refused = full_said = table_said = at_most = 0;
    pinned_out = nholes = 0;
    let_go = 0;
    device = NULL;
}

void khdays_gpu_hd_set_most(uint64_t bytes)
{
    most_bytes = bytes != 0 && bytes < MOST_BYTES ? bytes : MOST_BYTES;
}

int khdays_gpu_hd_on_card(uint32_t id)
{
    return id < known && places[id] < QUEUED;
}

SDL_GPUBuffer *khdays_gpu_hd_buffer(void)
{
    return buffer;
}

void khdays_gpu_hd_next_frame(void)
{
    ++now;
}

/* --- The texels' room --------------------------------------------------------------- */

/* `words` of texels: the first hole they fit (`holes_too`), else past texel_end. NOWHERE if there is no room. */
static uint32_t take(uint32_t words, int holes_too)
{
    for (uint32_t i = 0; holes_too && i < nholes; ++i) {
        if (holes[i].words >= words) {
            const uint32_t at = holes[i].at;
            holes[i].at += words;
            holes[i].words -= words;
            if (holes[i].words == 0) {
                SDL_memmove(&holes[i], &holes[i + 1], (nholes - i - 1) * sizeof(Hole));
                --nholes;
            }
            return at;
        }
    }
    if ((uint64_t)texel_end + words <= texel_capacity) {
        const uint32_t at = texel_end;
        texel_end += words;
        return at;
    }
    return NOWHERE;
}

/* `words` at `at` free again: joined to the holes beside them, or to the room past texel_end. */
static void give_back(uint32_t at, uint32_t words)
{
    uint32_t i = 0;
    if (at + words == texel_end) {
        texel_end = at;
        while (nholes > 0 && holes[nholes - 1].at + holes[nholes - 1].words == texel_end) {
            texel_end = holes[--nholes].at;
        }
        return;
    }
    while (i < nholes && holes[i].at < at)
        ++i;
    if (i > 0 && holes[i - 1].at + holes[i - 1].words == at) {
        holes[i - 1].words += words; /* after the one before */
        if (i < nholes && holes[i - 1].at + holes[i - 1].words == holes[i].at) {
            holes[i - 1].words += holes[i].words; /* and the one after joined too */
            SDL_memmove(&holes[i], &holes[i + 1], (nholes - i - 1) * sizeof(Hole));
            --nholes;
        }
        return;
    }
    if (i < nholes && at + words == holes[i].at) {
        holes[i].at = at;
        holes[i].words += words;
        return;
    }
    if (nholes == holes_room) {
        const uint32_t room = holes_room ? holes_room * 2u : 64u;
        Hole *more = (Hole *)SDL_realloc(holes, room * sizeof(Hole));
        if (more == NULL) {
            return; /* without memory the stretch stays unused */
        }
        holes = more;
        holes_room = room;
    }
    SDL_memmove(&holes[i + 1], &holes[i], (nholes - i) * sizeof(Hole));
    holes[i].at = at;
    holes[i].words = words;
    ++nholes;
}

/* The on-demand pictures on the card not drawn in this picture, the least lately drawn first. */
static uint32_t *leaving;
static uint32_t nleaving, leaving_room, leaving_next;

static int by_drawn(const void *a, const void *b)
{
    const uint32_t x = drawn[*(const uint32_t *)a], y = drawn[*(const uint32_t *)b];
    return x < y ? -1 : x > y;
}

static void list_leaving(uint32_t count)
{
    nleaving = leaving_next = 0;
    for (uint32_t id = 0; id < count && id < known; ++id) {
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        if (places[id] < QUEUED && on_demand(r) && drawn[id] != now) {
            if (nleaving == leaving_room) {
                const uint32_t room = leaving_room ? leaving_room * 2u : 256u;
                uint32_t *more = (uint32_t *)SDL_realloc(leaving, room * sizeof(uint32_t));
                if (more == NULL) {
                    break;
                }
                leaving = more;
                leaving_room = room;
            }
            leaving[nleaving++] = id;
        }
    }
    SDL_qsort(leaving, nleaving, sizeof(uint32_t), by_drawn);
}

/* A place for picture `id` (`words`): in a hole or past the end, else after letting go of the pictures
 * least lately drawn (`may_let_go`) until it fits. NOWHERE if it does not. */
static uint32_t place_for(uint32_t words, uint32_t count, int may_let_go, int *listed)
{
    uint32_t at = take(words, may_let_go);
    while (at == NOWHERE && may_let_go) {
        if (!*listed) {
            list_leaving(count);
            *listed = 1;
        }
        if (leaving_next >= nleaving) {
            break;
        }
        {
            const uint32_t id = leaving[leaving_next++];
            if (places[id] < QUEUED) {
                give_back(places[id], words_of(id, khdays_textures_replacement((int)id)));
                places[id] = NOWHERE;
                ++let_go;
            }
        }
        at = take(words, 1);
    }
    return at;
}

/* --- Uploads ------------------------------------------------------------------------ */

/* Uploads again, in place, the replacements whose pixels changed since the
 * card got them. */
static int sync_changed(SDL_GPUCommandBuffer *cmd)
{
    uint32_t bytes = 0, at = 0;
    uint8_t *mapped;
    SDL_GPUCopyPass *copy;
    for (uint32_t id = 0; id < known; ++id) {
        const uint32_t version = khdays_textures_version((int)id);
        if (version != 0 && version != versions[id] && places[id] < QUEUED) {
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
    for (uint32_t id = 0; id < known; ++id) {
        const uint32_t version = khdays_textures_version((int)id);
        if (version != 0 && version != versions[id] && places[id] < QUEUED) {
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
            dst.offset = (table_capacity * 4u + places[id]) * 4u;
            dst.size = n;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
            at += n;
        }
    }
    SDL_UnmapGPUTransferBuffer(device, changed_upload);
    SDL_EndGPUCopyPass(copy);
    return 1;
}

static int by_place_down(const void *a, const void *b)
{
    const uint32_t x = places[*(const uint32_t *)a], y = places[*(const uint32_t *)b];
    return x > y ? -1 : x < y;
}

/* The texels in use brought down to `room` words by letting go of the on-demand pictures at their top not
 * drawn in this picture (for a larger table at the most). Whether they are. */
static int free_top(uint32_t count, uint64_t room)
{
    list_leaving(count);
    SDL_qsort(leaving, nleaving, sizeof(uint32_t), by_place_down);
    for (uint32_t i = 0; i < nleaving && texel_end > room; ++i) {
        const uint32_t id = leaving[i], words = words_of(id, khdays_textures_replacement((int)id));
        if (places[id] + words != texel_end) {
            break; /* the top is a picture that stays */
        }
        give_back(places[id], words);
        places[id] = NOWHERE;
        ++let_go;
    }
    nleaving = 0;
    return texel_end <= room;
}

/* A larger buffer for `count` replacements and `need` words of texels (as much of them as MOST_BYTES
 * allows), what the card has copied over on the card. 1 when it was made (all the table's entries are to be
 * uploaded again), 0 when not: as large as it can be, or refused by the card. */
static int grow(SDL_GPUCopyPass *copy, uint32_t count, uint64_t need)
{
    uint32_t table = table_capacity;
    uint64_t texels = texel_capacity, most;
    SDL_GPUBuffer *grown;
    while (table < count)
        table *= 2;
    while (texels < need)
        texels *= 2;
    if ((uint64_t)table * 4u + texels > most_bytes / 4u && (!at_most || table > table_capacity)) {
        /* reaching the most, or a larger table there: room for four times the pictures, so that the table
         * need not grow there again (it takes room from the texels, which the pictures may be using) */
        while ((uint64_t)table < (uint64_t)count * 4u && (uint64_t)table * 2u * 4u < most_bytes / 8u)
            table *= 2;
    }
    most = most_bytes / 4u - (uint64_t)table * 4u;
    if (texels > most)
        texels = most;
    if (texels < texel_end && !free_top(count, texels)) {
        return 0; /* (pictures that stay on the card past the room a larger table leaves) */
    }
    if (table == table_capacity && (texels == texel_capacity || refused)) {
        return 0; /* as large as it can be */
    }
    grown = make_buffer(table, (uint32_t)texels);
    khdays_log("hd: the card's replacement memory grown: %u pictures, %llu MB wanted, room for %llu MB%s", count,
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
    SDL_ReleaseGPUBuffer(device, buffer); /* (released once the card is done with it) */
    buffer = grown;
    at_most = texels == most;
    table_capacity = table;
    texel_capacity = (uint32_t)texels;
    return 1;
}

/* `id` to be placed in this upload (a new one kept on the card, one that did not fit yet, an on-demand one
 * drawn while not on the card). */
static uint32_t *wanted;
static uint32_t nwanted, wanted_room;

static int want(uint32_t id)
{
    if (nwanted == wanted_room) {
        const uint32_t room = wanted_room ? wanted_room * 2u : 256u;
        uint32_t *more = (uint32_t *)SDL_realloc(wanted, room * sizeof(uint32_t));
        if (more == NULL) {
            return 0;
        }
        wanted = more;
        wanted_room = room;
    }
    wanted[nwanted++] = id;
    places[id] = QUEUED;
    return 1;
}

/* The upload given up: the pictures wanted are not on the card (their room free again), the kept ones
 * looked for again next time. */
static int unwant(void)
{
    for (uint32_t i = 0; i < nwanted; ++i) {
        const uint32_t id = wanted[i];
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        if (places[id] < QUEUED) {
            give_back(places[id], words_of(id, r));
        }
        places[id] = NOWHERE;
        pinned_out += !on_demand(r);
    }
    nwanted = 0;
    return 0;
}

/* Picture `id`'s texels and smaller copies into `out` (words_of's). */
static void fill(uint32_t id, const KhdaysTextureReplacement *r, uint8_t *out)
{
    const uint32_t bytes = (uint32_t)(r->width * r->height) * 4u;
    const uint32_t smaller = words_of(id, r) * 4u - bytes;
    khdays_textures_read_begin((int)id);
    versions[id] = khdays_textures_version((int)id);
    SDL_memcpy(out, r->rgba, bytes);
    if (smaller > 0) {
        /* made in memory of the program's (the mapped buffer may be slow to read back) */
        uint8_t *copies = (uint8_t *)SDL_malloc(smaller);
        if (copies != NULL) {
            make_smaller((const uint8_t *)r->rgba, (uint32_t)r->width, (uint32_t)r->height, copies);
            SDL_memcpy(out + bytes, copies, smaller);
            SDL_free(copies);
        } else {
            SDL_memset(out + bytes, 0, smaller);
        }
    }
    khdays_textures_read_end((int)id);
}

static void say_full(const char *what)
{
    if (!full_said) {
        full_said = 1;
        khdays_log("hd: the card's replacement memory is full (%llu MB): %s",
                   ((unsigned long long)table_capacity * 4u + texel_capacity) * 4u >> 20, what);
    }
}

static int sync(SDL_GPUCommandBuffer *cmd, const KhdaysG3dPolygon *polys, int32_t npolys)
{
    const uint32_t count = (uint32_t)khdays_textures_replacements();
    uint32_t entries = count; /* the pictures with an entry in the table */
    uint64_t need, size;
    uint32_t words = 0, at = 0;
    int remade = 0, listed = 0;
    uint8_t *mapped;
    SDL_GPUCopyPass *copy;
    if (device == NULL || buffer == NULL) {
        return 0;
    }
    if (count > places_capacity) {
        uint32_t n = places_capacity ? places_capacity : 64u;
        while (n < count)
            n *= 2;
        uint32_t *more = (uint32_t *)SDL_realloc(places, n * sizeof(uint32_t));
        if (more == NULL) {
            return 0; /* keep the old tables and try again next frame */
        }
        places = more;
        more = (uint32_t *)SDL_realloc(versions, n * sizeof(uint32_t));
        if (more == NULL) {
            return 0;
        }
        versions = more;
        more = (uint32_t *)SDL_realloc(drawn, n * sizeof(uint32_t));
        if (more == NULL) {
            return 0;
        }
        drawn = more;
        places_capacity = n;
    }
    /* what is to be placed: the new pictures kept on the card, the kept ones that did not fit yet, and the
     * on-demand ones the polygons draw */
    nwanted = 0;
    for (uint32_t id = known; id < count; ++id) {
        places[id] = NOWHERE;
        versions[id] = 0;
        drawn[id] = 0;
    }
    {
        const uint32_t out = pinned_out;
        pinned_out = 0;
        for (uint32_t id = out > 0 ? 0u : known; id < count; ++id) {
            if (places[id] == NOWHERE && !on_demand(khdays_textures_replacement((int)id)) && !want(id)) {
                return unwant();
            }
        }
    }
    for (int32_t i = 0; i < npolys; ++i) {
        const int32_t r = polys[i].replacement;
        if (r >= 0 && (uint32_t)r < count && on_demand(khdays_textures_replacement(r))) {
            drawn[r] = now;
            if (places[r] == NOWHERE && !want((uint32_t)r)) {
                return unwant();
            }
        }
    }
    if (count == known && nwanted == 0 && !white_pending) {
        return sync_changed(cmd);
    }
    need = texel_end;
    for (uint32_t i = 0; i < nwanted; ++i) {
        need += words_of(wanted[i], khdays_textures_replacement((int)wanted[i]));
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    if (count > table_capacity || need > texel_capacity) {
        remade = grow(copy, count, need);
    }
    if (count > table_capacity) {
        /* no room for all their entries (a table that cannot grow past the texels in use): the pictures past
         * it are not placed, and drawn wrong, but the rest is drawn */
        uint32_t kept = 0;
        entries = table_capacity;
        for (uint32_t i = 0; i < nwanted; ++i) {
            const uint32_t id = wanted[i];
            if (id < entries) {
                wanted[kept++] = id;
            } else {
                places[id] = NOWHERE;
            }
        }
        nwanted = kept;
        if (!table_said) {
            table_said = 1;
            khdays_log("hd: the card's replacement memory has no room for the entries of %u pictures: they are "
                       "not drawn right",
                       count - entries);
        }
    }
    /* their places: in the holes and past the end, letting go of pictures not drawn lately when there is
     * no room -- not in a buffer just made, whose texels the card is still copying over */
    for (uint32_t i = 0; i < nwanted; ++i) {
        const uint32_t id = wanted[i];
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        const uint32_t n = words_of(id, r);
        const uint64_t before = let_go;
        places[id] = place_for(n, count, !remade, &listed);
        if (places[id] == NOWHERE) {
            pinned_out += !on_demand(r);
            say_full("the 3D's pictures not drawn lately leave it; a picture with no room drawn untextured");
            continue;
        }
        if (let_go != before) {
            say_full("the 3D's pictures not drawn lately leave it and come back when drawn");
        }
        words += n;
    }
    /* the whole table (its entries' places changed), the texels placed, the white texel */
    size = (uint64_t)entries * 16u + (uint64_t)words * 4u + (white_pending ? 4u : 0u);
    if (size > 0xffffffffull) {
        SDL_EndGPUCopyPass(copy);
        return unwant();
    }
    if (size > upload_size) {
        SDL_GPUTransferBufferCreateInfo info;
        if (upload != NULL)
            SDL_ReleaseGPUTransferBuffer(device, upload);
        SDL_zero(info);
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = (Uint32)size;
        upload = SDL_CreateGPUTransferBuffer(device, &info);
        upload_size = upload != NULL ? (uint32_t)size : 0;
    }
    mapped = upload != NULL ? (uint8_t *)SDL_MapGPUTransferBuffer(device, upload, true) : NULL;
    if (mapped == NULL) {
        SDL_EndGPUCopyPass(copy);
        return unwant();
    }
    /* written whole, then the copies asked for */
    for (uint32_t id = 0; id < entries; ++id) {
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        const int out = places[id] >= QUEUED;
        const uint32_t entry[4] = {table_capacity * 4u + (out ? 0u : places[id]), out ? 1u : (uint32_t)r->width,
                                   out ? 1u : (uint32_t)r->height,
                                   out ? 0u
                                       : (r->info & ~KHDAYS_REPLACEMENT_ON_DEMAND) | (has_smaller(id, r) ? SMALLER : 0u)};
        SDL_memcpy(mapped + at, entry, sizeof(entry));
        at += 16u;
    }
    for (uint32_t i = 0; i < nwanted; ++i) {
        const uint32_t id = wanted[i];
        const KhdaysTextureReplacement *r = khdays_textures_replacement((int)id);
        if (places[id] != NOWHERE) {
            fill(id, r, mapped + at);
            at += words_of(id, r) * 4u;
        }
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
        dst.offset = 0;
        dst.size = entries * 16u;
        if (dst.size > 0)
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        src.offset = entries * 16u;
        for (uint32_t i = 0; i < nwanted; ++i) {
            const uint32_t id = wanted[i];
            if (places[id] == NOWHERE) {
                continue;
            }
            dst.offset = (table_capacity * 4u + places[id]) * 4u;
            dst.size = words_of(id, khdays_textures_replacement((int)id)) * 4u;
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
    nwanted = 0;
    known = entries;
    return sync_changed(cmd);
}

int khdays_gpu_hd_sync(SDL_GPUCommandBuffer *cmd)
{
    return sync(cmd, NULL, 0);
}

int khdays_gpu_hd_sync_drawn(SDL_GPUCommandBuffer *cmd, const KhdaysG3dPolygon *polys, int32_t npolys)
{
    return sync(cmd, polys, npolys);
}
