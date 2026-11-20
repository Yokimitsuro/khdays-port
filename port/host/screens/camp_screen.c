/* Mission Mode's camp menu on one screen (camp_screen.h), in DDD's camp pieces: a file
 * tools/hd/ddd_camp.py writes (its lines: docs/HD_SCREENS.md, "The camp menu"). It shows the game's
 * menu as runtime/menus/camp.c reads it -- options, description, the character's face and numbers, the
 * members, the game's line -- and the Preferences page in the model's and panel's place. */
#include "camp_screen.h"
#include "camp_look.h"

#include "../app/devices.h"
#include "../render/gpu_present.h"
#include "layout_screen.h"
#include "../app/places.h"
#include "../render/png_io.h"
#include "../app/profile.h"
#include "ui_text.h"
#include "mod_faces.h"
#include "../../runtime/menus/camp.h"
#include "../../runtime/core/rom.h"
#include "../../runtime/mods/installed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LABEL_HEARTS 11
#define SAV_YES 5  /* UI/cm/str/sav_&.s: "Yes", then "No" */
#define SAV_NO 6
#define DIMMED 0.4f  /* the options while the question is open */
#define CFG_HIDDEN 3 /* UI/cm/str/cfg_&.s: "?", a setting the game has off */
#define FACE_RING 4.0f
/* the level's four numbers (labels 0-3), then the rows' (labels, the fields that draw them) */
static const int LEVEL_FIELDS[4] = {2, 3, 5, 6};
static const int ROW_LABELS[9] = {4, 5, 6, 7, 8, 9, 10, 12, 14};
static const int ROW_FIELDS[9] = {7, 9, 0xb, 0xc, 0xd, 0xe, 0xf, 0x11, 0x13};

/* What the one-screen picture needs, read for a language without the renderer (it may ask before
 * the screen draws). */
typedef struct Plan {
    int tried, ok;
    float source[4], view[4], window[4];
    float story_source[4], story_view[4];
    uint8_t *backdrop; /* kept while the port runs: the picture's card keeps it by its address */
    int w, h;
    char backdrop_path[1300];
    uint8_t *page_backdrop; /* the Preferences page's, the same */
    int page_w, page_h;
} Plan;

static Plan plans[LANGUAGES];

static KhdaysCampLook s;

static void plan_line(char *line, const char *folder, void *ctx)
{
    Plan *p = (Plan *)ctx;
    float a, b, c, d;
    char file[256];
    if (SDL_sscanf(line, "model_source %f %f %f %f", &a, &b, &c, &d) == 4) {
        p->source[0] = a, p->source[1] = b, p->source[2] = c, p->source[3] = d;
    } else if (SDL_sscanf(line, "model_view %f %f %f %f", &a, &b, &c, &d) == 4) {
        p->view[0] = a, p->view[1] = b, p->view[2] = c, p->view[3] = d;
    } else if (SDL_sscanf(line, "story_model_source %f %f %f %f", &a, &b, &c, &d) == 4) {
        p->story_source[0] = a, p->story_source[1] = b, p->story_source[2] = c, p->story_source[3] = d;
    } else if (SDL_sscanf(line, "story_model_view %f %f %f %f", &a, &b, &c, &d) == 4) {
        p->story_view[0] = a, p->story_view[1] = b, p->story_view[2] = c, p->story_view[3] = d;
    } else if (SDL_sscanf(line, "model %f %f %f %f", &a, &b, &c, &d) == 4) {
        p->window[0] = a, p->window[1] = b, p->window[2] = c, p->window[3] = d;
    } else if (SDL_sscanf(line, "backdrop %255s", file) == 1 && p->backdrop == NULL) {
        const char *error = NULL;
        snprintf(p->backdrop_path, sizeof(p->backdrop_path), "%s%s", folder, file);
        p->backdrop = khdays_png_read(p->backdrop_path, &p->w, &p->h, &error);
        if (p->backdrop == NULL) {
            SDL_Log("camp menu: %s: %s", p->backdrop_path, error != NULL ? error : "not read");
        }
    } else if (SDL_sscanf(line, "page_backdrop %255s", file) == 1 && p->page_backdrop == NULL) {
        char at[1300];
        const char *error = NULL;
        snprintf(at, sizeof(at), "%s%s", folder, file);
        p->page_backdrop = khdays_png_read(at, &p->page_w, &p->page_h, &error);
        if (p->page_backdrop == NULL) {
            SDL_Log("camp menu: %s: %s", at, error != NULL ? error : "not read");
        }
    }
}

static const Plan *plan(int language)
{
    char path[1024], folder[1024];
    Plan *p;
    if (language < 1 || language >= LANGUAGES) {
        return NULL;
    }
    p = &plans[language];
    if (!p->tried) {
        p->tried = 1;
        if (khdays_camp_open_file(language, path, (int)sizeof(path), folder, (int)sizeof(folder)) &&
            khdays_camp_each_line(path, plan_line, folder, p)) {
            p->ok = p->backdrop != NULL && p->source[2] > 0 && p->source[3] > 0 && p->window[2] > 0 && p->window[3] > 0;
            if (p->view[2] <= 0 || p->view[3] <= 0) {
                memcpy(p->view, p->source, sizeof(p->view));
            }
            if (p->story_source[2] <= 0 || p->story_source[3] <= 0) {
                memcpy(p->story_source, p->source, sizeof(p->story_source));
            }
            if (p->story_view[2] <= 0 || p->story_view[3] <= 0) {
                memcpy(p->story_view, p->story_source, sizeof(p->story_view));
            }
            if (!p->ok) {
                SDL_Log("camp menu: %s has no backdrop, model or model_source", path);
            }
        }
    }
    return p->ok ? p : NULL;
}

static void forget(void)
{
    if (s.backdrop != NULL) {
        SDL_DestroyTexture(s.backdrop);
        s.backdrop = NULL;
    }
    for (int i = 0; i < KHDAYS_CAMP_OPTIONS; ++i) {
        SDL_free(s.option[i]);
        SDL_free(s.story_option[i]);
        s.option[i] = s.story_option[i] = NULL;
    }
    for (int i = 0; i < 4; ++i) {
        SDL_free(s.story_sub[i]);
        s.story_sub[i] = NULL;
    }
    SDL_free(s.menu_place);
    SDL_free(s.mode_label);
    SDL_free(s.mission_mode);
    s.menu_place = s.mode_label = s.mission_mode = NULL;
    for (int i = 0; i < 3; ++i) {
        SDL_free(s.difficulty[i]);
        s.difficulty[i] = NULL;
    }
    khdays_mod_faces_forget(&s.faces);
    khdays_layout_forget(&s.layout);
    khdays_layout_strings_free(&s.status);
    khdays_layout_strings_free(&s.root);
    khdays_layout_strings_free(&s.mlt);
    khdays_layout_strings_free(&s.info);
    khdays_layout_strings_free(&s.sav);
    khdays_layout_strings_free(&s.cfg);
    memset(s.page_rows, 0, sizeof(s.page_rows));
}

static int load(SDL_Renderer *renderer, int language)
{
    static KhdaysRomReader *reader; /* its own: the window's thread */
    char path[1024], folder[1024], table[96];
    const Plan *p;
    int w, h;
    forget();
    s.layout.who = "camp menu";
    s.layout.renderer = renderer;
    if ((p = plan(language)) == NULL || !khdays_camp_open_file(language, path, (int)sizeof(path), folder,
                                                               (int)sizeof(folder))) {
        return 0;
    }
    memcpy(s.window, p->window, sizeof(s.window));
    s.backdrop = khdays_layout_texture(&s.layout, p->backdrop_path, 0, &w, &h);
    s.bars[3] = 0;
    khdays_camp_each_line(path, khdays_camp_screen_line, folder, &s);
    khdays_camp_notice_style(&s);
    if (!khdays_layout_ready(&s.layout) || s.backdrop == NULL || s.bars[3] <= 0 || s.panel[2] <= 0 ||
        khdays_layout_piece(&s.layout, "bar") == NULL || khdays_layout_piece(&s.layout, "bar_selected") == NULL) {
        SDL_Log("camp menu: %s is incomplete", path);
        forget();
        return 0;
    }
    if (reader == NULL) {
        reader = khdays_rom_reader_new();
    }
    snprintf(table, sizeof(table), "UI/cm/str/status_%s.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &s.status, s.layout.who);
    snprintf(table, sizeof(table), "UI/cm/str/root_%s.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &s.root, s.layout.who);
    snprintf(table, sizeof(table), "UI/mlt/mlt_%s.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &s.mlt, s.layout.who);
    snprintf(table, sizeof(table), "UI/btl/%s/info.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &s.info, s.layout.who);
    snprintf(table, sizeof(table), "UI/cm/str/sav_%s.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &s.sav, s.layout.who);
    snprintf(table, sizeof(table), "UI/cm/str/cfg_%s.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &s.cfg, s.layout.who);
    return 1;
}

/* Whether the one-screen picture took the model's window when it was made last. */
static int applied;

void khdays_camp_screen_applied(int yes)
{
    if (khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        khdays_overhaul_camp_applied(yes); /* the Menu Overhaul's camp (overhaul_camp_screen.c) */
        return;
    }
    applied = yes;
}

int khdays_camp_screen_model(KhdaysCampModel *out)
{
    KhdaysCampView v;
    const Plan *p;
    if (khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return khdays_overhaul_camp_model(out);
    }
    khdays_camp_view(&v);
    if (!v.shown || (p = plan(v.language)) == NULL || (s.tried == v.language && !s.ok) ||
        (v.config && p->page_backdrop == NULL)) {
        return 0;
    }
    memcpy(out->source, v.story ? p->story_source : p->source, sizeof(out->source));
    memcpy(out->view, v.story ? p->story_view : p->view, sizeof(out->view));
    memcpy(out->window, p->window, sizeof(out->window));
    out->backdrop = v.config ? p->page_backdrop : p->backdrop;
    out->backdrop_w = v.config ? p->page_w : p->w;
    out->backdrop_h = v.config ? p->page_h : p->h;
    out->model = !v.config;
    return 1;
}

/* ---- Drawing --------------------------------------------------------------------------------- */

/* DDD's big digits, right-aligned at x, `h` high from y, in a colour. */
static void big_digits(const KhdaysLayoutFrame *fr, uint32_t value, const float *at, const Uint8 tint[3])
{
    char digits[16];
    float w = 0, x;
    const float h = at[3];
    snprintf(digits, sizeof(digits), "%u", value);
    for (const char *c = digits; *c != 0; ++c) {
        char name[16];
        const KhdaysLayoutPiece *p;
        snprintf(name, sizeof(name), "digit%c", *c);
        p = khdays_layout_piece(&s.layout, name);
        w += p != NULL ? p->w * h / p->h - h * s.overlap : 0;
    }
    x = at[0] - w;
    for (const char *c = digits; *c != 0; ++c) {
        char name[16];
        const KhdaysLayoutPiece *p;
        snprintf(name, sizeof(name), "digit%c", *c);
        p = khdays_layout_piece(&s.layout, name);
        if (p != NULL) {
            const float pw = p->w * h / p->h;
            khdays_layout_draw_tinted(&s.layout, fr, name, x, at[1], pw, h, tint, 1.0f);
            x += pw - h * s.overlap;
        }
    }
}

static void draw_options(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const float *lit = khdays_camp_place(&s, "bar_selected"), *body = khdays_camp_place(&s, "bar_body"),
        *glove = khdays_camp_place(&s, "glove");
    const float bx = s.bars[0], by = s.bars[1], pitch = s.bars[3], bh = s.bars[4];
    /* the story's Holo-Missions and Files show their two options in the list's place, as the game's */
    unsigned short *const *names = v->sub != 0 ? s.story_sub + 2 * (v->sub - 1) : v->story ? s.story_option : s.option;
    const int rows = v->sub != 0 ? 2 : KHDAYS_CAMP_OPTIONS;
    for (int i = 0; i < rows; ++i) {
        /* each row's bar, the selected one's lit (alone: the other showed round its edge); all
         * dimmed while the question is open, but Preferences on its page */
        const float alpha = v->question || v->waiting || (v->config && i != v->selected) ? DIMMED : 1.0f;
        if (i == v->selected) {
            khdays_layout_draw_piece(&s.layout, fr, "bar_selected", bx + lit[0], by + (float)i * pitch + lit[1], lit[2],
                                     lit[3], alpha);
        } else {
            khdays_layout_draw_piece(&s.layout, fr, "bar", bx, by + (float)i * pitch, s.bars[2], bh, alpha);
        }
    }
    for (int i = 0; i < rows; ++i) {
        /* the name's capitals' middle on the bar's drawing's; an option the game has locked its name
         * dimmed, as the DS's */
        const float alpha =
            v->question || v->waiting || v->locked[i] || (v->config && i != v->selected) ? DIMMED : 1.0f;
        khdays_layout_draw_text(&s.layout, fr, "option", names[i], bx,
                                by + (float)i * pitch + body[1] + body[3] / 2 - khdays_camp_capitals(&s, "option") / 2,
                                    alpha);
    }
    /* the hand at the selected one, as the DS's (at the question's answer while it is open, at the
     * Preferences page's setting on its page) */
    if (!v->question && !v->config && v->selected >= 0) {
        khdays_layout_draw_piece(&s.layout, fr, "glove", bx + body[0] + glove[0],
                                 by + (float)v->selected * pitch + body[1] + body[3] / 2 + glove[1], glove[2], glove[3],
                                 1.0f);
    }
}

/* Title Screen's question's two answers, side by side under it: DDD's buttons, the game's words,
 * the hand at the one the cursor is on. */
static void draw_answers(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    static const char *const names[2][3] = {{"button_yes", "button_selected_yes", "button_yes"},
                                            {"button_no", "button_selected_no", "button_no"}};
    const float *glove = khdays_camp_place(&s, "glove");
    for (int b = 0; b < 2; ++b) {
        const int on = b == 0 ? v->yes : !v->yes;
        const float *at = khdays_camp_place(&s, names[b][2]);
        if (at[2] <= 0) {
            continue;
        }
        khdays_layout_draw_piece(&s.layout, fr, names[b][on], at[0], at[1], at[2], at[3], 1.0f);
        /* a centred style's place is its words' middle */
        khdays_camp_text(&s, fr, "button", khdays_layout_string(&s.sav, b == 0 ? SAV_YES : SAV_NO), at[0] + at[2] / 2,
             at[1] + at[3] / 2);
        if (on) {
            khdays_layout_draw_piece(&s.layout, fr, "glove", at[0] + glove[0], at[1] + at[3] / 2 + glove[1], glove[2],
                                     glove[3], 1.0f);
        }
    }
}

/* A record's lines (as the game breaks them) in a style, `pitch` apart. */
static void draw_words(const KhdaysLayoutFrame *fr, const unsigned short *t, const char *style, float pitch)
{
    unsigned short line[256];
    float dy = 0;
    if (t == NULL) {
        return;
    }
    /* its lines, as the game breaks them */
    while (*t != 0) {
        int n = 0;
        while (t[n] != 0 && t[n] != '\n') {
            ++n;
        }
        for (int at = 0; at < n; at += 255) {
            const int take = SDL_min(255, n - at);
            memcpy(line, t + at, (size_t)take * sizeof(unsigned short));
            line[take] = 0;
            khdays_camp_text(&s, fr, style, line, 0, dy);
        }
        dy += pitch;
        t += n;
        if (*t == '\n') {
            ++t;
        }
    }
}

static void draw_lines(const KhdaysLayoutFrame *fr, const KhdaysLayoutStrings *table, int record, const char *style,
                       float pitch)
{
    draw_words(fr, khdays_layout_string(table, record), style, pitch);
}

static void draw_description(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    if (v->question) {
        /* Title Screen's question in the port's words (see above) */
        const char *words = khdays_text_in(TXT_CAMP_BACK_TO_MENU, v->language);
        unsigned short line[128];
        if (words != NULL) {
            khdays_layout_utf8(words, line, 128);
            khdays_camp_text(&s, fr, "description", line, 0, 0);
            return;
        }
    }
    draw_lines(fr, &s.root, v->description, "description", s.description_pitch);
}


/* The Preferences page's: its sections' tabs, the section's settings, the hand, the scroll bar. */
static void draw_page(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const KhdaysCampPage *pg = &v->page;
    const float *tabs = s.page_tabs, *rows = s.page_rows;
    const float *l = khdays_camp_place(&s, "page_l"), *r = khdays_camp_place(&s, "page_r"),
        *glove = khdays_camp_place(&s, "page_glove"), *arrow = khdays_camp_place(&s, "page_arrow");
    const float gap = khdays_camp_place(&s, "page_value_gap")[0];
    const KhdaysLayoutPiece *left = khdays_layout_piece(&s.layout, "page_arrow_left");
    const KhdaysLayoutPiece *right = khdays_layout_piece(&s.layout, "page_arrow_right");
    const int section = pg->section, first = pg->first[section], count = pg->count[section];
    const int shown = (int)rows[5];
    /* the sections between L and R, as the DS's tabs */
    khdays_layout_draw_action(&s.layout, fr, KHDAYS_ACT_L, l[0], l[1] - l[3] / 2, l[3], 1.0f);
    khdays_layout_draw_action(&s.layout, fr, KHDAYS_ACT_R, r[0], r[1] - r[3] / 2, r[3], 1.0f);
    for (int i = 0; i < pg->sections; ++i) {
        const int on = i == section;
        const float x = tabs[0] + (float)i * tabs[4], y = tabs[1];
        const float *at = khdays_camp_place(&s, on ? "page_tab_selected" : "page_tab");
        khdays_layout_draw_piece(&s.layout, fr, on ? "page_tab_selected" : "page_tab", x + at[0], y + at[1], at[2],
                                 at[3], 1.0f);
        khdays_camp_text(&s, fr, on ? "page_tab_on" : "page_tab", khdays_layout_string(&s.cfg, pg->title[i]),
                         x + tabs[2] / 2,
             y + tabs[3] / 2);
    }
    /* the section's settings, the chosen one in sight */
    if (pg->chosen >= first && pg->chosen < first + count) {
        if (pg->chosen - first < s.page_top) {
            s.page_top = pg->chosen - first;
        } else if (pg->chosen - first >= s.page_top + shown) {
            s.page_top = pg->chosen - first - shown + 1;
        }
    }
    if (s.page_top > count - shown) {
        s.page_top = count - shown;
    }
    if (s.page_top < 0) {
        s.page_top = 0;
    }
    for (int i = 0; i < shown && s.page_top + i < count; ++i) {
        const int n = first + s.page_top + i, on = n == pg->chosen;
        const KhdaysCampSetting *st = &pg->setting[n];
        const float x = rows[0], y = rows[1] + (float)i * rows[4], mid = y + rows[3] / 2;
        const float *at = khdays_camp_place(&s, on ? "page_row_selected" : "page_row");
        const float fade = st->enabled ? 1.0f : DIMMED; /* its arrows: the game changes it not */
        const int value = st->value >= 0 && st->value < st->values ? st->value : 0;
        float vx = x + arrow[0];
        khdays_layout_draw_piece(&s.layout, fr, on ? "page_row_selected" : "page_row", x + at[0], y + at[1], at[2],
                                 at[3], 1.0f);
        khdays_camp_text(&s, fr, "setting", khdays_layout_string(&s.cfg, st->enabled ? st->name : CFG_HIDDEN), x,
             mid - khdays_camp_capitals(&s, "setting") / 2);
        if (left != NULL && right != NULL) {
            const float aw = left->w * arrow[3] / left->h;
            unsigned short own[32];
            const unsigned short *found =
                khdays_camp_setting_words(&s, st->enabled ? st->value_name[value] : CFG_HIDDEN, v->language, own, 32);
            static const unsigned short none[1] = {0};
            const unsigned short *words = found != NULL ? found : none;
            khdays_layout_draw_piece(&s.layout, fr, "page_arrow_left", vx, mid - arrow[3] / 2, aw, arrow[3], fade);
            vx += aw + gap;
            khdays_camp_text(&s, fr, "setting_value", words, vx, mid - khdays_camp_capitals(&s, "setting_value") / 2);
            vx += khdays_camp_width_in(&s, "setting_value", words) + gap;
            khdays_layout_draw_piece(&s.layout, fr, "page_arrow_right", vx, mid - arrow[3] / 2,
                                     right->w * arrow[3] / right->h, arrow[3], fade);
        }
        if (on) {
            khdays_layout_draw_piece(&s.layout, fr, "glove", x + glove[0], mid + glove[1], glove[2], glove[3], 1.0f);
        }
    }
    if (count > shown) {
        /* a scroll bar (DDD's save screen's): its track, its thumb between the room its arrows
         * take above and below, as far down as the rows are */
        const float *track = khdays_camp_place(&s, "page_track"), *thumb = khdays_camp_place(&s, "page_thumb");
        const float travel = track[3] - 2 * thumb[1] - thumb[3];
        khdays_layout_draw_piece(&s.layout, fr, "page_track", track[0], track[1], track[2], track[3], 1.0f);
        khdays_layout_draw_piece(&s.layout, fr, "page_thumb", track[0] + thumb[0],
                                 track[1] + thumb[1] + travel * (float)s.page_top / (float)(count - shown), thumb[2],
                                 thumb[3], 1.0f);
    }
}

/* The chosen setting's help and its value's (yellow) in the description's place: "?" alone while
 * the game has it off (Ov025_DrawMenuPageTexts's). */
static void draw_setting_help(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const KhdaysCampPage *pg = &v->page;
    const KhdaysCampSetting *st;
    if (pg->chosen < 0 || pg->chosen >= pg->settings) {
        return;
    }
    st = &pg->setting[pg->chosen];
    if (!st->enabled) {
        draw_lines(fr, &s.cfg, CFG_HIDDEN, "description", s.description_pitch);
        return;
    }
    draw_lines(fr, &s.cfg, st->help, "description", s.description_pitch);
    if (st->value >= 0 && st->value < st->values) {
        unsigned short own[160];
        draw_words(fr, khdays_camp_setting_words(&s, st->value_help[st->value], v->language, own, 160), "detail",
                   s.detail_pitch);
    }
}

static void draw_prompt(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    unsigned short line[128];
    if (v->question || v->config) {
        /* the question and the Preferences page take B back: the game's prompt whole */
        khdays_camp_text(&s, fr, "prompt", khdays_layout_string(&s.mlt, PROMPT), 0, 0);
        return;
    }
    khdays_camp_prompt_part(&s, line, 128, 0);
    khdays_camp_text(&s, fr, "prompt", line, 0, 0);
}

static void draw_status(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const float px = s.panel[0], py = s.panel[1];
    const float *f = khdays_camp_place(&s, "face"), *ring = khdays_camp_place(&s, "face_ring"),
        *tag = khdays_camp_place(&s, "tag");
    const float *level = khdays_camp_place(&s, "level"), *rows = khdays_camp_place(&s, "stats");
    /* the place's colour (the story's, alone: the first place's, as Mission Mode's alone) */
    const Uint8 *own = KHDAYS_PLACE_COLOURS[v->story ? 0 : v->self & 3];
    char value[32];
    if (v->character >= 0) {
        unsigned short mod_name[64];
        const unsigned short *named = khdays_mod_name(v->character, 0, v->language, mod_name, 64);
        if (named != NULL) {
            /* a mod's character: its own picture (none drawn without one) and name */
            khdays_mod_face_draw(&s.faces, &s.layout, fr, v->character, px + f[0], py + f[1], f[2], f[3], 255);
            khdays_camp_text(&s, fr, "name", named, px, py);
        } else {
            char name[16];
            snprintf(name, sizeof(name), "face%d", v->character);
            khdays_layout_draw_piece(&s.layout, fr, name, px + f[0], py + f[1], f[2], f[3], 1.0f);
            khdays_camp_text(&s, fr, "name", khdays_layout_string(&s.status, NAMES + v->character), px, py);
        }
    }
    khdays_layout_ring_rounded(s.layout.renderer, fr, px + ring[0], py + ring[1], ring[2], ring[3], 0, FACE_RING, own,
                               1.0f);
    if (!v->story) {
        char label[16];
        unsigned short wide[16];
        const float cx = px + tag[0], cy = py + tag[1], r = tag[2];
        khdays_layout_fill_oval(s.layout.renderer, fr, cx, cy, r + RING, r + RING, WHITE, 1.0f);
        khdays_layout_fill_oval(s.layout.renderer, fr, cx, cy, r, r, own, 1.0f);
        snprintf(label, sizeof(label), khdays_text(TXT_ROOM_PLACE), v->self + 1);
        khdays_layout_utf8(label, wide, 16);
        khdays_camp_text(&s, fr, "tag", wide, cx, cy);
    }
    for (int i = 0; i < 4; ++i) {
        const float dx = px + level[0] + (float)(i % 2) * level[2], dy = py + level[1] + (float)(i / 2) * level[3];
        khdays_camp_text(&s, fr, "level_label", khdays_layout_string(&s.status, i), dx, dy);
        khdays_camp_value_text(v, LEVEL_FIELDS[i], value, sizeof(value));
        khdays_camp_number(&s, fr, "level_value", value, dx, dy);
    }
    for (int i = 0; i < 9; ++i) {
        const float dy = py + rows[1] + (float)i * rows[3];
        khdays_camp_text(&s, fr, "stat_label", khdays_layout_string(&s.status, ROW_LABELS[i]), px, dy);
        if (ROW_FIELDS[i] == 0x13) {
            if (v->field[0x13].has) {
                khdays_camp_text(&s, fr, "stat_value", khdays_layout_string(&s.status, (int)v->field[0x13].value[0]),
                                 px, dy);
            }
        } else {
            khdays_camp_value_text(v, ROW_FIELDS[i], value, sizeof(value));
            khdays_camp_number(&s, fr, "stat_value", value, px, dy);
        }
    }
    {
        /* the game mode, the last row: the story's difficulty, Mission Mode's own */
        const float dy = py + rows[1] + 9.0f * rows[3];
        khdays_camp_text(&s, fr, "stat_label", s.mode_label, px, dy);
        khdays_camp_text(&s, fr, "stat_value",
             v->story ? (v->difficulty >= 0 && v->difficulty < 3 ? s.difficulty[v->difficulty] : NULL) : s.mission_mode,
             px, dy);
    }
}

static void draw_settings_key(const KhdaysLayoutFrame *fr)
{
    const float *at = khdays_camp_place(&s, "settings");
    const KhdaysLayoutPiece *esc = khdays_layout_piece(&s.layout, "settings_esc");
    const KhdaysLayoutPiece *gear = khdays_layout_piece(&s.layout, "settings_gear");
    const float h = at[3];
    float x;
    if (gear == NULL || esc == NULL) {
        return;
    }
    x = at[0] - gear->w * h / gear->h;
    khdays_layout_draw_piece(&s.layout, fr, "settings_gear", x, at[1], gear->w * h / gear->h, h, 1.0f);
    if (khdays_devices_gamepad() != NULL && khdays_devices_pad_in_use()) {
        /* the pad's input for the settings menu (host/app/profile.c), its icon as tall as the key's
         * (measured first: drawn with no alpha) */
        const float key_h = h * 0.6f, y = at[1] + (h - key_h) / 2;
        const float w = khdays_layout_draw_action(&s.layout, fr, KHDAYS_ACT_OPEN_SETTINGS, 0, 0, key_h, 0.0f);
        khdays_layout_draw_action(&s.layout, fr, KHDAYS_ACT_OPEN_SETTINGS, x - w - h * 0.05f, y, key_h, 1.0f);
    } else {
        /* the keyboard's: Esc, always (KHDAYS_SETTINGS_KEY) */
        const float w = esc->w * h / esc->h;
        khdays_layout_draw_piece(&s.layout, fr, "settings_esc", x - w, at[1], w, h, 1.0f);
    }
}

int khdays_camp_screen_draw(SDL_Renderer *renderer)
{
    KhdaysCampView v;
    KhdaysLayoutFrame fr;
    if (khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return khdays_overhaul_camp_draw(renderer);
    }
    khdays_camp_view(&v);
    if (!v.shown || !applied || !khdays_present_one_picture()) {
        khdays_camp_set_page_drawn(0);
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
    if (!s.ok || (v.config &&
                  (s.page_rows[5] <= 0 || s.page_tabs[2] <= 0 || khdays_layout_piece(&s.layout, "page_row") == NULL))) {
        khdays_camp_set_page_drawn(0);
        return 0;
    }
    /* once a frame (the game's thread reads it at its pad's sample) */
    khdays_camp_set_page_drawn(v.config);
    khdays_layout_begin_over(renderer, &fr);
    if (v.config) {
        /* the Preferences page: its own backdrop is the picture (no model to cut) */
        draw_options(&fr, &v);
        big_digits(&fr, v.munny, khdays_camp_place(&s, "munny"), s.munny_colour);
        khdays_camp_text(&s, &fr, "hearts_label", khdays_layout_string(&s.status, LABEL_HEARTS), 0, 0);
        big_digits(&fr, v.hearts, khdays_camp_place(&s, "hearts"), s.hearts_colour);
        draw_page(&fr, &v);
        draw_setting_help(&fr, &v);
        draw_prompt(&fr, &v);
        draw_settings_key(&fr);
        if (!v.story) {
            khdays_camp_draw_members(&s, &fr, &v);
        }
        khdays_layout_shade(renderer, v.master);
        khdays_layout_end(renderer, &fr);
        return 1;
    }
    s.page_top = 0;
    {
        /* the backdrop again around the model's window: the model shows only in it, as the DS's
         * frame cuts it (its weapons reach out of it) */
        const float *m = s.window;
        const float strips[4][4] = {{0, 0, KHDAYS_LAYOUT_W, m[1]},
                                    {0, m[1] + m[3], KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H - m[1] - m[3]},
                                    {0, m[1], m[0], m[3]},
                                    {m[0] + m[2], m[1], KHDAYS_LAYOUT_W - m[0] - m[2], m[3]}};
        float tw = 0, th = 0;
        SDL_GetTextureSize(s.backdrop, &tw, &th);
        for (int i = 0; i < 4; ++i) {
            const float *r = strips[i];
            const SDL_FRect src = {r[0] * tw / KHDAYS_LAYOUT_W, r[1] * th / KHDAYS_LAYOUT_H,
                                   r[2] * tw / KHDAYS_LAYOUT_W, r[3] * th / KHDAYS_LAYOUT_H};
            const SDL_FRect dst = khdays_layout_to_window(&fr, r[0], r[1], r[2], r[3]);
            if (r[2] > 0 && r[3] > 0) {
                SDL_RenderTexture(renderer, s.backdrop, &src, &dst);
            }
        }
    }
    draw_options(&fr, &v);
    draw_status(&fr, &v);
    khdays_camp_draw_trials(&s, &fr, &v);
    big_digits(&fr, v.munny, khdays_camp_place(&s, "munny"), s.munny_colour);
    khdays_camp_text(&s, &fr, "hearts_label", khdays_layout_string(&s.status, LABEL_HEARTS), 0, 0);
    big_digits(&fr, v.hearts, khdays_camp_place(&s, "hearts"), s.hearts_colour);
    draw_description(&fr, &v);
    if (v.question) {
        draw_answers(&fr, &v);
    }
    draw_prompt(&fr, &v);
    draw_settings_key(&fr);
    if (v.story) {
        khdays_camp_draw_day(&s, &fr, &v);
    } else {
        khdays_camp_draw_members(&s, &fr, &v);
        khdays_camp_draw_notice(&s, &fr, &v);
    }
    khdays_layout_shade(renderer, v.master);
    khdays_layout_end(renderer, &fr);
    return 1;
}

void khdays_camp_screen_renderer_gone(void)
{
    khdays_overhaul_camp_renderer_gone();
    forget();
    s.tried = 0;
    s.ok = 0;
    s.layout.renderer = NULL;
    khdays_layout_renderer_gone();
}
