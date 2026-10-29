/* The field's HUD (docs/RUNTIME_MAP.md section 14): the game's BG cells grouped in pieces (the command
 * deck, the portrait, the lower panels) by where the game lays each out, each piece one element per
 * layer while its cells show its files' characters (runtime/core/origin.h). KHDAYS_TRACE_HUD="FRAME
 * ...": at those VBlanks, each text layer's groups of touching cells. */
#include "hud.h"
#include "kh2_hp_hud.h"
#include "kh2_enemy_hud.h"
#include "kh2_portrait_hud.h"
#include "kh2_party_hud.h"
#include "kh2_hud_labels.h"
#include "badges.h"
#include "minimap.h"
#include "kh2_mission_panel.h"
#include "crowns.h"
#include "autosave_notice.h"
#include "save_numbers.h"
#include "room_icons.h"
#include "player_names.h"
#include "bg.h"
#include "elements.h"
#include "parts.h"
#include "kh2_information.h"
#include "../gpu/gpu2d.h"
#include "../runtime/core/io.h"
#include "../runtime/core/origin.h"
#include "../runtime/core/window.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/vram.h"
#include "../runtime/mods/textures.h"
#include "../runtime/mods/tiles2d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *trace_frames;
static int trace_count;

/* --- The pieces ----------------------------------------------------------------------- */

/* A piece: its region of a screen (DS dots, [x0, x1) x [y0, y1)), the layers
 * it takes cells from, and the files its characters come from ('*' stands for
 * one part of the path; with no files, it is always there). Read on the
 * walks (KHDAYS_TRACE_HUD): the field's upper screen, the
 * same in Mission Mode and the story, and its lower screen in a mission. */
typedef struct Piece {
    const char *id;
    int screen; /* 0 engine A's, 1 engine B's */
    int x0, y0, x1, y1;
    unsigned layers; /* bit n: BGn */
    const char *from[3];
    int party_slot; /* 1-3: the live portrait bounds, rather than a fixed region */
    int label;      /* 1-5: player numbers and the native limit text */
} Piece;

static const Piece pieces_table[] = {
    {"hud.player_icon", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 0, 1},
    {"hud.party1.icon", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 0, 2},
    {"hud.party2.icon", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 0, 3},
    {"hud.party3.icon", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 0, 4},
    {"hud.limit_text", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 0, 5},
    {"hud.party1", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 1},
    {"hud.party2", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 2},
    {"hud.party3", 0, 0, 0, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}, 3},
    {"hud.deck", 0, 0, 136, 88, 192, 1u << 3, {"UI/btl/*/main.p2"}},
    {"hud.portrait", 0, 160, 136, 256, 192, 1u << 3, {"UI/btl/*/main.p2", "UI/btl/main.p2"}},
    {"hud.info", 1, 0, 0, 256, 24, 1u << 1, {"UI/btl/*/bl000.NCGR"}},
    {"hud.area", 1, 32, 32, 224, 48, 1u << 2, {"UI/btl/*/bl001.NCGR"}},
    {"hud.target", 1, 32, 48, 96, 144, 1u << 2, {"UI/btl/*/bl001.NCGR"}},
    {"hud.map", 1, 104, 48, 224, 144, 1u << 2 | 1u << 3, {"UI/btl/*/bl001.NCGR", "UI/btl/mapchr.p2"}},
    {"hud.mission", 1, 0, 152, 256, 192, 1u << 2, {"UI/btl/*/bl001.NCGR"}},
};
#define PIECE_COUNT ((int)(sizeof(pieces_table) / sizeof(pieces_table[0])))

/* Whether the file `name` (as origin.h names it: "@<hex>" for a part of a
 * file, ".z" kept for a decompressed one) matches `pattern`. */
static int file_is(const char *name, const char *pattern)
{
    const char *n = name, *p = pattern;
    while (*p != '\0') {
        if (*p == '*') {
            while (*n != '\0' && *n != '/' && *n != '@') {
                ++n;
            }
            ++p;
        } else if (*n == *p) {
            ++n, ++p;
        } else {
            return 0;
        }
    }
    return *n == '\0' || *n == '@' || strcmp(n, ".z") == 0 || strncmp(n, ".z@", 3) == 0;
}

static int from_piece(const Piece *piece, const char *file)
{
    if (file == NULL) {
        return 0;
    }
    for (int k = 0; k < 3 && piece->from[k] != NULL; ++k) {
        if (file_is(file, piece->from[k])) {
            return 1;
        }
    }
    return 0;
}

/* A piece's element on one layer: its cells as they were last drawn, and its
 * image. */
typedef struct Shown {
    int cells_x, cells_y, w, h; /* its top-left cell in the visible grid, and its size in cells */
    int room;                   /* how many cells the arrays hold */
    uint32_t *entries;          /* w * h: the map entry under each cell (0xffffffff: not the piece's) */
    uint8_t *chars;             /* w * h * 64: each cell's character as VRAM held it */
    uint32_t *hd;               /* w * h: the mod's tile drawn instead in each cell (tiles2d.h), 0 none */
    int image, image_w, image_h, image_scale;
    int hd_image1, hd_w, hd_h; /* the image of the mods' tiles in colour, + 1 (0: none), its size */
} Shown;

static Shown shown[PIECE_COUNT][4];

/* The screen's cells, 33 x 25 so that a layer's fine scroll is covered: cell
 * (i, j) is the map cell under screen point (i * 8 - hofs % 8,
 * j * 8 - vofs % 8). */
#define GW 33
#define GH 25

static int grow(Shown *sh, int cells)
{
    if (sh->room < cells) {
        uint32_t *e = (uint32_t *)realloc(sh->entries, (size_t)cells * sizeof(uint32_t));
        uint8_t *c;
        if (e == NULL) {
            return 0;
        }
        sh->entries = e;
        c = (uint8_t *)realloc(sh->chars, (size_t)cells * 64u);
        if (c == NULL) {
            return 0;
        }
        sh->chars = c;
        e = (uint32_t *)realloc(sh->hd, (size_t)cells * sizeof(uint32_t));
        if (e == NULL) {
            return 0;
        }
        sh->hd = e;
        sh->room = cells;
        sh->w = sh->h = 0; /* to be drawn again */
    }
    return 1;
}

/* A mod's HD tile sheet drawing a cell's tile (khdays_tiles2d_bg_tile's word), or NULL. */
static const KhdaysTextureReplacement *hd_sheet(uint32_t word)
{
    const KhdaysTextureReplacement *r = word != 0u ? khdays_textures_replacement((int)(word >> 20) - 1) : NULL;
    return r != NULL && r->rgba != NULL && (r->info & KHDAYS_REPLACEMENT_TILES) != 0u &&
                   ((r->info >> 8) & 255u) != 0u && (r->info >> 16) != 0u
               ? r
               : NULL;
}

static int emit_piece(int index, int bg, const KhdaysBgLayer *l)
{
    const Piece *piece = &pieces_table[index];
    Shown *sh = &shown[index][bg];
    const int fx = l->hofs & 7, fy = l->vofs & 7, size = l->colors256 ? 64 : 32;
    const int scale = khdays_ui_scale();
    int i0 = GW, j0 = GH, i1 = -1, j1 = -1, there = piece->from[0] == NULL, changed = 0, w, h;
    KhdaysUiElement e;
    KhdaysUiElement party_box = {0};
    if (piece->party_slot && !khdays_ui_kh2_party_box(piece->party_slot, &party_box))
        return 0;
    if (piece->label && !khdays_ui_kh2_label_box(piece->label - 1, &party_box))
        return 0;
    /* the visible cells whose middle lies in the region */
    for (int j = 0; j < GH; ++j) {
        for (int i = 0; i < GW; ++i) {
            const int mx = i * 8 - fx + 4, my = j * 8 - fy + 4;
            const int inside = (piece->party_slot || piece->label)
                                   ? mx >= party_box.x && mx < party_box.x + party_box.w && my >= party_box.y &&
                                         my < party_box.y + party_box.h
                                   : mx >= piece->x0 && mx < piece->x1 && my >= piece->y0 && my < piece->y1;
            if (inside) {
                i0 = i < i0 ? i : i0, i1 = i > i1 ? i : i1;
                j0 = j < j0 ? j : j0, j1 = j > j1 ? j : j1;
            }
        }
    }
    if (i1 < 0) {
        return 0;
    }
    w = i1 - i0 + 1, h = j1 - j0 + 1;
    /* it is there while one of its cells shows a character from its files */
    for (int j = j0; j <= j1 && !there; ++j) {
        for (int i = i0; i <= i1 && !there; ++i) {
            const uint32_t entry = khdays_bg_map_entry(piece->screen, l, (l->hofs >> 3) + i, (l->vofs >> 3) + j);
            there = khdays_bg_cell_shows(piece->screen, l, entry) &&
                    from_piece(piece, khdays_bg_origin(piece->screen, l->chars + (entry & 0x3ffu) * (uint32_t)size));
        }
    }
    if (!there || !grow(sh, w * h)) {
        return 0;
    }
    if (sh->cells_x != i0 || sh->cells_y != j0 || sh->w != w || sh->h != h) {
        sh->cells_x = i0, sh->cells_y = j0, sh->w = w, sh->h = h;
        memset(sh->entries, 0xff, (size_t)w * (size_t)h * sizeof(uint32_t));
        memset(sh->hd, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
        changed = 1;
    }
    /* its cells as the map and VRAM hold them now (those another part stands for are not its) */
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const int cx = ((l->hofs >> 3) + i0 + i) % l->wt, cy = ((l->vofs >> 3) + j0 + j) % l->ht;
            uint32_t entry = khdays_ui_cell_taken(piece->screen, bg, cx, cy)
                                 ? 0xffffffffu
                                 : khdays_bg_map_entry(piece->screen, l, cx, cy);
            if (piece->label && entry != 0xffffffffu && !khdays_ui_kh2_label_cell(piece->label - 1, entry))
                entry = 0xffffffffu;
            if (entry != 0xffffffffu &&
                (khdays_ui_kh2_hp_cell(piece->screen, bg, l, entry) || khdays_minimap_cell(piece->screen, bg, entry))) {
                khdays_ui_left_out_cell(KHDAYS_UI_HUD, piece->screen, bg, cx, cy);
                entry = 0xffffffffu;
            }
            uint8_t ch[64];
            uint32_t hd = 0u;
            memset(ch, 0, sizeof(ch));
            if (entry != 0xffffffffu) {
                const uint32_t at = l->chars + (entry & 0x3ffu) * (uint32_t)size;
                for (int k = 0; k < size; ++k) {
                    const uint8_t *b = khdays_bg_memory(piece->screen, at + (uint32_t)k);
                    ch[k] = b != NULL ? *b : 0;
                }
                hd = khdays_tiles2d_bg_tile(piece->screen, at); /* a mod's HD sheet draws the tile */
            }
            if (sh->entries[j * w + i] != entry || sh->hd[j * w + i] != hd ||
                memcmp(sh->chars + (size_t)(j * w + i) * 64u, ch, 64) != 0) {
                sh->entries[j * w + i] = entry;
                sh->hd[j * w + i] = hd;
                memcpy(sh->chars + (size_t)(j * w + i) * 64u, ch, 64);
                changed = 1;
            }
        }
    }
    if (sh->image < 0 || sh->image_w != w * 8 * scale || sh->image_h != h * 8 * scale || sh->image_scale != scale) {
        khdays_ui_image_put(sh->image);
        sh->image = khdays_ui_image_get(w * 8 * scale, h * 8 * scale);
        sh->image_w = w * 8 * scale, sh->image_h = h * 8 * scale, sh->image_scale = scale;
        changed = 1;
    }
    if (sh->image < 0) {
        return 0;
    }
    if (changed) {
        const int tw = w * 8 * scale, th = h * 8 * scale;
        uint32_t *texels, *colours = NULL;
        int coloured = 0;
        for (int c = 0; c < w * h; ++c) {
            const KhdaysTextureReplacement *r = sh->entries[c] != 0xffffffffu ? hd_sheet(sh->hd[c]) : NULL;
            coloured |= r != NULL && !(r->info & KHDAYS_REPLACEMENT_INDEXED);
        }
        /* the mods' tiles in colour go in an image of their own, drawn over the piece's */
        if (coloured && (sh->hd_image1 == 0 || sh->hd_w != tw || sh->hd_h != th)) {
            if (sh->hd_image1 != 0) {
                khdays_ui_image_put(sh->hd_image1 - 1);
            }
            sh->hd_image1 = khdays_ui_image_get(tw, th) + 1;
            sh->hd_w = tw, sh->hd_h = th;
        } else if (!coloured && sh->hd_image1 != 0) {
            khdays_ui_image_put(sh->hd_image1 - 1);
            sh->hd_image1 = 0;
        }
        texels = khdays_ui_image_begin(sh->image);
        if (texels == NULL) {
            return 0;
        }
        if (sh->hd_image1 != 0) {
            colours = khdays_ui_image_begin(sh->hd_image1 - 1);
        }
        for (int ty = 0; ty < th; ++ty) {
            for (int tx = 0; tx < tw; ++tx) {
                const int px = tx / scale, py = ty / scale, c = (py / 8) * w + px / 8;
                const uint32_t entry = sh->entries[c];
                const uint8_t *ch = sh->chars + (size_t)c * 64u;
                const KhdaysTextureReplacement *r = entry != 0xffffffffu ? hd_sheet(sh->hd[c]) : NULL;
                uint32_t t = 0, colour = 0, v = 0;
                if (r != NULL) {
                    /* the mod's tile at its own resolution: its texel under this one's middle, the
                     * tile turned as the cell's entry turns it */
                    const int rs = (int)((r->info >> 8) & 255u), cols = (int)(r->info >> 16);
                    const uint32_t tile = sh->hd[c] & 0xfffffu;
                    int ux = ((tx - (px / 8) * 8 * scale) * 2 + 1) * rs / (2 * scale);
                    int uy = ((ty - (py / 8) * 8 * scale) * 2 + 1) * rs / (2 * scale);
                    ux = (entry & 0x400u) ? 8 * rs - 1 - ux : ux;
                    uy = (entry & 0x800u) ? 8 * rs - 1 - uy : uy;
                    {
                        const uint32_t s =
                            r->rgba[(size_t)((int)(tile / (uint32_t)cols) * 8 * rs + uy) * (size_t)r->width +
                                    (size_t)((int)(tile % (uint32_t)cols) * 8 * rs + ux)];
                        if (r->info & KHDAYS_REPLACEMENT_INDEXED) {
                            v = s & 0xffu;
                        } else {
                            colour = s; /* its own colour and alpha; nothing of the game's tile under it */
                        }
                    }
                } else if (entry != 0xffffffffu) {
                    const int ex = (entry & 0x400u) ? 7 - (px & 7) : px & 7;
                    const int ey = (entry & 0x800u) ? 7 - (py & 7) : py & 7;
                    v = !l->colors256 ? (ch[ey * 4 + ex / 2] >> ((ex & 1) * 4)) & 15u : ch[ey * 8 + ex];
                }
                if (v != 0 && entry != 0xffffffffu) {
                    const uint32_t pal = entry >> 12;
                    t = !l->colors256 ? KET_OPAQUE | (pal * 16u + v)
                                      : KET_OPAQUE | KET_EXTENDED | pal << KET_EXT_PAL_SHIFT | v;
                }
                texels[(size_t)ty * (size_t)tw + (size_t)tx] = t;
                if (colours != NULL) {
                    colours[(size_t)ty * (size_t)tw + (size_t)tx] = colour;
                }
            }
        }
        khdays_ui_image_end(sh->image);
        if (colours != NULL) {
            khdays_ui_image_end(sh->hd_image1 - 1);
        }
    }
    e.id = piece->id;
    e.screen = piece->screen;
    e.slot = (uint32_t)bg;
    e.x = (float)(i0 * 8 - fx);
    e.y = (float)(j0 * 8 - fy);
    e.w = (float)(w * 8);
    e.h = (float)(h * 8);
    e.image = sh->image;
    KhdaysUiElement hd_element = e;
    hd_element.slot = KES_DIRECT | (uint32_t)bg;
    hd_element.image = sh->hd_image1 - 1;
    int native_hp = piece->label ? khdays_ui_kh2_label_native(piece->label - 1, &e)
                    : piece->party_slot
                        ? khdays_ui_kh2_party_compose(piece->party_slot, &e, sh->hd_image1 ? &hd_element : NULL)
                        : bg == 3 && strcmp(piece->id, "hud.portrait") == 0 &&
                              khdays_ui_kh2_hp_compose(&e, sh->hd_image1 ? &hd_element : NULL);
    if (piece->label && sh->hd_image1)
        khdays_ui_kh2_label_native(piece->label - 1, &hd_element);
    if (strcmp(piece->id, "hud.info") == 0 && khdays_ui_information_active(piece->screen))
        native_hp = 1;
    if (strcmp(piece->id, "hud.mission") == 0 && khdays_ui_mission_panel_owns())
        native_hp = 1;
    if (!native_hp)
        khdays_ui_emit(KHDAYS_UI_HUD, &e);
    if (sh->hd_image1 != 0 && !native_hp) {
        /* given later, so above the piece's own image */
        e.slot = KES_DIRECT | (uint32_t)bg;
        e.image = sh->hd_image1 - 1;
        khdays_ui_emit(KHDAYS_UI_HUD, &e);
    }
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            if (sh->entries[j * w + i] != 0xffffffffu) {
                khdays_ui_left_out_cell(KHDAYS_UI_HUD, piece->screen, bg, ((l->hofs >> 3) + i0 + i) % l->wt,
                                        ((l->vofs >> 3) + j0 + j) % l->ht);
            }
        }
    }
    return 1;
}

/* --- A piece's sprites ---------------------------------------------------------------- */

/* A sprite of the piece's screen that the port draws: its image, kept by its
 * OAM entry and made again when what it draws changed. */
typedef struct Sprite {
    uint32_t attrs; /* attr0 | attr1 << 16 as last drawn (without the position) */
    uint32_t attr2;
    uint8_t *pixels; /* w * h palette indices as drawn last */
    int room, w, h;
    int image, image_scale;
} Sprite;

static Sprite sprites[2][128];
static char sprite_ids[PIECE_COUNT][48];

static const uint8_t *obj_memory(int engine, uint32_t address)
{
    const KhdaysVramPages *pages = khdays_vram_pages();
    const uint32_t size = engine ? 0x20000u : 0x40000u;
    const uint8_t *page;
    address %= size;
    page = pages->page[engine ? KHDAYS_AREA_BOBJ : KHDAYS_AREA_AOBJ][address / KHDAYS_VRAM_PAGE];
    return page != NULL ? page + address % KHDAYS_VRAM_PAGE : NULL;
}

static int gauge_obj_origin(int engine, uint32_t address)
{
    const uint8_t *at = obj_memory(engine, address);
    if (!at)
        return 0;
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        const uint8_t *base = khdays_vram_bank(bank);
        if (base && at >= base && at < base + khdays_vram_bank_size[bank]) {
            const char *file;
            u32 offset, run;
            return khdays_origin_of_bank(bank, (u32)(at - base), &file, &offset, &run) && strstr(file, "UI/btl/") &&
                   strstr(file, "main.p2");
        }
    }
    return 0;
}

/* Makes the sprites whose middle lies in a piece that is there into elements
 * in the sprites' slot of their priority (as compose.h kd_sprites draws
 * them), and leaves their entries out. Affine sprites, the sprite window's,
 * bitmaps and 8-bit ones in the extended palettes stay the game's. */
static void emit_sprites(int index, int there)
{
    static const uint8_t obj_w[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const uint8_t obj_h[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    const Piece *piece = &pieces_table[index];
    const int gauge_probe = strcmp(piece->id, "hud.portrait") == 0 &&
                            (khdays_ui_kh2_hp_ready() || khdays_ui_kh2_party_ready(1) || khdays_ui_kh2_party_ready(2) ||
                             khdays_ui_kh2_party_ready(3) || khdays_ui_kh2_enemy_ready());
    if (piece->label || (!there && !gauge_probe))
        return;
    const int engine = piece->screen;
    const uint8_t *oam = (const uint8_t *)KHDAYS_W(0x07000000u + (uint32_t)engine * 0x400u);
    const uint32_t dispcnt = IO32(engine ? 0x1000u : 0u);
    const int scale = khdays_ui_scale();
    KhdaysUiElement party_box = {0};
    if (piece->party_slot && !khdays_ui_kh2_party_box(piece->party_slot, &party_box))
        return;
    if (!(dispcnt & 0x1000u)) {
        return; /* the screen shows no sprites (DISPCNT bit 12 off) */
    }
    if (sprite_ids[index][0] == '\0') {
        snprintf(sprite_ids[index], sizeof(sprite_ids[index]), "%s.sprite", piece->id);
    }
    for (int i = 0; i < 128; ++i) {
        const uint32_t a0 = (uint32_t)(oam[8 * i] | oam[8 * i + 1] << 8);
        const uint32_t a1 = (uint32_t)(oam[8 * i + 2] | oam[8 * i + 3] << 8);
        const uint32_t a2 = (uint32_t)(oam[8 * i + 4] | oam[8 * i + 5] << 8);
        const uint32_t shape = a0 >> 14, size = a1 >> 14, mode = (a0 >> 10) & 3u;
        const int colors256 = (a0 >> 13) & 1;
        Sprite *sp = &sprites[engine][i];
        int w, h, x, y, changed = 0;
        KhdaysUiElement e;
        if (strcmp(piece->id, "hud.mission") == 0 && khdays_ui_mission_panel_owns() && shape < 3u && mode < 2u &&
            ((a0 & 0x100u) || !(a0 & 0x200u))) {
            int sx = (int)(a1 & 511u), sy = (int)(a0 & 255u);
            sx = sx >= 256 ? sx - 512 : sx;
            sy = sy >= 192 ? sy - 256 : sy;
            int sw = obj_w[shape][size], sh = obj_h[shape][size];
            if ((a0 & 0x300u) == 0x300u) {
                sw *= 2;
                sh *= 2;
            }
            if (sx + sw / 2 >= piece->x0 && sx + sw / 2 < piece->x1 && sy + sh / 2 >= piece->y0 &&
                sy + sh / 2 < piece->y1) {
                khdays_ui_left_out_obj(KHDAYS_UI_HUD, engine, i);
                continue;
            }
        }
        if ((a0 & 0x100u) || (a0 & 0x200u) || shape == 3u || mode >= 2u || (colors256 && (dispcnt & 0x80000000u))) {
            continue;
        }
        w = obj_w[shape][size], h = obj_h[shape][size];
        x = (int)(a1 & 0x1ffu), y = (int)(a0 & 0xffu);
        x = x >= 256 ? x - 512 : x;
        y = y >= 192 ? y - 256 : y;
        const int outside =
            piece->party_slot
                ? x + w / 2 < party_box.x || x + w / 2 >= party_box.x + party_box.w || y + h / 2 < party_box.y ||
                      y + h / 2 >= party_box.y + party_box.h + 8
                : x + w / 2 < piece->x0 || x + w / 2 >= piece->x1 || y + h / 2 < piece->y0 || y + h / 2 >= piece->y1;
        if (outside && (!gauge_probe || h != 8)) {
            continue;
        }
        if (outside) {
            const uint32_t tile = colors256 && !(dispcnt & 0x10u) ? a2 & 0x3feu : a2 & 0x3ffu;
            const uint32_t address = tile * ((dispcnt & 0x10u) ? 32u << ((dispcnt >> 20) & 3u) : 32u);
            if (!gauge_obj_origin(engine, address))
                continue;
        }
        uint8_t party_pixels[16 * 8];
        Sprite party_sprite = {0};
        if (piece->party_slot) {
            if (w != 16 || h != 8)
                continue;
            party_sprite.pixels = party_pixels;
            party_sprite.room = sizeof(party_pixels);
            memset(party_pixels, 0, sizeof(party_pixels));
            sp = &party_sprite;
        }
        if (sp->room < w * h) {
            uint8_t *grown = (uint8_t *)realloc(sp->pixels, (size_t)w * (size_t)h);
            if (grown == NULL) {
                continue;
            }
            sp->pixels = grown;
            sp->room = w * h;
            changed = 1;
        }
        if (sp->w != w || sp->h != h || sp->attrs != ((a0 & ~0xffu) | (a1 & ~0x1ffu) << 16) || sp->attr2 != a2) {
            sp->w = w, sp->h = h;
            sp->attrs = (a0 & ~0xffu) | (a1 & ~0x1ffu) << 16;
            sp->attr2 = a2;
            changed = 1;
        }
        /* its pixels as kd_sprite_texel reads them */
        for (int ty = 0; ty < h; ++ty) {
            for (int tx = 0; tx < w; ++tx) {
                const uint32_t tile = a2 & 0x3ffu;
                uint32_t address;
                uint8_t v;
                const uint8_t *b;
                if (colors256) {
                    address = (dispcnt & 0x10u)
                                  ? tile * (32u << ((dispcnt >> 20) & 3u)) +
                                        ((uint32_t)(ty >> 3) * (uint32_t)(w >> 3) + (uint32_t)(tx >> 3)) * 64u
                                  : (tile & ~1u) * 32u + (uint32_t)(ty >> 3) * 1024u + (uint32_t)(tx >> 3) * 64u;
                    b = obj_memory(engine, address + (uint32_t)(ty & 7) * 8u + (uint32_t)(tx & 7));
                    v = b != NULL ? *b : 0;
                } else {
                    address = (dispcnt & 0x10u)
                                  ? tile * (32u << ((dispcnt >> 20) & 3u)) +
                                        ((uint32_t)(ty >> 3) * (uint32_t)(w >> 3) + (uint32_t)(tx >> 3)) * 32u
                                  : tile * 32u + (uint32_t)(ty >> 3) * 1024u + (uint32_t)(tx >> 3) * 32u;
                    b = obj_memory(engine, address + (uint32_t)(ty & 7) * 4u + (uint32_t)(tx & 7) / 2u);
                    v = b != NULL ? (uint8_t)((*b >> ((tx & 1) * 4)) & 15u) : 0;
                }
                if (sp->pixels[ty * w + tx] != v) {
                    sp->pixels[ty * w + tx] = v;
                    changed = 1;
                }
            }
        }
        if ((strcmp(piece->id, "hud.portrait") == 0 || piece->party_slot) &&
            (khdays_ui_kh2_hp_sprite(sp->pixels, w, h) || khdays_ui_kh2_enemy_sprite(sp->pixels, w, h))) {
            khdays_ui_left_out_obj(KHDAYS_UI_HUD, engine, i);
            continue;
        }
        if (piece->party_slot || outside || !there)
            continue; /* only the gauge's own OBJ copy is suppressed here */
        if (sp->image < 0 || sp->image_scale != scale) {
            khdays_ui_image_put(sp->image);
            sp->image = -1;
        }
        if (sp->image < 0) {
            sp->image = khdays_ui_image_get(w * scale, h * scale);
            sp->image_scale = scale;
            changed = 1;
        }
        if (sp->image < 0) {
            continue;
        }
        if (changed) {
            uint32_t *texels = khdays_ui_image_begin(sp->image);
            const uint32_t pal = a2 >> 12;
            if (texels == NULL) {
                continue;
            }
            for (int py = 0; py < h; ++py) {
                for (int px = 0; px < w; ++px) {
                    const int sx0 = (a1 & 0x1000u) ? w - 1 - px : px, sy0 = (a1 & 0x2000u) ? h - 1 - py : py;
                    const uint32_t v = sp->pixels[sy0 * w + sx0];
                    const uint32_t t = v == 0 ? 0u : KET_OPAQUE | (colors256 ? v : pal * 16u + v);
                    for (int yy = 0; yy < scale; ++yy) {
                        uint32_t *out =
                            texels + (size_t)(py * scale + yy) * (size_t)(w * scale) + (size_t)px * (size_t)scale;
                        for (int xx = 0; xx < scale; ++xx) {
                            out[xx] = t;
                        }
                    }
                }
            }
            khdays_ui_image_end(sp->image);
        }
        e.id = sprite_ids[index];
        e.screen = engine;
        e.slot = KES_SPRITES | ((a2 >> 10) & 3u) | (mode == 1u ? 1u << KES_SEMI_SHIFT : 0u);
        e.x = (float)x;
        e.y = (float)y;
        e.w = (float)w;
        e.h = (float)h;
        e.image = sp->image;
        khdays_ui_emit(KHDAYS_UI_HUD, &e);
        khdays_ui_left_out_obj(KHDAYS_UI_HUD, engine, i);
    }
}

static void emit(void)
{
    khdays_minimap_emit();
    khdays_ui_kh2_enemy_emit();
    khdays_ui_kh2_hp_prepare();
    khdays_ui_kh2_party_prepare();
    khdays_ui_kh2_labels_prepare();
    khdays_ui_kh2_portrait_prepare();
    int party_there = 0;
    for (int n = 0; n < PIECE_COUNT; ++n) {
        int there = 0;
        for (int bg = 0; bg < 4; ++bg) {
            KhdaysBgLayer l;
            if (((pieces_table[n].layers >> bg) & 1u) && khdays_bg_text_layer(pieces_table[n].screen, bg, &l)) {
                there |= emit_piece(n, bg, &l);
            }
        }
        if (there || strcmp(pieces_table[n].id, "hud.portrait") == 0) {
            emit_sprites(n, there);
        }
        if (there && strcmp(pieces_table[n].id, "hud.mission") == 0 && !khdays_ui_mission_panel_owns()) {
            khdays_ui_badges_emit(); /* the port's own addition to the panel, above it */
        }
        party_there |= there && strcmp(pieces_table[n].id, "hud.portrait") == 0;
    }
    khdays_ui_kh2_portrait_emit();
    khdays_ui_kh2_labels_emit();
    khdays_ui_player_names_emit(party_there); /* the port's: the players' names in multiplayer */
    khdays_ui_crowns_emit();                  /* the port's: the crowns on the story's day list */
    khdays_ui_save_numbers_emit();            /* the port's: the files' numbers on the game's save lists */
    khdays_ui_autosave_notice_emit();         /* the port's: an autosave written or failed */
    khdays_ui_room_icons_emit();              /* the port's: the rooms' signal and lock on the game's list */
}

void khdays_ui_hud_init(void)
{
    const char *text = getenv("KHDAYS_TRACE_HUD");
    int room = 0;
    for (int n = 0; n < PIECE_COUNT; ++n) {
        for (int bg = 0; bg < 4; ++bg) {
            shown[n][bg].image = -1;
        }
    }
    for (int e = 0; e < 2; ++e) {
        for (int i = 0; i < 128; ++i) {
            sprites[e][i].image = -1;
        }
    }
    khdays_ui_register(KHDAYS_UI_HUD, emit);
    if (khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_OFF) {
        khdays_origin_want(); /* whether a piece is there depends on its characters' files */
    }
    for (const char *s = text; s != NULL && *s != '\0';) {
        char *end;
        const unsigned long f = strtoul(s, &end, 10);
        if (end == s) {
            break;
        }
        if (trace_count == room) {
            uint32_t *grown;
            room = room ? room * 2 : 8;
            grown = (uint32_t *)realloc(trace_frames, (size_t)room * sizeof(uint32_t));
            if (grown == NULL) {
                return;
            }
            trace_frames = grown;
        }
        trace_frames[trace_count++] = (uint32_t)f;
        khdays_origin_want(); /* the tiles' files are reported with the groups */
        s = end;
        while (*s == ' ' || *s == ',') {
            ++s;
        }
    }
}

/* A group's tiles by file: each file once, with a count (the list grows). */
typedef struct FileCount {
    const char *file;
    int cells;
} FileCount;

static FileCount *files;
static int file_count, file_room;

static void count_file(const char *file)
{
    for (int i = 0; i < file_count; ++i) {
        if (files[i].file == file || (file != NULL && files[i].file != NULL && strcmp(files[i].file, file) == 0)) {
            ++files[i].cells;
            return;
        }
    }
    if (file_count == file_room) {
        const int room = file_room ? file_room * 2 : 8;
        FileCount *grown = (FileCount *)realloc(files, (size_t)room * sizeof(FileCount));
        if (grown == NULL) {
            return;
        }
        files = grown;
        file_room = room;
    }
    files[file_count].file = file;
    files[file_count].cells = 1;
    ++file_count;
}

static void trace_layer(uint32_t frame, int engine, int bg, const KhdaysBgLayer *l)
{
    static uint32_t entries[GH][GW]; /* (static: at VBlank the stack is the game's, so keep it small) */
    static int group[GH][GW];
    static int stack[GW * GH][2];
    int groups = 0;
    const int fx = l->hofs & 7, fy = l->vofs & 7;
    for (int j = 0; j < GH; ++j) {
        for (int i = 0; i < GW; ++i) {
            const uint32_t e = khdays_bg_map_entry(engine, l, (l->hofs >> 3) + i, (l->vofs >> 3) + j);
            entries[j][i] = e;
            group[j][i] = khdays_bg_cell_shows(engine, l, e) ? 0 : -1;
        }
    }
    for (int j = 0; j < GH; ++j) {
        for (int i = 0; i < GW; ++i) {
            int x0 = i, y0 = j, x1 = i, y1 = j, cells = 0, sp = 0;
            uint32_t tmin = 0x3ff, tmax = 0, pals = 0;
            if (group[j][i] != 0) {
                continue;
            }
            ++groups;
            group[j][i] = groups;
            file_count = 0;
            stack[sp][0] = i, stack[sp][1] = j, ++sp;
            while (sp > 0) {
                const int ci = stack[sp - 1][0], cj = stack[sp - 1][1];
                const uint32_t e = entries[cj][ci];
                --sp;
                ++cells;
                x0 = ci < x0 ? ci : x0, x1 = ci > x1 ? ci : x1;
                y0 = cj < y0 ? cj : y0, y1 = cj > y1 ? cj : y1;
                tmin = (e & 0x3ffu) < tmin ? (e & 0x3ffu) : tmin;
                tmax = (e & 0x3ffu) > tmax ? (e & 0x3ffu) : tmax;
                pals |= 1u << (e >> 12);
                count_file(khdays_bg_origin(engine, l->chars + (e & 0x3ffu) * (l->colors256 ? 64u : 32u)));
                for (int d = 0; d < 4; ++d) {
                    const int ni = ci + (d == 0) - (d == 1), nj = cj + (d == 2) - (d == 3);
                    if (ni >= 0 && ni < GW && nj >= 0 && nj < GH && group[nj][ni] == 0) {
                        group[nj][ni] = groups;
                        stack[sp][0] = ni, stack[sp][1] = nj, ++sp;
                    }
                }
            }
            {
                char line[1024];
                size_t at;
                const int px0 = x0 * 8 - fx, py0 = y0 * 8 - fy, px1 = (x1 + 1) * 8 - fx, py1 = (y1 + 1) * 8 - fy;
                snprintf(line, sizeof(line),
                         "hud: frame %u, engine %c BG%d (priority %d): dots %d,%d-%d,%d, map cells %d,%d %dx%d, %d "
                         "cells, characters %u-%u, palettes %04x",
                         frame, engine ? 'B' : 'A', bg, l->priority, px0, py0, px1, py1, ((l->hofs >> 3) + x0) % l->wt,
                         ((l->vofs >> 3) + y0) % l->ht, x1 - x0 + 1, y1 - y0 + 1, cells, tmin, tmax, pals);
                at = strlen(line);
                for (int k = 0; k < file_count && at < sizeof(line) - 1; ++k) {
                    at += (size_t)snprintf(line + at, sizeof(line) - at, "%s %s x%d", k ? "," : "; from",
                                           files[k].file != NULL ? files[k].file : "(no file)", files[k].cells);
                }
                fprintf(stderr, "%s\n", line);
                khdays_log("%s", line);
            }
        }
    }
}

void khdays_ui_hud_vblank(uint32_t frame)
{
    int listed = 0;
    for (int n = 0; n < trace_count && !listed; ++n) {
        listed = trace_frames[n] == frame;
    }
    if (!listed) {
        return;
    }
    for (int engine = 0; engine < 2; ++engine) {
        for (int bg = 0; bg < 4; ++bg) {
            KhdaysBgLayer l;
            if (khdays_bg_text_layer(engine, bg, &l)) {
                trace_layer(frame, engine, bg, &l);
            }
        }
    }
}
