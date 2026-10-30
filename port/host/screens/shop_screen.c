/* The Menu Overhaul's Moogle Shop on one screen (shop_screen.h; layouts/shop/shop.txt): Re:Chain of Memories'
 * bands in brown over the DS's stripes with moogles drifting (the one-screen picture's), the munny and heart
 * points over the game's moogle turned to the menu (runtime/menus/shop.c), the menu in the player's buttons. An
 * option chosen, the DS's lower screen (the list, its dialogs) floats in under the upper screen's counter, and
 * Synthesize's recipe in the moogle's place. The game's fade over all. */
#include "shop_screen.h"

#include "layout_screen.h"
#include "ui_text.h"
#include "../app/devices.h"
#include "../app/profile.h"
#include "../render/gfx_render.h"
#include "../render/gpu_present.h"
#include "../render/onescreen.h"
#include "../render/png_io.h"
#include "../../ui/situation.h"
#include "../../runtime/menus/camp.h"
#include "../../runtime/menus/shop.h"
#include "../../runtime/core/rom.h"
#include "../../runtime/core/session_log.h"
#include "../../runtime/mods/installed.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LANGUAGES 6
static const char *const CODES[LANGUAGES] = {"", "en", "fr", "de", "it", "es"};

/* UI/shop/shp_&.s: the options' names (0-4), their descriptions (7-11), each step's line */
enum {
    SHP_ABOUT = 7,
    SHP_BUY_HOW_MANY = 13,
    SHP_SELL_HOW_MANY = 16,
    SHP_SYNTH_ASK = 19,
    SHP_SYNTH_DONE = 22,
    SHP_REDEEM_LIST = 24,
    SHP_BUY_CONFIRM = 33,
    SHP_SELL_CONFIRM = 34,
    SHP_ONLY_ONE = 35
};
static const int LIST_LINE[4] = {12, 15, 18, 24}; /* Buy's, Sell's, Synthesize's, Redeem's list */
#define WELCOME (-1)                              /* the menu's word in the right pill: the port's */

#define TURN_DEGREES 35.0f /* the moogle looks to the menu */
#define MENU_SECONDS 0.3   /* a button sliding in or out */
#define MENU_STAGGER 0.045 /* each after the one above it */
#define MENU_SLIDE 560.0f  /* from this far to the left */
#define PANEL_SECONDS 0.36 /* the DS's screen floating in or out */
#define PANEL_SLIDE 1150.0f
#define RECIPE_SECONDS 0.3 /* Synthesize's ingredients rising in */
#define RECIPE_RISE 46.0f
#define LIT_RATE 16.0 /* the chosen button's shift: e^-rate of the way left a second */
#define GLOVE_RATE 30.0
#define WORD_SECONDS 0.18   /* a pill's words changing: the old fading out, the new in */
#define ROLL_SECONDS 0.55   /* a sum changing: its digits rolling to it */
#define CHANGE_SECONDS 0.15 /* a pending price showing or going */
#define FRESH_SECONDS 0.5   /* not drawn longer than this: the shop opening, everything set at once */
#define SHADOW_ALPHA 0.4f   /* the DS's screens' shadow */
#define SHADOW_OFFSET 12.0f
#define RECIPE_SHADOW 10.0f /* the recipe's shadow, down and to the right */
#define SETTINGS_GEAR 0.62f /* the settings key (the camp's) */
#define SETTINGS_KEY 0.6f
#define FIREFLY_GLOVE 91.0f /* KH2's firefly as large beside the glove as KH2's beside its own */
#define TEXT_MOST 256
static const Uint8 BLACK[3] = {0, 0, 0};
static const Uint8 MINUS[3] = {255, 120, 120}, PLUS[3] = {150, 240, 150};

static double seconds_now(void)
{
    return (double)SDL_GetTicksNS() / 1e9;
}

static float clamp01(double t)
{
    return (float)(t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t);
}

static float smooth(float t)
{
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

static float ease_out(float t)
{
    t = 1.0f - clamp01(t);
    return 1.0f - t * t * t;
}

/* ---- The file ----------------------------------------------------------------------------------- */

/* The Menu Overhaul's layouts/shop/shop.txt (runtime/mods/installed.h): without that mod, none (the game's two
 * screens shown, the moogle facing the player). */
static int find_file(char *path, int size, char *folder, int folder_size)
{
    char *slash;
    if (!khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) || !khdays_mods_find("layouts/shop/shop.txt", path, size)) {
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

static int each_line(const char *path, void (*line)(char *text, const char *folder), const char *folder)
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
        line(l, folder);
    }
    SDL_free(text);
    return 1;
}

/* ---- The picture under the moogle (the one-screen picture's: read without the renderer) ---------- */

static struct {
    int tried, ok;
    uint8_t *atlas; /* kept while the port runs: the picture's card keeps it by its address */
    int w, h;
    char path[1300]; /* its file (the screen's own copy for the transitions: hand_backdrop) */
    float stripes[4];
    uint32_t ground;
    float icon[4], icon_h, icon_alpha, step[2], speed[2];
    float sheet[4]; /* the pattern laid out once (pattern_sheet): one piece, else a piece a moogle */
    float floor[4], floor_alpha, floor_place[4];
    float block[4], block_place[4]; /* the player's block, under the moogle (its words and sums the screen's) */
    float source[4], view[4], window[4];
} plan;

static KhdaysPiece *pieces; /* grown to the most a moment asks (pieces_room) */
static int pieces_room;

/* Room for `n` pieces in `pieces`; 0 when it could not grow. */
static int pieces_for(int n)
{
    if (n > pieces_room) {
        KhdaysPiece *more = (KhdaysPiece *)realloc(pieces, (size_t)n * sizeof(*more));
        if (more == NULL) {
            return 0;
        }
        pieces = more;
        pieces_room = n;
    }
    return 1;
}

static void plan_line(char *line, const char *folder)
{
    float a, b, c, d, e, f, g, h, i, j;
    unsigned r, gr, bl;
    char file[256];
    if (SDL_sscanf(line, "backdrop %255s %f %f %f %f", file, &a, &b, &c, &d) == 5 && plan.atlas == NULL) {
        char path[1300];
        const char *error = NULL;
        snprintf(path, sizeof(path), "%s%s", folder, file);
        snprintf(plan.path, sizeof(plan.path), "%s", path);
        plan.atlas = khdays_png_read(path, &plan.w, &plan.h, &error);
        if (plan.atlas == NULL) {
            SDL_Log("moogle shop: %s: %s", path, error != NULL ? error : "not read");
        }
        plan.stripes[0] = a, plan.stripes[1] = b, plan.stripes[2] = c, plan.stripes[3] = d;
    } else if (SDL_sscanf(line, "ground %u %u %u", &r, &gr, &bl) == 3) {
        plan.ground = (r & 255u) | (gr & 255u) << 8 | (bl & 255u) << 16;
    } else if (SDL_sscanf(line, "pattern %f %f %f %f %f %f %f %f %f %f", &a, &b, &c, &d, &e, &f, &g, &h, &i, &j) ==
               10) {
        plan.icon[0] = a, plan.icon[1] = b, plan.icon[2] = c, plan.icon[3] = d;
        plan.icon_h = e, plan.icon_alpha = f, plan.step[0] = g, plan.step[1] = h, plan.speed[0] = i, plan.speed[1] = j;
    } else if (SDL_sscanf(line, "pattern_sheet %f %f %f %f", &a, &b, &c, &d) == 4) {
        plan.sheet[0] = a, plan.sheet[1] = b, plan.sheet[2] = c, plan.sheet[3] = d;
    } else if (SDL_sscanf(line, "floor %f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
        plan.floor[0] = a, plan.floor[1] = b, plan.floor[2] = c, plan.floor[3] = d, plan.floor_alpha = e;
    } else if (SDL_sscanf(line, "block_backdrop %f %f %f %f", &a, &b, &c, &d) == 4) {
        plan.block[0] = a, plan.block[1] = b, plan.block[2] = c, plan.block[3] = d;
    } else if (SDL_sscanf(line, "place block %f %f %f %f", &a, &b, &c, &d) == 4) {
        plan.block_place[0] = a, plan.block_place[1] = b, plan.block_place[2] = c, plan.block_place[3] = d;
    } else if (SDL_sscanf(line, "place floor %f %f %f %f", &a, &b, &c, &d) == 4) {
        plan.floor_place[0] = a, plan.floor_place[1] = b, plan.floor_place[2] = c, plan.floor_place[3] = d;
    } else if (SDL_sscanf(line, "model_source %f %f %f %f", &a, &b, &c, &d) == 4) {
        plan.source[0] = a, plan.source[1] = b, plan.source[2] = c, plan.source[3] = d;
    } else if (SDL_sscanf(line, "model_view %f %f %f %f", &a, &b, &c, &d) == 4) {
        plan.view[0] = a, plan.view[1] = b, plan.view[2] = c, plan.view[3] = d;
    } else if (SDL_sscanf(line, "model %f %f %f %f", &a, &b, &c, &d) == 4) {
        plan.window[0] = a, plan.window[1] = b, plan.window[2] = c, plan.window[3] = d;
    }
}

static int plan_ready(void)
{
    if (!plan.tried) {
        char path[1024], folder[1024];
        plan.tried = 1;
        if (find_file(path, (int)sizeof(path), folder, (int)sizeof(folder)) && each_line(path, plan_line, folder)) {
            plan.ok = plan.atlas != NULL && plan.stripes[2] > 0 && plan.icon[2] > 0 && plan.icon[3] > 0 &&
                      plan.icon_h > 0 && plan.step[0] > 0 && plan.step[1] > 0 && plan.source[2] > 0 &&
                      plan.source[3] > 0 && plan.window[2] > 0 && plan.window[3] > 0;
            if (plan.view[2] <= 0 || plan.view[3] <= 0) {
                memcpy(plan.view, plan.source, sizeof(plan.view));
            }
            if (!plan.ok) {
                SDL_Log("moogle shop: %s is incomplete (the game's two screens shown)", path);
            }
        }
    }
    return plan.ok;
}

/* A piece of the picture: its rectangle (layout units), its texels in the atlas, its alpha. */
static void piece_at(KhdaysPiece *p, float x, float y, float w, float h, const float texels[4], float alpha)
{
    memset(p, 0, sizeof(*p));
    p->rect[0] = x;
    p->rect[1] = y;
    p->rect[2] = x + w;
    p->rect[3] = y + h;
    memcpy(p->texels, texels, sizeof(p->texels));
    for (int c = 0; c < 4; ++c) {
        p->corners[4 * c] = p->corners[4 * c + 1] = p->corners[4 * c + 2] = 1.0f;
        p->corners[4 * c + 3] = alpha;
    }
    p->blend = KHDAYS_PIECE_OVER;
}

/* The pattern's sheet's texels shown at this moment over the whole picture (a texel a layout unit): the
 * grid repeats every step across and every two down (every other row half a step over), so the picture
 * shows the sheet from a step and two in, less the drift; 0 when shop.txt has no sheet (or too small). */
static int sheet_texels(double now, float out[4])
{
    const float sx = plan.step[0], sy = plan.step[1];
    if (plan.sheet[2] < KHDAYS_LAYOUT_W + sx || plan.sheet[3] < KHDAYS_LAYOUT_H + 2.0f * sy ||
        plan.sheet[0] + plan.sheet[2] > (float)plan.w || plan.sheet[1] + plan.sheet[3] > (float)plan.h) {
        return 0;
    }
    out[0] = plan.sheet[0] + sx - (float)fmod(now * plan.speed[0], (double)sx);
    out[1] = plan.sheet[1] + 2.0f * sy - (float)fmod(now * plan.speed[1], 2.0 * sy);
    out[2] = KHDAYS_LAYOUT_W;
    out[3] = KHDAYS_LAYOUT_H;
    return 1;
}

/* This moment's pieces: the stripes, the moogles of the pattern (a grid, every other row half a step
 * over, drifting down and to the right: its sheet as one piece, the graphics card testing every piece at
 * every texel of the picture), the moogle's shadow (`floor` its alpha), the player's block (the moogle
 * over it). */
static int make_pieces(double now, float floor)
{
    const float sx = plan.step[0], sy = plan.step[1];
    const float ih = plan.icon_h, iw = plan.icon_h * plan.icon[2] / plan.icon[3];
    const float ox = (float)fmod(now * plan.speed[0], (double)sx), oy = (float)fmod(now * plan.speed[1], 2.0 * sy);
    float sheet[4];
    const int one = sheet_texels(now, sheet);
    const int rows = (int)(KHDAYS_LAYOUT_H / sy) + 5, cols = (int)(KHDAYS_LAYOUT_W / sx) + 5;
    int n = 0;
    /* the stripes, the sheet or a piece a moogle of the grid, the shadow, the block */
    if (!pieces_for(4 + (one ? 0 : rows * cols))) {
        return 0;
    }
    piece_at(&pieces[n++], 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H, plan.stripes, 1.0f);
    if (one) {
        piece_at(&pieces[n++], 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H, sheet, plan.icon_alpha);
    }
    for (int row = -2; !one && row <= (int)(KHDAYS_LAYOUT_H / sy) + 2; ++row) {
        for (int col = -2; col <= (int)(KHDAYS_LAYOUT_W / sx) + 2; ++col) {
            const float x = (float)col * sx + ox + ((row & 1) ? sx / 2 : 0.0f), y = (float)row * sy + oy;
            if (x + iw <= 0 || x >= KHDAYS_LAYOUT_W || y + ih <= 0 || y >= KHDAYS_LAYOUT_H) {
                continue;
            }
            piece_at(&pieces[n++], x, y, iw, ih, plan.icon, plan.icon_alpha);
        }
    }
    if (floor > 0.002f && plan.floor[2] > 0 && plan.floor_place[2] > 0) {
        piece_at(&pieces[n++], plan.floor_place[0], plan.floor_place[1], plan.floor_place[2], plan.floor_place[3],
                 plan.floor, plan.floor_alpha * floor);
    }
    if (plan.block[2] > 0 && plan.block_place[2] > 0) {
        piece_at(&pieces[n++], plan.block_place[0], plan.block_place[1], plan.block_place[2], plan.block_place[3],
                 plan.block, 1.0f);
    }
    return n;
}

/* ---- The window's pieces ------------------------------------------------------------------------ */

enum { SET_MUNNY, SET_HEARTS, SET_CHANGE, SETS };
static const char SET_NAMES[SETS] = {'m', 'h', 'c'};

static struct {
    int tried, ok;
    int failed; /* its pieces could not be read: the game's own screens (the model says no) */
    int language;
    KhdaysLayout layout;
    KhdaysLayoutStrings shp;
    float pad[SETS], ink[SETS], advance[SETS][12]; /* KHGummi's digits: 0-9, then + and - */
    Uint8 line[3], menu_text[3], menu_text_lit[3], section_text[3], message_text[3];
    unsigned short welcome[LANGUAGES][32];
    /* the DS's screens as the one-screen picture handed them over, and their textures */
    uint32_t upper[256 * 192], lower[256 * 192];
    unsigned serial, uploaded;
    SDL_Texture *upper_texture, *lower_texture;
    /* each DS screen without its stripes, transparent around (0xAARRGGBB): Synthesize's recipe, and with
     * `alone` (shop.txt's screens_alone, 1 the default) the list and the counter too, none in a frame */
    uint32_t cut[2][256 * 192];
    int cut_ok[2];
    unsigned cut_serial[2], cut_uploaded[2];
    SDL_Texture *cut_texture[2];
    int alone;
    /* the screens composed on the card instead, with the HD tiles and fonts (gfx_render.h khdays_gfx_render_shop):
     * their texels a DS dot as large as the page shows them; each one without its stripes, how much each texel
     * covers (cover_alone), and the card's composition it was made from */
    int dots;
    SDL_Texture *cover[2];
    int cover_w[2];
    unsigned cover_made[2];
    unsigned drawn_card, told_card; /* the screens drawn from the card this picture (a bit each), the last told */
    SDL_Texture *backdrop;          /* the picture's atlas, for the transitions (hand_backdrop) */
} sh;

static int applied;

static void colour(const char *line, const char *key, Uint8 out[3])
{
    char name[48] = "";
    unsigned r, g, b;
    if (SDL_sscanf(line, "colour %47s %u %u %u", name, &r, &g, &b) == 4 && strcmp(name, key) == 0) {
        out[0] = (Uint8)r, out[1] = (Uint8)g, out[2] = (Uint8)b;
    }
}

static int set_of(char c)
{
    for (int s = 0; s < SETS; ++s) {
        if (SET_NAMES[s] == c) {
            return s;
        }
    }
    return -1;
}

static int digit_index(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c == '+' ? 10 : c == '-' ? 11 : -1;
}

static void screen_line(char *line, const char *folder)
{
    char set, c;
    float a, b, d;
    if (khdays_layout_line(&sh.layout, line, folder)) {
        return;
    }
    if (SDL_sscanf(line, "digits %c %f %f %f", &set, &a, &b, &d) == 4 && set_of(set) >= 0) {
        sh.pad[set_of(set)] = a;
        sh.ink[set_of(set)] = b;
    } else if (SDL_sscanf(line, "digit %c %c %f", &set, &c, &a) == 3 && set_of(set) >= 0 && digit_index(c) >= 0) {
        sh.advance[set_of(set)][digit_index(c)] = a;
    } else if (SDL_sscanf(line, "screens_alone %f", &a) == 1) {
        sh.alone = a != 0.0f;
    } else if (strncmp(line, "colour ", 7) == 0) {
        colour(line, "line", sh.line);
        colour(line, "menu_text", sh.menu_text);
        colour(line, "menu_text_lit", sh.menu_text_lit);
        colour(line, "section_text", sh.section_text);
        colour(line, "message_text", sh.message_text);
    }
}

static void forget(void)
{
    if (sh.upper_texture != NULL) {
        SDL_DestroyTexture(sh.upper_texture);
    }
    if (sh.lower_texture != NULL) {
        SDL_DestroyTexture(sh.lower_texture);
    }
    for (int c = 0; c < 2; ++c) {
        if (sh.cut_texture[c] != NULL) {
            SDL_DestroyTexture(sh.cut_texture[c]);
        }
        sh.cut_texture[c] = NULL;
        sh.cut_uploaded[c] = sh.cut_serial[c] - 1u;
        if (sh.cover[c] != NULL) {
            SDL_DestroyTexture(sh.cover[c]);
        }
        sh.cover[c] = NULL;
        sh.cover_made[c] = 0;
    }
    if (sh.backdrop != NULL) {
        SDL_DestroyTexture(sh.backdrop);
    }
    sh.upper_texture = sh.lower_texture = sh.backdrop = NULL;
    sh.uploaded = sh.serial - 1u;
    khdays_layout_forget(&sh.layout);
    khdays_layout_strings_free(&sh.shp);
    sh.language = 0;
    sh.ok = 0;
}

static int load(SDL_Renderer *renderer)
{
    static const char *const needed[] = {
        "band_top",       "band_bottom",   "pill_left", "pill_middle", "pill_right", "message_left",
        "message_middle", "message_right", "block",     "button",      "button_lit", "glove",
        "label_munny",    "label_hearts",  "m0",        "h0",          "c0",         "cminus",
        "cplus",          "title_en"};
    static const char *const places[] = {
        "band_top", "band_bottom",   "title",        "section",     "message",      "message_text",
        "block",    "label_munny",   "label_hearts", "munny_value", "hearts_value", "change",
        "menu",     "menu_text",     "glove",        "panel",       "header",       "header_source",
        "recipe",   "recipe_source", "screen_edge",  "float"};
    char path[1024], folder[1024];
    forget();
    sh.alone = 1;
    sh.layout.who = "moogle shop";
    sh.layout.renderer = renderer;
    if (!find_file(path, (int)sizeof(path), folder, (int)sizeof(folder))) {
        SDL_Log("moogle shop: no layouts/shop/shop.txt (the game's two screens shown)");
        return 0;
    }
    each_line(path, screen_line, folder);
    sh.ok = khdays_layout_ready(&sh.layout);
    for (size_t i = 0; sh.ok && i < sizeof(needed) / sizeof(needed[0]); ++i) {
        sh.ok = khdays_layout_piece(&sh.layout, needed[i]) != NULL;
    }
    for (size_t i = 0; sh.ok && i < sizeof(places) / sizeof(places[0]); ++i) {
        sh.ok = khdays_layout_place(&sh.layout, places[i]) != NULL;
    }
    for (int s = 0; sh.ok && s < SETS; ++s) {
        sh.ok = sh.ink[s] > 0;
    }
    if (!sh.ok) {
        SDL_Log("moogle shop: %s is incomplete (the game's two screens shown)", path);
        khdays_layout_forget(&sh.layout);
        return 0;
    }
    for (int l = 1; l < LANGUAGES; ++l) {
        const char *w = khdays_text_in(TXT_SHOP_WELCOME, l);
        khdays_layout_utf8(w != NULL ? w : "Welcome!", sh.welcome[l], 32);
    }
    if (plan_ready()) {
        int w, h;
        sh.backdrop = khdays_layout_texture(&sh.layout, plan.path, 0, &w, &h);
    }
    return 1;
}

/* The shop's strings in the game's language (the window's thread reads the ROM itself). */
static void strings_for(int language)
{
    static KhdaysRomReader *reader;
    char table[64];
    if (language == sh.language || language < 1 || language >= LANGUAGES) {
        return;
    }
    if (reader == NULL) {
        reader = khdays_rom_reader_new();
    }
    khdays_layout_strings_free(&sh.shp);
    snprintf(table, sizeof(table), "UI/shop/shp_%s.s.z", CODES[language]);
    khdays_layout_strings(reader, table, &sh.shp, sh.layout.who);
    sh.language = language;
}

static const float *place(const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&sh.layout, name);
    return p != NULL ? &p->x : none;
}

/* ---- What the window's thread hands over -------------------------------------------------------- */

static int list_step(int step)
{
    return step == KHDAYS_SHOP_LIST || step == KHDAYS_SHOP_QUANTITY || step == KHDAYS_SHOP_SYNTHESIS ||
           step == KHDAYS_SHOP_REWARD;
}

int khdays_shop_screen_model(KhdaysShopModel *out)
{
    KhdaysShopView v;
    static float floor = 1.0f;
    static double floor_at;
    double now;
    khdays_shop_view(&v);
    if (!v.shown || sh.failed || !plan_ready()) {
        return 0;
    }
    now = seconds_now();
    {
        /* the moogle's shadow goes with the moogle (Synthesize's recipe in its place) */
        const float dt = floor_at > 0 ? (float)(now - floor_at) : 0.0f;
        const float target = v.recipe ? 0.0f : 1.0f;
        floor_at = now;
        floor += (target - floor) * clamp01(dt / RECIPE_SECONDS * 2.0);
        if (fabsf(target - floor) < 0.01f || dt > 0.5f) {
            floor = target;
        }
    }
    memcpy(out->source, plan.source, sizeof(out->source));
    memcpy(out->view, plan.view, sizeof(out->view));
    memcpy(out->window, plan.window, sizeof(out->window));
    out->atlas = plan.atlas;
    out->atlas_w = plan.w;
    out->atlas_h = plan.h;
    out->ground = plan.ground;
    out->piece_count = make_pieces(now, floor);
    out->pieces = pieces;
    return 1;
}

void khdays_shop_screen_applied(int yes)
{
    applied = yes;
}

int khdays_shop_screen_shown(void)
{
    return applied;
}

void khdays_shop_screen_wanted(int one_screen)
{
    /* set before the shop opens (the game's thread turns the moogle from the shop's first step on: turned
     * only once the screen took the picture, it showed facing the player for a few frames first) */
    static int turned = -1;
    const int turn = one_screen && !sh.failed && plan_ready();
    if (turn != turned) {
        turned = turn;
        khdays_shop_set_turn(turn ? TURN_DEGREES : 0.0f);
    }
}

int khdays_shop_screen_request(KhdaysShopRequest *out)
{
    static int list_was, tab_was = -1, recipe_was;
    static unsigned generation;
    KhdaysShopView v;
    const KhdaysLayoutPlace *header, *recipe;
    khdays_shop_view(&v);
    /* only a list's: going back to the menu, the screens float out as they were (not the game's menu
     * coming back under its fade) */
    if (!sh.ok || !v.shown || !list_step(v.step) ||
        (header = khdays_layout_place(&sh.layout, "header_source")) == NULL ||
        (recipe = khdays_layout_place(&sh.layout, "recipe_source")) == NULL) {
        list_was = 0;
        return 0;
    }
    if (!list_was || v.tab != tab_was || v.recipe != recipe_was) {
        ++generation; /* another list: its stripes' layer found again */
    }
    list_was = 1;
    tab_was = v.tab;
    recipe_was = v.recipe;
    memset(out, 0, sizeof(*out));
    out->alone = sh.alone;
    out->recipe = v.recipe;
    out->tab = v.tab;
    out->header[0] = header->x, out->header[1] = header->y, out->header[2] = header->w, out->header[3] = header->h;
    out->recipe_rect[0] = recipe->x, out->recipe_rect[1] = recipe->y;
    out->recipe_rect[2] = recipe->w, out->recipe_rect[3] = recipe->h;
    out->generation = generation;
    return 1;
}

void khdays_shop_screen_card(unsigned wants[2], int fade[2], int *scale)
{
    KhdaysShopView v;
    wants[0] = wants[1] = 0u;
    fade[0] = 0;
    fade[1] = 1; /* the lower's list coming and going, as onescreen.c's thread composes it */
    *scale = sh.dots;
    khdays_shop_view(&v);
    if (!sh.ok || !v.shown || !list_step(v.step)) {
        return;
    }
    /* as draw_screens takes them: the counter and the list alone when they can be, the recipe alone when it can */
    if (sh.cut_ok[0] && (sh.alone || v.recipe)) {
        wants[0] |= KHDAYS_SHOP_WANT_CUT;
    }
    if (!sh.alone || !sh.cut_ok[0]) {
        wants[0] |= KHDAYS_SHOP_WANT_NORMAL;
    }
    wants[1] = sh.alone && sh.cut_ok[1] ? KHDAYS_SHOP_WANT_CUT : KHDAYS_SHOP_WANT_NORMAL;
}

void khdays_shop_screen_results(const uint32_t *upper, const uint32_t *lower, const uint32_t *upper_cut,
                                const uint32_t *lower_cut)
{
    const uint32_t *cut[2] = {upper_cut, lower_cut};
    if (upper != NULL) {
        memcpy(sh.upper, upper, sizeof(sh.upper));
    }
    if (lower != NULL) {
        memcpy(sh.lower, lower, sizeof(sh.lower));
    }
    if (upper != NULL || lower != NULL) {
        ++sh.serial;
    }
    for (int s = 0; s < 2; ++s) {
        sh.cut_ok[s] = cut[s] != NULL;
        if (cut[s] != NULL) {
            memcpy(sh.cut[s], cut[s], sizeof(sh.cut[s]));
            ++sh.cut_serial[s];
        }
    }
}

/* ---- Its motion --------------------------------------------------------------------------------- */

typedef struct Word {
    int now, was; /* a string's record (WELCOME: the port's word), -2 none */
    double at;
} Word;

typedef struct Sum {
    uint32_t from, to;
    double at;
    int32_t change; /* the pending price shown (kept while it fades out) */
    float shown;    /* its alpha */
} Sum;

static struct {
    double drawn_at;
    int menu_on, panel_on;
    double menu_at;      /* the menu's way changed */
    float menu[5];       /* each row's way in (0 out, 1 in) */
    float panel, recipe; /* the same, the DS's screen's and the ingredients' */
    float lit[5];        /* each row's shift when chosen */
    float glove;         /* the glove's row */
    Word section, message;
    Sum munny, hearts;
} mo;

static void word_to(Word *w, int record, double now, int fresh)
{
    if (fresh) {
        w->now = record;
        w->was = -2;
        w->at = now - WORD_SECONDS;
    } else if (record != w->now) {
        w->was = w->now;
        w->now = record;
        w->at = now;
    }
}

static void sum_to(Sum *s, uint32_t value, int32_t change, double now, float dt, int fresh)
{
    if (fresh) {
        s->from = s->to = value;
        s->at = now - ROLL_SECONDS;
        s->change = change;
        s->shown = change != 0 ? 1.0f : 0.0f;
        return;
    }
    if (value != s->to) {
        const float t = ease_out((float)((now - s->at) / ROLL_SECONDS));
        s->from = (uint32_t)lround((double)s->from + ((double)s->to - (double)s->from) * t);
        s->to = value;
        s->at = now;
    }
    if (change != 0) {
        s->change = change;
    }
    s->shown += (change != 0 ? 1.0f : -1.0f) * dt / (float)CHANGE_SECONDS;
    s->shown = clamp01(s->shown);
}

static uint32_t sum_now(const Sum *s, double now)
{
    const float t = ease_out((float)((now - s->at) / ROLL_SECONDS));
    return (uint32_t)lround((double)s->from + ((double)s->to - (double)s->from) * t);
}

/* The menu's rows: the options shown, in order; the chosen one's row (-1 none). */
static int menu_rows(const KhdaysShopView *v, int rows[5], int *chosen)
{
    int n = 0;
    *chosen = -1;
    for (int o = 0; o < 5; ++o) {
        if (v->options[o]) {
            if (o == v->cursor) {
                *chosen = n;
            }
            rows[n++] = o;
        }
    }
    return n;
}

static float toward(float value, float target, float step)
{
    return value < target ? (value + step < target ? value + step : target)
                          : (value - step > target ? value - step : target);
}

static void motion(const KhdaysShopView *v, double now)
{
    const int fresh = now - mo.drawn_at > FRESH_SECONDS;
    const float dt = fresh ? 0.0f : (float)(now - mo.drawn_at);
    const int menu_on = v->step == KHDAYS_SHOP_MENU || v->step == KHDAYS_SHOP_CLOSING;
    const int panel_on = list_step(v->step);
    int rows[5], chosen;
    const int count = menu_rows(v, rows, &chosen);
    int section, message;
    if (fresh) {
        memset(mo.menu, 0, sizeof(mo.menu));
        mo.panel = mo.recipe = 0;
        mo.menu_on = 0;
        mo.menu_at = now;
        for (int i = 0; i < 5; ++i) {
            mo.lit[i] = i == chosen ? 1.0f : 0.0f;
        }
        mo.glove = chosen >= 0 ? (float)chosen : 0.0f;
    }
    if (menu_on != mo.menu_on) {
        mo.menu_on = menu_on;
        mo.menu_at = now;
    }
    for (int i = 0; i < 5; ++i) {
        /* in: the top first; out: all together, quicker */
        const double delay = menu_on ? MENU_STAGGER * i : 0.0;
        if (now - mo.menu_at >= delay) {
            mo.menu[i] =
                toward(mo.menu[i], menu_on ? 1.0f : 0.0f, dt / (float)(menu_on ? MENU_SECONDS : MENU_SECONDS * 0.7));
        }
        mo.lit[i] += ((i == chosen ? 1.0f : 0.0f) - mo.lit[i]) * (1.0f - expf(-(float)LIT_RATE * dt));
    }
    if (chosen >= 0) {
        mo.glove += ((float)chosen - mo.glove) * (1.0f - expf(-(float)GLOVE_RATE * dt));
    }
    mo.panel_on = panel_on;
    mo.panel = toward(mo.panel, panel_on ? 1.0f : 0.0f, dt / (float)PANEL_SECONDS);
    mo.recipe = toward(mo.recipe, panel_on && v->recipe ? 1.0f : 0.0f, dt / (float)RECIPE_SECONDS);
    (void)count;
    /* the right pill's word, the bottom one's line */
    switch (v->step) {
    case KHDAYS_SHOP_OPENING:
    case KHDAYS_SHOP_MENU:
    case KHDAYS_SHOP_CLOSING:
        section = WELCOME;
        message = SHP_ABOUT + (v->cursor >= 0 && v->cursor < 5 ? v->cursor : 0);
        break;
    case KHDAYS_SHOP_TO_LIST: /* the tab not yet taken from the cursor */
        section = v->cursor >= 0 && v->cursor < 4 ? v->cursor : mo.section.now;
        message = v->cursor >= 0 && v->cursor < 4 ? LIST_LINE[v->cursor] : mo.message.now;
        break;
    case KHDAYS_SHOP_QUANTITY:
        section = v->tab >= 0 && v->tab < 4 ? v->tab : mo.section.now;
        message = v->quantity_stage == 0 ? (v->tab == KHDAYS_SHOP_SELL ? SHP_SELL_HOW_MANY : SHP_BUY_HOW_MANY)
                                         : (v->tab == KHDAYS_SHOP_SELL ? SHP_SELL_CONFIRM : SHP_BUY_CONFIRM);
        break;
    case KHDAYS_SHOP_SYNTHESIS:
        section = v->tab >= 0 && v->tab < 4 ? v->tab : mo.section.now;
        message = v->synthesis_stage == 1 ? SHP_SYNTH_DONE : v->synthesis_stage == 2 ? SHP_ONLY_ONE : SHP_SYNTH_ASK;
        break;
    case KHDAYS_SHOP_REWARD: /* Redeem's reward shown (the list's line for another tab) */
        section = v->tab >= 0 && v->tab < 4 ? v->tab : mo.section.now;
        message = v->tab >= 0 && v->tab < 4 ? LIST_LINE[v->tab] : SHP_REDEEM_LIST;
        break;
    default: /* a list, or going back from it */
        section = v->tab >= 0 && v->tab < 4 ? v->tab : mo.section.now;
        message = v->tab >= 0 && v->tab < 4 ? LIST_LINE[v->tab] : mo.message.now;
        break;
    }
    word_to(&mo.section, section, now, fresh);
    word_to(&mo.message, message, now, fresh);
    sum_to(&mo.munny, v->munny, v->munny_change, now, dt, fresh);
    sum_to(&mo.hearts, v->hearts, v->hearts_change, now, dt, fresh);
    mo.drawn_at = now;
}

/* ---- Drawing ------------------------------------------------------------------------------------ */

static void draw(const KhdaysLayoutFrame *fr, const char *name, float x, float y, float w, float h, float alpha)
{
    khdays_layout_draw_piece(&sh.layout, fr, name, x, y, w, h, alpha);
}

/* A pill as wide as asked: its caps (as wide as they are high) and its middle stretched. */
static void pill(const KhdaysLayoutFrame *fr, const char *prefix, float x, float y, float w, float h, float alpha)
{
    char left[24], middle[24], right[24];
    const KhdaysLayoutPiece *cap;
    float cw;
    snprintf(left, sizeof(left), "%s_left", prefix);
    snprintf(middle, sizeof(middle), "%s_middle", prefix);
    snprintf(right, sizeof(right), "%s_right", prefix);
    cap = khdays_layout_piece(&sh.layout, left);
    if (cap == NULL || cap->h <= 0) {
        return;
    }
    cw = cap->w * h / cap->h;
    draw(fr, left, x, y, cw, h, alpha);
    draw(fr, middle, x + cw, y, w - 2 * cw, h, alpha);
    draw(fr, right, x + w - cw, y, cw, h, alpha);
}

/* The string's text as one line (its line breaks spaces). */
static void one_line(const unsigned short *in, unsigned short *out, int room)
{
    int n = 0;
    for (; in != NULL && *in != 0 && n < room - 1; ++in) {
        out[n++] = *in == '\n' ? ' ' : *in;
    }
    out[n] = 0;
}

static const unsigned short *word_text(int record, int language, unsigned short *out, int room)
{
    if (record == WELCOME) {
        return sh.welcome[language >= 1 && language < LANGUAGES ? language : 1];
    }
    if (record < 0) {
        return NULL;
    }
    one_line(khdays_layout_string(&sh.shp, record), out, room);
    return out;
}

/* A line of KHMenu, its capitals `cap` high, its shadow first; its width. */
static float line_size(const unsigned short *t, float cap)
{
    const float ink = sh.layout.ink_bottom - sh.layout.ink_top;
    return t != NULL && ink > 0 ? khdays_layout_line_width(&sh.layout, t, cap / ink) : 0.0f;
}

static void line(const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y, float cap, const Uint8 rgb[3],
                 float alpha)
{
    const float ink = sh.layout.ink_bottom - sh.layout.ink_top;
    if (t == NULL || t[0] == 0 || ink <= 0 || alpha <= 0.002f) {
        return;
    }
    khdays_layout_draw_line_rgb(&sh.layout, fr, t, x, y, cap / ink, rgb, alpha, 1);
    khdays_layout_draw_line_rgb(&sh.layout, fr, t, x, y, cap / ink, rgb, alpha, 0);
}

/* KHGummi's digits of a set (and + and -), their pen ending at `right`, their ink `h` high from `top`;
 * their width. */
static float gummi(const KhdaysLayoutFrame *fr, int set, const char *text, float right, float top, float h,
                   const Uint8 *tint, float alpha, int drawn)
{
    const float k = h / sh.ink[set];
    float w = 0, x;
    for (const char *c = text; *c != 0; ++c) {
        if (digit_index(*c) >= 0) {
            w += sh.advance[set][digit_index(*c)] * k;
        }
    }
    if (!drawn || alpha <= 0.002f) {
        return w;
    }
    x = right - w;
    for (const char *c = text; *c != 0; ++c) {
        char name[16], digit[2] = {*c, 0};
        const KhdaysLayoutPiece *p;
        if (digit_index(*c) < 0) {
            continue;
        }
        snprintf(name, sizeof(name), "%c%s", SET_NAMES[set], *c == '+' ? "plus" : *c == '-' ? "minus" : digit);
        p = khdays_layout_piece(&sh.layout, name);
        if (p != NULL) {
            const float px = x - sh.pad[set] * k, py = top - sh.pad[set] * k;
            if (tint != NULL) {
                khdays_layout_draw_tinted(&sh.layout, fr, name, px, py, p->w * k, p->h * k, tint, alpha);
            } else {
                draw(fr, name, px, py, p->w * k, p->h * k, alpha);
            }
        }
        x += sh.advance[set][digit_index(*c)] * k;
    }
    return w;
}

static void draw_bands(const KhdaysLayoutFrame *fr, const KhdaysShopView *v, double now)
{
    const float *top = place("band_top"), *bottom = place("band_bottom"), *title = place("title");
    const float *section = place("section"), *section_cap = place("section_text");
    char name[24];
    const KhdaysLayoutPiece *tp;
    draw(fr, "band_top", top[0], top[1], top[2], top[3], 1.0f);
    draw(fr, "band_bottom", bottom[0], bottom[1], bottom[2], bottom[3], 1.0f);
    snprintf(name, sizeof(name), "title_%s", CODES[v->language >= 1 && v->language < LANGUAGES ? v->language : 1]);
    if ((tp = khdays_layout_piece(&sh.layout, name)) == NULL) {
        snprintf(name, sizeof(name), "title_en");
        tp = khdays_layout_piece(&sh.layout, name);
    }
    if (tp != NULL && tp->h > 0) {
        draw(fr, name, title[0], title[1], tp->w * title[3] / tp->h, title[3], 1.0f);
    }
    pill(fr, "pill", section[0], section[1], section[2], section[3], 1.0f);
    {
        /* its word: the new one fading in over the old */
        const float t = smooth((float)((now - mo.section.at) / WORD_SECONDS));
        const float cap = section_cap[0], y = section[1] + (section[3] - cap) / 2 - 2;
        unsigned short buffer[TEXT_MOST];
        const unsigned short *was = word_text(mo.section.was, v->language, buffer, TEXT_MOST);
        if (was != NULL && t < 1.0f) {
            line(fr, was, section[0] + (section[2] - line_size(was, cap)) / 2, y, cap, sh.section_text, 1.0f - t);
        }
        {
            const unsigned short *w = word_text(mo.section.now, v->language, buffer, TEXT_MOST);
            line(fr, w, section[0] + (section[2] - line_size(w, cap)) / 2, y, cap, sh.section_text,
                 was != NULL ? t : 1.0f);
        }
    }
}

static void draw_settings_key(const KhdaysLayoutFrame *fr)
{
    const float *at = place("settings");
    const KhdaysLayoutPiece *esc = khdays_layout_piece(&sh.layout, "settings_esc");
    const KhdaysLayoutPiece *gear = khdays_layout_piece(&sh.layout, "settings_gear");
    const float h = at[3] * SETTINGS_GEAR, key_h = at[3] * SETTINGS_KEY, middle = at[1] + at[3] / 2;
    float x;
    if (gear == NULL || esc == NULL || at[3] <= 0) {
        return;
    }
    x = at[0] - gear->w * h / gear->h;
    draw(fr, "settings_gear", x, middle - h / 2, gear->w * h / gear->h, h, 1.0f);
    if (khdays_devices_gamepad() != NULL && khdays_devices_pad_in_use()) {
        const float w = khdays_layout_draw_action(&sh.layout, fr, KHDAYS_ACT_OPEN_SETTINGS, 0, 0, key_h, 0.0f);
        khdays_layout_draw_action(&sh.layout, fr, KHDAYS_ACT_OPEN_SETTINGS, x - w - at[3] * 0.05f, middle - key_h / 2,
                                  key_h, 1.0f);
    } else {
        const float w = esc->w * key_h / esc->h;
        draw(fr, "settings_esc", x - w - at[3] * 0.05f, middle - key_h / 2, w, key_h, 1.0f);
    }
}

/* One of the block's sums: its value (rolling), and a pending price at its left in a smaller size. */
static void draw_sum(const KhdaysLayoutFrame *fr, int set, const Sum *s, const float *at, double now)
{
    char digits[16];
    float w;
    snprintf(digits, sizeof(digits), "%u", (unsigned)sum_now(s, now));
    w = gummi(fr, set, digits, at[0], at[1], at[3], NULL, 1.0f, 1);
    if (s->shown > 0.002f && s->change != 0) {
        const float *change = place("change");
        const float h = change[3];
        snprintf(digits, sizeof(digits), "%c%ld", s->change < 0 ? '-' : '+', labs((long)s->change));
        gummi(fr, SET_CHANGE, digits, at[0] - w - change[0], at[1] + at[3] - h, h, s->change < 0 ? MINUS : PLUS,
              smooth(s->shown), 1);
    }
}

static void draw_block(const KhdaysLayoutFrame *fr, double now)
{
    const float *block = place("block"), *ln = place("line");
    const float *ml = place("label_munny"), *hl = place("label_hearts");
    if (plan.block[2] <= 0) {
        draw(fr, "block", block[0], block[1], block[2], block[3], 1.0f); /* (else the picture's, under the moogle) */
    }
    if (ln[2] > 0) {
        khdays_layout_fill_rounded(sh.layout.renderer, fr, ln[0], ln[1], ln[2], ln[3], ln[3] / 2, sh.line, 1.0f);
    }
    draw(fr, "label_munny", ml[0], ml[1], ml[2], ml[3], 1.0f);
    draw(fr, "label_hearts", hl[0], hl[1], hl[2], hl[3], 1.0f);
    draw_sum(fr, SET_MUNNY, &mo.munny, place("munny_value"), now);
    draw_sum(fr, SET_HEARTS, &mo.hearts, place("hearts_value"), now);
}

static void draw_menu(const KhdaysLayoutFrame *fr, const KhdaysShopView *v)
{
    const float *menu = place("menu"), *lit = place("menu_lit"), *text = place("menu_text"), *glove = place("glove");
    const KhdaysLayoutPiece *button = khdays_layout_piece(&sh.layout, "button");
    int rows[5], chosen;
    const int count = menu_rows(v, rows, &chosen);
    float bh, y0;
    if (button == NULL || button->w <= 0 || count == 0) {
        return;
    }
    bh = button->h * menu[2] / button->w;
    y0 = menu[1] - ((float)(count - 1) * menu[3] + bh) / 2;
    for (int i = 0; i < count; ++i) {
        const float in = ease_out(mo.menu[i]), alpha = smooth(mo.menu[i] * 1.4f);
        const float x = menu[0] + lit[0] * mo.lit[i] - MENU_SLIDE * (1.0f - in), y = y0 + (float)i * menu[3];
        const unsigned short *word = khdays_layout_string(&sh.shp, rows[i]);
        Uint8 rgb[3];
        if (alpha <= 0.002f) {
            continue;
        }
        for (int c = 0; c < 3; ++c) {
            rgb[c] = (Uint8)lroundf(sh.menu_text[c] + (sh.menu_text_lit[c] - sh.menu_text[c]) * mo.lit[i]);
        }
        draw(fr, "button", x, y, menu[2], bh, alpha * (1.0f - mo.lit[i]));
        draw(fr, "button_lit", x, y, menu[2], bh, alpha * mo.lit[i]);
        line(fr, word, x + text[0], y + (bh - text[1]) / 2 - 1, text[1], rgb, alpha);
    }
    if (chosen >= 0) {
        /* the glove with the chosen row, gliding to it */
        const float in = ease_out(mo.menu[chosen]), alpha = smooth(mo.menu[chosen] * 1.4f);
        const float x = menu[0] + lit[0] - MENU_SLIDE * (1.0f - in), middle = y0 + mo.glove * menu[3] + bh / 2;
        if (alpha > 0.002f) {
            const float unit = KHDAYS_FIREFLY_UNIT * (glove[3] > 0 ? glove[3] / FIREFLY_GLOVE : 1.0f);
            draw(fr, "glove", x + glove[0] + khdays_layout_glove_sway(), middle + glove[1], glove[2], glove[3], alpha);
            if (mo.menu[chosen] >= 0.999f && v->step == KHDAYS_SHOP_MENU) {
                khdays_layout_firefly(sh.layout.renderer, fr, KHDAYS_FIREFLY_SHOP, x + menu[2] - 15.0f * unit,
                                      y0 + (float)chosen * menu[3] + bh / 2, unit, 1.0f);
            }
        }
    }
}

/* A DS screen's texture: its pixels sharp at any size (the renderer's pixel art scaling, else nearest). */
static SDL_Texture *screen_texture(SDL_Renderer *renderer)
{
    SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
    if (t != NULL && !SDL_SetTextureScaleMode(t, SDL_SCALEMODE_PIXELART)) {
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    }
    return t;
}

/* DS screen `screen` (0 the upper) as the card composed it last with its HD tiles and fonts (variant: as the DS
 * shows it, or without its stripes over black or white), its texels a DS dot in *k and its composition's number in
 * *made; NULL when the card has none (the CPU's then). */
static SDL_Texture *card(SDL_Renderer *renderer, int screen, int variant, float *k, unsigned *made)
{
    int scale = 1;
    SDL_Texture *t = khdays_gfx_render_shop(renderer, screen, variant, &scale, made);
    *k = (float)scale;
    return t;
}

/* A part of DS screen `screen` (src, its dots) in its black frame at `at` (layout units), its shadow under it: the
 * card's (the HD fonts), else `t`, the CPU's. */
static void floating(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, int screen, SDL_Texture *t, const float *src,
                     const float *at, float dx, float rise, float alpha)
{
    const float edge = place("screen_edge")[0];
    const float x = at[0] + dx, y = at[1] - rise, w = at[2], h = at[3];
    const float lift = place("float")[0] > 0 ? rise / place("float")[0] : 0.0f;
    float k = 1.0f;
    unsigned made;
    SDL_Texture *hd = card(renderer, screen, KHDAYS_SHOP_NORMAL, &k, &made);
    SDL_FRect from, dst;
    if (hd != NULL) {
        t = hd;
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        sh.drawn_card |= 1u << screen;
    } else {
        k = 1.0f;
    }
    from.x = src[0] * k, from.y = src[1] * k, from.w = src[2] * k, from.h = src[3] * k;
    if (alpha <= 0.002f || t == NULL) {
        return;
    }
    khdays_layout_fill_rounded(renderer, fr, x - edge + SHADOW_OFFSET, at[1] - edge + SHADOW_OFFSET, w + 2 * edge,
                               h + 2 * edge, edge * 1.5f, BLACK, alpha * SHADOW_ALPHA * (1.0f - 0.25f * lift));
    khdays_layout_fill_rounded(renderer, fr, x - edge, y - edge, w + 2 * edge, h + 2 * edge, edge, BLACK, alpha);
    dst = khdays_layout_to_window(fr, x, y, w, h);
    SDL_SetTextureAlphaModFloat(t, alpha);
    SDL_RenderTexture(renderer, t, &from, &dst);
    SDL_SetTextureAlphaModFloat(t, 1.0f);
}

/* The shadow of a part of a DS screen without its stripes: its own shape in black, three times a little apart
 * (softer), down and to the right. */
static const float ALONE_SHADOW[3][3] = {{RECIPE_SHADOW, RECIPE_SHADOW, 0.16f},
                                         {RECIPE_SHADOW * 0.6f, RECIPE_SHADOW * 0.6f, 0.16f},
                                         {RECIPE_SHADOW * 1.4f, RECIPE_SHADOW * 1.4f, 0.12f}};

/* The blendings the card's screens without their stripes are drawn with (cover_alone): what is there less the
 * texture's colour; what is there times 1 - the texture's colour. Neither changes what is there's alpha. */
static SDL_BlendMode take_blend, shade_blend;

static int blendings(void)
{
    if (take_blend == 0) {
        take_blend =
            SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                       SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        shade_blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
                                                 SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE,
                                                 SDL_BLENDOPERATION_ADD);
    }
    return take_blend != SDL_BLENDMODE_INVALID && shade_blend != SDL_BLENDMODE_INVALID;
}

/* alone() from the card's screen (the HD fonts): composed over black (B: its colours times how much they cover)
 * and over white (W), how much each texel covers is 1 - (W - B) -- the soft edges of the HD tiles too, where the
 * stripes were under them -- made in a texture of its own once for each composition (white, less W, plus B);
 * drawn, what is there is multiplied by 1 - that, then B added (the colours as premultiplied by their alpha). 0
 * when the card has none of the screen. */
static int cover_alone(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, int screen, const float *src,
                       const float *at, float dx, float rise, float alpha)
{
    float k, kw;
    unsigned made, made_white;
    SDL_Texture *black = card(renderer, screen, KHDAYS_SHOP_CUT_BLACK, &k, &made);
    SDL_Texture *white = card(renderer, screen, KHDAYS_SHOP_CUT_WHITE, &kw, &made_white);
    SDL_Texture *cover;
    SDL_FRect from, dst;
    const int w = (int)(256.0f * k), h = (int)(192.0f * k);
    if (black == NULL || white == NULL || made != made_white || k != kw || !blendings()) {
        return 0;
    }
    if (sh.cover[screen] != NULL && sh.cover_w[screen] != w) {
        SDL_DestroyTexture(sh.cover[screen]);
        sh.cover[screen] = NULL;
    }
    if (sh.cover[screen] == NULL) {
        sh.cover[screen] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (sh.cover[screen] == NULL) {
            return 0;
        }
        sh.cover_w[screen] = w;
        sh.cover_made[screen] = 0;
        SDL_SetTextureScaleMode(sh.cover[screen], SDL_SCALEMODE_LINEAR);
    }
    cover = sh.cover[screen];
    if (!SDL_SetTextureBlendMode(white, take_blend) || !SDL_SetTextureBlendMode(cover, shade_blend) ||
        !SDL_SetTextureBlendMode(black, SDL_BLENDMODE_ADD_PREMULTIPLIED)) {
        return 0; /* blendings the renderer has not: the CPU's */
    }
    if (sh.cover_made[screen] != made) {
        SDL_Texture *const was = SDL_GetRenderTarget(renderer);
        SDL_BlendMode draw_blend;
        Uint8 r, g, b, a;
        SDL_GetRenderDrawBlendMode(renderer, &draw_blend);
        SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
        if (!SDL_SetRenderTarget(renderer, cover)) {
            return 0;
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        SDL_SetTextureColorModFloat(white, 1.0f, 1.0f, 1.0f);
        SDL_RenderTexture(renderer, white, NULL, NULL);
        SDL_SetTextureColorModFloat(black, 1.0f, 1.0f, 1.0f);
        SDL_RenderTexture(renderer, black, NULL, NULL);
        SDL_SetRenderTarget(renderer, was);
        SDL_SetRenderDrawBlendMode(renderer, draw_blend);
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        sh.cover_made[screen] = made;
    }
    if (alpha <= 0.002f) {
        return 1;
    }
    from.x = src[0] * k, from.y = src[1] * k, from.w = src[2] * k, from.h = src[3] * k;
    for (int i = 0; i < 3; ++i) {
        const float s = alpha * ALONE_SHADOW[i][2];
        dst = khdays_layout_to_window(fr, at[0] + dx + ALONE_SHADOW[i][0], at[1] - rise + ALONE_SHADOW[i][1], at[2],
                                      at[3]);
        SDL_SetTextureColorModFloat(cover, s, s, s);
        SDL_RenderTexture(renderer, cover, &from, &dst);
    }
    dst = khdays_layout_to_window(fr, at[0] + dx, at[1] - rise, at[2], at[3]);
    SDL_SetTextureColorModFloat(cover, alpha, alpha, alpha);
    SDL_RenderTexture(renderer, cover, &from, &dst);
    SDL_SetTextureColorModFloat(black, alpha, alpha, alpha);
    SDL_RenderTexture(renderer, black, &from, &dst);
    SDL_SetTextureColorModFloat(black, 1.0f, 1.0f, 1.0f);
    sh.drawn_card |= 1u << screen;
    return 1;
}

/* A part of a DS screen (src, its dots) as the game draws it without the DS's stripes behind it (onescreen.c
 * composes the screen without that layer): no frame, a soft shadow down and to the right, at `at` moved by
 * (dx, -rise); the card's (cover_alone), else the CPU's; 0 when there is none of that screen. */
static int alone(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, int screen, const float *src, const float *at,
                 float dx, float rise, float alpha)
{
    const SDL_FRect from = {src[0], src[1], src[2], src[3]};
    SDL_Texture *t;
    SDL_FRect dst;
    if (!sh.cut_ok[screen] || src[2] <= 0) {
        return 0;
    }
    if (cover_alone(renderer, fr, screen, src, at, dx, rise, alpha)) {
        return 1;
    }
    if (sh.cut_texture[screen] == NULL) {
        sh.cut_texture[screen] =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
        if (sh.cut_texture[screen] == NULL) {
            return 0;
        }
        SDL_SetTextureBlendMode(sh.cut_texture[screen], SDL_BLENDMODE_BLEND);
        if (!SDL_SetTextureScaleMode(sh.cut_texture[screen], SDL_SCALEMODE_PIXELART)) {
            SDL_SetTextureScaleMode(sh.cut_texture[screen], SDL_SCALEMODE_NEAREST);
        }
        sh.cut_uploaded[screen] = sh.cut_serial[screen] - 1u;
    }
    t = sh.cut_texture[screen];
    if (sh.cut_uploaded[screen] != sh.cut_serial[screen]) {
        SDL_UpdateTexture(t, NULL, sh.cut[screen], 256 * 4);
        sh.cut_uploaded[screen] = sh.cut_serial[screen];
    }
    if (alpha <= 0.002f) {
        return 1;
    }
    /* the shadow: its own shape in black, three times a little apart (softer) */
    SDL_SetTextureColorMod(t, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        dst = khdays_layout_to_window(fr, at[0] + dx + ALONE_SHADOW[i][0], at[1] - rise + ALONE_SHADOW[i][1], at[2],
                                      at[3]);
        SDL_SetTextureAlphaModFloat(t, alpha * ALONE_SHADOW[i][2]);
        SDL_RenderTexture(renderer, t, &from, &dst);
    }
    SDL_SetTextureColorMod(t, 255, 255, 255);
    dst = khdays_layout_to_window(fr, at[0] + dx, at[1] - rise, at[2], at[3]);
    SDL_SetTextureAlphaModFloat(t, alpha);
    SDL_RenderTexture(renderer, t, &from, &dst);
    SDL_SetTextureAlphaModFloat(t, 1.0f);
    return 1;
}

static void draw_screens(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, double now)
{
    static const float whole[4] = {0, 0, 256, 192};
    const float *fl = place("float");
    const float period = fl[1] > 0 ? fl[1] : 1.0f;
    const float rise = fl[0] * (float)sin(now * 6.28318530718 / period);
    const float rise2 = fl[0] * (float)sin(now * 6.28318530718 / period + 1.7);
    {
        /* the card's screens as large as the panel shows the lower one: whole texels a DS dot, from 2 */
        const float shown = place("panel")[2] * fr->scale / 256.0f;
        const int n = (int)SDL_ceilf(shown);
        sh.dots = n < 2 ? 2 : n > KHDAYS_SHOP_SCALE ? KHDAYS_SHOP_SCALE : n;
    }
    if (mo.panel <= 0.002f && mo.recipe <= 0.002f) {
        return;
    }
    sh.drawn_card = 0u;
    if (sh.lower_texture == NULL) {
        sh.lower_texture = screen_texture(renderer);
        sh.uploaded = sh.serial - 1u;
    }
    if (sh.upper_texture == NULL) {
        sh.upper_texture = screen_texture(renderer);
        sh.uploaded = sh.serial - 1u;
    }
    if (sh.lower_texture == NULL || sh.upper_texture == NULL) {
        return;
    }
    if (sh.uploaded != sh.serial) {
        SDL_UpdateTexture(sh.lower_texture, NULL, sh.lower, 256 * 4);
        SDL_UpdateTexture(sh.upper_texture, NULL, sh.upper, 256 * 4);
        sh.uploaded = sh.serial;
    }
    {
        /* in from the left, slowing; out the same way, quicker at first */
        const float in = mo.panel_on ? ease_out(mo.panel) : smooth(mo.panel);
        const float dx = -PANEL_SLIDE * (1.0f - in), alpha = smooth(mo.panel * 1.5f);
        if (!sh.alone || !alone(renderer, fr, 0, place("header_source"), place("header"), dx, rise, alpha)) {
            floating(renderer, fr, 0, sh.upper_texture, place("header_source"), place("header"), dx, rise, alpha);
        }
        if (!sh.alone || !alone(renderer, fr, 1, whole, place("panel"), dx, rise, alpha)) {
            floating(renderer, fr, 1, sh.lower_texture, whole, place("panel"), dx, rise, alpha);
        }
    }
    if (mo.recipe > 0.002f) {
        const float in = ease_out(mo.recipe), dy = rise2 - RECIPE_RISE * (1.0f - in), alpha = smooth(mo.recipe);
        if (!alone(renderer, fr, 0, place("recipe_source"), place("recipe"), 0.0f, dy, alpha)) {
            floating(renderer, fr, 0, sh.upper_texture, place("recipe_source"), place("recipe"), 0.0f, dy, alpha);
        }
    }
    if (sh.drawn_card != sh.told_card) {
        sh.told_card = sh.drawn_card;
        khdays_log("shop: the DS's upper screen from the %s, the lower from the %s (%d texels a dot on the card)",
                   sh.drawn_card & 1u ? "card (HD fonts)" : "CPU (the DS's fonts)",
                   sh.drawn_card & 2u ? "card (HD fonts)" : "CPU (the DS's fonts)", sh.dots);
    }
}

static void draw_message(const KhdaysLayoutFrame *fr, const KhdaysShopView *v, double now)
{
    const float *pill_at = place("message"), *text = place("message_text");
    const float room = pill_at[2] - 2 * text[0];
    const float t = smooth((float)((now - mo.message.at) / WORD_SECONDS));
    unsigned short buffer[TEXT_MOST];
    pill(fr, "message", pill_at[0], pill_at[1], pill_at[2], pill_at[3], 1.0f);
    for (int which = 0; which < 2; ++which) {
        const int record = which == 0 ? mo.message.was : mo.message.now;
        const float alpha = which == 0 ? 1.0f - t : (mo.message.was != -2 ? t : 1.0f);
        const unsigned short *w;
        float cap = text[1], width;
        if ((which == 0 && t >= 1.0f) || (w = word_text(record, v->language, buffer, TEXT_MOST)) == NULL) {
            continue;
        }
        width = line_size(w, cap);
        if (width > room && width > 0) {
            cap *= room / width; /* a long line: smaller, its middle kept */
        }
        line(fr, w, pill_at[0] + text[0], pill_at[1] + (pill_at[3] - cap) / 2 - 1, cap, sh.message_text, alpha);
    }
}

/* ---- Mission Mode: the camp's menu and the shop into each other ------------------------------------
 * The game fades one to black, loads the other and fades it in; the window keeps a picture up instead
 * (khdays_shop_screen_take_over), drawn over everything until the other screen has shown at full brightness a
 * moment, then faded out. */
enum { HAND_NONE, HAND_IN, HAND_OUT };
#define HAND_STEADY_SECONDS 0.15 /* the screen coming shown whole this long before the picture fades */
#define HAND_FADE_SECONDS 0.35
#define HAND_WAIT_SECONDS 2.0 /* the shop not come so long after the camp handed over: let go */
#define HAND_LONGEST 6.0

/* The story's shop (ov026) closes back to the field through black, as the game itself goes between them: kept,
 * faded to black (FIELD_TO_BLACK_SECONDS), the black held while the game takes the field up again (its HUD's
 * pieces coming one by one) and faded off the field once it is lit and steady a moment (FIELD_STEADY_SECONDS).
 * The shop's backdrop held there over the field's ~1.7 s of loading looked like the game stuck. */
#define FIELD_TO_BLACK_SECONDS 0.25
#define FIELD_STEADY_SECONDS 0.2
#define FIELD_FADE_SECONDS 0.4
#define FIELD_LOWER_3D_MOST 1.5 /* the field's lower-3d waited for at most this long */

static struct {
    int phase;
    int story; /* HAND_OUT to the field (the story's shop), else to Mission Mode's camp menu */
    SDL_Texture *picture;
    double at;        /* taken */
    double steady_at; /* the screen coming whole since (0 not) */
    double fade_at;   /* fading since (0 not yet) */
    double field_at;  /* the story's field back since (0 not) */
} hand;

static void hand_done(void)
{
    if (hand.picture != NULL) {
        SDL_DestroyTexture(hand.picture);
    }
    hand.picture = NULL;
    hand.phase = HAND_NONE;
    hand.steady_at = hand.fade_at = hand.field_at = 0;
}

/* The window as it is now (the drawing's logical size set aside) kept for `phase`. */
static void hand_take(SDL_Renderer *renderer, int phase)
{
    SDL_Surface *got = SDL_RenderReadPixels(renderer, NULL), *opaque = NULL;
    hand_done();
    if (got != NULL) {
        opaque = SDL_ConvertSurface(got, SDL_PIXELFORMAT_XRGB8888);
        SDL_DestroySurface(got);
    }
    if (opaque != NULL) {
        hand.picture = SDL_CreateTextureFromSurface(renderer, opaque);
        SDL_DestroySurface(opaque);
    }
    if (hand.picture == NULL) {
        SDL_Log("moogle shop: the window's picture could not be kept (%s)", SDL_GetError());
        return;
    }
    SDL_SetTextureBlendMode(hand.picture, SDL_BLENDMODE_BLEND);
    hand.phase = phase;
    hand.at = seconds_now();
}

int khdays_shop_screen_ready(void)
{
    return khdays_present_one_picture() && !sh.failed && plan_ready();
}

void khdays_shop_screen_take_over(SDL_Renderer *renderer)
{
    hand_take(renderer, HAND_IN);
}

int khdays_shop_screen_handing(void)
{
    return hand.phase == HAND_IN ? KHDAYS_SHOP_HAND_IN : hand.phase == HAND_OUT ? KHDAYS_SHOP_HAND_OUT : 0;
}

int khdays_shop_screen_draw(SDL_Renderer *renderer)
{
    KhdaysShopView v;
    KhdaysLayoutFrame fr;
    const double now = seconds_now();
    int keep;
    khdays_shop_view(&v);
    if (!v.shown || !applied || !khdays_present_one_picture()) {
        return 0;
    }
    if (renderer != sh.layout.renderer || !sh.tried) {
        sh.tried = 1;
        sh.failed = !load(renderer);
    }
    if (!sh.ok) {
        return 0;
    }
    strings_for(v.language);
    motion(&v, now);
    /* closing back to the camp's menu (Mission Mode) or the field (the story): this picture kept, before the
     * game's fade darkens it */
    keep = v.step == KHDAYS_SHOP_CLOSING && hand.phase == HAND_NONE;
    khdays_layout_begin_over(renderer, &fr);
    draw_bands(&fr, &v, now);
    draw_settings_key(&fr);
    draw_block(&fr, now);
    draw_screens(renderer, &fr, now);
    draw_menu(&fr, &v);
    draw_message(&fr, &v, now);
    if (keep) {
        hand_take(renderer, HAND_OUT);
        hand.story = !v.mission;
    } else {
        khdays_layout_shade(renderer, v.master_upper);
    }
    khdays_layout_end(renderer, &fr);
    return 1;
}

/* The shop's backdrop as the picture under the moogle has it -- the stripes, the moogles drifting -- with its
 * bands and title, drawn by the screen itself (the window's resolution) at `alpha`: what the transitions show
 * while the game is between the two screens, alive, not a still picture. */
static void hand_backdrop(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, double now, int language, float alpha)
{
    const float sx = plan.step[0], sy = plan.step[1];
    const float ih = plan.icon_h, iw = plan.icon_h * plan.icon[2] / plan.icon[3];
    const float ox = (float)fmod(now * plan.speed[0], (double)sx), oy = (float)fmod(now * plan.speed[1], 2.0 * sy);
    const float *top = place("band_top"), *bottom = place("band_bottom"), *title = place("title");
    const SDL_FRect stripes = {plan.stripes[0], plan.stripes[1], plan.stripes[2], plan.stripes[3]};
    const SDL_FRect icon = {plan.icon[0], plan.icon[1], plan.icon[2], plan.icon[3]};
    SDL_FRect dst = khdays_layout_to_window(fr, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H);
    float sheet[4] = {0, 0, 0, 0};
    char name[24];
    const KhdaysLayoutPiece *tp;
    if (sh.backdrop == NULL || alpha <= 0.002f) {
        return;
    }
    SDL_SetTextureAlphaModFloat(sh.backdrop, alpha);
    SDL_RenderTexture(renderer, sh.backdrop, &stripes, &dst);
    SDL_SetTextureAlphaModFloat(sh.backdrop, alpha * plan.icon_alpha);
    if (sheet_texels(now, sheet)) {
        const SDL_FRect from = {sheet[0], sheet[1], sheet[2], sheet[3]};
        SDL_RenderTexture(renderer, sh.backdrop, &from, &dst); /* (dst: the whole picture) */
    }
    for (int row = -2; !sheet[2] && row <= (int)(KHDAYS_LAYOUT_H / sy) + 2; ++row) {
        for (int col = -2; col <= (int)(KHDAYS_LAYOUT_W / sx) + 2; ++col) {
            const float x = (float)col * sx + ox + ((row & 1) ? sx / 2 : 0.0f), y = (float)row * sy + oy;
            if (x + iw <= 0 || x >= KHDAYS_LAYOUT_W || y + ih <= 0 || y >= KHDAYS_LAYOUT_H) {
                continue;
            }
            dst = khdays_layout_to_window(fr, x, y, iw, ih);
            SDL_RenderTexture(renderer, sh.backdrop, &icon, &dst);
        }
    }
    SDL_SetTextureAlphaModFloat(sh.backdrop, 1.0f);
    draw(fr, "band_top", top[0], top[1], top[2], top[3], alpha);
    draw(fr, "band_bottom", bottom[0], bottom[1], bottom[2], bottom[3], alpha);
    snprintf(name, sizeof(name), "title_%s", CODES[language >= 1 && language < LANGUAGES ? language : 1]);
    if ((tp = khdays_layout_piece(&sh.layout, name)) != NULL && tp->h > 0) {
        draw(fr, name, title[0], title[1], tp->w * title[3] / tp->h, title[3], alpha);
    }
}

/* Over everything (after the camp's menu): the screen going, kept, fading into the shop's backdrop alive
 * (HAND_TO_BACKDROP_SECONDS), which stays while the game is between the two and fades over the screen
 * coming once that one is lit a moment. 1 while it draws (drawn at the display's rate). */
#define HAND_TO_BACKDROP_SECONDS 0.3
int khdays_shop_screen_draw_over(SDL_Renderer *renderer)
{
    KhdaysLayoutFrame fr;
    const double now = seconds_now();
    int steady = 0, language = sh.language;
    float alpha = 1.0f, kept;
    if (hand.phase == HAND_NONE) {
        return 0;
    }
    if (hand.phase == HAND_IN) {
        KhdaysShopView v;
        khdays_shop_view(&v);
        if (!v.shown && hand.steady_at <= 0 && now - hand.at > HAND_WAIT_SECONDS) {
            hand_done(); /* the shop never came */
            return 0;
        }
        steady = v.shown && applied && sh.ok && v.step == KHDAYS_SHOP_MENU && v.master_upper >= 0;
        language = v.shown ? v.language : language;
    } else if (hand.story) {
        /* the field again (its situation, the shop gone), at its full brightness -- and past the moment the
         * game shows the 3D on the lower screen as it takes the field up (`lower-3d`, ~0.9 s: the DS's own
         * screens seen through the fade, its HUD's pieces popping in after), however long it lasts at
         * most FIELD_LOWER_3D_MOST */
        KhdaysShopView v;
        int field;
        khdays_shop_view(&v);
        field = !v.shown && khdays_onescreen_scene() == KHDAYS_SIT_FIELD && khdays_camp_game_brightness() >= 0;
        if (!field) {
            hand.field_at = 0;
        } else if (hand.field_at <= 0) {
            hand.field_at = now;
        }
        steady = field && (!(khdays_onescreen_states() & (1u << KHDAYS_ST_LOWER_3D)) ||
                           now - hand.field_at > FIELD_LOWER_3D_MOST);
    } else {
        KhdaysCampView camp;
        khdays_camp_view(&camp);
        steady = camp.shown && khdays_camp_game_brightness() >= 0;
    }
    if (now - hand.at > HAND_LONGEST || !khdays_present_one_picture()) {
        hand_done();
        return 0;
    }
    if (renderer != sh.layout.renderer || !sh.tried) {
        sh.tried = 1;
        sh.failed = !load(renderer); /* (handed over before the shop's screen ever drew) */
    }
    if (hand.fade_at <= 0) {
        if (!steady) {
            hand.steady_at = 0;
        } else if (hand.steady_at <= 0) {
            hand.steady_at = now;
        } else if (now - hand.steady_at >= (hand.story ? FIELD_STEADY_SECONDS : HAND_STEADY_SECONDS)) {
            hand.fade_at = now;
        }
    }
    if (hand.fade_at > 0) {
        alpha = 1.0f - smooth((float)((now - hand.fade_at) / (hand.story ? FIELD_FADE_SECONDS : HAND_FADE_SECONDS)));
    }
    if (alpha <= 0.002f) {
        hand_done();
        return 0;
    }
    if (hand.story) {
        /* the shop kept, faded into black; the black faded off the field */
        kept = 1.0f - smooth((float)((now - hand.at) / FIELD_TO_BLACK_SECONDS));
        khdays_layout_begin_over(renderer, &fr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 0.0f, alpha);
        SDL_RenderFillRect(renderer, NULL);
        if (kept > 0.002f && hand.picture != NULL) {
            SDL_SetTextureAlphaModFloat(hand.picture, kept * alpha);
            SDL_RenderTexture(renderer, hand.picture, NULL, NULL);
        }
        khdays_layout_end(renderer, &fr);
        return 1;
    }
    /* the screen going, kept, over the backdrop until it has faded into it (the backdrop missing: kept) */
    kept = sh.ok && sh.backdrop != NULL ? 1.0f - smooth((float)((now - hand.at) / HAND_TO_BACKDROP_SECONDS)) : 1.0f;
    khdays_layout_begin_over(renderer, &fr);
    if (kept < 1.0f) {
        hand_backdrop(renderer, &fr, now, language, alpha);
    }
    if (kept > 0.002f && hand.picture != NULL) {
        SDL_SetTextureAlphaModFloat(hand.picture, kept * alpha);
        SDL_RenderTexture(renderer, hand.picture, NULL, NULL);
    }
    khdays_layout_end(renderer, &fr);
    return 1;
}

void khdays_shop_screen_renderer_gone(void)
{
    forget();
    hand_done();
    sh.tried = 0;
    sh.layout.renderer = NULL;
    khdays_layout_renderer_gone();
}
