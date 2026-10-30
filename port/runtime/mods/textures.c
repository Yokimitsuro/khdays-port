/* The 3D's textures by name, and the textures drawn instead (textures.h): a polygon's texture and
 * palette slots, their bank, the bank's origin (origin.c) and the names there (names.cpp); looked up
 * once per slot place, format and palette, and again only after its bank changed. */
#include "textures.h"
#include "names.h"
#include "../core/origin.h"
#include "../core/rom.h"
#include "../core/vram.h"
#include "../../gpu/texdecode.h"
#include "../../host/app/host.h"
#include "../../host/render/png_io.h"

#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int env_read;
static int trace;
static int env_hd;
static char env_dump_dir[MAX_PATH];

/* The directory the dump writes to now ("" when it is off): KHDAYS_DUMP_GRAPHICS=DIR,
 * or saves/dump/graphics with the video settings' "Dump graphics". */
static const char *dump_dir(void)
{
    if (env_dump_dir[0] != '\0')
        return env_dump_dir;
    return (khdays_host_graphics_flags() & KHDAYS_HOST_DUMP_GRAPHICS) ? "saves/dump/graphics" : "";
}

int khdays_textures_wanted(void)
{
    if (!env_read) {
        const char *dir = getenv("KHDAYS_DUMP_GRAPHICS");
        const char *t = getenv("KHDAYS_TRACE_TEXTURES");
        if (dir != NULL && *dir != '\0') {
            snprintf(env_dump_dir, sizeof(env_dump_dir), "%s", dir);
        }
        const char *hd = getenv("KHDAYS_HD_GRAPHICS");
        trace = t != NULL && *t != '\0' && *t != '0';
        env_hd = hd != NULL && *hd != '\0' && *hd != '0';
        env_read = 1;
    }
    /* the HD graphics need the names too, so with a window this is on by default */
    return env_dump_dir[0] != '\0' || trace || env_hd || khdays_host_graphics_flags() != 0;
}

/* Whether the replacements are drawn: the settings' "HD graphics", or
 * KHDAYS_HD_GRAPHICS=1 (for runs without a window). */
static int hd_on(void)
{
    return env_hd || (khdays_host_graphics_flags() & KHDAYS_HOST_HD_GRAPHICS) != 0;
}

const char *khdays_textures_dump_dir(void)
{
    khdays_textures_wanted(); /* reads the environment */
    return dump_dir();
}

int khdays_textures_hd(void)
{
    khdays_textures_wanted(); /* reads the environment */
    return hd_on();
}

/* --- What a polygon's texture is -------------------------------------------- */

/* Where byte `address` of `area` is: the bank and the offset into it; -1
 * when no bank is mapped there. */
static int bank_of(int area, u32 address, u32 *offset)
{
    const KhdaysVramPages *pages = khdays_vram_pages();
    const u8 *at;
    if (address / KHDAYS_VRAM_PAGE >= KHDAYS_AREA_MAX_PAGES) {
        return -1;
    }
    at = pages->page[area][address / KHDAYS_VRAM_PAGE];
    if (at == NULL) {
        return -1;
    }
    at += address % KHDAYS_VRAM_PAGE;
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        const u8 *base = khdays_vram_bank(bank);
        if (at >= base && at < base + khdays_vram_bank_size[bank]) {
            *offset = (u32)(at - base);
            return bank;
        }
    }
    return -1;
}

/* The texture's name, from its TEXIMAGE_PARAM and PLTT_BASE; 0 if unknown.
 * `banks` gets the banks it was looked up in. */
static int name_of(u32 param, u32 pltt, char *name, int size, int banks[2])
{
    const int format = (int)((param >> 26) & 7);
    const char *file, *pltt_file = NULL;
    u32 offset, file_offset, run, pltt_offset = 0;
    const int bank = bank_of(KHDAYS_AREA_TEX, (param & 0xffff) * 8u, &offset);
    banks[0] = bank;
    banks[1] = -1;
    if (bank < 0 || !khdays_origin_of_bank(bank, offset, &file, &file_offset, &run)) {
        return 0;
    }
    if (format != 7) {
        u32 pal_offset;
        const int pal_bank = bank_of(KHDAYS_AREA_TEXPAL, pltt * (format == 2 ? 8u : 16u), &pal_offset);
        banks[1] = pal_bank;
        if (pal_bank >= 0 && !khdays_origin_of_bank(pal_bank, pal_offset, &pltt_file, &pltt_offset, &run)) {
            pltt_file = NULL;
        }
    }
    return khdays_names_texture(file, file_offset, format, pltt_file, pltt_offset, name, size);
}

/* --- Seen once ------------------------------------------------------------------ */

typedef struct Seen {
    u32 param, pltt; /* the texture's place, size and format; its palette's place */
    int named;       /* 1: named, 0: no name found */
    int banks[2];    /* the banks it was looked up in (-1: none) */
    u32 changes[2];  /* and their change counts then */
    int hd;          /* whether replacements were looked for */
    int replacement; /* the texture drawn instead, or -1 */
} Seen;

/* Whether what `s` was looked up in has changed since. */
static int stale(const Seen *s)
{
    for (int k = 0; k < 2; ++k) {
        if (s->banks[k] >= 0 && khdays_origin_bank_changes(s->banks[k]) != s->changes[k])
            return 1;
    }
    return s->hd != hd_on();
}

static Seen *seen;
static int nseen, cap_seen;

/* A hash table to find `seen` by texture and palette: open addressing, each
 * slot holding an index + 1 (0 is empty). It doubles, and is filled again,
 * before it is half full, so it holds as many textures as are seen. */
static int *table;
static unsigned table_size; /* a power of two */

static unsigned slot_of(u32 param, u32 pltt)
{
    return (param * 2654435761u ^ pltt * 40503u) & (table_size - 1);
}

static Seen *seen_find(u32 param, u32 pltt)
{
    if (table == NULL)
        return NULL;
    for (unsigned i = slot_of(param, pltt), n = 0; n < table_size; i = (i + 1) & (table_size - 1), ++n) {
        const int k = table[i];
        if (k == 0)
            return NULL;
        if (seen[k - 1].param == param && seen[k - 1].pltt == pltt)
            return &seen[k - 1];
    }
    return NULL;
}

/* Puts `seen` entry `k` (its index + 1) into the table. */
static void table_put(int k)
{
    unsigned i = slot_of(seen[k - 1].param, seen[k - 1].pltt);
    while (table[i] != 0)
        i = (i + 1) & (table_size - 1);
    table[i] = k;
}

/* Doubles the table and puts every entry in again. 0 if there is no memory. */
static int table_grow(void)
{
    const unsigned size = table_size ? table_size * 2 : 1024;
    int *grown = (int *)calloc(size, sizeof(int));
    if (grown == NULL)
        return 0;
    free(table);
    table = grown;
    table_size = size;
    for (int k = 1; k <= nseen; ++k)
        table_put(k);
    return 1;
}

/* A new `seen` entry for this texture and palette (its other fields are the
 * caller's to fill), or NULL if there is no memory. */
static Seen *seen_add(u32 param, u32 pltt)
{
    if (nseen == cap_seen) {
        const int n = cap_seen ? cap_seen * 2 : 256;
        Seen *grown = (Seen *)realloc(seen, sizeof(Seen) * (size_t)n);
        if (grown == NULL)
            return NULL;
        seen = grown;
        cap_seen = n;
    }
    if ((unsigned)(nseen + 1) * 2 > table_size && !table_grow())
        return NULL;
    seen[nseen].param = param;
    seen[nseen].pltt = pltt;
    ++nseen;
    table_put(nseen);
    return &seen[nseen - 1];
}

/* --- The replacements ------------------------------------------------------------- */

/* The replacements, as many as the mods have. They are kept in blocks that
 * never move, so the renderer's thread can read an entry while another one is
 * added: block k holds BLOCK << k entries, so the blocks hold more entries
 * than an int can count. Only the game's thread adds entries. */
#define BLOCK 256
typedef struct Replacement {
    KhdaysTextureReplacement texture;
    char *name;
    volatile LONG version; /* for one that changes, its version; else 0 */
    SRWLOCK lock;          /* for one that changes, guards its texels */
} Replacement;
static Replacement *volatile blocks[32];
static volatile LONG nreplacements;

/* Which block holds entry `id`. */
static unsigned long block_of(int id)
{
    unsigned long k;
    _BitScanReverse(&k, (unsigned long)id / BLOCK + 1);
    return k;
}

/* Entry `id` (which must exist). */
static Replacement *entry(int id)
{
    const unsigned long k = block_of(id);
    return &blocks[k][(unsigned)id - BLOCK * ((1u << k) - 1u)];
}

/* Entry `n` (the next one, not counted yet) with `name`, its block made if
 * needed. NULL if there is no memory. */
static Replacement *new_entry(int n, const char *name)
{
    const unsigned long k = block_of(n);
    Replacement *e;
    if (blocks[k] == NULL) {
        Replacement *block = (Replacement *)calloc((size_t)BLOCK << k, sizeof(Replacement));
        if (block == NULL)
            return NULL;
        blocks[k] = block;
    }
    e = entry(n);
    e->name = _strdup(name);
    return e->name != NULL ? e : NULL;
}

/* The index of the replacement named `name`, or -1. */
static int find_entry(const char *name, int n)
{
    for (int i = 0; i < n; ++i) {
        if (strcmp(entry(i)->name, name) == 0)
            return i;
    }
    return -1;
}

int khdays_textures_replacements(void)
{
    return (int)InterlockedCompareExchange(&nreplacements, 0, 0);
}

const char *khdays_textures_name(int id)
{
    return id >= 0 && id < (int)InterlockedCompareExchange(&nreplacements, 0, 0) ? entry(id)->name : NULL;
}

const KhdaysTextureReplacement *khdays_textures_replacement(int id)
{
    return id >= 0 && id < (int)InterlockedCompareExchange(&nreplacements, 0, 0) ? &entry(id)->texture : NULL;
}

/* The replacement a mod has for `name` (mods/<Mod>/graphics/<name>.png),
 * read once. -1 if there is none, or it cannot be read. */
static int replacement_for(const char *name, u32 param)
{
    char relative[200], path[MAX_PATH];
    const char *error = NULL;
    int width, height, ow, oh, n = (int)InterlockedCompareExchange(&nreplacements, 0, 0);
    u8 *pixels;
    Replacement *e;
    const int known = find_entry(name, n);
    if (known >= 0)
        return known;
    snprintf(relative, sizeof(relative), "graphics/%s.png", name);
    if (!khdays_mods_find(relative, path, sizeof(path))) {
        /* one for the texture whatever its palette: <file>/tex/<texture>.png */
        const char *at = strrchr(name, '@');
        if (at == NULL || strstr(name, "/tex/") == NULL) {
            return -1;
        }
        snprintf(relative, sizeof(relative), "graphics/%.*s.png", (int)(at - name), name);
        if (!khdays_mods_find(relative, path, sizeof(path))) {
            return -1;
        }
    }
    pixels = khdays_png_read(path, &width, &height, &error);
    if (pixels == NULL) {
        fprintf(stderr, "textures: %s cannot be read (%s): the game's own is drawn\n", path, error);
        return -1;
    }
    khdays_tex_size(param, &ow, &oh);
    if (width % ow != 0 || height % oh != 0 || width / ow != height / oh) {
        fprintf(stderr, "textures: %s is %dx%d, not a whole multiple of the original's %dx%d: stretched\n", path, width,
                height, ow, oh);
    }
    e = new_entry(n, name);
    if (e == NULL) {
        fprintf(stderr, "textures: no memory for %s: the game's own is drawn\n", path);
        khdays_png_free(pixels);
        return -1;
    }
    e->texture.width = width;
    e->texture.height = height;
    e->texture.rgba = (const uint32_t *)pixels; /* 8-bit RGBA, red first: little-endian words */
    InterlockedExchange(&nreplacements, n + 1);
    if (trace) {
        fprintf(stderr, "textures: %s drawn instead from %s (%dx%d)\n", name, path, width, height);
    }
    return n;
}

int khdays_textures_add(const char *name, int width, int height, const uint8_t *rgba)
{
    return khdays_textures_add_info(name, width, height, (const uint32_t *)rgba, 0);
}

/* A new entry, counted once whole: one that changes has its version and lock
 * before any other thread sees it (the card keeps no smaller sizes of those,
 * host/render/gpu_hd.c). */
static int add(const char *name, int width, int height, const uint32_t *texels, uint32_t info, int changing)
{
    const int n = (int)InterlockedCompareExchange(&nreplacements, 0, 0);
    const int known = find_entry(name, n);
    Replacement *e;
    if (known >= 0)
        return entry(known)->texture.rgba != NULL ? known : -1;
    e = new_entry(n, name);
    if (e == NULL) {
        fprintf(stderr, "textures: no memory for %s: it is not drawn\n", name);
        return -1;
    }
    e->texture.width = width;
    e->texture.height = height;
    e->texture.rgba = texels;
    e->texture.info = info;
    if (changing) {
        InitializeSRWLock(&e->lock);
        e->version = 1;
    }
    InterlockedExchange(&nreplacements, n + 1);
    return n;
}

int khdays_textures_add_info(const char *name, int width, int height, const uint32_t *texels, uint32_t info)
{
    return add(name, width, height, texels, info, 0);
}

int khdays_textures_add_changing(const char *name, int width, int height, uint32_t info)
{
    uint32_t *texels = (uint32_t *)calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    int id;
    if (texels == NULL) {
        return -1;
    }
    id = add(name, width, height, texels, info, 1);
    if (id < 0 || entry(id)->texture.rgba != texels) {
        free(texels);
    }
    return id;
}

uint32_t *khdays_textures_change_begin(int id)
{
    AcquireSRWLockExclusive(&entry(id)->lock);
    return (uint32_t *)entry(id)->texture.rgba;
}

void khdays_textures_change_end(int id)
{
    InterlockedIncrement(&entry(id)->version);
    ReleaseSRWLockExclusive(&entry(id)->lock);
}

uint32_t khdays_textures_version(int id)
{
    return id >= 0 && id < (int)InterlockedCompareExchange(&nreplacements, 0, 0) ? (uint32_t)entry(id)->version : 0u;
}

void khdays_textures_read_begin(int id)
{
    if (entry(id)->version != 0) {
        AcquireSRWLockShared(&entry(id)->lock);
    }
}

void khdays_textures_read_end(int id)
{
    if (entry(id)->version != 0) {
        ReleaseSRWLockShared(&entry(id)->lock);
    }
}

/* --- The dump ------------------------------------------------------------------- */

static char (*dumped)[160];
static int ndumped, cap_dumped;

static int dumped_already(const char *name)
{
    for (int i = 0; i < ndumped; ++i) {
        if (strcmp(dumped[i], name) == 0)
            return 1;
    }
    if (ndumped == cap_dumped) {
        const int n = cap_dumped ? cap_dumped * 2 : 256;
        char(*grown)[160] = (char(*)[160])realloc(dumped, sizeof(dumped[0]) * (size_t)n);
        if (grown == NULL)
            return 1;
        dumped = grown;
        cap_dumped = n;
    }
    snprintf(dumped[ndumped++], sizeof(dumped[0]), "%s", name);
    return 0;
}

/* Creates every directory on the way to `path`. */
static void make_directories(const char *path)
{
    char part[MAX_PATH];
    for (int i = 0; path[i] != '\0' && i < MAX_PATH - 1; ++i) {
        if ((path[i] == '/' || path[i] == '\\') && i > 0) {
            memcpy(part, path, (size_t)i);
            part[i] = '\0';
            CreateDirectoryA(part, NULL);
        }
    }
}

/* Decodes a texture as the port draws it, from `pages`: the DS's 5-bit
 * colours and alpha 0-31, widened to 8 bits. 0 if there is no memory. */
static int decode_rgba(const KhdaysVramPages *pages, u32 param, u32 pltt, u8 *rgba)
{
    int width, height;
    u32 *texels;
    khdays_tex_size(param, &width, &height);
    texels = (u32 *)malloc(sizeof(u32) * (size_t)width * (size_t)height);
    if (texels == NULL) {
        return 0;
    }
    khdays_tex_decode(pages, param, pltt, texels);
    for (int i = 0; i < width * height; ++i) {
        const u32 v = texels[i];
        const u32 r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31, a = (v >> 16) & 31;
        rgba[4 * i] = (u8)(r << 3 | r >> 2);
        rgba[4 * i + 1] = (u8)(g << 3 | g >> 2);
        rgba[4 * i + 2] = (u8)(b << 3 | b >> 2);
        rgba[4 * i + 3] = (u8)(a * 255 / 31);
    }
    free(texels);
    return 1;
}

int khdays_textures_write_png(const char *path, const uint8_t *rgba, int width, int height)
{
    const char *error;
    make_directories(path);
    error = khdays_png_write(path, rgba, width, height);
    if (error != NULL) {
        fprintf(stderr, "textures: cannot write %s: %s\n", path, error);
        return 0;
    }
    return 1;
}

int khdays_textures_write_indexed_png(const char *path, const uint8_t *indices, int width, int height,
                                      const uint8_t *palette, int colours)
{
    const char *error;
    make_directories(path);
    error = khdays_png_write_indexed(path, indices, width, height, palette, colours, 1);
    if (error != NULL) {
        fprintf(stderr, "textures: cannot write %s: %s\n", path, error);
        return 0;
    }
    return 1;
}

void khdays_textures_decode(uint32_t param, const uint8_t *texels, uint32_t texel_bytes, const uint8_t *index4x4,
                            uint32_t index_bytes, const uint8_t *palette, uint32_t palette_bytes, uint8_t *rgba)
{
    /* the texture alone at the start of slot 0 (a 4x4 one's indices at slot
     * 1's, where the hardware looks for them), its palette at 0 */
    static u8 tex[KHDAYS_AREA_MAX_PAGES * KHDAYS_VRAM_PAGE], pal[6 * KHDAYS_VRAM_PAGE];
    KhdaysVramPages pages;
    memset(&pages, 0, sizeof(pages));
    memset(tex, 0, sizeof(tex));
    memset(pal, 0, sizeof(pal));
    memcpy(tex, texels, texel_bytes < 0x20000u ? texel_bytes : 0x20000u);
    if (index4x4 != NULL) {
        memcpy(tex + 0x20000, index4x4, index_bytes < 0x10000u ? index_bytes : 0x10000u);
    }
    if (palette != NULL) {
        memcpy(pal, palette, palette_bytes < sizeof(pal) ? palette_bytes : sizeof(pal));
    }
    for (int i = 0; i < KHDAYS_AREA_MAX_PAGES; ++i) {
        pages.page[KHDAYS_AREA_TEX][i] = tex + (size_t)i * KHDAYS_VRAM_PAGE;
    }
    for (int i = 0; i < 6; ++i) {
        pages.page[KHDAYS_AREA_TEXPAL][i] = pal + (size_t)i * KHDAYS_VRAM_PAGE;
    }
    decode_rgba(&pages, param & ~0xffffu, 0, rgba);
}

static void dump(const char *name, u32 param, u32 pltt)
{
    char path[MAX_PATH];
    int width, height;
    u8 *rgba;
    if (dumped_already(name)) {
        return;
    }
    khdays_tex_size(param, &width, &height);
    rgba = (u8 *)malloc((size_t)width * (size_t)height * 4);
    if (rgba == NULL || !decode_rgba(khdays_vram_pages(), param, pltt, rgba)) {
        free(rgba);
        return;
    }
    snprintf(path, sizeof(path), "%s/%s.png", dump_dir(), name);
    khdays_textures_write_png(path, rgba, width, height);
    free(rgba);
}

/* --- Each list ------------------------------------------------------------------ */

void khdays_textures_shown(const KhdaysG3dList *list)
{
    if (!khdays_textures_wanted()) {
        return;
    }
    for (int i = 0; i < list->npolys; ++i) {
        const KhdaysG3dPolygon *p = &list->polys[i];
        /* the place, size, format and colour-0 bit, but not how it repeats or flips */
        const u32 param = p->tex & 0x3ff0ffffu;
        const u32 format = (param >> 26) & 7;
        const u32 pltt = format == 7 || format == 0 ? 0 : p->pltt;
        char name[160];
        Seen *s;
        if (format == 0 || (p->flags & KHDAYS_G3D_MOD)) {
            continue; /* untextured, or a mod model's own texture */
        }
        s = seen_find(param, pltt);
        if (s != NULL && !stale(s)) {
            continue;
        }
        if (s == NULL && (s = seen_add(param, pltt)) == NULL) {
            return;
        }
        s->named = name_of(param, pltt, name, sizeof(name), s->banks);
        s->hd = hd_on();
        s->replacement = s->named && s->hd ? replacement_for(name, param) : -1;
        if (trace) {
            if (s->named) {
                fprintf(stderr, "textures: %s (param %08x pltt %04x)\n", name, param, pltt);
            } else {
                const char *file = "no file";
                u32 offset, file_offset = 0, run;
                const int bank = bank_of(KHDAYS_AREA_TEX, (param & 0xffff) * 8u, &offset);
                if (bank >= 0)
                    khdays_origin_of_bank(bank, offset, &file, &file_offset, &run);
                fprintf(stderr, "textures: no name for param %08x pltt %04x (bank %d, %s+0x%x)\n", param, pltt, bank,
                        file, file_offset);
            }
        }
        for (int k = 0; k < 2; ++k) {
            s->changes[k] = s->banks[k] >= 0 ? khdays_origin_bank_changes(s->banks[k]) : 0;
        }
        if (s->named && dump_dir()[0] != '\0') {
            dump(name, param, pltt);
        }
    }
}

static void mark(KhdaysG3dPolygon *polys, int32_t n)
{
    for (int i = 0; i < n; ++i) {
        KhdaysG3dPolygon *p = &polys[i];
        const u32 param = p->tex & 0x3ff0ffffu;
        const u32 format = (param >> 26) & 7;
        const Seen *s;
        if (p->flags & KHDAYS_G3D_MOD) {
            continue; /* a mod model's polygon: its texture is already chosen */
        }
        s = format == 0 || nseen == 0 ? NULL : seen_find(param, format == 7 ? 0 : p->pltt);
        p->replacement = s != NULL ? s->replacement : -1;
    }
}

void khdays_textures_mark(KhdaysG3dList *list)
{
    mark(list->polys, list->npolys);
    mark(list->pre, list->npre); /* the polygons before clipping, for the host's pictures between lists */
}
