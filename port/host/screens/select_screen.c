/* Mission Mode's character select on one screen (select_screen.h), in the theater's pieces: a file
 * tools/hd/theater_mission.py writes (docs/HD_SCREENS.md). It shows the game's screen as
 * runtime/menus/charselect.c reads it: the characters in the game's order, this copy's character with
 * its gauges, and each place's plate, ringed once its player has chosen. */
#include "select_screen.h"

#include "layout_screen.h"
#include "../app/places.h"
#include "ui_text.h"
#include "../../runtime/menus/charselect.h"
#include "../../runtime/characters/characters.h"
#include "mod_faces.h"
#include "../../runtime/core/rom.h"
#include "../app/profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAMES 0x0b
#define WEAPONS 0x1f
#define BARS_MOST 10
/* the port's look, as the mock-up the maintainer chose */
#define PANEL_ALPHA 0.6f
#define TILE_ALPHA 0.8f
#define GREY_LIGHT 140 /* the grey faces' light: the characters nobody is on */
#define RING 3.0f      /* the white ring around a place's circle */
#define EDGE 2.0f      /* a panel's edge */
#define QUESTION 0.55f /* the random choice's "?": its height in its tile's */
#define KEY_SIZE 36.0f /* the random choice's button (Y) in its tile's corner */
static const Uint8 BLACK[3] = {0, 0, 0}, WHITE[3] = {255, 255, 255}, EDGE_GREY[3] = {110, 110, 110},
                   BAR_OFF[3] = {40, 40, 40}, OTHERS_GREY[3] = {80, 80, 80};

static struct {
    int tried, ok;
    KhdaysLayout layout;
    KhdaysLayoutStrings strings;
    SDL_Texture *art;
    int columns;
    float gap, stat_pitch, gauge_pitch, plate_pitch, frame_place, frame_other;
    Uint8 bars[BARS_MOST][3];
    int bar_count;
    KhdaysModFaces faces; /* the mods' characters' */
} s;

static void forget(void)
{
    if (s.art != NULL) {
        SDL_DestroyTexture(s.art);
        s.art = NULL;
    }
    s.bar_count = 0;
    khdays_mod_faces_forget(&s.faces);
    khdays_layout_forget(&s.layout);
    khdays_layout_strings_free(&s.strings);
}

static int load(SDL_Renderer *renderer, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    static KhdaysRomReader *reader; /* its own: the window's thread */
    char relative[96], path[1024], folder[1024], file[256];
    char *text, *next, *slash;
    size_t size = 0;
    forget();
    s.layout.who = "character select";
    s.layout.renderer = renderer;
    if (language < 1 || language > 5) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/select/%s/select.txt", codes[language]);
    if (!khdays_mods_find(relative, path, (int)sizeof(path)) || (text = (char *)SDL_LoadFile(path, &size)) == NULL) {
        return 0;
    }
    snprintf(folder, sizeof(folder), "%s", path);
    slash = strrchr(folder, '/');
    if (strrchr(folder, '\\') > slash) {
        slash = strrchr(folder, '\\');
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
    s.columns = 0;
    for (char *line = text; line != NULL && *line != '\0'; line = next) {
        float a, b;
        int n, r, g, bl;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (khdays_layout_line(&s.layout, line, folder)) {
            continue;
        }
        if (SDL_sscanf(line, "grid %d %f", &n, &a) == 2) {
            s.columns = n > 0 ? n : 1;
            s.gap = a;
        } else if (SDL_sscanf(line, "stats %f", &a) == 1) {
            s.stat_pitch = a;
        } else if (SDL_sscanf(line, "gauge_pitch %f", &a) == 1) {
            s.gauge_pitch = a;
        } else if (SDL_sscanf(line, "plates %f", &a) == 1) {
            s.plate_pitch = a;
        } else if (SDL_sscanf(line, "frame %f %f", &a, &b) == 2) {
            s.frame_place = a;
            s.frame_other = b;
        } else if (SDL_sscanf(line, "gauge_colour %d %d %d", &r, &g, &bl) == 3 && s.bar_count < BARS_MOST) {
            s.bars[s.bar_count][0] = (Uint8)r;
            s.bars[s.bar_count][1] = (Uint8)g;
            s.bars[s.bar_count][2] = (Uint8)bl;
            ++s.bar_count;
        } else if (SDL_sscanf(line, "art %255s", file) == 1) {
            char full[1300];
            int w, h;
            snprintf(full, sizeof(full), "%s%s", folder, file);
            s.art = khdays_layout_texture(&s.layout, full, 0, &w, &h);
        }
    }
    SDL_free(text);
    if (!khdays_layout_ready(&s.layout) || s.columns == 0 || s.bar_count == 0 ||
        khdays_layout_place(&s.layout, "tile") == NULL || khdays_layout_place(&s.layout, "face") == NULL) {
        SDL_Log("character select: %s is incomplete", path);
        forget();
        return 0;
    }
    if (reader == NULL) {
        reader = khdays_rom_reader_new();
    }
    snprintf(path, sizeof(path), "UI/mlt/mlt_%s.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings, s.layout.who);
    khdays_charselect_columns(s.columns);
    return 1;
}

/* ---- Drawing --------------------------------------------------------------------------------- */

static const float *place(const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&s.layout, name);
    return p != NULL ? &p->x : none;
}

static void face(const KhdaysLayoutFrame *fr, int character, int in_colour, float x, float y, float w, float h)
{
    char name[24];
    if (character >= KHDAYS_SELECT_CHARACTERS) {
        khdays_mod_face_draw(&s.faces, &s.layout, fr, character, x, y, w, h, in_colour ? 255 : GREY_LIGHT);
        return;
    }
    snprintf(name, sizeof(name), in_colour ? "face%d" : "face%d_grey", character);
    if (in_colour) {
        khdays_layout_draw_piece(&s.layout, fr, name, x, y, w, h, 1.0f);
    } else {
        const Uint8 light[3] = {GREY_LIGHT, GREY_LIGHT, GREY_LIGHT};
        khdays_layout_draw_tinted(&s.layout, fr, name, x, y, w, h, light, 1.0f);
    }
}

static void line(const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t, float dx, float dy)
{
    khdays_layout_draw_text(&s.layout, fr, style, t, dx, dy, 1.0f);
}

/* A place's circle: its colour in a white ring, its label in it. */
static void circle(const KhdaysLayoutFrame *fr, const char *style, int which, float cx, float cy, float r)
{
    char label[16];
    unsigned short wide[16];
    khdays_layout_fill_oval(s.layout.renderer, fr, cx, cy, r + RING, r + RING, WHITE, 1.0f);
    khdays_layout_fill_oval(s.layout.renderer, fr, cx, cy, r, r, KHDAYS_PLACE_COLOURS[which & 3], 1.0f);
    snprintf(label, sizeof(label), khdays_text(TXT_ROOM_PLACE), which + 1);
    khdays_layout_utf8(label, wide, 16);
    line(fr, style, wide, cx, cy);
}

/* Character `c`'s name (`record` NAMES) or its weapon's (WEAPONS): the game's text, or a mod's
 * character's in the game's language `language` (1-5), into `out` when it is one. */
static const unsigned short *named(int c, int record, int language, unsigned short *out, int room)
{
    const unsigned short *mod = khdays_mod_name(c, record == WEAPONS, language, out, room);
    return mod != NULL ? mod : khdays_layout_string(&s.strings, record + c);
}

/* The character a place is on as shown: this copy's as it moves. */
static int character_of(const KhdaysSelectView *v, int place)
{
    return place == v->self ? v->character : v->places[place].character;
}

/* A big "?" in the middle of a w x h box at (x, y): the random choice. */
static void question(const KhdaysLayoutFrame *fr, float x, float y, float w, float h, float grey)
{
    static const unsigned short mark[2] = {'?', 0};
    const float ink = s.layout.ink_bottom - s.layout.ink_top;
    const float size = ink > 0 ? QUESTION * h / ink : 1.0f;
    const float width = khdays_layout_line_width(&s.layout, mark, size);
    const float mx = x + (w - width) / 2, my = y + (h - ink * size) / 2;
    if (!s.layout.font_colours) {
        khdays_layout_draw_line(&s.layout, fr, mark, mx, my, size, grey, 1.0f, 1); /* its shadow */
    }
    khdays_layout_draw_line(&s.layout, fr, mark, mx, my, size, grey, 1.0f, 0);
}

static void draw_chosen(const KhdaysLayoutFrame *fr, const KhdaysSelectView *v)
{
    const float *f = place("face");
    const float *row = place("stat"), *bar = place("gauge");
    const int c = v->character;
    const float t = s.frame_place;
    if (v->random) {
        unsigned short name[32];
        khdays_layout_fill_rounded(s.layout.renderer, fr, f[0] - t, f[1] - t, f[2] + 2 * t, f[3] + 2 * t, 10, BLACK,
                                   PANEL_ALPHA);
        question(fr, f[0], f[1], f[2], f[3], 255.0f);
        khdays_layout_ring_rounded(s.layout.renderer, fr, f[0] - t, f[1] - t, f[2] + 2 * t, f[3] + 2 * t, 10, t,
                                   KHDAYS_PLACE_COLOURS[v->self & 3], 1.0f);
        khdays_layout_utf8(khdays_text(TXT_SELECT_RANDOM), name, 32);
        line(fr, "name", name, 0, 0);
        return;
    }
    unsigned short text[64];
    if (c < 0 || (c >= KHDAYS_SELECT_CHARACTERS && khdays_characters_numbered(c) == NULL)) {
        return;
    }
    khdays_layout_fill_rounded(s.layout.renderer, fr, f[0] - t, f[1] - t, f[2] + 2 * t, f[3] + 2 * t, 10, BLACK,
                               PANEL_ALPHA);
    face(fr, c, 1, f[0], f[1], f[2], f[3]);
    khdays_layout_ring_rounded(s.layout.renderer, fr, f[0] - t, f[1] - t, f[2] + 2 * t, f[3] + 2 * t, 10, t,
                               KHDAYS_PLACE_COLOURS[v->self & 3], 1.0f);
    line(fr, "name", named(c, NAMES, v->language, text, 64), 0, 0);
    line(fr, "weapon", named(c, WEAPONS, v->language, text, 64), 0, 0);
    for (int i = 0; i < KHDAYS_SELECT_STATS; ++i) {
        const float y = row[1] + (float)i * s.stat_pitch;
        khdays_layout_fill_rounded(s.layout.renderer, fr, row[0], y, row[2], row[3], row[3] / 2, BLACK, PANEL_ALPHA);
        khdays_layout_ring_rounded(s.layout.renderer, fr, row[0], y, row[2], row[3], row[3] / 2, EDGE, EDGE_GREY, 1.0f);
        line(fr, "stat", khdays_layout_string(&s.strings, v->stat_label[i]), row[0], y);
        for (int k = 0; k < s.bar_count; ++k) {
            const SDL_FRect r =
                khdays_layout_to_window(fr, row[0] + bar[0] + (float)k * s.gauge_pitch, y + bar[1], bar[2], bar[3]);
            const Uint8 *colour = k < v->stat[i] ? s.bars[k] : BAR_OFF;
            SDL_SetRenderDrawColor(s.layout.renderer, colour[0], colour[1], colour[2], 255);
            SDL_RenderFillRect(s.layout.renderer, &r);
        }
    }
}

static void draw_grid(const KhdaysLayoutFrame *fr, const KhdaysSelectView *v)
{
    const float *tile = place("tile"), *tag_at = place("tile_tag");
    int count = 0; /* the characters before the random choice */
    while (khdays_charselect_at(v, count) >= 0 && khdays_charselect_at(v, count) != KHDAYS_SELECT_CHARACTERS) {
        ++count;
    }
    for (int p = 0; p <= count; ++p) {
        /* the characters, then the random choice (charselect.c's grid has it last too) */
        const int row = p / s.columns, in_row = SDL_min(s.columns, count + 1 - row * s.columns);
        const float left = tile[0] + (float)(s.columns - in_row) * (tile[2] + s.gap) / 2;
        const float x = left + (float)(p % s.columns) * (tile[2] + s.gap), y = tile[1] + (float)row * (tile[3] + s.gap);
        int on[KHDAYS_SELECT_PLACES], n = 0;
        if (p == count) {
            const float t = v->random ? s.frame_place : s.frame_other;
            khdays_layout_fill_rounded(s.layout.renderer, fr, x - t, y - t, tile[2] + 2 * t, tile[3] + 2 * t, 8, BLACK,
                                       TILE_ALPHA);
            question(fr, x, y, tile[2], tile[3], v->random ? 255.0f : (float)GREY_LIGHT);
            khdays_layout_draw_action(&s.layout, fr, KHDAYS_ACT_Y, x + tile[2] - KEY_SIZE - 8,
                                      y + tile[3] - KEY_SIZE - 8, KEY_SIZE, 1.0f);
            khdays_layout_ring_rounded(s.layout.renderer, fr, x - t, y - t, tile[2] + 2 * t, tile[3] + 2 * t, 8, t,
                                       v->random ? KHDAYS_PLACE_COLOURS[v->self & 3] : OTHERS_GREY, 1.0f);
            if (v->random) {
                circle(fr, "tag", v->self, x + tag_at[0], y + tag_at[1], tag_at[2]);
            }
            break;
        }
        for (int i = 0; i < KHDAYS_SELECT_PLACES; ++i) {
            if ((v->places[i].present || i == v->self) && character_of(v, i) == khdays_charselect_at(v, p)) {
                on[n++] = i;
            }
        }
        {
            const float t = n > 0 ? s.frame_place : s.frame_other;
            khdays_layout_fill_rounded(s.layout.renderer, fr, x - t, y - t, tile[2] + 2 * t, tile[3] + 2 * t, 8, BLACK,
                                       TILE_ALPHA);
            face(fr, khdays_charselect_at(v, p), n > 0, x, y, tile[2], tile[3]);
            khdays_layout_ring_rounded(s.layout.renderer, fr, x - t, y - t, tile[2] + 2 * t, tile[3] + 2 * t, 8, t,
                                       n > 0 ? KHDAYS_PLACE_COLOURS[on[0] & 3] : OTHERS_GREY, 1.0f);
        }
        for (int k = 0; k < n; ++k) {
            circle(fr, "tag", on[k], x + tag_at[0] + (float)k * (tag_at[2] * 2 + RING * 2 + 6), y + tag_at[1],
                   tag_at[2]);
        }
    }
}

static void draw_places(const KhdaysLayoutFrame *fr, const KhdaysSelectView *v)
{
    const float *plate = place("plate"), *ring = place("plate_circle"), *small = place("plate_face");
    int k = 0;
    for (int i = 0; i < KHDAYS_SELECT_PLACES; ++i) {
        const KhdaysSelectPlace *pl = &v->places[i];
        const int c = character_of(v, i);
        float x, y;
        if (!pl->present && i != v->self) {
            continue; /* the game shows the places there, packed (Ov006_SetMissionRowSlotValue) */
        }
        x = plate[0] + (float)k++ * s.plate_pitch;
        y = plate[1];
        khdays_layout_fill_rounded(s.layout.renderer, fr, x, y, plate[2], plate[3], plate[3] / 2, BLACK, PANEL_ALPHA);
        khdays_layout_ring_rounded(s.layout.renderer, fr, x, y, plate[2], plate[3], plate[3] / 2, pl->ready ? 4 : EDGE,
                                   pl->ready ? KHDAYS_PLACE_COLOURS[i & 3] : EDGE_GREY, 1.0f);
        circle(fr, "plate_tag", i, x + ring[0], y + ring[1], ring[2]);
        if (c >= 0 && (c < KHDAYS_SELECT_CHARACTERS || khdays_characters_numbered(c) != NULL)) {
            unsigned short text[64];
            face(fr, c, 1, x + small[0], y + small[1], small[2], small[3]);
            if (pl->name[0] != 0) {
                line(fr, "plate_player", pl->name, x, y);
                line(fr, "plate_character", named(c, NAMES, v->language, text, 64), x, y);
            } else {
                line(fr, "plate_alone", named(c, NAMES, v->language, text, 64), x, y);
            }
        }
    }
}

int khdays_select_screen_draw(SDL_Renderer *renderer)
{
    KhdaysSelectView v;
    KhdaysLayoutFrame fr;
    khdays_charselect_view(&v);
    if (!v.shown) {
        if (s.ok) {
            s.tried = 0;
            s.ok = 0;
            forget();
        }
        return 0;
    }
    if (renderer != s.layout.renderer || v.language != s.tried) {
        s.tried = v.language;
        s.ok = load(renderer, v.language);
    }
    if (!s.ok) {
        return 0;
    }
    khdays_layout_begin(renderer, &fr);
    if (s.art != NULL) {
        const SDL_FRect all = khdays_layout_to_window(&fr, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H);
        SDL_RenderTexture(renderer, s.art, NULL, &all);
    }
    khdays_layout_draw_piece(&s.layout, &fr, "veil", 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H, 1.0f);
    line(&fr, "heading", khdays_layout_string(&s.strings, v.heading), 0, 0);
    line(&fr, "help", khdays_layout_string(&s.strings, v.help), 0, 0);
    {
        const float *rule = place("rule");
        if (rule[2] > 0) {
            khdays_layout_fill_rounded(renderer, &fr, rule[0], rule[1], rule[2], rule[3], 0, WHITE, 0.47f);
        }
    }
    draw_chosen(&fr, &v);
    draw_grid(&fr, &v);
    draw_places(&fr, &v);
    khdays_layout_shade(renderer, v.master);
    khdays_layout_end(renderer, &fr);
    return 1;
}

void khdays_select_screen_renderer_gone(void)
{
    forget();
    s.tried = 0;
    s.ok = 0;
    s.layout.renderer = NULL;
    khdays_layout_renderer_gone();
}
