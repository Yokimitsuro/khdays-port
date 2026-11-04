/* The 3D's texture memory past the DS's (see texture_banks.h). A bank is the file's own: its places come
 * from the game's VRAM managers reset for it (the DS's states kept and given back), its bytes are copied
 * as the game's Tex_LoadVram and Gfx_UploadBlock would upload them, and it is dropped when another file is
 * set up where the file's model was: the game keeps a file only up to its texture images once they are in
 * VRAM (ModelArchive_DropTextureImage) and puts other files after that. The DS's frame managers free
 * nothing on their own (NNS_GfdFreeFrmTexVram), so the game's frees of a bank's places touch nothing. */
#include "texture_banks.h"
#include "../core/origin.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../mods/names.h"
#include "../mods/textures.h"
#include "../../gpu/texdecode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The NitroSystem frame managers' states (EntityMgr_PushVramState keeps them in slots of these sizes). */
#define TEX_STATE_BYTES 0x28
#define PLTT_STATE_BYTES 8

/* The game's functions: a state is a reference into the game's memory (the game passes its stack's). */
void NNS_GfdResetFrmTexVramState(void);
void NNS_GfdGetFrmTexVramState(KhdaysRef state);
void NNS_GfdSetFrmTexVramState(KhdaysRef state);
void NNS_GfdResetFrmPlttVramState(void);
void GFXi_SaveStateTo(KhdaysRef state); /* the decomp's name of the palette manager's NNS_GfdGetFrmPlttVramState */
void NNS_GfdSetFrmPlttVramState(KhdaysRef state);

/* A copy into a bank: where in its texture (or palette) memory, how many bytes, and the game file they
 * came from (origin.h; NULL when not followed). */
typedef struct Upload {
    u32 at, size;
    char *file;
    u32 file_offset;
} Upload;

/* A texture of the bank already drawn: its place, size and format (TEXIMAGE_PARAM without how it repeats
 * or flips), its palette, and the replacement drawn for it. */
typedef struct Known {
    u32 param, pltt;
    int32_t replacement;
} Known;

typedef struct Bank {
    int live;
    KhdaysRef start, end; /* the file it belongs to, in the game's memory, up to its texture images */
    int drawn;            /* said in the trace */
    u8 *tex, *pltt;       /* its texture and palette memory */
    KhdaysVramPages pages;
    Upload uploads[4]; /* texels, 4x4 texels, 4x4 palette indices (texture memory); palettes */
    Known *known;
    int nknown, known_room, last;
} Bank;

static Bank *banks; /* bank n is banks[n - 1] */
static int nbanks, banks_room, live;

static u8 ds_tex_state[TEX_STATE_BYTES], ds_pltt_state[PLTT_STATE_BYTES];

/* The DS's managers' states kept, and given back: through the game's stack (this runs on a game thread),
 * in its memory as the game's functions want them. */
static void keep_states(void)
{
    u8 tex[TEX_STATE_BYTES], pltt[PLTT_STATE_BYTES];
    NNS_GfdGetFrmTexVramState(KHDAYS_REF(tex));
    GFXi_SaveStateTo(KHDAYS_REF(pltt));
    memcpy(ds_tex_state, tex, sizeof(tex));
    memcpy(ds_pltt_state, pltt, sizeof(pltt));
}

static void give_states_back(void)
{
    u8 tex[TEX_STATE_BYTES], pltt[PLTT_STATE_BYTES];
    memcpy(tex, ds_tex_state, sizeof(tex));
    memcpy(pltt, ds_pltt_state, sizeof(pltt));
    NNS_GfdSetFrmTexVramState(KHDAYS_REF(tex));
    NNS_GfdSetFrmPlttVramState(KHDAYS_REF(pltt));
}

static int32_t current, *outer; /* the bank in effect, and the draws' it interrupted */
static int nouter, outer_room;

/* KHDAYS_TRACE_TEXTURE_BANKS=1: the banks dropped and the first draw from each, in the session log. */
static int trace = -1;

static int tracing(void)
{
    if (trace < 0) {
        const char *v = getenv("KHDAYS_TRACE_TEXTURE_BANKS");
        trace = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    return trace;
}

static u32 rd32(const u8 *at)
{
    u32 v;
    memcpy(&v, at, 4);
    return v;
}

static u32 rd16(const u8 *at)
{
    return (u32)at[0] | (u32)at[1] << 8;
}

static void drop(Bank *b)
{
    for (int i = 0; i < 4; ++i) {
#pragma warning(suppress : 6001) /* every slot is zeroed when the table grows and when it is dropped */
        free(b->uploads[i].file);
    }
    free(b->tex);
    free(b->pltt);
    free(b->known);
    memset(b, 0, sizeof(*b));
    --live;
}

void khdays_texture_banks_file(void *file)
{
    const KhdaysRef start = KHDAYS_REF(file);
    const u32 size = rd32((const u8 *)file + 8); /* the Nitro header's file size */
    const KhdaysRef end = start + (size > 16 ? size : 16);
    for (int i = 0; live > 0 && i < nbanks; ++i) {
        if (banks[i].live && banks[i].start < end && start < banks[i].end) {
            if (tracing()) {
                khdays_log("texture memory: bank %d dropped (a file set up at %#x where its model was)", i + 1, start);
            }
            drop(&banks[i]);
        }
    }
    keep_states();
}

int khdays_texture_banks_open(void)
{
    NNS_GfdResetFrmTexVramState();
    NNS_GfdResetFrmPlttVramState();
    return 1;
}

/* A bank's slot: a free one, or a new one at the end. NULL without memory. */
static Bank *new_bank(int *number)
{
    for (int i = 0; i < nbanks; ++i) {
        if (!banks[i].live) {
            *number = i + 1;
            return &banks[i];
        }
    }
    if (nbanks == banks_room) {
        const int room = banks_room ? banks_room * 2 : 8;
        Bank *grown = (Bank *)realloc(banks, sizeof(Bank) * (size_t)room);
        if (grown == NULL) {
            return NULL;
        }
        memset(grown + banks_room, 0, sizeof(Bank) * (size_t)(room - banks_room));
        banks = grown;
        banks_room = room;
    }
    *number = ++nbanks;
    return &banks[nbanks - 1];
}

/* Memory for `bytes` of an area, in whole pages, its pages in `pages` (`limit` the area's addresses). */
static u8 *area_memory(u32 bytes, u32 limit, u8 **pages)
{
    const u32 npages = (bytes + KHDAYS_VRAM_PAGE - 1) / KHDAYS_VRAM_PAGE;
    u8 *memory;
    if (bytes == 0 || bytes > limit) {
        return NULL;
    }
    memory = (u8 *)calloc(npages, KHDAYS_VRAM_PAGE);
    for (u32 i = 0; memory != NULL && i < npages; ++i) {
        pages[i] = memory + (size_t)i * KHDAYS_VRAM_PAGE;
    }
    return memory;
}

/* `size` bytes from `source` to `at` of `memory`, followed to the game file they came from. */
static void upload(Upload *u, u8 *memory, u32 at, const u8 *source, u32 size)
{
    const char *file;
    u32 file_offset, run;
    memcpy(memory + at, source, size);
    u->at = at;
    u->size = size;
    if (khdays_origin_on() && khdays_origin_of_ram(KHDAYS_REF(source), &file, &file_offset, &run)) {
        u->file = _strdup(file);
        u->file_offset = file_offset;
    }
}

void khdays_texture_banks_close(void *file, void *tex, int allocated)
{
    u8 *t = (u8 *)tex;
    /* Tex_LoadVram's texels and 4x4 texels, Gfx_UploadBlock's palettes: place, size, data */
    const u32 tex_at = (rd32(t + 0x08) & 0xffff) << 3, tex_size = rd16(t + 0x0c) << 3;
    const u32 c4_at = (rd32(t + 0x18) & 0xffff) << 3, c4_size = rd16(t + 0x1c) << 3;
    const u32 index_at = ((c4_at & 0x1ffff) >> 1) + 0x20000 + ((c4_at & 0x40000) >> 2);
    const u32 pltt_at = (rd32(t + 0x2c) & 0xffff) << 3, pltt_size = rd16(t + 0x30) << 3;
    u32 tex_end = 0, pltt_end = pltt_size ? pltt_at + pltt_size : 0;
    Bank *b = NULL;
    int number = 0;
    if (tex_size && tex_at + tex_size > tex_end)
        tex_end = tex_at + tex_size;
    if (c4_size && c4_at + c4_size > tex_end)
        tex_end = c4_at + c4_size;
    if (c4_size && index_at + c4_size / 2 > tex_end)
        tex_end = index_at + c4_size / 2;
    if (allocated && (b = new_bank(&number)) == NULL) {
        allocated = 0;
    }
    if (allocated) {
        b->tex = area_memory(tex_end, 0x80000, b->pages.page[KHDAYS_AREA_TEX]);
        b->pltt = area_memory(pltt_end, 0x18000, b->pages.page[KHDAYS_AREA_TEXPAL]);
        if ((tex_end != 0 && b->tex == NULL) || (pltt_end != 0 && b->pltt == NULL)) {
            free(b->tex);
            free(b->pltt);
            memset(b, 0, sizeof(*b));
            allocated = 0;
        } else {
            b->live = 1;
            ++live;
            b->start = KHDAYS_REF(file);
            b->end = KHDAYS_REF(t) + rd32(t + 0x14); /* texInfo.ofsTex: where the images start */
            if (b->end <= b->start) {
                b->end = KHDAYS_REF(t);
            }
            if (tex_size) {
                upload(&b->uploads[0], b->tex, tex_at, t + rd32(t + 0x14), tex_size);
                *(u16 *)(t + 0x10) |= 1;
            }
            if (c4_size) {
                upload(&b->uploads[1], b->tex, c4_at, t + rd32(t + 0x24), c4_size);
                upload(&b->uploads[2], b->tex, index_at, t + rd32(t + 0x28), c4_size / 2);
                *(u16 *)(t + 0x20) |= 1;
            }
            if (pltt_size) {
                upload(&b->uploads[3], b->pltt, pltt_at, t + rd32(t + 0x38), pltt_size);
            }
            *(u16 *)(t + 0x32) |= 1;
            khdays_log("texture memory: %s's textures (%u bytes, %u in 4x4, palettes %u) past the DS's VRAM: "
                       "bank %d",
                       b->uploads[0].file   ? b->uploads[0].file
                       : b->uploads[1].file ? b->uploads[1].file
                                            : "a file",
                       tex_size, c4_size, pltt_size, number);
        }
    }
    if (!allocated) {
        khdays_log("texture memory: a file's textures (%u bytes, %u in 4x4, palettes %u) have no place: untextured",
                   tex_size, c4_size, pltt_size);
    }
    give_states_back();
}

/* The live bank holding the game's memory at `at` (0: none). */
static int32_t bank_at(KhdaysRef at)
{
    for (int i = 0; live > 0 && i < nbanks; ++i) {
        if (banks[i].live && at >= banks[i].start && at < banks[i].end) {
            return i + 1;
        }
    }
    return 0;
}

uint32_t khdays_texture_banks_tag_pltt(const void *file, uint32_t pltt)
{
    const int32_t bank = file != NULL ? bank_at(KHDAYS_REF(file)) : 0;
    if (bank == 0) {
        return pltt;
    }
    if (!banks[bank - 1].drawn && tracing()) {
        khdays_log("texture memory: bank %d's first texture set apart (file %#x)", bank, KHDAYS_REF(file));
    }
    banks[bank - 1].drawn = 1;
    return (pltt & 0x1fffu) | KHDAYS_G3D_PLTT_BANK_TAG | ((uint32_t)bank << 16);
}

void khdays_texture_banks_draw_begin(const void *model)
{
    int32_t bank = 0;
    if (nouter == outer_room) {
        const int room = outer_room ? outer_room * 2 : 8;
        int32_t *grown = (int32_t *)realloc(outer, sizeof(int32_t) * (size_t)room);
        if (grown != NULL) {
            outer = grown;
            outer_room = room;
        }
    }
    if (nouter < outer_room) {
        outer[nouter] = current;
    }
    ++nouter;
    if (model != NULL && (bank = bank_at(KHDAYS_REF(model))) != 0) {
        if (!banks[bank - 1].drawn && tracing()) {
            khdays_log("texture memory: bank %d drawn (model %#x)", bank, KHDAYS_REF(model));
        }
        banks[bank - 1].drawn = 1;
    }
    current = bank;
    khdays_g3d_texture_bank(bank);
}

void khdays_texture_banks_draw_end(void)
{
    if (nouter > 0) {
        --nouter;
        current = nouter < outer_room ? outer[nouter] : 0;
    }
    khdays_g3d_texture_bank(current);
}

/* Where the bank's byte at `at` of its texture (or `palette`) memory came from. */
static int origin(const Bank *b, int palette, u32 at, const char **file, u32 *offset)
{
    for (int i = palette ? 3 : 0; i < (palette ? 4 : 3); ++i) {
        const Upload *u = &b->uploads[i];
        if (u->file != NULL && at >= u->at && at - u->at < u->size) {
            *file = u->file;
            *offset = u->file_offset + (at - u->at);
            return 1;
        }
    }
    return 0;
}

/* A mod's texture for the bank's texture, by the name the game's would have (textures.c name_of). */
static int32_t mod_texture(const Bank *b, u32 param, u32 pltt)
{
    const int format = (int)((param >> 26) & 7);
    const char *file, *pltt_file = NULL;
    u32 offset, pltt_offset = 0;
    char name[160];
    if (!origin(b, 0, (param & 0xffff) * 8u, &file, &offset)) {
        return -1;
    }
    if (format != 7 && !origin(b, 1, pltt * (format == 2 ? 8u : 16u), &pltt_file, &pltt_offset)) {
        pltt_file = NULL;
    }
    if (!khdays_names_texture(file, offset, format, pltt_file, pltt_offset, name, sizeof(name))) {
        return -1;
    }
    return khdays_textures_mod_replacement(name, param);
}

/* The bank's texture, decoded, as a replacement named by what it shows (the same texture in any bank, or
 * loaded again, is one). */
static int32_t decoded(const Bank *b, u32 param, u32 pltt)
{
    uint32_t *rgba = khdays_textures_decode_pages(&b->pages, param, pltt);
    int width, height;
    unsigned long long hash = 14695981039346656037ull;
    char name[64];
    int32_t id;
    if (rgba == NULL) {
        return -1;
    }
    khdays_tex_size(param, &width, &height);
    for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
        hash = (hash ^ rgba[i]) * 1099511628211ull;
    }
    snprintf(name, sizeof(name), "texture bank/%016llx-%dx%d", hash, width, height);
    id = khdays_textures_add_info(name, width, height, rgba, 0);
    if (id < 0 || khdays_textures_replacement(id)->rgba != rgba) {
        free(rgba); /* none, or the same texture already there */
    }
    return id;
}

int32_t khdays_texture_banks_replacement(int32_t bank, uint32_t param, uint32_t pltt)
{
    Bank *b = NULL;
    int32_t id = -1;
    param &= 0x3ff0ffffu; /* the place, size, format and colour-0 bit: the polygon says how it repeats */
    if (((param >> 26) & 7) == 7) {
        pltt = 0;
    }
    if (bank < 1 || bank > nbanks || !banks[bank - 1].live || ((param >> 26) & 7) == 0) {
        return -1;
    }
    b = &banks[bank - 1];
    if (b->last < b->nknown && b->known[b->last].param == param && b->known[b->last].pltt == pltt) {
        return b->known[b->last].replacement;
    }
    for (int i = 0; i < b->nknown; ++i) {
        if (b->known[i].param == param && b->known[i].pltt == pltt) {
            b->last = i;
            return b->known[i].replacement;
        }
    }
    if (khdays_textures_hd()) {
        id = mod_texture(b, param, pltt);
    }
    if (id < 0) {
        id = decoded(b, param, pltt);
    }
    if (b->nknown == b->known_room) {
        const int room = b->known_room ? b->known_room * 2 : 16;
        Known *grown = (Known *)realloc(b->known, sizeof(Known) * (size_t)room);
        if (grown == NULL) {
            return id;
        }
        b->known = grown;
        b->known_room = room;
    }
    b->known[b->nknown].param = param;
    b->known[b->nknown].pltt = pltt;
    b->known[b->nknown].replacement = id;
    b->last = b->nknown++;
    return id;
}
