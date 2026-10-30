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
#include "../../host/app/host.h"
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
    u32 grid; /* its DS grid: two rows of twelve words (-2 none, -1 a bought character's place, else one) */
} Screen;

static const Screen screens[] = {
    {6, Ov006_UpdateMissionMemberMenuScreen, Ov006_MemberMenuNextStateNoOp, Ov006_MissionGetCursorEntry,
     Ov006_MissionScene_IsFlagSet, Ov006_CanConfirmMissionMenu, Ov006_MissionScene_GetState, Ov006_MissionIsEntryActive,
     Ov006_CountPlayers, 0x020565e4u, 0x02056660u, 0x020561d0u, 0x020561e0u, 0x02056410u},
    {8, Ov008_LobbyStep, Ov008_LobbyNextStepNoOp, Ov008_GetLocalPlayerIndex, Ov008_MissionScene_IsFlagSet,
     Ov008_CanConfirmMissionMenu, Ov008_MissionScene_GetState, Ov008_MissionIsEntryActive, Ov008_CountPlayers,
     0x02090f24u, 0x02090fa0u, 0x0208fc8cu, 0x0208fc9cu, 0x02090c00u},
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

/* ---- The game's own screen's pages (no layout of the HD pieces) -------------------------------- */

/* The `k`-th mod's character the select lets be chosen, in its order; -1 past them. */
static int ds_mod(const KhdaysSelectView *v, int k)
{
    for (int p = 0, c; (c = khdays_charselect_at(v, p)) >= 0; ++p) {
        if (c > RANDOM && k-- == 0) {
            return c;
        }
    }
    return -1;
}

static int ds_mod_count(const KhdaysSelectView *v)
{
    int n = 0;
    while (ds_mod(v, n) >= 0) {
        ++n;
    }
    return n;
}

/* Where character `c` is among them; -1 when it is none of them (the game's own). */
static int ds_mod_index(const KhdaysSelectView *v, int c)
{
    for (int k = 0, m; (m = ds_mod(v, k)) >= 0; ++k) {
        if (m == c) {
            return k;
        }
    }
    return -1;
}

int khdays_charselect_ds_at(const KhdaysSelectView *v, int slot)
{
    if (v->ds_page < 1 || slot < 0 || slot >= v->ds_slots) {
        return -1;
    }
    return ds_mod(v, (v->ds_page - 1) * v->ds_slots + slot);
}

/* A slot's middle along the game's grid, in its columns: its second row stands half a column to the right. */
static float ds_x(const KhdaysSelectView *v, int slot)
{
    return (float)v->ds_slot_column[slot] + (v->ds_slot_row[slot] != 0 ? 0.5f : 0.0f);
}

static int last_game = -1; /* the game's character the player left its own grid from */

/* The mod's character nearest place `aim` of the mods' page `page` (0 their first) lets be moved onto: `aim`, then
 * after and before it; -1 none. */
static int ds_on_page(const KhdaysSelectView *v, int page, int aim, int mods)
{
    const int first = page * v->ds_slots, n = mods - first < v->ds_slots ? mods - first : v->ds_slots;
    aim = aim < n ? aim : n - 1;
    for (int k = 0; k < n; ++k) {
        if (aim + k < n && !taken(v, ds_mod(v, first + aim + k))) {
            return ds_mod(v, first + aim + k);
        }
        if (k > 0 && aim - k >= 0 && !taken(v, ds_mod(v, first + aim - k))) {
            return ds_mod(v, first + aim - k);
        }
    }
    return -1;
}

/* Up or down on a mods' page: the nearest free character of the slot's other row. -1 none. */
static int ds_other_row(const KhdaysSelectView *v, int page, int slot, int mods)
{
    const int slots = v->ds_slots, lower = v->ds_slot_row[slot] == 0;
    const float x = ds_x(v, slot);
    int best = -1;
    float best_d = 0.0f;
    for (int s = 0; s < slots && page * slots + s < mods; ++s) {
        const int c = ds_mod(v, page * slots + s);
        const float d = ds_x(v, s) - x, dist = d < 0 ? -d : d;
        if ((v->ds_slot_row[s] != 0) == lower && !taken(v, c) && (best < 0 || dist < best_d)) {
            best = c;
            best_d = dist;
        }
    }
    return best;
}

/* L from the mods' first page: back onto the game's grid, on the character it was left from, else its first
 * free one. -1 none. */
static int ds_back_to_game(const KhdaysSelectView *v)
{
    if (last_game >= 0 && last_game < KHDAYS_SELECT_CHARACTERS && choosable(v, last_game) && !taken(v, last_game)) {
        return last_game;
    }
    for (int c = 0; c < KHDAYS_SELECT_CHARACTERS; ++c) {
        if (choosable(v, c) && !taken(v, c)) {
            return c;
        }
    }
    return -1;
}

/* Where a move (KHDAYS_SELECT_*) leads on the game's own screen with mods' characters: from its own grid, R onto
 * the mods' first page (the game moves on its own grid itself); on their pages along them, between the two rows,
 * and from page to page. -1: nowhere. */
static int ds_step(const KhdaysSelectView *v, int move)
{
    const int from = want >= 0 ? want : v->places[v->self].character;
    const int mods = ds_mod_count(v), slots = v->ds_slots, k = ds_mod_index(v, from);
    if (mods == 0 || slots == 0) {
        return -1;
    }
    if (k < 0) {
        if (move != KHDAYS_SELECT_NEXT_PAGE) {
            return -1;
        }
        last_game = from;
        return ds_on_page(v, 0, 0, mods);
    }
    const int page = k / slots, slot = k % slots;
    switch (move) {
    case KHDAYS_SELECT_LEFT:
    case KHDAYS_SELECT_RIGHT: {
        const int dir = move == KHDAYS_SELECT_RIGHT ? 1 : -1;
        for (int i = 1; i < mods; ++i) {
            const int c = ds_mod(v, ((k + dir * i) % mods + mods) % mods);
            if (!taken(v, c)) {
                return c;
            }
        }
        return -1;
    }
    case KHDAYS_SELECT_UP:
    case KHDAYS_SELECT_DOWN: return ds_other_row(v, page, slot, mods);
    case KHDAYS_SELECT_NEXT_PAGE: return (page + 1) * slots < mods ? ds_on_page(v, page + 1, slot, mods) : -1;
    case KHDAYS_SELECT_PREVIOUS_PAGE: return page > 0 ? ds_on_page(v, page - 1, slot, mods) : ds_back_to_game(v);
    default: return -1;
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

/* The game's own grid's slots: every place of its table that is not an edge, row by row. */
static void read_ds_slots(const Screen *sc, KhdaysSelectView *v)
{
    for (int k = 0; k < KHDAYS_SELECT_DS_GRID; ++k) {
        if ((int32_t)rd32(sc->grid + (u32)k * 4u) != -2) {
            v->ds_slot_row[v->ds_slots] = (signed char)(k / 12);
            v->ds_slot_column[v->ds_slots] = (signed char)(k % 12);
            ++v->ds_slots;
        }
    }
}

/* The mods' characters on pages after the game's own grid, and the page the cursor (on `cursor`) is on. */
static void set_ds_pages(KhdaysSelectView *v, int cursor)
{
    const int mods = ds_mod_count(v), k = ds_mod_index(v, cursor);
    if (mods > 0) {
        v->ds_pages = 1 + (mods + v->ds_slots - 1) / v->ds_slots;
        v->ds_page = k >= 0 ? 1 + k / v->ds_slots : 0;
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
    read_ds_slots(sc, v);
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
    if (!v->shown && v->ds_slots > 0 && !(khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN)) {
        set_ds_pages(v, shown_character); /* the presenter draws them over the lower screen */
    }
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

void *khdays_charselect_name(int member, int weapon, void *game, void *out, int units)
{
    const KhdaysCharacter *c = khdays_characters_numbered(member);
    u16 *to = (u16 *)out;
    int n = 0;
    if (c == NULL || to == NULL || units < 2) {
        return game;
    }
    const char *text = weapon ? khdays_characters_weapon(c, language() - 1) : khdays_characters_name(c, language() - 1);
    if (text == NULL) {
        return game;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p != 0 && n < units - 1;) {
        to[n++] = khdays_utf8_next(&p);
    }
    to[n] = 0;
    return out;
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

/* The keys the port moves with: the directions, then L and R (the pages: the game's select does not read them). */
static const u16 move_keys[6] = {KHDAYS_KEY_UP,    KHDAYS_KEY_DOWN, KHDAYS_KEY_LEFT,
                                 KHDAYS_KEY_RIGHT, KHDAYS_KEY_L,    KHDAYS_KEY_R};
static const int moves[6] = {KHDAYS_SELECT_UP,    KHDAYS_SELECT_DOWN,          KHDAYS_SELECT_LEFT,
                             KHDAYS_SELECT_RIGHT, KHDAYS_SELECT_PREVIOUS_PAGE, KHDAYS_SELECT_NEXT_PAGE};
#define DIRECTIONS (KHDAYS_KEY_UP | KHDAYS_KEY_DOWN | KHDAYS_KEY_LEFT | KHDAYS_KEY_RIGHT)
#define PAGE_KEYS (KHDAYS_KEY_L | KHDAYS_KEY_R)

/* The first of move_keys[first..last) that moves this frame; -1 none. */
static int first_moving(u16 buttons, int first, int last)
{
    for (int k = first; k < last; ++k) {
        if (repeated(buttons, move_keys[k])) {
            return k;
        }
    }
    return -1;
}

/* The port's grid: its directions and, with pages, L and R; the game gets none of them. */
static u16 port_grid_keys(u16 buttons, u16 out, int choosing, int a_pressed)
{
    const int paged = khdays_select_pages(grid_of(&now)) > 1;
    out &= (u16) ~(DIRECTIONS | (paged ? PAGE_KEYS : 0));
    if (!choosing) {
        return out;
    }
    const int k = first_moving(buttons, 0, paged ? 6 : 4);
    if (k >= 0) {
        const int from = on_random ? RANDOM : want >= 0 ? want : now.places[now.self].character;
        const int to = step(&now, from, moves[k]);
        if (to != from && to >= 0) {
            on_random = to == RANDOM;
            want = to == RANDOM ? -1 : to;
            asked = 0;
        }
    }
    if (on_random && a_pressed) {
        choose_at_random();
    }
    return out;
}

/* The game's own screen with mods' characters: L and R turn its pages; on the mods' pages the directions are the
 * port's too. */
static u16 ds_page_keys(u16 buttons, u16 out, int choosing)
{
    const int on_mods = now.ds_page >= 1;
    out &= (u16) ~(PAGE_KEYS | (on_mods ? DIRECTIONS : 0));
    const int k = choosing ? first_moving(buttons, on_mods ? 0 : 4, 6) : -1;
    if (k >= 0) {
        const int to = ds_step(&now, moves[k]);
        if (to >= 0) {
            on_random = 0;
            want = to;
            asked = 0;
        }
    }
    return out;
}

uint16_t khdays_charselect_buttons(uint16_t buttons)
{
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
        out = port_grid_keys(buttons, out, choosing, a_pressed);
    } else if (now.ds_pages > 1) {
        out = ds_page_keys(buttons, out, choosing);
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
