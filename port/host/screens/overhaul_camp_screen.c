/* The Menu Overhaul's camps on one screen (camp_screen.h, used in camp_screen.c's place with that mod): Mission
 * Mode's menu in DDD's camp pieces with the mod's touches, and the story's in a look of its own -- its menu, its
 * Panels, Files and save pages, the transitions between them without the game's black -- from the mod's layouts
 * (docs/MENU_OVERHAUL.md), as runtime/menus/camp.c reads the game's menu. */
#include "camp_screen.h"
#include "shop_screen.h"

#include "../app/devices.h"
#include "../render/gfx_render.h"
#include "../render/gpu_present.h"
#include "layout_screen.h"
#include "kh2_seq.h"
#include "../app/places.h"
#include "../render/png_io.h"
#include "../app/profile.h"
#include "ui_text.h"
#include "../../runtime/menus/camp.h"
#include "../../runtime/menus/enemy_viewer.h"
#include "../../runtime/menus/savelist.h"
#include "../../runtime/menus/saveworld.h"
#include "../../runtime/core/rom.h"
#include "../../runtime/core/session_log.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NAMES 21 /* status_&.s: the characters' names, in the game's order */
#define LABEL_HEARTS 11
#define LABEL_TRIALS 13
#define PROMPT 63  /* mlt_&.s: A's prompt, then B's (the menu page has no B: cut there) */
#define INFO_DAY 0 /* UI/btl/&/info.s: "DAY %d" */
#define SAV_YES 5  /* UI/cm/str/sav_&.s: "Yes", then "No" */
#define SAV_NO 6
#define SAV_LEVEL 9      /* "LV %d" */
#define SAV_CORRUPT 12   /* a file the game cannot read */
#define LRD_TITLE 0      /* UI/load/lrd_&.s: the load list's help, */
#define LRD_KEYS 1       /* its keys, */
#define LRD_DELETE_KEY 2 /* the delete key's, */
#define LRD_YES 7        /* "Yes", "No" */
#define LRD_NO 8
#define DIMMED 0.4f  /* the options while the question is open */
#define CFG_HIDDEN 3 /* UI/cm/str/cfg_&.s: "?", a setting the game has off */
#define GLYPH_A 0x3349
#define GLYPH_B 0x3314
#define VIEWER_GLYPH_X 0x3322 /* the DS's X, L, R, Y and directions in the game's strings (layout_screen.c's) */
#define VIEWER_GLYPH_L 0x3318
#define VIEWER_GLYPH_Y 0xE059
#define VIEWER_GLYPH_LEFT 0x337b
#define VIEWER_GLYPH_RIGHT 0x333b
#define VIEWER_GLYPH_UP 0x332b
#define VIEWER_GLYPH_DOWN 0x334a
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
    float mission_source[4], mission_view[4]; /* Mission Mode's model in the story's look (its camera is farther) */
    uint8_t *backdrop;                        /* kept while the port runs: the picture's card keeps it by its address */
    int w, h;
    char backdrop_path[1300];
    uint8_t *page_backdrop; /* the Preferences page's, the same */
    int page_w, page_h;
    char page_backdrop_path[1300];
} Plan;

static Plan plans[LANGUAGES];

static struct {
    int tried, ok;
    KhdaysLayout layout;
    KhdaysLayoutStrings status, root, mlt, info, sav, cfg, lrd;
    SDL_Texture *backdrop;      /* the plan's, drawn around the model's window */
    SDL_Texture *page_backdrop; /* the Preferences page's, drawn over the menu's while the two slide */
    float window[4];
    unsigned short *option[KHDAYS_CAMP_OPTIONS], *story_option[KHDAYS_CAMP_OPTIONS], *story_sub[4];
    unsigned short *menu_place, *mode_label, *mission_mode, *difficulty[3];
    float bars[5], panel[4], trials[4];
    float description_pitch, overlap, member_pitch;
    float page_tabs[5], page_rows[6], detail_pitch;
    int page_top; /* the first of the section's settings shown */
    Uint8 munny_colour[3], hearts_colour[3], day_colour[3];
    SDL_Texture *hearts_word; /* hearts_label.png: the heart points' word as DDD's MUNNY is drawn */
    int hearts_word_w, hearts_word_h;
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
    } else if (SDL_sscanf(line, "mission_model_source %f %f %f %f", &a, &b, &c, &d) == 4) {
        p->mission_source[0] = a, p->mission_source[1] = b, p->mission_source[2] = c, p->mission_source[3] = d;
    } else if (SDL_sscanf(line, "mission_model_view %f %f %f %f", &a, &b, &c, &d) == 4) {
        p->mission_view[0] = a, p->mission_view[1] = b, p->mission_view[2] = c, p->mission_view[3] = d;
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
        const char *error = NULL;
        snprintf(p->page_backdrop_path, sizeof(p->page_backdrop_path), "%s%s", folder, file);
        p->page_backdrop = khdays_png_read(p->page_backdrop_path, &p->page_w, &p->page_h, &error);
        if (p->page_backdrop == NULL) {
            SDL_Log("camp menu: %s: %s", p->page_backdrop_path, error != NULL ? error : "not read");
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
            if (p->mission_source[2] <= 0 || p->mission_source[3] <= 0) {
                memcpy(p->mission_source, p->story_source, sizeof(p->mission_source));
            }
            if (p->mission_view[2] <= 0 || p->mission_view[3] <= 0) {
                memcpy(p->mission_view, p->story_view, sizeof(p->mission_view));
            }
            if (!p->ok) {
                SDL_Log("camp menu: %s has no backdrop, model or model_source", path);
            }
        }
    }
    return p->ok ? p : NULL;
}

/* ---- The story's menu: its plan ----------------------------------------------------------------------
 * The story's camp menu (layouts/campstory/campstory.txt): KH2's command buttons for its options, Birth by
 * Sleep's character plate in red for Roxas (his model in it, his numbers) and its deck plate for his panels, a
 * black block behind them, the Files pages' pattern drifting under all. The pattern, the block and the plates
 * are the one-screen picture's animated backdrop, the model over them; the screen draws the rest. */
#define STORY_PLATES 3        /* the block, Roxas's plate, the panels' plate: the picture's pieces after the pattern */
#define STORY_PANELS_OPTION 0 /* Panels: the story's options' first */
#define MISSION_PANELS_OPTION 1 /* and Mission Mode's second (its entry 102) */
#define STORY_STATS 4
#define STORY_ITEMS 0x277 /* the panels' records: item ids 1 to 0x276 */
#define STORY_SHAPES 64   /* the links' shapes (msg db 0x1b's 63) */
static const char *const STORY_PLATE_NAMES[STORY_PLATES] = {"backing", "chara_plate", "deck_plate"};

typedef struct StoryPlan {
    int tried, ok;
    uint8_t *atlas; /* kept while the port runs: the picture's card keeps it by its address */
    int w, h;
    float sheet[4], tile[2], drift[2]; /* the pattern laid out once, its tile there, its drift */
    float piece[STORY_PLATES][4], at[STORY_PLATES][4];
    float model[4];
    char seq_file[128], sheet_file[128];
    float sheet_units;
    float list[6];   /* the first button's place, the rows' pitch (KH2's dots), a dot's size, the widening, from */
    float option[3]; /* the words: from the button's place (dots), their capitals' height */
    int stat_record[STORY_STATS];
    Uint8 stat_colour[STORY_STATS][3];
    float stat[STORY_STATS][5]; /* the words' left and capitals' top, the number's right and top, the capitals */
    float title[3], deck_title[3], day[3];
    Uint8 day_colour[3];
    /* the panels' grid: its place, the panels' textures (<lang>/icons: `icon_cell` texels each, `icon_columns` to
     * a row), the page's words' place; panels.txt: each item's texture (its tag, 1-based) and link shape (-1
     * none, its anchor's column and row in it), the shapes' masks (bit col + 8 * row) */
    float panels[4], pages[3], panels_float[2];
    char icons[128];
    float icon_cell;
    int icon_columns, icon_count;
    struct {
        Uint8 tag, col, row;
        Sint8 shape;
    } item[STORY_ITEMS];
    Uint64 shape[STORY_SHAPES];
    /* the Panels page (see "The story's Panels page"): the old status panel cut out (its file) and its top
     * left; the lower screen's card (its place, the DS's rows it shows); what X shows (the upper screen's dots
     * across, a dot's size in the block); the left room's card (its middle, its width); the swap (its time, its
     * paths' bow) */
    char status_file[128];
    float status_at[2], lower[5], list_card[2], slot[3], swap[2];
} StoryPlan;

static StoryPlan stp;
static KhdaysPiece story_pieces[1 + STORY_PLATES];
static int story_failed; /* the screen's own pieces (KH2's sequence and sheet) missing: Mission Mode's look */

static void story_line(char *line, const char *folder, void *ctx)
{
    float a[8];
    char name[64] = "", file[128] = "", file2[128] = "";
    int n, record, r, g, b;
    (void)ctx;
    if (SDL_sscanf(line, "atlas %127s", file) == 1 && stp.atlas == NULL) {
        char path[1300];
        const char *error = NULL;
        snprintf(path, sizeof(path), "%s%s", folder, file);
        stp.atlas = khdays_png_read(path, &stp.w, &stp.h, &error);
        if (stp.atlas == NULL) {
            SDL_Log("camp menu: %s: %s", path, error != NULL ? error : "not read");
        }
    } else if (SDL_sscanf(line, "pattern_sheet %f %f %f %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5],
                          &a[6], &a[7]) == 8) {
        memcpy(stp.sheet, a, sizeof(stp.sheet));
        stp.tile[0] = a[4], stp.tile[1] = a[5], stp.drift[0] = a[6], stp.drift[1] = a[7];
    } else if (SDL_sscanf(line, "piece %63s %f %f %f %f", name, &a[0], &a[1], &a[2], &a[3]) == 5 ||
               SDL_sscanf(line, "place %63s %f %f %f %f", name, &a[0], &a[1], &a[2], &a[3]) == 5) {
        const int placed = line[1] == 'l';
        if (placed && strcmp(name, "model") == 0) {
            memcpy(stp.model, a, sizeof(stp.model));
        } else if (placed && strcmp(name, "panels") == 0) {
            memcpy(stp.panels, a, sizeof(stp.panels));
        }
        for (int k = 0; k < STORY_PLATES; ++k) {
            if (strcmp(name, STORY_PLATE_NAMES[k]) == 0) {
                memcpy(placed ? stp.at[k] : stp.piece[k], a, sizeof(stp.at[k]));
            }
        }
    } else if (SDL_sscanf(line, "kh2 %127s %127s %f", file, file2, &a[0]) == 3) {
        snprintf(stp.seq_file, sizeof(stp.seq_file), "%s", file);
        snprintf(stp.sheet_file, sizeof(stp.sheet_file), "%s", file2);
        stp.sheet_units = a[0];
    } else if (SDL_sscanf(line, "list %f %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6) {
        memcpy(stp.list, a, sizeof(stp.list));
    } else if (SDL_sscanf(line, "option_text %f %f %f", &a[0], &a[1], &a[2]) == 3) {
        memcpy(stp.option, a, sizeof(stp.option));
    } else if (SDL_sscanf(line, "stat %d %d %d %d %d %f %f %f %f %f", &n, &record, &r, &g, &b, &a[0], &a[1], &a[2],
                          &a[3], &a[4]) == 10 &&
               n >= 0 && n < STORY_STATS) {
        stp.stat_record[n] = record;
        stp.stat_colour[n][0] = (Uint8)r, stp.stat_colour[n][1] = (Uint8)g, stp.stat_colour[n][2] = (Uint8)b;
        memcpy(stp.stat[n], a, sizeof(stp.stat[n]));
    } else if (SDL_sscanf(line, "title %f %f %f", &a[0], &a[1], &a[2]) == 3) {
        memcpy(stp.title, a, sizeof(stp.title));
    } else if (SDL_sscanf(line, "deck_title %f %f %f", &a[0], &a[1], &a[2]) == 3) {
        memcpy(stp.deck_title, a, sizeof(stp.deck_title));
    } else if (SDL_sscanf(line, "day %f %f %f %d %d %d", &a[0], &a[1], &a[2], &r, &g, &b) == 6) {
        memcpy(stp.day, a, sizeof(stp.day));
        stp.day_colour[0] = (Uint8)r, stp.day_colour[1] = (Uint8)g, stp.day_colour[2] = (Uint8)b;
    } else if (SDL_sscanf(line, "pages %f %f %f", &a[0], &a[1], &a[2]) == 3) {
        memcpy(stp.pages, a, sizeof(stp.pages));
    } else if (SDL_sscanf(line, "panels_float %f %f", &a[0], &a[1]) == 2) {
        memcpy(stp.panels_float, a, sizeof(stp.panels_float));
    } else if (SDL_sscanf(line, "panel_icons %127s %f %d %d", file, &a[0], &r, &g) == 4) {
        snprintf(stp.icons, sizeof(stp.icons), "%s", file);
        stp.icon_cell = a[0];
        stp.icon_columns = r;
        stp.icon_count = g;
    } else if (SDL_sscanf(line, "panels_page_status %127s %f %f", file, &a[0], &a[1]) == 3) {
        snprintf(stp.status_file, sizeof(stp.status_file), "%s", file);
        memcpy(stp.status_at, a, sizeof(stp.status_at));
    } else if (SDL_sscanf(line, "panels_page_lower %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4]) == 5) {
        memcpy(stp.lower, a, sizeof(stp.lower));
    } else if (SDL_sscanf(line, "panels_page_list %f %f", &a[0], &a[1]) == 2) {
        memcpy(stp.list_card, a, sizeof(stp.list_card));
    } else if (SDL_sscanf(line, "panels_page_slot %f %f %f", &a[0], &a[1], &a[2]) == 3) {
        memcpy(stp.slot, a, sizeof(stp.slot));
    } else if (SDL_sscanf(line, "panels_page_swap %f %f", &a[0], &a[1]) == 2) {
        memcpy(stp.swap, a, sizeof(stp.swap));
    }
}

/* panels.txt's lines (see the plan). */
static void story_panel_line(char *line, const char *folder, void *ctx)
{
    int id, tag, shape, col, row;
    unsigned low, high;
    (void)folder;
    (void)ctx;
    if (SDL_sscanf(line, "panel %d %d %d %d %d", &id, &tag, &shape, &col, &row) == 5 && id > 0 && id < STORY_ITEMS &&
        tag > 0 && tag < 256) {
        stp.item[id].tag = (Uint8)tag;
        stp.item[id].shape = (Sint8)(shape >= 0 && shape < STORY_SHAPES ? shape : -1);
        stp.item[id].col = (Uint8)col;
        stp.item[id].row = (Uint8)row;
    } else if (SDL_sscanf(line, "shape %d %u %u", &shape, &low, &high) == 3 && shape >= 0 && shape < STORY_SHAPES) {
        stp.shape[shape] = (Uint64)low | (Uint64)high << 32;
    }
}

static const StoryPlan *story_plan(void)
{
    if (!stp.tried) {
        char path[1024], folder[1024], *slash;
        stp.tried = 1;
        if (khdays_mods_find("layouts/campstory/campstory.txt", path, (int)sizeof(path))) {
            snprintf(folder, sizeof(folder), "%s", path);
            slash = strrchr(folder, '/');
            if (strrchr(folder, '\\') > slash) {
                slash = strrchr(folder, '\\');
            }
            if (slash != NULL) {
                slash[1] = '\0';
            }
            each_line(path, story_line, folder, NULL);
            {
                char table[1100];
                snprintf(table, sizeof(table), "%spanels.txt", folder);
                each_line(table, story_panel_line, folder, NULL); /* (none: the panels' plate stays empty) */
            }
            stp.ok = stp.atlas != NULL && stp.tile[0] > 0 && stp.tile[1] > 0 &&
                     stp.sheet[2] >= KHDAYS_LAYOUT_W + stp.tile[0] && stp.sheet[3] >= KHDAYS_LAYOUT_H + stp.tile[1] &&
                     stp.sheet[0] + stp.sheet[2] <= (float)stp.w && stp.sheet[1] + stp.sheet[3] <= (float)stp.h &&
                     stp.piece[1][2] > 0 && stp.at[1][2] > 0 && stp.model[2] > 0 && stp.list[3] > 0 &&
                     stp.seq_file[0] != '\0' && stp.sheet_units > 0;
            if (!stp.ok) {
                SDL_Log("camp menu: %s is incomplete (the story's menu in Mission Mode's look)", path);
            }
        }
    }
    return stp.ok ? &stp : NULL;
}

/* Whether the menu has the story's look (r27): the story's, and since r32 Mission Mode's too (its options, its
 * members and its notice in that look; see "The story's menu"). */
static int story_look(const KhdaysCampView *v)
{
    (void)v;
    return !story_failed && story_plan() != NULL;
}

/* Whether the story's Panels page has its own look (else the game's screens show there). */
static int panels_look(const KhdaysCampView *v)
{
    return story_look(v) && stp.lower[2] > 0 && stp.lower[4] > 0 && stp.list_card[0] > 0 && stp.list_card[1] > 0 &&
           stp.slot[2] > 0 && stp.swap[0] > 0;
}

static void story_piece(KhdaysPiece *p, const float at[4], const float texels[4])
{
    memset(p, 0, sizeof(*p));
    p->rect[0] = at[0];
    p->rect[1] = at[1];
    p->rect[2] = at[0] + at[2];
    p->rect[3] = at[1] + at[3];
    memcpy(p->texels, texels, sizeof(p->texels));
    for (int c = 0; c < 4; ++c) {
        p->corners[4 * c] = p->corners[4 * c + 1] = p->corners[4 * c + 2] = p->corners[4 * c + 3] = 1.0f;
    }
    p->blend = KHDAYS_PIECE_OVER;
}

/* The pattern's texels shown over the whole picture at this moment: its sheet from where the drift has
 * come to (the Files pages' pattern, files_pattern_part's, the same at the same moment). */
static void story_sheet_texels(double now, float out[4])
{
    double ox = fmod(-now * stp.drift[0], (double)stp.tile[0]), oy = fmod(-now * stp.drift[1], (double)stp.tile[1]);
    ox += ox < 0 ? stp.tile[0] : 0.0;
    oy += oy < 0 ? stp.tile[1] : 0.0;
    out[0] = stp.sheet[0] + (float)ox;
    out[1] = stp.sheet[1] + (float)oy;
    out[2] = KHDAYS_LAYOUT_W;
    out[3] = KHDAYS_LAYOUT_H;
}

/* This moment's pieces of the picture: the pattern, the block, the two plates (with `plates`). */
static int story_make_pieces(double now, int plates)
{
    static const float whole[4] = {0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H};
    float sheet[4];
    int n = 0;
    story_sheet_texels(now, sheet);
    story_piece(&story_pieces[n++], whole, sheet);
    for (int k = 0; k < STORY_PLATES && plates; ++k) {
        if (stp.piece[k][2] > 0 && stp.at[k][2] > 0) {
            story_piece(&story_pieces[n++], stp.at[k], stp.piece[k]);
        }
    }
    return n;
}

static double seconds_now(void);
static int story_on(const KhdaysCampView *v);

static void forget_group(void);
static void forget_files(void);
static void files_load(SDL_Renderer *renderer);
static void forget_save(void);
static void save_load(SDL_Renderer *renderer);
static void story_forget(void);

static void panels_forget(void);

static void forget(void)
{
    forget_files();
    forget_save();
    story_forget();
    panels_forget();
    if (s.backdrop != NULL) {
        SDL_DestroyTexture(s.backdrop);
        s.backdrop = NULL;
    }
    if (s.page_backdrop != NULL) {
        SDL_DestroyTexture(s.page_backdrop);
        s.page_backdrop = NULL;
    }
    forget_group();
    if (s.hearts_word != NULL) {
        SDL_DestroyTexture(s.hearts_word);
        s.hearts_word = NULL;
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
    khdays_layout_strings_free(&s.lrd);
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
    if (p->page_backdrop != NULL) {
        s.page_backdrop = khdays_layout_texture(&s.layout, p->page_backdrop_path, 0, &w, &h);
    }
    s.bars[3] = 0;
    each_line(path, screen_line, folder, NULL);
    notice_style();
    {
        /* the heart points' word as a picture, when the folder has one (else the game's words) */
        char word[1100];
        snprintf(word, sizeof(word), "%shearts_label.png", folder);
        if (SDL_GetPathInfo(word, NULL)) {
            s.hearts_word = khdays_layout_texture(&s.layout, word, 0, &s.hearts_word_w, &s.hearts_word_h);
        }
    }
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
    snprintf(table, sizeof(table), "UI/load/lrd_%s.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &s.lrd, s.layout.who);
    files_load(renderer);
    save_load(renderer);
    return 1;
}

/* Whether the one-screen picture took the model's window when it was made last. */
static int applied;

void khdays_overhaul_camp_applied(int yes)
{
    applied = yes;
}

/* The menu as the game shows it, or, just after the game has stopped showing it, as it showed it
 * last: held (`holding`) until the curtain over it is black (see "Its opening and closing"), so the
 * one-screen picture keeps the menu's backdrop and model and the screen keeps its pieces over them
 * -- no picture of the backdrop alone, nor the DS's pieces, between the menu and the black. Either
 * thread (the one-screen picture's and the window's). 2 the game's view, 1 the held one, 0 none. */
static SDL_SpinLock held_lock;
static KhdaysCampView held;
static int holding;

/* The window's own copy of the view's lists (its rows and their titles, the description): the view the
 * window draws points into it, the held one too (the window's thread alone reads them). */
static KhdaysCampLists drawn_lists;

/* `lists` NULL: the view without its lists (the one-screen picture's, which reads none of them). */
static int camp_view_now(KhdaysCampView *v, KhdaysCampLists *lists)
{
    int got = 0;
    if (lists != NULL) {
        khdays_camp_view_lists(v, lists);
    } else {
        khdays_camp_view(v);
    }
    SDL_LockSpinlock(&held_lock);
    if (v->shown) {
        held = *v;
        holding = 1;
        got = 2;
    } else if (holding && held.shown) {
        *v = held;
        got = 1;
    }
    SDL_UnlockSpinlock(&held_lock);
    if (got == 1) {
        v->master = (int16_t)khdays_camp_game_brightness(); /* the game's fade goes on */
    }
    if (lists != NULL) {
        khdays_camp_lists_point(v, lists); /* the held view's lists: the last the window copied */
    } else {
        khdays_camp_lists_point(v, &(KhdaysCampLists){0});
    }
    return got;
}

static void release_held(void)
{
    SDL_LockSpinlock(&held_lock);
    holding = 0;
    held.shown = 0;
    SDL_UnlockSpinlock(&held_lock);
}

static int files_unavailable(void);
static int save_ready(void);
static int viewer_model(KhdaysCampModel *out);
static void panels_lower_now(double now, float out[4], float *dx, float *alpha);
static void panels_lower_made(const float card[4], float dx, float alpha);
static void panels_visit(double now);

int khdays_overhaul_camp_model(KhdaysCampModel *out)
{
    KhdaysCampView v;
    const Plan *p;
    camp_view_now(&v, NULL);
    if (!v.shown || (p = plan(v.language)) == NULL || (s.tried == v.language && !s.ok) ||
        (v.config && p->page_backdrop == NULL) || (v.files && files_unavailable()) || (v.panels && !panels_look(&v)) ||
        (v.save && !save_ready())) {
        return 0; /* (the save screen without its page: the game's own, its screens shown) */
    }
    out->files = v.files;
    memcpy(out->source,
           !story_look(&v) ? p->source
           : v.story       ? p->story_source
                           : p->mission_source,
           sizeof(out->source));
    memcpy(out->view, !story_look(&v) ? p->view : v.story ? p->story_view : p->mission_view, sizeof(out->view));
    memcpy(out->window, p->window, sizeof(out->window));
    out->backdrop = v.config ? p->page_backdrop : p->backdrop;
    out->backdrop_w = v.config ? p->page_w : p->w;
    out->backdrop_h = v.config ? p->page_h : p->h;
    /* the Files page: the screen draws all of it, the DS's screen too; the Panels page: its cards */
    out->model = !v.config && !v.files && !v.save && !v.panels;
    if (v.files && viewer_model(out)) {
        return 1; /* Enemy Profiles' model viewer: the enemy's model over the page darkened */
    }
    if ((out->model || v.panels) && story_look(&v)) {
        /* the story's menu: its animated picture under the model, in its own window (see its plan); the Panels
         * page: its pattern alone, the DS's lower screen's card over it (see "The story's Panels page") */
        const double now = seconds_now();
        memcpy(out->window, stp.model, sizeof(out->window));
        out->atlas = stp.atlas;
        out->atlas_w = stp.w;
        out->atlas_h = stp.h;
        out->ground = 0x0e0808u;
        out->pieces = story_pieces;
        out->piece_count = story_make_pieces(now, !v.panels);
        if (v.panels) {
            float dx;
            out->cards = 1;
            panels_lower_now(now, out->lower_card, &dx, &out->lower_alpha);
            panels_lower_made(out->lower_card, dx, out->lower_alpha);
            out->lower_card[0] += dx;
            out->lower_src[0] = out->lower_src[1] = 0.0f;
            out->lower_src[2] = 256.0f;
            out->lower_src[3] = stp.lower[4];
            /* the upper screen whatever it shows, settled: composed slid back to its list (page B's layers and
             * sprites all move with its slide), or as it is while the page sets up (the list as set up, before the
             * numbers slide in); a visit's own (the panels' abilities change only here) */
            out->panels_upper = v.panels_slide == 0 || v.panels_slide == 192 || v.panels_early;
            out->panels_unslide = v.panels_early ? 0 : v.panels_slide;
            panels_visit(now);
        }
    }
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

static int gummi_number(const KhdaysLayoutFrame *fr, uint32_t value, float right, float top, float h, int red);
#define NUMBER_SCALE 0.8f /* the munny's and the heart points' digits: KHGummi's, this much of their place's height */

/* The menu's numbers (the munny, the heart points), right-aligned at the place's x in the middle of its
 * height: KHGummi's digits as the Files pages' day, yellow darkening at their foot (the heart points' red);
 * without the Files pages' pieces, DDD's big digits in a colour. */
static void big_digits(const KhdaysLayoutFrame *fr, uint32_t value, const float *at, const Uint8 tint[3])
{
    char digits[16];
    float w = 0, x;
    const float h = at[3];
    if (gummi_number(fr, value, at[0], at[1] + h * (1.0f - NUMBER_SCALE) / 2, h * NUMBER_SCALE,
                     tint == s.hearts_colour)) {
        return;
    }
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

/* The heart points' word over its digits: hearts_label.png at the place hearts_label (its capitals'
 * left and top, and their height: the picture has room for accents above them, 0.45 of it, and 0.15
 * below), as DDD's MUNNY over the money's; else the game's words in the style hearts_label. */
static void hearts_word(const KhdaysLayoutFrame *fr)
{
    const KhdaysLayoutPlace *at = khdays_layout_place(&s.layout, "hearts_label");
    if (s.hearts_word != NULL && at != NULL && at->h > 0 && s.hearts_word_h > 0) {
        const float h = at->h * 1.6f, w = (float)s.hearts_word_w * h / (float)s.hearts_word_h;
        const SDL_FRect dst = khdays_layout_to_window(fr, at->x, at->y - at->h * 0.45f, w, h);
        SDL_RenderTexture(s.layout.renderer, s.hearts_word, NULL, &dst);
        return;
    }
    text(fr, "hearts_label", khdays_layout_string(&s.status, LABEL_HEARTS), 0, 0);
}

/* The collection's pointing glove sways as KH2's, independent of refresh rate (layout_screen.h). */
static float glove_motion(void)
{
    return khdays_layout_glove_sway();
}

/* KH2's firefly at the selected one's right end (layout_screen.h), as large beside the glove as KH2's
 * beside its own; one for the camp, flying from the options to the answers and back. */
static void firefly(const KhdaysLayoutFrame *fr, float right, float middle, float alpha)
{
    const float *glove = place("glove");
    const float unit = KHDAYS_FIREFLY_UNIT * (glove[3] > 0 ? glove[3] / 91.0f : 1.0f);
    khdays_layout_firefly(s.layout.renderer, fr, KHDAYS_FIREFLY_CAMP, right - 15.0f * unit, middle, unit, alpha);
}

static void draw_menu_news(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, unsigned short *const *names, int rows,
                           float others);

/* The options' column: `page` how far it looks as the Preferences page has it (0 the menu's: the hand
 * at the selected one; 1 the page's: the others dimmed, no hand; between while the two slide). */
static void draw_options(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, float page)
{
    const float *lit = place("bar_selected"), *body = place("bar_body"), *glove = place("glove");
    const float bx = s.bars[0], by = s.bars[1], pitch = s.bars[3], bh = s.bars[4];
    /* the story's Holo-Missions and Files show their two options in the list's place, as the game's */
    unsigned short *const *names = v->sub != 0 ? s.story_sub + 2 * (v->sub - 1) : v->story ? s.story_option : s.option;
    const int rows = v->sub != 0 ? 2 : KHDAYS_CAMP_OPTIONS;
    const float others = 1.0f - page * (1.0f - DIMMED); /* the others' alpha on the page's way */
    for (int i = 0; i < rows; ++i) {
        /* each row's bar, the selected one's lit (alone: the other showed round its edge); all
         * dimmed while the question is open, but Preferences on its page */
        const float alpha = v->question || v->waiting ? DIMMED : i != v->selected ? others : 1.0f;
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
        const float alpha = v->question || v->waiting || v->locked[i] ? DIMMED : i != v->selected ? others : 1.0f;
        khdays_layout_draw_text(&s.layout, fr, "option", names[i], bx,
                                by + (float)i * pitch + body[1] + body[3] / 2 - capitals("option") / 2, alpha);
    }
    draw_menu_news(fr, v, names, rows, others);
    /* the hand at the selected one, as the DS's (at the question's answer while it is open, at the
     * Preferences page's setting on its page: gone as the page comes) */
    if (!v->question && page < 1.0f && v->selected >= 0) {
        khdays_layout_draw_piece(&s.layout, fr, "glove", bx + body[0] + glove[0] + glove_motion(),
                                 by + (float)v->selected * pitch + body[1] + body[3] / 2 + glove[1], glove[2], glove[3],
                                 1.0f - page);
        if (!v->waiting) {
            firefly(fr, bx + lit[0] + lit[2], by + (float)v->selected * pitch + body[1] + body[3] / 2, 1.0f - page);
        }
    }
}

/* Title Screen's question's two answers, side by side under it: DDD's buttons, the game's words,
 * the hand at the one the cursor is on. */
static void draw_answers_in(const KhdaysLayoutFrame *fr, int yes, const unsigned short *yes_word,
                            const unsigned short *no_word, const float *yes_at, const float *no_at);

static void draw_answers_words(const KhdaysLayoutFrame *fr, int yes, const unsigned short *yes_word,
                               const unsigned short *no_word)
{
    draw_answers_in(fr, yes, yes_word, no_word, place("button_yes"), place("button_no"));
}

/* The same at the places given (the buttons' x, y, w, h). */
static void draw_answers_in(const KhdaysLayoutFrame *fr, int yes, const unsigned short *yes_word,
                            const unsigned short *no_word, const float *yes_at, const float *no_at)
{
    static const char *const names[2][2] = {{"button_yes", "button_selected_yes"}, {"button_no", "button_selected_no"}};
    const float *glove = place("glove");
    for (int b = 0; b < 2; ++b) {
        const int on = b == 0 ? yes : !yes;
        const float *at = b == 0 ? yes_at : no_at;
        if (at[2] <= 0) {
            continue;
        }
        khdays_layout_draw_piece(&s.layout, fr, names[b][on], at[0], at[1], at[2], at[3], 1.0f);
        /* a centred style's place is its words' middle */
        text(fr, "button", b == 0 ? yes_word : no_word, at[0] + at[2] / 2, at[1] + at[3] / 2);
        if (on) {
            khdays_layout_draw_piece(&s.layout, fr, "glove", at[0] + glove[0] + glove_motion(),
                                     at[1] + at[3] / 2 + glove[1], glove[2], glove[3], 1.0f);
            firefly(fr, at[0] + at[2], at[1] + at[3] / 2, 1.0f);
        }
    }
}

static void draw_answers(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    draw_answers_words(fr, v->yes, khdays_layout_string(&s.sav, SAV_YES), khdays_layout_string(&s.sav, SAV_NO));
}

/* A text's lines (as the game breaks them) in a style, `pitch` apart. */
static void draw_text_lines(const KhdaysLayoutFrame *fr, const unsigned short *t, const char *style, float pitch)
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
            text(fr, style, line, 0, dy);
        }
        dy += pitch;
        t += n;
        if (*t == '\n') {
            ++t;
        }
    }
}

/* A record's lines in a style, `pitch` apart. */
static void draw_lines(const KhdaysLayoutFrame *fr, const KhdaysLayoutStrings *table, int record, const char *style,
                       float pitch)
{
    draw_text_lines(fr, khdays_layout_string(table, record), style, pitch);
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

/* The Preferences page's: its sections' tabs, the section's settings, the hand, the scroll bar; the
 * firefly with `lit` (left with the options while the page slides in or out: it flies over after). */
static void draw_page(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, int lit)
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
    khdays_layout_draw_action_key(&s.layout, fr, KHDAYS_ACT_L, SDL_SCANCODE_LEFT, l[0], l[1] - l[3] / 2, l[3], 1.0f);
    khdays_layout_draw_action_key(&s.layout, fr, KHDAYS_ACT_R, SDL_SCANCODE_RIGHT, r[0], r[1] - r[3] / 2, r[3], 1.0f);
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
            const float *row = place("page_row_selected");
            khdays_layout_draw_piece(&s.layout, fr, "glove", x + glove[0] + glove_motion(), mid + glove[1], glove[2],
                                     glove[3], 1.0f);
            if (lit) {
                firefly(fr, x + row[0] + row[2], mid, 1.0f);
            }
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

/* UTF-8 words after a line's (room for `room` characters and the end): how many. */
static int utf16_append(unsigned short *out, int room, const char *utf8)
{
    int n = 0;
    if (room <= 1) {
        return 0;
    }
    khdays_layout_utf8(utf8, out, room);
    while (out[n] != 0) {
        ++n;
    }
    return n;
}

/* The buttons' prompt in a plate of its own (the members' look), its right where the prompt's style
 * puts the line's: a hint at the description's bottom right, not words left in the middle of it. */
static void prompt_plate(const KhdaysLayoutFrame *fr, const unsigned short *t);

static void draw_prompt(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    unsigned short line[128];
    const unsigned short *t = line;
    if (v->question || v->config || v->save) {
        /* the question, the Preferences page and the save page take B back: the game's prompt whole */
        t = khdays_layout_string(&s.mlt, PROMPT);
    } else if (v->files) {
        /* the Files pages: B back alone (A shows an entry's other version while held, not a choice); on a
         * met enemy, X its model first (enemy_viewer.c) */
        const unsigned short *whole = khdays_layout_string(&s.mlt, PROMPT);
        const KhdaysCampFiles *f = &v->file;
        int n = 0;
        line[0] = 0;
        if (f->mode == 1 && f->cursor >= 0 && f->cursor < f->rows && f->cursor < f->count &&
            !f->row[f->cursor].locked) {
            const char *model = khdays_text_in(TXT_VIEWER_MODEL, v->language);
            line[n++] = VIEWER_GLYPH_X;
            line[n++] = ' ';
            if (model != NULL) {
                n += utf16_append(line + n, 128 - n - 4, model);
            }
            line[n++] = ' ';
            line[n++] = ' ';
            line[n++] = ' ';
        }
        while (whole != NULL && *whole != 0 && *whole != GLYPH_B) {
            ++whole;
        }
        while (whole != NULL && *whole != 0 && n < 127) {
            line[n++] = *whole++;
        }
        line[n] = 0;
    } else {
        prompt_part(line, 128, 0);
    }
    prompt_plate(fr, t);
}

/* A prompt's line in a plate of its own (see draw_prompt). */
static void prompt_plate(const KhdaysLayoutFrame *fr, const unsigned short *t)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, "prompt");
    if (t == NULL || t[0] == 0) {
        return;
    }
    if (st != NULL) {
        const float cap = capitals("prompt"), pad = cap * 1.1f, h = cap * 2.6f;
        const float w = khdays_layout_line_width(&s.layout, t, st->size) + 2 * pad;
        const float right = st->align == 2 ? st->x : st->align == 1 ? st->x + w / 2 - pad : st->x + w - 2 * pad;
        const float x = right + pad - w, y = st->y + cap / 2 - h / 2;
        khdays_layout_fill_rounded(s.layout.renderer, fr, x, y, w, h, h / 2, BLACK, PLATE_ALPHA);
        khdays_layout_ring_rounded(s.layout.renderer, fr, x, y, w, h, h / 2, PLATE_EDGE, EDGE_GREY, 1.0f);
    }
    text(fr, "prompt", t, 0, 0);
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

/* A number field in `style` at (dx, dy), as number(): with a panel held on the Panels page (the field's `delta`,
 * the game's preview), as the DS shows it, the value as it is and the change on its right ("+2") in orange,
 * the two together where the value alone goes. The DS draws that preview only on its own upper screen. */
static float camp_text(const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y, float cap,
                       const Uint8 rgb[3], int right);
#define PREVIEW_DELTA_SIZE 0.8f /* the change: so much of the value's height */
#define PREVIEW_DELTA_GAP 8.0f
static const Uint8 PREVIEW_ORANGE[3] = {255, 160, 40};
static void field_value(const KhdaysLayoutFrame *fr, const char *style, const KhdaysCampView *v, int field, float dx,
                        float dy)
{
    const KhdaysCampField *f = &v->field[field];
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, style);
    const float ink = s.layout.ink_bottom - s.layout.ink_top;
    char value[32], change[16];
    unsigned short wide[32], wide_change[16];
    value_text(v, field, value, sizeof(value));
    if (!f->has || f->delta == 0 || st == NULL || ink <= 0 || field == 0xf || field == 0x11) {
        number(fr, style, value, dx, dy);
        return;
    }
    {
        const float cap = ink * st->size, small = cap * PREVIEW_DELTA_SIZE;
        float wv, wd, room, x, y;
        snprintf(change, sizeof(change), "%+d", (int)f->delta);
        khdays_layout_utf8(value, wide, 32);
        khdays_layout_utf8(change, wide_change, 16);
        wv = khdays_layout_line_width(&s.layout, wide, st->size);
        wd = khdays_layout_line_width(&s.layout, wide_change, st->size * PREVIEW_DELTA_SIZE);
        room = PREVIEW_DELTA_GAP + wd; /* the value moves over by so much to leave the change its place */
        number(fr, style, value, dx - (st->align == 1 ? room / 2 : st->align == 2 ? room : 0), dy);
        x = dx + st->x + (st->align == 1 ? (wv + room) / 2 - wd : st->align == 2 ? -wd : wv + PREVIEW_DELTA_GAP);
        y = dy + st->y - (st->align == 1 ? cap / 2 : 0);
        camp_text(fr, wide_change, x, y + (cap - small), small, PREVIEW_ORANGE, 0);
    }
}

/* The status panel's pieces with its top left at (px, py) (camp.txt's panel: the menu's place). */
static void draw_status_at(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, float px, float py)
{
    const float *f = place("face"), *ring = place("face_ring"), *tag = place("tag");
    const float *level = place("level"), *rows = place("stats");
    /* the place's colour (the story's, alone: the first place's, as Mission Mode's alone) */
    const Uint8 *own = KHDAYS_PLACE_COLOURS[v->story ? 0 : v->self & 3];
    if (v->character >= 0) {
        char name[16];
        /* the story's own picture of the character when the atlas has one (face<N>_story) */
        snprintf(name, sizeof(name), "face%d_story", v->character);
        if (!v->story || khdays_layout_piece(&s.layout, name) == NULL) {
            snprintf(name, sizeof(name), "face%d", v->character);
        }
        khdays_layout_draw_piece(&s.layout, fr, name, px + f[0], py + f[1], f[2], f[3], 1.0f);
        text(fr, "name", khdays_layout_string(&s.status, NAMES + v->character), px, py);
    }
    /* the face's frame: black in the story (the port's look), the place's colour in Mission Mode */
    khdays_layout_ring_rounded(s.layout.renderer, fr, px + ring[0], py + ring[1], ring[2], ring[3], 0, FACE_RING,
                               v->story ? BLACK : own, 1.0f);
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
        field_value(fr, "level_value", v, LEVEL_FIELDS[i], dx, dy);
    }
    for (int i = 0; i < 9; ++i) {
        const float dy = py + rows[1] + (float)i * rows[3];
        text(fr, "stat_label", khdays_layout_string(&s.status, ROW_LABELS[i]), px, dy);
        if (ROW_FIELDS[i] == 0x13) {
            if (v->field[0x13].has) {
                text(fr, "stat_value", khdays_layout_string(&s.status, (int)v->field[0x13].value[0]), px, dy);
            }
        } else {
            field_value(fr, "stat_value", v, ROW_FIELDS[i], px, dy);
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

static void draw_status(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    draw_status_at(fr, v, s.panel[0], s.panel[1]);
}

/* The status panel whole somewhere else (the story's Panels page): its plate (the menu's backdrop's, with its
 * table's lines) and its pieces, its top left at (x, y), at `alpha` (its pieces whole: the switches fade them in
 * a group). */
static void draw_status_plate(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float x, float y, float alpha)
{
    float tw = 0, th = 0;
    SDL_GetTextureSize(s.backdrop, &tw, &th);
    {
        const SDL_FRect src = {s.panel[0] * tw / KHDAYS_LAYOUT_W, s.panel[1] * th / KHDAYS_LAYOUT_H,
                               s.panel[2] * tw / KHDAYS_LAYOUT_W, s.panel[3] * th / KHDAYS_LAYOUT_H};
        const SDL_FRect dst = khdays_layout_to_window(fr, x, y, s.panel[2], s.panel[3]);
        SDL_SetTextureAlphaModFloat(s.backdrop, alpha);
        SDL_RenderTexture(renderer, s.backdrop, &src, &dst);
        SDL_SetTextureAlphaModFloat(s.backdrop, 1.0f);
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

#define SETTINGS_GEAR 0.62f /* the gear's height: this much of the settings place's */
#define SETTINGS_KEY 0.6f   /* the key's (the pad's button or Esc) */
static void draw_settings_key(const KhdaysLayoutFrame *fr)
{
    const float *at = place("settings");
    const KhdaysLayoutPiece *esc = khdays_layout_piece(&s.layout, "settings_esc");
    const KhdaysLayoutPiece *gear = khdays_layout_piece(&s.layout, "settings_gear");
    /* the gear and the key a little smaller than the place (its middle theirs): the key as tall on the
     * keyboard (Esc) as on the pad */
    const float h = at[3] * SETTINGS_GEAR, key_h = at[3] * SETTINGS_KEY, middle = at[1] + at[3] / 2;
    float x;
    if (gear == NULL || esc == NULL) {
        return;
    }
    x = at[0] - gear->w * h / gear->h;
    khdays_layout_draw_piece(&s.layout, fr, "settings_gear", x, middle - h / 2, gear->w * h / gear->h, h, 1.0f);
    if (khdays_devices_gamepad() != NULL && khdays_devices_pad_in_use()) {
        /* the pad's input for the settings menu (host/app/profile.c) (measured first: drawn with no alpha) */
        const float w = khdays_layout_draw_action(&s.layout, fr, KHDAYS_ACT_OPEN_SETTINGS, 0, 0, key_h, 0.0f);
        khdays_layout_draw_action(&s.layout, fr, KHDAYS_ACT_OPEN_SETTINGS, x - w - at[3] * 0.05f, middle - key_h / 2,
                                  key_h, 1.0f);
    } else {
        /* the keyboard's: Esc, always (KHDAYS_SETTINGS_KEY) */
        const float w = esc->w * key_h / esc->h;
        khdays_layout_draw_piece(&s.layout, fr, "settings_esc", x - w - at[3] * 0.05f, middle - key_h / 2, w, key_h,
                                 1.0f);
    }
}

/* ---- Its opening and closing ------------------------------------------------------------------ */

/* The menu comes in and goes out through black, as KH2's. It opens from black (the DS's pieces the
 * one-screen picture shows a moment before it, hidden). When the game stops showing it -- B at its
 * root, an option's page -- its last view is held, drawn whole, while the black comes over it; only
 * once it is black is the menu let go (the one-screen picture then goes on to whatever the game shows),
 * and the black is held while the game is still dark, lifted as its next picture fades in. B at its root
 * (the view's `leaving`) starts the black at once. */
enum { CURTAIN_NONE, CURTAIN_OPEN, CURTAIN_CLOSE, CURTAIN_HOLD, CURTAIN_LIFT };
#define OPEN_SECONDS 0.25
#define CLOSE_SECONDS 0.22
#define HOLD_LONGEST 4.0
#define LIFT_SECONDS 0.35
#define FRESH_SECONDS 0.5 /* hidden longer than this, the menu opens anew */

static struct {
    int phase;
    double at, from; /* the phase's start (seconds) and the black then */
    double shown_at; /* the game showed the menu last */
    int darkest;     /* the game's darkest since the black was held */
} curtain;

static double seconds_now(void)
{
    return (double)SDL_GetTicksNS() / 1e9;
}

static double eased(double t)
{
    t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    return t * t * (3.0 - 2.0 * t);
}

static double curtain_black(double now)
{
    const double t = now - curtain.at;
    switch (curtain.phase) {
    case CURTAIN_OPEN: return curtain.from * (1.0 - eased(t / OPEN_SECONDS));
    case CURTAIN_CLOSE: return curtain.from + (1.0 - curtain.from) * eased(t / CLOSE_SECONDS);
    case CURTAIN_HOLD: return 1.0;
    case CURTAIN_LIFT: return curtain.from * (1.0 - eased(t / LIFT_SECONDS));
    default: return 0.0;
    }
}

static void curtain_to(int phase, double now)
{
    curtain.from = curtain_black(now);
    curtain.phase = phase;
    curtain.at = now;
}

static void draw_curtain(SDL_Renderer *renderer, double black)
{
    if (black > 0.002) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)(black * 255.0 + 0.5));
        SDL_RenderFillRect(renderer, NULL);
    }
}

/* The black, closed over: the menu let go, the black held till the game's next picture. */
static void curtain_closed(double now)
{
    release_held();
    curtain.phase = CURTAIN_HOLD;
    curtain.at = now;
    curtain.from = 1.0;
    curtain.darkest = khdays_camp_game_brightness();
}

/* The menu is drawn this picture: the game's view (`real`, `leaving` its closing) or the held one. */
static void curtain_shown(int real, int leaving, double now)
{
    if (!real || leaving) {
        if (curtain.phase != CURTAIN_CLOSE) {
            curtain_to(CURTAIN_CLOSE, now);
        } else if (now - curtain.at >= CLOSE_SECONDS && !real) {
            curtain_closed(now);
        }
    } else if (curtain.phase == CURTAIN_CLOSE || curtain.phase == CURTAIN_HOLD || curtain.phase == CURTAIN_LIFT) {
        curtain_to(CURTAIN_OPEN, now); /* back before it went */
    } else if (curtain.phase == CURTAIN_NONE && now - curtain.shown_at > FRESH_SECONDS) {
        curtain.phase = CURTAIN_OPEN;
        curtain.at = now;
        curtain.from = 1.0;
    } else if (curtain.phase == CURTAIN_OPEN && now - curtain.at > OPEN_SECONDS) {
        curtain.phase = CURTAIN_NONE;
    }
    if (real) {
        curtain.shown_at = now;
    }
}

/* The menu is not drawn: the black after it, until the game's next picture fades in; 1 while there is one. */
static int curtain_hidden(SDL_Renderer *renderer, double now)
{
    const int bright = khdays_camp_game_brightness();
    if (curtain.phase == CURTAIN_CLOSE || curtain.phase == CURTAIN_OPEN) {
        /* (the menu could not be drawn while it closed: black at once) */
        curtain_closed(now);
    } else if (curtain.phase == CURTAIN_HOLD) {
        const int rising = bright >= curtain.darkest + 3; /* the game's next picture fading in */
        const int never_dark = curtain.darkest > -3 && now - curtain.at > 0.2;
        if (bright < curtain.darkest) {
            curtain.darkest = bright;
        }
        if (rising || never_dark || now - curtain.at > HOLD_LONGEST) {
            curtain_to(CURTAIN_LIFT, now);
        }
    } else if (curtain.phase == CURTAIN_LIFT && now - curtain.at > LIFT_SECONDS) {
        curtain.phase = CURTAIN_NONE;
    }
    if (curtain.phase == CURTAIN_NONE) {
        return 0;
    }
    draw_curtain(renderer, curtain_black(now));
    return 1;
}

/* ---- The Files pages' state (see "The Files pages" below) --------------------------------------- */

/* Roxas's Diary and Enemy Profiles (runtime/menus/camp.h KhdaysCampFiles; layouts/files/files.txt): the camp's
 * bands over a drifting pattern; the list in the player's rows -- the day or number, the title or name, KH2's
 * NEW! -- with the glove and firefly, gliding when it scrolls; beside it the DS's upper screen (the entry as the
 * game draws it, its A mark when A shows another page) in a floating black frame. The lower screen not shown. */
#define FILES_SCROLL_RATE 14.0 /* the rows and the glove follow their places: e^-rate of the way left a second */
#define FILES_GLOVE_RATE 30.0
#define FILES_FRESH 0.5 /* the page not drawn longer than this: everything put in its place at once */
#define FILES_ARROW_BOB 5.0f
#define FILES_ARROW_PERIOD 0.8
#define FILES_NEW_PERIOD 1.1
#define FILES_NEW_VIEWED 0.45f  /* the badge once viewed: this dark */
#define FILES_SHADOW_ALPHA 0.4f /* the DS's screen's shadow */
#define FILES_SHADOW_OFFSET 12.0f
#define FILES_WORD 16

static struct {
    int tried, ok;
    KhdaysLayout layout;
    float rows[6]; /* the first row's left and top, a row's width and height, the pitch, how many show */
    float pattern_scale, pattern_dx, pattern_dy;
    float advance[10], digit_ink;
    unsigned short day_word[LANGUAGES][FILES_WORD], number_word[LANGUAGES][FILES_WORD];
    SDL_Texture *pattern;
    int pattern_w, pattern_h;
    /* the DS's upper screen as the one-screen picture handed it over, and its texture */
    SDL_SpinLock lock;
    uint32_t pixels[256 * 192];
    unsigned serial, uploaded;
    SDL_Texture *ds, *sharp; /* sharp: the screen N times over, nearest, when the renderer has no pixel art mode */
    int pixel_art, sharp_k;
    volatile int frozen; /* the page going (the menu coming back): the screen kept as it was, not the game's
                          * teardown */
    int dots; /* the screen on the card (gfx_render.h khdays_gfx_render_shop): its texels a DS dot as shown */
    /* the DS's A mark (the game's on a row whose other page A shows: tools/ds_hint.py's frames) */
    SDL_Texture *hint;
    int hint_w, hint_h;
    float hint_cell[2];
    /* the list's motion: the rows' first shown (a fraction while they glide), the glove's row */
    double drawn_at;
    float top, glove;
    int mode, count;
} fl;
#define FILES_IN_SECONDS 0.3 /* the Files pages' pieces sliding in, or the menu's back */

/* ---- The menu and its Preferences page, the one into the other -------------------------------- */

/* Preferences chosen, the game fades the menu to black, loads its Preferences page (the camp's
 * screen 5) in its place and fades that in; B on the page, the same back. The two look alike but for
 * the middle and the right -- the model, the trials and the status panel against the page's tabs and
 * settings -- so the window's screen keeps the picture up instead of the black: the camp's next
 * screen (the view's `next`) says where it goes while the one still shows; the menu's own pieces
 * slide and fade out while the status panel grows to the left over the model's box and the trials into
 * the page's panel (the page's backdrop is the menu's with its status panel that wide: see
 * draw_grown); once the game shows the page (its view read) the page's pieces slide in. Back, the
 * page's panel shrinks to the status panel over the model come back. The options, the numbers, the
 * settings key and the members stay where they are, the options taking the page's look on the way.
 * The game's fade is left undrawn until it is over. */
enum { SW_NONE, SW_OUT, SW_WAIT, SW_IN };
#define SW_OUT_SECONDS 0.2
#define SW_GROW_SECONDS                                                                                                \
    0.2 /* the status panel over the model's box: fast first, before the game takes the
                             * model down (its fade is 100 ms) */
#define SW_IN_SECONDS 0.25
#define SW_RETIRE_SECONDS 0.42 /* back on the menu: the panel shrinks back slower than the pieces come */
#define SW_SETTLE_SECONDS 0.3  /* the destination shown but its brightness not rising: in anyway */
#define SW_WAIT_LONGEST 2.0    /* the destination not come: the game went elsewhere (the curtain's way) */
#define SW_SLIDE 40.0f         /* layout units the pieces slide: deeper to the left, back to the right */
#define MENU_SCREEN 0
#define PAGE_SCREEN 5
#define FILES_SCREEN 7  /* the story's Files pages (see "The menu and the Files pages") */
#define FILES_OPTION 2  /* the story's menu's Files */
#define PANELS_SCREEN 2 /* the story's Panels page (see "The story's Panels page") */

static struct {
    int phase;
    int to_page;         /* 1 the menu goes to its page, 0 the page comes back */
    int files;           /* the page the Files pages, not Preferences */
    int save;            /* the page the save page (see "The menu and the save page") */
    int panels;          /* the page the story's Panels page */
    unsigned serial;     /* the Files' DS screen's when the page's view came: in once a newer one is there */
    double at;           /* the phase's start */
    double came;         /* SW_WAIT: when the destination's view came (0 not yet) */
    KhdaysCampView from; /* the source's view: its own pieces drawn going out */
    int unshaded;        /* the game's fade left undrawn until its brightness is back */
    SDL_Texture *group;  /* the window's size: a group of pieces drawn into it, then over the window with one
                          * alpha, moved */
    int group_w, group_h;
} sw;

static void forget_group(void)
{
    if (sw.group != NULL) {
        SDL_DestroyTexture(sw.group);
        sw.group = NULL;
    }
    sw.group_w = sw.group_h = 0;
}

/* Begins a group: what is drawn until group_end goes into a cleared texture of the window's size, to
 * be laid over the window as one; 0 without one (the pieces drawn straight, whole). */
static int group_begin(SDL_Renderer *renderer)
{
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) {
        return 0;
    }
    if (sw.group != NULL && (sw.group_w != w || sw.group_h != h)) {
        forget_group();
    }
    if (sw.group == NULL) {
        sw.group = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (sw.group == NULL) {
            SDL_Log("camp menu: no texture for its pieces to slide in: %s", SDL_GetError());
            return 0;
        }
        sw.group_w = w;
        sw.group_h = h;
        /* drawn over nothing, the texture holds its colours multiplied by their alpha */
        SDL_SetTextureBlendMode(sw.group, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureScaleMode(sw.group, SDL_SCALEMODE_LINEAR);
    }
    if (!SDL_SetRenderTarget(renderer, sw.group)) {
        return 0;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return 1;
}

/* The group over the window at `alpha`, moved `dx` layout units. */
static void group_end(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float alpha, float dx)
{
    const SDL_FRect dst = {dx * fr->scale, 0, (float)sw.group_w, (float)sw.group_h};
    SDL_SetRenderTarget(renderer, NULL);
    alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
    SDL_SetTextureColorModFloat(sw.group, alpha, alpha, alpha);
    SDL_SetTextureAlphaModFloat(sw.group, alpha);
    SDL_RenderTexture(renderer, sw.group, NULL, &dst);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

/* The menu's own pieces (the ones the page has not): the status panel, the trials, the description,
 * the question's answers, the prompt, the story's day or Mission Mode's notice. */
static void tab_button(const KhdaysLayoutFrame *fr, const char *name, const unsigned short *label, float alpha,
                       int kh2);

/* The story's Holo-Missions or Files open on the menu (their two sub-options in the list's place): the
 * option beside the MENU pill. */
static void menu_sub_button(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    if (v->story && v->sub >= 1 && v->sub <= 2) {
        tab_button(fr, "menu_button", s.story_option[v->sub == 1 ? 1 : FILES_OPTION], 1.0f, story_on(v));
    }
}

static void draw_menu_group(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    menu_sub_button(fr, v);
    draw_status(fr, v);
    draw_trials(fr, v);
    draw_description(fr, v);
    if (v->question) {
        draw_answers(fr, v);
    }
    draw_prompt(fr, v);
    if (v->story) {
        draw_day(fr, v);
    } else {
        draw_notice(fr, v);
    }
}

static void option_button(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, const char *name, int option,
                          float alpha);

/* The page's own: the menu's Preferences beside its MENU pill, its tabs and settings, the chosen
 * setting's help, its prompt. */
static void draw_page_group(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, int lit)
{
    option_button(fr, v, "menu_button", v->selected, 1.0f);
    draw_page(fr, v, lit);
    draw_setting_help(fr, v);
    draw_prompt(fr, v);
}

/* The pieces both have: the options (`page` their look, see draw_options), the numbers, the settings
 * key, Mission Mode's members. */
static void draw_common(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, float page)
{
    draw_options(fr, v, page);
    big_digits(fr, v->munny, place("munny"), s.munny_colour);
    hearts_word(fr);
    big_digits(fr, v->hearts, place("hearts"), s.hearts_colour);
    draw_settings_key(fr);
    if (!v->story) {
        draw_members(fr, v);
    }
}

/* A backdrop over the whole picture at `alpha`. */
static void draw_backdrop(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, SDL_Texture *backdrop, float alpha)
{
    const SDL_FRect dst = khdays_layout_to_window(fr, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H);
    if (backdrop == NULL || alpha <= 0.002f) {
        return;
    }
    SDL_SetTextureAlphaModFloat(backdrop, alpha < 1.0f ? alpha : 1.0f);
    SDL_RenderTexture(renderer, backdrop, NULL, &dst);
    SDL_SetTextureAlphaModFloat(backdrop, 1.0f);
}

/* The menu's backdrop again around the model's window: the model shows only in it, as the DS's frame
 * cuts it (its weapons reach out of it). */
static void draw_strips(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr)
{
    const float *m = s.window;
    const float strips[4][4] = {{0, 0, KHDAYS_LAYOUT_W, m[1]},
                                {0, m[1] + m[3], KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H - m[1] - m[3]},
                                {0, m[1], m[0], m[3]},
                                {m[0] + m[2], m[1], KHDAYS_LAYOUT_W - m[0] - m[2], m[3]}};
    float tw = 0, th = 0;
    SDL_GetTextureSize(s.backdrop, &tw, &th);
    for (int i = 0; i < 4; ++i) {
        const float *r = strips[i];
        const SDL_FRect src = {r[0] * tw / KHDAYS_LAYOUT_W, r[1] * th / KHDAYS_LAYOUT_H, r[2] * tw / KHDAYS_LAYOUT_W,
                               r[3] * th / KHDAYS_LAYOUT_H};
        const SDL_FRect dst = khdays_layout_to_window(fr, r[0], r[1], r[2], r[3]);
        if (r[2] > 0 && r[3] > 0) {
            SDL_RenderTexture(renderer, s.backdrop, &src, &dst);
        }
    }
}

/* The page's panel, its left at `left` (the layout's units): the status panel (camp.txt's panel) grown
 * to the left. The page's backdrop (the Menu Overhaul's camp_page_backdrop.png) is the menu's with
 * the status panel made as wide as the model's box and the panel together, of the status panel's own
 * texels: its left edge (GROW_LEFT texels, the highlight and the bevel), its middle (one shade a row,
 * the same across: GROW_MIDDLE's column stretched), its right edge (GROW_RIGHT) -- so drawn with its
 * left at the status panel's it is the status panel, at the model's box's it is the page's. Under it,
 * the page's ground over what reaches below the panel (the trials' bar); over it, the table's lines
 * of the status panel at `lines` (the backdrop's: white at 50/255). */
#define GROW_LEFT 24.0f
#define GROW_MIDDLE 32.0f
#define GROW_RIGHT 20.0f
#define STAT_LINE_ALPHA (50.0f / 255.0f)
#define STAT_ROWS 10

static void draw_grown(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float left, float lines)
{
    const float px = s.panel[0], py = s.panel[1], pw = s.panel[2], ph = s.panel[3], right = px + pw;
    const float page_x = s.trials[0], below = s.trials[1] + s.trials[3] + 4.0f;
    float tw = 0, th = 0, kx, ky;
    SDL_GetTextureSize(s.page_backdrop, &tw, &th);
    kx = tw / KHDAYS_LAYOUT_W;
    ky = th / KHDAYS_LAYOUT_H;
    SDL_SetTextureAlphaModFloat(s.page_backdrop, 1.0f);
    if (below > py + ph && right > left) {
        const SDL_FRect src = {left * kx, (py + ph) * ky, (right - left) * kx, (below - py - ph) * ky};
        const SDL_FRect dst = khdays_layout_to_window(fr, left, py + ph, right - left, below - py - ph);
        SDL_RenderTexture(renderer, s.page_backdrop, &src, &dst);
    }
    {
        const float middle_w = right - GROW_RIGHT - left - GROW_LEFT;
        const SDL_FRect src[3] = {{page_x * kx, py * ky, GROW_LEFT * kx, ph * ky},
                                  {(page_x + GROW_MIDDLE) * kx, py * ky, 1.0f * kx, ph * ky},
                                  {(right - GROW_RIGHT) * kx, py * ky, GROW_RIGHT * kx, ph * ky}};
        const SDL_FRect dst[3] = {khdays_layout_to_window(fr, left, py, GROW_LEFT, ph),
                                  khdays_layout_to_window(fr, left + GROW_LEFT, py, middle_w, ph),
                                  khdays_layout_to_window(fr, right - GROW_RIGHT, py, GROW_RIGHT, ph)};
        for (int i = 0; i < 3; ++i) {
            if (dst[i].w > 0) {
                SDL_RenderTexture(renderer, s.page_backdrop, &src[i], &dst[i]);
            }
        }
    }
    if (lines > 0.002f) {
        const float *line = place("stat_line"), *rows = place("stats");
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColorFloat(renderer, 1.0f, 1.0f, 1.0f, STAT_LINE_ALPHA * (lines < 1.0f ? lines : 1.0f));
        for (int i = 0; i < STAT_ROWS && line[2] > 0; ++i) {
            const SDL_FRect r =
                khdays_layout_to_window(fr, px + line[0], py + line[1] + (float)i * rows[3], line[2], line[3]);
            SDL_RenderFillRect(renderer, &r);
        }
    }
}

static double eased_out(double t)
{
    t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
}

/* Whether the page's pieces are in the layout (else the page is the game's own). */
static int save_ready(void);
static KhdaysSaveView save_from; /* the save list as the save page going showed it */
static KhdaysSaveView save_list; /* the save list as it showed last (the save page's) */

static int page_ready(void)
{
    return s.page_backdrop != NULL && s.page_rows[5] > 0 && s.page_tabs[2] > 0 &&
           khdays_layout_piece(&s.layout, "page_row") != NULL;
}

/* Whether the camp goes from the menu to its page (1) or back (2), to the Files pages (3) or back (4), to
 * the save page (5) or back (6), to the story's Panels page (7) or back (8): the game's view's `next` turned to
 * the other screen while this one still shows (before, it was this one's own, or where it came from).
 * Only the game's own views count, each against the one before it. */
static int switch_wanted(const KhdaysCampView *v, int real)
{
    static int had, config, files, save, panels, next;
    int wanted = 0;
    if (!real) {
        return 0;
    }
    if (had && config == v->config && files == v->files && save == v->save && panels == v->panels && next != v->next &&
        !v->leaving) {
        const int menu = !v->config && !v->files && !v->save && !v->panels;
        if (menu && v->save_screen > 0 && v->next == v->save_screen) {
            wanted = 5;
        } else if (menu && v->next == PAGE_SCREEN) {
            wanted = 1;
        } else if (v->config && v->next == MENU_SCREEN) {
            wanted = 2;
        } else if (v->story && menu && v->next == FILES_SCREEN) {
            wanted = 3;
        } else if (v->files && v->next == MENU_SCREEN) {
            wanted = 4;
        } else if (v->save && v->next == MENU_SCREEN) {
            wanted = 6;
        } else if (menu && v->next == PANELS_SCREEN && story_on(v)) {
            wanted = 7;
        } else if (v->panels && v->next == MENU_SCREEN) {
            wanted = 8;
        }
    }
    had = 1;
    config = v->config;
    files = v->files;
    save = v->save;
    panels = v->panels;
    next = v->next;
    return wanted;
}

/* Whether the view is the switch's destination's (the game's, or held). */
static int switch_there(const KhdaysCampView *v)
{
    if (sw.panels) {
        return sw.to_page ? v->panels != 0 : !v->panels && !v->files && !v->config && !v->save;
    }
    if (sw.save) {
        return sw.to_page ? v->save != 0 : !v->save && !v->files && !v->config;
    }
    if (sw.files) {
        return sw.to_page ? v->files != 0 : !v->files && !v->config;
    }
    return (v->config != 0) == sw.to_page;
}

static void switch_off(void)
{
    sw.phase = SW_NONE;
    sw.unshaded = 0;
    fl.frozen = 0;
}

/* Starts, moves on or ends the switch for this picture. */
static int panels_ready(const KhdaysCampView *v);

static void switch_step(const KhdaysCampView *v, int got, double now)
{
    const int wanted = switch_wanted(v, got == 2);
    if (sw.phase == SW_NONE) {
        if ((wanted == 1 || wanted == 2)   ? page_ready()
            : (wanted == 5 || wanted == 6) ? save_ready()
            : (wanted == 7 || wanted == 8) ? fl.ok && panels_ready(v)
                                           : wanted != 0 && fl.ok) {
            sw.phase = SW_OUT;
            sw.to_page = wanted == 1 || wanted == 3 || wanted == 5 || wanted == 7;
            sw.files = wanted == 3 || wanted == 4;
            sw.save = wanted == 5 || wanted == 6;
            sw.panels = wanted == 7 || wanted == 8;
            save_from = save_list;
            sw.at = now;
            sw.came = 0;
            sw.from = *v;
            sw.unshaded = 1;
            curtain.phase = CURTAIN_NONE; /* (none to speak of: the menu is open) */
            /* the page going: its DS screen kept as it is (the game's next pictures there are its teardown) */
            fl.frozen = sw.files && !sw.to_page;
        }
        return;
    }
    if (v->leaving) {
        switch_off(); /* B at the root meanwhile: the curtain's black, as ever */
        return;
    }
    if (sw.phase == SW_OUT && now - sw.at >= SW_OUT_SECONDS) {
        sw.phase = SW_WAIT;
        sw.at = now;
    }
    if (sw.phase == SW_WAIT) {
        /* the destination's view read: the page's pieces come at once; the menu's once the game's
         * brightness rises again (its model loaded by then), or soon anyway */
        const int there = got == 2 && switch_there(v);
        if (there && sw.came == 0) {
            sw.came = now;
            sw.serial = fl.serial;
            fl.frozen = 0; /* the menu back: (its view stops the Files' screen anyway) */
        }
        /* the Files pages: once the game's picture comes up (and its DS screen made since: the page's,
         * not the last one's), as the menu's */
        /* the Panels page: once its brightness rises (its grid's panels made), as the menu's */
        if (there && (sw.files && sw.to_page && fl.serial == sw.serial && now - sw.came < SW_SETTLE_SECONDS)) {
            /* (not yet) */
        } else if (there &&
                   ((sw.to_page && !sw.files && !sw.panels) || v->master > -14 || now - sw.came >= SW_SETTLE_SECONDS)) {
            sw.phase = SW_IN;
            sw.at = now;
        } else if (!there && now - sw.at > SW_WAIT_LONGEST) {
            switch_off();
        }
    }
    if (sw.phase == SW_IN && now - sw.at >= (sw.files || sw.save || sw.panels ? FILES_IN_SECONDS
                                             : sw.to_page                     ? SW_IN_SECONDS
                                                                              : SW_RETIRE_SECONDS)) {
        sw.phase = SW_NONE;
        fl.frozen = 0;
    }
}

static void draw_story_page_switch(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, int got,
                                   double now);

/* The picture while the two switch: the common pieces over the page's backdrop coming or going over
 * the menu's, the source's own pieces sliding out, the destination's sliding in. */
static void draw_page_switch(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, int got,
                             double now)
{
    const double t = now - sw.at;
    const float out = sw.phase == SW_OUT ? (float)eased(t / SW_OUT_SECONDS) : 1.0f;
    const float in = sw.phase == SW_IN ? (float)eased(t / SW_IN_SECONDS) : 0.0f;
    const float page = sw.to_page ? (sw.phase == SW_OUT ? out : 1.0f) : (sw.phase == SW_OUT ? 1.0f - out : 0.0f);
    const float dir = sw.to_page ? -1.0f : 1.0f;
    const int there = (v->config != 0) == sw.to_page; /* the destination's view (the game's, or held) */
    const float wide = s.panel[0] - s.trials[0];      /* how far the panel grows */
    float left = s.trials[0], lines = 0.0f;
    if (story_on(v)) {
        draw_story_page_switch(renderer, fr, v, got, now);
        return;
    }
    if (sw.to_page && sw.phase == SW_OUT) {
        left = s.panel[0] - wide * (float)eased_out(t / SW_GROW_SECONDS);
        lines = 1.0f - out;
    } else if (!sw.to_page && sw.phase == SW_IN) {
        left = s.trials[0] + wide * (float)eased(t / SW_RETIRE_SECONDS);
        lines = in;
    }
    khdays_camp_set_page_drawn(got == 2 && v->config);
    khdays_layout_begin_over(renderer, fr);
    if (v->config) {
        /* the page's backdrop whole, at the window's own resolution (see khdays_camp_screen_draw) */
        draw_backdrop(renderer, fr, s.page_backdrop, 1.0f);
    } else if (!applied) {
        /* the one-screen picture did not take the model's window this frame (the game's screen is the
         * page's, its view not read yet): the menu's backdrop whole under everything */
        draw_backdrop(renderer, fr, s.backdrop, 1.0f);
    } else {
        draw_strips(renderer, fr);
    }
    draw_grown(renderer, fr, left, lines);
    draw_common(fr, v, page);
    if (sw.phase == SW_OUT) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            draw_menu_group(fr, &sw.from);
        } else {
            draw_page_group(fr, &sw.from, 0);
        }
        if (grouped) {
            group_end(renderer, fr, 1.0f - out, dir * SW_SLIDE * out);
        }
    }
    if (sw.phase == SW_IN && there) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            draw_page_group(fr, v, 0);
        } else {
            draw_menu_group(fr, v);
        }
        if (grouped) {
            group_end(renderer, fr, in, -dir * SW_SLIDE * (1.0f - in));
        }
    }
    khdays_layout_end(renderer, fr);
}

/* ---- The Files pages -------------------------------------------------------------------------- */

static int files_unavailable(void)
{
    return fl.tried && !fl.ok;
}

void khdays_camp_screen_upper(const uint32_t *pixels)
{
    if (fl.frozen) {
        return;
    }
    SDL_LockSpinlock(&fl.lock);
    if (pixels != NULL) {
        memcpy(fl.pixels, pixels, sizeof(fl.pixels));
    }
    ++fl.serial;
    SDL_UnlockSpinlock(&fl.lock);
}

int khdays_camp_screen_upper_wanted(int *scale)
{
    *scale = fl.dots;
    return !fl.frozen;
}

static void forget_files(void)
{
    if (fl.pattern != NULL) {
        SDL_DestroyTexture(fl.pattern);
    }
    if (fl.ds != NULL) {
        SDL_DestroyTexture(fl.ds);
    }
    if (fl.sharp != NULL) {
        SDL_DestroyTexture(fl.sharp);
    }
    if (fl.hint != NULL) {
        SDL_DestroyTexture(fl.hint);
    }
    fl.pattern = fl.ds = fl.sharp = fl.hint = NULL;
    fl.sharp_k = 0;
    fl.uploaded = fl.serial - 1u;
    khdays_layout_forget(&fl.layout);
    memset(fl.rows, 0, sizeof(fl.rows));
    fl.digit_ink = 0;
    fl.ok = 0;
    fl.tried = 0;
}

static void files_line(char *line, const char *folder, void *ctx)
{
    float a, b, c, d, e, f;
    int n;
    char file[256] = "", lang[8] = "", day[FILES_WORD * 2] = "", number[FILES_WORD * 2] = "";
    (void)ctx;
    if (khdays_layout_line(&fl.layout, line, folder)) {
        return;
    }
    if (SDL_sscanf(line, "rows %f %f %f %f %f %f", &a, &b, &c, &d, &e, &f) == 6) {
        fl.rows[0] = a, fl.rows[1] = b, fl.rows[2] = c, fl.rows[3] = d, fl.rows[4] = e, fl.rows[5] = f;
    } else if (SDL_sscanf(line, "pattern %255s %f %f %f", file, &a, &b, &c) == 4 && fl.pattern == NULL) {
        char path[1300];
        snprintf(path, sizeof(path), "%s%s", folder, file);
        fl.pattern = khdays_layout_texture(&fl.layout, path, 0, &fl.pattern_w, &fl.pattern_h);
        fl.pattern_scale = a, fl.pattern_dx = b, fl.pattern_dy = c;
    } else if (SDL_sscanf(line, "hint %255s %f %f", file, &a, &b) == 3 && fl.hint == NULL && a > 0 && b > 0) {
        char path[1300];
        snprintf(path, sizeof(path), "%s%s", folder, file);
        fl.hint = khdays_layout_texture(&fl.layout, path, 0, &fl.hint_w, &fl.hint_h);
        if (fl.hint != NULL && !SDL_SetTextureScaleMode(fl.hint, SDL_SCALEMODE_PIXELART)) {
            SDL_SetTextureScaleMode(fl.hint, SDL_SCALEMODE_NEAREST);
        }
        fl.hint_cell[0] = a, fl.hint_cell[1] = b;
    } else if (SDL_sscanf(line, "digit_ink %f", &a) == 1) {
        fl.digit_ink = a;
    } else if (SDL_sscanf(line, "digit %d %f", &n, &a) == 2 && n >= 0 && n <= 9) {
        fl.advance[n] = a;
    } else if (SDL_sscanf(line, "label %7s %31s %31s", lang, day, number) == 3) {
        for (int i = 1; i < LANGUAGES; ++i) {
            if (strcmp(lang, CODES[i]) == 0) {
                khdays_layout_utf8(day, fl.day_word[i], FILES_WORD);
                khdays_layout_utf8(number, fl.number_word[i], FILES_WORD);
            }
        }
    }
}

static void files_load(SDL_Renderer *renderer)
{
    char path[1024], folder[1024], *slash;
    static const char *const needed[] = {"button", "button_selected", "arrow", "new", "gdigit0"};
    forget_files();
    fl.tried = 1;
    fl.layout.who = "files page";
    fl.layout.renderer = renderer;
    if (!khdays_mods_find("layouts/files/files.txt", path, (int)sizeof(path))) {
        SDL_Log("files page: no layouts/files/files.txt (the game's own screens shown)");
        return;
    }
    snprintf(folder, sizeof(folder), "%s", path);
    slash = strrchr(folder, '/');
    if (strrchr(folder, '\\') > slash) {
        slash = strrchr(folder, '\\');
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
    each_line(path, files_line, folder, NULL);
    fl.ok = khdays_layout_ready(&fl.layout) && fl.pattern != NULL && fl.rows[2] > 0 && fl.rows[5] >= 1 &&
            fl.digit_ink > 0 && khdays_layout_place(&fl.layout, "screen") != NULL;
    for (size_t i = 0; i < sizeof(needed) / sizeof(needed[0]); ++i) {
        fl.ok = fl.ok && khdays_layout_piece(&fl.layout, needed[i]) != NULL;
    }
    if (!fl.ok) {
        SDL_Log("files page: %s is incomplete (the game's own screens shown)", path);
        khdays_layout_forget(&fl.layout);
    }
}

static const float *files_place(const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&fl.layout, name);
    return p != NULL ? &p->x : none;
}

/* The pattern over a part of the picture (x, y, w, h: the layout's), at `alpha`, wrapped (a power-of-two
 * tile: every renderer repeats it), its place moving with the time -- the same place whatever part is
 * drawn. */
static void files_pattern_part(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, double now, float x, float y,
                               float w, float h, float alpha)
{
    const float tw = (float)fl.pattern_w * fl.pattern_scale, th = (float)fl.pattern_h * fl.pattern_scale;
    const SDL_FRect r = khdays_layout_to_window(fr, x, y, w, h);
    /* the tile's coordinates at the picture's corners: the drift moves the pattern, so the corner reads
     * the tile where the pattern has come from */
    const float u0 = (float)fmod(-now * fl.pattern_dx / tw, 1.0) + x / tw,
                v0 = (float)fmod(-now * fl.pattern_dy / th, 1.0) + y / th;
    const float u1 = u0 + w / tw, v1 = v0 + h / th;
    const float px[4] = {r.x, r.x + r.w, r.x, r.x + r.w}, py[4] = {r.y, r.y, r.y + r.h, r.y + r.h};
    const float tu[4] = {u0, u1, u0, u1}, tv[4] = {v0, v0, v1, v1};
    static const int order[6] = {0, 1, 2, 2, 1, 3};
    SDL_Vertex v[4];
    if (w <= 0 || h <= 0 || alpha <= 0.002f) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        v[i].position.x = px[i];
        v[i].position.y = py[i];
        v[i].color.r = v[i].color.g = v[i].color.b = 1.0f;
        v[i].color.a = alpha < 1.0f ? alpha : 1.0f;
        v[i].tex_coord.x = tu[i];
        v[i].tex_coord.y = tv[i];
    }
    SDL_SetRenderTextureAddressMode(renderer, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP);
    SDL_RenderGeometry(renderer, fl.pattern, v, 4, order, 6);
    SDL_SetRenderTextureAddressMode(renderer, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
}

static void files_pattern(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, double now)
{
    files_pattern_part(renderer, fr, now, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H, 1.0f);
}

/* The camp's top and bottom bands from its own backdrop: what the menu shows there (the MENU pill, the
 * money's word, the heart). */
static void files_bands(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr)
{
    const float bands[2][2] = {{0, 192}, {856, KHDAYS_LAYOUT_H - 856}};
    float tw = 0, th = 0;
    SDL_GetTextureSize(s.backdrop, &tw, &th);
    for (int i = 0; i < 2; ++i) {
        const SDL_FRect src = {0, bands[i][0] * th / KHDAYS_LAYOUT_H, tw, bands[i][1] * th / KHDAYS_LAYOUT_H};
        const SDL_FRect dst = khdays_layout_to_window(fr, 0, bands[i][0], KHDAYS_LAYOUT_W, bands[i][1]);
        SDL_RenderTexture(renderer, s.backdrop, &src, &dst);
    }
}

/* The DS's upper screen in its frame: its black edge and its shadow (left where it is while the screen
 * floats), its pixels sharp -- the renderer's pixel art scaling, or else the screen drawn N whole times
 * as large and that scaled smoothly. */
/* The DS's A mark over its screen (dst: the screen's rectangle in the window), as the game shows it on
 * the chosen row when A shows another page of it (the diary's rewritten entry, an enemy's drops): its
 * frames lit and blinking (the DS's 15-frame steps), dark while A is held. */
#define FILES_HINT_STEP 0.25
static void files_hint(SDL_Renderer *renderer, const KhdaysCampView *v, const SDL_FRect *dst, double now)
{
    const KhdaysCampFiles *f = &v->file;
    const float *at = files_place("hint"), cw = fl.hint_cell[0], ch = fl.hint_cell[1];
    const float k = dst->w / 256.0f;
    int frame;
    if (fl.hint == NULL || f->count <= 0 || f->cursor < 0 || f->cursor >= f->rows || !f->row[f->cursor].alternate ||
        cw <= 0 || (float)fl.hint_h < ch * (float)(f->mode + 1)) {
        return;
    }
    frame = f->held ? 0 : 2 + (int)fmod(now / FILES_HINT_STEP, 2.0);
    {
        const SDL_FRect src = {cw * (float)frame, ch * (float)f->mode, cw, ch};
        const SDL_FRect to = {dst->x + at[0] * k, dst->y + at[1] * k, cw * k, ch * k};
        SDL_RenderTexture(renderer, fl.hint, &src, &to);
    }
}

static void files_screen(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, const KhdaysCampView *view, double now)
{
    const float *at = files_place("screen"), *fl_place = files_place("float");
    const float edge = files_place("screen_edge")[0];
    const float rise = fl_place[1] > 0 ? fl_place[0] * (float)sin(now * 6.28318530718 / fl_place[1]) : 0.0f;
    const float x = at[0], y = at[1] - rise, w = at[2], h = at[3];
    SDL_FRect dst;
    unsigned serial;
    if (fl.ds == NULL) {
        fl.ds = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
        if (fl.ds == NULL) {
            return;
        }
        fl.pixel_art = SDL_SetTextureScaleMode(fl.ds, SDL_SCALEMODE_PIXELART);
        if (!fl.pixel_art) {
            SDL_SetTextureScaleMode(fl.ds, SDL_SCALEMODE_NEAREST);
        }
        fl.uploaded = fl.serial - 1u;
    }
    SDL_LockSpinlock(&fl.lock);
    serial = fl.serial;
    if (serial != fl.uploaded) {
        SDL_UpdateTexture(fl.ds, NULL, fl.pixels, 256 * 4);
    }
    SDL_UnlockSpinlock(&fl.lock);
    khdays_layout_fill_rounded(renderer, fr, x - edge + FILES_SHADOW_OFFSET, at[1] - edge + FILES_SHADOW_OFFSET,
                               w + 2 * edge, h + 2 * edge, edge * 1.5f, BLACK,
                               FILES_SHADOW_ALPHA * (1.0f - 0.25f * rise / (fl_place[0] > 0 ? fl_place[0] : 1.0f)));
    khdays_layout_fill_rounded(renderer, fr, x - edge, y - edge, w + 2 * edge, h + 2 * edge, edge, BLACK, 1.0f);
    dst = khdays_layout_to_window(fr, x, y, w, h);
    {
        /* the screen as the card composed it, the HD tiles and fonts in it (0.1.4's text is laid out for them),
         * about as large as shown */
        const int n = (int)SDL_ceilf(dst.w / 256.0f);
        int scale = 1;
        unsigned made;
        SDL_Texture *hd = khdays_gfx_render_shop(renderer, 0, KHDAYS_SHOP_NORMAL, &scale, &made);
        fl.dots = n < 2 ? 2 : n > KHDAYS_SHOP_SCALE ? KHDAYS_SHOP_SCALE : n;
        if (hd != NULL) {
            SDL_SetTextureBlendMode(hd, SDL_BLENDMODE_NONE);
            SDL_RenderTexture(renderer, hd, NULL, &dst);
            files_hint(renderer, view, &dst, now);
            fl.uploaded = serial;
            return;
        }
    }
    if (fl.pixel_art) {
        SDL_RenderTexture(renderer, fl.ds, NULL, &dst);
        files_hint(renderer, view, &dst, now);
    } else {
        /* the screen k times as large, nearest (whole pixels), then scaled smoothly to its place */
        const int k = SDL_max(1, (int)SDL_ceilf(dst.w / 256.0f));
        if (fl.sharp == NULL || fl.sharp_k != k) {
            if (fl.sharp != NULL) {
                SDL_DestroyTexture(fl.sharp);
            }
            fl.sharp =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 256 * k, 192 * k);
            fl.sharp_k = k;
            fl.uploaded = serial - 1u;
            if (fl.sharp != NULL) {
                SDL_SetTextureScaleMode(fl.sharp, SDL_SCALEMODE_LINEAR);
            }
        }
        if (fl.sharp == NULL) {
            SDL_RenderTexture(renderer, fl.ds, NULL, &dst);
            files_hint(renderer, view, &dst, now);
            fl.uploaded = serial;
            return;
        }
        if (serial != fl.uploaded) {
            /* (drawn into it while a group's texture is the target: back to that one after) */
            SDL_Texture *target = SDL_GetRenderTarget(renderer);
            if (SDL_SetRenderTarget(renderer, fl.sharp)) {
                SDL_RenderTexture(renderer, fl.ds, NULL, NULL);
                SDL_SetRenderTarget(renderer, target);
            }
        }
        SDL_RenderTexture(renderer, fl.sharp, NULL, &dst);
        files_hint(renderer, view, &dst, now);
    }
    fl.uploaded = serial;
}

/* A line in the Files layout's font, its capitals' top at y, `cap` high, its shadow first; its width. */
static float files_text(const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y, float cap,
                        float width_most)
{
    const float ink = fl.layout.ink_bottom - fl.layout.ink_top;
    float size = ink > 0 ? cap / ink : 1.0f;
    float w;
    if (t == NULL || t[0] == 0) {
        return 0;
    }
    w = khdays_layout_line_width(&fl.layout, t, size);
    if (width_most > 0 && w > width_most) {
        size *= width_most / w; /* a long title: smaller, its capitals' middle kept */
        y += (cap - ink * size) / 2;
        w = width_most;
    }
    khdays_layout_draw_line(&fl.layout, fr, t, x, y, size, 255.0f, 1.0f, 1);
    khdays_layout_draw_line(&fl.layout, fr, t, x, y, size, 255.0f, 1.0f, 0);
    return w;
}

/* KHGummi's digits, their right at x, their ink `h` high from y; their width. */
static float files_digits(const KhdaysLayoutFrame *fr, int value, float right, float y, float h, int draw)
{
    char digits[12];
    const float k = h / fl.digit_ink;
    float w = 0, x;
    snprintf(digits, sizeof(digits), "%d", value < 0 ? 0 : value);
    for (const char *c = digits; *c != 0; ++c) {
        w += fl.advance[*c - '0'] * k;
    }
    if (!draw) {
        return w;
    }
    x = right - w;
    for (const char *c = digits; *c != 0; ++c) {
        char name[16];
        const KhdaysLayoutPiece *p;
        snprintf(name, sizeof(name), "gdigit%c", *c);
        p = khdays_layout_piece(&fl.layout, name);
        if (p != NULL) {
            khdays_layout_draw_piece(&fl.layout, fr, name, x, y, p->w * k, p->h * k, 1.0f);
        }
        x += fl.advance[*c - '0'] * k;
    }
    return w;
}

/* KHGummi's digits (the Files pages' pieces: gdigit, hdigit the red), their right at `right`, their ink `h`
 * high from `top`; 0 without them. */
static int gummi_number(const KhdaysLayoutFrame *fr, uint32_t value, float right, float top, float h, int red)
{
    char digits[16], name[16];
    const float k = fl.digit_ink > 0 ? h / fl.digit_ink : 0;
    float w = 0, x;
    snprintf(name, sizeof(name), "%cdigit0", red ? 'h' : 'g');
    if (!fl.ok || k <= 0 || khdays_layout_piece(&fl.layout, name) == NULL) {
        return 0;
    }
    snprintf(digits, sizeof(digits), "%u", value);
    for (const char *c = digits; *c != 0; ++c) {
        w += fl.advance[*c - '0'] * k;
    }
    x = right - w;
    for (const char *c = digits; *c != 0; ++c) {
        const KhdaysLayoutPiece *p;
        snprintf(name, sizeof(name), "%cdigit%c", red ? 'h' : 'g', *c);
        if ((p = khdays_layout_piece(&fl.layout, name)) != NULL) {
            khdays_layout_draw_piece(&fl.layout, fr, name, x, top, p->w * k, p->h * k, 1.0f);
        }
        x += fl.advance[*c - '0'] * k;
    }
    return 1;
}

/* KH2's NEW!, `h` high from (x, y), at `alpha`: pulsing while never viewed (a glow added over it), dark
 * once viewed on this visit. */
static void files_badge(const KhdaysLayoutFrame *fr, int badge, float x, float y, float h, double now, float alpha)
{
    const KhdaysLayoutPiece *p = khdays_layout_piece(&fl.layout, "new");
    float w;
    if (p == NULL || badge < 0 || alpha <= 0.002f) {
        return;
    }
    w = p->w * h / p->h;
    if (badge == 0) {
        const float pulse = 0.5f + 0.5f * (float)sin(now * 6.28318530718 / FILES_NEW_PERIOD);
        khdays_layout_draw_piece(&fl.layout, fr, "new", x, y, w, h, (0.85f + 0.15f * pulse) * alpha);
        SDL_SetTextureBlendMode(fl.layout.atlas, SDL_BLENDMODE_ADD);
        khdays_layout_draw_piece(&fl.layout, fr, "new", x, y, w, h, 0.55f * pulse * alpha);
        SDL_SetTextureBlendMode(fl.layout.atlas, SDL_BLENDMODE_BLEND);
    } else {
        const Uint8 dark[3] = {(Uint8)(255 * FILES_NEW_VIEWED), (Uint8)(255 * FILES_NEW_VIEWED),
                               (Uint8)(255 * FILES_NEW_VIEWED)};
        khdays_layout_draw_tinted(&fl.layout, fr, "new", x, y, w, h, dark, 0.9f * alpha);
    }
}

/* The menu's NEW, KH2's as the Files pages show it: on Files while one of its two pages has an entry never
 * viewed (camp.h's files_new), on those two themselves while Files' two are open -- a column of its own
 * after the longest of the options shown, at their bars' middle. */
#define STORY_FILES_ROW 2 /* Files: the story's options' third */
static void draw_menu_news(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, unsigned short *const *names, int rows,
                           float others)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, "option");
    const float *at = files_place("menu_new"), *body = place("bar_body");
    float longest = 0;
    if (!fl.ok || !v->story || v->files_new == 0 || st == NULL || at[3] <= 0 || (v->sub != 0 && v->sub != 2)) {
        return;
    }
    for (int i = 0; i < rows; ++i) {
        longest = SDL_max(longest, width_in("option", names[i]));
    }
    for (int i = 0; i < rows; ++i) {
        const int on = v->sub == 0 ? i == STORY_FILES_ROW : ((v->files_new >> i) & 1) != 0;
        const float alpha = v->question || v->waiting || v->locked[i] ? DIMMED : i != v->selected ? others : 1.0f;
        if (on) {
            files_badge(fr, 0, s.bars[0] + st->x + longest + at[0],
                        s.bars[1] + (float)i * s.bars[3] + body[1] + body[3] / 2 + at[1] - at[3] / 2, at[3],
                        seconds_now(), alpha);
        }
    }
}

/* How far a row is to the right: the chosen one select_shift, its neighbours none, between them while
 * the glove glides (the rows follow the glove). */
static float files_shift(int i)
{
    const float d = SDL_fabsf((float)i - fl.glove);
    return files_place("select_shift")[0] * (d < 1.0f ? 1.0f - d * d * (3.0f - 2.0f * d) : 0.0f);
}

/* One row of the list at y. */
static void files_row(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, int i, float y, double now)
{
    const KhdaysCampFiles *f = &v->file;
    const KhdaysCampFileRow *r = &f->row[i];
    const float *rows = fl.rows, *label = files_place("label"), *title = files_place("title");
    const float *digits = files_place("digits"), *badge = files_place("new");
    const float x = rows[0] + files_shift(i);
    const int language = v->language > 0 && v->language < LANGUAGES ? v->language : 1;
    const unsigned short *word = f->mode == 0 ? fl.day_word[language] : fl.number_word[language];
    const float ink = fl.layout.ink_bottom - fl.layout.ink_top;
    const float word_w = ink > 0 ? khdays_layout_line_width(&fl.layout, word, label[3] / ink) : 0;
    const float gap = label[3] * 0.35f, room = digits[0] - label[0];
    float digits_h = digits[3], digits_w = files_digits(fr, r->number, 0, 0, digits_h, 0);
    if (word_w + gap + digits_w > room && digits_w > 0) {
        /* three digits (a long word): the digits smaller, to a point, then the word */
        const float k = SDL_clamp((room - word_w - gap) / digits_w, 0.72f, 1.0f);
        digits_h *= k;
        digits_w *= k;
    }
    khdays_layout_draw_piece(&fl.layout, fr, i == f->cursor ? "button_selected" : "button", x, y, rows[2], rows[3],
                             1.0f);
    files_text(fr, word, x + label[0], y + label[1], label[3], SDL_max(room - digits_w - gap, label[3]));
    files_digits(fr, r->number, x + digits[0], y + digits[1] + (digits[3] - digits_h) / 2, digits_h, 1);
    {
        /* the NEW in the button, at its right end (place new: its right's distance from the row's right);
         * the title kept clear of it */
        const KhdaysLayoutPiece *p = khdays_layout_piece(&fl.layout, "new");
        const float bw = p != NULL && r->badge >= 0 ? p->w * badge[3] / p->h : 0;
        files_text(fr, r->title, x + title[0], y + title[1], title[3],
                   rows[2] - title[0] - (bw > 0 ? badge[0] + bw + label[0] : label[0]));
        files_badge(fr, r->badge, x + rows[2] - badge[0] - bw, y + badge[1], badge[3], now, 1.0f);
    }
}

/* The list's first row to show for the cursor (a row of room kept above and below it while there are
 * more), from the one shown before. */
static int files_target(const KhdaysCampFiles *f, int shown, float before)
{
    int top = (int)SDL_floorf(before + 0.5f);
    if (f->count <= shown) {
        return 0;
    }
    if (f->cursor < top + 1) {
        top = f->cursor - 1;
    }
    if (f->cursor > top + shown - 2) {
        top = f->cursor - shown + 2;
    }
    return SDL_clamp(top, 0, f->count - shown);
}

/* The list's motion to this picture: the rows glide to the first the cursor wants shown, the glove to
 * the cursor; at once when the page is new (or its mode). */
static void files_motion(const KhdaysCampFiles *f, double now)
{
    const int shown = (int)fl.rows[5];
    const double dt = now - fl.drawn_at;
    const int fresh = dt > FILES_FRESH || dt < 0 || f->mode != fl.mode;
    const int target = files_target(f, shown, fresh ? (float)f->top : fl.top); /* fresh: from the game's own */
    if (dt == 0 && !fresh) {
        return; /* (drawn twice this picture: going out and coming) */
    }
    if (fresh) {
        fl.top = (float)target;
        fl.glove = (float)f->cursor;
    } else {
        /* far: the last rows of the way only (the cursor wrapped round) */
        if (fl.top < (float)target - (float)shown) {
            fl.top = (float)(target - shown);
        } else if (fl.top > (float)(target + shown)) {
            fl.top = (float)(target + shown);
        }
        if (SDL_fabsf(fl.glove - (float)f->cursor) > (float)shown) {
            fl.glove = (float)f->cursor;
        }
        fl.top += ((float)target - fl.top) * (float)(1.0 - exp(-dt * FILES_SCROLL_RATE));
        fl.glove += ((float)f->cursor - fl.glove) * (float)(1.0 - exp(-dt * FILES_GLOVE_RATE));
    }
    fl.drawn_at = now;
    fl.mode = f->mode;
}

/* The page's own pieces: the DS's screen, the rows, the glove and the firefly, the arrows, the prompt. */
static void files_group(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now)
{
    const KhdaysCampFiles *f = &v->file;
    const float *rows = fl.rows, *arrows = files_place("arrows");
    const int shown = (int)rows[5], count = SDL_min(f->count, f->rows);
    files_motion(f, now);
    /* the Files' page beside the MENU pill: Roxas's Diary or Enemy Profiles (the story's sub-options 2, 3) */
    tab_button(fr, "menu_button", s.story_sub[2 + (f->mode == 1)], 1.0f, story_on(v));
    files_screen(renderer, fr, v, now);
    {
        /* the rows, cut at the list's room (its first's top to its last's bottom) while they glide */
        const SDL_FRect room = khdays_layout_to_window(fr, rows[0] - 40, rows[1] - 2, rows[2] + 80,
                                                       rows[4] * (float)(shown - 1) + rows[3] + 4);
        const SDL_Rect clip = {(int)SDL_floorf(room.x), (int)SDL_floorf(room.y), (int)SDL_ceilf(room.w),
                               (int)SDL_ceilf(room.h)};
        const int first = SDL_max(0, (int)SDL_floorf(fl.top) - 1),
                  last = SDL_min(count, (int)SDL_ceilf(fl.top) + shown + 1);
        SDL_Rect before;
        const int had_clip = SDL_RenderClipEnabled(renderer) && SDL_GetRenderClipRect(renderer, &before);
        SDL_SetRenderClipRect(renderer, &clip);
        for (int i = first; i < last; ++i) {
            files_row(fr, v, i, rows[1] + ((float)i - fl.top) * rows[4], now);
        }
        SDL_SetRenderClipRect(renderer, had_clip ? &before : NULL);
    }
    if (count > 0) {
        /* the glove at the chosen row (gliding to it), the firefly at its right end; both as far to the
         * right as the row */
        const float *glove = place("glove"), gx = files_place("glove_at")[0];
        const float shift = files_place("select_shift")[0];
        const float mid = rows[1] + (fl.glove - fl.top) * rows[4] + rows[3] / 2;
        const float lit = rows[1] + ((float)f->cursor - fl.top) * rows[4] + rows[3] / 2;
        if (mid > rows[1] - rows[3] && mid < rows[1] + rows[4] * (float)shown) {
            khdays_layout_draw_piece(&s.layout, fr, "glove", rows[0] + gx + shift + glove_motion(), mid + glove[1],
                                     glove[2], glove[3], 1.0f);
        }
        firefly(fr, rows[0] + rows[2] + files_shift(f->cursor), lit, 1.0f);
    }
    {
        /* the arrows, bobbing: more rows above, more below */
        const KhdaysLayoutPiece *p = khdays_layout_piece(&fl.layout, "arrow");
        const float bob = FILES_ARROW_BOB * (float)(0.5 - 0.5 * cos(now * 6.28318530718 / FILES_ARROW_PERIOD));
        const int top = (int)SDL_floorf(fl.top + 0.5f);
        if (p != NULL && arrows[2] > 0) {
            const float w = arrows[2], h = p->h * w / p->w;
            if (top > 0) {
                const SDL_FRect src = {p->u, p->v, p->w, p->h};
                const SDL_FRect dst = khdays_layout_to_window(fr, arrows[0] - w / 2, arrows[1] - h / 2 - bob, w, h);
                SDL_RenderTextureRotated(fl.layout.renderer, fl.layout.atlas, &src, &dst, 0.0, NULL, SDL_FLIP_VERTICAL);
            }
            if (top + shown < f->count) {
                khdays_layout_draw_piece(&fl.layout, fr, "arrow", arrows[0] - w / 2, arrows[3] - h / 2 + bob, w, h,
                                         1.0f);
            }
        }
    }
    draw_prompt(fr, v);
}

/* What the page and the menu both show where they show it: the numbers, the description (the same
 * words: the menu's cursor is on the page's option), the settings key. */
static void files_common(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    big_digits(fr, v->munny, place("munny"), s.munny_colour);
    hearts_word(fr);
    big_digits(fr, v->hearts, place("hearts"), s.hearts_colour);
    draw_description(fr, v);
    draw_settings_key(fr);
}

static void files_draw(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now)
{
    khdays_layout_begin_over(renderer, fr);
    /* the pattern, the camp's top and bottom bands over it (the list's rows stay in its room between) */
    files_pattern(renderer, fr, now);
    files_bands(renderer, fr);
    files_group(renderer, fr, v, now);
    files_common(fr, v);
}

/* ---- The save page ------------------------------------------------------------------------------ */

/* Save chosen on the camp's menu: the port's list (runtime/menus/savelist.h) in the camp's look, the files in
 * KH2's save list's pieces made grey (layouts/campsave/campsave.txt): a row a file, the chosen one red, the world
 * played last before the save (runtime/menus/saveworld.h) with its logo's words, the face, the day, the level,
 * the munny, the time; the camp's bands, glove and firefly. The menu slides into it as into the Files pages.
 * Without its pieces the port's own save list (save_screen.c) shows instead. */
#define SAVE_SCROLL_RATE 20.0
#define SAVE_GLOVE_RATE 26.0
#define SAVE_LIT_RATE 20.0
#define SAVE_FRESH 0.25 /* not drawn longer than this: the rows in their places at once */
#define SAVE_EMPTY_WORDS 48

static struct {
    int tried, ok;
    KhdaysLayout layout;
    float rows[6]; /* the first row's panel's left and top, a row's size, the pitch, how many show */
    Uint8 munny_word[3], munny_digits[3];
    float face_alpha;
    unsigned short empty[LANGUAGES][SAVE_EMPTY_WORDS], load_empty[LANGUAGES][SAVE_EMPTY_WORDS];
    unsigned short guest[LANGUAGES][SAVE_EMPTY_WORDS]; /* Mission Mode's load list's guest row */
    /* the list's motion: the first row shown (a fraction while it scrolls), the glove's row, each
     * file's red */
    double drawn_at;
    float top, glove, lit[KHDAYS_SLOT_COUNT + 1];
    int target;
    volatile int drawn; /* the page drawn at the last picture (save_screen.c leaves the list to it) */
} sp;

static void save_line(char *line, const char *folder, void *ctx)
{
    float a, b, c, d, e, f;
    int r, g, bl;
    char name[40] = "", lang[8] = "";
    (void)ctx;
    if (khdays_layout_line(&sp.layout, line, folder)) {
        return;
    }
    if (SDL_sscanf(line, "rows %f %f %f %f %f %f", &a, &b, &c, &d, &e, &f) == 6) {
        sp.rows[0] = a, sp.rows[1] = b, sp.rows[2] = c, sp.rows[3] = d, sp.rows[4] = e, sp.rows[5] = f;
    } else if (SDL_sscanf(line, "colour %39s %d %d %d", name, &r, &g, &bl) == 4) {
        Uint8 *out = strcmp(name, "munny_word") == 0     ? sp.munny_word
                     : strcmp(name, "munny_digits") == 0 ? sp.munny_digits
                                                         : NULL;
        if (out != NULL) {
            out[0] = (Uint8)r, out[1] = (Uint8)g, out[2] = (Uint8)bl;
        }
    } else if (SDL_sscanf(line, "face_alpha %f", &a) == 1) {
        sp.face_alpha = a;
    } else if (SDL_sscanf(line, "words %39s %7s", name, lang) == 2 &&
               (strcmp(name, "empty") == 0 || strcmp(name, "load_empty") == 0 || strcmp(name, "guest") == 0)) {
        const char *words = line + 6 + strlen(name) + 1 + strlen(lang);
        unsigned short (*slot)[SAVE_EMPTY_WORDS] = name[0] == 'e'   ? sp.empty
                                                   : name[0] == 'l' ? sp.load_empty
                                                                    : sp.guest;
        while (*words == ' ') {
            ++words;
        }
        for (int i = 1; i < LANGUAGES; ++i) {
            if (strcmp(lang, CODES[i]) == 0) {
                char clean[160];
                snprintf(clean, sizeof(clean), "%s", words);
                clean[strcspn(clean, "\r\n")] = '\0';
                khdays_layout_utf8(clean, slot[i], SAVE_EMPTY_WORDS);
            }
        }
    }
}

static void forget_save(void)
{
    khdays_layout_forget(&sp.layout);
    memset(sp.rows, 0, sizeof(sp.rows));
    sp.ok = 0;
    sp.tried = 0;
}

static void save_load(SDL_Renderer *renderer)
{
    char path[1024], folder[1024], *slash;
    static const char *const needed[] = {"band",       "frame",        "row",    "row_selected", "ring",    "munny",
                                         "scroll_bar", "scroll_thumb", "world0", "band_load",    "menu_bar"};
    forget_save();
    sp.tried = 1;
    sp.layout.who = "save page";
    sp.layout.renderer = renderer;
    sp.face_alpha = 0.6f;
    if (!khdays_mods_find("layouts/campsave/campsave.txt", path, (int)sizeof(path))) {
        SDL_Log("save page: no layouts/campsave/campsave.txt (the port's own save list shown)");
        return;
    }
    snprintf(folder, sizeof(folder), "%s", path);
    slash = strrchr(folder, '/');
    if (strrchr(folder, '\\') > slash) {
        slash = strrchr(folder, '\\');
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
    each_line(path, save_line, folder, NULL);
    sp.ok = khdays_layout_ready(&sp.layout) && sp.rows[2] > 0 && sp.rows[5] >= 1;
    for (size_t i = 0; i < sizeof(needed) / sizeof(needed[0]); ++i) {
        sp.ok = sp.ok && khdays_layout_piece(&sp.layout, needed[i]) != NULL;
    }
    if (!sp.ok) {
        SDL_Log("save page: %s is incomplete (the port's own save list shown)", path);
        khdays_layout_forget(&sp.layout);
    }
}

/* Whether the save page can be drawn (its pieces, and the Files pages' pattern behind it). */
static int save_ready(void)
{
    return sp.ok && fl.ok;
}

int khdays_camp_screen_draws_save(void)
{
    return sp.drawn;
}

static const float *save_place(const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&sp.layout, name);
    return p != NULL ? &p->x : none;
}

/* A line in the page's font (the Files pages'), its capitals' top at y, `cap` high, grey `grey`; its width.
 * centre: x its middle. */
static float save_text(const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y, float cap, float grey,
                       float alpha, int centre)
{
    const float ink = sp.layout.ink_bottom - sp.layout.ink_top;
    const float size = ink > 0 ? cap / ink : 1.0f;
    float w;
    if (t == NULL || t[0] == 0) {
        return 0;
    }
    w = khdays_layout_line_width(&sp.layout, t, size);
    if (centre) {
        x -= w / 2;
    }
    khdays_layout_draw_line(&sp.layout, fr, t, x, y, size, grey, alpha, 1);
    khdays_layout_draw_line(&sp.layout, fr, t, x, y, size, grey, alpha, 0);
    return w;
}

/* A line in the camp's own font (the menu's description's), its capitals' top at y, `cap` high, in
 * `rgb` over its shadow; its width. right: x its right. */
static float camp_text(const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y, float cap,
                       const Uint8 rgb[3], int right)
{
    const float ink = s.layout.ink_bottom - s.layout.ink_top;
    const float size = ink > 0 ? cap / ink : 1.0f;
    float w;
    if (t == NULL || t[0] == 0) {
        return 0;
    }
    w = khdays_layout_line_width(&s.layout, t, size);
    if (right) {
        x -= w;
    }
    if (!s.layout.font_colours) {
        khdays_layout_draw_line_rgb(&s.layout, fr, t, x, y, size, rgb, 1.0f, 1);
    }
    khdays_layout_draw_line_rgb(&s.layout, fr, t, x, y, size, rgb, 1.0f, 0);
    return w;
}

/* A record of the game's strings with its first %d made `n`. */
static void with_number(const unsigned short *in, int n, unsigned short *out, int room)
{
    int o = 0, done = 0;
    out[0] = 0;
    if (in == NULL) {
        return;
    }
    for (; *in != 0 && o < room - 12; ++in) {
        if (!done && in[0] == '%' && in[1] == 'd') {
            char digits[12];
            snprintf(digits, sizeof(digits), "%d", n);
            for (const char *d = digits; *d != '\0'; ++d) {
                out[o++] = (unsigned short)*d;
            }
            ++in;
            done = 1;
        } else {
            out[o++] = *in;
        }
    }
    out[o] = 0;
}

/* The face of a file: its piece's middle, as wide as the place, the panel's height (KH2's eyes' strip). */
static void save_face(const KhdaysLayoutFrame *fr, int face, float x, float y, float alpha)
{
    char name[16];
    const KhdaysLayoutPiece *p;
    const float *at = save_place("face");
    snprintf(name, sizeof(name), "face%d", face);
    if ((p = khdays_layout_piece(&sp.layout, name)) == NULL || at[2] <= 0 || at[3] <= 0) {
        return;
    }
    {
        /* drawn as wide as the place, its height cut to the place's */
        const float scale = at[2] / p->w, shown = at[3] / scale < p->h ? at[3] / scale : p->h;
        const SDL_FRect src = {p->u, p->v + (p->h - shown) / 2, p->w, shown};
        const SDL_FRect dst =
            khdays_layout_to_window(fr, x + at[0], y + at[1] + (at[3] - shown * scale) / 2, at[2], shown * scale);
        SDL_SetTextureAlphaModFloat(sp.layout.atlas, alpha);
        SDL_RenderTexture(sp.layout.renderer, sp.layout.atlas, &src, &dst);
        SDL_SetTextureAlphaModFloat(sp.layout.atlas, 1.0f);
    }
}

/* One file's row, its panel's top left at (x, y). */
static void save_row(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, const KhdaysSaveView *sl, int i, float x,
                     float y)
{
    const KhdaysSaveRow *r = &sl->rows[i < KHDAYS_SLOT_COUNT ? i : 0];
    const float *piece = save_place("row_piece"), *ring = save_place("ring"), *num = save_place("number");
    const float lit = sp.lit[i];
    const int language = v->language > 0 && v->language < LANGUAGES ? v->language : 1;
    char digits[32];
    unsigned short line[128];
    if (lit < 1.0f) {
        khdays_layout_draw_piece(&sp.layout, fr, "row", x + piece[0], y + piece[1], piece[2], piece[3], 1.0f);
    }
    if (lit > 0.0f) {
        khdays_layout_draw_piece(&sp.layout, fr, "row_selected", x + piece[0], y + piece[1], piece[2], piece[3], lit);
        khdays_layout_draw_piece(&sp.layout, fr, "ring", x + ring[0], y + ring[1], ring[2], ring[3], lit);
    }
    if (i >= KHDAYS_SLOT_COUNT) {
        /* Mission Mode's load list's guest row: its words alone */
        const float *empty = save_place("empty");
        save_text(fr, sp.guest[language], x + empty[0], y + empty[1] - empty[3] / 2, empty[3], 255.0f, 1.0f, 1);
        return;
    }
    /* its number in the ring's place; the autosave's file its tag over it */
    snprintf(digits, sizeof(digits), "%02d", i + 1);
    khdays_layout_utf8(digits, line, 128);
    save_text(fr, line, x + num[0], y + num[1] - num[3] / 2, num[3], 255.0f, 1.0f, 1);
    if (i == KHDAYS_SLOT_COUNT - 1) {
        const char *tag = khdays_text_in(TXT_SAVE_AUTOSAVE_TAG, v->language);
        khdays_layout_utf8(tag != NULL ? tag : "", line, 128);
        save_text(fr, line, x + num[0], y + num[1] - num[3] * 1.55f, num[3] * 0.55f, 230.0f, 1.0f, 1);
    }
    if (r->state == KHDAYS_SLOT_SAVED) {
        const float *world = save_place("world"), *day = save_place("day"), *level = save_place("level");
        const float *difficulty = save_place("difficulty"), *munny = save_place("munny"), *time = save_place("time");
        const float *crown = save_place("crown");
        const float *title = save_place("title");
        const KhdaysLayoutPiece *word = khdays_layout_piece(&sp.layout, "munny"), *words;
        const int which = khdays_saveworld_of(r->counter);
        char name[16];
        save_face(fr, r->face, x, y, sp.face_alpha);
        snprintf(name, sizeof(name), "world%d", which);
        khdays_layout_draw_piece(&sp.layout, fr, name, x + world[0], y + world[1], world[2], world[3], 1.0f);
        /* the world's title (the words of the logo the game shows on first reaching it; the Castle has
         * none) over the world's lower part: as large as its place lets it be, its middle the place's,
         * its bottom the place's */
        snprintf(name, sizeof(name), "title%d", which);
        if (title[2] > 0 && title[3] > 0 && (words = khdays_layout_piece(&sp.layout, name)) != NULL && words->w > 0 &&
            words->h > 0) {
            const float k = SDL_min(title[2] / words->w, title[3] / words->h), tw = words->w * k, th = words->h * k;
            khdays_layout_draw_piece(&sp.layout, fr, name, x + title[0] + (title[2] - tw) / 2,
                                     y + title[1] + title[3] - th, tw, th, 1.0f);
        }
        if (r->crown && crown[2] > 0) {
            khdays_layout_draw_piece(&sp.layout, fr, "crown", x + crown[0], y + crown[1], crown[2], crown[3], 1.0f);
        }
        with_number(khdays_layout_string(&s.info, INFO_DAY), r->day, line, 128);
        camp_text(fr, line, x + day[0], y + day[1], day[3], WHITE, 0);
        with_number(khdays_layout_string(&s.sav, SAV_LEVEL), r->level, line, 128);
        {
            /* the difficulty after the level, `difficulty[0]` apart */
            const float lw = camp_text(fr, line, x + level[0], y + level[1], level[3], WHITE, 0);
            if (r->difficulty < 3 && s.difficulty[r->difficulty] != NULL) {
                static const Uint8 grey[3] = {205, 205, 205};
                camp_text(fr, s.difficulty[r->difficulty], x + level[0] + lw + difficulty[0], y + difficulty[1],
                          difficulty[3], grey, 0);
            }
        }
        if (word != NULL && word->h > 0) {
            /* KH2's MUNNY (white, a little smaller, its middle the digits') and the amount after it in the
             * money's yellow (the camp's font), on the time's line */
            static const Uint8 black[3] = {0, 0, 0};
            const float wh = munny[3] * 0.8f, ww = word->w * wh / word->h, wy = munny[1] + (munny[3] - wh) / 2;
            const float shadow = munny[3] * 0.07f;
            khdays_layout_draw_tinted(&sp.layout, fr, "munny", x + munny[0] + shadow, y + wy + shadow, ww, wh, black,
                                      0.8f);
            khdays_layout_draw_tinted(&sp.layout, fr, "munny", x + munny[0], y + wy, ww, wh, sp.munny_word, 1.0f);
            snprintf(digits, sizeof(digits), "%u", (unsigned)r->munny);
            khdays_layout_utf8(digits, line, 128);
            camp_text(fr, line, x + munny[0] + ww + munny[3] * 0.35f, y + munny[1], munny[3], sp.munny_digits, 0);
        }
        {
            /* the hours and the minutes, as KH2's list */
            snprintf(digits, sizeof(digits), "%u:%02u", (unsigned)(r->seconds / 3600),
                     (unsigned)(r->seconds / 60 % 60));
            khdays_layout_utf8(digits, line, 128);
            camp_text(fr, line, x + time[0], y + time[1], time[3], WHITE, 1);
        }
    } else {
        const float *empty = save_place("empty");
        const unsigned short *words = r->state == KHDAYS_SLOT_BROKEN       ? khdays_layout_string(&s.sav, SAV_CORRUPT)
                                      : sl->screen == KHDAYS_SAVELIST_LOAD ? sp.load_empty[language]
                                                                           : sp.empty[language];
        save_text(fr, words, x + empty[0], y + empty[1] - empty[3] / 2, empty[3], 255.0f, 1.0f, 1);
    }
}

/* The list's motion to this picture: the cursor's file kept in sight, the rows scrolling to it, the
 * glove gliding, the red coming and going; all at once when the page is new. */
static void save_motion(const KhdaysSaveView *sl, double now)
{
    const int shown = (int)sp.rows[5], count = KHDAYS_SLOT_COUNT + (sl->guest != 0);
    const double dt = now - sp.drawn_at;
    const int fresh = dt > SAVE_FRESH || dt < 0;
    const int cursor = SDL_clamp(sl->cursor, 0, count - 1);
    int top = sp.target;
    if (dt == 0 && !fresh) {
        return; /* (drawn twice this picture) */
    }
    if (cursor < top) {
        top = cursor;
    }
    if (cursor >= top + shown) {
        top = cursor - shown + 1;
    }
    top = SDL_clamp(top, 0, count - shown);
    sp.target = top;
    if (fresh || SDL_fabsf((float)top - sp.top) > (float)shown) {
        sp.top = (float)top; /* (a wrap: at once) */
    } else {
        sp.top += ((float)top - sp.top) * (float)(1.0 - exp(-dt * SAVE_SCROLL_RATE));
    }
    if (fresh || SDL_fabsf((float)cursor - sp.glove) > (float)shown) {
        sp.glove = (float)cursor;
    } else {
        sp.glove += ((float)cursor - sp.glove) * (float)(1.0 - exp(-dt * SAVE_GLOVE_RATE));
    }
    for (int i = 0; i < count; ++i) {
        const float to = i == cursor ? 1.0f : 0.0f;
        sp.lit[i] = fresh ? to : sp.lit[i] + (to - sp.lit[i]) * (float)(1.0 - exp(-dt * SAVE_LIT_RATE));
    }
    sp.drawn_at = now;
}

/* The load list's keys in the prompt's plate, as the port's own list shows them: the game's (A load, B
 * back), the delete key's on a file, Y the guest's game on Mission Mode's list. */
static void load_prompt(const KhdaysLayoutFrame *fr, const KhdaysSaveView *sl)
{
    unsigned short keys[256];
    const unsigned short *a = khdays_layout_string(&s.lrd, LRD_KEYS), *b = khdays_layout_string(&s.lrd, LRD_DELETE_KEY);
    const int language = sl->language > 0 && sl->language < LANGUAGES ? sl->language : 1;
    int o = 0;
    if (!sl->help || sl->ask[0] >= 0 || a == NULL) {
        return;
    }
    for (; *a != 0 && o < 200; ++a) {
        keys[o++] = *a;
    }
    if (sl->cursor < KHDAYS_SLOT_COUNT && b != NULL) {
        keys[o++] = ' ';
        keys[o++] = ' ';
        keys[o++] = ' ';
        for (; *b != 0 && o < 250; ++b) {
            keys[o++] = *b;
        }
    }
    if (sl->guest && sp.guest[language][0] != 0 && o < 200) {
        const unsigned short *g = sp.guest[language];
        keys[o++] = ' ';
        keys[o++] = ' ';
        keys[o++] = ' ';
        keys[o++] = KHDAYS_LAYOUT_GLYPH_Y;
        keys[o++] = ' ';
        for (; *g != 0 && o < 254; ++g) {
            keys[o++] = *g;
        }
    }
    keys[o] = 0;
    prompt_plate(fr, keys);
}

/* The menu's option (or sub-option) the page is of, as its bar shows it lit -- without its glow, a little
 * smaller (menu_bar: save_assets.py) -- at the place `name` of the page's layout (beside the SAVE pill, or
 * the camp's MENU pill on its other pages), at `alpha`, with a firefly of its own at its right end. */
#define MENU_BAR_TEXT_X                                                                                                \
    38.67f                     /* the name's left in menu_bar (its texels): the menu's (58.67 from the lit bar's
                                * left, whose glow menu_bar has not: 20) */
#define MENU_BAR_MIDDLE 0.487f /* the bar's body's middle, of its height */
static void story_tab_button(const KhdaysLayoutFrame *fr, const float *at, const unsigned short *label, float alpha);

/* (`kh2`: the story's menu's look, its KH2 button in that place: story_tab_button.) */
static void tab_button(const KhdaysLayoutFrame *fr, const char *name, const unsigned short *label, float alpha, int kh2)
{
    const float *at = save_place(name);
    const KhdaysLayoutPiece *bar = sp.ok ? khdays_layout_piece(&sp.layout, "menu_bar") : NULL;
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, "option");
    if (bar == NULL || at[2] <= 0 || label == NULL || label[0] == 0 || st == NULL || alpha <= 0.002f) {
        return;
    }
    if (kh2) {
        story_tab_button(fr, at, label, alpha);
        return;
    }
    {
        const float k = at[2] / bar->w, size = st->size * k;
        const float cap = (s.layout.ink_bottom - s.layout.ink_top) * size;
        const float middle = at[1] + at[3] * MENU_BAR_MIDDLE;
        const float *glove = place("glove");
        const float unit = KHDAYS_FIREFLY_UNIT * (glove[3] > 0 ? glove[3] / 91.0f : 1.0f) * 0.8f;
        khdays_layout_draw_piece(&sp.layout, fr, "menu_bar", at[0], at[1], at[2], at[3], alpha);
        khdays_layout_draw_line(&s.layout, fr, label, at[0] + MENU_BAR_TEXT_X * k, middle - cap / 2, size, 255.0f,
                                alpha, 1);
        khdays_layout_draw_line(&s.layout, fr, label, at[0] + MENU_BAR_TEXT_X * k, middle - cap / 2, size, 255.0f,
                                alpha, 0);
        khdays_layout_firefly(s.layout.renderer, fr, KHDAYS_FIREFLY_TAB, at[0] + at[2] - 15.0f * unit, middle, unit,
                              alpha);
    }
}

/* The menu's option `option` (KHDAYS_CAMP_OPTIONS of the menu the view is of) in tab_button. */
static void option_button(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, const char *name, int option,
                          float alpha)
{
    unsigned short *const *names = v->story ? s.story_option : s.option;
    if (option >= 0 && option < KHDAYS_CAMP_OPTIONS) {
        tab_button(fr, name, names[option], alpha, story_on(v));
    }
}

/* The page's own pieces: the option beside the pill, the frame, the rows, the scroll bar, the glove and
 * the firefly. */
static void save_group(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, const KhdaysCampView *v,
                       const KhdaysSaveView *sl, double now)
{
    const float *rows = sp.rows, *frame = save_place("frame"), *scroll = save_place("scroll");
    const int shown = (int)rows[5], count = KHDAYS_SLOT_COUNT + (sl->guest != 0);
    save_motion(sl, now);
    option_button(fr, v, "button", v->selected, 1.0f);
    khdays_layout_draw_piece(&sp.layout, fr, "frame", frame[0], frame[1], frame[2], frame[3], 1.0f);
    {
        /* the rows, cut at the list's room while they scroll */
        const float *piece = save_place("row_piece");
        /* from just under the row above the first (its panel's bottom: the pitch less the panel) to the top
         * of the one under the last: those two never show */
        const float gap = rows[4] - rows[3];
        const SDL_FRect room = khdays_layout_to_window(fr, rows[0] + piece[0] - 40, rows[1] - gap + 1, piece[2] + 80,
                                                       rows[4] * (float)(shown - 1) + rows[3] + 2 * gap - 2);
        const SDL_Rect clip = {(int)SDL_floorf(room.x), (int)SDL_floorf(room.y), (int)SDL_ceilf(room.w),
                               (int)SDL_ceilf(room.h)};
        const int first = SDL_max(0, (int)SDL_floorf(sp.top)),
                  last = SDL_min(count, (int)SDL_ceilf(sp.top) + shown + 1);
        SDL_Rect before;
        const int had_clip = SDL_RenderClipEnabled(renderer) && SDL_GetRenderClipRect(renderer, &before);
        SDL_SetRenderClipRect(renderer, &clip);
        for (int i = first; i < last; ++i) {
            save_row(fr, v, sl, i, rows[0], rows[1] + ((float)i - sp.top) * rows[4]);
        }
        SDL_SetRenderClipRect(renderer, had_clip ? &before : NULL);
    }
    {
        /* KH2's scroll bar (drawn whole: save_assets.py), the thumb where the rows are, from the groove's
         * top (the first file) to its bottom (the last) */
        const float *groove = save_place("scroll_groove");
        const float w = scroll[2], x = scroll[0] - w / 2;
        if (w > 0) {
            const float run = groove[1] - groove[0];
            const float at = scroll[1] + groove[0] + run * SDL_clamp(sp.top / (float)(count - shown), 0.0f, 1.0f);
            khdays_layout_draw_piece(&sp.layout, fr, "scroll_bar", x, scroll[1], w, scroll[3], 1.0f);
            khdays_layout_draw_piece(&sp.layout, fr, "scroll_thumb", x, at, w, groove[2], 1.0f);
        }
    }
    if (!sl->buttons) {
        /* the glove at the chosen file (gliding to it), the firefly at its right end */
        const float *glove = place("glove"), *at = save_place("glove_at");
        const float mid = rows[1] + (sp.glove - sp.top) * rows[4] + rows[3] / 2;
        const float lit = rows[1] + ((float)SDL_clamp(sl->cursor, 0, count - 1) - sp.top) * rows[4] + rows[3] / 2;
        if (mid > rows[1] - rows[3] && mid < rows[1] + rows[4] * (float)shown) {
            khdays_layout_draw_piece(&s.layout, fr, "glove", rows[0] + at[0] + glove_motion(), mid + glove[1], glove[2],
                                     glove[3], 1.0f);
        }
        if (lit > rows[1] - 1 && lit < rows[1] + rows[4] * (float)shown) {
            firefly(fr, rows[0] + rows[2], lit, 1.0f);
        }
    }
    if (sl->screen == KHDAYS_SAVELIST_LOAD) {
        load_prompt(fr, sl);
    } else {
        draw_prompt(fr, v);
    }
}

/* What the page and the menu both show where they show it: the numbers, the settings key; the menu's
 * description of Save, or the game's question or message in its place with the answers (sl NULL: the
 * description). */
static void save_common(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, const KhdaysSaveView *sl)
{
    const int loading = sl != NULL && sl->screen == KHDAYS_SAVELIST_LOAD;
    const KhdaysLayoutStrings *table = sl != NULL && sl->words == KHDAYS_WORDS_LOAD ? &s.lrd : &s.sav;
    if (loading) {
        /* the title's load list: the chosen file's munny (no game is on: no heart points) */
        if (sl->cursor >= 0 && sl->cursor < KHDAYS_SLOT_COUNT && sl->rows[sl->cursor].state == KHDAYS_SLOT_SAVED) {
            big_digits(fr, sl->rows[sl->cursor].munny, place("munny"), s.munny_colour);
        }
    } else {
        big_digits(fr, v->munny, place("munny"), s.munny_colour);
        hearts_word(fr);
        big_digits(fr, v->hearts, place("hearts"), s.hearts_colour);
    }
    draw_settings_key(fr);
    if (sl != NULL && (sl->screen == KHDAYS_SAVELIST_SAVE || loading) && sl->ask[0] >= 0) {
        unsigned short line[256], one[256];
        /* with the buttons, the question higher (the answers under it: the page's answer_yes, answer_no) */
        float dy = sl->buttons ? save_place("ask")[1] : 0.0f;
        for (int i = 0; i < 2; ++i) {
            const unsigned short *t;
            if (sl->ask[i] < 0) {
                continue;
            }
            with_number(khdays_layout_string(table, sl->ask[i]), sl->ask_file, line, 256);
            for (t = line; *t != 0;) {
                int n = 0;
                while (t[n] != 0 && t[n] != '\n' && n < 255) {
                    ++n;
                }
                memcpy(one, t, (size_t)n * sizeof(one[0]));
                one[n] = 0;
                text(fr, "description", one, 0, dy);
                dy += s.description_pitch;
                t += n;
                if (*t == '\n') {
                    ++t;
                }
            }
        }
        if (sl->buttons) {
            const int own = save_place("answer_yes")[2] > 0 && save_place("answer_no")[2] > 0;
            draw_answers_in(fr, sl->yes, khdays_layout_string(table, table == &s.lrd ? LRD_YES : SAV_YES),
                            khdays_layout_string(table, table == &s.lrd ? LRD_NO : SAV_NO),
                            own ? save_place("answer_yes") : place("button_yes"),
                            own ? save_place("answer_no") : place("button_no"));
        }
    } else if (loading) {
        if (sl->help) {
            draw_lines(fr, &s.lrd, LRD_TITLE, "description", s.description_pitch); /* the list's help */
        }
    } else {
        draw_description(fr, v);
    }
}

/* The page's top band (the SAVE pill) at `page`, over the camp's. */
static void save_band(const KhdaysLayoutFrame *fr, float page, const char *name)
{
    const float *band = save_place("band");
    if (page > 0.002f && band[2] > 0) {
        khdays_layout_draw_piece(&sp.layout, fr, name, band[0], band[1], band[2], band[3], page < 1.0f ? page : 1.0f);
    }
}

static void save_draw(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, const KhdaysSaveView *sl,
                      double now)
{
    khdays_layout_begin_over(renderer, fr);
    files_pattern(renderer, fr, now);
    files_bands(renderer, fr);
    save_band(fr, 1.0f, "band");
    save_group(renderer, fr, v, sl, now);
    save_common(fr, v, sl);
}

/* The title's load list (the story's and Mission Mode's) in the save page's look, the LOAD pill in its
 * band: no camp under it, the camp's pieces read for the list's language. 1 drawn. */
static int load_page_draw(SDL_Renderer *renderer, const KhdaysSaveView *sl, double now)
{
    KhdaysCampView v;
    KhdaysLayoutFrame fr;
    if (renderer != s.layout.renderer || sl->language != s.tried) {
        s.tried = sl->language;
        s.ok = load(renderer, sl->language);
    }
    if (!s.ok || !save_ready()) {
        return 0;
    }
    memset(&v, 0, sizeof(v));
    v.language = sl->language;
    v.story = 1;
    v.selected = -1; /* (no menu option beside the pill) */
    v.save = 1;
    khdays_layout_begin_over(renderer, &fr);
    files_pattern(renderer, &fr, now);
    files_bands(renderer, &fr);
    save_band(&fr, 1.0f, "band_load");
    save_group(renderer, &fr, &v, sl, now);
    save_common(&fr, &v, sl);
    khdays_layout_shade(renderer, sl->master); /* the title's fades */
    khdays_layout_end(renderer, &fr);
    sp.drawn = 1;
    return 1;
}

/* ---- Enemy Profiles' model viewer --------------------------------------------------------------- */

/* X on a met enemy: the game draws its model (runtime/menus/enemy_viewer.c) and the one-screen picture
 * shows it, larger, in the middle of the window (the 3D's polygons, as the camp's model; see
 * khdays_camp_screen_model). Under the picture is a backdrop, over it this screen: so the page as it
 * was, darkened, is taken once (drawn into a texture, read back) and becomes the picture's backdrop, and
 * the screen draws only the enemy's name and the controls over it. */
#define VIEWER_DARK 0.72f
static const float VIEWER_WINDOW[4] = {420.0f, 140.0f, 1080.0f, 810.0f}; /* the DS's 4:3, the layout's units */

static struct {
    SDL_SpinLock lock;
    uint8_t *pixels[2]; /* two, taken in turn: the picture's card keeps a backdrop by its address */
    int w[2], h[2];
    int which;       /* the one in use; -1 none */
    unsigned serial; /* the viewer's opening it was taken for */
    int taken;
} vs = {0, {NULL, NULL}, {0, 0}, {0, 0}, -1, 0, 0};

static int viewer_model(KhdaysCampModel *out)
{
    KhdaysEnemyViewer ev;
    int ok = 0;
    khdays_enemy_viewer_read(&ev);
    if (!ev.open || !ev.ready) {
        return 0;
    }
    SDL_LockSpinlock(&vs.lock);
    if (vs.taken && vs.serial == ev.opening && vs.which >= 0) {
        static const float whole[4] = {0, 0, 256, 192}; /* the 3D's viewport: the viewer frames it */
        memcpy(out->source, whole, sizeof(out->source));
        memcpy(out->view, whole, sizeof(out->view));
        memcpy(out->window, VIEWER_WINDOW, sizeof(out->window));
        out->backdrop = vs.pixels[vs.which];
        out->backdrop_w = vs.w[vs.which];
        out->backdrop_h = vs.h[vs.which];
        out->model = 1;
        ok = 1;
    }
    SDL_UnlockSpinlock(&vs.lock);
    return ok;
}

/* The page as it is, darkened, read back at the layout's size (1920 x 1080 at most) for the picture. */
static void viewer_take(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now,
                        unsigned serial)
{
    SDL_Surface *shot = NULL, *rgba = NULL;
    SDL_Rect area;
    if (!group_begin(renderer)) {
        return;
    }
    files_pattern(renderer, fr, now);
    files_bands(renderer, fr);
    files_group(renderer, fr, v, now);
    files_common(fr, v);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColorFloat(renderer, 0, 0, 0, VIEWER_DARK);
    SDL_RenderFillRect(renderer, NULL);
    area.x = (int)SDL_floorf(fr->ox);
    area.y = (int)SDL_floorf(fr->oy);
    area.w = (int)SDL_ceilf(KHDAYS_LAYOUT_W * fr->scale);
    area.h = (int)SDL_ceilf(KHDAYS_LAYOUT_H * fr->scale);
    shot = SDL_RenderReadPixels(renderer, &area);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (shot == NULL) {
        SDL_Log("enemy viewer: the page not read back: %s", SDL_GetError());
        return;
    }
    if (shot->w > (int)KHDAYS_LAYOUT_W) {
        SDL_Surface *small = SDL_ScaleSurface(shot, (int)KHDAYS_LAYOUT_W, (int)KHDAYS_LAYOUT_H, SDL_SCALEMODE_LINEAR);
        if (small != NULL) {
            SDL_DestroySurface(shot);
            shot = small;
        }
    }
    rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(shot);
    if (rgba == NULL) {
        return;
    }
    {
        const int next = vs.which == 0 ? 1 : 0, w = rgba->w, h = rgba->h;
        uint8_t *pixels = (uint8_t *)SDL_malloc((size_t)w * (size_t)h * 4u);
        if (pixels != NULL) {
            for (int y = 0; y < h; ++y) {
                memcpy(pixels + (size_t)y * (size_t)w * 4u,
                       (const uint8_t *)rgba->pixels + (size_t)y * (size_t)rgba->pitch, (size_t)w * 4u);
            }
            for (size_t i = 3; i < (size_t)w * (size_t)h * 4u; i += 4) {
                pixels[i] = 255; /* (the page is whole: no holes to the picture under it) */
            }
            SDL_LockSpinlock(&vs.lock);
            SDL_free(vs.pixels[next]); /* (the one before the last: long gone from the card) */
            vs.pixels[next] = pixels;
            vs.w[next] = w;
            vs.h[next] = h;
            vs.which = next;
            vs.serial = serial;
            vs.taken = 1;
            SDL_UnlockSpinlock(&vs.lock);
        }
    }
    SDL_DestroySurface(rgba);
}

/* Over the model: the enemy's name above it, the controls in a plate below. */
static void viewer_draw(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, const KhdaysEnemyViewer *ev)
{
    const KhdaysCampFiles *f = &v->file;
    const KhdaysLayoutText *st = khdays_layout_text(&s.layout, "prompt");
    unsigned short line[200];
    int n = 0;
    if (ev->row >= 0 && ev->row < f->rows && ev->row < f->count) {
        const unsigned short *name = f->row[ev->row].title;
        const float cap = 34.0f;
        const float ink = fl.layout.ink_bottom - fl.layout.ink_top;
        const float w = ink > 0 ? khdays_layout_line_width(&fl.layout, name, cap / ink) : 0;
        files_text(fr, name, KHDAYS_LAYOUT_W / 2 - w / 2, VIEWER_WINDOW[1] - 28.0f, cap, 0);
    }
    if (st == NULL) {
        return;
    }
#define VIEWER_ADD(c) (line[n++] = (unsigned short)(c))
#define VIEWER_WORDS(id)                                                                                               \
    do {                                                                                                               \
        const char *w_ = khdays_text_in((id), v->language);                                                            \
        VIEWER_ADD(' ');                                                                                               \
        n += utf16_append(line + n, (int)(sizeof(line) / sizeof(line[0])) - n - 12, w_ != NULL ? w_ : "");             \
        VIEWER_ADD(' ');                                                                                               \
        VIEWER_ADD(' ');                                                                                               \
        VIEWER_ADD(' ');                                                                                               \
    } while (0)
    /* the DS's glyphs (shown as the player's pad or keys): turning (the left stick too), zooming (the right
     * stick too), the enemy before (L) and after (R: the shoulder's, R1 or RB -- the DS's R when it is there,
     * the port's lock-on when the player has it apart, the DS's R then on R3), the view reset, back */
    {
        const KhdaysProfile *profile = khdays_devices_profile();
        const int next = profile != NULL && profile->enh.separate_lock_on ? KHDAYS_ACT_LOCK_ON : KHDAYS_ACT_R;
        const float cap = capitals("prompt"), pad = cap * 1.1f, h = cap * 2.6f;
        unsigned short tail[96];
        float r_w, w, x, y, at;
        /* after R */
        VIEWER_WORDS(TXT_VIEWER_ENEMY);
        VIEWER_ADD(VIEWER_GLYPH_Y);
        VIEWER_WORDS(TXT_VIEWER_RESET);
        VIEWER_ADD(GLYPH_B);
        VIEWER_WORDS(TXT_VIEWER_BACK);
        while (n > 0 && line[n - 1] == ' ') {
            --n;
        }
        line[n] = 0;
        memcpy(tail, line, (size_t)(n + 1) * sizeof(line[0]));
        /* before R */
        n = 0;
        VIEWER_ADD(VIEWER_GLYPH_LEFT);
        VIEWER_ADD(VIEWER_GLYPH_RIGHT);
        VIEWER_WORDS(TXT_VIEWER_TURN);
        VIEWER_ADD(VIEWER_GLYPH_UP);
        VIEWER_ADD(VIEWER_GLYPH_DOWN);
        VIEWER_WORDS(TXT_VIEWER_ZOOM);
        VIEWER_ADD(VIEWER_GLYPH_L);
        line[n] = 0;
        r_w = khdays_layout_draw_action(&s.layout, fr, next, 0, 0, h, 0.0f);
        w = khdays_layout_line_width(&s.layout, line, st->size) + (r_w > 0 ? r_w + 8.0f : 0) +
            khdays_layout_line_width(&s.layout, tail, st->size) + 2 * pad;
        x = KHDAYS_LAYOUT_W / 2 - w / 2;
        y = VIEWER_WINDOW[1] + VIEWER_WINDOW[3] + 22.0f;
        khdays_layout_fill_rounded(s.layout.renderer, fr, x, y, w, h, h / 2, BLACK, PLATE_ALPHA);
        khdays_layout_ring_rounded(s.layout.renderer, fr, x, y, w, h, h / 2, PLATE_EDGE, EDGE_GREY, 1.0f);
        at = x + pad;
        for (int sh = 1; sh >= 0; --sh) {
            khdays_layout_draw_line(&s.layout, fr, line, at, y + h / 2 - cap / 2, st->size, 255.0f, 1.0f, sh);
        }
        at += khdays_layout_line_width(&s.layout, line, st->size);
        if (r_w > 0) {
            at += 4.0f;
            at += khdays_layout_draw_action(&s.layout, fr, next, at, y, h, 1.0f) + 4.0f;
        }
        for (int sh = 1; sh >= 0; --sh) {
            khdays_layout_draw_line(&s.layout, fr, tail, at, y + h / 2 - cap / 2, st->size, 255.0f, 1.0f, sh);
        }
    }
#undef VIEWER_WORDS
#undef VIEWER_ADD
}

/* ---- The story's menu --------------------------------------------------------------------------------
 * Over the picture (see its plan above): the camp's bands; KH2's command buttons for the options (camp.2ld's
 * sequence 1: group 133 coming in, 134 a button, 132 the lit one), the words in the camp's font, KH2's NEW!
 * inside Files, KH2's glove (group 22) and firefly at the lit one; Roxas's name and numbers on his plate, the
 * panels' word on theirs; the day in the top band. */
enum { KH2_GLOVE = 22, KH2_LIT = 132, KH2_COMING = 133, KH2_PLAIN = 134 };
#define KH2_AT_X 34.0f /* where the buttons' groups put a button (KH2's dots) */
#define KH2_AT_Y 79.0f
#define KH2_LIT_X 8.0f /* the lit one so much further right, and its pivot KH2_PIVOT more over frames 0-3 */
#define KH2_PIVOT 10.0f
#define KH2_PIVOT_FRAMES 3.0
#define KH2_COMING_FRAMES 15.0
#define KH2_COMING_FROM 60.0f /* a button coming in: from so far left (its pivot's) */
#define KH2_FRAMES_PER_SECOND 60.0
#define KH2_BAND_RIGHT 104.0f /* a button's band's right end: its words' room (dots) */
#define KH2_GLOVE_X (-16.0f)  /* the glove's place from the lit button's */
#define KH2_FIREFLY_X 99.0f   /* the firefly's, at the lit button's top right */
#define KH2_FIREFLY_Y (-3.0f)
#define KH2_PLAIN_GREY 205
#define STORY_FRESH 0.4 /* the menu not drawn longer than this: its buttons come in anew */
#define STORY_NEW_H 30.0f
#define STORY_VALUE_SIZE 1.1f /* the numbers: so much taller than the rows' words */

static struct {
    int tried, ok;
    SDL_Renderer *renderer;
    SDL_Texture *atlas, *sheet, *bright;
    SDL_Texture *icons; /* the panels' textures, the language's (icons_language) */
    int icons_language;
    SDL_Texture *plate; /* the Panels page's status panel, cut out (NULL: the menu's backdrop's part) */
    KhdaysKh2Seq seq;
    double drawn_at, opened_at, lit_at;
    int lit, sub;
} sl;

static void story_forget(void)
{
    if (sl.atlas != NULL) {
        SDL_DestroyTexture(sl.atlas);
    }
    if (sl.icons != NULL) {
        SDL_DestroyTexture(sl.icons);
    }
    if (sl.sheet != NULL) {
        SDL_DestroyTexture(sl.sheet);
    }
    if (sl.bright != NULL) {
        SDL_DestroyTexture(sl.bright);
    }
    if (sl.plate != NULL) {
        SDL_DestroyTexture(sl.plate);
    }
    khdays_kh2_seq_free(&sl.seq);
    memset(&sl, 0, sizeof(sl));
}

/* The screen's own pieces of the story's menu (KH2's sequence and sheet, the plan's atlas again for the
 * switches with its pages), loaded once for the renderer; 0 without them (the menu in Mission Mode's look). */
static int story_ready(SDL_Renderer *renderer)
{
    char relative[200];
    if (sl.tried && sl.renderer == renderer) {
        return sl.ok;
    }
    story_forget();
    sl.tried = 1;
    sl.renderer = renderer;
    sl.lit = -1;
    if (story_plan() == NULL) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/campstory/%s", stp.seq_file);
    khdays_kh2_seq_load(&sl.seq, relative, stp.sheet_units, NULL);
    snprintf(relative, sizeof(relative), "layouts/campstory/%s", stp.sheet_file);
    sl.sheet = khdays_kh2_picture(renderer, relative, 0);
    sl.bright = khdays_kh2_picture(renderer, relative, 1);
    if (stp.status_file[0] != '\0') {
        snprintf(relative, sizeof(relative), "layouts/campstory/%s", stp.status_file);
        sl.plate = khdays_kh2_picture(renderer, relative, 0);
    }
    {
        SDL_Surface *surface = SDL_CreateSurfaceFrom(stp.w, stp.h, SDL_PIXELFORMAT_RGBA32, stp.atlas, stp.w * 4);
        if (surface != NULL) {
            sl.atlas = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_DestroySurface(surface);
        }
        if (sl.atlas != NULL) {
            SDL_SetTextureScaleMode(sl.atlas, SDL_SCALEMODE_LINEAR);
        }
    }
    sl.ok = sl.seq.agroup_count > KH2_PLAIN && sl.sheet != NULL && sl.atlas != NULL;
    story_failed = !sl.ok;
    if (!sl.ok) {
        SDL_Log(
            "camp menu: layouts/campstory's KH2 sequence or sheet missing (the story's menu in Mission Mode's look)");
    }
    return sl.ok;
}

/* Whether the screen draws the story's menu in its look (its pieces loaded for the renderer). */
static int story_on(const KhdaysCampView *v)
{
    return story_look(v) && story_ready(s.layout.renderer);
}

/* Drawn this picture: the buttons come in anew when the menu was not drawn a moment. */
static void story_drawn(double now)
{
    if (now - sl.drawn_at > STORY_FRESH) {
        sl.opened_at = now;
        sl.lit = -1;
    }
    sl.drawn_at = now;
}

/* The part of a piece of the picture's atlas (placed at `at`, its texels `texels`) inside `clip` (the layout's
 * units; NULL all of it), over the window at `alpha`. */
static void story_atlas_part(const KhdaysLayoutFrame *fr, const float at[4], const float texels[4], const float *clip,
                             float alpha)
{
    float x0 = at[0], y0 = at[1], x1 = at[0] + at[2], y1 = at[1] + at[3];
    if (clip != NULL) {
        x0 = SDL_max(x0, clip[0]);
        y0 = SDL_max(y0, clip[1]);
        x1 = SDL_min(x1, clip[0] + clip[2]);
        y1 = SDL_min(y1, clip[1] + clip[3]);
    }
    if (x1 <= x0 || y1 <= y0 || alpha <= 0.002f || at[2] <= 0 || at[3] <= 0) {
        return;
    }
    {
        const float kx = texels[2] / at[2], ky = texels[3] / at[3];
        const SDL_FRect src = {texels[0] + (x0 - at[0]) * kx, texels[1] + (y0 - at[1]) * ky, (x1 - x0) * kx,
                               (y1 - y0) * ky};
        const SDL_FRect dst = khdays_layout_to_window(fr, x0, y0, x1 - x0, y1 - y0);
        SDL_SetTextureAlphaModFloat(sl.atlas, alpha < 1.0f ? alpha : 1.0f);
        SDL_RenderTexture(sl.renderer, sl.atlas, &src, &dst);
        SDL_SetTextureAlphaModFloat(sl.atlas, 1.0f);
    }
}

/* The picture's block and plates, inside `clip`, at `alpha`. */
static void story_plates(const KhdaysLayoutFrame *fr, const float *clip, float alpha)
{
    for (int k = 0; k < STORY_PLATES; ++k) {
        story_atlas_part(fr, stp.at[k], stp.piece[k], clip, alpha);
    }
}

/* The picture again, at the window's resolution: the pattern drifting as the Files pages and the save page draw
 * it (files_pattern_part, the same at the same moment: the picture's own, made at the 3D's scale and
 * stretched, looked a little off beside theirs), the block and the plates at `plates`, all around Roxas's plate,
 * and over it at `1 - model` (1: the picture's own there, the model in it; 0: covered, no model). Roxas's plate's
 * frame hides the seam; his model is cut at its edge. */
static void story_backdrop(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, double now, float model, float plates)
{
    const float *m = stp.at[1];
    const float strips[4][4] = {{0, 0, KHDAYS_LAYOUT_W, m[1]},
                                {0, m[1] + m[3], KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H - m[1] - m[3]},
                                {0, m[1], m[0], m[3]},
                                {m[0] + m[2], m[1], KHDAYS_LAYOUT_W - m[0] - m[2], m[3]}};
    if (!applied) {
        model = 0.0f; /* the picture is not the menu's: no model under, the window drawn whole */
    }
    for (int i = 0; i < 4; ++i) {
        if (fl.pattern != NULL) {
            files_pattern_part(renderer, fr, now, strips[i][0], strips[i][1], strips[i][2], strips[i][3], 1.0f);
        }
        story_plates(fr, strips[i], plates);
    }
    if (model < 1.0f) {
        if (fl.pattern != NULL) {
            files_pattern_part(renderer, fr, now, m[0], m[1], m[2], m[3], 1.0f - model);
        }
        story_plates(fr, m, plates * (1.0f - model));
    }
}

static float story_eased(double t)
{
    t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    return (float)((sin(t * 3.14159265358979 - 1.5707963267949) + 1.0) / 2.0); /* KH2's (SEQD without LINEAR) */
}

/* A line in the camp's font, its capitals `cap` high from y, its left at x, at `alpha` over its shadow; made
 * smaller (its capitals' middle kept) to fit `room` (0 any width). */
static void story_line_at(const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y, float cap,
                          const Uint8 rgb[3], float alpha, float room)
{
    const float ink = s.layout.ink_bottom - s.layout.ink_top;
    float size = ink > 0 ? cap / ink : 1.0f, w;
    if (t == NULL || t[0] == 0 || alpha <= 0.002f) {
        return;
    }
    w = khdays_layout_line_width(&s.layout, t, size);
    if (room > 0 && w > room) {
        size *= room / w;
        y += (cap - ink * size) / 2;
    }
    khdays_layout_draw_line_rgb(&s.layout, fr, t, x, y, size, rgb, alpha, 1);
    khdays_layout_draw_line_rgb(&s.layout, fr, t, x, y, size, rgb, alpha, 0);
}

/* The options' column (`page` as draw_options': 0 the menu's, 1 dimmed as on its pages), at `alpha`; the glove
 * and the firefly with `pointer`. */
static void story_list(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, float page, float alpha, int pointer,
                       double now)
{
    unsigned short *const *names = v->sub != 0 ? s.story_sub + 2 * (v->sub - 1) : v->story ? s.story_option : s.option;
    const int rows = v->sub != 0 ? 2 : KHDAYS_CAMP_OPTIONS;
    const float k = stp.list[3], pitch = stp.list[2], widen = stp.list[4];
    const float others = 1.0f - page * (1.0f - DIMMED);
    const double coming = (now - sl.opened_at) * KH2_FRAMES_PER_SECOND;
    const float came = coming >= KH2_COMING_FRAMES ? 1.0f : story_eased(coming / KH2_COMING_FRAMES);
    const int news = fl.ok && v->files_new != 0 && (v->sub == 0 || v->sub == 2);
    const KhdaysLayoutPiece *badge = fl.ok ? khdays_layout_piece(&fl.layout, "new") : NULL;
    KhdaysKh2Stage st;
    float shift;
    if (alpha <= 0.002f) {
        return;
    }
    /* the lit one lit anew when the cursor moves: its slide from its first frame */
    if (v->selected != sl.lit || v->sub != sl.sub) {
        sl.lit = v->selected;
        sl.sub = v->sub;
        sl.lit_at = now;
    }
    shift = KH2_LIT_X + KH2_PIVOT * story_eased((now - sl.lit_at) * KH2_FRAMES_PER_SECOND / KH2_PIVOT_FRAMES);
    khdays_kh2_stage(&st, sl.renderer, sl.sheet, sl.bright, fr->scale * k,
                     fr->ox + (stp.list[0] - KH2_AT_X * k) * fr->scale,
                     fr->oy + (stp.list[1] - KH2_AT_Y * k) * fr->scale);
    st.widen = widen;
    st.widen_from = stp.list[5];
    for (int i = 0; i < rows; ++i) {
        const int lit = i == v->selected;
        const float row = (v->question || v->waiting ? DIMMED : lit ? 1.0f : others) * alpha;
        const float words = (v->locked[i] ? DIMMED * alpha : row) * (lit ? 1.0f : came);
        const float from = lit ? shift : -KH2_COMING_FROM * (1.0f - came);
        const Uint8 grey = lit ? 255 : KH2_PLAIN_GREY, rgb[3] = {grey, grey, grey};
        const int on = news && (v->sub == 0 ? i == STORY_FILES_ROW : ((v->files_new >> i) & 1) != 0);
        const float middle = stp.list[1] + ((float)i * pitch + stp.option[1]) * k;
        float room = (KH2_BAND_RIGHT + widen - stp.option[0] - 6.0f) * k;
        st.alpha = row;
        if (lit) {
            khdays_kh2_seq_draw(&st, &sl.seq, KH2_LIT, (int)((now - sl.lit_at) * KH2_FRAMES_PER_SECOND), 0,
                                (float)i * pitch);
        } else if (coming < KH2_COMING_FRAMES) {
            khdays_kh2_seq_draw(&st, &sl.seq, KH2_COMING, (int)coming, 0, (float)i * pitch);
        } else {
            khdays_kh2_seq_draw(&st, &sl.seq, KH2_PLAIN, 0, 0, (float)i * pitch);
        }
        if (on && badge != NULL && badge->h > 0) {
            /* KH2's NEW! at the band's right end, inside it */
            const float bw = badge->w * STORY_NEW_H / badge->h;
            files_badge(fr, 0, stp.list[0] + (from + KH2_BAND_RIGHT + widen - 4.0f) * k - bw, middle - STORY_NEW_H / 2,
                        STORY_NEW_H, now, words);
            room -= bw + 8.0f;
        }
        story_line_at(fr, names[i], stp.list[0] + (from + stp.option[0]) * k, middle - stp.option[2] / 2, stp.option[2],
                      rgb, words, room);
    }
    if (pointer && !v->question && v->selected >= 0 && v->selected < rows && page < 1.0f) {
        const float y = (float)v->selected * pitch;
        st.alpha = alpha * (1.0f - page);
        khdays_kh2_seq_draw(&st, &sl.seq, KH2_GLOVE, (int)(now * KH2_FRAMES_PER_SECOND), KH2_AT_X + shift + KH2_GLOVE_X,
                            KH2_AT_Y + y);
        if (!v->waiting) {
            khdays_layout_firefly(sl.renderer, fr, KHDAYS_FIREFLY_CAMP,
                                  stp.list[0] + (shift + KH2_FIREFLY_X + widen) * k,
                                  stp.list[1] + (y + KH2_FIREFLY_Y) * k, k, alpha * (1.0f - page));
        }
    }
}

/* The option a page is of beside the MENU or SAVE pill (tab_button's place `at`): KH2's lit button made as
 * tall and as long as the place (its frame there, its middle stretched), pulsing, the option's words in it
 * (white, as the list's lit one's), the firefly at its right end. */
#define KH2_FRAME_LEFT 36.0f /* the lit group's frame's left and top (its dots: its place, its pivot, its edges) */
#define KH2_FRAME_TOP 74.0f
#define KH2_FRAME_W 125.0f
#define KH2_FRAME_H 31.0f
#define KH2_EDGE_X 16.0f /* a button's place from its frame's left and top */
#define KH2_EDGE_Y 5.0f
static void story_tab_button(const KhdaysLayoutFrame *fr, const float *at, const unsigned short *label, float alpha)
{
    static const Uint8 white[3] = {255, 255, 255};
    const float k = at[3] / KH2_FRAME_H, widen = at[2] / k - KH2_FRAME_W;
    const float cap = stp.option[2] * k / stp.list[3];
    const double now = seconds_now();
    KhdaysKh2Stage st;
    if (!sl.ok) {
        return;
    }
    khdays_kh2_stage(&st, sl.renderer, sl.sheet, sl.bright, fr->scale * k,
                     fr->ox + (at[0] - KH2_FRAME_LEFT * k) * fr->scale,
                     fr->oy + (at[1] - KH2_FRAME_TOP * k) * fr->scale);
    st.widen = widen;
    st.widen_from = stp.list[5];
    st.alpha = alpha;
    khdays_kh2_seq_draw(&st, &sl.seq, KH2_LIT, (int)KH2_PIVOT_FRAMES + 1 + (int)(now * KH2_FRAMES_PER_SECOND), 0, 0);
    story_line_at(fr, label, at[0] + (KH2_EDGE_X + stp.option[0]) * k,
                  at[1] + (KH2_EDGE_Y + stp.option[1]) * k - cap / 2, cap, white, alpha,
                  (KH2_BAND_RIGHT + widen - stp.option[0] - 6.0f) * k);
    khdays_layout_firefly(sl.renderer, fr, KHDAYS_FIREFLY_TAB, at[0] + (KH2_EDGE_X + KH2_FIREFLY_X + widen) * k,
                          at[1] + (KH2_EDGE_Y + KH2_FIREFLY_Y) * k, k, alpha);
}

/* ---- The story's menu's panels -------------------------------------------------------------------
 * Roxas's panels on their plate as the Panels page has them (runtime/menus/camp.h's grid), a page of its five
 * columns and eight rows, Y turning the pages (camp.c): each panel the page's own texture (UI/pnl/3d_&.pak:
 * a DS dot three layout units), without the page's frame, over a soft shadow (the Moogle Shop's lists': its
 * shape in black, three times a little apart); the slots not open yet the game's locked one, faint; each
 * link's footprint outlined in yellow (its shape around its anchor, the panels in it the panels linked); all
 * floating as the DS's screens on the Files pages (up and down, the shadow left where it is, fainter as they
 * rise); the page and Y's icon at the plate's top right while there is more than one. */
#define PANEL_LOCKED 0xd0 /* the locked slot's texture (Ov025_ShowGridPage: tag 0xd1) */
#define PANEL_LOCKED_ALPHA 0.35f
#define PANEL_COLUMNS 5
#define PANEL_ROWS 8
#define PANEL_LINE 3.0f /* a link's outline: a DS dot, and its dark edge */
#define PANEL_LINE_EDGE 1.5f
#define STORY_PAGES_CAP 22.0f
static const Uint8 PANEL_LINK[3] = {248, 216, 0};
static const float PANEL_SHADOW[3][3] = {{4.5f, 4.5f, 0.16f}, {2.7f, 2.7f, 0.16f}, {6.3f, 6.3f, 0.12f}};

/* The panels' textures for the language (loaded when it changes); NULL without them. */
static SDL_Texture *story_icons(int language)
{
    if (language != sl.icons_language) {
        char relative[200];
        if (sl.icons != NULL) {
            SDL_DestroyTexture(sl.icons);
            sl.icons = NULL;
        }
        sl.icons_language = language;
        if (stp.icons[0] != '\0' && stp.icon_columns > 0) {
            snprintf(relative, sizeof(relative), "layouts/campstory/%s/%s",
                     CODES[language >= 1 && language < LANGUAGES ? language : 1], stp.icons);
            sl.icons = khdays_kh2_picture(sl.renderer, relative, 0);
        }
    }
    return sl.icons;
}

/* How many pages the grid has open (its slots'), and the one Y has turned to. */
static int story_panel_pages(const KhdaysCampView *v)
{
    return v->panel_cells > 0 ? (v->panel_cells - 1) / KHDAYS_CAMP_PANEL_CELLS + 1 : 1;
}

/* A side of a link's cell, outlined: on the cells' edge, over its dark edge. */
static void story_link_side(const KhdaysLayoutFrame *fr, float x, float y, float w, float h, float alpha)
{
    const float e = PANEL_LINE_EDGE;
    SDL_FRect r = khdays_layout_to_window(fr, x - e, y - e, w + 2 * e, h + 2 * e);
    SDL_SetRenderDrawBlendMode(sl.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColorFloat(sl.renderer, 0.0f, 0.0f, 0.0f, 0.6f * alpha);
    SDL_RenderFillRect(sl.renderer, &r);
    r = khdays_layout_to_window(fr, x, y, w, h);
    SDL_SetRenderDrawColor(sl.renderer, PANEL_LINK[0], PANEL_LINK[1], PANEL_LINK[2], (Uint8)(alpha * 255.0f + 0.5f));
    SDL_RenderFillRect(sl.renderer, &r);
}

static void story_panels(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, float alpha)
{
    const int pages = story_panel_pages(v), page = (int)(khdays_camp_panel_turns() % (unsigned)pages);
    const float *at = stp.panels, cell = at[2] / PANEL_COLUMNS, half = PANEL_LINE / 2;
    const float amp = stp.panels_float[0], period = stp.panels_float[1];
    const float rise = period > 0 ? amp * (float)sin(seconds_now() * 6.28318530718 / period) : 0.0f;
    const float shade = 1.0f - 0.25f * (amp > 0 ? rise / amp : 0.0f);
    const uint16_t *grid = v->panel[page < KHDAYS_CAMP_PANEL_PAGES ? page : 0];
    SDL_Texture *icons = story_icons(v->language);
    if (icons == NULL || at[2] <= 0 || alpha <= 0.002f) {
        return;
    }
    for (int pass = 0; pass < 2; ++pass) {
        /* the shadows first, then the panels */
        for (int i = 0; i < KHDAYS_CAMP_PANEL_CELLS; ++i) {
            const int id = grid[i], locked = page * KHDAYS_CAMP_PANEL_CELLS + i >= v->panel_cells;
            const int texture = locked ? PANEL_LOCKED : id > 0 && id < STORY_ITEMS ? stp.item[id].tag - 1 : -1;
            const float a = locked ? alpha * PANEL_LOCKED_ALPHA : alpha;
            const float x = at[0] + (float)(i % PANEL_COLUMNS) * cell, y = at[1] + (float)(i / PANEL_COLUMNS) * cell;
            SDL_FRect src, dst;
            if (texture < 0 || texture >= stp.icon_count) {
                continue;
            }
            src.x = (float)(texture % stp.icon_columns) * stp.icon_cell;
            src.y = (float)(texture / stp.icon_columns) * stp.icon_cell;
            src.w = src.h = stp.icon_cell;
            for (int k = pass == 0 ? 0 : 3; k < 3; ++k) {
                dst = khdays_layout_to_window(fr, x + PANEL_SHADOW[k][0], y + PANEL_SHADOW[k][1], cell, cell);
                SDL_SetTextureColorModFloat(icons, 0.0f, 0.0f, 0.0f);
                SDL_SetTextureAlphaModFloat(icons, a * PANEL_SHADOW[k][2] * shade);
                SDL_RenderTexture(sl.renderer, icons, &src, &dst);
            }
            if (pass == 1) {
                dst = khdays_layout_to_window(fr, x, y - rise, cell, cell);
                SDL_SetTextureColorModFloat(icons, 1.0f, 1.0f, 1.0f);
                SDL_SetTextureAlphaModFloat(icons, a);
                SDL_RenderTexture(sl.renderer, icons, &src, &dst);
            }
        }
    }
    SDL_SetTextureColorModFloat(icons, 1.0f, 1.0f, 1.0f);
    SDL_SetTextureAlphaModFloat(icons, 1.0f);
    for (int i = 0; i < KHDAYS_CAMP_PANEL_CELLS; ++i) {
        /* each link's footprint (its shape with its anchor here), its outer sides */
        const int id = grid[i];
        Uint64 mask;
        int left, top;
        if (id <= 0 || id >= STORY_ITEMS || stp.item[id].shape < 0 ||
            page * KHDAYS_CAMP_PANEL_CELLS + i >= v->panel_cells) {
            continue;
        }
        mask = stp.shape[stp.item[id].shape];
        left = i % PANEL_COLUMNS - stp.item[id].col;
        top = i / PANEL_COLUMNS - stp.item[id].row;
        for (int b = 0; b < 64; ++b) {
            const int c = b % 8, r = b / 8, gc = left + c, gr = top + r;
            float x, y;
            if (!((mask >> b) & 1u) || gc < 0 || gc >= PANEL_COLUMNS || gr < 0 || gr >= PANEL_ROWS) {
                continue;
            }
            x = at[0] + (float)gc * cell;
            y = at[1] + (float)gr * cell - rise;
            if (c == 0 || !((mask >> (b - 1)) & 1u)) {
                story_link_side(fr, x - half, y - half, PANEL_LINE, cell + PANEL_LINE, alpha);
            }
            if (c == 7 || !((mask >> (b + 1)) & 1u)) {
                story_link_side(fr, x + cell - half, y - half, PANEL_LINE, cell + PANEL_LINE, alpha);
            }
            if (r == 0 || !((mask >> (b - 8)) & 1u)) {
                story_link_side(fr, x - half, y - half, cell + PANEL_LINE, PANEL_LINE, alpha);
            }
            if (r == 7 || !((mask >> (b + 8)) & 1u)) {
                story_link_side(fr, x - half, y + cell - half, cell + PANEL_LINE, PANEL_LINE, alpha);
            }
        }
    }
    if (pages > 1 && stp.pages[2] > 0) {
        /* Y's icon (the player's input for the DS's Y: the game's glyph) and the page */
        static const Uint8 white[3] = {255, 255, 255};
        unsigned short line[16];
        char digits[12];
        int n = 0;
        line[n++] = (unsigned short)KHDAYS_LAYOUT_GLYPH_Y;
        line[n++] = ' ';
        snprintf(digits, sizeof(digits), "%d/%d", page + 1, pages);
        for (const char *d = digits; *d != 0 && n < 15; ++d) {
            line[n++] = (unsigned short)*d;
        }
        line[n] = 0;
        camp_text(fr, line, stp.pages[0], stp.pages[1] + (stp.pages[2] - STORY_PAGES_CAP) / 2, STORY_PAGES_CAP, white,
                  1);
    }
}

/* A row of Roxas's numbers: the game's field its status_&.s word names (LV, HP, EXP, Next Level). */
static int story_field(int record)
{
    switch (record) {
    case 0: return 2;
    case 1: return 3;
    case 9: return 0xe;
    case 10: return 0xf;
    default: return -1;
    }
}

/* The tokens on Roxas's plate, at its bottom left by his feet, in one row on his rows' left: status_&.s's word, the
 * DS's two symbols (the old menu's trials: the sigil, the crown) each with its count (the status panel's field
 * 0x12, its two values), the counts in the LV line's yellow. */
#define STORY_TOKENS_FROM_BOTTOM 62.0f /* the row's middle above the plate's bottom */
#define STORY_TOKENS_CAP 22.0f
#define STORY_TOKENS_SYMBOL 30.0f
#define STORY_TOKENS_GAP 14.0f
static const Uint8 STORY_TOKENS_COUNT[3] = {255, 230, 60};

static void story_tokens(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    static const char *const symbols[2] = {"sigil", "crown"};
    const unsigned short *label = khdays_layout_string(&s.status, LABEL_TRIALS);
    const float mid = stp.at[1][1] + stp.at[1][3] - STORY_TOKENS_FROM_BOTTOM, top = mid - STORY_TOKENS_CAP / 2;
    float x = stp.stat[0][0];
    if (stp.at[1][3] <= 0 || x <= 0) {
        return;
    }
    x += camp_text(fr, label, x, top, STORY_TOKENS_CAP, WHITE, 0);
    for (int i = 0; i < 2; ++i) {
        const KhdaysLayoutPiece *p = khdays_layout_piece(&s.layout, symbols[i]);
        char value[16];
        unsigned short count[16];
        x += STORY_TOKENS_GAP * 1.6f;
        if (p != NULL && p->h > 0) {
            const float w = p->w * STORY_TOKENS_SYMBOL / p->h;
            khdays_layout_draw_piece(&s.layout, fr, symbols[i], x, mid - STORY_TOKENS_SYMBOL / 2, w,
                                     STORY_TOKENS_SYMBOL, 1.0f);
            x += w + STORY_TOKENS_GAP;
        }
        snprintf(value, sizeof(value), "%u", v->field[0x12].has ? v->field[0x12].value[i] : 0u);
        khdays_layout_utf8(value, count, 16);
        x += camp_text(fr, count, x, mid - STORY_TOKENS_CAP * STORY_VALUE_SIZE / 2, STORY_TOKENS_CAP * STORY_VALUE_SIZE,
                       STORY_TOKENS_COUNT, 0);
    }
}

/* Roxas's plate: his name (in capitals, as BBS's), his rows, each its words and its number in its line's
 * colour; the tokens; the panels' plate's word. */
static void story_status(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    if (v->character >= 0 && stp.title[2] > 0) {
        const unsigned short *name = khdays_layout_string(&s.status, NAMES + v->character);
        unsigned short upper[32];
        int n = 0;
        for (; name != NULL && name[n] != 0 && n < 31; ++n) {
            upper[n] = name[n] >= 'a' && name[n] <= 'z' ? (unsigned short)(name[n] - 32) : name[n];
        }
        upper[n] = 0;
        camp_text(fr, upper, stp.title[0], stp.title[1], stp.title[2], WHITE, 0);
    }
    for (int r = 0; r < STORY_STATS; ++r) {
        const float *at = stp.stat[r];
        const int field = story_field(stp.stat_record[r]);
        char value[32];
        unsigned short wide[32];
        if (at[4] <= 0) {
            continue;
        }
        camp_text(fr, khdays_layout_string(&s.status, stp.stat_record[r]), at[0], at[1], at[4], stp.stat_colour[r], 0);
        if (field >= 0) {
            value_text(v, field, value, sizeof(value));
            khdays_layout_utf8(value, wide, 32);
            camp_text(fr, wide, at[2], at[3], at[4] * STORY_VALUE_SIZE, stp.stat_colour[r], 1);
        }
    }
    if (stp.deck_title[2] > 0) {
        camp_text(fr, v->story ? s.story_option[STORY_PANELS_OPTION] : s.option[MISSION_PANELS_OPTION],
                  stp.deck_title[0], stp.deck_title[1], stp.deck_title[2], WHITE, 0);
    }
    story_tokens(fr, v);
    story_panels(fr, v, 1.0f);
}

/* The day in the top band (info.s's "DAY %d", its day): its words' capitals first, the rest small, as the
 * band's own words ("Day 159"). */
static void story_day(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const unsigned short *format = khdays_layout_string(&s.info, INFO_DAY);
    unsigned short line[64];
    int first = 1;
    if (format == NULL || stp.day[2] <= 0) {
        return;
    }
    with_number(format, v->day, line, 64);
    for (int i = 0; line[i] != 0; ++i) {
        const unsigned short c = line[i];
        const int upper = (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7);
        const int letter = upper || (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7);
        if (upper && !first) {
            line[i] = (unsigned short)(c + 32);
        }
        first = !letter;
    }
    camp_text(fr, line, stp.day[0], stp.day[1], stp.day[2], stp.day_colour, 1);
}

/* The menu's own pieces (the ones its pages have not): the character's plate's words, the description
 * (`description`), the question's answers, the prompt, the story's day or Mission Mode's notice (the DS's line
 * over its menu, in the top band's right as its own look has it). */
static void story_menu_group(const KhdaysLayoutFrame *fr, const KhdaysCampView *v, int description)
{
    menu_sub_button(fr, v);
    story_status(fr, v);
    if (description) {
        draw_description(fr, v);
    }
    if (v->question) {
        draw_answers(fr, v);
    }
    draw_prompt(fr, v);
    if (v->story) {
        story_day(fr, v);
    } else {
        /* Mission Mode's members (each player's place, character, name and where in the menu) in the top band, as
         * its own look has them: the menu's alone (the pages' option beside the MENU pill is where they are) */
        draw_members(fr, v);
        draw_notice(fr, v);
    }
}

/* What it has with its pages: the numbers, the settings key. */
static void story_numbers(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    big_digits(fr, v->munny, place("munny"), s.munny_colour);
    hearts_word(fr);
    big_digits(fr, v->hearts, place("hearts"), s.hearts_colour);
    draw_settings_key(fr);
}

/* ---- The story's Panels page -------------------------------------------------------------------------
 * The camp's screen 2 (runtime/menus/camp.h `panels`) in the story's menu's look. The DS's lower screen -- its
 * grid and list, its 3D panels in it -- is a floating card (the one-screen picture's), its description's line
 * in the bottom band; left of it the old status panel. What X shows on the upper screen (the list of what the
 * panels give) is a second card, small in the left room until X swaps the two as cards of a deck. */
#define PANELS_STATUS_W 640.0f /* camp.txt's panel: the old status panel's size */
#define PANELS_STATUS_H 650.0f
#define PANELS_EDGE 5.0f       /* the DS's cards' black frame */
#define PANELS_FRESH 0.5       /* the page not drawn longer than this: its cards in their places at once */
#define PANELS_CONTENT_IN 0.15 /* the list's card: its first picture fading in over its back */
#define PANELS_WORD_CAP 26.0f  /* the left room's word: its capitals' height, below the card */
#define PANELS_WORD_GAP 26.0f
static const float PANELS_SHADOW[3][3] = {{11.0f, 11.0f, 0.16f}, {7.0f, 7.0f, 0.16f}, {16.0f, 16.0f, 0.12f}};
static const Uint8 PANELS_BACK[3] = {26, 40, 78}, PANELS_BACK_EDGE[3] = {112, 146, 204};

static struct {
    /* the swap: t 0 the numbers in the block, 1 the list; from `from` to `to` since `at` */
    double drawn_at, at, upper_at, opened_at;
    float from, to;
    int view;           /* the view the cards go to (page B's), -1 none yet */
    int instant;        /* opened anew: the list's card shown at once when it has its picture (no fading in) */
    double modelled_at; /* the one-screen picture's thread: the page's model made last (a visit's start) */
    SDL_Texture *plate;
    int plate_w, plate_h;
    /* the DS's upper screen as composed apart (khdays_camp_screen_panels_upper), its texture */
    SDL_SpinLock lock;
    uint32_t pixels[256 * 192];
    unsigned serial, uploaded;
    int have, shown;
    SDL_Texture *ds, *sharp;
    int pixel_art, sharp_k;
    /* the last one taken showed the list (this visit); the card's (gfx_render.h khdays_gfx_render_shop), composed
     * once that did: its texels a DS dot as shown */
    volatile int listed;
    int dots;
    /* the lower card as the one-screen picture made last has it: its place (floating, not slid), the switches'
     * slide, its opacity */
    SDL_SpinLock card_lock;
    float card[4], card_dx, card_alpha;
    int card_set;
} pn = {.view = -1};

static void panels_forget(void)
{
    if (pn.plate != NULL) {
        SDL_DestroyTexture(pn.plate);
    }
    if (pn.ds != NULL) {
        SDL_DestroyTexture(pn.ds);
    }
    if (pn.sharp != NULL) {
        SDL_DestroyTexture(pn.sharp);
    }
    pn.plate = pn.ds = pn.sharp = NULL;
    pn.plate_w = pn.plate_h = pn.sharp_k = 0;
    pn.uploaded = pn.serial - 1u;
}

/* Whether the upper screen shows the list of what the panels give: the list's frame, the DS's blue, at its left (the
 * numbers' view has its red column there; an empty screen, black). */
#define PANELS_LIST_TEST_X0 4
#define PANELS_LIST_TEST_X1 60
#define PANELS_LIST_TEST_Y0 20
#define PANELS_LIST_TEST_Y1 140
static int panels_shows_list(const uint32_t *pixels)
{
    int blue = 0, all = 0;
    for (int y = PANELS_LIST_TEST_Y0; y < PANELS_LIST_TEST_Y1; y += 2) {
        for (int x = PANELS_LIST_TEST_X0; x < PANELS_LIST_TEST_X1; x += 2) {
            const uint32_t c = pixels[y * 256 + x];
            const int r = (int)((c >> 16) & 255u), b = (int)(c & 255u);
            blue += b > r + 8;
            ++all;
        }
    }
    return blue * 2 >= all;
}

void khdays_camp_screen_panels_upper(const uint32_t *pixels)
{
    pn.listed = panels_shows_list(pixels);
    if (!pn.listed) {
        /* (a picture of the page setting up that is not the list yet, or no more) */
        static int told;
        if (told < 4) {
            ++told;
            khdays_log("camp: the Panels page's upper screen, slid back, is not its list: left out (its dot 30, 80: "
                       "%06x)",
                       (unsigned)(pixels[80 * 256 + 30] & 0xffffffu));
        }
        return;
    }
    SDL_LockSpinlock(&pn.lock);
    memcpy(pn.pixels, pixels, sizeof(pn.pixels));
    ++pn.serial;
    pn.have = 1;
    SDL_UnlockSpinlock(&pn.lock);
}

int khdays_camp_screen_panels_listed(int *scale)
{
    *scale = pn.dots;
    return pn.listed && pn.have;
}

/* The one-screen picture's thread, at each model of the page: a visit's start (the page not modelled a moment) forgets
 * the last visit's list (the panels may have changed since). */
static void panels_visit(double now)
{
    if (now - pn.modelled_at > PANELS_FRESH) {
        SDL_LockSpinlock(&pn.lock);
        pn.have = 0;
        pn.listed = 0;
        SDL_UnlockSpinlock(&pn.lock);
    }
    pn.modelled_at = now;
}

static int panels_ready(const KhdaysCampView *v)
{
    return panels_look(v) && story_ready(s.layout.renderer);
}

/* How much of the page's own pieces shows and how far they are moved (the switches with the menu: sliding in from
 * the right and fading in, out the same way). Either thread. */
static void panels_presence(double now, float *alpha, float *dx)
{
    const double t = now - sw.at;
    *alpha = 1.0f;
    *dx = 0.0f;
    if (sw.phase == SW_NONE || !sw.panels) {
        return;
    }
    if (sw.to_page) {
        const float in = sw.phase == SW_IN ? (float)eased(t / FILES_IN_SECONDS) : 0.0f;
        *alpha = in;
        *dx = SW_SLIDE * (1.0f - in);
    } else {
        const float out = sw.phase == SW_OUT ? (float)eased(t / SW_OUT_SECONDS) : 1.0f;
        *alpha = 1.0f - out;
        *dx = SW_SLIDE * out;
    }
}

/* The lower card at `now`: its place floating (the switch's slide apart: `dx`), its opacity. Either thread. */
static void panels_lower_now(double now, float out[4], float *dx, float *alpha)
{
    const float rise =
        stp.panels_float[1] > 0 ? stp.panels_float[0] * (float)sin(now * 6.28318530718 / stp.panels_float[1]) : 0.0f;
    out[0] = stp.lower[0];
    out[1] = stp.lower[1] - rise;
    out[2] = stp.lower[2];
    out[3] = stp.lower[3];
    panels_presence(now, alpha, dx);
}

/* The one-screen picture's thread: the lower card as the picture now made has it (the screen's frame and hole go
 * where it is in that picture). */
static void panels_lower_made(const float card[4], float dx, float alpha)
{
    SDL_LockSpinlock(&pn.card_lock);
    memcpy(pn.card, card, sizeof(pn.card));
    pn.card_dx = dx;
    pn.card_alpha = alpha;
    pn.card_set = 1;
    SDL_UnlockSpinlock(&pn.card_lock);
}

static int panels_lower_shown(float card[4], float *dx, float *alpha)
{
    int set;
    SDL_LockSpinlock(&pn.card_lock);
    memcpy(card, pn.card, sizeof(pn.card));
    *dx = pn.card_dx;
    *alpha = pn.card_alpha;
    set = pn.card_set && applied;
    SDL_UnlockSpinlock(&pn.card_lock);
    return set;
}

/* The page's pieces in a switch go where the picture made last has the lower card (its slide, its opacity: the
 * same moment's as the card's, their frame round it), else as the switch has them now. */
static void panels_group_made(float *alpha, float *dx)
{
    float c[4], made_dx, made_alpha;
    if (panels_lower_shown(c, &made_dx, &made_alpha)) {
        *alpha = made_alpha;
        *dx = made_dx;
    }
}

/* The pattern round the lower card (over it at what the card lacks of its opacity), the camp's bands. */
static void panels_backdrop(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, double now)
{
    float c[4], dx, alpha;
    if (!panels_lower_shown(c, &dx, &alpha)) {
        files_pattern(renderer, fr, now);
    } else {
        const float x = c[0] + dx, y = c[1], w = c[2], h = c[3];
        const float strips[4][4] = {{0, 0, KHDAYS_LAYOUT_W, y},
                                    {0, y + h, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H - y - h},
                                    {0, y, x, h},
                                    {x + w, y, KHDAYS_LAYOUT_W - x - w, h}};
        for (int i = 0; i < 4; ++i) {
            files_pattern_part(renderer, fr, now, strips[i][0], strips[i][1], strips[i][2], strips[i][3], 1.0f);
        }
        files_pattern_part(renderer, fr, now, x, y, w, h, 1.0f - alpha);
    }
    files_bands(renderer, fr);
}

/* The lower card's frame and shadow (the shadow where the card is not: its texels are the picture's, under all
 * this), where the picture has the card (its slide the group's, when it is drawn in one). */
static void panels_lower_frame(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, int grouped)
{
    float c[4], dx, alpha;
    if (!panels_lower_shown(c, &dx, &alpha)) {
        return;
    }
    {
        const float e = PANELS_EDGE, x = c[0] - e + (grouped ? 0.0f : dx), y = c[1] - e;
        const float w = c[2] + 2 * e, h = c[3] + 2 * e, sy = stp.lower[1] - e;
        const float rise = stp.lower[1] - c[1], most = stp.panels_float[0] > 0 ? stp.panels_float[0] : 1.0f;
        const SDL_FRect frame = khdays_layout_to_window(fr, x, y, w, h);
        const SDL_Rect clips[2] = {{(int)SDL_ceilf(frame.x + frame.w), 0, 1 << 14, 1 << 14},
                                   {0, (int)SDL_ceilf(frame.y + frame.h), (int)SDL_ceilf(frame.x + frame.w), 1 << 14}};
        SDL_Rect before;
        const int had_clip = SDL_RenderClipEnabled(renderer) && SDL_GetRenderClipRect(renderer, &before);
        for (int k = 0; k < 2; ++k) {
            SDL_SetRenderClipRect(renderer, &clips[k]);
            for (int i = 0; i < 3; ++i) {
                khdays_layout_fill_rounded(renderer, fr, x + PANELS_SHADOW[i][0], sy + PANELS_SHADOW[i][1], w, h, e * 2,
                                           BLACK, PANELS_SHADOW[i][2] * (1.0f - 0.25f * rise / most));
            }
        }
        SDL_SetRenderClipRect(renderer, had_clip ? &before : NULL);
        khdays_layout_ring_rounded(renderer, fr, x, y, w, h, e * 2, e, BLACK, 1.0f);
    }
}

/* The swap's t at `now`. */
static float panels_t(double now)
{
    const float span = fabsf(pn.to - pn.from);
    double u;
    if (span < 1e-4f) {
        return pn.to;
    }
    u = (now - pn.at) / (stp.swap[0] * span);
    u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
    return pn.from + (pn.to - pn.from) * (float)(u * u * (3.0 - 2.0 * u));
}

/* The swap as the view has it: page B's view changing turns the cards from where they are. */
static float panels_swap(const KhdaysCampView *v, double now)
{
    const float target = v->panels_view == 0 ? 1.0f : 0.0f;
    if (now - pn.drawn_at > PANELS_FRESH || pn.view < 0) {
        /* opened anew: the cards in their places, the list's card shown at once with the picture this visit took
         * of it (panels_visit forgets the last visit's) */
        pn.from = pn.to = target;
        pn.view = v->panels_view;
        pn.shown = 0;
        pn.instant = 1;
        pn.opened_at = now;
    } else if (v->panels_view != pn.view) {
        pn.from = panels_t(now);
        pn.to = target;
        pn.at = now;
        pn.view = v->panels_view;
    }
    pn.drawn_at = now;
    return panels_t(now);
}

/* A card `e` of the way from the left room (0) to the block (1): its middle and width, its path bowing `bow`
 * (up: 1). */
static void panels_at(float e, float big_w, float bow, float *cx, float *cy, float *w)
{
    const float bx = stp.status_at[0] + PANELS_STATUS_W / 2, by = stp.status_at[1] + PANELS_STATUS_H / 2;
    *w = stp.slot[2] + (big_w - stp.slot[2]) * e;
    *cx = stp.slot[0] + (bx - stp.slot[0]) * e;
    *cy = stp.slot[1] + (by - stp.slot[1]) * e - stp.swap[1] * sinf(3.14159265f * e) * bow;
}

/* The status panel whole (its plate, its words) at the window's scale, for its card to be drawn at any size. */
static SDL_Texture *panels_plate(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const int w = (int)SDL_ceilf(PANELS_STATUS_W * fr->scale), h = (int)SDL_ceilf(PANELS_STATUS_H * fr->scale);
    SDL_Texture *was;
    KhdaysLayoutFrame at = *fr;
    if (w <= 0 || h <= 0) {
        return NULL;
    }
    if (pn.plate != NULL && (pn.plate_w != w || pn.plate_h != h)) {
        SDL_DestroyTexture(pn.plate);
        pn.plate = NULL;
    }
    if (pn.plate == NULL) {
        pn.plate = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (pn.plate == NULL) {
            return NULL;
        }
        pn.plate_w = w;
        pn.plate_h = h;
        /* drawn over nothing, the texture holds its colours multiplied by their alpha */
        SDL_SetTextureBlendMode(pn.plate, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureScaleMode(pn.plate, SDL_SCALEMODE_LINEAR);
    }
    was = SDL_GetRenderTarget(renderer);
    if (!SDL_SetRenderTarget(renderer, pn.plate)) {
        return NULL;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    at.ox = -stp.status_at[0] * fr->scale;
    at.oy = -stp.status_at[1] * fr->scale;
    if (sl.plate != NULL) {
        const SDL_FRect dst =
            khdays_layout_to_window(&at, stp.status_at[0], stp.status_at[1], PANELS_STATUS_W, PANELS_STATUS_H);
        SDL_RenderTexture(renderer, sl.plate, NULL, &dst);
    } else {
        draw_status_plate(renderer, &at, stp.status_at[0], stp.status_at[1], 1.0f);
    }
    draw_status_at(&at, v, stp.status_at[0], stp.status_at[1]);
    SDL_SetRenderTarget(renderer, was);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return pn.plate;
}

/* The status panel's card `e` of the way to the block (its path bowing down), over its shadow (its own shape). */
static void panels_status_card(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, const KhdaysCampView *v, float e)
{
    SDL_Texture *plate = panels_plate(renderer, fr, v);
    float cx, cy, w;
    panels_at(e, PANELS_STATUS_W, -1.0f, &cx, &cy, &w);
    if (plate != NULL) {
        const float h = w * PANELS_STATUS_H / PANELS_STATUS_W, k = w / PANELS_STATUS_W;
        const SDL_FRect dst = khdays_layout_to_window(fr, cx - w / 2, cy - h / 2, w, h);
        for (int i = 0; i < 3; ++i) {
            const SDL_FRect shadow = {dst.x + PANELS_SHADOW[i][0] * k * fr->scale,
                                      dst.y + PANELS_SHADOW[i][1] * k * fr->scale, dst.w, dst.h};
            SDL_SetTextureColorModFloat(plate, 0.0f, 0.0f, 0.0f);
            SDL_SetTextureAlphaModFloat(plate, PANELS_SHADOW[i][2]);
            SDL_RenderTexture(renderer, plate, NULL, &shadow);
        }
        SDL_SetTextureColorModFloat(plate, 1.0f, 1.0f, 1.0f);
        SDL_SetTextureAlphaModFloat(plate, 1.0f);
        SDL_RenderTexture(renderer, plate, NULL, &dst);
    }
}

/* The DS's upper screen as composed last, in its texture: its pixels sharp (the renderer's pixel art scaling, or
 * the screen drawn N times as large and that scaled smoothly); its texels' size in DS dots (*k). NULL none. */
static SDL_Texture *panels_upper_texture(SDL_Renderer *renderer, float dst_w, float *k)
{
    unsigned serial;
    if (pn.ds == NULL) {
        pn.ds = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
        if (pn.ds == NULL) {
            return NULL;
        }
        pn.pixel_art = SDL_SetTextureScaleMode(pn.ds, SDL_SCALEMODE_PIXELART);
        if (!pn.pixel_art) {
            SDL_SetTextureScaleMode(pn.ds, SDL_SCALEMODE_NEAREST);
        }
        pn.uploaded = pn.serial - 1u;
    }
    SDL_LockSpinlock(&pn.lock);
    serial = pn.serial;
    if (!pn.have) {
        SDL_UnlockSpinlock(&pn.lock);
        return NULL;
    }
    if (serial != pn.uploaded) {
        SDL_UpdateTexture(pn.ds, NULL, pn.pixels, 256 * 4);
    }
    SDL_UnlockSpinlock(&pn.lock);
    {
        /* the card's, the HD tiles and fonts in it (0.1.4's text is laid out for them): composed from the picture
         * the list was first taken this visit, about as large as shown */
        const int n = (int)SDL_ceilf(dst_w / 256.0f);
        int scale = 1;
        unsigned made;
        SDL_Texture *hd = khdays_gfx_render_shop(renderer, 0, KHDAYS_SHOP_NORMAL, &scale, &made);
        pn.dots = n < 2 ? 2 : n > KHDAYS_SHOP_SCALE ? KHDAYS_SHOP_SCALE : n;
        if (hd != NULL) {
            SDL_SetTextureBlendMode(hd, SDL_BLENDMODE_BLEND);
            pn.uploaded = serial;
            *k = (float)scale;
            return hd;
        }
    }
    *k = 1.0f;
    if (!pn.pixel_art) {
        const int n = SDL_max(1, (int)SDL_ceilf(dst_w / 256.0f));
        if (pn.sharp == NULL || pn.sharp_k != n) {
            if (pn.sharp != NULL) {
                SDL_DestroyTexture(pn.sharp);
            }
            pn.sharp =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 256 * n, 192 * n);
            pn.sharp_k = n;
            pn.uploaded = serial - 1u;
            if (pn.sharp != NULL) {
                SDL_SetTextureScaleMode(pn.sharp, SDL_SCALEMODE_LINEAR);
            }
        }
        if (pn.sharp != NULL) {
            if (serial != pn.uploaded) {
                /* (drawn into it while a group's texture may be the target: back to that one after) */
                SDL_Texture *target = SDL_GetRenderTarget(renderer);
                if (SDL_SetRenderTarget(renderer, pn.sharp)) {
                    SDL_RenderTexture(renderer, pn.ds, NULL, NULL);
                    SDL_SetRenderTarget(renderer, target);
                }
            }
            pn.uploaded = serial;
            *k = (float)n;
            return pn.sharp;
        }
    }
    pn.uploaded = serial;
    return pn.ds;
}

/* The list's card `e` of the way to the block (its path bowing up): what the DS's upper screen shows of it (its
 * dots 0 to list_card[0] across), in its black frame over its shadow; its back until it was seen. */
static void panels_list_card(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float e, double now)
{
    const float dots = stp.list_card[0], big_w = dots * stp.list_card[1];
    float cx, cy, w, h, k, x, y, edge, tk = 1.0f;
    SDL_Texture *upper;
    panels_at(e, big_w, 1.0f, &cx, &cy, &w);
    h = w * 192.0f / dots;
    k = w / big_w;
    x = cx - w / 2;
    y = cy - h / 2;
    edge = PANELS_EDGE * SDL_max(k, 0.5f);
    for (int i = 0; i < 3; ++i) {
        khdays_layout_fill_rounded(renderer, fr, x - edge + PANELS_SHADOW[i][0] * k, y - edge + PANELS_SHADOW[i][1] * k,
                                   w + 2 * edge, h + 2 * edge, edge * 2, BLACK, PANELS_SHADOW[i][2]);
    }
    khdays_layout_fill_rounded(renderer, fr, x - edge, y - edge, w + 2 * edge, h + 2 * edge, edge * 2, BLACK, 1.0f);
    upper = panels_upper_texture(renderer, w * fr->scale * 256.0f / dots, &tk);
    if (upper != NULL && !pn.shown) {
        pn.shown = 1;
        pn.upper_at = pn.instant ? now - PANELS_CONTENT_IN : now;
    }
    {
        const float seen = upper == NULL ? 0.0f : (float)SDL_min(1.0, (now - pn.upper_at) / PANELS_CONTENT_IN);
        if (seen < 1.0f) {
            /* its back: the DS's list's blue, its word */
            const unsigned short *word = khdays_layout_string(&s.status, 2);
            const float cap = PANELS_WORD_CAP * SDL_max(k, 0.6f);
            khdays_layout_fill_rounded(renderer, fr, x, y, w, h, edge, PANELS_BACK, 1.0f);
            khdays_layout_ring_rounded(renderer, fr, x + edge, y + edge, w - 2 * edge, h - 2 * edge, edge,
                                       SDL_max(1.5f, 2.0f * k), PANELS_BACK_EDGE, 1.0f);
            if (word != NULL) {
                const KhdaysLayoutText *st = khdays_layout_text(&s.layout, "option");
                const float ink = s.layout.ink_bottom - s.layout.ink_top, size = ink > 0 ? cap / ink : 1.0f;
                const float ww = st != NULL ? khdays_layout_line_width(&s.layout, word, size) : 0.0f;
                story_line_at(fr, word, cx - ww / 2, cy - cap / 2, cap, WHITE, 1.0f, w - 4 * edge);
            }
        }
        if (upper != NULL && seen > 0.0f) {
            const SDL_FRect src = {0, 0, dots * tk, 192.0f * tk};
            const SDL_FRect dst = khdays_layout_to_window(fr, x, y, w, h);
            SDL_SetTextureAlphaModFloat(upper, seen);
            SDL_RenderTexture(renderer, upper, &src, &dst);
            SDL_SetTextureAlphaModFloat(upper, 1.0f);
        }
    }
}

/* The two cards as the swap has them (the one further forward drawn last), X's word under the left room's. */
static void panels_cards(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now)
{
    const float t = panels_swap(v, now);
    if (t < 0.5f) {
        panels_list_card(renderer, fr, t, now);
        panels_status_card(renderer, fr, v, 1.0f - t);
    } else {
        panels_status_card(renderer, fr, v, 1.0f - t);
        panels_list_card(renderer, fr, t, now);
    }
    if (pn.instant && (pn.shown || now - pn.opened_at > PANELS_FRESH)) {
        pn.instant = 0; /* (its picture come, or not by now: X's later fades it in) */
    }
    {
        /* X's word: what the left room's card is, fading out while the cards swap and in again */
        const float alpha = fabsf(2.0f * t - 1.0f);
        const int list = t < 0.5f;
        const char *stats = khdays_text_in(TXT_PANELS_STATS, v->language);
        const unsigned short *abilities = khdays_layout_string(&s.status, 2);
        const float small_h = stp.slot[2] * (list ? 192.0f / stp.list_card[0] : PANELS_STATUS_H / PANELS_STATUS_W);
        const float ink = s.layout.ink_bottom - s.layout.ink_top, size = ink > 0 ? PANELS_WORD_CAP / ink : 1.0f;
        unsigned short line[64];
        int n = 0;
        line[n++] = VIEWER_GLYPH_X;
        line[n++] = ' ';
        if (list) {
            for (int i = 0; abilities != NULL && abilities[i] != 0 && n < 62; ++i) {
                line[n++] = abilities[i];
            }
            line[n] = 0;
        } else {
            line[n] = 0;
            if (stats != NULL) {
                utf16_append(line + n, 64 - n, stats);
            }
        }
        if (alpha > 0.002f) {
            const float lw = khdays_layout_line_width(&s.layout, line, size);
            story_line_at(fr, line, stp.slot[0] - lw / 2, stp.slot[1] + small_h / 2 + PANELS_WORD_GAP, PANELS_WORD_CAP,
                          WHITE, alpha, 0);
        }
    }
}

/* START's submenu and B in the prompt's plate (the page's own help on the DS's upper screen shows only with
 * the list). */
static void panels_prompt(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    static const unsigned short start[3] = {0x3336u, 0x3351u, 0x3357u}; /* the game's START (layout_screen.c's) */
    const unsigned short *whole = khdays_layout_string(&s.mlt, PROMPT);
    const char *submenu = khdays_text_in(TXT_PANELS_SUBMENU, v->language);
    unsigned short line[128];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        line[n++] = start[i];
    }
    line[n++] = ' ';
    if (submenu != NULL) {
        n += utf16_append(line + n, 128 - n - 4, submenu);
    }
    line[n++] = ' ';
    line[n++] = ' ';
    line[n++] = ' ';
    while (whole != NULL && *whole != 0 && *whole != GLYPH_B) {
        ++whole;
    }
    while (whole != NULL && *whole != 0 && n < 127) {
        line[n++] = *whole++;
    }
    line[n] = 0;
    prompt_plate(fr, line);
}

/* The page's own pieces: Panels beside the MENU pill, the lower card's frame, the two cards, the line, the
 * prompt (`grouped`: drawn in a switch's group, which slides them). */
static void panels_group(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now,
                         int grouped)
{
    option_button(fr, v, "menu_button", v->story ? STORY_PANELS_OPTION : MISSION_PANELS_OPTION, 1.0f);
    panels_lower_frame(renderer, fr, grouped);
    panels_cards(renderer, fr, v, now);
    static const uint16_t no_text[1] = {0};
    draw_text_lines(fr, v->panel_text != NULL ? v->panel_text : no_text, "description", s.description_pitch);
    panels_prompt(fr, v);
}

static void panels_draw(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now)
{
    khdays_layout_begin_over(renderer, fr);
    panels_backdrop(renderer, fr, now);
    panels_group(renderer, fr, v, now, 0);
    story_numbers(fr, v);
}

/* The menu and its Preferences page into each other in the story's look: the page's backdrop over the menu's
 * picture, KH2's buttons taking the page's look (the others dimmed, no glove), the rest as draw_page_switch's. */
static void draw_story_page_switch(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, int got,
                                   double now)
{
    const double t = now - sw.at;
    const float out = sw.phase == SW_OUT ? (float)eased(t / SW_OUT_SECONDS) : 1.0f;
    const float in = sw.phase == SW_IN ? (float)eased(t / SW_IN_SECONDS) : 0.0f;
    const float page = sw.to_page ? (sw.phase == SW_OUT ? out : 1.0f) : (sw.phase == SW_OUT ? 1.0f - out : 0.0f);
    const float dir = sw.to_page ? -1.0f : 1.0f;
    const int there = (v->config != 0) == sw.to_page;
    khdays_camp_set_page_drawn(got == 2 && v->config);
    khdays_layout_begin_over(renderer, fr);
    story_drawn(now);
    story_backdrop(renderer, fr, now, v->config ? 0.0f : 1.0f, 1.0f);
    files_bands(renderer, fr);
    draw_backdrop(renderer, fr, s.page_backdrop, page);
    story_list(fr, v, page, 1.0f, 1, now);
    story_numbers(fr, v);
    if (sw.phase == SW_OUT) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            story_menu_group(fr, &sw.from, 1);
        } else {
            draw_page_group(fr, &sw.from, 0);
        }
        if (grouped) {
            group_end(renderer, fr, 1.0f - out, dir * SW_SLIDE * out);
        }
    }
    if (sw.phase == SW_IN && there) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            draw_page_group(fr, v, 0);
        } else {
            story_menu_group(fr, v, 1);
        }
        if (grouped) {
            group_end(renderer, fr, in, -dir * SW_SLIDE * (1.0f - in));
        }
    }
    khdays_layout_end(renderer, fr);
}

/* ---- The menu and the Files pages, the one into the other ----------------------------------------- */

/* Roxas's Diary or Enemy Profiles chosen, the game fades the menu to black and loads the page (the
 * camp's screen 7); B there, the same back. As between the menu and its Preferences page (above), the
 * window's screen keeps the picture up: the bands, the numbers, the description and the settings key
 * stay; the menu's own pieces (its options among them) slide out to the left and its backdrop fades,
 * the pattern drifting in its place (the model's box first: the game takes the model down soon), and
 * once the game shows the page and its picture comes up, the page's pieces -- its rows, the DS's
 * screen -- slide in; back, the other way. */
/* The menu's backdrop at `menu` over the pattern, the model's box showing the model (the one-screen
 * picture's, under all this) at `model`. */
static void files_switch_backdrop(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, double now, float menu,
                                  float model, int story)
{
    const float *m = s.window;
    if (story) {
        /* the story's menu: its block and plates fading over the pattern, its own (the picture's) */
        story_drawn(now);
        story_backdrop(renderer, fr, now, model, menu);
        files_bands(renderer, fr);
        return;
    }
    const float strips[4][4] = {{0, 0, KHDAYS_LAYOUT_W, m[1]},
                                {0, m[1] + m[3], KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H - m[1] - m[3]},
                                {0, m[1], m[0], m[3]},
                                {m[0] + m[2], m[1], KHDAYS_LAYOUT_W - m[0] - m[2], m[3]}};
    if (!applied) {
        model = 0.0f; /* no model under: the box is the backdrop's */
    }
    for (int i = 0; i < 4; ++i) {
        files_pattern_part(renderer, fr, now, strips[i][0], strips[i][1], strips[i][2], strips[i][3], 1.0f);
    }
    files_pattern_part(renderer, fr, now, m[0], m[1], m[2], m[3], 1.0f - model);
    if (menu > 0.002f) {
        if (model > 0.0f) {
            SDL_SetTextureAlphaModFloat(s.backdrop, menu < 1.0f ? menu : 1.0f);
            draw_strips(renderer, fr);
            SDL_SetTextureAlphaModFloat(s.backdrop, 1.0f);
        } else {
            draw_backdrop(renderer, fr, s.backdrop, menu);
        }
    }
    files_bands(renderer, fr);
}

/* The menu's own pieces as the page has them not: its options, the status panel, the trials, the
 * question's answers, the prompt, the day (the description stays). */
static void draw_menu_own(const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    if (story_on(v)) {
        story_list(fr, v, 0.0f, 1.0f, 1, seconds_now());
        story_menu_group(fr, v, 0);
        return;
    }
    menu_sub_button(fr, v);
    draw_options(fr, v, 0.0f);
    draw_status(fr, v);
    draw_trials(fr, v);
    if (v->question) {
        draw_answers(fr, v);
    }
    draw_prompt(fr, v);
    draw_day(fr, v);
}

static void draw_files_switch(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now)
{
    const double t = now - sw.at;
    const float out = sw.phase == SW_OUT ? (float)eased(t / SW_OUT_SECONDS) : 1.0f;
    const float in = sw.phase == SW_IN ? (float)eased(t / FILES_IN_SECONDS) : 0.0f;
    const float dir = sw.to_page ? -1.0f : 1.0f;
    const int there = switch_there(v);
    float menu, model;
    if (sw.to_page) {
        menu = sw.phase == SW_OUT ? 1.0f - out : 0.0f;
        model = sw.phase == SW_OUT ? 1.0f - (float)eased_out(t / SW_GROW_SECONDS) : 0.0f;
    } else {
        menu = sw.phase == SW_IN && there ? in : 0.0f;
        model = menu;
    }
    khdays_camp_set_page_drawn(0);
    khdays_layout_begin_over(renderer, fr);
    files_switch_backdrop(renderer, fr, now, menu, v->files ? 0.0f : model, story_on(v));
    if (sw.phase == SW_OUT) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            draw_menu_own(fr, &sw.from);
        } else {
            files_group(renderer, fr, &sw.from, now);
        }
        if (grouped) {
            group_end(renderer, fr, 1.0f - out, dir * SW_SLIDE * out);
        }
    }
    if (sw.phase == SW_IN && there) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            files_group(renderer, fr, v, now);
        } else {
            draw_menu_own(fr, v);
        }
        if (grouped) {
            group_end(renderer, fr, in, -dir * SW_SLIDE * (1.0f - in));
        }
    }
    files_common(fr, sw.phase == SW_IN && there ? v : &sw.from);
    khdays_layout_end(renderer, fr);
}

/* ---- The menu and the save page, the one into the other ---------------------------------------- */

/* Save chosen, the game fades the menu out, loads its save screen and fades that in; B there (or a file
 * saved), the same back. As with the Files pages the window keeps the picture up: the menu's backdrop
 * fades to the Files pages' pattern (the model's box with it) while the menu's own pieces slide out and
 * the SAVE pill's band comes over the MENU pill's; once the game shows its save screen the page's pieces
 * -- the Save beside the pill, the list's frame, rows and scroll bar -- slide in. Back, the other way.
 * The numbers, the description and the settings key stay. */
static void draw_save_switch(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now)
{
    const double t = now - sw.at;
    const float out = sw.phase == SW_OUT ? (float)eased(t / SW_OUT_SECONDS) : 1.0f;
    const float in = sw.phase == SW_IN ? (float)eased(t / FILES_IN_SECONDS) : 0.0f;
    const float dir = sw.to_page ? -1.0f : 1.0f;
    const int there = switch_there(v);
    float menu, model, page;
    if (sw.to_page) {
        menu = sw.phase == SW_OUT ? 1.0f - out : 0.0f;
        model = sw.phase == SW_OUT ? 1.0f - (float)eased_out(t / SW_GROW_SECONDS) : 0.0f;
        page = sw.phase == SW_OUT ? out : 1.0f;
    } else {
        menu = sw.phase == SW_IN && there ? in : 0.0f;
        model = menu;
        page = 1.0f - menu;
    }
    khdays_camp_set_page_drawn(0);
    khdays_layout_begin_over(renderer, fr);
    files_switch_backdrop(renderer, fr, now, menu, v->save ? 0.0f : model, story_on(v));
    save_band(fr, page, "band");
    if (sw.phase == SW_OUT) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            draw_menu_own(fr, &sw.from);
        } else {
            save_group(renderer, fr, &sw.from, &save_from, now);
        }
        if (grouped) {
            group_end(renderer, fr, 1.0f - out, dir * SW_SLIDE * out);
        }
    }
    if (sw.phase == SW_IN && there) {
        const int grouped = group_begin(renderer);
        if (sw.to_page) {
            save_group(renderer, fr, v, &save_list, now);
        } else {
            draw_menu_own(fr, v);
        }
        if (grouped) {
            group_end(renderer, fr, in, -dir * SW_SLIDE * (1.0f - in));
        }
    }
    save_common(fr, sw.phase == SW_IN && there ? v : &sw.from, NULL);
    khdays_layout_end(renderer, fr);
    sp.drawn = 1;
}

/* ---- The menu and the Panels page, the one into the other --------------------------------------- */

/* Panels chosen, the game fades the menu out and loads its Panels page (the camp's screen 2); B there, the same
 * back. As with the Files pages the window keeps the picture up: the menu's block and plates fade to the pattern
 * (Roxas's model's plate first: the game takes him down soon) while the menu's own pieces -- its description with
 * them, the page's line is another -- slide out to the left; once the game shows the page and its brightness
 * rises, the page's pieces slide in from the right, the DS's lower screen's card with them (the picture's: its
 * place and opacity the same, panels_presence). Back, the other way. The numbers and the settings key stay. */
static void draw_panels_switch(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, double now)
{
    const double t = now - sw.at;
    const float out = sw.phase == SW_OUT ? (float)eased(t / SW_OUT_SECONDS) : 1.0f;
    const float in = sw.phase == SW_IN ? (float)eased(t / FILES_IN_SECONDS) : 0.0f;
    const float dir = sw.to_page ? -1.0f : 1.0f;
    const int there = switch_there(v);
    float menu, model;
    if (sw.to_page) {
        menu = sw.phase == SW_OUT ? 1.0f - out : 0.0f;
        model = sw.phase == SW_OUT ? 1.0f - (float)eased_out(t / SW_GROW_SECONDS) : 0.0f;
    } else {
        menu = sw.phase == SW_IN && there ? in : 0.0f;
        model = menu;
    }
    khdays_camp_set_page_drawn(0);
    khdays_layout_begin_over(renderer, fr);
    if (v->panels) {
        panels_backdrop(renderer, fr, now);
    } else {
        files_switch_backdrop(renderer, fr, now, menu, model, 1);
    }
    if (sw.phase == SW_OUT) {
        const int grouped = group_begin(renderer);
        float alpha = 1.0f - out, dx = dir * SW_SLIDE * out;
        if (sw.to_page) {
            draw_menu_own(fr, &sw.from);
            draw_description(fr, &sw.from);
        } else {
            panels_group(renderer, fr, &sw.from, now, grouped);
            panels_group_made(&alpha, &dx);
        }
        if (grouped) {
            group_end(renderer, fr, alpha, dx);
        }
    }
    if (sw.phase == SW_IN && there) {
        const int grouped = group_begin(renderer);
        float alpha = in, dx = -dir * SW_SLIDE * (1.0f - in);
        if (sw.to_page) {
            panels_group(renderer, fr, v, now, grouped);
            panels_group_made(&alpha, &dx);
        } else {
            draw_menu_own(fr, v);
            draw_description(fr, v);
        }
        if (grouped) {
            group_end(renderer, fr, alpha, dx);
        }
    }
    story_numbers(fr, sw.phase == SW_IN && there ? v : &sw.from);
    khdays_layout_end(renderer, fr);
}

static void draw_switch(SDL_Renderer *renderer, KhdaysLayoutFrame *fr, const KhdaysCampView *v, int got, double now)
{
    if (sw.panels) {
        draw_panels_switch(renderer, fr, v, now);
    } else if (sw.save) {
        draw_save_switch(renderer, fr, v, now);
    } else if (sw.files) {
        draw_files_switch(renderer, fr, v, now);
    } else {
        draw_page_switch(renderer, fr, v, got, now);
    }
}

/* Mission Mode's menu into its Moogle Shop and back without the game's black (shop_screen.h): its Shop
 * chosen (the option whose description is root_&.s's 29) and the game fading the menu out (or the menu
 * gone), the menu is drawn once more as it was, unshaded, and the window handed to the shop's screen,
 * which keeps that picture over everything until the shop is lit; the shop closing, it keeps its own over
 * the menu until the menu is back and lit, the menu opening without its curtain. */
#define SHOP_DESCRIPTION 29
static int to_shop;         /* 1 the menu hands the window over this picture, 2 handed over */
static int16_t shop_master; /* the game's fade at the last picture of the menu */

int khdays_overhaul_camp_draw(SDL_Renderer *renderer)
{
    KhdaysCampView v;
    KhdaysLayoutFrame fr;
    const double now = seconds_now();
    const int got = camp_view_now(&v, &drawn_lists);
    const int handing = khdays_shop_screen_handing();
    khdays_camp_set_panels_drawn(0);  /* (the story's menu says it again below when it draws its panels) */
    khdays_camp_set_preview_drawn(0); /* (and the Panels page, its status card showing the panel held's changes) */
    khdays_camp_set_files_drawn(0);   /* (and the Files page) */
    if (to_shop == 2) {
        if (handing == KHDAYS_SHOP_HAND_IN) {
            return 0; /* the shop's screen holds the window */
        }
        to_shop = 0;
    }
    if (handing == KHDAYS_SHOP_HAND_OUT && got == 2) {
        /* back from the shop: no curtain (the shop's picture is over the menu until it is lit) */
        curtain.phase = CURTAIN_NONE;
        curtain.shown_at = now;
    }
    if (to_shop == 0 && got != 0 && !v.story && v.description == SHOP_DESCRIPTION && !v.leaving && !v.config &&
        !v.save && !v.files && sw.phase == SW_NONE && khdays_shop_screen_ready() &&
        (got == 1 || (v.master < 0 && v.master < shop_master))) {
        to_shop = 1;
        sw.unshaded = 1;
    }
    if (to_shop == 1 && (!khdays_present_one_picture() || (!applied && sw.phase == SW_NONE))) {
        to_shop = 0; /* the menu not drawn: the curtain's way */
    }
    if (got == 2) {
        shop_master = v.master;
    }
    {
        /* the save list as the game's thread left it: kept while it is the camp's save screen's */
        static KhdaysSaveView list;
        static int list_seen; /* the port's list seen on this visit to the save screen */
        khdays_savelist_view(&list);
        if (list.screen == KHDAYS_SAVELIST_SAVE) {
            save_list = list;
            list_seen = 1;
        } else if (!got || !v.save) {
            list_seen = 0;
        }
        sp.drawn = 0;
        if (!got && list.screen == KHDAYS_SAVELIST_LOAD) {
            return load_page_draw(renderer, &list, now); /* the title's load list */
        }
        if (got && v.save && !list_seen && sw.phase == SW_NONE) {
            /* the save screen without the port's list: the game's own (and its pieces) */
            khdays_camp_set_page_drawn(0);
            return curtain_hidden(renderer, now);
        }
    }
    if (!got || !khdays_present_one_picture() || (!applied && sw.phase == SW_NONE && !v.save)) {
        khdays_camp_set_page_drawn(0);
        if (!got || !khdays_present_one_picture()) {
            switch_off();
        }
        /* (its pieces kept: read again at each opening they cost the opening a stall) */
        return curtain_hidden(renderer, now);
    }
    if (renderer != s.layout.renderer || v.language != s.tried) {
        s.tried = v.language;
        s.ok = load(renderer, v.language);
    }
    if (!s.ok || (v.config && !page_ready()) || (v.files && !fl.ok) || (v.save && !save_ready()) ||
        (v.panels && (!fl.ok || !panels_ready(&v)))) {
        khdays_camp_set_page_drawn(0);
        switch_off();
        return 0;
    }
    switch_step(&v, got, now);
    if (sw.phase != SW_NONE) {
        if (!v.config) {
            s.page_top = 0;
        }
        curtain.shown_at = now;
        draw_switch(renderer, &fr, &v, got, now);
        return 1;
    }
    if (sw.unshaded && v.master >= 0) {
        sw.unshaded = 0; /* the game's fade is over: drawn again from here */
    }
    curtain_shown(got == 2, v.leaving, now);
    if (curtain.phase == CURTAIN_HOLD) {
        khdays_camp_set_page_drawn(0);
        return curtain_hidden(renderer, now); /* closed over just now */
    }
    if (v.save) {
        /* the save page: drawn whole by the screen (see "The save page") */
        khdays_camp_set_page_drawn(0);
        save_draw(renderer, &fr, &v, &save_list, now);
        if (!sw.unshaded) {
            khdays_layout_shade(renderer, v.master);
        }
        draw_curtain(renderer, curtain_black(now));
        khdays_layout_end(renderer, &fr);
        sp.drawn = 1;
        return 1;
    }
    if (v.panels) {
        /* the story's Panels page: the picture its pattern and the DS's lower screen's card, the screen the rest (see
         * "The story's Panels page") */
        khdays_camp_set_page_drawn(0);
        khdays_camp_set_preview_drawn(1);
        panels_draw(renderer, &fr, &v, now);
        if (!sw.unshaded) {
            khdays_layout_shade(renderer, v.master);
        }
        draw_curtain(renderer, curtain_black(now));
        khdays_layout_end(renderer, &fr);
        return 1;
    }
    if (v.files) {
        /* the Files pages: drawn whole by the screen (see "The Files pages"); with the model viewer open, its
         * darkened page the picture's backdrop once taken, and over it only the viewer's words */
        KhdaysEnemyViewer ev;
        khdays_enemy_viewer_read(&ev);
        khdays_camp_set_page_drawn(0);
        khdays_camp_set_files_drawn(1);
        if (ev.open && ev.ready && vs.taken && vs.serial == ev.opening) {
            khdays_layout_begin_over(renderer, &fr);
            viewer_draw(&fr, &v, &ev);
            khdays_layout_end(renderer, &fr);
            return 1;
        }
        if (ev.open && ev.ready) {
            khdays_layout_begin_over(renderer, &fr);
            viewer_take(renderer, &fr, &v, now, ev.opening);
            khdays_layout_end(renderer, &fr);
        }
        files_draw(renderer, &fr, &v, now);
        if (!sw.unshaded) {
            khdays_layout_shade(renderer, v.master);
        }
        draw_curtain(renderer, curtain_black(now));
        khdays_layout_end(renderer, &fr);
        return 1;
    }
    /* once a frame (the game's thread reads it at its pad's sample) */
    khdays_camp_set_page_drawn(v.config);
    khdays_layout_begin_over(renderer, &fr);
    if (v.config) {
        /* the Preferences page: its own backdrop drawn again whole, as the menu's is around the model's
         * window. The one-screen picture under it is made at the DS's scale (192 dots high, the 3D's) and
         * stretched to the window: the backdrop alone there came out softer than the menu's, whose
         * backdrop the screen draws itself at the window's resolution (draw_strips) */
        draw_backdrop(renderer, &fr, s.page_backdrop, 1.0f);
        if (story_on(&v)) {
            /* the story's: its KH2 buttons, the others dimmed (the page's own glove at its settings) */
            story_drawn(now);
            story_list(&fr, &v, 1.0f, 1.0f, 0, now);
            story_numbers(&fr, &v);
        } else {
            draw_common(&fr, &v, 1.0f);
        }
        draw_page_group(&fr, &v, 1);
    } else if (story_on(&v)) {
        /* the story's menu in its own look: the picture its backdrop (the pattern, the plates, the model);
         * the camp's bands over it */
        s.page_top = 0;
        story_drawn(now);
        khdays_camp_set_panels_drawn(story_panel_pages(&v)); /* (Y turns their pages here) */
        story_backdrop(renderer, &fr, now, 1.0f, 1.0f);
        files_bands(renderer, &fr);
        story_list(&fr, &v, 0.0f, 1.0f, 1, now);
        story_numbers(&fr, &v);
        story_menu_group(&fr, &v, 1);
    } else {
        s.page_top = 0;
        draw_strips(renderer, &fr);
        draw_common(&fr, &v, 0.0f);
        draw_menu_group(&fr, &v);
    }
    if (!sw.unshaded) {
        khdays_layout_shade(renderer, v.master);
    }
    if (to_shop == 1) {
        /* this picture handed to the shop's screen; the menu let go, no curtain after it */
        khdays_shop_screen_take_over(renderer);
        to_shop = khdays_shop_screen_handing() == KHDAYS_SHOP_HAND_IN ? 2 : 0;
        if (to_shop == 2) {
            release_held();
            curtain.phase = CURTAIN_NONE;
        }
    } else {
        draw_curtain(renderer, curtain_black(now));
    }
    khdays_layout_end(renderer, &fr);
    return 1;
}

void khdays_overhaul_camp_renderer_gone(void)
{
    forget();
    switch_off();
    s.tried = 0;
    s.ok = 0;
    s.layout.renderer = NULL;
    khdays_layout_renderer_gone();
}
