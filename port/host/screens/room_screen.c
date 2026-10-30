/* The port's list of rooms on the screen (room_screen.h), in the HD theater's pieces
 * (layouts/rooms/<lang>/rooms.txt, tools/hd/theater_mission.py; its lines: docs/HD_SCREENS.md, "The
 * rooms"): each room's signal, name, host, lock, players and ping, the forms to create or join, and the
 * members' cards in the places' colours. Typing: the system's text input, or a gamepad as the settings
 * menu's text entry. */
#include "room_screen.h"

#include "../app/devices.h"
#include "layout_screen.h"
#include "../app/profile.h"
#include "../app/places.h"
#include "ui_text.h"
#include "../../runtime/net/roomlist.h"
#include "../../runtime/core/rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MLT_HEADING = 0x3c,
    MLT_CREATE = 0x3e,
    MLT_KEYS = 0x3f,
    MLT_SEEKING = 0x40,
    MLT_HOST_NOTE = 0x41,
    MLT_GUEST_WAITS = 0x42,
    MLT_PROCEED = 0x43,
    MLT_HOST_KEYS = 0x45,
    MLT_GUEST_KEYS = 0x46
};

/* the port's look, as the mock-ups the maintainer chose */
#define CARD_ALPHA 0.63f
#define CARD_ROUND 24.0f
#define NOBODY_ALPHA 0.25f /* an empty place's figure */
#define RING 3.0f          /* the white ring around a place's circle */
#define LEVEL_SIZE 1.5f    /* a member's level's text */
static const Uint8 BLACK[3] = {0, 0, 0}, WHITE[3] = {255, 255, 255}, NOBODY[3] = {90, 90, 90};

static struct {
    int tried, ok;
    KhdaysLayout layout;
    KhdaysLayoutStrings strings;
    SDL_Texture *art;
    float fixed[2], rows[2], form[2], card_pitch, frame_there, frame_nobody, signal_pitch, signal_h[4];
    int rows_shown, signal_ms[3];
    int top;    /* the first room shown */
    int typing; /* the system's text input is on */
    SDL_Window *window;
} s;

/* ---- The pieces' file ------------------------------------------------------------------------- */

static void forget(void)
{
    if (s.art != NULL) {
        SDL_DestroyTexture(s.art);
        s.art = NULL;
    }
    khdays_layout_forget(&s.layout);
    khdays_layout_strings_free(&s.strings);
}

static int load(SDL_Renderer *renderer, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    static KhdaysRomReader *reader; /* its own: the window's thread */
    char relative[96], path[1024], folder[1024], file[256];
    char *file_text, *next, *slash;
    size_t file_size = 0;
    forget();
    s.layout.who = "room list";
    s.layout.renderer = renderer;
    s.rows_shown = 0;
    if (language < 1 || language > 5) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/rooms/%s/rooms.txt", codes[language]);
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
        int n, m, k;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (khdays_layout_line(&s.layout, line, folder)) {
            continue;
        }
        if (SDL_sscanf(line, "fixed %f %f", &a, &b) == 2) {
            s.fixed[0] = a;
            s.fixed[1] = b;
        } else if (SDL_sscanf(line, "rows %f %f %d", &a, &b, &n) == 3) {
            s.rows[0] = a;
            s.rows[1] = b;
            s.rows_shown = n > 0 ? n : 1;
        } else if (SDL_sscanf(line, "form %f %f", &a, &b) == 2) {
            s.form[0] = a;
            s.form[1] = b;
        } else if (SDL_sscanf(line, "cards %f", &a) == 1) {
            s.card_pitch = a;
        } else if (SDL_sscanf(line, "frame %f %f", &a, &b) == 2) {
            s.frame_there = a;
            s.frame_nobody = b;
        } else if (SDL_sscanf(line, "signal_ms %d %d %d", &n, &m, &k) == 3) {
            s.signal_ms[0] = n;
            s.signal_ms[1] = m;
            s.signal_ms[2] = k;
        } else if (SDL_sscanf(line, "signal %f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
            s.signal_pitch = a;
            s.signal_h[0] = b;
            s.signal_h[1] = c;
            s.signal_h[2] = d;
            s.signal_h[3] = e;
        } else if (SDL_sscanf(line, "art %255s", file) == 1) {
            char full[1300];
            int w, h;
            snprintf(full, sizeof(full), "%s%s", folder, file);
            s.art = khdays_layout_texture(&s.layout, full, 0, &w, &h);
        }
    }
    SDL_free(file_text);
    if (!khdays_layout_ready(&s.layout) || s.rows_shown == 0 || khdays_layout_place(&s.layout, "card") == NULL) {
        SDL_Log("room list: %s is incomplete", path);
        forget();
        return 0;
    }
    if (reader == NULL) {
        reader = khdays_rom_reader_new();
    }
    snprintf(path, sizeof(path), "UI/mlt/mlt_%s.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings, s.layout.who);
    return 1;
}

/* ---- Drawing --------------------------------------------------------------------------------- */

/* UTF-8 text as the layout's UTF-16, in a buffer that grows. */
static unsigned short *wide(const char *utf8)
{
    static unsigned short *buffer;
    static size_t room;
    const size_t want = strlen(utf8) + 1;
    if (want > room) {
        unsigned short *more = (unsigned short *)realloc(buffer, want * sizeof(*buffer));
        if (more == NULL) {
            return NULL;
        }
        buffer = more;
        room = want;
    }
    khdays_layout_utf8(utf8, buffer, (int)want);
    return buffer;
}

static void text(const KhdaysLayoutFrame *fr, const char *style, const char *utf8, float x, float y, float alpha)
{
    const unsigned short *w = wide(utf8);
    if (w != NULL) {
        khdays_layout_draw_text(&s.layout, fr, style, w, x, y, alpha);
    }
}

static void words(const KhdaysLayoutFrame *fr, const char *style, int record, float x, float y, float alpha)
{
    khdays_layout_draw_text(&s.layout, fr, style, khdays_layout_string(&s.strings, record), x, y, alpha);
}

static const float *place(const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&s.layout, name);
    return p != NULL ? &p->x : none;
}

/* Lines of a record joined into one (the DS's line breaks, blank lines left out). */
static void joined(const unsigned short *in, unsigned short *out, int room)
{
    int o = 0;
    for (; in != NULL && *in != 0 && o < room - 1; ++in) {
        if (*in == '\n') {
            if (o > 0 && out[o - 1] != ' ') {
                out[o++] = ' ';
            }
        } else {
            out[o++] = *in;
        }
    }
    out[o] = 0;
}

static const char *field_label(int which)
{
    return khdays_text(which == KHDAYS_ROOMFIELD_SEARCH ? TXT_ROOM_SEARCH
                       : which == KHDAYS_ROOMFIELD_NAME ? TXT_ROOM_NAME
                                                        : TXT_ROOM_PASSWORD);
}

/* KH2's firefly at the selected row's right end (layout_screen.h), as large beside the glove as KH2's
 * beside its own. */
static void firefly_at(const KhdaysLayoutFrame *fr, float right, float middle)
{
    const KhdaysLayoutPlace *g = khdays_layout_place(&s.layout, "glove");
    const float unit = KHDAYS_FIREFLY_UNIT * (g != NULL && g->h > 0 ? g->h / 91.0f : 1.0f);
    khdays_layout_firefly(s.layout.renderer, fr, KHDAYS_FIREFLY_ROOM, right - 15.0f * unit, middle, unit, 1.0f);
}

static void firefly_row(const KhdaysLayoutFrame *fr, float top)
{
    const KhdaysLayoutPlace *r = khdays_layout_place(&s.layout, "row_selected");
    if (r != NULL) {
        firefly_at(fr, r->x + r->w, top + r->y + r->h / 2);
    }
}

/* A row's pieces at `y`: the theater's bar (red when selected) and square, the glove before it. */
static void row_pieces(const KhdaysLayoutFrame *fr, float y, int selected)
{
    khdays_layout_draw_placed(&s.layout, fr, selected ? "row_selected" : "row", 0, y, 1.0f);
    khdays_layout_draw_placed(&s.layout, fr, "square", 0, y, 1.0f);
    if (selected) {
        khdays_layout_draw_placed(&s.layout, fr, "glove", khdays_layout_glove_sway(), y, 1.0f);
        firefly_row(fr, y);
    }
}

/* Four bars, lit as the ping is short (none when it is not measured). */
static void signal(const KhdaysLayoutFrame *fr, float x, float y, float tallest, int ping_ms)
{
    const int lit = ping_ms < 0                ? 0
                    : ping_ms < s.signal_ms[0] ? 4
                    : ping_ms < s.signal_ms[1] ? 3
                    : ping_ms < s.signal_ms[2] ? 2
                                               : 1;
    const float *bar = place("signal");
    for (int k = 0; k < 4; ++k) {
        const float h = s.signal_h[k] * (tallest / (s.signal_h[3] > 0 ? s.signal_h[3] : 1.0f));
        const SDL_FRect r = khdays_layout_to_window(fr, x + (float)k * s.signal_pitch, y + tallest - h, bar[2], h);
        const Uint8 *c = k < lit ? WHITE : NOBODY;
        SDL_SetRenderDrawColor(s.layout.renderer, c[0], c[1], c[2], 255);
        SDL_RenderFillRect(s.layout.renderer, &r);
    }
}

/* The game's UTF-16 name as UTF-8. */
static void utf8_of(const uint16_t *in, int count, char *out, size_t room)
{
    size_t at = 0;
    for (int i = 0; i < count && in[i] != 0 && at + 4 < room; ++i) {
        const unsigned c = in[i];
        if (c < 0x80) {
            out[at++] = (char)c;
        } else if (c < 0x800) {
            out[at++] = (char)(0xc0 | c >> 6);
            out[at++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[at++] = (char)(0xe0 | c >> 12);
            out[at++] = (char)(0x80 | (c >> 6 & 0x3f));
            out[at++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[at] = '\0';
}

static void draw_row(const KhdaysLayoutFrame *fr, const KhdaysRoomView *v, int index, float y)
{
    const KhdaysRoomRow *r = &v->rows[index];
    const int selected = index == v->cursor;
    char line[96];
    row_pieces(fr, y, selected);
    switch (r->kind) {
    case KHDAYS_ROOMROW_ROOM: {
        const float alpha = r->room.open ? 1.0f : 0.5f; /* a room in a mission takes nobody now */
        const KhdaysLayoutText *name = khdays_layout_text(&s.layout, "name");
        const unsigned short *w = wide(r->text[0] != '\0' ? r->text : "-");
        char host[64];
        const float *bar = place("signal");
        signal(fr, bar[0], y + bar[1], bar[3], r->room.ping_ms);
        if (w != NULL && name != NULL) {
            const float after = name->x + khdays_layout_line_width(&s.layout, w, name->size);
            khdays_layout_draw_text(&s.layout, fr, "name", w, 0, y, alpha);
            if (r->room.locked) {
                text(fr, "locked", khdays_text(TXT_ROOM_PASSWORD_TAG), after, y, alpha);
            }
        }
        utf8_of(r->room.host, (int)(sizeof(r->room.host) / sizeof(r->room.host[0])), host, sizeof(host));
        snprintf(line, sizeof(line), khdays_text(TXT_ROOM_HOST), host);
        text(fr, "host", line, 0, y, alpha);
        if (r->room.most > 0) {
            snprintf(line, sizeof(line), khdays_text(TXT_ROOM_PLAYERS), r->room.players, r->room.most);
        } else {
            snprintf(line, sizeof(line), "%d", r->room.players);
        }
        text(fr, "players", line, 0, y, alpha);
        if (r->room.ping_ms >= 0) {
            snprintf(line, sizeof(line), "%d ms", r->room.ping_ms);
        } else {
            snprintf(line, sizeof(line), "- ms");
        }
        text(fr, "ping", line, 0, y, alpha);
        break;
    }
    case KHDAYS_ROOMROW_FIELD: {
        /* what is typed (a password as asterisks), with a caret while it is typed; its label under it,
         * or alone while it is empty */
        const char *t = r->text;
        size_t n = strlen(t);
        const int caret = selected && v->editing;
        char *shown;
        if (n == 0 && !caret) {
            text(fr, "placeholder", field_label(r->which), 0, y, 1.0f);
            break;
        }
        shown = (char *)malloc(n + 4);
        if (shown == NULL) {
            break;
        }
        if (r->which == KHDAYS_ROOMFIELD_PASSWORD) {
            size_t k = 0;
            for (size_t i = 0; i < n; ++i) {
                if (((unsigned char)t[i] & 0xc0) != 0x80) {
                    shown[k++] = '*';
                }
            }
            n = k;
        } else {
            memcpy(shown, t, n);
        }
        if (caret && (SDL_GetTicks() / 500) % 2 == 0) {
            shown[n++] = '_';
        }
        shown[n] = '\0';
        text(fr, "name", shown, 0, y, 1.0f);
        text(fr, "host", field_label(r->which), 0, y, 1.0f);
        free(shown);
        break;
    }
    case KHDAYS_ROOMROW_ACTION:
        if (r->which == KHDAYS_ROOMACTION_CREATE) {
            words(fr, "single", MLT_CREATE, 0, y, 1.0f);
        } else {
            text(fr, "single", khdays_text(TXT_ROOM_JOIN), 0, y, 1.0f);
        }
        break;
    default: text(fr, "placeholder", khdays_text(TXT_ROOM_NONE), 0, y, 1.0f); break;
    }
}

/* The list: its first rows where they stay, the rooms under them as many as show, the cursor's in
 * sight. */
static void draw_list(const KhdaysLayoutFrame *fr, const KhdaysRoomView *v)
{
    const int fixed = SDL_min(KHDAYS_ROOMS_FIXED, v->row_count), rooms = v->row_count - fixed;
    const float *trough = place("trough"), *thumb = place("thumb");
    for (int i = 0; i < fixed; ++i) {
        draw_row(fr, v, i, s.fixed[0] + (float)i * s.fixed[1]);
    }
    {
        const float *d = place("divider");
        if (d[2] > 0) {
            khdays_layout_fill_rounded(s.layout.renderer, fr, d[0], d[1], d[2], d[3], 0, WHITE, 0.27f);
        }
    }
    if (v->cursor >= fixed) {
        const int at = v->cursor - fixed;
        if (at < s.top) {
            s.top = at;
        }
        if (at >= s.top + s.rows_shown) {
            s.top = at - s.rows_shown + 1;
        }
    }
    if (s.top > rooms - s.rows_shown) {
        s.top = rooms - s.rows_shown;
    }
    if (s.top < 0) {
        s.top = 0;
    }
    for (int i = 0; i < s.rows_shown && s.top + i < rooms; ++i) {
        draw_row(fr, v, fixed + s.top + i, s.rows[0] + (float)i * s.rows[1]);
    }
    if (trough[2] > 0) {
        const float travel = trough[3] - thumb[3];
        const float t = rooms > s.rows_shown ? (float)s.top / (float)(rooms - s.rows_shown) : 0.0f;
        khdays_layout_draw_placed(&s.layout, fr, "trough", 0, 0, 1.0f);
        khdays_layout_draw_placed(&s.layout, fr, "end", 0, 0, 1.0f);
        khdays_layout_draw_piece(&s.layout, fr, "thumb", thumb[0], trough[1] + travel * t, thumb[2], thumb[3], 1.0f);
    }
}

/* A place's circle: its colour in a white ring, its label in it. */
static void circle(const KhdaysLayoutFrame *fr, int which, float cx, float cy, float r)
{
    char label[16];
    khdays_layout_fill_oval(s.layout.renderer, fr, cx, cy, r + RING, r + RING, WHITE, 1.0f);
    khdays_layout_fill_oval(s.layout.renderer, fr, cx, cy, r, r, KHDAYS_PLACE_COLOURS[which & 3], 1.0f);
    snprintf(label, sizeof(label), khdays_text(TXT_ROOM_PLACE), which + 1);
    text(fr, "card_tag", label, cx, cy, 1.0f);
}

/* A room: a card for each place (see above), and the host's Proceed. */
static void draw_members(const KhdaysLayoutFrame *fr, const KhdaysRoomView *v)
{
    const float *card = place("card"), *figure = place("card_face"), *tag = place("card_tag");
    const float *bars = place("card_signal");
    char line[96];
    for (int i = 0; i < v->row_count && i < 4; ++i) {
        const KhdaysRoomRow *r = &v->rows[i];
        const int there = r->room.players != 0, self = r->which == v->self;
        const float x = card[0] + (float)i * s.card_pitch, y = card[1];
        const float t = there ? s.frame_there : s.frame_nobody;
        khdays_layout_fill_rounded(s.layout.renderer, fr, x, y, card[2], card[3], CARD_ROUND, BLACK, CARD_ALPHA);
        khdays_layout_ring_rounded(s.layout.renderer, fr, x, y, card[2], card[3], CARD_ROUND, t,
                                   there ? KHDAYS_PLACE_COLOURS[r->which & 3] : NOBODY, 1.0f);
        khdays_layout_draw_piece(&s.layout, fr, "unknown", x + figure[0], y + figure[1], figure[2], figure[3],
                                 there ? 1.0f : NOBODY_ALPHA);
        circle(fr, r->which, x + tag[0], y + tag[1], tag[2]);
        if (!there) {
            text(fr, "card_empty", "------", x, y, 1.0f);
            continue;
        }
        text(fr, "card_name", r->text[0] != '\0' ? r->text : "-", x, y, 1.0f);
        if (r->which == 0) {
            snprintf(line, sizeof(line), "%s%s%s", khdays_text(TXT_ROOM_HOST_TAG), self ? " - " : "",
                     self ? khdays_text(TXT_ROOM_YOU) : "");
            text(fr, "card_host", line, x, y, 1.0f);
        } else if (self) {
            text(fr, "card_you", khdays_text(TXT_ROOM_YOU), x, y, 1.0f);
        }
        if (!self && r->room.ping_ms >= 0) {
            signal(fr, x + bars[0], y + bars[1], bars[3], r->room.ping_ms);
            snprintf(line, sizeof(line), "%d ms", r->room.ping_ms);
            text(fr, "card_ping", line, x, y, 1.0f);
        }
        if (r->level > 0) {
            /* the player's level in the card's upper right corner, as far in as its place's circle */
            const unsigned short *level;
            snprintf(line, sizeof(line), khdays_text(TXT_ROOM_LEVEL), r->level);
            level = wide(line);
            if (level != NULL) {
                const float w = khdays_layout_line_width(&s.layout, level, LEVEL_SIZE);
                const float h = (s.layout.ink_bottom - s.layout.ink_top) * LEVEL_SIZE;
                const float lx = x + card[2] - (tag[0] - tag[2]) - w, ly = y + tag[1] - h / 2;
                if (!s.layout.font_colours) {
                    khdays_layout_draw_line(&s.layout, fr, level, lx, ly, LEVEL_SIZE, 255.0f, 1.0f, 1); /* its shadow */
                }
                khdays_layout_draw_line(&s.layout, fr, level, lx, ly, LEVEL_SIZE, 255.0f, 1.0f, 0);
            }
        }
    }
    if (v->host) {
        const float *b = place("proceed");
        const float alpha = v->can_proceed ? 1.0f : 0.5f;
        khdays_layout_draw_piece(&s.layout, fr, v->can_proceed ? "button_lit" : "button", b[0], b[1], b[2], b[3],
                                 alpha);
        if (v->can_proceed) {
            const float *g = place("proceed_glove");
            khdays_layout_draw_piece(&s.layout, fr, "glove", g[0] + khdays_layout_glove_sway(), g[1], g[2], g[3], 1.0f);
            firefly_at(fr, b[0] + b[2], b[1] + b[3] / 2);
        }
        words(fr, "proceed", MLT_PROCEED, 0, 0, alpha);
    }
}

/* The DS's buttons in the game's font (layout_screen.h): A, B and X, as UTF-8. */
#define DS_A "\xe3\x8d\x89"
#define DS_B "\xe3\x8c\x94"
#define DS_X "\xe3\x8c\xa2"

int khdays_room_screen_draw(SDL_Renderer *renderer)
{
    const KhdaysRoomView *v = khdays_roomlist_hold();
    KhdaysLayoutFrame fr;
    unsigned short line[512];
    if (v == NULL || v->screen == KHDAYS_ROOMS_NONE) {
        khdays_roomlist_release();
        if (s.typing && s.window != NULL) {
            SDL_StopTextInput(s.window);
            s.typing = 0;
        }
        return 0;
    }
    if (renderer != s.layout.renderer || v->language != s.tried) {
        s.tried = v->language;
        s.ok = load(renderer, v->language);
    }
    /* the system's text input while a field takes it */
    s.window = SDL_GetRenderWindow(renderer);
    if (v->editing != s.typing && s.window != NULL) {
        if (v->editing) {
            SDL_StartTextInput(s.window);
        } else {
            SDL_StopTextInput(s.window);
        }
        s.typing = v->editing;
    }
    if (!s.ok) {
        khdays_roomlist_release();
        return 0;
    }
    khdays_layout_begin(renderer, &fr);
    if (s.art != NULL) {
        const SDL_FRect all = khdays_layout_to_window(&fr, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H);
        SDL_RenderTexture(renderer, s.art, NULL, &all);
    }
    khdays_layout_draw_piece(&s.layout, &fr, "veil", 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H, 1.0f);
    {
        const float *rule = place("rule");
        if (rule[2] > 0) {
            khdays_layout_fill_rounded(renderer, &fr, rule[0], rule[1], rule[2], rule[3], 0, WHITE, 0.47f);
        }
    }
    khdays_layout_draw_placed(&s.layout, &fr, "help_bar", 0, 0, 1.0f);
    switch (v->screen) {
    case KHDAYS_ROOMS_MEMBERS:
        /* the room's name (the game's words when it is not known here) */
        if (v->title[0] != '\0') {
            text(&fr, "heading", v->title, 0, 0, 1.0f);
        } else {
            words(&fr, "heading", MLT_SEEKING, 0, 0, 1.0f);
        }
        joined(khdays_layout_string(&s.strings, v->host ? MLT_HOST_NOTE : MLT_GUEST_WAITS), line,
               (int)(sizeof(line) / sizeof(line[0])));
        khdays_layout_draw_text(&s.layout, &fr, "message", line, 0, 0, 1.0f);
        draw_members(&fr, v);
        words(&fr, "help", v->host ? MLT_HOST_KEYS : MLT_GUEST_KEYS, 0, 0, 1.0f);
        break;
    case KHDAYS_ROOMS_LIST:
        words(&fr, "heading", MLT_HEADING, 0, 0, 1.0f);
        if (v->mods_needed[0] != 0) {
            char needs[1200];
            const int n = SDL_snprintf(needs, sizeof(needs), khdays_text(TXT_ROOM_NEEDS_MODS), v->mods_needed);
            if (v->mods_more > 0 && n > 0 && n < (int)sizeof(needs)) {
                SDL_snprintf(needs + n, sizeof(needs) - (size_t)n, " (+%d)", v->mods_more);
            }
            text(&fr, "notice", needs, 0, 0, 1.0f);
        } else if (v->wrong_password) {
            text(&fr, "notice", khdays_text(TXT_ROOM_WRONG_PASSWORD), 0, 0, 1.0f);
        }
        draw_list(&fr, v);
        break;
    default:
        /* a new room's name and password, or a room's password */
        if (v->screen == KHDAYS_ROOMS_CREATE) {
            words(&fr, "heading", MLT_CREATE, 0, 0, 1.0f);
            text(&fr, "message", khdays_text(TXT_ROOM_PASSWORD_NOTE), 0, 0, 1.0f);
        } else {
            if (v->title[0] != '\0') {
                text(&fr, "heading", v->title, 0, 0, 1.0f);
            } else {
                words(&fr, "heading", MLT_HEADING, 0, 0, 1.0f);
            }
            text(&fr, "message", khdays_text(TXT_ROOM_PASSWORD_ASK), 0, 0, 1.0f);
        }
        for (int i = 0; i < v->row_count; ++i) {
            draw_row(&fr, v, i, s.form[0] + (float)i * s.form[1]);
        }
        break;
    }
    if (v->screen != KHDAYS_ROOMS_MEMBERS) {
        /* the help: the keys of the typing, or the list's */
        if (v->editing) {
            const int pad = khdays_devices_gamepad() != NULL && khdays_devices_pad_in_use();
            char hints[256];
            if (pad) {
                snprintf(hints, sizeof(hints), khdays_text(TXT_HINTS_TEXT_GAMEPAD), DS_A, DS_X, DS_B);
            } else {
                snprintf(hints, sizeof(hints), "%s", khdays_text(TXT_HINTS_TEXT_KEYBOARD));
            }
            text(&fr, "help", hints, 0, 0, 1.0f);
        } else {
            words(&fr, "help", MLT_KEYS, 0, 0, 1.0f);
        }
    }
    khdays_layout_shade(renderer, v->master);
    khdays_layout_end(renderer, &fr);
    khdays_roomlist_release();
    return 1;
}

/* ---- Typing ----------------------------------------------------------------------------------- */

/* The gamepad's input bound to DS button `action`, its first; -1 none. */
static int pad_input_of(int action)
{
    const KhdaysProfile *p = khdays_devices_profile();
    for (int slot = 0; slot < KHDAYS_INPUTS_PER_ACTION; ++slot) {
        const int input = p->input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][action][slot];
        if (input != KHDAYS_NO_INPUT) {
            return input;
        }
    }
    return -1;
}

static int is_pad(SDL_GamepadButton button, int action)
{
    const int input = pad_input_of(action);
    return input >= 0 && input < KHDAYS_PAD_COUNT && khdays_devices_sdl_button(input) == button;
}

/* The field's last character, if it is one byte (the pad's entry changes those only). */
static int last_char(void)
{
    const KhdaysRoomView *v = khdays_roomlist_hold();
    int c = -1;
    if (v != NULL && v->cursor >= 0 && v->cursor < v->row_count && v->rows[v->cursor].kind == KHDAYS_ROOMROW_FIELD) {
        const char *t = v->rows[v->cursor].text;
        const size_t n = strlen(t);
        c = n == 0 ? 0 : ((unsigned char)t[n - 1] < 0x80 ? (unsigned char)t[n - 1] : -1);
    }
    khdays_roomlist_release();
    return c;
}

static void type_char(int c)
{
    const char one[2] = {(char)c, '\0'};
    khdays_roomlist_type(one);
}

/* A gamepad direction while typing: up and down change the last character, right adds one (a
 * copy of the last, or an A), left erases one (the settings menu's text entry). */
static void pad_direction(SDL_GamepadButton button)
{
    const int c = last_char();
    if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
        khdays_roomlist_edit(KHDAYS_EDIT_ERASE);
    } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT || c == 0) {
        type_char(c > 0 ? c : 'A');
    } else if (c > 0 && (button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
        /* the next or previous printable character */
        const int dir = button == SDL_GAMEPAD_BUTTON_DPAD_UP ? 1 : -1;
        khdays_roomlist_edit(KHDAYS_EDIT_ERASE);
        type_char(0x20 + (c - 0x20 + dir + 0x5f) % 0x5f);
    }
}

int khdays_room_screen_event(const SDL_Event *e)
{
    if (!khdays_roomlist_editing()) {
        return 0;
    }
    switch (e->type) {
    case SDL_EVENT_TEXT_INPUT: khdays_roomlist_type(e->text.text); return 1;
    case SDL_EVENT_KEY_DOWN:
        switch (e->key.scancode) {
        case SDL_SCANCODE_BACKSPACE: khdays_roomlist_edit(KHDAYS_EDIT_ERASE); break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: khdays_roomlist_edit(KHDAYS_EDIT_DONE); break;
        case SDL_SCANCODE_ESCAPE: khdays_roomlist_edit(KHDAYS_EDIT_CANCEL); break;
        default: break;
        }
        return 1; /* the game's keys (and the settings') wait while a field takes the keyboard */
    case SDL_EVENT_KEY_UP: return 1;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        const SDL_GamepadButton b = (SDL_GamepadButton)e->gbutton.button;
        if (is_pad(b, KHDAYS_ACT_A)) {
            khdays_roomlist_edit(KHDAYS_EDIT_DONE);
        } else if (is_pad(b, KHDAYS_ACT_B)) {
            khdays_roomlist_edit(KHDAYS_EDIT_CANCEL);
        } else if (is_pad(b, KHDAYS_ACT_X)) {
            khdays_roomlist_edit(KHDAYS_EDIT_CLEAR);
        } else if (b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
                   b == SDL_GAMEPAD_BUTTON_DPAD_LEFT || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
            pad_direction(b);
        }
        return 1;
    }
    default: return 0;
    }
}

void khdays_room_screen_renderer_gone(void)
{
    forget();
    s.tried = 0;
    s.ok = 0;
    s.layout.renderer = NULL;
    khdays_layout_renderer_gone();
}
