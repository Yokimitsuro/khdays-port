/* The Menu Overhaul's pause (modern_pause.h), drawn as Kingdom Hearts II's: its pause.2ld's sequence and sheet
 * (that mod's layouts/kh2pause, from KH2's menu/<lang>/pause.2ld) played as the PS2 game plays it -- PAUSE flying
 * in, its E flipping back with a jump, the bands, the capsules, the bobbing glove -- with the pause's own words;
 * KH2's blue lines drawn white and grey. Without those files, a plainer pause of layouts/pause's pieces. */
#include "modern_pause.h"
#include "game_pause.h"
#include "../render/png_io.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/core/rom.h"
#include "../../runtime/menus/retry.h"
#include "../../runtime/mods/installed.h"
#include "../app/devices.h"
#include "../app/profile.h"
#include "../settings/settings_ui.h"
#include "kh2_seq.h"
#include "layout_screen.h"
#include "ui_text.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------------------------------
 * KH2's sequence: pause.2ld's (kh2_seq.h). */

static KhdaysKh2Seq seq;
static int seq_tried, seq_ok;

/* The sheet's size in the PS2 game (pause.2ld's IMGZ picture), the unit of the sprites' rectangles. */
#define SHEET_UNITS 64.0f

/* The pause's animation groups in pause.2ld. */
enum {
    AG_TITLE = 15,     /* PAUSE: its band, the letters flying in, the E's flip */
    AG_BAND = 14,      /* the band behind the choices */
    AG_FOCUSED = 11,   /* a capsule with the focus: red */
    AG_CAPSULE = 12,   /* one without */
    AG_GLOVE = 6,      /* the pointing glove and its shadow */
    AG_FIREFLY_IN = 7, /* the firefly growing in */
    AG_FIREFLY = 8     /* the firefly wandering (8 each way, 26 and 34 swings in 1500 frames) */
};

/* KH2's screen (512 x 416) and the pause's places in it. */
#define KH2_W 512.0f
#define KH2_H 416.0f
#define PAUSE_X 256.0f
#define TITLE_Y 150.0f
#define BAND_Y 250.0f
#define BAND_TOP 47.0f /* the band's half height (its sprite group) */
#define ROW_STEP 40.0f
#define CAPSULE_LEFT (-78.0f)
#define CAPSULE_RIGHT 81.0f
#define CAPSULE_HALF 15.0f
#define FIREFLY_X (CAPSULE_RIGHT - 8.0f) /* the firefly's middle: the capsule's right end */
#define FRAMES_PER_SECOND 60.0

/* The rows' places: two 40 apart in the band, three 36 apart in it drawn taller. */
static float row_at(int rows, int row)
{
    const float step = rows >= 3 ? 36.0f : ROW_STEP;
    return BAND_Y + ((float)row - (float)(rows - 1) / 2.0f) * step;
}

static float row_y(int row)
{
    return row_at(2, row);
}

/* KH2's blues (the lines' 52 1B CC, the unfocused capsule's 00 00 80) as greys: a line light, an
 * edge darker. */
static uint32_t grey_for_blue(uint32_t c)
{
    const uint32_t r = c & 0xFFu, g = c >> 8 & 0xFFu, b = c >> 16 & 0xFFu;
    if (b >= 0x40u && b > r + 0x30u && b > g + 0x30u) {
        const uint32_t grey = r == 0 && g == 0 ? 0x70u : 0x90u;
        return (c & 0xFF000000u) | grey << 16 | grey << 8 | grey;
    }
    return c;
}

static int seq_load(void)
{
    if (!seq_tried) {
        seq_tried = 1;
        seq_ok = khdays_kh2_seq_load(&seq, "layouts/kh2pause/pause.seqd", SHEET_UNITS, grey_for_blue) &&
                 seq.agroup_count > AG_TITLE;
    }
    return seq_ok;
}

/* ---------------------------------------------------------------------------------------------------
 * Pictures. */

static const char *const codes[] = {"en", "en", "fr", "de", "it", "es"};

/* The KH2 sheet as it is and with its colours doubled: a PS2 tint goes to twice the texel (0x80 is
 * 1.0), which a vertex colour cannot, so a sprite tinted past 1.0 is drawn from the doubled sheet with
 * half its tint. Then the capsules' words and the question. */
enum {
    PIC_SHEET,
    PIC_SHEET_BRIGHT,
    PIC_CONTINUE,
    PIC_SKIP,
    PIC_YES,
    PIC_NO,
    PIC_QUESTION,
    PIC_WITHDRAW,
    PIC_RETRY,
    PIC_GO_CONTINUE,
    PIC_GO_EASIER,
    PIC_GO_WITHDRAW,
    PIC_COUNT
};
static SDL_Texture *kh2[6][PIC_COUNT];
static char kh2_missing[6][PIC_COUNT];
/* the plainer pause's pieces */
static SDL_Texture *pictures[6][6];
static char pictures_missing[6][6];
static const char *const files[] = {"title", "continue", "continue_lit", "skip", "skip_lit", "hand"};

static void settings_key_forget(void);

void khdays_modern_pause_renderer_gone(void)
{
    for (int l = 0; l < 6; ++l) {
        for (int p = 0; p < 15; ++p) {
            SDL_DestroyTexture(pictures[l][p]);
            pictures[l][p] = NULL;
            pictures_missing[l][p] = 0;
        }
        for (int p = 0; p < PIC_COUNT; ++p) {
            SDL_DestroyTexture(kh2[l][p]);
            kh2[l][p] = NULL;
            kh2_missing[l][p] = 0;
        }
    }
    settings_key_forget();
}

/* A mod's picture as a texture (NULL if none); `bright` doubles its colours. */
static SDL_Texture *load_picture(SDL_Renderer *renderer, const char *relative, int bright)
{
    return khdays_kh2_picture(renderer, relative, bright);
}

static SDL_Texture *kh2_picture(SDL_Renderer *renderer, int lang, int which)
{
    static const char *const names[] = {"sheet",    "sheet",    "continue", "skip",        "yes",       "no",
                                        "question", "withdraw", "retry",    "go_continue", "go_easier", "go_withdraw"};
    if (kh2[lang][which] == NULL && !kh2_missing[lang][which]) {
        char relative[160];
        snprintf(relative, sizeof(relative), "layouts/kh2pause/%s/%s.png", codes[lang], names[which]);
        kh2[lang][which] = load_picture(renderer, relative, which == PIC_SHEET_BRIGHT);
        kh2_missing[lang][which] = kh2[lang][which] == NULL; /* looked for once, not at each picture */
    }
    return kh2[lang][which];
}

static SDL_Texture *texture(SDL_Renderer *renderer, int lang, int which)
{
    if (pictures[lang][which] == NULL && !pictures_missing[lang][which]) {
        char relative[160];
        snprintf(relative, sizeof(relative), "layouts/pause/%s/%s.png", codes[lang], files[which]);
        if (which == 5)
            snprintf(relative, sizeof(relative), "layouts/pause/hand.png");
        pictures[lang][which] = load_picture(renderer, relative, 0);
        pictures_missing[lang][which] = pictures[lang][which] == NULL;
    }
    return pictures[lang][which];
}

/* ---------------------------------------------------------------------------------------------------
 * A sequence's animation group drawn at a point of KH2's screen (OpenKh's SequenceRenderer), on the
 * 1920 x 1080 picture. */

typedef KhdaysKh2Stage Stage;

static void draw_group(const Stage *st, int index, int frame, float x, float y)
{
    khdays_kh2_seq_draw(st, &seq, index, frame, x, y);
}

/* A word's picture (made for the 1080 picture) centred on a point of KH2's screen. */
static void draw_word(const Stage *st, SDL_Texture *t, float x, float y, Uint8 alpha)
{
    float w, h;
    if (t == NULL || !SDL_GetTextureSize(t, &w, &h))
        return;
    const SDL_FRect at = {SDL_roundf(st->left + x * st->scale - w / 2), SDL_roundf(y * st->scale - h / 2), w, h};
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_RenderTexture(st->renderer, t, NULL, &at);
}

/* The pause's opening (for its animation's frames), seen as it opens. */
static Uint64 opened_at;
static int was_open;

static int word_picture(int key)
{
    switch (key) {
    case KHDAYS_PAUSE_CONTINUE: return PIC_CONTINUE;
    case KHDAYS_PAUSE_SKIP: return PIC_SKIP;
    case KHDAYS_PAUSE_WITHDRAW: return PIC_WITHDRAW;
    case KHDAYS_PAUSE_RETRY: return PIC_RETRY;
    case KHDAYS_PAUSE_YES: return PIC_YES;
    case KHDAYS_PAUSE_NO: return PIC_NO;
    case KHDAYS_PAUSE_GO_CONTINUE: return PIC_GO_CONTINUE;
    case KHDAYS_PAUSE_GO_EASIER: return PIC_GO_EASIER;
    case KHDAYS_PAUSE_GO_WITHDRAW: return PIC_GO_WITHDRAW;
    default: return -1;
    }
}

/* KH2's pause: the title (with `title`); `rows` (0-3) rows of the words `keys` (KHDAYS_PAUSE_*), `row`
 * lit; the question over them; the row `dimmed` (or -1) drawn faint. What is behind is darkened by
 * `shade` (0-255). */
static int draw_kh2(SDL_Renderer *renderer, int lang, int rows, const int *keys, int row, int question, int dimmed,
                    int title, Uint8 shade)
{
    Stage st;
    if (!seq_load())
        return 0;
    khdays_kh2_stage(&st, renderer, kh2_picture(renderer, lang, PIC_SHEET),
                     kh2_picture(renderer, lang, PIC_SHEET_BRIGHT), 1080.0f / KH2_H,
                     (1920.0f - KH2_W * (1080.0f / KH2_H)) / 2.0f, 0.0f);
    if (st.sheet == NULL)
        return 0;
    const int frame = (int)((double)(SDL_GetTicks() - opened_at) * FRAMES_PER_SECOND / 1000.0);
    const float band_stretch = rows >= 3 ? 1.32f : 1.0f;

    /* what is behind darkened by half, as KH2's (group 0: black at 0x40 of 0x80) */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, shade);
    SDL_RenderFillRect(renderer, NULL);

    if (title) {
        draw_group(&st, AG_TITLE, frame, PAUSE_X, TITLE_Y);
    }
    if (question) {
        draw_word(&st, kh2_picture(renderer, lang, PIC_QUESTION), PAUSE_X, BAND_Y - BAND_TOP * band_stretch - 12.0f,
                  255);
    }
    if (rows <= 0)
        return 1; /* the title alone (a scene the game lets no one skip) */
    if (rows > 3)
        rows = 3;
    if (row < 0 || row >= rows)
        row = 0;
    st.stretch = band_stretch;
    if (title) {
        /* (the game over's capsules alone: no band, as no title) */
        draw_group(&st, AG_BAND, frame, PAUSE_X, BAND_Y - 33.0f); /* (its group is placed 33 below) */
    }
    st.stretch = 1.0f;
    for (int n = 0; n < rows; ++n) {
        const int word = word_picture(keys[n]);
        draw_group(&st, n == row ? AG_FOCUSED : AG_CAPSULE, frame, PAUSE_X, row_at(rows, n));
        if (word >= 0) {
            draw_word(&st, kh2_picture(renderer, lang, word), PAUSE_X, row_at(rows, n), n == dimmed ? 90 : 255);
        }
    }
    draw_group(&st, AG_GLOVE, frame, PAUSE_X, row_at(rows, row));
    {
        /* KH2's firefly at the lit capsule's right end: it grows in (group 7) as the menu opens, then
         * wanders (group 8), its light added; when the focus moves it flies over to the new capsule */
        static Uint64 fly_open, fly_born, fly_at;
        static float fly_y;
        const Uint64 now = SDL_GetTicks();
        const float to = row_at(rows, row);
        int f;
        if (fly_open != (Uint64)opened_at) {
            fly_open = (Uint64)opened_at;
            fly_born = fly_at = now;
            fly_y = to;
        } else {
            /* eased toward its row, fast then gently, whatever the display's rate */
            const float dt = (float)(now - fly_at) / 1000.0f;
            fly_y = to + (fly_y - to) * expf(-dt * 18.0f);
            if (fabsf(fly_y - to) < 0.05f)
                fly_y = to;
            fly_at = now;
        }
        f = (int)((double)(now - fly_born) * FRAMES_PER_SECOND / 1000.0);
        if (f < 16) {
            draw_group(&st, AG_FIREFLY_IN, f, PAUSE_X + FIREFLY_X, fly_y);
        } else {
            draw_group(&st, AG_FIREFLY, f - 16, PAUSE_X + FIREFLY_X, fly_y);
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------------------
 * The plainer pause, without KH2's files. */

static void draw(SDL_Renderer *r, int lang, int which, float x, float y, float w, float h)
{
    SDL_Texture *t = texture(r, lang, which);
    const SDL_FRect at = {x, y, w, h};
    if (t)
        SDL_RenderTexture(r, t, NULL, &at);
}

#define PLAIN_TOP 460.0f /* the plain panel's first capsule */

static void draw_plain(SDL_Renderer *renderer, int language, int row, int can_skip)
{
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(renderer, NULL);
    draw(renderer, language, 0, 768, 290, 384, 96);
    const float top = PLAIN_TOP;
    for (int n = 0; n < 2; ++n) {
        int which = 1 + n * 2;
        if (row == n)
            ++which;
        SDL_Texture *t = texture(renderer, language, which);
        if (t)
            SDL_SetTextureAlphaMod(t, n == 1 && !can_skip ? 90 : 255);
        draw(renderer, language, which, 696, top + n * 112.0f, 528, 96);
    }
    draw(renderer, language, 5, 676 + khdays_layout_glove_sway(), top + row * 112.0f - 12, 120, 90);
}

/* ---------------------------------------------------------------------------------------------------
 * The settings key at the top right of a pause: the camp menu's -- its gear and the input that
 * opens the settings, Esc or the pad's -- from layouts/camp/<lang>/camp.txt, at its place `settings`.
 * The settings menu opens from the pause (window.c); a click on the key opens it too. */

#define SETTINGS_GEAR 0.62f /* the gear's height: this much of the place's (the camp's) */
#define SETTINGS_KEY 0.6f   /* the key's */
#define KEY_SHOWN_MS 250    /* the key drawn this lately takes a click */

static struct {
    SDL_Renderer *renderer;
    int language; /* the one read (0 none) */
    int ok;
    KhdaysLayout layout;
    SDL_FRect at; /* drawn there last (the 1920 x 1080 picture's units) */
    Uint64 drawn_at;
} sk;

static void settings_key_forget(void)
{
    khdays_layout_forget(&sk.layout);
    sk.renderer = NULL;
    sk.language = 0;
    sk.ok = 0;
    sk.drawn_at = 0;
}

static int key_load(SDL_Renderer *renderer, int language)
{
    char relative[64], path[1024], folder[1024];
    char *text, *next, *slash;
    size_t size = 0;
    if (sk.renderer == renderer && sk.language == language) {
        return sk.ok;
    }
    khdays_layout_forget(&sk.layout);
    sk.renderer = renderer;
    sk.language = language;
    sk.ok = 0;
    sk.layout.who = "pause's settings key";
    sk.layout.renderer = renderer;
    snprintf(relative, sizeof(relative), "layouts/camp/%s/camp.txt", codes[language]);
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
    for (char *line = text; line != NULL && *line != '\0'; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (*line != '#') {
            khdays_layout_line(&sk.layout, line, folder);
        }
    }
    SDL_free(text);
    khdays_layout_ready(&sk.layout);
    sk.ok = sk.layout.atlas != NULL && khdays_layout_piece(&sk.layout, "settings_gear") != NULL &&
            khdays_layout_piece(&sk.layout, "settings_esc") != NULL &&
            khdays_layout_place(&sk.layout, "settings") != NULL;
    return sk.ok;
}

/* Drawn in the picture's units (the renderer's presentation 1920 x 1080). */
static void draw_settings_key(SDL_Renderer *renderer, int language)
{
    const KhdaysLayoutFrame fr = {1.0f, 0.0f, 0.0f, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED};
    const KhdaysLayoutPlace *at;
    const KhdaysLayoutPiece *gear, *esc;
    float h, key_h, middle, x, w;
    if (khdays_settings_is_open() || !key_load(renderer, language)) {
        return;
    }
    at = khdays_layout_place(&sk.layout, "settings");
    gear = khdays_layout_piece(&sk.layout, "settings_gear");
    esc = khdays_layout_piece(&sk.layout, "settings_esc");
    h = at->h * SETTINGS_GEAR;
    key_h = at->h * SETTINGS_KEY;
    middle = at->y + at->h / 2;
    x = at->x - gear->w * h / gear->h;
    khdays_layout_draw_piece(&sk.layout, &fr, "settings_gear", x, middle - h / 2, gear->w * h / gear->h, h, 1.0f);
    if (khdays_devices_gamepad() != NULL && khdays_devices_pad_in_use()) {
        w = khdays_layout_draw_action(&sk.layout, &fr, KHDAYS_ACT_OPEN_SETTINGS, 0, 0, key_h, 0.0f);
        khdays_layout_draw_action(&sk.layout, &fr, KHDAYS_ACT_OPEN_SETTINGS, x - w - at->h * 0.05f, middle - key_h / 2,
                                  key_h, 1.0f);
    } else {
        w = esc->w * key_h / esc->h;
        khdays_layout_draw_piece(&sk.layout, &fr, "settings_esc", x - w - at->h * 0.05f, middle - key_h / 2, w, key_h,
                                 1.0f);
    }
    sk.at.x = x - w - at->h * 0.05f;
    sk.at.y = at->y;
    sk.at.w = at->x - sk.at.x;
    sk.at.h = at->h;
    sk.drawn_at = SDL_GetTicks();
}

int khdays_movie_pause_settings_at(SDL_Renderer *renderer, float wx, float wy)
{
    int lw, lh;
    float x, y;
    SDL_RendererLogicalPresentation mode;
    if (sk.drawn_at == 0 || SDL_GetTicks() - sk.drawn_at > KEY_SHOWN_MS || khdays_settings_is_open()) {
        return 0;
    }
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &x, &y);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
    return x >= sk.at.x && x <= sk.at.x + sk.at.w && y >= sk.at.y && y <= sk.at.y + sk.at.h;
}

static int kh2_drawn; /* the last picture was KH2's (the mouse's capsules are its) */

/* The game's pause as the pause part told it (game_pause.h): when it was told (ms, 0 never), and
 * shown | rows << 4 | row << 8 | language << 12. And whether this window draws KH2's pause. */
static SDL_AtomicInt game_told_at, game_state, kh2_ready;
#define GAME_TOLD_FRESH_MS                                                                                             \
    3000 /* a pause not told of for this long is no longer up (the paused game
                                  * may send a picture a second; its closing is told at once) */

int khdays_game_pause_kh2_ready(void)
{
    return SDL_GetAtomicInt(&kh2_ready) && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL);
}

/* A menu told by the game's thread: shown | rows << 1 | row << 3 | question << 5 | language << 6 | each
 * key's 4 bits from 9; and when it was told (ms, 0 never). */
static SDL_AtomicInt over_told_at, over_state;

static void tell(SDL_AtomicInt *state_out, SDL_AtomicInt *at_out, int shown, int rows, const int *keys, int row,
                 int question, int language)
{
    int state = (shown != 0) | (rows & 3) << 1 | (row & 3) << 3 | (question != 0) << 5 | (language & 7) << 6;
    for (int n = 0; n < 3; ++n)
        state |= ((n < rows && keys != NULL ? keys[n] : 0) & 15) << (9 + 4 * n);
    SDL_SetAtomicInt(state_out, state);
    SDL_SetAtomicInt(at_out, (int)(SDL_GetTicks() & 0x7fffffff) | 1);
}

static int told(SDL_AtomicInt *state_in, SDL_AtomicInt *at_in, int *rows, int *keys, int *row, int *question,
                int *language)
{
    const int at = SDL_GetAtomicInt(at_in), state = SDL_GetAtomicInt(state_in);
    if (at == 0 || !(state & 1) || (int)(SDL_GetTicks() & 0x7fffffff) - at > GAME_TOLD_FRESH_MS)
        return 0;
    *rows = state >> 1 & 3;
    *row = state >> 3 & 3;
    *question = state >> 5 & 1;
    *language = state >> 6 & 7;
    for (int n = 0; n < 3; ++n)
        keys[n] = state >> (9 + 4 * n) & 15;
    return 1;
}

void khdays_game_pause_publish(int shown, int rows, const int *keys, int row, int question, int language)
{
    tell(&game_state, &game_told_at, shown, rows, keys, row, question, language);
}

void khdays_game_over_publish(int shown, int rows, const int *keys, int row, int question, int language)
{
    tell(&over_state, &over_told_at, shown, rows, keys, row, question, language);
}

static int game_pause_up(int *rows, int *keys, int *row, int *question, int *language)
{
    return told(&game_state, &game_told_at, rows, keys, row, question, language);
}

static int game_over_up(int *rows, int *keys, int *row, int *question, int *language)
{
    return told(&over_state, &over_told_at, rows, keys, row, question, language);
}

int khdays_game_pause_kh2_showing(void)
{
    int rows, keys[3], row, question, language;
    return SDL_GetAtomicInt(&kh2_ready) && (game_pause_up(&rows, keys, &row, &question, &language) ||
                                            game_over_up(&rows, keys, &row, &question, &language));
}

void khdays_movie_pause_draw(SDL_Renderer *renderer)
{
    int row, can_skip, lw, lh, rows = 2, language, question = 0, dimmed = -1;
    int keys[3] = {KHDAYS_PAUSE_CONTINUE, KHDAYS_PAUSE_SKIP, 0};
    SDL_RendererLogicalPresentation mode;
    const int movie = khdays_movie_pause_menu(&row, &can_skip);
    const int lang = khdays_language_choose(khdays_devices_profile()->language);
    language = lang >= 1 && lang <= 5 ? lang : 1;
    /* KH2's pause for the game's own: on one picture (the overlay covers the window), with its files */
    SDL_SetAtomicInt(&kh2_ready, khdays_devices_profile()->video.one_screen && seq_load() &&
                                     kh2_picture(renderer, language, PIC_SHEET) != NULL);
    int game = 0;
    if (movie) {
        if (!can_skip) {
            dimmed = 1;
        }
    } else if (SDL_GetAtomicInt(&kh2_ready) && game_pause_up(&rows, keys, &row, &question, &language)) {
        game = 1;
        if (language < 1 || language > 5)
            language = 1;
    } else if (SDL_GetAtomicInt(&kh2_ready) && game_over_up(&rows, keys, &row, &question, &language)) {
        game = 2; /* the game over's menu: no PAUSE, its picture behind darkened less */
        if (language < 1 || language > 5)
            language = 1;
    }
    if (!movie && !game) {
        was_open = 0;
        return;
    }
    if (!was_open) {
        opened_at = SDL_GetTicks(); /* PAUSE flies in at each opening */
        was_open = 1;
    }
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    kh2_drawn = draw_kh2(renderer, language, rows, keys, row, question, dimmed, game != 2, game == 2 ? 72 : 128);
    if (!kh2_drawn && movie)
        draw_plain(renderer, language, row, can_skip);
    draw_settings_key(renderer, language);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}

int khdays_movie_pause_event(SDL_Renderer *renderer, const SDL_Event *event)
{
    /* the capsule the pointer was last over (-1 none), forgotten when the pause opens again */
    static int hovered = -1, was_open_here;
    int row, skip, lw, lh;
    SDL_RendererLogicalPresentation mode;
    const int open = khdays_movie_pause_menu(&row, &skip);
    if (open && !was_open_here)
        hovered = -1;
    was_open_here = open;
    if (!open)
        return 0;
    if (event->type != SDL_EVENT_MOUSE_MOTION && event->type != SDL_EVENT_MOUSE_BUTTON_DOWN)
        return 0;
    float x, y;
    const float wx = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.x : event->button.x;
    const float wy = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.y : event->button.y;
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &x, &y);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
    /* the capsule under the pointer: a move onto one takes the focus (only on entering it, so a resting
     * mouse never undoes the pad's choice); a click on one chooses it */
    int over = -1;
    for (int n = 0; n < 2; ++n) {
        int inside;
        if (kh2_drawn) {
            const float s = 1080.0f / KH2_H, left = (1920.0f - KH2_W * s) / 2.0f;
            inside = x >= left + (PAUSE_X + CAPSULE_LEFT) * s && x <= left + (PAUSE_X + CAPSULE_RIGHT) * s &&
                     y >= (row_y(n) - CAPSULE_HALF) * s && y <= (row_y(n) + CAPSULE_HALF) * s;
        } else {
            inside = x >= 696 && x <= 1224 && y >= PLAIN_TOP + n * 112 && y <= PLAIN_TOP + n * 112 + 96;
        }
        if (inside)
            over = n;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (over >= 0 && over != hovered)
            khdays_movie_pause_focus(over, 0);
        hovered = over;
        return 1;
    }
    if (event->button.button == SDL_BUTTON_LEFT && over >= 0) {
        khdays_movie_pause_focus(over, 1);
        return 1;
    }
    /* a click elsewhere is the game's button bound to it (the player's A may be the left button) */
    return 0;
}
