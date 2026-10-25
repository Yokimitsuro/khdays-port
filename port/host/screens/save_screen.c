/* The port's save list on the screen (save_screen.h), in DDD's save screen pieces: a file
 * tools/hd/save_screen.py writes (docs/HD_SCREENS.md, "The save list"). Days's own words are read as
 * the game reads them, their DS buttons drawn as the player's inputs' icons; the glove points at the
 * selected row. With the guest's row (Mission Mode's) Y plays as the guest from anywhere. */
#include "save_screen.h"

#include "camp_screen.h"
#include "layout_screen.h"
#include "ui_text.h"
#include "../../runtime/core/rom.h"
#include "../../runtime/menus/savelist.h"
#include "../../runtime/mods/installed.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the game's strings (see above) */
enum {
    LRD_TITLE = 0,
    LRD_KEYS = 1,
    LRD_DELETE_KEY = 2,
    LRD_YES = 7,
    LRD_NO = 8,
    LRD_TIME = 11,
    LRD_LEVEL = 12,
    LRD_BROKEN = 13
};
enum { STRINGS_LRD, STRINGS_SAV, STRINGS_NGM, STRINGS_INFO, STRINGS_MLT, STRINGS_FILES };
enum { SAV_TITLE = 0, SAV_NOTE = 1, SAV_YES = 5, SAV_NO = 6 };
#define NGM_DIFFICULTY 2
#define INFO_DAY 0
#define MLT_KEYS 63

static struct {
    int tried, ok; /* the language last tried, and whether its pieces were there */
    KhdaysLayout layout;
    float rows[4];
    float row_pitch;
    int rows_shown;
    float scroll[2];
    float pitch_ask, pitch_tell;
    unsigned short empty[64], guest[64];
    KhdaysLayoutStrings strings[STRINGS_FILES];
    int top; /* the first row shown */
    /* with the Menu Overhaul, the motion: the list's top as it scrolls (in rows), the glove's middle, each
     * row's red (0-1), the screen shown last picture and when */
    float view_top, glove_y, lit[KHDAYS_SLOT_COUNT + 1];
    int was_screen;
    Uint64 was_at;
} s;

/* A value eased toward `to` over a picture `dt` seconds after the last, fast then gently in. */
static float ease_to(float from, float to, float dt, float rate)
{
    const float k = 1.0f - expf(-dt * rate);
    const float out = from + (to - from) * k;
    return fabsf(to - out) < 0.002f ? to : out;
}

/* ---- The pieces' file ------------------------------------------------------------------------- */

static void forget(void)
{
    khdays_layout_forget(&s.layout);
    for (int f = 0; f < STRINGS_FILES; ++f) {
        khdays_layout_strings_free(&s.strings[f]);
    }
}

static int load(SDL_Renderer *renderer, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    static KhdaysRomReader *reader; /* its own: the window's thread */
    char relative[96], path[1024], folder[1024], name[KHDAYS_LAYOUT_NAME] = "";
    char *file_text, *next, *slash;
    size_t file_size = 0;
    forget();
    s.layout.who = "save list";
    s.layout.renderer = renderer;
    if (language < 1 || language > 5) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/save/%s/save.txt", codes[language]);
    if (!khdays_mods_find(relative, path, (int)sizeof(path)) ||
        (file_text = (char *)SDL_LoadFile(path, &file_size)) == NULL) {
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
    for (char *line = file_text; line != NULL && *line != '\0'; line = next) {
        float a, b, c, d, e;
        int shown;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (khdays_layout_line(&s.layout, line, folder)) {
            continue;
        }
        if (SDL_sscanf(line, "rows %f %f %f %f %f %d", &a, &b, &c, &d, &e, &shown) == 6) {
            s.rows[0] = a;
            s.rows[1] = b;
            s.rows[2] = c;
            s.rows[3] = d;
            s.row_pitch = e;
            s.rows_shown = shown > 0 ? shown : 1;
        } else if (SDL_sscanf(line, "scroll %f %f", &a, &b) == 2) {
            s.scroll[0] = a;
            s.scroll[1] = b;
        } else if (SDL_sscanf(line, "pitch %39s %f", name, &a) == 2) {
            if (strcmp(name, "ask") == 0) {
                s.pitch_ask = a;
            } else if (strcmp(name, "tell") == 0) {
                s.pitch_tell = a;
            }
        } else if (strncmp(line, "words empty ", 12) == 0) {
            line[strcspn(line, "\r\n")] = '\0';
            khdays_layout_utf8(line + 12, s.empty, (int)(sizeof(s.empty) / sizeof(s.empty[0])));
        } else if (strncmp(line, "words guest ", 12) == 0) {
            line[strcspn(line, "\r\n")] = '\0';
            khdays_layout_utf8(line + 12, s.guest, (int)(sizeof(s.guest) / sizeof(s.guest[0])));
        }
    }
    SDL_free(file_text);
    if (!khdays_layout_ready(&s.layout) || s.rows_shown == 0) {
        SDL_Log("save list: %s is incomplete", path);
        forget();
        return 0;
    }
    if (reader == NULL) {
        reader = khdays_rom_reader_new();
    }
    snprintf(path, sizeof(path), "UI/load/lrd_%s.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings[STRINGS_LRD], s.layout.who);
    snprintf(path, sizeof(path), "UI/cm/str/sav_%s.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings[STRINGS_SAV], s.layout.who);
    snprintf(path, sizeof(path), "UI/newgame/ngm_%s.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings[STRINGS_NGM], s.layout.who);
    snprintf(path, sizeof(path), "UI/btl/%s/info.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings[STRINGS_INFO], s.layout.who);
    snprintf(path, sizeof(path), "UI/mlt/mlt_%s.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings[STRINGS_MLT], s.layout.who);
    return 1;
}

static const unsigned short *string_of(int file, int record)
{
    return khdays_layout_string(&s.strings[file], record);
}

/* ---- Drawing --------------------------------------------------------------------------------- */

static void draw_text(const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t, float dx, float dy)
{
    khdays_layout_draw_text(&s.layout, fr, style, t, dx, dy, 1.0f);
}

/* A record of the game's strings with its %d made `n` (the first one). */
static void formatted(const unsigned short *in, int n, unsigned short *out, int room)
{
    int o = 0, done = 0;
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

static void ascii(const char *in, unsigned short *out, int room)
{
    int o = 0;
    for (; *in != '\0' && o < room - 1; ++in) {
        out[o++] = (unsigned char)*in;
    }
    out[o] = 0;
}

/* Lines of a record (its \n), drawn one under another from a style's place. */
static void draw_lines(const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t, float pitch, int *line)
{
    unsigned short buffer[256];
    int o = 0;
    if (khdays_layout_text(&s.layout, style) == NULL) {
        return;
    }
    for (;; ++t) {
        if (*t == '\n' || *t == 0) {
            buffer[o] = 0;
            draw_text(fr, style, buffer, 0, (float)*line * pitch);
            ++*line;
            o = 0;
            if (*t == 0) {
                return;
            }
        } else if (o < 255) {
            buffer[o++] = *t;
        }
    }
}

/* The hand before a row or a button: its place from the left at `x`, its middle at `middle`. */
static void draw_glove(const KhdaysLayoutFrame *fr, float x, float middle)
{
    const KhdaysLayoutPlace *g = khdays_layout_place(&s.layout, "glove");
    if (g != NULL) {
        khdays_layout_draw_piece(&s.layout, fr, "glove", x + g->x + khdays_layout_glove_sway(),
                                 middle + g->y - g->h / 2, g->w, g->h, 1.0f);
    }
}

/* KH2's firefly by the selected row's or button's right end (layout_screen.h), as large beside the
 * glove as KH2's beside its own. */
static void draw_firefly(const KhdaysLayoutFrame *fr, float right, float middle)
{
    const KhdaysLayoutPlace *g = khdays_layout_place(&s.layout, "glove");
    const float unit = KHDAYS_FIREFLY_UNIT * (g != NULL ? g->h / 91.0f : 1.0f);
    khdays_layout_firefly(s.layout.renderer, fr, KHDAYS_FIREFLY_SAVE, right - 15.0f * unit, middle, unit, 1.0f);
}

static void draw_row(const KhdaysLayoutFrame *fr, const KhdaysSaveView *v, int index, float x, float y)
{
    unsigned short line[128];
    char digits[32];
    /* the red comes and goes with the cursor (s.lit) */
    const float lit = index >= 0 && index <= KHDAYS_SLOT_COUNT ? s.lit[index] : 0.0f;
    if (lit < 1.0f) {
        khdays_layout_draw_piece(&s.layout, fr, "row", x, y, s.rows[2], s.rows[3], 1.0f);
    }
    if (lit > 0.0f) {
        khdays_layout_draw_piece(&s.layout, fr, "row_selected", x, y, s.rows[2], s.rows[3], lit);
    }
    if (index >= KHDAYS_SLOT_COUNT) {
        khdays_layout_draw_placed(&s.layout, fr, "guest", x, y, 1.0f); /* Mission Mode's guest: its words, Days's */
        return;
    }
    snprintf(digits, sizeof(digits), "%02d", index + 1);
    ascii(digits, line, 128);
    draw_text(fr, "number", line, x, y);
    if (index == KHDAYS_SLOT_COUNT - 1) {
        /* the autosave's file: its tag over the number in the number's own letters, a third of the
         * row higher (the game's own lists' tag, ui/save_numbers.cpp); the tag's words are ASCII */
        ascii(khdays_text_in(TXT_SAVE_AUTOSAVE_TAG, v->language), line, 128);
        draw_text(fr, "number", line, x, y - s.rows[3] / 3.0f);
    }
    switch (v->rows[index].state) {
    case KHDAYS_SLOT_SAVED: {
        const KhdaysSaveRow *r = &v->rows[index];
        char face[8];
        snprintf(face, sizeof(face), "face%d", r->face);
        khdays_layout_draw_placed(&s.layout, fr, face, x, y, 1.0f);
        if (r->crown) {
            khdays_layout_draw_placed(&s.layout, fr, "crown", x, y, 1.0f);
        }
        formatted(string_of(STRINGS_INFO, INFO_DAY), r->day, line, 128);
        draw_text(fr, "day", line, x, y);
        formatted(string_of(STRINGS_LRD, LRD_LEVEL), r->level, line, 128);
        draw_text(fr, "level", line, x, y);
        khdays_layout_draw_placed(&s.layout, fr, "label_munny", x, y, 1.0f);
        khdays_layout_draw_placed(&s.layout, fr, "label_time", x, y, 1.0f);
        snprintf(digits, sizeof(digits), "%u", (unsigned)r->munny);
        ascii(digits, line, 128);
        draw_text(fr, "munny", line, x, y);
        {
            /* H:MM:SS, the game's separator (Ov000_DrawSaveSlotText); H:MM with the Menu Overhaul */
            const unsigned short *colon = string_of(STRINGS_LRD, LRD_TIME);
            const unsigned short sep = colon[0] != 0 ? colon[0] : ':';
            if (khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
                snprintf(digits, sizeof(digits), "%u%c%02u", (unsigned)(r->seconds / 3600), ':',
                         (unsigned)(r->seconds / 60 % 60));
            } else {
                snprintf(digits, sizeof(digits), "%u%c%02u%c%02u", (unsigned)(r->seconds / 3600), ':',
                         (unsigned)(r->seconds / 60 % 60), ':', (unsigned)(r->seconds % 60));
            }
            ascii(digits, line, 128);
            for (int i = 0; line[i] != 0; ++i) {
                if (line[i] == ':') {
                    line[i] = sep;
                }
            }
            draw_text(fr, "time", line, x, y);
        }
        draw_text(fr, "difficulty", string_of(STRINGS_NGM, NGM_DIFFICULTY + (r->difficulty < 3 ? r->difficulty : 0)), x,
                  y);
        break;
    }
    case KHDAYS_SLOT_BROKEN: draw_text(fr, "empty", string_of(STRINGS_LRD, LRD_BROKEN), x, y); break;
    default: draw_text(fr, "empty", s.empty, x, y); break;
    }
}

int khdays_save_screen_draw(SDL_Renderer *renderer)
{
    static KhdaysSaveView v;
    KhdaysLayoutFrame fr;
    int rows_in, words;
    khdays_savelist_view(&v);
    if (v.screen == KHDAYS_SAVELIST_NONE) {
        return 0;
    }
    if (khdays_camp_screen_draws_save()) {
        return 0; /* the camp's save page shows this list, or the title's load list in its look (camp_screen.c,
                   * "The save page") */
    }
    if (renderer != s.layout.renderer || v.language != s.tried) {
        s.tried = v.language;
        s.ok = load(renderer, v.language);
    }
    if (!s.ok) {
        return 0;
    }
    khdays_layout_begin(renderer, &fr);
    khdays_layout_draw_placed(&s.layout, &fr, "screen", 0, 0, 1.0f);
    khdays_layout_draw_placed(&s.layout, &fr, v.screen == KHDAYS_SAVELIST_SAVE ? "heading_save" : "heading_load", 0, 0,
                              1.0f);

    /* the rows: the files, then the guest's; the cursor's in sight */
    rows_in = KHDAYS_SLOT_COUNT + (v.guest ? 1 : 0);
    if (v.cursor < s.top) {
        s.top = v.cursor;
    }
    if (v.cursor >= s.top + s.rows_shown) {
        s.top = v.cursor - s.rows_shown + 1;
    }
    if (s.top > rows_in - s.rows_shown) {
        s.top = rows_in - s.rows_shown;
    }
    if (s.top < 0) {
        s.top = 0;
    }
    {
        /* the motion (the Menu Overhaul's): the list scrolls, the glove glides, the red comes and goes; all at
         * once when the screen opens, and a wrap (the last file to the first) too */
        const Uint64 now = SDL_GetTicksNS();
        const int opened =
            !khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) || v.screen != s.was_screen || now - s.was_at > 250000000u;
        const float dt = opened ? 0.0f : (float)((double)(now - s.was_at) / 1e9);
        const float glove_to = s.rows[1] + ((float)v.cursor - (float)s.top) * s.row_pitch + s.rows[3] / 2;
        s.was_screen = v.screen;
        s.was_at = now;
        if (opened || fabsf((float)s.top - s.view_top) > (float)s.rows_shown) {
            s.view_top = (float)s.top;
        }
        s.view_top = opened ? (float)s.top : ease_to(s.view_top, (float)s.top, dt, 22.0f);
        s.glove_y = opened || fabsf(glove_to - s.glove_y) > s.row_pitch * (float)s.rows_shown
                        ? glove_to
                        : ease_to(s.glove_y, glove_to, dt, 26.0f);
        for (int i = 0; i <= KHDAYS_SLOT_COUNT; ++i) {
            const float to = i == v.cursor ? 1.0f : 0.0f;
            s.lit[i] = opened ? to : ease_to(s.lit[i], to, dt, 20.0f);
        }
    }
    {
        /* the rows in the list's band; those scrolling in or out cut at its edges */
        const int first = (int)floorf(s.view_top);
        const SDL_FRect band = khdays_layout_to_window(&fr, 0.0f, s.rows[1] - 2.0f, KHDAYS_LAYOUT_W,
                                                       (float)(s.rows_shown - 1) * s.row_pitch + s.rows[3] + 4.0f);
        const SDL_Rect clip = {(int)floorf(band.x), (int)floorf(band.y), (int)ceilf(band.w), (int)ceilf(band.h)};
        SDL_SetRenderClipRect(renderer, &clip);
        for (int i = first; i <= first + s.rows_shown && i < rows_in; ++i) {
            if (i >= 0) {
                draw_row(&fr, &v, i, s.rows[0], s.rows[1] + ((float)i - s.view_top) * s.row_pitch);
            }
        }
        SDL_SetRenderClipRect(renderer, NULL);
    }
    if (!v.buttons) {
        draw_glove(&fr, s.rows[0], s.glove_y);
        draw_firefly(&fr, s.rows[0] + s.rows[2], s.glove_y);
    }
    {
        const KhdaysLayoutPlace *thumb = khdays_layout_place(&s.layout, "thumb");
        if (thumb != NULL) {
            const float t = rows_in > s.rows_shown ? s.view_top / (float)(rows_in - s.rows_shown) : 0.0f;
            khdays_layout_draw_piece(&s.layout, &fr, "thumb", thumb->x, s.scroll[0] + t * (s.scroll[1] - s.scroll[0]),
                                     thumb->w, thumb->h, 1.0f);
        }
    }

    /* the help, or the game's question or message */
    words = v.words == KHDAYS_WORDS_SAVE ? STRINGS_SAV : STRINGS_LRD;
    if (v.ask[0] >= 0) {
        unsigned short line[256];
        int n = 0;
        const char *style = v.buttons ? "ask" : "tell";
        const float pitch = v.buttons ? s.pitch_ask : s.pitch_tell;
        for (int i = 0; i < 2; ++i) {
            if (v.ask[i] >= 0) {
                formatted(string_of(words, v.ask[i]), v.ask_file, line, 256);
                draw_lines(&fr, style, line, pitch, &n);
            }
        }
        if (v.buttons) {
            static const char *const names[2][2] = {{"button_yes", "button_selected_yes"},
                                                    {"button_no", "button_selected_no"}};
            for (int b = 0; b < 2; ++b) {
                const int on = b == 0 ? v.yes : !v.yes;
                const KhdaysLayoutPlace *pl = khdays_layout_place(&s.layout, names[b][0]);
                if (pl == NULL) {
                    continue;
                }
                khdays_layout_draw_piece(&s.layout, &fr, names[b][on], pl->x, pl->y, pl->w, pl->h, 1.0f);
                if (on) {
                    draw_glove(&fr, pl->x, pl->y + pl->h / 2);
                    draw_firefly(&fr, pl->x + pl->w, pl->y + pl->h / 2);
                }
                /* the word in the button's middle (the style's place is from there) */
                {
                    const int word = words == STRINGS_SAV ? (b == 0 ? SAV_YES : SAV_NO) : (b == 0 ? LRD_YES : LRD_NO);
                    draw_text(&fr, "button", string_of(words, word), pl->x + pl->w / 2, pl->y + pl->h / 2);
                }
            }
        }
    } else if (v.help && words == STRINGS_SAV) {
        /* the save screen's title (Ov025_SaveMenuDrawPrompt's first phase), and its keys as the
         * load list's (see above) */
        draw_text(&fr, "help", string_of(STRINGS_SAV, SAV_TITLE), 0, 0);
        draw_text(&fr, "help_keys", string_of(STRINGS_MLT, MLT_KEYS), 0, 0);
    } else if (v.help) {
        unsigned short keys[256];
        const unsigned short *a = string_of(STRINGS_LRD, LRD_KEYS);
        const unsigned short *b = string_of(STRINGS_LRD, LRD_DELETE_KEY);
        int o = 0;
        draw_text(&fr, "help", string_of(STRINGS_LRD, LRD_TITLE), 0, 0);
        for (; *a != 0 && o < 200; ++a) {
            keys[o++] = *a;
        }
        if (v.cursor < KHDAYS_SLOT_COUNT) {
            /* the DS's list shows the delete button's line on a file only (not on the guest's row) */
            keys[o++] = ' ';
            keys[o++] = ' ';
            keys[o++] = ' ';
            for (; *b != 0 && o < 254; ++b) {
                keys[o++] = *b;
            }
        }
        if (v.guest && s.guest[0] != 0 && o < 200) {
            /* Y, the guest's game */
            const unsigned short *g = s.guest;
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
        draw_text(&fr, "help_keys", keys, 0, 0);
    }

    /* the game's fades: the lower screen's master brightness */
    khdays_layout_shade(renderer, v.master);
    khdays_layout_end(renderer, &fr);
    return 1;
}

void khdays_save_screen_renderer_gone(void)
{
    forget();
    s.tried = 0;
    s.ok = 0;
    s.layout.renderer = NULL;
    khdays_layout_renderer_gone();
}
