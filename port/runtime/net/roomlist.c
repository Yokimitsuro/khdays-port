/* The port's list of rooms (roomlist.h) over the game's "Select a group to join" (ov006 in scene 7,
 * ov008 in the camp, scene 19) and its members' screen; the game keeps taking the buttons, its rooms
 * in its session context and its cursor in its menu. Without the HD screens' pieces the same list in
 * the game's look (ui/rooms2d.c). docs/RUNTIME_MAP.md section 16.2. */
#include "roomlist.h"

#include "../input/input.h"
#include "../core/io.h"
#include "../core/rom.h"
#include "../core/game_memory.h"
#include "../menus/savelist.h"
#include "../battle/stop_magic.h"
#include "../../host/screens/ui_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern int GetLanguage(void);
extern void Ov006_UpdateMissionMenuOptionScreen(void);
extern void Ov008_UpdateMissionMenuOptionScreen(void);
extern void Ov006_UpdateMissionMenuSelectionScreen(void);
extern void Ov008_UpdateMissionMenuSelectionScreen(void);
extern void func_ov006_020505b0(void);
extern void Ov006_CheckMissionMenuConfirmInput(void);
extern void Ov006_UpdateMissionMenuConfirmScreen(void);
extern void func_ov008_0207c498(void);
extern void Ov008_CheckMissionMenuConfirmInput(void);
extern void Ov008_UpdateMissionMenuConfirmScreen(void);
extern void Ov006_MissionBuildOptionRows(void);
extern void Ov008_MissionMenuWaitReady(void);
#undef PlaySound /* windows.h's (winmm's), not the game's */
extern int PlaySound(int bank, int sound);
int khdays_language_wanted(void); /* language.c */
u32 khdays_events_vblank_count(void);

#define SEEN_FRAMES                                                                                                    \
    2u /* the game's list is gone when its update did not run for longer (the game's
                             * frames: a frame the PC held up is not one without the list) */
#define ROWS_COUNT 0x100u
#define ROWS 0x104u
#define ROW_SIZE 0xc0u
#define ROW_ID 0x4u
#define MENU_CURSOR 0x38u
#define MENU_HOST 0x20u
#define MEMBERS_PRESENT 0x434u
#define MEMBERS_NAMES 0x436u
#define MEMBER_NAME 11u /* UTF-16 characters a name takes (the game's) */
/* this copy's player in gPartyMembers: +2 its level less one (Ov008_RefreshEquipPanel shows it plus
 * one), +6 a figure its level gives (PartyMember_Reset), 0 until the game made the record */
#define PARTY_MEMBER 0x0204c678u
#define PLACES 4 /* the game's group: its host and three guests */
#define JOIN_WAIT                                                                                                      \
    300u                     /* VBlanks a chosen room may take to reach the game's list (it is told
                             * of it at once on the local network, within a second online) */
#define REPEAT_AFTER 20u     /* a held direction moves again after this many VBlanks, */
#define REPEAT_EVERY 5u      /* then every this many (the port's) */
#define GAME_REPEAT_AFTER 6u /* the same on the game's own screens: its list's */
#define GAME_REPEAT_EVERY 2u /* (Ov006_UpdateMissionMenuCursor) */
#define PLATES 4             /* the game's list's plates, its button below them */

static const struct {
    void (*update)(void), (*members[4])(void); /* the members' screen, and the states before it */
    void (*after)(void);                       /* the state after it, the character select's first */
    u32 session, menu;
} screens[] = {
    {Ov006_UpdateMissionMenuOptionScreen,
     {Ov006_UpdateMissionMenuSelectionScreen, func_ov006_020505b0, Ov006_CheckMissionMenuConfirmInput,
      Ov006_UpdateMissionMenuConfirmScreen},
     Ov006_MissionBuildOptionRows,
     0x020565e4u,
     0x02056660u},
    {Ov008_UpdateMissionMenuOptionScreen,
     {Ov008_UpdateMissionMenuSelectionScreen, func_ov008_0207c498, Ov008_CheckMissionMenuConfirmInput,
      Ov008_UpdateMissionMenuConfirmScreen},
     Ov008_MissionMenuWaitReady,
     0x02090f24u,
     0x02090fa0u},
};
#define SCREENS ((int)(sizeof(screens) / sizeof(screens[0])))

/* A growing UTF-8 string. */
typedef struct Text {
    char *s;
    size_t n, room;
} Text;

static int on = -1;         /* screens' index whose update ran last, -1 none */
static u32 frames, seen_at; /* the game's frames (khdays_roomlist_buttons), the last with the list */
static int members_on = -1; /* the same for the members' screen */
static u32 members_seen;
static Text session_name; /* the room's name, as chosen or joined here */
static int host_view, self_view, proceed_view;
static int mode = KHDAYS_ROOMS_NONE, cursor, editing, wrong_password;
static char mods_needed[1024]; /* the host's gameplay mods when the last join was refused for them */
static int mods_more;

/* The Stop magic among them, in this copy's language: the host's off, or this copy's off and the host's on. */
static void stop_needed(void)
{
    const int host_off = khdays_net_last_refusal_stop_off();
    const size_t n = strlen(mods_needed);
    if (!host_off && khdays_stop_enabled()) {
        return; /* both on: not why */
    }
    snprintf(mods_needed + n, sizeof(mods_needed) - n, "%s%s", n > 0 ? ", " : "",
             khdays_text(host_off ? TXT_STOP_MAGIC_OFF : TXT_STOP_MAGIC_ON));
}
/* Without the HD screens' pieces the game's own screens draw the list (ui/rooms2d.c): its rows in
 * the game's order, the action last on the game's button, the others on its four plates from `top` */
static int look_game, top;
static int cursor_kind = -1, cursor_which; /* the cursor's row when it is not a room, kept there */
static Text search, room_name, password;
static unsigned char selected[8], target[8];
static int selected_room; /* the cursor's row is a room: `selected` its id */
static int join_pending, create_pending;
static u32 join_since;
static u16 held;
static int repeat_dir;
static u32 repeat_from, repeat_count;
/* this frame's rows, and the rooms' names they point into */
static KhdaysRoomRow *rows;
static int row_count, row_room;
static Text names;
/* what the window's thread typed, for the game's thread to take */
static SRWLOCK edit_lock = SRWLOCK_INIT;
static Text typed;
static int *keys;
static int key_count, key_room;
static volatile LONG editing_now;
/* the view published */
static SRWLOCK view_lock = SRWLOCK_INIT;
static KhdaysRoomView *shown;

static int in_ram(u32 address)
{
    return khdays_game_ram(address); /* main RAM or the game's heaps */
}

static int language(void)
{
    return khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
}

/* Whether a mod gives the port's screens in the language (layouts/rooms/<lang>/rooms.txt). */
static int available(int lang)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    static int asked_lang = -1, answer;
    char relative[64], path[1024];
    if (lang != asked_lang) {
        asked_lang = lang;
        answer = 0;
        if (lang >= 1 && lang <= 5) {
            snprintf(relative, sizeof(relative), "layouts/rooms/%s/rooms.txt", codes[lang]);
            answer = khdays_mods_find(relative, path, (int)sizeof(path));
        }
    }
    return answer;
}

/* ---- Text ------------------------------------------------------------------------------------- */

static int text_append(Text *t, const char *bytes, size_t n)
{
    if (t->n + n + 1 > t->room) {
        size_t want = t->room != 0 ? t->room * 2 : 32;
        char *more;
        while (want < t->n + n + 1) {
            want *= 2;
        }
        more = (char *)realloc(t->s, want);
        if (more == NULL) {
            return 0;
        }
        t->s = more;
        t->room = want;
    }
    memcpy(t->s + t->n, bytes, n);
    t->n += n;
    t->s[t->n] = '\0';
    return 1;
}

static void text_clear(Text *t)
{
    t->n = 0;
    if (t->s != NULL) {
        t->s[0] = '\0';
    }
}

static const char *text_of(const Text *t)
{
    return t->s != NULL ? t->s : "";
}

/* The last character erased (a UTF-8 character's bytes all go). */
static void text_erase(Text *t)
{
    while (t->n > 0) {
        const unsigned char c = (unsigned char)t->s[--t->n];
        if ((c & 0xc0) != 0x80) {
            break;
        }
    }
    if (t->s != NULL) {
        t->s[t->n] = '\0';
    }
}

/* UTF-16 (ended) as UTF-8, appended. */
static void text_utf16(Text *t, const unsigned short *w, int count)
{
    for (int i = 0; i < count && w[i] != 0; ++i) {
        const unsigned c = w[i];
        char b[3];
        if (c < 0x80) {
            b[0] = (char)c;
            text_append(t, b, 1);
        } else if (c < 0x800) {
            b[0] = (char)(0xc0 | c >> 6);
            b[1] = (char)(0x80 | (c & 0x3f));
            text_append(t, b, 2);
        } else {
            b[0] = (char)(0xe0 | c >> 12);
            b[1] = (char)(0x80 | (c >> 6 & 0x3f));
            b[2] = (char)(0x80 | (c & 0x3f));
            text_append(t, b, 3);
        }
    }
}

static unsigned char lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c;
}

/* Whether `in` holds `part`, ASCII letters whatever their case. */
static int holds(const char *in, size_t n, const char *part)
{
    const size_t k = strlen(part);
    if (k == 0) {
        return 1;
    }
    for (size_t i = 0; i + k <= n; ++i) {
        size_t j = 0;
        while (j < k && lower((unsigned char)in[i + j]) == lower((unsigned char)part[j])) {
            ++j;
        }
        if (j == k) {
            return 1;
        }
    }
    return 0;
}

/* ---- The game's list -------------------------------------------------------------------------- */

void khdays_roomlist_update_ran(uintptr_t update)
{
    for (int i = 0; i < SCREENS; ++i) {
        if (update == (uintptr_t)screens[i].update) {
            on = i;
            seen_at = frames;
        }
        for (int k = 0; k < 4; ++k) {
            if (update == (uintptr_t)screens[i].members[k]) {
                members_on = i;
                members_seen = frames;
            }
        }
        if (update == (uintptr_t)screens[i].after && mode == KHDAYS_ROOMS_MEMBERS) {
            /* the host went on: the room stays while the game fades out to the character select */
            members_on = i;
            members_seen = frames;
        }
    }
}

/* The game's list's row holding room `id`, -1 when it has not heard of it yet. */
static int game_row(const unsigned char *id)
{
    const u32 session = rd32(screens[on].session);
    u32 count;
    if (!in_ram(session)) {
        return -1;
    }
    count = *(const volatile u8 *)KHDAYS_W(session + ROWS_COUNT);
    for (u32 i = 0; i < count && i < 4u; ++i) {
        if (memcmp((const void *)KHDAYS_W(session + ROWS + i * ROW_SIZE + ROW_ID), id, 6) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Puts the game's cursor on `index` (0 create, 1-4 a room). */
static int game_cursor(u32 index)
{
    const u32 menu = rd32(screens[on].menu);
    if (!in_ram(menu)) {
        return 0;
    }
    wr32(menu + MENU_CURSOR, index);
    return 1;
}

/* The owner's nickname (the firmware's user settings, runtime/core/boot.c), the name a room gets
 * unless the player gives it another. */
static void nickname(Text *t)
{
    unsigned short name[10];
    const u32 length = *(const volatile u16 *)KHDAYS_W(0x027ffc9au);
    for (u32 i = 0; i < 10; ++i) {
        name[i] = i < length ? *(const volatile u16 *)KHDAYS_W(0x027ffc86u + 2 * i) : 0;
    }
    text_clear(t);
    text_utf16(t, name, 10);
}

/* ---- The rows --------------------------------------------------------------------------------- */

static KhdaysRoomRow *add_row(int kind, int which)
{
    KhdaysRoomRow *r;
    if (row_count == row_room) {
        const int want = row_room != 0 ? row_room * 2 : 16;
        KhdaysRoomRow *more = (KhdaysRoomRow *)realloc(rows, (size_t)want * sizeof(*rows));
        if (more == NULL) {
            return NULL;
        }
        rows = more;
        row_room = want;
    }
    r = &rows[row_count++];
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    r->which = which;
    return r;
}

/* This frame's rows for the mode: the rooms the search lets through, in the order known. A row's
 * text is an offset into `names` until the view is published (names may move as it grows). */
static void build_rows(void)
{
    char buffer[256];
    row_count = 0;
    text_clear(&names);
    if (mode == KHDAYS_ROOMS_LIST) {
        /* Create a Group and the search first, the rooms after them: however many rooms there are,
         * the two are as near (the screen keeps them in sight). On the game's screens its order:
         * the search, the rooms, Create a Group on its button. */
        const int count = khdays_net_room_count();
        int shown_rooms = 0;
        if (!look_game) {
            add_row(KHDAYS_ROOMROW_ACTION, KHDAYS_ROOMACTION_CREATE);
        }
        add_row(KHDAYS_ROOMROW_FIELD, KHDAYS_ROOMFIELD_SEARCH);
        for (int i = 0; i < count; ++i) {
            KhdaysNetRoomView v;
            const int size = khdays_net_room_at(i, &v, buffer, sizeof(buffer));
            size_t start;
            if (size < 0) {
                continue;
            }
            start = names.n;
            if (size >= (int)sizeof(buffer)) {
                /* a long name: asked again whole */
                char *whole = (char *)malloc((size_t)size + 1);
                if (whole != NULL && khdays_net_room_at(i, &v, whole, (unsigned int)size + 1) >= 0) {
                    text_append(&names, whole, strlen(whole));
                }
                free(whole);
            } else {
                text_append(&names, buffer, strlen(buffer));
            }
            {
                /* the search finds a room by its name or its host's */
                Text host = {0};
                int found;
                text_utf16(&host, v.host, (int)(sizeof(v.host) / sizeof(v.host[0])));
                found = holds(names.s != NULL ? names.s + start : "", names.n - start, text_of(&search)) ||
                        holds(text_of(&host), host.n, text_of(&search));
                free(host.s);
                if (!found) {
                    names.n = start;
                    continue;
                }
            }
            text_append(&names, "", 1); /* each name ended */
            {
                KhdaysRoomRow *r = add_row(KHDAYS_ROOMROW_ROOM, 0);
                if (r != NULL) {
                    r->room = v;
                    r->text = (const char *)(uintptr_t)(start + 1); /* an offset, 1-based */
                }
            }
            ++shown_rooms;
        }
        if (look_game) {
            add_row(KHDAYS_ROOMROW_ACTION, KHDAYS_ROOMACTION_CREATE); /* empty plates say there are none */
        } else if (shown_rooms == 0) {
            add_row(KHDAYS_ROOMROW_NONE, 0);
        }
    } else if (mode == KHDAYS_ROOMS_CREATE) {
        add_row(KHDAYS_ROOMROW_FIELD, KHDAYS_ROOMFIELD_NAME);
        add_row(KHDAYS_ROOMROW_FIELD, KHDAYS_ROOMFIELD_PASSWORD);
        add_row(KHDAYS_ROOMROW_ACTION, KHDAYS_ROOMACTION_CREATE);
    } else if (mode == KHDAYS_ROOMS_PASSWORD) {
        add_row(KHDAYS_ROOMROW_FIELD, KHDAYS_ROOMFIELD_PASSWORD);
        add_row(KHDAYS_ROOMROW_ACTION, KHDAYS_ROOMACTION_JOIN);
    }
}

/* This copy's level: the game's record of its player once made (in Mission Mode's camp), else the
 * level the title's list counted for the file loaded (a group made from the title, before the camp). */
static int own_level(void)
{
    if (*(const volatile u16 *)KHDAYS_W(PARTY_MEMBER + 6u) != 0) {
        return *(const volatile u8 *)KHDAYS_W(PARTY_MEMBER + 2u) + 1;
    }
    return khdays_savelist_played_level();
}

/* The members' screen's rows: the group's four places, each its player's name (the game's), level and
 * the ping to it as the session measures it (the host's guests; a guest's host). */
static void build_members(void)
{
    const u32 session = rd32(screens[members_on].session), menu = rd32(screens[members_on].menu);
    unsigned int aids[PLACES * 2], level_aids[PLACES * 2];
    int ms[PLACES * 2], levels[PLACES * 2], measured, leveled, present, count = 0;
    row_count = 0;
    text_clear(&names);
    host_view = self_view = proceed_view = 0;
    if (!in_ram(session) || !in_ram(menu)) {
        return;
    }
    present = *(const volatile u16 *)KHDAYS_W(session + MEMBERS_PRESENT);
    /* this copy's level goes to the room with its pings; everyone's come back */
    khdays_net_set_level(own_level());
    leveled = khdays_net_levels(level_aids, levels, PLACES * 2);
    measured = khdays_net_round_trips(aids, ms, PLACES * 2);
    for (int place = 0; place < PLACES; ++place) {
        unsigned short name[MEMBER_NAME];
        KhdaysRoomRow *r = add_row(KHDAYS_ROOMROW_MEMBER, place);
        const size_t start = names.n;
        if (r == NULL) {
            break;
        }
        r->room.players = (present >> place) & 1;
        r->room.ping_ms = -1;
        r->level = 0;
        for (int i = 0; i < leveled && i < PLACES * 2; ++i) {
            if ((int)level_aids[i] == place) {
                r->level = levels[i];
            }
        }
        for (int i = 0; i < measured && i < PLACES * 2; ++i) {
            if ((int)aids[i] == place) {
                r->room.ping_ms = ms[i];
            }
        }
        for (u32 k = 0; k < MEMBER_NAME; ++k) {
            name[k] = *(const volatile u16 *)KHDAYS_W(session + MEMBERS_NAMES + (u32)place * MEMBER_NAME * 2 + 2 * k);
        }
        if (r->room.players) {
            text_utf16(&names, name, (int)MEMBER_NAME);
            ++count;
        }
        text_append(&names, "", 1);
        r->text = (const char *)(uintptr_t)(start + 1); /* an offset, 1-based, as a room's */
    }
    host_view = rd32(menu + MENU_HOST) != 0;
    self_view = khdays_net_own_aid();
    proceed_view = host_view && count >= 2;
}

static int selectable(int i)
{
    return i >= 0 && i < row_count && rows[i].kind != KHDAYS_ROOMROW_NONE;
}

/* The cursor kept on the room it was on when the rooms change, else within the rows (on the game's
 * screens also on the search or Create a Group, which the rooms move there). */
static void keep_cursor(void)
{
    if (selected_room) {
        for (int i = 0; i < row_count; ++i) {
            if (rows[i].kind == KHDAYS_ROOMROW_ROOM && memcmp(rows[i].room.id, selected, 6) == 0) {
                cursor = i;
                return;
            }
        }
    } else if (look_game && cursor_kind >= 0 && cursor_kind != KHDAYS_ROOMROW_ROOM) {
        for (int i = 0; i < row_count; ++i) {
            if (rows[i].kind == cursor_kind && rows[i].which == cursor_which) {
                cursor = i;
                return;
            }
        }
    }
    if (cursor >= row_count) {
        cursor = row_count - 1;
    }
    if (cursor < 0) {
        cursor = 0;
    }
    while (!selectable(cursor) && cursor > 0) {
        --cursor;
    }
}

/* The cursor's row now, to find it again in the rows rebuilt: a room by its id, another by its kind. */
static void remember_cursor(void)
{
    selected_room = selectable(cursor) && rows[cursor].kind == KHDAYS_ROOMROW_ROOM;
    if (selected_room) {
        memcpy(selected, rows[cursor].room.id, 6);
    }
    cursor_kind = selectable(cursor) ? rows[cursor].kind : -1;
    cursor_which = selectable(cursor) ? rows[cursor].which : 0;
}

static void move(int dir)
{
    int next = cursor + dir;
    if (look_game) {
        /* the game's list goes round, its sound when there is more than one row */
        for (int tries = 0; tries < row_count; ++tries, next += dir) {
            next = (next % row_count + row_count) % row_count;
            if (selectable(next)) {
                if (row_count > 1) {
                    PlaySound(0, 0);
                }
                cursor = next;
                return;
            }
        }
        return;
    }
    while (next >= 0 && next < row_count && !selectable(next)) {
        next += dir;
    }
    if (selectable(next)) {
        cursor = next;
    }
}

/* ---- What is typed ---------------------------------------------------------------------------- */

static Text *field_text(int which)
{
    return which == KHDAYS_ROOMFIELD_SEARCH ? &search : which == KHDAYS_ROOMFIELD_NAME ? &room_name : &password;
}

/* For a test run without a keyboard: KHDAYS_TEST_TYPE's texts, '|' between them, each typed into the
 * next field that takes text, which then ends as with Enter. */
static void test_typing(Text *t)
{
    static char *texts;
    static size_t length, at;
    static int read;
    const char *end;
    if (!read) {
        const DWORD size = GetEnvironmentVariableA("KHDAYS_TEST_TYPE", NULL, 0); /* its end included */
        read = 1;
        texts = size > 1 ? (char *)malloc(size) : NULL;
        if (texts != NULL && GetEnvironmentVariableA("KHDAYS_TEST_TYPE", texts, size) == size - 1) {
            length = size - 1;
        }
    }
    if (at >= length) {
        return;
    }
    end = (const char *)memchr(texts + at, '|', length - at);
    text_clear(t);
    text_append(t, texts + at, end != NULL ? (size_t)(end - (texts + at)) : length - at);
    at = end != NULL ? (size_t)(end - texts) + 1 : length;
    editing = 0;
}

/* The window thread's typing, applied to the field being edited. */
static void take_typing(void)
{
    Text *t = editing && selectable(cursor) && rows[cursor].kind == KHDAYS_ROOMROW_FIELD
                  ? field_text(rows[cursor].which)
                  : NULL;
    if (t != NULL) {
        test_typing(t);
    }
    AcquireSRWLockExclusive(&edit_lock);
    if (t != NULL && typed.n > 0) {
        text_append(t, typed.s, typed.n);
    }
    text_clear(&typed);
    for (int i = 0; i < key_count; ++i) {
        if (t == NULL) {
            continue;
        }
        if (keys[i] == KHDAYS_EDIT_ERASE) {
            text_erase(t);
        } else if (keys[i] == KHDAYS_EDIT_CLEAR) {
            text_clear(t);
        } else {
            if (keys[i] == KHDAYS_EDIT_CANCEL && t == &search) {
                text_clear(t);
            }
            editing = 0;
        }
    }
    key_count = 0;
    ReleaseSRWLockExclusive(&edit_lock);
}

int khdays_roomlist_editing(void)
{
    return InterlockedCompareExchange(&editing_now, 0, 0) != 0;
}

void khdays_roomlist_type(const char *utf8)
{
    AcquireSRWLockExclusive(&edit_lock);
    text_append(&typed, utf8, strlen(utf8));
    ReleaseSRWLockExclusive(&edit_lock);
}

void khdays_roomlist_edit(int key)
{
    AcquireSRWLockExclusive(&edit_lock);
    if (key_count == key_room) {
        const int want = key_room != 0 ? key_room * 2 : 8;
        int *more = (int *)realloc(keys, (size_t)want * sizeof(*keys));
        if (more != NULL) {
            keys = more;
            key_room = want;
        }
    }
    if (key_count < key_room) {
        keys[key_count++] = key;
    }
    ReleaseSRWLockExclusive(&edit_lock);
}

/* ---- The view --------------------------------------------------------------------------------- */

static void publish(int screen, int master)
{
    KhdaysRoomView *v, *old;
    size_t texts = names.n + session_name.n + 1 + strlen(mods_needed) + 1, at;
    const int fields[3] = {KHDAYS_ROOMFIELD_SEARCH, KHDAYS_ROOMFIELD_NAME, KHDAYS_ROOMFIELD_PASSWORD};
    char *pool;
    for (int f = 0; f < 3; ++f) {
        texts += field_text(fields[f])->n + 1;
    }
    v = (KhdaysRoomView *)calloc(1, sizeof(*v) + (size_t)row_count * sizeof(KhdaysRoomRow) + texts + 1);
    if (v == NULL) {
        return;
    }
    v->screen = screen;
    v->look_game = look_game;
    v->top = top;
    v->language = language();
    v->cursor = cursor;
    v->editing = editing;
    v->wrong_password = wrong_password;
    v->master = (int16_t)master;
    v->host = host_view;
    v->self = self_view;
    v->can_proceed = proceed_view;
    v->row_count = screen != KHDAYS_ROOMS_NONE ? row_count : 0;
    v->rows = (KhdaysRoomRow *)(v + 1);
    pool = (char *)(v->rows + v->row_count);
    memcpy(pool, names.s != NULL ? names.s : "", names.n);
    at = names.n;
    for (int i = 0; i < v->row_count; ++i) {
        v->rows[i] = rows[i];
        if (rows[i].kind == KHDAYS_ROOMROW_ROOM || rows[i].kind == KHDAYS_ROOMROW_MEMBER) {
            v->rows[i].text = pool + ((uintptr_t)rows[i].text - 1);
        } else if (rows[i].kind == KHDAYS_ROOMROW_FIELD) {
            const Text *t = field_text(rows[i].which);
            memcpy(pool + at, text_of(t), t->n + 1);
            v->rows[i].text = pool + at;
            at += t->n + 1;
        }
    }
    memcpy(pool + at, text_of(&session_name), session_name.n + 1);
    v->title = pool + at;
    at += session_name.n + 1;
    memcpy(pool + at, mods_needed, strlen(mods_needed) + 1);
    v->mods_needed = pool + at;
    v->mods_more = mods_more;
    AcquireSRWLockExclusive(&view_lock);
    old = shown;
    shown = v;
    ReleaseSRWLockExclusive(&view_lock);
    free(old);
}

const KhdaysRoomView *khdays_roomlist_hold(void)
{
    AcquireSRWLockShared(&view_lock);
    return shown;
}

void khdays_roomlist_release(void)
{
    ReleaseSRWLockShared(&view_lock);
}

/* ---- The buttons ------------------------------------------------------------------------------ */

/* Whether `key` moves the cursor this frame: on its press, then again while held. */
static int repeated(u16 buttons, u16 key, int dir)
{
    const u32 now = khdays_events_vblank_count();
    const u32 after = look_game ? GAME_REPEAT_AFTER : REPEAT_AFTER,
              every = look_game ? GAME_REPEAT_EVERY : REPEAT_EVERY;
    if ((buttons & key) && !(held & key)) {
        repeat_dir = dir;
        repeat_from = now;
        repeat_count = 0;
        return 1;
    }
    if ((buttons & key) && repeat_dir == dir && now - repeat_from >= after + repeat_count * every) {
        ++repeat_count;
        return 1;
    }
    return 0;
}

static void start_join(const unsigned char *id)
{
    memcpy(target, id, 6);
    join_pending = 1;
    join_since = khdays_events_vblank_count();
    wrong_password = 0;
    mods_needed[0] = 0;
    mods_more = 0;
    khdays_net_scan_only(target);
}

/* The game's fade: the lower screen's master brightness (MASTER_BRIGHT, engine B). */
static int brightness(void)
{
    const u16 bright = IO16(0x106c);
    const int bmode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
    return bmode == 1 ? factor : bmode == 2 ? -factor : 0;
}

static void reset(void)
{
    mode = KHDAYS_ROOMS_NONE;
    editing = 0;
    join_pending = create_pending = 0;
    selected_room = 0;
    cursor = 0;
    top = 0;
    InterlockedExchange(&editing_now, 0);
    khdays_net_scan_only(NULL);
    publish(KHDAYS_ROOMS_NONE, 0);
}

uint16_t khdays_roomlist_buttons(uint16_t buttons)
{
    static const unsigned char none[8] = {0}; /* an id no room has (theirs are locally administered) */
    const int active = on >= 0 && frames + 1 - seen_at <= SEEN_FRAMES;
    const u16 now = (u16)(buttons & ~held);
    u16 out = 0;
    int master;
    int opened = 0, typing_ended;
    ++frames;
    look_game = !available(language());
    if (!active) {
        const int members = members_on >= 0 && frames - members_seen <= SEEN_FRAMES;
        if (members) {
            if (mode != KHDAYS_ROOMS_MEMBERS) {
                editing = 0;
                join_pending = create_pending = 0;
                InterlockedExchange(&editing_now, 0);
                khdays_net_scan_only(NULL);
                mode = KHDAYS_ROOMS_MEMBERS;
            }
            build_members();
            cursor = proceed_view ? PLACES : -1;
            publish(mode, brightness());
        } else if (mode != KHDAYS_ROOMS_NONE) {
            reset(); /* the game left its list: joining, creating, or back */
        }
        held = buttons;
        return buttons;
    }
    if (mode == KHDAYS_ROOMS_MEMBERS) {
        mode = KHDAYS_ROOMS_NONE; /* back from the members' screen: the list afresh */
    }
    if (mode == KHDAYS_ROOMS_NONE) {
        /* the list (again): a join the host refused for its password is said */
        mode = KHDAYS_ROOMS_LIST;
        cursor = 0;
        selected_room = 0;
        editing = 0;
        wrong_password = khdays_net_last_refusal() == KHDAYS_NET_REFUSE_PASSWORD;
        if (khdays_net_last_refusal() == KHDAYS_NET_REFUSE_MODS) {
            khdays_net_last_refusal_mods(mods_needed, sizeof(mods_needed), &mods_more);
            stop_needed();
        } else {
            mods_needed[0] = 0;
            mods_more = 0;
        }
        opened = 1;
    }
    if (create_pending) {
        /* the room's details are set: the game creates it */
        create_pending = 0;
        held = buttons;
        return game_cursor(0) ? KHDAYS_KEY_A : 0;
    }
    if (join_pending) {
        const int row = game_row(target);
        if (row >= 0 && game_cursor((u32)row + 1u)) {
            join_pending = 0;
            held = buttons;
            return KHDAYS_KEY_A;
        }
        if (khdays_events_vblank_count() - join_since > JOIN_WAIT) {
            join_pending = 0; /* never heard of: back to the list */
        }
    }
    build_rows();
    if (opened && look_game) {
        /* on Create a Group, as the game's list opens */
        cursor = row_count - 1;
        cursor_kind = -1;
        top = 0;
    } else if (opened) {
        /* on the first room, or on Create a Group when there is none */
        cursor = row_count > KHDAYS_ROOMS_FIXED && rows[KHDAYS_ROOMS_FIXED].kind == KHDAYS_ROOMROW_ROOM
                     ? KHDAYS_ROOMS_FIXED
                     : 0;
    }
    keep_cursor();
    {
        /* the press that ends the typing (a gamepad's A) does nothing else */
        const int was_editing = editing;
        take_typing();
        typing_ended = was_editing && !editing;
    }
    if (!editing && !typing_ended && !join_pending) {
        const int mode_before = mode;
        if (repeated(buttons, KHDAYS_KEY_UP, -1)) {
            move(-1);
        } else if (repeated(buttons, KHDAYS_KEY_DOWN, 1)) {
            move(1);
        }
        if ((now & KHDAYS_KEY_A) && selectable(cursor)) {
            /* on the game's screens its sound for the port's own steps (the game's A plays its own) */
            const KhdaysRoomRow *r = &rows[cursor];
            if (r->kind == KHDAYS_ROOMROW_FIELD) {
                editing = 1;
                if (look_game) {
                    PlaySound(0, 1);
                }
            } else if (r->kind == KHDAYS_ROOMROW_ROOM) {
                text_clear(&session_name);
                text_append(&session_name, names.s + ((uintptr_t)r->text - 1),
                            strlen(names.s + ((uintptr_t)r->text - 1)));
                if (r->room.locked) {
                    memcpy(target, r->room.id, 6);
                    text_clear(&password);
                    mode = KHDAYS_ROOMS_PASSWORD;
                    cursor = 0;
                    top = 0;
                    editing = 1;
                    if (look_game) {
                        PlaySound(0, 1);
                    }
                } else {
                    khdays_net_join_password(NULL);
                    start_join(r->room.id);
                }
            } else if (r->which == KHDAYS_ROOMACTION_CREATE && mode == KHDAYS_ROOMS_LIST) {
                if (room_name.n == 0) {
                    nickname(&room_name);
                }
                mode = KHDAYS_ROOMS_CREATE;
                /* the game's: on its button, A again creates the room as its one A did */
                cursor = look_game ? 2 : 0;
                top = 0;
                if (look_game) {
                    PlaySound(0, 1);
                }
            } else if (r->which == KHDAYS_ROOMACTION_CREATE) {
                khdays_net_room_details(text_of(&room_name), text_of(&password));
                text_clear(&session_name);
                text_append(&session_name, text_of(&room_name), room_name.n);
                text_clear(&password);
                create_pending = 1;
                mode = KHDAYS_ROOMS_LIST;
            } else if (r->which == KHDAYS_ROOMACTION_JOIN) {
                khdays_net_join_password(text_of(&password));
                text_clear(&password);
                mode = KHDAYS_ROOMS_LIST;
                start_join(target);
            }
        } else if (now & KHDAYS_KEY_B) {
            if (mode == KHDAYS_ROOMS_LIST) {
                out = KHDAYS_KEY_B; /* the game's own: leaving its list */
            } else {
                text_clear(&password);
                mode = KHDAYS_ROOMS_LIST;
                cursor = 0;
                if (look_game) {
                    PlaySound(0, 3);
                    cursor_kind = KHDAYS_ROOMROW_ACTION; /* back on Create a Group */
                    cursor_which = KHDAYS_ROOMACTION_CREATE;
                    top = 0;
                }
            }
        }
        if (mode == mode_before) {
            remember_cursor(); /* where it moved to */
        } else {
            selected_room = 0; /* the cursor set for the step's own rows */
        }
        build_rows();
        keep_cursor();
    }
    if (look_game) {
        /* the game's plates show the rows before its button from `top`: the cursor's among them */
        const int plated = row_count - 1;
        if (cursor < plated && cursor < top) {
            top = cursor;
        } else if (cursor < plated && cursor >= top + PLATES) {
            top = cursor - PLATES + 1;
        }
        if (top > plated - PLATES) {
            top = plated - PLATES;
        }
        if (top < 0) {
            top = 0;
        }
    }
    /* the game is told of the room under the cursor only (or of the one being joined) */
    remember_cursor();
    if (!join_pending) {
        khdays_net_scan_only(mode == KHDAYS_ROOMS_LIST && selected_room ? selected : none);
    }
    InterlockedExchange(&editing_now, editing);
    master = brightness();
    publish(mode, master);
    held = buttons;
    return out;
}
