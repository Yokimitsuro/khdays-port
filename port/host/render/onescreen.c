/* The one-screen picture, on the presenting side (see onescreen.h). */
#include "onescreen.h"
#include "../../ui/hud_options.h"
#include "onescreen_images.h"
#include "../../gfx/describe.h"
#include "../screens/camp_screen.h"
#include "../app/devices.h"
#include "../../runtime/menus/newgame.h"
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
/* Information visibility follows native text changes, independently of animated replacement pixels. */
static uint32_t information_pixel(int screen, int x, int y)
{
    const int engine = screen == 0 ? pixels.p->upper : !pixels.p->upper;
    uint32_t desc[KHDAYS_DESC_WORDS];
    memcpy(desc, pixels.desc[engine], sizeof(desc));
    desc[KD_ELEMENTS] = desc[KD_LEFT_OUT] = 0;
    return khdays_gfx_pixel_cpu(desc, &pixels.memory[engine], x, y);
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
        if (!near_white(information_pixel(1, below[i], 23))) {
            return;
        }
    }
    for (int i = 0; i < (int)(sizeof(above) / sizeof(above[0])); ++i) {
        if (!near_white(information_pixel(1, above[i], 8))) {
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
    const uint32_t ground = information_pixel(1, 6, 15);
    for (int y = 11; y <= 19; ++y) {
        for (int x = 8; x < 224; ++x) {
            const uint32_t c = information_pixel(1, x, y);
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
    const unsigned duration = p->information_hold_frames ? p->information_hold_frames : INFO_FRAMES;
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
        changed = p->frame - duration;
        return;
    }
    for (int y = 10; y <= 20; y += 2) {
        for (int x = 4; x < 252; ++x) {
            sum = (sum ^ (information_pixel(1, x, y) & 0xffffffu)) * 16777619u;
        }
    }
    if (after_start) {
        if (!seen || sum != last_sum) {
            seen = 1;
            last_sum = sum;
            still_since = p->frame;
        }
        changed = p->frame - duration;
        after_start = p->frame - still_since < INFO_SETTLE_FRAMES;
    } else if (!seen || sum != last_sum) {
        seen = 1;
        last_sum = sum;
        changed = p->frame;
    }
    if (p->frame - changed < duration) {
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
    {
        const float c = (float)columns;
        const float k = c / 1920.0f < 192.0f / 1080.0f ? c / 1920.0f : 192.0f / 1080.0f; /* layout -> dots */
        const float ox = (c - 1920.0f * k) / 2.0f, oy = (192.0f - 1080.0f * k) / 2.0f;
        const float *src = m.source, *view = m.view, *dst = m.window;
        const float scale = (dst[2] / view[2] < dst[3] / view[3] ? dst[2] / view[2] : dst[3] / view[3]) * k;
        /* the 3D's polygons are where the game draws them before its 3D layer's scroll (BG0HOFS: a screen dot
         * x shows the 3D's x + scroll), in the rectangle where the screen shows them */
        const int hofs = (int)(p->info[0].bg0_hofs & 511u);
        model_shape.group = KHDAYS_SHAPE3D_ALL;
        model_shape.hide = 0;
        /* the whole model: a long weapon's polygons with their middle outside the DS's box (Marluxia's
         * scythe's blade) went missing; the camp screen's backdrop round the window cuts it there */
        model_shape.whole = 1;
        model_shape.src_x = src[0] + (float)(hofs >= 256 ? hofs - 512 : hofs);
        model_shape.src_y = src[1];
        model_shape.src_w = src[2];
        model_shape.src_h = src[3];
        model_shape.scale = scale;
        /* the view centred in the window (the source's corner where that puts it) */
        model_shape.x = ox + dst[0] * k + (dst[2] * k - view[2] * scale) / 2.0f - (view[0] - src[0]) * scale;
        model_shape.y = oy + dst[1] * k + (dst[3] * k - view[3] * scale) / 2.0f - (view[1] - src[1]) * scale;
    }
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
        const int information_mod = khdays_onescreen_information(p, &now);
        title_menu(&p->situation, &now);
        trace_title(p, &now);
        if (khdays_situation_hud_mode() == KHDAYS_HUD_COMPLETE) {
            /* the HUD's exploration mode leaves the INFORMATION banner out */
            info_lower(&now);
            if (!information_mod)
                info_new(p, &now);
        }
        mission_panel(&now);
        if (p->mission_panel_state & 1u) {
            now.states |= 1u << KHDAYS_ST_MISSION_PANEL_MOD;
            if (p->mission_panel_state & 2u)
                now.states |= 1u << KHDAYS_ST_MISSION_PANEL;
        }
        lower_shown(&p->situation, &now);
        screens_content(&now);
        if (!told || now.scene != situation.scene || now.states != situation.states) {
            situation = now;
            told = 1;
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
    khdays_composition_set_hud_options(p->hud_options | ((p->mission_panel_state & 4u) ? KHDAYS_HUD_PANEL_HIDDEN : 0u));
    khdays_composition_for(&situation, columns, composition);
    model_window(p, columns, composition);
    khdays_onescreen_images(p, &situation, composition);
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
