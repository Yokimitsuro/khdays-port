/* Mission Mode's camp menu on one screen (camp_screen.h), in DDD's camp pieces: a file
 * tools/hd/ddd_camp.py writes (its lines: docs/HD_SCREENS.md, "The camp menu"). It shows the game's
 * menu as runtime/menus/camp.c reads it -- options, description, the character's face and numbers, the
 * members, the game's line -- and the Preferences page in the model's and panel's place. */
#include "camp_screen.h"

#include "../app/devices.h"
#include "../render/gpu_present.h"
#include "layout_screen.h"
#include "../app/places.h"
#include "../render/png_io.h"
#include "../app/profile.h"
#include "ui_text.h"
#include "../../runtime/menus/camp.h"
#include "../../runtime/core/rom.h"
#include "../../runtime/mods/installed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAMES 21 /* status_&.s: the characters' names, in the game's order */
#define LABEL_HEARTS 11
#define LABEL_TRIALS 13
#define PROMPT 63  /* mlt_&.s: A's prompt, then B's (the menu page has no B: cut there) */
#define INFO_DAY 0 /* UI/btl/&/info.s: "DAY %d" */
#define SAV_YES 5  /* UI/cm/str/sav_&.s: "Yes", then "No" */
#define SAV_NO 6
#define DIMMED 0.4f  /* the options while the question is open */
#define CFG_HIDDEN 3 /* UI/cm/str/cfg_&.s: "?", a setting the game has off */
#define GLYPH_A 0x3349
#define GLYPH_B 0x3314
#define LANGUAGES 6
#define RING 3.0f /* the white ring around the place's circle */
#define FACE_RING 4.0f
#define PLATE_ALPHA 0.6f /* a member's plate (the character select's) */
#define PLATE_EDGE 2.0f
#define PLATE_READY 4.0f
static const Uint8 BLACK[3] = {0, 0, 0}, EDGE_GREY[3] = {110, 110, 110};
static const Uint8 WHITE[3] = {255, 255, 255};
/* the level's four numbers (labels 0-3), then the rows' (labels, the fields that draw them) */
static const int LEVEL_FIELDS[4] = {2, 3, 5, 6};
static const int ROW_LABELS[9] = {4, 5, 6, 7, 8, 9, 10, 12, 14};
static const int ROW_FIELDS[9] = {7, 9, 0xb, 0xc, 0xd, 0xe, 0xf, 0x11, 0x13};
static const char *const CODES[LANGUAGES] = {"", "en", "fr", "de", "it", "es"};

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

static struct {
    int tried, ok;
    KhdaysLayout layout;
    KhdaysLayoutStrings status, root, mlt, info, sav, cfg;
    SDL_Texture *backdrop; /* the plan's, drawn around the model's window */
    float window[4];
    unsigned short *option[KHDAYS_CAMP_OPTIONS], *story_option[KHDAYS_CAMP_OPTIONS], *story_sub[4];
    unsigned short *menu_place, *mode_label, *mission_mode, *difficulty[3];
    float bars[5], panel[4], trials[4];
    float description_pitch, overlap, member_pitch;
    float page_tabs[5], page_rows[6], detail_pitch;
    int page_top; /* the first of the section's settings shown */
    Uint8 munny_colour[3], hearts_colour[3], day_colour[3];
} s;

static int open_file(int language, char *path, int size, char *folder, int folder_size)
{
    char relative[96], *slash;
    if (language < 1 || language >= LANGUAGES) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/camp/%s/camp.txt", CODES[language]);
    if (!khdays_mods_find(relative, path, size)) {
        return 0;
    }
    snprintf(folder, (size_t)folder_size, "%s", path);
    slash = strrchr(folder, '/');
    if (strrchr(folder, '\\') > slash) {
        slash = strrchr(folder, '\\');
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
    return 1;
}

/* Calls `line` with each of the file's lines (changed in place). */
static int each_line(const char *path, void (*line)(char *text, const char *folder, void *ctx), const char *folder,
                     void *ctx)
{
    size_t size = 0;
    char *text = (char *)SDL_LoadFile(path, &size), *next;
    if (text == NULL) {
        return 0;
    }
    for (char *l = text; l != NULL && *l != '\0'; l = next) {
        next = strchr(l, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        line(l, folder, ctx);
    }
    SDL_free(text);
    return 1;
}

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
        if (open_file(language, path, (int)sizeof(path), folder, (int)sizeof(folder)) &&
            each_line(path, plan_line, folder, p)) {
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
    khdays_layout_forget(&s.layout);
    khdays_layout_strings_free(&s.status);
    khdays_layout_strings_free(&s.root);
    khdays_layout_strings_free(&s.mlt);
    khdays_layout_strings_free(&s.info);
    khdays_layout_strings_free(&s.sav);
    khdays_layout_strings_free(&s.cfg);
    memset(s.page_rows, 0, sizeof(s.page_rows));
}

static void colour(const char *line, const char *key, Uint8 out[3])
{
    char format[64];
    int r, g, b;
    snprintf(format, sizeof(format), "%s %%d %%d %%d", key);
    if (SDL_sscanf(line, format, &r, &g, &b) == 3) {
        out[0] = (Uint8)r, out[1] = (Uint8)g, out[2] = (Uint8)b;
    }
}

/* A line's words (UTF-8) kept as UTF-16. */
static void words(unsigned short **slot, const char *text)
{
    const size_t room = strlen(text) + 1;
    SDL_free(*slot);
    *slot = (unsigned short *)SDL_malloc(room * sizeof(unsigned short));
    if (*slot != NULL) {
        khdays_layout_utf8(text, *slot, (int)room);
    }
}

static void screen_line(char *line, const char *folder, void *ctx)
{
    float a, b, c, d, e;
    int n, at;
    (void)ctx;
    if (khdays_layout_line(&s.layout, line, folder)) {
        return;
    }
    if (SDL_sscanf(line, "bars %f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
        s.bars[0] = a, s.bars[1] = b, s.bars[2] = c, s.bars[3] = d, s.bars[4] = e;
    } else if (SDL_sscanf(line, "panel %f %f %f %f", &a, &b, &c, &d) == 4) {
        s.panel[0] = a, s.panel[1] = b, s.panel[2] = c, s.panel[3] = d;
    } else if (SDL_sscanf(line, "trials %f %f %f %f", &a, &b, &c, &d) == 4) {
        s.trials[0] = a, s.trials[1] = b, s.trials[2] = c, s.trials[3] = d;
    } else if (strncmp(line, "menu_place ", 11) == 0) {
        words(&s.menu_place, line + 11);
    } else if (strncmp(line, "mode_label ", 11) == 0) {
        words(&s.mode_label, line + 11);
    } else if (strncmp(line, "mission_mode ", 13) == 0) {
        words(&s.mission_mode, line + 13);
    } else if (SDL_sscanf(line, "difficulty %d %n", &n, &at) == 1 && n >= 0 && n < 3) {
        words(&s.difficulty[n], line + at);
    } else if (SDL_sscanf(line, "description_pitch %f", &a) == 1) {
        s.description_pitch = a;
    } else if (SDL_sscanf(line, "detail_pitch %f", &a) == 1) {
        s.detail_pitch = a;
    } else if (SDL_sscanf(line, "page_tabs %f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
        s.page_tabs[0] = a, s.page_tabs[1] = b, s.page_tabs[2] = c, s.page_tabs[3] = d, s.page_tabs[4] = e;
    } else if (SDL_sscanf(line, "page_rows %f %f %f %f %f %d", &a, &b, &c, &d, &e, &n) == 6 && n > 0) {
        s.page_rows[0] = a, s.page_rows[1] = b, s.page_rows[2] = c, s.page_rows[3] = d, s.page_rows[4] = e;
        s.page_rows[5] = (float)n;
    } else if (SDL_sscanf(line, "digit_overlap %f", &a) == 1) {
        s.overlap = a;
    } else if (SDL_sscanf(line, "members %f", &a) == 1) {
        s.member_pitch = a;
    } else if ((SDL_sscanf(line, "option %d %n", &n, &at) == 1 ||
                SDL_sscanf(line, "story_option %d %n", &n, &at) == 1 ||
                SDL_sscanf(line, "story_sub %d %n", &n, &at) == 1) &&
               n >= 0 && n < (strncmp(line, "story_sub", 9) == 0 ? 4 : KHDAYS_CAMP_OPTIONS)) {
        unsigned short **slot = strncmp(line, "story_sub", 9) == 0 ? &s.story_sub[n]
                                : line[0] == 's'                   ? &s.story_option[n]
                                                                   : &s.option[n];
        const size_t room = strlen(line + at) + 1;
        SDL_free(*slot);
        *slot = (unsigned short *)SDL_malloc(room * sizeof(unsigned short));
        if (*slot != NULL) {
            khdays_layout_utf8(line + at, *slot, (int)room);
        }
    } else {
        colour(line, "munny_colour", s.munny_colour);
        colour(line, "hearts_colour", s.hearts_colour);
        colour(line, "day_colour", s.day_colour);
    }
}

/* The notice's style (Mission Mode's line over the menu): the file's, or for a file made before it
 * tools/hd/ddd_camp.py's -- the prompt's size, right-aligned in the top band before the settings
 * key. */
static void notice_style(void)
{
    const KhdaysLayoutText *prompt = khdays_layout_text(&s.layout, "prompt");
    char line[128];
    if (khdays_layout_text(&s.layout, "notice") != NULL || prompt == NULL) {
        return;
    }
    snprintf(line, sizeof(line), "text notice 1700 %.1f %.4f right 255", prompt->y - (1016.0f - 46.0f), prompt->size);
    khdays_layout_line(&s.layout, line, "");
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
    if ((p = plan(language)) == NULL || !open_file(language, path, (int)sizeof(path), folder, (int)sizeof(folder))) {
        return 0;
    }
    memcpy(s.window, p->window, sizeof(s.window));
    s.backdrop = khdays_layout_texture(&s.layout, p->backdrop_path, 0, &w, &h);
    s.bars[3] = 0;
    each_line(path, screen_line, folder, NULL);
    notice_style();
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

static const float *place(const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&s.layout, name);
    return p != NULL ? &p->x : none;
}

static void text(const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t, float dx, float dy)
{
    khdays_layout_draw_text(&s.layout, fr, style, t, dx, dy, 1.0f);
}

static void number(const KhdaysLayoutFrame *fr, const char *style, const char *digits, float dx, float dy)
{
    unsigned short wide[48];
    khdays_layout_utf8(digits, wide, 48);
    text(fr, style, wide, dx, dy);
}

/* A style's capitals' height. */
static float capitals(const char *style)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, style);
    return st != NULL ? (s.layout.ink_bottom - s.layout.ink_top) * st->size : 0;
}

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
    const float *lit = place("bar_selected"), *body = place("bar_body"), *glove = place("glove");
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
                                by + (float)i * pitch + body[1] + body[3] / 2 - capitals("option") / 2, alpha);
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
    const float *glove = place("glove");
    for (int b = 0; b < 2; ++b) {
        const int on = b == 0 ? v->yes : !v->yes;
        const float *at = place(names[b][2]);
        if (at[2] <= 0) {
            continue;
        }
        khdays_layout_draw_piece(&s.layout, fr, names[b][on], at[0], at[1], at[2], at[3], 1.0f);
        /* a centred style's place is its words' middle */
        text(fr, "button", khdays_layout_string(&s.sav, b == 0 ? SAV_YES : SAV_NO), at[0] + at[2] / 2,
             at[1] + at[3] / 2);
        if (on) {
            khdays_layout_draw_piece(&s.layout, fr, "glove", at[0] + glove[0], at[1] + at[3] / 2 + glove[1], glove[2],
                                     glove[3], 1.0f);
        }
    }
}

/* A record's lines (as the game breaks them) in a style, `pitch` apart. */
static void draw_lines(const KhdaysLayoutFrame *fr, const KhdaysLayoutStrings *table, int record, const char *style,
                       float pitch)
{
    const unsigned short *t = khdays_layout_string(table, record);
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
            text(fr, style, line, 0, dy);
        }
        dy += pitch;
        t += n;
        if (*t == '\n') {
            ++t;
        }
    }
}

/* The game's A prompt (mlt_&.s record 63: A's icon and its word, then B's): up to B's icon, the
 * spaces round it left out; with word_alone without A's icon (the word for accepting). */
static void prompt_part(unsigned short *out, int room, int word_alone)
{
    const unsigned short *t = khdays_layout_string(&s.mlt, PROMPT);
    int n = 0, at = 0;
    out[0] = 0;
    if (t == NULL) {
        return;
    }
    if (word_alone) {
        while (t[at] != 0 && (t[at] == GLYPH_A || t[at] == ' ')) {
            ++at;
        }
    }
    while (t[at] != 0 && t[at] != GLYPH_B && n < room - 1) {
        out[n++] = t[at++];
    }
    while (n > 0 && out[n - 1] == ' ') {
        --n;
    }
    out[n] = 0;
}

static void draw_members(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const float *plate = place("member"), *ring = place("member_circle"), *small = place("member_face");
    for (int i = 0; i < KHDAYS_CAMP_PLACES; ++i) {
        const KhdaysCampMember *m = &v->member[i];
        const int ready = m->page == KHDAYS_CAMP_READY;
        const Uint8 *own = KHDAYS_PLACE_COLOURS[i];
        /* each place where the DS has it: its own (a place gone leaves its room) */
        const float x = plate[0] + (float)i * s.member_pitch, y = plate[1];
        char label[16];
        unsigned short wide[64];
        if (!m->present) {
            continue;
        }
        khdays_layout_fill_rounded(s.layout.renderer, fr, x, y, plate[2], plate[3], plate[3] / 2, BLACK, PLATE_ALPHA);
        khdays_layout_ring_rounded(s.layout.renderer, fr, x, y, plate[2], plate[3], plate[3] / 2,
                                   ready ? PLATE_READY : PLATE_EDGE, ready ? own : EDGE_GREY, 1.0f);
        khdays_layout_fill_oval(s.layout.renderer, fr, x + ring[0], y + ring[1], ring[2] + RING, ring[2] + RING, WHITE,
                                1.0f);
        khdays_layout_fill_oval(s.layout.renderer, fr, x + ring[0], y + ring[1], ring[2], ring[2], own, 1.0f);
        snprintf(label, sizeof(label), khdays_text(TXT_ROOM_PLACE), i + 1);
        khdays_layout_utf8(label, wide, 16);
        text(fr, "member_tag", wide, x + ring[0], y + ring[1]);
        if (m->character >= 0) {
            char name[16];
            snprintf(name, sizeof(name), "face%d", m->character);
            khdays_layout_draw_piece(&s.layout, fr, name, x + small[0], y + small[1], small[2], small[3], 1.0f);
        }
        text(fr, "member_name",
             m->name[0] != 0 ? (const unsigned short *)m->name
                             : khdays_layout_string(&s.status, NAMES + (m->character >= 0 ? m->character : 0)),
             x, y);
        if (ready) {
            prompt_part(wide, 64, 1);
            text(fr, "member_ready", wide, x, y);
        } else if (m->page >= 1 && m->page <= KHDAYS_CAMP_OPTIONS - 1) {
            text(fr, "member_place", s.option[m->page - 1], x, y);
        } else if (m->page == KHDAYS_CAMP_IN_MENU) {
            text(fr, "member_place", s.menu_place, x, y);
        }
    }
}

static void draw_description(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    if (v->question) {
        /* Title Screen's question in the port's words (see above) */
        const char *words = khdays_text_in(TXT_CAMP_BACK_TO_MENU, v->language);
        unsigned short line[128];
        if (words != NULL) {
            khdays_layout_utf8(words, line, 128);
            text(fr, "description", line, 0, 0);
            return;
        }
    }
    draw_lines(fr, &s.root, v->description, "description", s.description_pitch);
}

static float width_in(const char *style, const unsigned short *t);

/* The Preferences page's: its sections' tabs, the section's settings, the hand, the scroll bar. */
static void draw_page(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const KhdaysCampPage *pg = &v->page;
    const float *tabs = s.page_tabs, *rows = s.page_rows;
    const float *l = place("page_l"), *r = place("page_r"), *glove = place("page_glove"), *arrow = place("page_arrow");
    const float gap = place("page_value_gap")[0];
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
        const float *at = place(on ? "page_tab_selected" : "page_tab");
        khdays_layout_draw_piece(&s.layout, fr, on ? "page_tab_selected" : "page_tab", x + at[0], y + at[1], at[2],
                                 at[3], 1.0f);
        text(fr, on ? "page_tab_on" : "page_tab", khdays_layout_string(&s.cfg, pg->title[i]), x + tabs[2] / 2,
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
        const float *at = place(on ? "page_row_selected" : "page_row");
        const float fade = st->enabled ? 1.0f : DIMMED; /* its arrows: the game changes it not */
        const int value = st->value >= 0 && st->value < st->values ? st->value : 0;
        float vx = x + arrow[0];
        khdays_layout_draw_piece(&s.layout, fr, on ? "page_row_selected" : "page_row", x + at[0], y + at[1], at[2],
                                 at[3], 1.0f);
        text(fr, "setting", khdays_layout_string(&s.cfg, st->enabled ? st->name : CFG_HIDDEN), x,
             mid - capitals("setting") / 2);
        if (left != NULL && right != NULL) {
            const float aw = left->w * arrow[3] / left->h;
            const unsigned short *found =
                khdays_layout_string(&s.cfg, st->enabled ? st->value_name[value] : CFG_HIDDEN);
            static const unsigned short none[1] = {0};
            const unsigned short *words = found != NULL ? found : none;
            khdays_layout_draw_piece(&s.layout, fr, "page_arrow_left", vx, mid - arrow[3] / 2, aw, arrow[3], fade);
            vx += aw + gap;
            text(fr, "setting_value", words, vx, mid - capitals("setting_value") / 2);
            vx += width_in("setting_value", words) + gap;
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
        const float *track = place("page_track"), *thumb = place("page_thumb");
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
        draw_lines(fr, &s.cfg, st->value_help[st->value], "detail", s.detail_pitch);
    }
}

/* Mission Mode's line over the menu, where the DS has it (its top band's right): the host choosing a
 * mission, START to join it or to begin, ... (camp.h `notice`). */
static void draw_notice(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    if (v->notice >= 0) {
        text(fr, "notice", khdays_layout_string(&s.root, v->notice), 0, 0);
    }
}

static void draw_prompt(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    unsigned short line[128];
    if (v->question || v->config) {
        /* the question and the Preferences page take B back: the game's prompt whole */
        text(fr, "prompt", khdays_layout_string(&s.mlt, PROMPT), 0, 0);
        return;
    }
    prompt_part(line, 128, 0);
    text(fr, "prompt", line, 0, 0);
}

static void value_text(const KhdaysCampView *v, int field, char *out, size_t size)
{
    const KhdaysCampField *f = &v->field[field];
    out[0] = '\0';
    if (!f->has) {
        return;
    }
    if (field == 0xf && (int)f->value[0] < 0) {
        snprintf(out, size, "--"); /* the game's for no next level (data_ov008_02090814) */
    } else if (field == 0x11) {
        snprintf(out, size, "%u/%u", f->value[0], f->value[1]);
    } else {
        snprintf(out, size, "%u", f->value[0]);
    }
}

static void draw_status(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const float px = s.panel[0], py = s.panel[1];
    const float *f = place("face"), *ring = place("face_ring"), *tag = place("tag");
    const float *level = place("level"), *rows = place("stats");
    /* the place's colour (the story's, alone: the first place's, as Mission Mode's alone) */
    const Uint8 *own = KHDAYS_PLACE_COLOURS[v->story ? 0 : v->self & 3];
    char value[32];
    if (v->character >= 0) {
        char name[16];
        snprintf(name, sizeof(name), "face%d", v->character);
        khdays_layout_draw_piece(&s.layout, fr, name, px + f[0], py + f[1], f[2], f[3], 1.0f);
        text(fr, "name", khdays_layout_string(&s.status, NAMES + v->character), px, py);
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
        text(fr, "tag", wide, cx, cy);
    }
    for (int i = 0; i < 4; ++i) {
        const float dx = px + level[0] + (float)(i % 2) * level[2], dy = py + level[1] + (float)(i / 2) * level[3];
        text(fr, "level_label", khdays_layout_string(&s.status, i), dx, dy);
        value_text(v, LEVEL_FIELDS[i], value, sizeof(value));
        number(fr, "level_value", value, dx, dy);
    }
    for (int i = 0; i < 9; ++i) {
        const float dy = py + rows[1] + (float)i * rows[3];
        text(fr, "stat_label", khdays_layout_string(&s.status, ROW_LABELS[i]), px, dy);
        if (ROW_FIELDS[i] == 0x13) {
            if (v->field[0x13].has) {
                text(fr, "stat_value", khdays_layout_string(&s.status, (int)v->field[0x13].value[0]), px, dy);
            }
        } else {
            value_text(v, ROW_FIELDS[i], value, sizeof(value));
            number(fr, "stat_value", value, px, dy);
        }
    }
    {
        /* the game mode, the last row: the story's difficulty, Mission Mode's own */
        const float dy = py + rows[1] + 9.0f * rows[3];
        text(fr, "stat_label", s.mode_label, px, dy);
        text(fr, "stat_value",
             v->story ? (v->difficulty >= 0 && v->difficulty < 3 ? s.difficulty[v->difficulty] : NULL) : s.mission_mode,
             px, dy);
    }
}

/* A line's width in a text style. */
static float width_in(const char *style, const unsigned short *t)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, style);
    return st != NULL && t != NULL ? khdays_layout_line_width(&s.layout, t, st->size) : 0;
}

/* A line in a style with its left at x and its capitals' middle at y (the style's own place left
 * aside). */
static void text_at(const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t, float x, float y)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, style);
    if (st != NULL) {
        text(fr, style, t, x - st->x, y - capitals(style) / 2 - st->y);
    }
}

/* The trials' label, symbols and counts in one row, centred in the trials' bar both ways; the gaps the
 * layout's (place trial, trials_value). */
static void draw_trials(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    static const char *const symbols[2] = {"sigil", "crown"};
    const float *sym = place("trial");
    const KhdaysLayoutText *count_style = khdays_layout_text(&s.layout, "trials_value");
    const unsigned short *label = khdays_layout_string(&s.status, LABEL_TRIALS);
    const float h = sym[3], mid = s.trials[1] + s.trials[3] / 2, gap = count_style != NULL ? count_style->x : 0;
    unsigned short counts[2][16];
    float sym_w[2], total = width_in("trials_label", label), x;
    for (int i = 0; i < 2; ++i) {
        const KhdaysLayoutPiece *p = khdays_layout_piece(&s.layout, symbols[i]);
        char value[16];
        sym_w[i] = p != NULL ? p->w * h / p->h : 0;
        snprintf(value, sizeof(value), "%u", v->field[0x12].has ? v->field[0x12].value[i] : 0u);
        khdays_layout_utf8(v->field[0x12].has ? value : "", counts[i], 16);
        total += sym[2] + sym_w[i] + gap + width_in("trials_value", counts[i]);
    }
    x = s.trials[0] + (s.trials[2] - total) / 2;
    text_at(fr, "trials_label", label, x, mid);
    x += width_in("trials_label", label);
    for (int i = 0; i < 2; ++i) {
        x += sym[2];
        khdays_layout_draw_piece(&s.layout, fr, symbols[i], x, mid - h / 2, sym_w[i], h, 1.0f);
        x += sym_w[i] + gap;
        text_at(fr, "trials_value", counts[i], x, mid);
        x += width_in("trials_value", counts[i]);
    }
}

/* The story's day on its tag at the model's box's top right (the game's "DAY %d", its day). */
static void draw_day(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const unsigned short *format = khdays_layout_string(&s.info, INFO_DAY);
    const float *tag = place("day_tag");
    unsigned short line[64];
    char digits[16];
    int n = 0;
    float w;
    if (format == NULL || tag[2] <= 0) {
        return;
    }
    snprintf(digits, sizeof(digits), "%d", v->day);
    for (int i = 0; format[i] != 0 && n < 62; ++i) {
        if (format[i] == '%' && format[i + 1] == 'd') {
            for (const char *d = digits; *d != 0 && n < 62; ++d) {
                line[n++] = (unsigned short)*d;
            }
            ++i;
        } else {
            line[n++] = format[i];
        }
    }
    line[n] = 0;
    w = width_in("day", line) + 2 * tag[3];
    khdays_layout_fill_rounded(s.layout.renderer, fr, tag[0] - w, tag[1], w, tag[2], tag[2] / 2, s.day_colour, 1.0f);
    text(fr, "day", line, tag[0] - tag[3], tag[1] + tag[2] / 2 - capitals("day") / 2);
}

static void draw_settings_key(const KhdaysLayoutFrame *fr)
{
    const float *at = place("settings");
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
        big_digits(&fr, v.munny, place("munny"), s.munny_colour);
        text(&fr, "hearts_label", khdays_layout_string(&s.status, LABEL_HEARTS), 0, 0);
        big_digits(&fr, v.hearts, place("hearts"), s.hearts_colour);
        draw_page(&fr, &v);
        draw_setting_help(&fr, &v);
        draw_prompt(&fr, &v);
        draw_settings_key(&fr);
        if (!v.story) {
            draw_members(&fr, &v);
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
    draw_trials(&fr, &v);
    big_digits(&fr, v.munny, place("munny"), s.munny_colour);
    text(&fr, "hearts_label", khdays_layout_string(&s.status, LABEL_HEARTS), 0, 0);
    big_digits(&fr, v.hearts, place("hearts"), s.hearts_colour);
    draw_description(&fr, &v);
    if (v.question) {
        draw_answers(&fr, &v);
    }
    draw_prompt(&fr, &v);
    draw_settings_key(&fr);
    if (v.story) {
        draw_day(&fr, &v);
    } else {
        draw_members(&fr, &v);
        draw_notice(&fr, &v);
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
