/* Mission Mode's character select (charselect.h), over the game's (ov006 in scene 7, ov008 in the
 * camp): the port reads the places, names, characters and gauges, and moves a place by writing the
 * request the game's own input makes, one step at a time, the game's sounds following. Addresses and
 * offsets: docs/RUNTIME_MAP.md section 16.3. */
#include "charselect.h"
#include "select_pages.h"

#include "../characters/characters.h"
#include "../input/input.h"
#include "../core/io.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/utf8.h"
#include "../core/window.h"
#include "../../host/screens/ui_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern int GetLanguage(void);
int khdays_language_wanted(void); /* language.c */
u32 khdays_events_vblank_count(void);
int khdays_overlay_loaded(int id); /* overlays.c */

/* A place's row as the game gives it (Ov006_GetMissionRowInfo). */
typedef struct GameRow {
    u16 count;
    u8 present, ready;
    s8 character;
} GameRow;

extern void Ov006_UpdateMissionMemberMenuScreen(void);
extern void Ov006_MemberMenuNextStateNoOp(void);
extern int Ov006_MissionGetCursorEntry(void);
extern int Ov006_MissionScene_IsFlagSet(void);
extern int Ov006_CanConfirmMissionMenu(void);
extern int Ov006_MissionScene_GetState(void);
extern int Ov006_MissionIsEntryActive(void);
extern int Ov006_CountPlayers(void);
extern void Ov008_LobbyStep(void);
extern void Ov008_LobbyNextStepNoOp(void);
extern int Ov008_GetLocalPlayerIndex(void);
extern int Ov008_MissionScene_IsFlagSet(void);
extern int Ov008_CanConfirmMissionMenu(void);
extern int Ov008_MissionScene_GetState(void);
extern int Ov008_MissionIsEntryActive(void);
extern int Ov008_CountPlayers(void);

#define SEEN_FRAMES 2u   /* the game's screen is gone when its update did not run for longer */
#define STATE_CHOOSING 5 /* the screen's state while the places move (Ov006_MissionScene_GetState) */
#define MENU_HOST 0x20u
#define MENU_MESSAGE 0x32u
#define MENU_BOUGHT 0x30u
#define ROWS 0x4acu    /* the places' rows, six bytes each: */
#define ROW_FLAGS 0x1u /*   bit 0 a player there, bit 1 chosen */
#define ROW_CHARACTER 0x2u
#define ROW_COUNT 0x4u
#define ROW_SIZE 6u
#define REQUEST 0x4e0u    /* a guest's request, as a row (its first byte the place) */
#define LOCKED_OUT 0x4e8u /* set: the request is place 0's whatever this copy's (the game's) */
#define NAMES_PRESENT 0x434u
#define NAMES 0x436u
#define NAME_LENGTH 11u
#define STAT_ROW 9u
#define ALWAYS 13                       /* characters 0-12 are always there; 13 on are bought */
#define RANDOM KHDAYS_SELECT_CHARACTERS /* the grid's random choice, after the characters */
#define ASK_WAIT                                                                                                       \
    120u                 /* VBlanks a move may take to be answered before the port lets it go (the
                             * host's answer comes within a few frames) */
#define REPEAT_AFTER 20u /* a held direction moves again after this many VBlanks, */
#define REPEAT_EVERY 6u  /* then every this many (the port's) */
/* the game's messages (mlt_&.s) */
#define MSG_CHOOSE 0x33
#define MSG_WAIT 0x34
#define MSG_PRESS_START 0x37
#define MSG_HOST_LEAVING 0x36
#define HELP_CHOOSE 0x38
#define HELP_CHANGE 0x39
#define HELP_CHANGE_START 0x3a
#define HELP_HOST 0x3b
#define GLYPH_Y 0x334du /* the Y button in the game's font (text/font_eu_10.nftr), which its text never writes */

typedef struct Screen {
    int overlay;
    void (*update)(void), (*leaving)(void); /* the screen's update; the state it leaves in */
    int (*self)(void);
    int (*flag_set)(void), (*confirming)(void), (*state)(void), (*entry_active)(void), (*players)(void);
    u32 session, menu, captions, stats;
} Screen;

static const Screen screens[] = {
    {6, Ov006_UpdateMissionMemberMenuScreen, Ov006_MemberMenuNextStateNoOp, Ov006_MissionGetCursorEntry,
     Ov006_MissionScene_IsFlagSet, Ov006_CanConfirmMissionMenu, Ov006_MissionScene_GetState, Ov006_MissionIsEntryActive,
     Ov006_CountPlayers, 0x020565e4u, 0x02056660u, 0x020561d0u, 0x020561e0u},
    {8, Ov008_LobbyStep, Ov008_LobbyNextStepNoOp, Ov008_GetLocalPlayerIndex, Ov008_MissionScene_IsFlagSet,
     Ov008_CanConfirmMissionMenu, Ov008_MissionScene_GetState, Ov008_MissionIsEntryActive, Ov008_CountPlayers,
     0x02090f24u, 0x02090fa0u, 0x0208fc8cu, 0x0208fc9cu},
};
#define SCREENS ((int)(sizeof(screens) / sizeof(screens[0])))

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysSelectView shown;
static int on = -1; /* screens' index whose update ran last */
static u32 seen_at;
static volatile LONG columns = 5;
static volatile LONG page_rows; /* a page's rows (the layout's), 0 one page */
/* the game's thread's own */
static KhdaysSelectView now; /* as last read, after the game's update */
static int want = -1;        /* the character the player moved to, until the game has it */
static int on_random;        /* the player is on the grid's random choice */
static int asked;            /* the port's request for it is made */
static u32 asked_at;
static int a_kept; /* an A pressed while the move was unanswered */
static int a_give; /* ... to hand the game now that it is answered */
static u16 held;
static int repeat_dir;
static u32 repeat_from, repeat_count;

static u32 rd32(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

static u16 rd16(u32 address)
{
    return *(const volatile u16 *)KHDAYS_W(address);
}

static u8 rd8(u32 address)
{
    return *(const volatile u8 *)KHDAYS_W(address);
}

static void wr8(u32 address, u8 value)
{
    *(volatile u8 *)KHDAYS_W(address) = value;
}

static void wr16(u32 address, u16 value)
{
    *(volatile u16 *)KHDAYS_W(address) = value;
}

static int in_ram(u32 address)
{
    return khdays_game_ram(address); /* main RAM or the game's heaps */
}

static int language(void)
{
    return khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
}

/* Whether a mod gives the port's screen in the language. */
static int available(int lang)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    static int asked_lang = -1, answer;
    char relative[64], path[1024];
    if (lang != asked_lang) {
        asked_lang = lang;
        answer = 0;
        if (lang >= 1 && lang <= 5) {
            snprintf(relative, sizeof(relative), "layouts/select/%s/select.txt", codes[lang]);
            answer = khdays_mods_find(relative, path, (int)sizeof(path));
        }
    }
    return answer;
}

/* The game's fade: the lower screen's master brightness (MASTER_BRIGHT, engine B). */
static int16_t brightness(void)
{
    const u16 bright = IO16(0x106c);
    const int mode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
    return (int16_t)(mode == 1 ? factor : mode == 2 ? -factor : 0);
}

static int active(void)
{
    return on >= 0 && khdays_events_vblank_count() - seen_at <= SEEN_FRAMES && now.shown;
}

/* ---- The grid --------------------------------------------------------------------------------- */

/* Whether character `c` (the game's 0-18 or a mod's number) may be chosen. */
static int choosable(const KhdaysSelectView *v, int c)
{
    if (c >= 0 && c < KHDAYS_SELECT_CHARACTERS) {
        return (v->choosable >> c) & 1u;
    }
    const KhdaysCharacter *mod = khdays_characters_numbered(c);
    /* a mod's character follows its base's lock when it says so */
    return mod != NULL && (!mod->locked_with_base || ((v->choosable >> khdays_characters_menu_entry(c)) & 1u));
}

/* Whether another place is on character `c` (the game lets no two places have one). */
static int taken(const KhdaysSelectView *v, int c)
{
    for (int i = 0; i < KHDAYS_SELECT_PLACES; ++i) {
        if (i != v->self && v->places[i].present && v->places[i].character == c) {
            return 1;
        }
    }
    return 0;
}

int khdays_charselect_at(const KhdaysSelectView *v, int position)
{
    if (position < 0) {
        return -1;
    }
    for (int c = 0; c < KHDAYS_SELECT_CHARACTERS; ++c) {
        if (choosable(v, c) && position-- == 0) {
            return c;
        }
    }
    for (int i = 0; i < khdays_characters_count(); ++i) {
        const int c = khdays_characters_at(i)->number;
        if (choosable(v, c) && position-- == 0) {
            return c;
        }
    }
    return position == 0 ? RANDOM : -1;
}

/* The grid's count, the random choice with them. */
static int grid_count(const KhdaysSelectView *v)
{
    int count = 0;
    while (khdays_charselect_at(v, count) >= 0) {
        ++count;
    }
    return count;
}

/* Whether the cursor may go onto grid position `position` (`user` the view): the random choice, or a
 * character no other place is on. */
static int may_move_onto(int position, void *user)
{
    const KhdaysSelectView *v = (const KhdaysSelectView *)user;
    const int c = khdays_charselect_at(v, position);
    return c == RANDOM || (c >= 0 && !taken(v, c));
}

/* The port's grid now: its positions, the layout's columns, and its rows a page (select_pages.h). */
static KhdaysSelectGrid grid_of(const KhdaysSelectView *v)
{
    const KhdaysSelectGrid g = {grid_count(v), (int)columns > 0 ? (int)columns : 1, (int)page_rows};
    return g;
}

/* The character a move leads to from `from` (KHDAYS_SELECT_*), across the grid's pages, the ones taken
 * stepped over; the grid's last is RANDOM, the random choice. */
static int step(const KhdaysSelectView *v, int from, int move)
{
    const KhdaysSelectGrid g = grid_of(v);
    int at = 0;
    for (int p = 0; p < g.count; ++p) {
        if (khdays_charselect_at(v, p) == from) {
            at = p;
        }
    }
    if (g.count == 0) {
        return from;
    }
    const int to = khdays_select_move(g, at, move, may_move_onto, (void *)v);
    return to == at ? from : khdays_charselect_at(v, to);
}

/* A character the game lets be chosen and no other place is on, at random: another than `now` when
 * there is one; -1 when none is free. */
static int roll(const KhdaysSelectView *v, int now_on)
{
    static u32 state;
    const int positions = grid_count(v);
    int count = 0;
    for (int p = 0; p < positions; ++p) {
        const int c = khdays_charselect_at(v, p);
        count += c != RANDOM && !taken(v, c) && c != now_on;
    }
    if (count == 0) {
        return now_on >= 0 && choosable(v, now_on) ? now_on : -1;
    }
    if (state == 0) {
        state = (khdays_events_vblank_count() ^ (u32)GetTickCount64()) * 2654435761u | 1u;
    }
    state ^= state << 13; /* xorshift32 */
    state ^= state >> 17;
    state ^= state << 5;
    for (int p = 0, k = (int)(state % (u32)count); p < positions; ++p) {
        const int c = khdays_charselect_at(v, p);
        if (c != RANDOM && !taken(v, c) && c != now_on && k-- == 0) {
            return c;
        }
    }
    return -1;
}

/* The player chose at random (the grid's random choice, or Y): the character moves there and the
 * game's A follows once it has it, as a choice. */
static void choose_at_random(void)
{
    const int c = roll(&now, now.places[now.self].character);
    on_random = 0;
    if (c >= 0) {
        want = c;
        asked = 0;
        a_kept = 1;
    }
}

/* ---- The game's screen ------------------------------------------------------------------------ */

static void messages(const Screen *sc, const GameRow *rows, int self, u32 menu, KhdaysSelectView *v)
{
    if (sc->overlay == 8) {
        /* Ov008_LobbyStep's */
        const int confirming = sc->confirming() != 0;
        v->heading = confirming ? MSG_HOST_LEAVING : MSG_CHOOSE;
        v->help = confirming ? HELP_HOST : HELP_CHOOSE;
        return;
    }
    /* Ov006_UpdateMissionMemberMenuScreen's */
    {
        const int host = rd32(menu + MENU_HOST) != 0;
        int all = 0;
        if (sc->flag_set() && host) {
            int count = 0;
            all = 1;
            for (int i = 0; i < KHDAYS_SELECT_PLACES; ++i) {
                if (rows[i].present) {
                    if (!rows[i].ready) {
                        all = 0;
                        break;
                    }
                    ++count;
                }
            }
            if (all && count != sc->players()) {
                all = 0;
            }
        }
        if (!rows[self].ready) {
            v->heading = MSG_CHOOSE;
            v->help = HELP_CHOOSE;
        } else if (host) {
            v->heading = all ? MSG_PRESS_START : MSG_WAIT;
            v->help = all ? HELP_CHANGE_START : HELP_CHANGE;
        } else {
            v->heading = rd16(menu + MENU_MESSAGE) & 0xff; /* "Wait..." and "Wait for the host." in turn */
            v->help = HELP_CHANGE;
        }
    }
}

/* The screen as the game has it after its update (its overlay loaded). */
static int read_screen(const Screen *sc, GameRow rows[KHDAYS_SELECT_PLACES], KhdaysSelectView *v)
{
    const u32 session = rd32(sc->session), menu = rd32(sc->menu);
    int self, shown_character;
    u16 present;
    memset(v, 0, sizeof(*v));
    if (!in_ram(session) || !in_ram(menu)) {
        return 0;
    }
    self = sc->self() & 0xffff;
    if (self >= KHDAYS_SELECT_PLACES) {
        return 0; /* the game's screen is not taking moves yet (Ov006_MissionGetCursorEntry's 0xff) */
    }
    v->self = self;
    v->choosable = ((1u << ALWAYS) - 1u) | ((uint32_t)(rd8(menu + MENU_BOUGHT) & 0x3fu) << ALWAYS);
    present = rd16(session + NAMES_PRESENT);
    for (int i = 0; i < KHDAYS_SELECT_PLACES; ++i) {
        KhdaysSelectPlace *p = &v->places[i];
        const u32 at = session + ROWS + (u32)i * ROW_SIZE;
        const u8 flags = rd8(at + ROW_FLAGS);
        rows[i].count = rd16(at + ROW_COUNT);
        rows[i].present = flags & 1u;
        rows[i].ready = (flags >> 1) & 1u;
        rows[i].character = (s8)rd8(at + ROW_CHARACTER);
        p->present = rows[i].present != 0;
        p->ready = rows[i].ready != 0;
        p->character = (rows[i].character >= 0 && rows[i].character < KHDAYS_SELECT_CHARACTERS) ||
                               khdays_characters_known(rows[i].character)
                           ? rows[i].character
                           : -1;
        if ((present >> i) & 1u) {
            for (u32 k = 0; k < NAME_LENGTH; ++k) {
                p->name[k] = rd16(session + NAMES + ((u32)i * NAME_LENGTH + k) * 2u);
            }
        }
    }
    messages(sc, rows, self, menu, v);
    shown_character = want >= 0 ? want : v->places[self].character;
    if (on_random && !v->places[self].ready) {
        shown_character = -1;
        v->random = 1;
    }
    v->character = shown_character;
    for (int i = 0; i < 4; ++i) {
        v->stat_label[i] = rd8(sc->captions + 6u + (u32)i);
    }
    for (int i = 0; i < 3; ++i) {
        v->stat_label[4 + i] = rd8(sc->captions + (u32)i);
    }
    if (shown_character >= 0) {
        /* Ov006_MissionApplyParameterRow: the left column's gauges from bytes 4, 0, 2, 1; the
         * right's from 7, 3, 8 */
        static const u8 bytes[KHDAYS_SELECT_STATS] = {4, 0, 2, 1, 7, 3, 8};
        const KhdaysCharacter *mod = khdays_characters_numbered(shown_character);
        /* a mod's: its own nine numbers, else its base's row */
        const int row = khdays_characters_menu_entry(shown_character);
        for (int i = 0; i < KHDAYS_SELECT_STATS; ++i) {
            v->stat[i] = mod != NULL && mod->has_gauges ? mod->gauges[bytes[i]]
                                                        : rd8(sc->stats + (u32)row * STAT_ROW + bytes[i]);
        }
    }
    v->language = language();
    v->master = brightness();
    v->shown = available(v->language);
    return 1;
}

/* KHDAYS_TEST_CHARACTER=<mod id>/<key>: this copy's place is moved to that mod's character once, on the
 * game's own screen too (the walks run without the port's screen's layout); -1 when not asked. */
static int test_character(void)
{
    static int asked_for = -2;
    if (asked_for == -2) {
        const char *identity = getenv("KHDAYS_TEST_CHARACTER");
        asked_for = -1;
        for (int i = 0; identity != NULL && i < khdays_characters_count(); ++i) {
            if (strcmp(khdays_characters_at(i)->identity, identity) == 0) {
                asked_for = khdays_characters_at(i)->number;
            }
        }
    }
    return asked_for;
}

/* After the game's update: the player's move asked of the game as its own input would. */
/* KHDAYS_TRACE_CHARACTERS: the moves asked and answered. */
static int tracing(void)
{
    static int on_trace = -1;
    if (on_trace < 0) {
        on_trace = getenv("KHDAYS_TRACE_CHARACTERS") != NULL;
    }
    return on_trace;
}

static void move_self(const Screen *sc, GameRow rows[KHDAYS_SELECT_PLACES], const KhdaysSelectView *v)
{
    GameRow row;
    const KhdaysSelectPlace *self = &v->places[v->self];
    if (want < 0) {
        return;
    }
    if (tracing()) {
        static int told_want = -2, told_on = -2, told_state = -2, told_gate = -2;
        const int gate = sc->flag_set() << 3 | sc->confirming() << 2 | sc->entry_active() << 1 | asked;
        if (told_want != want || told_on != self->character || told_state != sc->state() || told_gate != gate) {
            told_want = want;
            told_on = self->character;
            told_state = sc->state();
            told_gate = gate;
            khdays_log(
                "charselect: place %d on %d (row %d), the port asks %d; ready %d, state %d, flag/confirm/active/asked %x",
                v->self, self->character, rows[v->self].character, want, self->ready, sc->state(), gate);
        }
    }
    if (self->character == want) {
        want = -1; /* the game has it */
        asked = 0;
        a_give = a_kept;
        a_kept = 0;
        return;
    }
    if (self->ready || !choosable(v, want) || taken(v, want) ||
        (asked && khdays_events_vblank_count() - asked_at > ASK_WAIT)) {
        want = -1; /* chosen, gone or never answered: the game's stays */
        asked = 0;
        a_kept = 0;
        return;
    }
    if (sc->flag_set() && !sc->confirming() && sc->state() == STATE_CHOOSING && sc->entry_active()) {
        /* Ov006_MissionArmCursorRequest: the host's row itself, a guest's request */
        const u32 session = rd32(sc->session);
        const int place = rd32(session + LOCKED_OUT) != 0 ? 0 : v->self;
        const u32 at = place == 0 ? session + ROWS : session + REQUEST;
        row = rows[v->self];
        wr8(at, (u8)place);
        wr8(at + ROW_FLAGS, (u8)((rd8(at + ROW_FLAGS) & ~3u) | 1u | (row.ready ? 2u : 0u)));
        wr8(at + ROW_CHARACTER, (u8)(s8)want);
        wr16(at + ROW_COUNT, (u16)(row.count + 1u));
        if (!asked) {
            asked = 1;
            asked_at = khdays_events_vblank_count();
        }
    }
}

void khdays_charselect_update_ran(uintptr_t update)
{
    for (int i = 0; i < SCREENS; ++i) {
        if (update == (uintptr_t)screens[i].leaving && on == i && now.shown) {
            /* the game leaves the screen (START, or the group gone): its fade over the port's, the
             * places as they were, so that the DS's screen never shows on the way out */
            seen_at = khdays_events_vblank_count();
            now.master = brightness();
            AcquireSRWLockExclusive(&lock);
            shown = now;
            ReleaseSRWLockExclusive(&lock);
            return;
        }
        if (update == (uintptr_t)screens[i].update && khdays_overlay_loaded(screens[i].overlay)) {
            GameRow rows[KHDAYS_SELECT_PLACES];
            KhdaysSelectView v;
            if (!read_screen(&screens[i], rows, &v)) {
                return;
            }
            on = i;
            seen_at = khdays_events_vblank_count();
            {
                /* asked again until the place is on it: a move not yet answered is forgotten when the
                 * screen's update misses a frame */
                static int test_reached;
                const int test = test_character();
                if (test >= 0 && !test_reached) {
                    if (v.places[v.self].character == test) {
                        test_reached = 1;
                    } else if (want < 0 && !v.places[v.self].ready) {
                        want = test;
                    }
                }
            }
            if (v.shown || want >= 0) {
                move_self(&screens[i], rows, &v);
                if (want >= 0) {
                    /* the player sees the move at once */
                    read_screen(&screens[i], rows, &v);
                }
            }
            now = v;
            AcquireSRWLockExclusive(&lock);
            shown = v;
            ReleaseSRWLockExclusive(&lock);
            return;
        }
    }
}

/* ---- The buttons ------------------------------------------------------------------------------ */

/* Whether `key` moves this frame: on its press, then again while held. */
static int repeated(u16 buttons, u16 key)
{
    const u32 t = khdays_events_vblank_count();
    if ((buttons & key) && !(held & key)) {
        repeat_dir = key;
        repeat_from = t;
        repeat_count = 0;
        return 1;
    }
    if ((buttons & key) && repeat_dir == key && t - repeat_from >= REPEAT_AFTER + repeat_count * REPEAT_EVERY) {
        ++repeat_count;
        return 1;
    }
    return 0;
}

uint16_t khdays_charselect_buttons(uint16_t buttons)
{
    /* the directions, then L and R (the pages: the game's select does not read them) */
    static const u16 keys[6] = {KHDAYS_KEY_UP,    KHDAYS_KEY_DOWN, KHDAYS_KEY_LEFT,
                                KHDAYS_KEY_RIGHT, KHDAYS_KEY_L,    KHDAYS_KEY_R};
    static const int moves[6] = {KHDAYS_SELECT_UP,    KHDAYS_SELECT_DOWN,          KHDAYS_SELECT_LEFT,
                                 KHDAYS_SELECT_RIGHT, KHDAYS_SELECT_PREVIOUS_PAGE, KHDAYS_SELECT_NEXT_PAGE};
    const u16 directions = KHDAYS_KEY_UP | KHDAYS_KEY_DOWN | KHDAYS_KEY_LEFT | KHDAYS_KEY_RIGHT;
    const int a_pressed = (buttons & KHDAYS_KEY_A) && !(held & KHDAYS_KEY_A);
    const int y_pressed = (buttons & KHDAYS_KEY_Y) && !(held & KHDAYS_KEY_Y);
    u16 out = buttons;
    int choosing;
    if (on < 0 || khdays_events_vblank_count() - seen_at > SEEN_FRAMES) {
        if (on >= 0) {
            on = -1; /* the screen left: nothing of it kept */
            want = -1;
            on_random = asked = a_kept = a_give = 0;
            AcquireSRWLockExclusive(&lock);
            memset(&shown, 0, sizeof(shown));
            ReleaseSRWLockExclusive(&lock);
        }
        held = buttons;
        return buttons;
    }
    choosing = !now.places[now.self].ready;
    if (!choosing || !active()) {
        on_random = 0;
    }
    if (active()) {
        const int paged = khdays_select_pages(grid_of(&now)) > 1;
        out &= (u16)~directions; /* the port's grid, not the game's */
        if (paged) {
            out &= (u16) ~(KHDAYS_KEY_L | KHDAYS_KEY_R); /* its pages */
        }
        if (choosing) {
            const int from = on_random ? RANDOM : want >= 0 ? want : now.places[now.self].character;
            for (int k = 0; k < (paged ? 6 : 4); ++k) {
                if (repeated(buttons, keys[k])) {
                    const int to = step(&now, from, moves[k]);
                    if (to != from && to >= 0) {
                        on_random = to == RANDOM;
                        want = to == RANDOM ? -1 : to;
                        asked = 0;
                    }
                    break;
                }
            }
            if (on_random && a_pressed) {
                choose_at_random();
            }
        }
    }
    if (choosing && y_pressed) {
        choose_at_random(); /* on the game's own screen too: its directions stay its own */
    }
    if (want >= 0 && a_pressed) {
        a_kept = 1; /* the game would take it on the character before the move */
    }
    if (want >= 0 || a_kept || on_random) {
        out &= (u16)~KHDAYS_KEY_A; /* (on the random choice the game's A would choose its own) */
    }
    if (a_give) {
        out |= KHDAYS_KEY_A;
        a_give = 0;
    }
    held = buttons;
    return out;
}

void *khdays_charselect_help(int message, void *game, void *out, int units)
{
    const u16 *from = (const u16 *)game;
    u16 *to = (u16 *)out;
    const char *word = khdays_text_in(TXT_SELECT_RANDOM, language());
    int n = 0;
    if (message != HELP_CHOOSE || from == NULL || to == NULL || units < 4) {
        return game;
    }
    if (word == NULL) {
        word = khdays_text_in(TXT_SELECT_RANDOM, 1);
    }
    while (from[n] != 0 && n < units - 1) {
        to[n] = from[n];
        ++n;
    }
    /* then two spaces, Y and its word, as the game writes its buttons */
    if (n + 4 < units) {
        to[n++] = ' ';
        to[n++] = ' ';
        to[n++] = GLYPH_Y;
        to[n++] = ' ';
    }
    for (const unsigned char *p = (const unsigned char *)word; *p != 0 && n < units - 1;) {
        to[n++] = khdays_utf8_next(&p);
    }
    to[n] = 0;
    return out;
}

void khdays_charselect_view(KhdaysSelectView *out)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    ReleaseSRWLockShared(&lock);
}

void khdays_charselect_columns(int cols)
{
    InterlockedExchange(&columns, cols > 0 ? cols : 1);
}

void khdays_charselect_rows(int rows)
{
    InterlockedExchange(&page_rows, rows > 0 ? rows : 0);
}
