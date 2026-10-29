/* The port's elements (see elements.h). */
#include "elements.h"
#include "layout.h"
#include "parts.h"
#include "../host/app/host.h"
#include "../runtime/core/io.h"
#include "../runtime/mods/textures.h"
#include "../runtime/core/vram.h"
#include "../runtime/core/window.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Images ------------------------------------------------------------------------ */

/* Each image is a changing replacement (runtime/mods/textures.h) that the
 * graphics card holds. One that is given back is kept for the next image of
 * its size. */
typedef struct Image {
    int replacement;
    int width, height;
    int used;
    uint32_t version; /* counts its changes (khdays_ui_image_end) */
    /* for an element drawn on the other screen: its texels in direct colour,
     * from its own engine's palettes (made again when it or they changed) */
    int direct; /* that image, -1: none yet */
    uint32_t direct_version, direct_palettes;
} Image;

static Image *images;
static int image_count, image_room;

int khdays_ui_image_get(int width, int height)
{
    char name[64];
    int id;
    if (width <= 0 || height <= 0) {
        return -1;
    }
    for (int i = 0; i < image_count; ++i) {
        if (!images[i].used && images[i].width == width && images[i].height == height) {
            uint32_t *texels = khdays_textures_change_begin(images[i].replacement);
            memset(texels, 0, (size_t)width * (size_t)height * sizeof(uint32_t));
            khdays_textures_change_end(images[i].replacement);
            images[i].used = 1;
            return i;
        }
    }
    if (image_count == image_room) {
        const int room = image_room ? image_room * 2 : 32;
        Image *grown = (Image *)realloc(images, (size_t)room * sizeof(Image));
        if (grown == NULL) {
            return -1;
        }
        images = grown;
        image_room = room;
    }
    snprintf(name, sizeof(name), "ui:image:%d", image_count);
    id = khdays_textures_add_changing(name, width, height, 0);
    if (id < 0) {
        return -1;
    }
    images[image_count].replacement = id;
    images[image_count].width = width;
    images[image_count].height = height;
    images[image_count].used = 1;
    images[image_count].version = 0;
    images[image_count].direct = -1;
    return image_count++;
}

void khdays_ui_image_put(int image)
{
    if (image >= 0 && image < image_count) {
        images[image].used = 0;
    }
}

uint32_t *khdays_ui_image_begin(int image)
{
    return image >= 0 && image < image_count ? khdays_textures_change_begin(images[image].replacement) : NULL;
}

void khdays_ui_image_end(int image)
{
    if (image >= 0 && image < image_count) {
        khdays_textures_change_end(images[image].replacement);
        ++images[image].version;
    }
}

int khdays_ui_scale(void)
{
    const int n = khdays_host_frame_scale();
    return n < 1 ? 1 : n > 8 ? 8 : n;
}

const uint32_t *khdays_ui_texels(uint32_t image)
{
    const KhdaysTextureReplacement *r = khdays_textures_replacement((int)image);
    return r != NULL ? r->rgba : NULL;
}

int khdays_ui_image_replacement(int image)
{
    return image >= 0 && image < image_count ? images[image].replacement : -1;
}

/* --- This frame's elements ------------------------------------------------------- */

static void (*emitters[KHDAYS_UI_PARTS])(void);

typedef struct Entry {
    KhdaysUiElement e;
    KhdaysUiElement box;
    int part, order, seq;
    int source;           /* the engine it belongs to */
    float dx, dy, dw, dh; /* its DS place */
    uint32_t dslot;       /* its slot as given */
    int placed;           /* a layout line placed it */
    int leader;           /* the first element of its id the layout placed, which it moves with; -1: none */
    int follows;          /* explicit earlier anchor, even without a matching layout line */
} Entry;

static Entry *entries;
static int entry_count, entry_room;

/* per screen: the records of the parts that are on, and those of every part
 * that is not off; and each record's element id */
static uint32_t *records[2][2];
static const char **record_ids[2][2];
static uint32_t record_count[2][2], record_room[2][2];

/* what the parts stand for, left out of the game's drawing: per list (0 the
 * parts that are on, 1 every part that is not off) and per screen, one bit
 * per cell of the text layers' maps and one per sprite (gfx/elements.h) */
static uint8_t left_cells[2][2][KE_LEFT_CELL_WORDS * 4];
static uint8_t left_obj[2][2][KE_LEFT_OBJ_WORDS * 4];
static int left_any_cells[2][2], left_any_obj[2][2];

void khdays_ui_register(int part, void (*emit)(void))
{
    if (part >= 0 && part < KHDAYS_UI_PARTS) {
        emitters[part] = emit;
    }
}

static int changed; /* a part's own state changed since the display last asked (the game's thread) */

void khdays_ui_changed(void)
{
    changed = 1;
}

int khdays_ui_take_changed(void)
{
    const int was = changed;
    changed = 0;
    return was;
}

void khdays_ui_emit_extended(int part, const KhdaysUiElement *element, const KhdaysUiElement *box)
{
    if (part < 0 || part >= KHDAYS_UI_PARTS || element->screen < 0 || element->screen > 1 || element->image < 0 ||
        element->image >= image_count) {
        return;
    }
    if (entry_count == entry_room) {
        const int room = entry_room ? entry_room * 2 : 64;
        Entry *grown = (Entry *)realloc(entries, (size_t)room * sizeof(Entry));
        if (grown == NULL) {
            return;
        }
        entries = grown;
        entry_room = room;
    }
    entries[entry_count].e = *element;
    entries[entry_count].box = *box;
    entries[entry_count].part = part;
    entries[entry_count].order = 0;
    entries[entry_count].seq = entry_count;
    entries[entry_count].follows = -1;
    ++entry_count;
}

void khdays_ui_emit_following(int part, const KhdaysUiElement *element, const KhdaysUiElement *anchor)
{
    const int at = entry_count;
    khdays_ui_emit(part, element);
    if (entry_count == at || !anchor->id)
        return;
    for (int i = 0; i < at; ++i) {
        if (entries[i].part == part && entries[i].e.screen == anchor->screen && entries[i].e.id &&
            strcmp(entries[i].e.id, anchor->id) == 0 && entries[i].e.image == anchor->image &&
            entries[i].e.x == anchor->x && entries[i].e.y == anchor->y) {
            entries[at].follows = i;
            break;
        }
    }
}

void khdays_ui_emit(int part, const KhdaysUiElement *element)
{
    khdays_ui_emit_extended(part, element, element);
}

void khdays_ui_left_out_cell(int part, int screen, int layer, int cx, int cy)
{
    const int mode = khdays_ui_mode(part);
    uint32_t bit;
    if (mode == KHDAYS_UI_OFF || screen < 0 || screen > 1 || layer < 0 || layer > 3 || cx < 0 || cx > 63 || cy < 0 ||
        cy > 63) {
        return;
    }
    bit = (uint32_t)layer * KE_CELLS + (uint32_t)cy * 64u + (uint32_t)cx;
    for (int list = mode == KHDAYS_UI_ON ? 0 : 1; list < 2; ++list) {
        left_cells[list][screen][bit >> 3] |= (uint8_t)(1u << (bit & 7u));
        left_any_cells[list][screen] = 1;
    }
}

int khdays_ui_cell_taken(int screen, int layer, int cx, int cy)
{
    uint32_t bit;
    if (screen < 0 || screen > 1 || layer < 0 || layer > 3 || cx < 0 || cx > 63 || cy < 0 || cy > 63) {
        return 0;
    }
    bit = (uint32_t)layer * KE_CELLS + (uint32_t)cy * 64u + (uint32_t)cx;
    return (left_cells[1][screen][bit >> 3] >> (bit & 7u)) & 1u;
}

void khdays_ui_left_out_obj(int part, int screen, int index)
{
    const int mode = khdays_ui_mode(part);
    if (mode == KHDAYS_UI_OFF || screen < 0 || screen > 1 || index < 0 || index >= 128) {
        return;
    }
    for (int list = mode == KHDAYS_UI_ON ? 0 : 1; list < 2; ++list) {
        left_obj[list][screen][index >> 3] |= (uint8_t)(1u << (index & 7));
        left_any_obj[list][screen] = 1;
    }
}

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* topmost first: a higher order first, then the one given later */
static int above(const void *a, const void *b)
{
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    if (x->order != y->order) {
        return x->order > y->order ? -1 : 1;
    }
    return x->seq > y->seq ? -1 : x->seq < y->seq ? 1 : 0;
}

static void add_record(int screen, int list, const Entry *n)
{
    const Image *image = &images[n->e.image];
    uint32_t *r;
    if (record_count[screen][list] == record_room[screen][list]) {
        const uint32_t room = record_room[screen][list] ? record_room[screen][list] * 2 : 32;
        uint32_t *grown = (uint32_t *)realloc(records[screen][list], (size_t)room * KE_WORDS * sizeof(uint32_t));
        const char **grown_ids;
        if (grown == NULL) {
            return;
        }
        records[screen][list] = grown;
        grown_ids = (const char **)realloc((void *)record_ids[screen][list], (size_t)room * sizeof(*grown_ids));
        if (grown_ids == NULL) {
            return;
        }
        record_ids[screen][list] = grown_ids;
        record_room[screen][list] = room;
    }
    record_ids[screen][list][record_count[screen][list]] = n->e.id;
    r = records[screen][list] + (size_t)record_count[screen][list]++ * KE_WORDS;
    r[KE_SLOT] = n->e.slot;
    r[KE_X] = float_bits(n->e.x);
    r[KE_Y] = float_bits(n->e.y);
    r[KE_W] = float_bits(n->e.w);
    r[KE_H] = float_bits(n->e.h);
    r[KE_IMAGE] = (uint32_t)image->replacement;
    r[KE_IW] = (uint32_t)image->width;
    r[KE_IH] = (uint32_t)image->height;
}

/* --- An element drawn on the other screen ----------------------------------------- */

static uint32_t palettes_seen[2];

/* The engine's palettes this frame, as one number (an FNV-1a hash of its
 * standard palettes and its BG extended ones). */
static uint32_t palettes_of(int engine)
{
    const uint8_t *pal = (const uint8_t *)KHDAYS_W(0x05000000u + (uint32_t)engine * 0x400u);
    const KhdaysVramPages *pages = khdays_vram_pages();
    uint32_t h = 2166136261u;
    for (int i = 0; i < 0x400; ++i) {
        h = (h ^ pal[i]) * 16777619u;
    }
    for (int k = 0; k < 2; ++k) {
        const uint8_t *ext = pages->page[engine ? KHDAYS_AREA_BBGEXT : KHDAYS_AREA_ABGEXT][k];
        for (int i = 0; ext != NULL && i < (int)KHDAYS_VRAM_PAGE; ++i) {
            h = (h ^ ext[i]) * 16777619u;
        }
    }
    return h;
}

static uint32_t rgb_of(uint32_t bgr555)
{
    uint32_t out = 0xff000000u;
    for (int n = 0; n < 3; ++n) {
        const uint32_t c = (bgr555 >> (5 * n)) & 31u;
        out |= ((c << 3) | (c >> 2)) << (8 * n); /* as the composition widens a DS colour (compose.h kd_fine) */
    }
    return out;
}

/* The colour engine `engine` gives an indexed texel in slot `slot` (as
 * compose.h kd_element_layer and kd_element_sprites do). */
static uint32_t colour_of(int engine, uint32_t slot, uint32_t t)
{
    const uint8_t *pal = (const uint8_t *)KHDAYS_W(0x05000000u + (uint32_t)engine * 0x400u);
    if ((t & KET_OPAQUE) == 0u) {
        return 0u;
    }
    if (slot & KES_SPRITES) {
        const uint32_t i = 0x200u + (t & 0xffu) * 2u;
        return rgb_of((uint32_t)(pal[i] | pal[i + 1] << 8) & 0x7fffu);
    }
    if (t & KET_EXTENDED) {
        const uint32_t io = engine ? 0x1000u : 0u;
        const uint32_t dispcnt = IO32(io), bg = slot & KES_NUMBER_MASK;
        const uint32_t cnt = IO16(io + 0x08u + 2u * bg);
        if (dispcnt & (1u << 30)) {
            const uint32_t ext = (bg < 2u && (cnt & 0x2000u)) ? bg + 2u : bg; /* gfx/describe.c ext_slot */
            const uint32_t a = ext * 0x2000u + ((t >> KET_EXT_PAL_SHIFT) & 15u) * 512u + (t & 0xffu) * 2u;
            const uint8_t *page =
                khdays_vram_pages()->page[engine ? KHDAYS_AREA_BBGEXT : KHDAYS_AREA_ABGEXT][a / KHDAYS_VRAM_PAGE];
            return page != NULL
                       ? rgb_of((uint32_t)(page[a % KHDAYS_VRAM_PAGE] | page[a % KHDAYS_VRAM_PAGE + 1] << 8) & 0x7fffu)
                       : rgb_of(0);
        }
    }
    return rgb_of((uint32_t)(pal[(t & 0xffu) * 2u] | pal[(t & 0xffu) * 2u + 1u] << 8) & 0x7fffu);
}

uint32_t khdays_ui_palette_colour(int engine, uint32_t slot, uint32_t indexed)
{
    return colour_of(engine, slot, indexed);
}

uint32_t khdays_ui_palette_hash(int engine)
{
    /* Extended palette selectors can change colors without changing palette bytes. */
    const uint32_t io = engine ? 0x1000u : 0u;
    const uint32_t selectors =
        ((IO32(io) >> 30) & 1u) | ((IO16(io + 0x08u) & 0x2000u) >> 12) | ((IO16(io + 0x0au) & 0x2000u) >> 11);
    return (palettes_of(engine) ^ selectors) * 16777619u;
}

/* The image element `n` draws on the other screen: its own, in direct colour. */
uint32_t khdays_ui_element_rgba(const KhdaysUiElement *element, int x, int y)
{
    if (element == NULL || element->image < 0 || element->image >= image_count)
        return 0;
    const Image *im = &images[element->image];
    const KhdaysTextureReplacement *r = khdays_textures_replacement(im->replacement);
    if (r == NULL || r->rgba == NULL || x < 0 || y < 0 || x >= im->width || y >= im->height)
        return 0;
    const uint32_t t = r->rgba[(size_t)y * (size_t)im->width + (size_t)x];
    return (element->slot & KES_DIRECT) ? t : colour_of(element->screen, element->slot, t);
}

static int direct_image(const Entry *n)
{
    Image *im = &images[n->e.image];
    if (im->direct < 0) {
        const int direct = khdays_ui_image_get(im->width, im->height);
        im = &images[n->e.image]; /* the array may have moved: writing through the old `im` would hit freed memory */
        im->direct = direct;
        im->direct_version = im->version - 1u;
        if (direct < 0) {
            return -1;
        }
    }
    if (im->direct_version != im->version || im->direct_palettes != palettes_seen[n->source]) {
        const KhdaysTextureReplacement *r = khdays_textures_replacement(im->replacement);
        uint32_t *out = khdays_ui_image_begin(im->direct);
        if (r == NULL || r->rgba == NULL || out == NULL) {
            return -1;
        }
        uint32_t palette[256];
        for (unsigned i = 0; i < 256; ++i)
            palette[i] = colour_of(n->source, n->e.slot, KET_OPAQUE | i);
        for (size_t i = 0; i < (size_t)im->width * (size_t)im->height; ++i) {
            const uint32_t t = r->rgba[i];
            out[i] = !(t & KET_OPAQUE)    ? 0u
                     : (t & KET_EXTENDED) ? colour_of(n->source, n->e.slot, t)
                                          : palette[t & 255u];
        }
        khdays_ui_image_end(im->direct);
        im->direct_version = im->version;
        im->direct_palettes = palettes_seen[n->source];
    }
    return im->direct;
}

int khdays_ui_element_direct(const KhdaysUiElement *element)
{
    if (element->slot & KES_DIRECT)
        return khdays_ui_image_replacement(element->image);
    Entry n;
    memset(&n, 0, sizeof(n));
    n.e = *element;
    n.source = element->screen;
    palettes_seen[n.source] = palettes_of(n.source);
    return khdays_ui_image_replacement(direct_image(&n));
}

/* Widescreen: each element's group, as the game's pieces are made (gpu2d.h KhdaysGpu2dWide) -- the
 * elements of engine A no layout line places whose 8x8 cells touch one another's -- and the group's
 * DS columns. -1: in none (another engine's, or a line places it). */
static int *group_of;
static float (*group_x)[2];
static int group_room;

static int group_root(int i)
{
    while (group_of[i] != i) {
        group_of[i] = group_of[group_of[i]];
        i = group_of[i];
    }
    return i;
}

static int cells_touch(const KhdaysUiElement *a, const KhdaysUiElement *b)
{
    const int ax0 = (int)floorf(a->x / 8.0f), ax1 = (int)floorf((a->x + a->w - 1.0f) / 8.0f);
    const int ay0 = (int)floorf(a->y / 8.0f), ay1 = (int)floorf((a->y + a->h - 1.0f) / 8.0f);
    const int bx0 = (int)floorf(b->x / 8.0f), bx1 = (int)floorf((b->x + b->w - 1.0f) / 8.0f);
    const int by0 = (int)floorf(b->y / 8.0f), by1 = (int)floorf((b->y + b->h - 1.0f) / 8.0f);
    const int x_over = ax0 <= bx1 && bx0 <= ax1, y_over = ay0 <= by1 && by0 <= ay1;
    const int x_next = ax0 <= bx1 + 1 && bx0 <= ax1 + 1, y_next = ay0 <= by1 + 1 && by0 <= ay1 + 1;
    return (x_over && y_next) || (y_over && x_next); /* sharing a cell, or side by side */
}

/* Gives every element its group (group_of, group_x); 0 when there was no room for them. */
static int find_groups(void)
{
    if (group_room < entry_count) {
        int *g = (int *)realloc(group_of, (size_t)entry_count * sizeof(*group_of));
        float(*gx)[2] = g != NULL ? (float(*)[2])realloc(group_x, (size_t)entry_count * sizeof(*group_x)) : NULL;
        if (g != NULL) {
            group_of = g;
        }
        if (gx == NULL) {
            return 0;
        }
        group_x = gx;
        group_room = entry_count;
    }
    for (int i = 0; i < entry_count; ++i) {
        const Entry *n = &entries[i];
        const int alone = n->follows >= 0 || n->e.screen != 0 || n->e.w <= 0.0f || n->e.h <= 0.0f ||
                          khdays_ui_mode(n->part) != KHDAYS_UI_ON || khdays_ui_layout_names(n->e.id);
        group_of[i] = alone ? -1 : i;
    }
    for (int i = 0; i < entry_count; ++i) {
        for (int k = i + 1; k < entry_count && group_of[i] >= 0; ++k) {
            if (group_of[k] >= 0 && cells_touch(&entries[i].box, &entries[k].box)) {
                const int a = group_root(i), b = group_root(k);
                if (a != b) {
                    group_of[b] = a;
                }
            }
        }
    }
    for (int i = 0; i < entry_count; ++i) {
        group_x[i][0] = 1.0e9f;
        group_x[i][1] = -1.0e9f;
    }
    for (int i = 0; i < entry_count; ++i) {
        if (group_of[i] >= 0) {
            const int r = group_root(i);
            const float x0 = entries[i].box.x, x1 = entries[i].box.x + entries[i].box.w;
            group_x[r][0] = x0 < group_x[r][0] ? x0 : group_x[r][0];
            group_x[r][1] = x1 > group_x[r][1] ? x1 : group_x[r][1];
        }
    }
    return 1;
}

void khdays_ui_collect(int wide_margin, int upper)
{
    int grouped;
    static const char **ids;
    static int id_room;
    entry_count = 0;
    memset(record_count, 0, sizeof(record_count));
    for (int list = 0; list < 2; ++list) {
        for (int s = 0; s < 2; ++s) {
            if (left_any_cells[list][s])
                memset(left_cells[list][s], 0, sizeof(left_cells[list][s]));
        }
    }
    memset(left_obj, 0, sizeof(left_obj));
    memset(left_any_cells, 0, sizeof(left_any_cells));
    memset(left_any_obj, 0, sizeof(left_any_obj));
    for (int part = 0; part < KHDAYS_UI_PARTS; ++part) {
        if (emitters[part] != NULL && khdays_ui_mode(part) != KHDAYS_UI_OFF) {
            emitters[part]();
        }
    }
    /* the layout's sections apply while an element of their id is there */
    if (id_room < entry_count) {
        const char **grown = (const char **)realloc((void *)ids, (size_t)entry_count * sizeof(*ids));
        if (grown != NULL) {
            ids = grown;
            id_room = entry_count;
        }
    }
    for (int i = 0; i < entry_count && i < id_room; ++i) {
        ids[i] = entries[i].e.id;
    }
    khdays_ui_layout_frame(ids, entry_count < id_room ? entry_count : id_room);
    grouped = wide_margin > 0 && find_groups();
    for (int i = 0; i < entry_count; ++i) {
        Entry *n = &entries[i];
        n->seq = i;
        n->source = n->e.screen;
        n->dx = n->e.x, n->dy = n->e.y, n->dw = n->e.w, n->dh = n->e.h;
        n->dslot = n->e.slot;
        n->placed = 0;
        n->leader = n->follows;
        if (n->leader >= 0)
            continue;
        if (khdays_ui_mode(n->part) == KHDAYS_UI_ON) {
            /* if one with its id is placed already (a piece's other layers), it goes the same way */
            for (int k = 0; k < i && n->leader < 0; ++k) {
                if (entries[k].placed && entries[k].source == n->source && entries[k].e.id != NULL && n->e.id != NULL &&
                    strcmp(entries[k].e.id, n->e.id) == 0) {
                    n->leader = k;
                }
            }
            if (n->leader < 0) {
                /* where the layout puts it (the check compares at the DS's place) */
                const float *group = grouped && group_of[i] >= 0 ? group_x[group_root(i)] : NULL;
                float x = n->box.x, y = n->box.y, w = n->box.w, h = n->box.h;
                n->placed = khdays_ui_layout_place(n->e.id, &n->e.screen, upper, wide_margin, group, &x, &y, &w, &h,
                                                   &n->e.slot, &n->order);
                const float sx = n->box.w > 0 ? w / n->box.w : 1, sy = n->box.h > 0 ? h / n->box.h : 1;
                n->e.x = x + (n->e.x - n->box.x) * sx;
                n->e.y = y + (n->e.y - n->box.y) * sy;
                n->e.w *= sx;
                n->e.h *= sy;
            }
        }
    }
    /* an element no line places, lying in one a line places (the text in a
     * HUD piece), moves with it: with the smallest one of its own engine that
     * holds the middle of what the screen shows of it (a canvas can be wider
     * than the screen) */
    for (int i = 0; i < entry_count; ++i) {
        Entry *n = &entries[i];
        const Entry *parent = NULL;
        float mx, my;
        if (n->placed || khdays_ui_mode(n->part) != KHDAYS_UI_ON) {
            continue;
        }
        if (n->leader >= 0) {
            parent = &entries[n->leader];
            goto follow;
        }
        mx = ((n->dx > 0.0f ? n->dx : 0.0f) + (n->dx + n->dw < 256.0f ? n->dx + n->dw : 256.0f)) / 2.0f;
        my = ((n->dy > 0.0f ? n->dy : 0.0f) + (n->dy + n->dh < 192.0f ? n->dy + n->dh : 192.0f)) / 2.0f;
        for (int k = 0; k < entry_count; ++k) {
            const Entry *q = &entries[k];
            if (k == i || !q->placed || q->source != n->source || q->dw <= 0.0f || q->dh <= 0.0f || mx < q->dx ||
                my < q->dy || mx >= q->dx + q->dw || my >= q->dy + q->dh) {
                continue;
            }
            if (parent == NULL || q->dw * q->dh < parent->dw * parent->dh) {
                parent = q;
            }
        }
    follow:
        if (parent != NULL) {
            const float sx = parent->e.w / parent->dw, sy = parent->e.h / parent->dh;
            n->e.x = parent->e.x + (n->dx - parent->dx) * sx;
            n->e.y = parent->e.y + (n->dy - parent->dy) * sy;
            n->e.w = n->dw * sx;
            n->e.h = n->dh * sy;
            n->e.screen = parent->e.screen;
            n->order = parent->order;
            if (n->leader >= 0 &&
                (n->dslot & (KES_SPRITES | KES_NUMBER_MASK)) == (parent->dslot & (KES_SPRITES | KES_NUMBER_MASK))) {
                /* of its id and its layer (a piece's mods' tiles over it, ui/hud.c): its layer too */
                n->e.slot =
                    (n->e.slot & ~(KES_SPRITES | KES_NUMBER_MASK)) | (parent->e.slot & (KES_SPRITES | KES_NUMBER_MASK));
            }
        }
    }
    /* one drawn on the other screen in a slot that is the 3D there (engine A's
     * BG0, DISPCNT bit 3) goes to the BG layer drawn above the others, above
     * that layer's elements, as it was above them on its own screen */
    for (int i = 0; i < entry_count; ++i) {
        Entry *n = &entries[i];
        if (n->e.screen != n->source && n->e.screen == 0 && !(n->e.slot & KES_SPRITES) &&
            (n->e.slot & KES_NUMBER_MASK) == 0u && (IO32(0) & 0x08u)) {
            int best = 1;
            for (int b = 2; b < 4; ++b) {
                if ((IO16(0x08u + 2u * (uint32_t)b) & 3u) < (IO16(0x08u + 2u * (uint32_t)best) & 3u)) {
                    best = b;
                }
            }
            n->e.slot = (n->e.slot & ~KES_NUMBER_MASK) | (uint32_t)best;
            n->order += 1;
        }
    }
    for (int e = 0; e < 2; ++e) {
        int moved = 0;
        for (int i = 0; i < entry_count && !moved; ++i) {
            moved =
                entries[i].source == e &&
                (entries[i].e.screen != e || ((entries[i].e.slot & KES_PICTURE) && !(entries[i].e.slot & KES_DIRECT)));
        }
        if (moved) {
            palettes_seen[e] = palettes_of(e);
        }
    }
    qsort(entries, (size_t)entry_count, sizeof(Entry), above);
    for (int i = 0; i < entry_count; ++i) {
        Entry *n = &entries[i];
        if ((n->e.screen != n->source || (n->e.slot & KES_PICTURE)) && !(n->e.slot & KES_DIRECT)) {
            /* Independent images and cross-screen elements need colours from their source palette. */
            const int direct = direct_image(n);
            if (direct < 0) {
                continue;
            }
            n->e.image = direct;
            n->e.slot |= KES_DIRECT;
        }
        if (khdays_ui_mode(n->part) == KHDAYS_UI_ON) {
            add_record(n->e.screen, 0, n);
        }
        if (n->e.screen == n->source) {
            add_record(n->e.screen, 1, n); /* (the check's list keeps the DS's place) */
        }
    }
}

const uint32_t *khdays_ui_records(int screen, int list, uint32_t *count)
{
    *count = record_count[screen & 1][list != 0];
    return records[screen & 1][list != 0];
}

int khdays_ui_checked_bounds(int screen, int *x0, int *y0, int *x1, int *y1)
{
    int any = 0;
    for (int i = 0; i < entry_count; ++i) {
        const Entry *n = &entries[i];
        int ax, ay, bx, by;
        if (n->e.screen != screen || khdays_ui_mode(n->part) == KHDAYS_UI_OFF) {
            continue;
        }
        ax = (int)n->e.x;
        ay = (int)n->e.y;
        bx = (int)(n->e.x + n->e.w + 0.999f);
        by = (int)(n->e.y + n->e.h + 0.999f);
        if (!any) {
            *x0 = ax, *y0 = ay, *x1 = bx, *y1 = by;
            any = 1;
        } else {
            *x0 = ax < *x0 ? ax : *x0;
            *y0 = ay < *y0 ? ay : *y0;
            *x1 = bx > *x1 ? bx : *x1;
            *y1 = by > *y1 ? by : *y1;
        }
    }
    return any;
}

const char *khdays_ui_element_at(int screen, int list, float x, float y)
{
    const uint32_t n = record_count[screen & 1][list != 0];
    const uint32_t *r = records[screen & 1][list != 0];
    for (uint32_t i = 0; i < n; ++i, r += KE_WORDS) {
        float ex, ey, ew, eh;
        memcpy(&ex, &r[KE_X], sizeof(float));
        memcpy(&ey, &r[KE_Y], sizeof(float));
        memcpy(&ew, &r[KE_W], sizeof(float));
        memcpy(&eh, &r[KE_H], sizeof(float));
        if (x >= ex && y >= ey && x < ex + ew && y < ey + eh) {
            return record_ids[screen & 1][list != 0][i];
        }
    }
    return NULL;
}

const uint8_t *khdays_ui_left_out_cell_bits(int screen, int list)
{
    return left_any_cells[list != 0][screen & 1] ? left_cells[list != 0][screen & 1] : NULL;
}

const uint8_t *khdays_ui_left_out_obj_bits(int screen, int list)
{
    return left_any_obj[list != 0][screen & 1] ? left_obj[list != 0][screen & 1] : NULL;
}
