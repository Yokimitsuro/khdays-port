/* The one-screen picture, on the presenting side (see onescreen.h). */
#include "onescreen.h"
#include "gfx_render.h"
#include "../../gfx/describe.h"
#include "../screens/camp_screen.h"
#include "../screens/shop_screen.h"
#include "../app/devices.h"
#include "../../runtime/menus/newgame.h"
#include "../../runtime/menus/retry.h"
#include "../../runtime/core/events.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/core/session_log.h"
#include "../../runtime/mods/textures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where each area of 2D memory is in a package's own copy
 * (runtime/display/display.c capture_2d), for the CPU's composition (describe.h):
 * the area, its first page in the copy, and how many pages. */
static const struct {
    int area, first, pages;
} areas[] = {
    {KHDAYS_AREA_ABG, KHDAYS_2D_ABG, 32},      {KHDAYS_AREA_AOBJ, KHDAYS_2D_AOBJ, 16},
    {KHDAYS_AREA_BBG, KHDAYS_2D_BBG, 8},       {KHDAYS_AREA_BOBJ, KHDAYS_2D_BOBJ, 8},
    {KHDAYS_AREA_ABGEXT, KHDAYS_2D_ABGEXT, 2}, {KHDAYS_AREA_AOBJEXT, KHDAYS_2D_AOBJEXT, 1},
    {KHDAYS_AREA_BBGEXT, KHDAYS_2D_BBGEXT, 2}, {KHDAYS_AREA_BOBJEXT, KHDAYS_2D_BOBJEXT, 1},
};

typedef struct Pixels {
    const KhdaysFramePackage *p;
    KhdaysVramPages pages;
    KhdaysGfxMemory memory[2];
    uint32_t desc[2][KHDAYS_DESC_WORDS]; /* each engine's description, with KDF_SPLIT set */
} Pixels;

static Pixels pixels;
static KhdaysSituation situation = {KHDAYS_SIT_UNKNOWN, 0};
static SDL_AtomicInt scene_shown; /* situation.scene, for other threads (khdays_onescreen_scene) */

int khdays_onescreen_scene(void)
{
    return SDL_GetAtomicInt(&scene_shown);
}

static SDL_AtomicInt states_shown; /* situation.states, for other threads (khdays_onescreen_states) */

unsigned khdays_onescreen_states(void)
{
    return (unsigned)SDL_GetAtomicInt(&states_shown);
}
static int told = 0; /* the first situation was written to the log */
/* The package decided last, and how many times it has been decided again (each one more of its
 * frame in the situation's history, as an alike VBlank after it): the next package's alike ones
 * are those not taken so. */
static uint32_t decided_package = ~0u;
static uint32_t alike_taken;

/* An HD replacement image's pixels, for a screen element (NULL if none). */
static const uint32_t *element_texels(uint32_t image)
{
    const KhdaysTextureReplacement *r = khdays_textures_replacement((int)image);
    return r != NULL ? r->rgba : NULL;
}

/* A screen's pixel as kd_split gives it (`screen` 0 is the upper, 1 the
 * lower), for the situation's checks. */
static uint32_t pixel(void *ctx, int screen, int x, int y)
{
    Pixels *px = (Pixels *)ctx;
    const int engine = screen == 0 ? px->p->upper : !px->p->upper;
    return khdays_gfx_pixel_cpu(px->desc[engine], &px->memory[engine], x, y);
}

/* Sets up `pixels` so the CPU can read package `p`'s 2D: its memory pages and
 * each engine's description. */
static void make_pixels(const KhdaysFramePackage *p)
{
    memset(&pixels.pages, 0, sizeof(pixels.pages));
    pixels.p = p;
    for (size_t i = 0; i < sizeof(areas) / sizeof(areas[0]); ++i) {
        for (int n = 0; n < areas[i].pages; ++n) {
            pixels.pages.page[areas[i].area][n] =
                (uint8_t *)p->mem2d + ((size_t)areas[i].first + (size_t)n) * KHDAYS_FRAME_VRAM_PAGE;
        }
    }
    for (int e = 0; e < 2; ++e) {
        KhdaysGfxMemory *m = &pixels.memory[e];
        memset(m, 0, sizeof(*m));
        m->vram = &pixels.pages;
        m->palette = p->palette2d;
        /* a VRAM display shows its bank from the package's copy */
        m->bank[p->desc2d[e][KD_VRAM] & 3] = p->mem2d + (size_t)KHDAYS_2D_VRAM * KHDAYS_FRAME_VRAM_PAGE;
        m->elements = p->elements[e].count != 0 ? p->elements[e].records : NULL;
        m->element_texels = element_texels;
        if (p->desc2d[e][KD_LEFT_OUT] != 0) {
            m->left_out_cells = p->left_out_cells[e];
            m->left_out_obj = p->left_out_obj[e];
        }
        memcpy(pixels.desc[e], p->desc2d[e], sizeof(pixels.desc[e]));
        pixels.desc[e][KD_FLAGS] |= KDF_SPLIT | KDF_FINE;
        pixels.desc[e][KD_WIDE] = 0;
    }
}

/* Whether `frame` is one of the frames listed in KHDAYS_TRACE_2D (numbers
 * separated by spaces). For those frames tell_left_out reports what the
 * composition leaves out of the upper screen's 2D. */
static int traced_frame(uint32_t frame)
{
    static int read = 0;
    static char *list = NULL;
    if (!read) {
        const char *v = getenv("KHDAYS_TRACE_2D");
        read = 1;
        if (v != NULL) {
            list = (char *)malloc(strlen(v) + 3);
            if (list != NULL) {
                snprintf(list, strlen(v) + 3, " %s ", v);
            }
        }
    }
    if (list != NULL) {
        char at[24];
        snprintf(at, sizeof(at), " %u ", frame);
        return strstr(list, at) != NULL;
    }
    return 0;
}

/* Reports the upper screen's 2D pixels (shown over the 3D) that no upper-screen shape takes, so the
 * composition leaves them out (the 2D shows only inside its shapes): how many and their box, on stderr
 * and in the session log. */
static void tell_left_out(const KhdaysFramePackage *p, const KhdaysComposition *composition)
{
    int count = 0, x0 = 256, y0 = 192, x1 = -1, y1 = -1;
    for (int y = 0; y < 192; ++y) {
        for (int x = 0; x < 256; ++x) {
            int taken = 0;
            if ((pixel(&pixels, 0, x, y) >> 24) == 0u) {
                continue; /* nothing of the 2D over the 3D there */
            }
            for (int s = 0; s < composition->shape_count && !taken; ++s) {
                const uint32_t *w = composition->shapes[s].words;
                float sx, sy, sw, sh;
                memcpy(&sx, &w[KO_SRC_X], 4);
                memcpy(&sy, &w[KO_SRC_Y], 4);
                memcpy(&sw, &w[KO_SRC_W], 4);
                memcpy(&sh, &w[KO_SRC_H], 4);
                taken = !(w[KO_FLAGS] & KOF_LOWER) && (float)x >= sx && (float)x < sx + sw && (float)y >= sy &&
                        (float)y < sy + sh;
            }
            if (!taken) {
                ++count;
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    fprintf(stderr, "one screen: frame %u, %s: %d pixels of the upper screen's 2D in no shape", p->frame,
            khdays_situation_name(situation.scene), count);
    if (count != 0) {
        fprintf(stderr, ", within %d,%d-%d,%d", x0, y0, x1, y1);
    }
    fprintf(stderr, "\n");
    khdays_log("one screen: frame %u, %s: %d pixels of the upper screen's 2D in no shape (%d,%d-%d,%d)", p->frame,
               khdays_situation_name(situation.scene), count, x0, y0, x1, y1);
}

/* The screens that show something (the situation's `content`, for the compositions' common rule): a
 * screen whose points of a 16 x 12 grid are not all alike. */
static void screens_content(KhdaysSituation *s)
{
    s->content = 0;
    for (int screen = 0; screen < 2; ++screen) {
        const uint32_t first = pixel(&pixels, screen, 8, 8);
        for (int gy = 0; gy < 12 && !(s->content & (1u << screen)); ++gy) {
            for (int gx = 0; gx < 16; ++gx) {
                if (pixel(&pixels, screen, 8 + gx * 16, 8 + gy * 16) != first) {
                    s->content |= 1u << screen;
                    break;
                }
            }
        }
    }
}

/* The port's own state "info-lower": the lower screen shows the field's INFORMATION banner, its white
 * lines across at y 23 and, right of its label, at y 8 (the play walk's shots: white from f4680 on,
 * the panel's grey 36 before the banner comes, f4320-4560). */
static int near_white(uint32_t c)
{
    for (int n = 0; n < 3; ++n) {
        if (((c >> (8 * n)) & 0xffu) < 0xf0u) {
            return 0;
        }
    }
    return 1;
}

static void info_lower(KhdaysSituation *s)
{
    static const int below[] = {16, 64, 128, 192, 236}, above[] = {128, 192, 236};
    if (s->scene != KHDAYS_SIT_FIELD && s->scene != KHDAYS_SIT_FIELD_3D_BOTH && s->scene != KHDAYS_SIT_TUTORIAL &&
        s->scene != KHDAYS_SIT_PAUSE) {
        return;
    }
    for (int i = 0; i < (int)(sizeof(below) / sizeof(below[0])); ++i) {
        if (!near_white(pixel(&pixels, 1, below[i], 23))) {
            return;
        }
    }
    for (int i = 0; i < (int)(sizeof(above) / sizeof(above[0])); ++i) {
        if (!near_white(pixel(&pixels, 1, above[i], 8))) {
            return;
        }
    }
    s->states |= 1u << KHDAYS_ST_INFO_LOWER;
}

/* The port's own state "info-new": on the DS the lower screen's INFORMATION line keeps its last
 * message; on one screen it shows only for a while after its text changes (the text row's pixels are
 * summed every frame; a new sum is a new message, shown for INFO_FRAMES). */
#define INFO_FRAMES 300       /* 5 seconds */
#define INFO_SETTLE_FRAMES 30 /* a line unchanged so long stands still */

/* Whether the INFORMATION line holds a message: a pixel of its text rows that stands out from its ground
 * (the line's left end), dimmed or not -- a fade darkens both (white 255 or grey 134 text over black). */
static int info_has_text(void)
{
    const uint32_t ground = pixel(&pixels, 1, 6, 15);
    for (int y = 11; y <= 19; ++y) {
        for (int x = 8; x < 224; ++x) {
            const uint32_t c = pixel(&pixels, 1, x, y);
            for (int n = 0; n < 3; ++n) {
                const int d = (int)((c >> (8 * n)) & 0xffu) - (int)((ground >> (8 * n)) & 0xffu);
                if (d > 40 || d < -40) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void info_new(const KhdaysFramePackage *p, KhdaysSituation *s)
{
    static uint32_t last_sum, changed, still_since;
    static int seen, after_start;
    uint32_t sum = 2166136261u; /* FNV-1a */
    if (s->states & (1u << KHDAYS_ST_INFO_UPPER)) {
        /* a mission's start shows its information (info-upper): the lower screen's first message
         * after it -- once its line stands still, the game drawing it over a frame or two -- is
         * taken as seen, so the banner is not shown twice (the maintainer, 2026-10-15) */
        after_start = 1;
    }
    if (!(s->states & (1u << KHDAYS_ST_INFO_LOWER))) {
        seen = 0; /* the same message coming back is shown again */
        return;
    }
    if (!info_has_text()) {
        /* the line emptied (a message over): no banner, and the next message is a new one */
        seen = 0;
        changed = p->frame - INFO_FRAMES;
        return;
    }
    for (int y = 10; y <= 20; y += 2) {
        for (int x = 4; x < 252; ++x) {
            sum = (sum ^ (pixel(&pixels, 1, x, y) & 0xffffffu)) * 16777619u;
        }
    }
    if (after_start) {
        if (!seen || sum != last_sum) {
            seen = 1;
            last_sum = sum;
            still_since = p->frame;
        }
        changed = p->frame - INFO_FRAMES;
        after_start = p->frame - still_since < INFO_SETTLE_FRAMES;
    } else if (!seen || sum != last_sum) {
        seen = 1;
        last_sum = sum;
        changed = p->frame;
    }
    if (p->frame - changed < INFO_FRAMES) {
        s->states |= 1u << KHDAYS_ST_INFO_NEW;
    }
}

/* The port's own state "mission-panel": the lower screen shows the field's mission panel, its upper
 * edge two rows across (y 152 the grey 0x6d, y 153 the grey 0xae). Unlike the HUD it is there at a
 * mission's start too. */
static int near_grey(uint32_t c, uint32_t v)
{
    for (int n = 0; n < 3; ++n) {
        const int d = (int)((c >> (8 * n)) & 0xffu) - (int)v;
        if (d < -12 || d > 12) {
            return 0;
        }
    }
    return 1;
}

static void mission_panel(KhdaysSituation *s)
{
    static const int xs[] = {16, 64, 128, 192, 236};
    for (int i = 0; i < (int)(sizeof(xs) / sizeof(xs[0])); ++i) {
        if (!near_grey(pixel(&pixels, 1, xs[i], 152), 0x6du) || !near_grey(pixel(&pixels, 1, xs[i], 153), 0xaeu)) {
            return;
        }
    }
    s->states |= 1u << KHDAYS_ST_MISSION_PANEL;
}

/* The port's own state "lower-shown": with the 3D on both screens the lower screen shows a picture --
 * Sora's memories, the game's noise over them (mission 61's Guard Armor) -- not black, nor faded out
 * by its brightness. Once shown it stays until it has been dark for LOWER_DARK_FRAMES, so a dark frame
 * of the noise does not switch the picture back and forth. */
#define LOWER_DARK_FRAMES 8

static void lower_shown(const KhdaysSituationInputs *in, KhdaysSituation *s)
{
    static int shown, dark;
    const uint16_t master = in->master[(in->powcnt1 & 0x8000u) != 0u ? 1 : 0]; /* the lower screen's engine */
    const int mode = (master >> 14) & 3, factor = (master & 0x1f) > 16 ? 16 : master & 0x1f;
    int lit = 0, all = 0;
    if (s->scene != KHDAYS_SIT_FIELD_3D_BOTH) {
        shown = dark = 0;
        return;
    }
    if (mode == 0 || factor < 12) {
        for (int y = 4; y < 192; y += 8) {
            for (int x = 4; x < 256; x += 8) {
                const uint32_t c = pixel(&pixels, 1, x, y);
                const uint32_t r = c & 0xffu, g = (c >> 8) & 0xffu, b = (c >> 16) & 0xffu;
                lit += (r > g ? (r > b ? r : b) : (g > b ? g : b)) > 40u;
                ++all;
            }
        }
    }
    if (lit * 4 >= all && all > 0) {
        shown = 1;
        dark = 0;
    } else if (shown && ++dark >= LOWER_DARK_FRAMES) {
        shown = 0;
    }
    if (shown) {
        s->states |= 1u << KHDAYS_ST_LOWER_SHOWN;
    }
}

/* The port's: the title's menu as the inputs have it (Ov000_RefreshMenuLayout): its first level (two
 * options, or three with THEATER MODE), the story level (NEW GAME, and CONTINUE with a save), the
 * mission level; none where the menu does not show, and its fade-in. */
static void title_menu(const KhdaysSituationInputs *in, KhdaysSituation *s)
{
    /* the new game's dialogs come over the title as DDD's do, with no blink: the lower screen's fades into
     * them, of their steps and back are not shown (state title-lit) */
    static int from_newgame;
    s->title_menu = KHDAYS_TITLE_NONE;
    s->title_count = s->title_row = 0;
    s->title_lang = in->title_lang;
    if (in->title_newgame != KHDAYS_NEWGAME_NONE) { /* the new game's dialogs (runtime/menus/newgame.h) */
        s->title_menu = in->title_load ? KHDAYS_TITLE_NEWGAME_LOAD : KHDAYS_TITLE_NEWGAME;
        s->title_count = in->title_newgame;
        s->title_row =
            in->title_newgame == KHDAYS_NEWGAME_REVIEW
                ? in->title_newgame_difficulty * 4 + (in->title_newgame_subtitles ? 0 : 2) + in->title_newgame_button
                : in->title_newgame_button;
        s->states |= 1u << KHDAYS_ST_TITLE_LIT;
        from_newgame = 1;
        return;
    }
    if (in->title_passing == 1 || (in->title_passing == 2 && from_newgame)) {
        s->states |= 1u << KHDAYS_ST_TITLE_LIT;
    } else {
        from_newgame = 0;
    }
    if (in->title_logo != 0) { /* the boot logos: their screen as the row */
        s->title_menu = KHDAYS_TITLE_LOGO;
        s->title_count = 3;
        s->title_row = in->title_logo - 1;
        return;
    }
    if (!in->title_valid || in->title_shown == KHDAYS_TITLE_HIDDEN) {
        return;
    }
    if (in->title_shown == KHDAYS_TITLE_WHITE) {
        s->title_menu = KHDAYS_TITLE_INTRO;
        return;
    }
    if (in->title_page == 0) {
        s->title_menu = KHDAYS_TITLE_ROOT;
        s->title_count = in->title_extra ? 3 : 2;
    } else if (in->title_alt) {
        s->title_menu = KHDAYS_TITLE_MISSION;
        s->title_count = 2;
    } else {
        s->title_menu = KHDAYS_TITLE_STORY;
        s->title_count = in->title_load ? 2 : 1;
    }
    s->title_row = in->title_row >= 0 && in->title_row < s->title_count ? in->title_row : 0;
}

/* KHDAYS_TRACE_TITLE: each change of the title's picture (its level, count, row, whether its
 * fades show) and the two screens' brightness, with the frame (stderr and the session log). */
static void trace_title(const KhdaysFramePackage *p, const KhdaysSituation *s)
{
    static int on = -1, menu = -1, count, row, lit;
    static uint16_t master[2];
    const int now_lit = (s->states >> KHDAYS_ST_TITLE_LIT) & 1u;
    if (on < 0) {
        on = getenv("KHDAYS_TRACE_TITLE") != NULL;
    }
    if (!on || (s->title_menu == menu && s->title_count == count && s->title_row == row && now_lit == lit &&
                p->situation.master[0] == master[0] && p->situation.master[1] == master[1])) {
        return;
    }
    menu = s->title_menu;
    count = s->title_count;
    row = s->title_row;
    lit = now_lit;
    master[0] = p->situation.master[0];
    master[1] = p->situation.master[1];
    fprintf(stderr, "title: frame %u, menu %d count %d row %d%s, brightness upper 0x%04x lower 0x%04x, passing %u\n",
            p->frame, menu, count, row, lit ? " lit" : "", master[0], master[1], p->situation.title_passing);
    khdays_log("title: frame %u, menu %d count %d row %d%s, brightness upper 0x%04x lower 0x%04x, passing %u", p->frame,
               menu, count, row, lit ? " lit" : "", master[0], master[1], p->situation.title_passing);
}

/* The settings' prompt (ui/composition.h `prompt open_settings`): while a gamepad is played, its
 * inputs for the settings, named by its family as the pictures' icons are (an Xbox pad's, a
 * PlayStation's, any other's); none with the keyboard, whose settings key is Esc. */
static void settings_prompt(void)
{
    char names[KHDAYS_INPUTS_PER_ACTION][48];
    const char *list[KHDAYS_INPUTS_PER_ACTION];
    SDL_Gamepad *pad = khdays_devices_gamepad();
    int n = 0;
    if (pad != NULL && khdays_devices_pad_in_use()) {
        const KhdaysProfile *profile = khdays_devices_profile();
        const SDL_GamepadType type = SDL_GetGamepadType(pad);
        const char *family =
            type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE ? "xbox"
            : type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5
                ? "playstation"
                : "pad";
        for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
            const int input = profile->input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][KHDAYS_ACT_OPEN_SETTINGS][s];
            const char *name = input != KHDAYS_NO_INPUT ? khdays_pad_input_name(input) : NULL;
            if (name != NULL) {
                SDL_snprintf(names[n], sizeof(names[n]), "%s:%s", family, name);
                list[n] = names[n];
                ++n;
            }
        }
    }
    khdays_composition_set_prompt("open_settings", list, n);
}

/* Writes the situation (the scene and its states) to stderr and the session log. */
static void tell(uint32_t frame)
{
    char states[400];
    size_t at = 0;
    states[0] = '\0';
    for (int s = 0; s < KHDAYS_STATES; ++s) {
        if (situation.states & (1u << s)) {
            const int n =
                snprintf(states + at, sizeof(states) - at, "%s%s", at != 0 ? " " : "", khdays_situation_state_name(s));
            at += n > 0 && (size_t)n < sizeof(states) - at ? (size_t)n : 0;
        }
    }
    fprintf(stderr, "one screen: frame %u, %s [%s]\n", frame, khdays_situation_name(situation.scene), states);
    khdays_log("one screen: frame %u, %s [%s]", frame, khdays_situation_name(situation.scene), states);
}

/* A screen of the port's that shows the game's 3D model in a window of its own (the camp menu's,
 * screens/camp_screen.c): the picture is its backdrop and the upper screen's 3D alone, the polygons in
 * the model's rectangle moved and scaled into the window's place (given in the port's 1920 x 1080
 * layout, fitted as the screen fits it), and nothing of the 2D. The screen draws everything else over
 * it, the game's fade over all (KOB_OWN). */
static KhdaysShape3d model_shape;

/* The game over's own firefly -- the orb that floats to the line chosen, drawn as the HUD's 3D (prepare.py's
 * hook in Ov027_UpdateFlushForward) -- hidden while the window draws the game over's menu as KH2's pause,
 * with KH2's firefly (runtime/menus/retry.h khdays_game_over_hides_orb). */
static KhdaysShape3d game_over_shapes[8];

static void game_over_orb(KhdaysComposition *composition)
{
    const int n = composition->shape3d_count;
    if (situation.scene != KHDAYS_SIT_GAME_OVER || !khdays_game_over_hides_orb() || n <= 0 ||
        n > (int)(sizeof(game_over_shapes) / sizeof(game_over_shapes[0]))) {
        return;
    }
    memcpy(game_over_shapes, composition->shapes3d, (size_t)n * sizeof(game_over_shapes[0]));
    for (int i = 0; i < n; ++i) {
        if (game_over_shapes[i].group == KHDAYS_SHAPE3D_HUD) {
            game_over_shapes[i].hide = 1;
        }
    }
    composition->shapes3d = game_over_shapes;
}

/* The model's shape: the polygons of `src` (the DS's dots) -- the part `view` of them centred in the window
 * `dst` (the port's 1920 x 1080 layout, fitted in the picture as the screen fits it), as large as it fits. */
static void place_model(const KhdaysFramePackage *p, int columns, const float *src, const float *view, const float *dst)
{
    const float c = (float)columns;
    const float k = c / 1920.0f < 192.0f / 1080.0f ? c / 1920.0f : 192.0f / 1080.0f; /* layout -> dots */
    const float ox = (c - 1920.0f * k) / 2.0f, oy = (192.0f - 1080.0f * k) / 2.0f;
    const float scale = (dst[2] / view[2] < dst[3] / view[3] ? dst[2] / view[2] : dst[3] / view[3]) * k;
    /* the 3D's polygons are where the game draws them before its 3D layer's scroll (BG0HOFS: a screen dot
     * x shows the 3D's x + scroll), in the rectangle where the screen shows them */
    const int hofs = (int)(p->info[0].bg0_hofs & 511u);
    model_shape.group = KHDAYS_SHAPE3D_ALL;
    model_shape.hide = 0;
    /* the whole model: a long weapon's polygons with their middle outside the DS's box (Marluxia's
     * scythe's blade) went missing; the camp screen's backdrop round the window cuts it there */
    model_shape.whole = 1;
    model_shape.world = 0;
    model_shape.src_x = src[0] + (float)(hofs >= 256 ? hofs - 512 : hofs);
    model_shape.src_y = src[1];
    model_shape.src_w = src[2];
    model_shape.src_h = src[3];
    model_shape.scale = scale;
    /* the view centred in the window (the source's corner where that puts it) */
    model_shape.x = ox + dst[0] * k + (dst[2] * k - view[2] * scale) / 2.0f - (view[0] - src[0]) * scale;
    model_shape.y = oy + dst[1] * k + (dst[3] * k - view[3] * scale) / 2.0f - (view[1] - src[1]) * scale;
}

/* A DS screen as the 2D engines compose it, at the DS's resolution, from the package make_pixels took
 * (`fade`: with its master brightness, else without: the port's screen draws the game's fade itself). */
static void compose_screen(const KhdaysFramePackage *p, int engine, int fade, uint32_t *out)
{
    uint32_t desc[KHDAYS_DESC_WORDS];
    memcpy(desc, p->desc2d[engine], sizeof(desc));
    if (!fade) {
        desc[KD_MASTER] = 0;
    }
    /* the DS's 6 bits a channel, which the composer's output widens to 8 itself: no KDF_FINE (its 8-bit
     * channels read as 6 there, the colours wrong), and the 3D with the 2D (no KDF_SPLIT) */
    desc[KD_FLAGS] &= ~(uint32_t)(KDF_FINE | KDF_SPLIT);
    desc[KD_WIDE] = 0;
    khdays_gfx_compose_cpu(desc, &pixels.memory[engine], out);
}

static void put_word_float(uint32_t *words, int at, float v)
{
    memcpy(&words[at], &v, sizeof(v));
}

/* The story's Panels page (camp_screen.h): the DS's lower screen as a card over the animated picture -- its dots
 * `lower_src` (its grid and list, every texel) at its rectangle of the 1920 x 1080 layout (fitted in the picture
 * as the screen fits it), at its opacity. Its 3D (the grid's panels, the list's icons: the lower engine draws the
 * 3D) comes in it: the engine's 2D is composed with its 3D for once (`lower_whole`), not apart from it with the 3D
 * placed at the picture's texel -- the 3D would have to be at the card's place for that and at the DS's for the
 * 2D to know where it shows (moved, the panels came out as black holes, small, beside the grid; not moved, black
 * where they are) -- and the 3D is not shown again over the picture. */
static KhdaysShape card_shape;

static void camp_cards(int columns, const KhdaysCampModel *m, KhdaysComposition *composition)
{
    const float c = (float)columns;
    const float k = c / 1920.0f < 192.0f / 1080.0f ? c / 1920.0f : 192.0f / 1080.0f; /* layout -> dots */
    const float ox = (c - 1920.0f * k) / 2.0f, oy = (192.0f - 1080.0f * k) / 2.0f;
    const float *r = m->lower_card, *src = m->lower_src;
    const int on = r[2] > 0.0f && r[3] > 0.0f && src[2] > 0.0f && src[3] > 0.0f && m->lower_alpha > 0.002f;
    uint32_t *words = card_shape.words;
    composition->shapes = &card_shape;
    composition->shape_count = on ? 1 : 0;
    composition->key_count = 0;
    composition->hide3d_all = 1;
    composition->lower_whole = 1;
    composition->shapes3d = NULL;
    composition->shape3d_count = 0;
    if (!on) {
        return;
    }
    memset(words, 0, sizeof(card_shape.words));
    words[KO_FLAGS] = KOF_FORCE | KOF_LOWER | (m->lower_alpha < 0.998f ? KOF_WEIGHT : 0u);
    put_word_float(words, KO_SRC_X, src[0]);
    put_word_float(words, KO_SRC_Y, src[1]);
    put_word_float(words, KO_SRC_W, src[2]);
    put_word_float(words, KO_SRC_H, src[3]);
    put_word_float(words, KO_DST_X1, ox + r[0] * k);
    put_word_float(words, KO_DST_Y1, oy + r[1] * k);
    put_word_float(words, KO_DST_X2, ox + (r[0] + r[2]) * k);
    put_word_float(words, KO_DST_Y2, oy + (r[1] + r[3]) * k);
    put_word_float(words, KO_OPACITY, m->lower_alpha);
    put_word_float(words, KO_SCALE_X, r[2] * k / src[2]);
    put_word_float(words, KO_SCALE_Y, r[3] * k / src[3]);
}

static void shop_screens(const KhdaysFramePackage *p, int card, int card_slide);

static void model_window(const KhdaysFramePackage *p, int columns, KhdaysComposition *composition)
{
    KhdaysCampModel m = {0};
    /* the camp's menu page; its Preferences page, which the situations take for the game's
     * settings page (a camp's screen 5 is that page) */
    const int got = (situation.scene == KHDAYS_SIT_CAMP_MENU || situation.scene == KHDAYS_SIT_SETTINGS) &&
                    khdays_camp_screen_model(&m);
    const int on = got && (situation.scene == KHDAYS_SIT_CAMP_MENU || !m.model);
    khdays_camp_screen_applied(on);
    if (!on) {
        return;
    }
    if (m.files && pixels.p == p) {
        /* the Files page draws the DS's upper screen itself, its own size and float: the screen as the
         * 2D engines compose it, from this package -- on the card, the HD tiles and fonts in it (0.1.4's text is
         * laid out for them: without, unreadable), else at the DS's resolution (make_pixels) */
        static uint32_t upper[KHDAYS_FRAME_PIXELS];
        int scale;
        if (khdays_camp_screen_upper_wanted(&scale)) {
            composition->shop_screens[0] = KHDAYS_SHOP_WANT_NORMAL;
            composition->shop_with3d[0] = 1; /* as the CPU's (no fade: the game's is over everything) */
            composition->shop_scale = scale;
        }
        if (khdays_gfx_render_shop_made(0, KHDAYS_SHOP_NORMAL)) {
            khdays_camp_screen_upper(NULL);
        } else {
            compose_screen(p, p->upper, 0, upper);
            khdays_camp_screen_upper(upper);
        }
    }
    place_model(p, columns, m.source, m.view, m.window);
    composition->screens = KHDAYS_SCREENS_UPPER;
    composition->brightness = (int)KOB_OWN;
    composition->shapes = NULL;
    composition->shape_count = 0;
    composition->key_count = 0;
    /* the Preferences page shows no model: its backdrop alone */
    composition->shapes3d = m.model ? &model_shape : NULL;
    composition->shape3d_count = m.model ? 1 : 0;
    composition->hide3d_all = !m.model;
    composition->backdrop = m.backdrop;
    composition->backdrop_w = m.backdrop_w;
    composition->backdrop_h = m.backdrop_h;
    composition->backdrop_ax = composition->backdrop_ay = 0.5f;
    composition->backdrop_fit = 1;
    composition->animated = 0;
    composition->picture_w = composition->picture_h = 0;
    composition->piece_count = 0;
    composition->button_count = 0;
    if (m.atlas != NULL) {
        /* the story's menu: its animated picture (the pattern drifting, its plates) under the model */
        composition->backdrop = m.atlas;
        composition->backdrop_w = m.atlas_w;
        composition->backdrop_h = m.atlas_h;
        composition->animated = 1;
        composition->picture_w = 1920;
        composition->picture_h = 1080;
        composition->ground = m.ground;
        composition->pieces = m.pieces;
        composition->piece_count = m.piece_count;
        composition->buttons = NULL;
    }
    if (m.cards) {
        camp_cards(columns, &m, composition);
    }
    if (m.panels_upper) {
        /* the Panels page's upper screen (what X shows there): composed on the CPU, some 10 ms, so on the Moogle
         * Shop's thread (the shop is not open meanwhile), when it changed; and, once that showed the list, on the
         * card too, with the HD tiles and fonts, which the page draws */
        int scale;
        shop_screens(p, 1, m.panels_unslide);
        if (khdays_camp_screen_panels_listed(&scale)) {
            composition->shop_screens[0] = KHDAYS_SHOP_WANT_NORMAL;
            composition->shop_slide[0] = m.panels_unslide;
            composition->shop_scale = scale;
        }
    }
}

/* ---- The Moogle Shop's DS screens, composed apart -------------------------------------------------
 * The shop's page floats parts of the DS's two screens (the lower's list, the upper's counter and
 * Synthesize's recipe), without the DS's stripes behind them (or as they are, in a frame, as the mod's
 * shop.txt says or when that cannot be made). Composing a screen on the CPU takes some 10 ms, so it is
 * not done here: each of the game's pictures with a list up, what the composition reads (both engines'
 * descriptions, their layers' and sprites' memory, the palettes, the port's elements) is copied -- when it
 * changed: a picture alike to the last gives nothing new -- and a thread of its own composes it, the
 * newest only; the page takes what it made when it is done. The stripes' layers are found once for each
 * list shown (the request's generation): each layer alone, over six strips of the screen, its own pixels
 * counted and those of the stripes' mustard browns -- the layers mostly of the stripes, over a quarter of
 * the screen or more, are left out (Redeem's list has its own over the screen's); none, the backmost that
 * covers the screen whole. What each layer showed goes to the session log. */
#define SHOP_PAGES 51 /* an engine's layers, sprites and extended palettes (engine A's: the most) */
#define SHOP_KEY 0xff00ffu
enum { SHOP_UPPER, SHOP_LOWER };

typedef struct ShopInput {
    KhdaysShopRequest request;
    int engine[2];
    uint32_t desc[2][KHDAYS_DESC_WORDS];
    uint8_t mem[2][SHOP_PAGES * KHDAYS_FRAME_VRAM_PAGE];
    uint8_t palette[0x800];
    uint32_t *elements[2];
    uint32_t element_count[2], element_room[2];
    uint8_t left_cells[2][KE_LEFT_CELL_WORDS * 4], left_obj[2][KE_LEFT_OBJ_WORDS * 4];
    int has_left[2];
    uint64_t hash;
    int card;       /* the story's Panels page's instead (see model_window): its upper screen whole */
    int card_slide; /* slid back by so many dots: its layers' scroll less, its sprites more */
} ShopInput;

typedef struct ShopOutput {
    uint32_t normal[2][KHDAYS_FRAME_PIXELS], cut[2][KHDAYS_FRAME_PIXELS];
    int normal_ok[2], cut_ok[2];
    unsigned layers[2]; /* the layers each cut left out (for the card's: gfx_render.h khdays_gfx_render_shop) */
    int card;
} ShopOutput;

static struct {
    SDL_Mutex *lock;
    SDL_Condition *wake;
    SDL_Thread *thread;
    int failed;
    ShopInput *staging, *pending, *working;
    int pending_new;
    ShopOutput *made, *ready, *taken;
    unsigned ready_serial, taken_serial;
    uint64_t last_hash;
    uint32_t last_frame;
    /* the thread's: each screen's stripes' layers (a bit each) for a generation of the request, 0 none */
    unsigned layers[2];
    unsigned layer_generation[2];
    uint64_t layer_setup[2]; /* the layers' setup they were found on (another: found again) */
    int layer_known[2];
    int told_thin[2]; /* a cut with too little left told for this generation */
    /* the timings (khdays_onescreen_shop_work): under `lock` */
    unsigned jobs, alike;
    double ms, most;
    unsigned cut_layers[2]; /* the window's: the layers the last cuts taken left out */
} shop_bg;

/* each engine's areas in areas[] (A: its layers, sprites, extended palettes; B the same) */
static const int ENGINE_AREAS[2][4] = {{0, 1, 4, 5}, {2, 3, 6, 7}};

static uint64_t hash_bytes(uint64_t h, const void *data, size_t size)
{
    const uint8_t *b = (const uint8_t *)data;
    size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        uint64_t w;
        memcpy(&w, b + i, 8);
        h = (h ^ w) * 0x100000001b3ull;
        h ^= h >> 29;
    }
    for (; i < size; ++i) {
        h = (h ^ b[i]) * 0x100000001b3ull;
    }
    return h;
}

/* The window's thread: what the screens' composition reads, as a hash -- its memory by its pages'
 * versions (runtime/display/display.c: a page's changes when its bytes do) -- so a picture alike to the
 * last is not composed again. */
static uint64_t shop_signature(const KhdaysFramePackage *p, const KhdaysShopRequest *r)
{
    uint64_t h = 1469598103934665603ull;
    h = hash_bytes(h, r, sizeof(*r));
    h = hash_bytes(h, p->palette2d, sizeof(p->palette2d));
    for (int s = 0; s < 2; ++s) {
        const int engine = s == SHOP_UPPER ? p->upper : !p->upper;
        h = hash_bytes(h, p->desc2d[engine], sizeof(p->desc2d[engine]));
        for (int k = 0; k < 4; ++k) {
            const int a = ENGINE_AREAS[engine][k];
            h = hash_bytes(h, &p->mem2d_version[areas[a].first], (size_t)areas[a].pages * sizeof(uint32_t));
        }
        if (p->elements[engine].count != 0 && p->elements[engine].records != NULL) {
            h = hash_bytes(h, p->elements[engine].records,
                           (size_t)p->elements[engine].count * KE_WORDS * sizeof(uint32_t));
        }
        if (p->desc2d[engine][KD_LEFT_OUT] != 0) {
            h = hash_bytes(h, p->left_out_cells[engine], sizeof(p->left_out_cells[engine]));
            h = hash_bytes(h, p->left_out_obj[engine], sizeof(p->left_out_obj[engine]));
        }
    }
    return h;
}

/* The window's thread: what the screens' composition reads, copied from the package. */
static void shop_fill(ShopInput *in, const KhdaysFramePackage *p, const KhdaysShopRequest *r, uint64_t hash)
{
    in->request = *r;
    in->hash = hash;
    memcpy(in->palette, p->palette2d, sizeof(in->palette));
    for (int s = 0; s < 2; ++s) {
        const int engine = s == SHOP_UPPER ? p->upper : !p->upper;
        size_t at = 0;
        in->engine[s] = engine;
        memcpy(in->desc[s], p->desc2d[engine], sizeof(in->desc[s]));
        for (int k = 0; k < 4; ++k) {
            const int a = ENGINE_AREAS[engine][k];
            const size_t bytes = (size_t)areas[a].pages * KHDAYS_FRAME_VRAM_PAGE;
            memcpy(in->mem[s] + at, (const uint8_t *)p->mem2d + (size_t)areas[a].first * KHDAYS_FRAME_VRAM_PAGE, bytes);
            at += bytes;
        }
        in->element_count[s] = 0;
        if (p->elements[engine].count != 0 && p->elements[engine].records != NULL) {
            const uint32_t n = p->elements[engine].count;
            if (n > in->element_room[s]) {
                uint32_t *more = (uint32_t *)realloc(in->elements[s], (size_t)n * KE_WORDS * sizeof(uint32_t));
                if (more != NULL) {
                    in->elements[s] = more;
                    in->element_room[s] = n;
                }
            }
            if (n <= in->element_room[s]) {
                memcpy(in->elements[s], p->elements[engine].records, (size_t)n * KE_WORDS * sizeof(uint32_t));
                in->element_count[s] = n;
            }
        }
        in->has_left[s] = p->desc2d[engine][KD_LEFT_OUT] != 0;
        if (in->has_left[s]) {
            memcpy(in->left_cells[s], p->left_out_cells[engine], sizeof(in->left_cells[s]));
            memcpy(in->left_obj[s], p->left_out_obj[engine], sizeof(in->left_obj[s]));
        }
    }
}

/* The thread's: a screen's memory as the composition reads it, from the copy. */
static void shop_memory(ShopInput *in, int s, KhdaysVramPages *pages, KhdaysGfxMemory *m)
{
    size_t at = 0;
    memset(pages, 0, sizeof(*pages));
    for (int k = 0; k < 4; ++k) {
        const int a = ENGINE_AREAS[in->engine[s]][k];
        for (int n = 0; n < areas[a].pages; ++n) {
            pages->page[areas[a].area][n] = in->mem[s] + at + (size_t)n * KHDAYS_FRAME_VRAM_PAGE;
        }
        at += (size_t)areas[a].pages * KHDAYS_FRAME_VRAM_PAGE;
    }
    memset(m, 0, sizeof(*m));
    m->vram = pages;
    m->palette = in->palette;
    m->elements = in->element_count[s] != 0 ? in->elements[s] : NULL;
    m->element_texels = element_texels;
    if (in->has_left[s]) {
        m->left_out_cells = in->left_cells[s];
        m->left_out_obj = in->left_obj[s];
    }
}

/* A description for the CPU's composition: the DS's 6 bits a channel, the 3D left out (no KDF_FINE nor
 * KDF_SPLIT; see compose_screen); `bare`: no fade, no blending, what nothing covers magenta, the layers of
 * `without` left out (a bit each) or only layer `only` shown (-1: all). */
static void shop_desc(const uint32_t *from, uint32_t *desc, int fade, int bare, unsigned without, int only)
{
    memcpy(desc, from, KHDAYS_DESC_WORDS * sizeof(uint32_t));
    desc[KD_FLAGS] &= ~(uint32_t)(KDF_FINE | KDF_SPLIT);
    desc[KD_WIDE] = 0;
    if (!fade) {
        desc[KD_MASTER] = 0;
    }
    if (bare) {
        desc[KD_BLEND] = 0;
        desc[KD_BACKDROP] = 0x7c1fu; /* BGR555 magenta */
    }
    for (int l = 0; l < 4; ++l) {
        uint32_t *kind = &desc[KD_LAYERS + l * KD_LAYER_WORDS + KDL_KIND];
        if ((*kind & 0xffu) == KDK_3D || (without & (1u << l)) || (only >= 0 && l != only)) {
            *kind = (*kind & ~0xffu) | KDK_NONE;
        }
    }
}

/* Whether a DS colour (0x00RRGGBB) is one of the shop's stripes' (UI/shop's background: two mustard browns). */
static int stripe_like(uint32_t c)
{
    const uint32_t r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
    return b < 24u && r >= 100u && r <= 165u && g >= 60u && g <= 125u;
}

/* The thread's: the screen's stripes' layers (see above), a bit each; what each showed in `report`. */
static unsigned shop_find_stripes(ShopInput *in, int s, uint32_t *scratch, char *report, size_t size)
{
    static uint32_t desc[KHDAYS_DESC_WORDS];
    KhdaysVramPages pages;
    KhdaysGfxMemory m;
    unsigned found = 0;
    int full = -1;
    size_t at = 0;
    report[0] = '\0';
    shop_memory(in, s, &pages, &m);
    for (int prio = 3; prio >= 0; --prio) {
        for (int l = 3; l >= 0; --l) {
            const uint32_t w = in->desc[s][KD_LAYERS + l * KD_LAYER_WORDS + KDL_KIND];
            const uint32_t kind = w & 0xffu;
            int all = 0, covered = 0, stripes = 0;
            if (kind == KDK_NONE || kind == KDK_3D || ((w >> 8) & 3u) != (uint32_t)prio ||
                !(in->desc[s][KD_FLAGS] & (1u << (KDF_BG_ON_SHIFT + l)))) {
                continue;
            }
            shop_desc(in->desc[s], desc, 0, 1, 0u, l);
            for (int strip = 0; strip < 6; ++strip) {
                const int y0 = 6 + strip * 32;
                khdays_gfx_compose_cpu_rect(desc, &m, scratch, 0, y0, 256, y0 + 4);
                for (int y = y0; y < y0 + 4; ++y) {
                    for (int x = 0; x < 256; ++x) {
                        const uint32_t c = scratch[y * 256 + x] & 0xffffffu;
                        ++all;
                        if (c != SHOP_KEY) {
                            ++covered;
                            stripes += stripe_like(c);
                        }
                    }
                }
            }
            if (stripes * 4 >= all && stripes * 10 >= covered * 6) {
                found |= 1u << l; /* mostly the stripes, over a quarter of the screen or more */
            } else if (covered * 10 >= all * 9 && full < 0) {
                full = l;
            }
            if (at < size) {
                at += (size_t)snprintf(report + at, size - at,
                                       "%s%d (kind %u, priority %d): %d%% covered, %d%% of it "
                                       "the stripes'",
                                       at ? "; " : "", l, kind, prio, covered * 100 / all,
                                       covered ? stripes * 100 / covered : 0);
            }
        }
    }
    return found != 0 ? found : full >= 0 ? 1u << full : 0u;
}

/* What the screen's layers are (their kinds and priorities, sizes, where their tiles and maps are, which
 * are on), not where they are scrolled: the game setting a list up anew changes it. */
static uint64_t layer_setup(const uint32_t *desc)
{
    static const int words[] = {KDL_KIND, KDL_WIDTH, KDL_HEIGHT, KDL_CHARS, KDL_MAP, KDL_BITMAP};
    uint64_t h = 1469598103934665603ull;
    const uint32_t on = desc[KD_FLAGS] & (0xfu << KDF_BG_ON_SHIFT);
    h = hash_bytes(h, &on, sizeof(on));
    for (int l = 0; l < 4; ++l) {
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
            h = hash_bytes(h, &desc[KD_LAYERS + l * KD_LAYER_WORDS + words[i]], sizeof(uint32_t));
        }
    }
    return h;
}

/* The thread's: rectangle `r` (DS dots) of screen `s` composed into `out` (its own pixels only). */
static void shop_rect(const uint32_t *desc, const KhdaysGfxMemory *m, uint32_t *out, const float *r)
{
    const int x0 = r[0] < 0 ? 0 : (int)r[0], y0 = r[1] < 0 ? 0 : (int)r[1];
    const int x1 = r[0] + r[2] > 256 ? 256 : (int)(r[0] + r[2]), y1 = r[1] + r[3] > 192 ? 192 : (int)(r[1] + r[3]);
    if (x1 > x0 && y1 > y0) {
        khdays_gfx_compose_cpu_rect(desc, m, out, x0, y0, x1, y1);
    }
}

/* The thread's: one input made into the screens' pictures. */
static void shop_work(ShopInput *in, ShopOutput *out)
{
    static uint32_t desc[KHDAYS_DESC_WORDS];
    static const float whole[4] = {0, 0, 256, 192};
    const KhdaysShopRequest *r = &in->request;
    out->card = in->card;
    if (in->card) {
        KhdaysVramPages pages;
        KhdaysGfxMemory m;
        shop_memory(in, SHOP_UPPER, &pages, &m);
        shop_desc(in->desc[SHOP_UPPER], desc, 0, 0, 0u, -1);
        if (in->card_slide != 0) {
            /* the page's other view: its four layers scrolled and its sprites moved by the slide
             * (Ov025_TickPageBTransition), put back */
            for (int l = 0; l < 4; ++l) {
                uint32_t *h = &desc[KD_LAYERS + l * KD_LAYER_WORDS + KDL_HOFS];
                *h = (uint32_t)((int)*h - in->card_slide) & 511u;
            }
            for (int s = 0; s < 128; ++s) {
                uint32_t *x = &desc[KD_SPRITES + s * KD_SPRITE_WORDS + KDS_X];
                *x = (uint32_t)((int32_t)*x + in->card_slide);
            }
        }
        shop_rect(desc, &m, out->normal[SHOP_UPPER], whole);
        out->normal_ok[SHOP_UPPER] = 1;
        out->normal_ok[SHOP_LOWER] = out->cut_ok[SHOP_UPPER] = out->cut_ok[SHOP_LOWER] = 0;
        return;
    }
    for (int s = 0; s < 2; ++s) {
        KhdaysVramPages pages;
        KhdaysGfxMemory m;
        /* the upper screen: only its counter and the recipe are shown */
        const float *rects[2] = {s == SHOP_UPPER ? r->header : whole,
                                 s == SHOP_UPPER && r->recipe ? r->recipe_rect : NULL};
        const float *judged = s == SHOP_UPPER && r->recipe ? r->recipe_rect : rects[0];
        const int wanted = r->alone || (s == SHOP_UPPER && r->recipe);
        out->cut_ok[s] = out->normal_ok[s] = 0;
        out->layers[s] = 0u;
        shop_memory(in, s, &pages, &m);
        if (wanted) {
            const uint64_t setup = layer_setup(in->desc[s]);
            if (!shop_bg.layer_known[s] || shop_bg.layer_generation[s] != r->generation ||
                shop_bg.layer_setup[s] != setup) {
                char report[600];
                const unsigned was = shop_bg.layers[s];
                const int another = !shop_bg.layer_known[s] || shop_bg.layer_generation[s] != r->generation;
                shop_bg.layers[s] = shop_find_stripes(in, s, out->cut[s], report, sizeof(report));
                shop_bg.layer_generation[s] = r->generation;
                shop_bg.layer_setup[s] = setup;
                shop_bg.layer_known[s] = 1;
                if (another) {
                    shop_bg.told_thin[s] = 0;
                }
                if (another || shop_bg.layers[s] != was) {
                    khdays_log("shop: the %s screen's layers (list %u, tab %d): %s; left out: %s%s%s%s%s",
                               s == SHOP_UPPER ? "upper" : "lower", r->generation, r->tab, report[0] ? report : "none",
                               shop_bg.layers[s] == 0 ? "none (the screen shown as it is)" : "",
                               shop_bg.layers[s] & 1u ? "0 " : "", shop_bg.layers[s] & 2u ? "1 " : "",
                               shop_bg.layers[s] & 4u ? "2 " : "", shop_bg.layers[s] & 8u ? "3" : "");
                }
            }
            if (shop_bg.layers[s] != 0) {
                int all = 0, ink = 0;
                const int x0 = (int)judged[0], y0 = (int)judged[1];
                const int x1 = (int)(judged[0] + judged[2]), y1 = (int)(judged[1] + judged[3]);
                uint32_t *cut = out->cut[s];
                shop_desc(in->desc[s], desc, 0, 1, shop_bg.layers[s], -1);
                for (int k = 0; k < KHDAYS_FRAME_PIXELS; ++k) {
                    cut[k] = SHOP_KEY;
                }
                for (int i = 0; i < 2; ++i) {
                    if (rects[i] != NULL) {
                        shop_rect(desc, &m, cut, rects[i]);
                    }
                }
                for (int y = y0 < 0 ? 0 : y0; y < (y1 > 192 ? 192 : y1); ++y) {
                    for (int x = x0 < 0 ? 0 : x0; x < (x1 > 256 ? 256 : x1); ++x) {
                        ++all;
                        ink += (cut[y * 256 + x] & 0xffffffu) != SHOP_KEY;
                    }
                }
                if (ink * 33 >= all) {
                    for (int k = 0; k < KHDAYS_FRAME_PIXELS; ++k) {
                        const uint32_t c = cut[k] & 0xffffffu;
                        cut[k] = c == SHOP_KEY ? 0u : 0xff000000u | c;
                    }
                    out->cut_ok[s] = 1;
                    out->layers[s] = shop_bg.layers[s];
                } else if (!shop_bg.told_thin[s]) {
                    shop_bg.told_thin[s] = 1;
                    khdays_log("shop: the %s screen without its stripes keeps %d of %d dots where it is shown: "
                               "shown as it is",
                               s == SHOP_UPPER ? "upper" : "lower", ink, all);
                }
            }
        }
        if (!out->cut_ok[s] || (s == SHOP_UPPER && !r->alone)) {
            /* as the DS shows it (the lower screen with its fades: its list coming and going) */
            shop_desc(in->desc[s], desc, s == SHOP_LOWER, 0, 0u, -1);
            for (int i = 0; i < 2; ++i) {
                if (rects[i] != NULL) {
                    shop_rect(desc, &m, out->normal[s], rects[i]);
                }
            }
            out->normal_ok[s] = 1;
        }
    }
}

static int SDLCALL shop_thread(void *unused)
{
    (void)unused;
    for (;;) {
        ShopInput *in;
        ShopOutput *made;
        SDL_LockMutex(shop_bg.lock);
        while (!shop_bg.pending_new) {
            SDL_WaitCondition(shop_bg.wake, shop_bg.lock);
        }
        in = shop_bg.pending;
        shop_bg.pending = shop_bg.working;
        shop_bg.working = in;
        shop_bg.pending_new = 0;
        SDL_UnlockMutex(shop_bg.lock);
        {
            const Uint64 start = SDL_GetTicksNS();
            double ms;
            shop_work(in, shop_bg.made);
            ms = (double)(SDL_GetTicksNS() - start) / 1e6;
            SDL_LockMutex(shop_bg.lock);
            ++shop_bg.jobs;
            shop_bg.ms += ms;
            shop_bg.most = ms > shop_bg.most ? ms : shop_bg.most;
        }
        made = shop_bg.made;
        shop_bg.made = shop_bg.ready;
        shop_bg.ready = made;
        ++shop_bg.ready_serial;
        SDL_UnlockMutex(shop_bg.lock);
    }
}

static int shop_thread_ready(void)
{
    if (shop_bg.thread != NULL) {
        return 1;
    }
    if (shop_bg.failed) {
        return 0;
    }
    shop_bg.staging = (ShopInput *)calloc(1, sizeof(ShopInput));
    shop_bg.pending = (ShopInput *)calloc(1, sizeof(ShopInput));
    shop_bg.working = (ShopInput *)calloc(1, sizeof(ShopInput));
    shop_bg.made = (ShopOutput *)calloc(1, sizeof(ShopOutput));
    shop_bg.ready = (ShopOutput *)calloc(1, sizeof(ShopOutput));
    shop_bg.taken = (ShopOutput *)calloc(1, sizeof(ShopOutput));
    shop_bg.lock = SDL_CreateMutex();
    shop_bg.wake = SDL_CreateCondition();
    if (shop_bg.staging == NULL || shop_bg.pending == NULL || shop_bg.working == NULL || shop_bg.made == NULL ||
        shop_bg.ready == NULL || shop_bg.taken == NULL || shop_bg.lock == NULL || shop_bg.wake == NULL ||
        (shop_bg.thread = SDL_CreateThread(shop_thread, "shop screens", NULL)) == NULL) {
        SDL_Log("moogle shop: its screens' thread could not start (%s)", SDL_GetError());
        shop_bg.failed = 1;
        return 0;
    }
    return 1;
}

/* The window's thread, each picture of the shop (or of the story's Panels page wanting its upper screen: `card`):
 * a new package's screens handed to the thread when what they show changed; the thread's newest pictures handed to
 * the page. */
static void shop_screens(const KhdaysFramePackage *p, int card, int card_slide)
{
    KhdaysShopRequest r;
    if (!shop_thread_ready()) {
        return;
    }
    memset(&r, 0, sizeof(r));
    if (pixels.p == p && p->frame != shop_bg.last_frame && (card || khdays_shop_screen_request(&r))) {
        const uint64_t hash = shop_signature(p, &r) ^
                              (card ? 0x9e3779b97f4a7c15ull + (uint64_t)(uint32_t)card_slide * 0x100000001b3ull : 0u);
        shop_bg.last_frame = p->frame;
        if (hash == shop_bg.last_hash) {
            ++shop_bg.alike; /* (only this thread writes it) */
        } else {
            ShopInput *in = shop_bg.staging;
            shop_fill(in, p, &r, hash);
            in->card = card;
            in->card_slide = card_slide;
            shop_bg.last_hash = hash;
            SDL_LockMutex(shop_bg.lock);
            shop_bg.staging = shop_bg.pending;
            shop_bg.pending = in;
            shop_bg.pending_new = 1;
            SDL_SignalCondition(shop_bg.wake);
            SDL_UnlockMutex(shop_bg.lock);
        }
    }
    if (shop_bg.ready_serial != shop_bg.taken_serial) {
        ShopOutput *out;
        SDL_LockMutex(shop_bg.lock);
        out = shop_bg.ready;
        shop_bg.ready = shop_bg.taken;
        shop_bg.taken = out;
        shop_bg.taken_serial = shop_bg.ready_serial;
        SDL_UnlockMutex(shop_bg.lock);
        if (out->card) {
            if (card && out->normal_ok[SHOP_UPPER]) {
                khdays_camp_screen_panels_upper(out->normal[SHOP_UPPER]);
            }
            return;
        }
        for (int s = 0; s < 2; ++s) {
            if (out->cut_ok[s]) {
                shop_bg.cut_layers[s] = out->layers[s];
            }
        }
        khdays_shop_screen_results(out->normal_ok[SHOP_UPPER] ? out->normal[SHOP_UPPER] : NULL,
                                   out->normal_ok[SHOP_LOWER] ? out->normal[SHOP_LOWER] : NULL,
                                   out->cut_ok[SHOP_UPPER] ? out->cut[SHOP_UPPER] : NULL,
                                   out->cut_ok[SHOP_LOWER] ? out->cut[SHOP_LOWER] : NULL);
    }
}

void khdays_onescreen_shop_work(unsigned *jobs, double *ms, double *most, unsigned *alike)
{
    *jobs = 0, *ms = *most = 0.0, *alike = shop_bg.alike;
    shop_bg.alike = 0;
    if (shop_bg.lock == NULL) {
        return;
    }
    SDL_LockMutex(shop_bg.lock);
    *jobs = shop_bg.jobs, *ms = shop_bg.ms, *most = shop_bg.most;
    shop_bg.jobs = 0, shop_bg.ms = shop_bg.most = 0.0;
    SDL_UnlockMutex(shop_bg.lock);
}

/* The Moogle Shop on one screen (screens/shop_screen.c): the picture is the screen's animated backdrop -- the
 * DS's stripes, its moogles drifting, the moogle's shadow -- and over it the upper screen's 3D alone, the
 * moogle, moved and scaled into the screen's window for it; nothing of the 2D. The screen draws the rest
 * over it, the game's fade over all (KOB_OWN). It takes the two DS screens as they are composed: the
 * lower's list and the upper's counter and recipe float in it. */
static void shop_window(const KhdaysFramePackage *p, int columns, KhdaysComposition *composition)
{
    KhdaysShopModel m;
    const int on = situation.scene == KHDAYS_SIT_SHOP && khdays_shop_screen_model(&m);
    khdays_shop_screen_applied(on);
    if (!on) {
        return;
    }
    shop_screens(p, 0, 0);
    /* the screens the page shows composed again on the card, its HD tiles and fonts in them (the CPU's have none),
     * the cuts without the layers the thread found */
    khdays_shop_screen_card(composition->shop_screens, composition->shop_fade, &composition->shop_scale);
    composition->shop_cut_layers[SHOP_UPPER] = shop_bg.cut_layers[SHOP_UPPER];
    composition->shop_cut_layers[SHOP_LOWER] = shop_bg.cut_layers[SHOP_LOWER];
    place_model(p, columns, m.source, m.view, m.window);
    composition->screens = KHDAYS_SCREENS_UPPER;
    composition->brightness = (int)KOB_OWN;
    composition->shapes = NULL;
    composition->shape_count = 0;
    composition->key_count = 0;
    composition->shapes3d = &model_shape;
    composition->shape3d_count = 1;
    composition->hide3d_all = 0;
    composition->backdrop = m.atlas;
    composition->backdrop_w = m.atlas_w;
    composition->backdrop_h = m.atlas_h;
    composition->backdrop_ax = composition->backdrop_ay = 0.5f;
    composition->backdrop_fit = 1;
    composition->animated = 1;
    composition->picture_w = 1920;
    composition->picture_h = 1080;
    composition->ground = m.ground;
    composition->pieces = m.pieces;
    composition->piece_count = m.piece_count;
    composition->buttons = NULL;
    composition->button_count = 0;
}

void khdays_onescreen_prepare(const KhdaysFramePackage *p, int columns, KhdaysComposition *composition,
                              KhdaysOneScreenDraw *draw)
{
    if ((p->wants & KHDAYS_WANT_SITUATION) && p->situation.taken && (p->wants & KHDAYS_WANT_2D)) {
        KhdaysSituationFrame f;
        KhdaysSituation now;
        make_pixels(p);
        f.in = &p->situation;
        if (p->frame != decided_package) {
            f.alike = p->alike > alike_taken ? p->alike - alike_taken : 0u;
            decided_package = p->frame;
            alike_taken = 0u;
        } else {
            f.alike = 0u;
            ++alike_taken;
        }
        f.polygons = p->list3d.npolys;
        f.vertices = p->list3d.nverts;
        f.pixel = pixel;
        f.ctx = &pixels;
        memset(&now, 0, sizeof(now));
        khdays_situation_decide(&f, &now);
        title_menu(&p->situation, &now);
        trace_title(p, &now);
        if (khdays_situation_hud_mode() == KHDAYS_HUD_COMPLETE) {
            /* the HUD's exploration mode leaves the INFORMATION banner out */
            info_lower(&now);
            info_new(p, &now);
        }
        mission_panel(&now);
        lower_shown(&p->situation, &now);
        screens_content(&now);
        if (!told || now.scene != situation.scene || now.states != situation.states) {
            situation = now;
            told = 1;
            SDL_SetAtomicInt(&scene_shown, situation.scene);
            SDL_SetAtomicInt(&states_shown, (int)situation.states);
            tell(p->frame);
        } else {
            situation.title_menu = now.title_menu;
            situation.title_count = now.title_count;
            situation.title_row = now.title_row;
            situation.title_lang = now.title_lang;
            situation.content = now.content;
        }
    }
    situation.frame =
        khdays_events_vblank_count(); /* an animated backdrop's clock: it moves when the frames are alike */
    settings_prompt();
    khdays_composition_for(&situation, columns, composition);
    model_window(p, columns, composition);
    shop_window(p, columns, composition);
    game_over_orb(composition);
    if (traced_frame(p->frame) && pixels.p == p &&
        (composition->screens == KHDAYS_SCREENS_UPPER || composition->screens == KHDAYS_SCREENS_LOWER)) {
        tell_left_out(p, composition);
    }
    draw->shapes = composition->shape_count != 0 ? composition->shapes[0].words : NULL;
    draw->shape_count = composition->shape_count;
    draw->keys = composition->keys;
    draw->key_count = composition->key_count;
    draw->frame = composition->screens == KHDAYS_SCREENS_LOWER ? (int)KOS_LOWER : (int)KOS_UPPER;
    draw->rule = composition->brightness;
    draw->columns = columns;
    draw->backdrop = composition->backdrop;
    draw->backdrop_w = composition->backdrop_w;
    draw->backdrop_h = composition->backdrop_h;
    draw->backdrop_ax = composition->backdrop_ax;
    draw->backdrop_ay = composition->backdrop_ay;
    draw->backdrop_fit = composition->backdrop_fit;
    draw->hide3d_all = composition->hide3d_all;
    draw->lower_whole = composition->lower_whole;
    for (int s = 0; s < 2; ++s) {
        draw->shop_screens[s] = composition->shop_screens[s];
        draw->shop_cut_layers[s] = composition->shop_cut_layers[s];
        draw->shop_fade[s] = composition->shop_fade[s];
        draw->shop_with3d[s] = composition->shop_with3d[s];
        draw->shop_slide[s] = composition->shop_slide[s];
    }
    draw->shop_scale = composition->shop_scale;
    draw->animated = composition->animated;
    draw->picture_w = composition->picture_w;
    draw->picture_h = composition->picture_h;
    draw->ground = composition->ground;
    draw->pieces = composition->pieces;
    draw->piece_count = composition->piece_count;
}

int khdays_onescreen_settling(const KhdaysFramePackage *package)
{
    /* standing still: the game has gone past the VBlank after it without another package */
    return package->frame == decided_package && khdays_situation_settling() &&
           khdays_events_vblank_count() > package->frame + 1u;
}

int khdays_onescreen_makes_picture(const KhdaysFramePackage *p)
{
    if (situation.scene != KHDAYS_SIT_FIELD_3D_BOTH) {
        return 1;
    }
    /* p->upper is the engine on the upper screen; 0 is engine A, the one with the 3D: its 3D is the
     * upper screen's view */
    return p->upper == 0;
}
