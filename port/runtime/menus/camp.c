/* The camp menus (camp.h): Mission Mode's (scene 19, ov008) and the story's (ov025), the same code
 * under other names. The port reads what each screen shows (options, status numbers, members, locked
 * options, Preferences' tabs) and steers Preferences with the game's own presses. Every address and
 * offset, and how each was found: docs/RUNTIME_MAP.md section 16.4. */
#include "camp.h"
#include "savelist.h"
#include "enemy_viewer.h"

#include "../characters/characters.h"

#include "../input/input.h"
#include "../core/io.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../mods/installed.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#undef PlaySound /* the game's sequence function, not Windows' wave playback macro */

extern int PlaySound(int kind, int sound);

extern int GetLanguage(void);
extern int GameState_GetField(int a, int b);
extern int Ov008_GetLocalPlayerIndex(void);
extern void Ov008_RouteCommittedPageState(void);
extern void Ov025_GetIdleHandler(void);
extern int Ov025_GetLocalPlayerCharacter(int unused);
extern int Ov008_SelectTierIfActive(int list, unsigned tab, int c);
extern int Ov025_SelectTierIfActive(int list, unsigned tab, int c);
extern int GetFrameRateMode(void);
extern int GameState_IsFlagSet(int flag);
extern int Ov008_IsSessionReady(void);
int khdays_language_wanted(void); /* language.c */
u32 khdays_events_vblank_count(void);
int khdays_overlay_loaded(int id); /* overlays.c */

/* the menu is gone when the camp's update did not run for longer than the game's slowest pace (a step
 * every three VBlanks, with three players or more) */
#define SEEN_FRAMES 3u
#define SCREEN_STATE 0x959cu
#define NEXT_SCREEN 0x95c4u /* the screen the camp goes to next (SetTargetSlot): -1 out of the camp */
#define ENTRY 0x88u
#define DESCRIPTION 0x5c4u
#define NOTICE 0x5c0u /* the line over the menu: a root_&.s record (see camp.h) */
#define NOTICE_FIRST 0x16
#define NOTICE_LAST 0x1c
#define WAITING 9               /* Mission Mode's entry while the group waits for the mission */
#define MISSION_ACCEPTED 0x200c /* the game state's flag Ov008_DrawMenuValue reads */
#define WAIT_FOR_MISSION 0x20   /* root_&.s: "stay waiting for your mission" */
#define FIRST_OPTION 101
#define ANSWER_YES 7
#define ANSWER_NO 8
#define QUIT_QUESTION 0x15     /* root_&.s: "Quit the game?" */
#define TITLE_SCREEN 6         /* the options' last, whose question it is */
#define GAME_STATE 0x0204be18u /* gGameState: the game's state, a reference */
#define MUNNY 0x8u
#define HEARTS 0xcu
#define ITEM_COUNTS 0x810u
#define PANEL_GRID 0xee0u     /* the game state's panels installed: u16 [3 pages][8 rows x 5 columns] */
#define PANEL_SLOTS_FREE 0xfu /* the slots open: item 1's count and these (Ov025_GridMenuInitStep) */
#define PANEL_SLOTS_MOST 0x78
#define SESSION 0x02090f24u
#define ROWS 0x4acu
#define ROW_SIZE 6u
#define ROW_CHARACTER 2u
#define NAMES_PRESENT 0x434u
#define NAMES 0x436u
#define NAME_LENGTH 11u
#define HEAP 0x02090f00u /* the camp's heap: a reference */
#define MASK 0x2cu
#define RECORDS 0x2eu
#define RECORD_SIZE 6u
#define RECORD_PAGE 3u
#define PAGE_GONE 7           /* Ov008_BuildSlotMatchMask(7): left out of the members shown */
#define STORY_QUESTION 0x5c0u /* the story's: its question open (Ov025_Hub_SetSubMenu) */
#define ROXAS 12              /* mlt_&.s's order */
#define ENTRY_LIST 0x4a38u    /* the context's entries (see above) */
#define ENTRY_ID 0xcu
#define ENTRY_ALTERNATE 0x84u /* bit 0: locked, its alternate (dimmed) set shown */
#define CONFIG_SCREEN 5       /* the Preferences page (see above) */
#define PREFERENCES 4         /* its option: the options' fifth in both camps */
#define CONFIG_TAB 0x0u
#define CONFIG_LIST 0x2u
#define CONFIG_ENTRY 0x4cu
#define CONFIG_VALUES 0x4eu
#define ENTRY_SIZE 0x14u
#define LIST_SIZE 0x12u
#define LISTS 3 /* Ov008_GetPageTableEntry's three */
#define PAGE_SIZE 4u
#define TITLE_SETTINGS 0    /* cfg_&.s: "Adjust your settings." */
#define CAMP_REPEAT 0x963eu /* the camp's KeyRepeat (savelist.c's) */
#define REPEAT_DELAY 2u
#define REPEAT_INTERVAL 4u

/* A camp: its overlay, its object's update while a screen runs, its table A's index (table B's
 * the next word) and the variable whose second word is its context. */
typedef struct Camp {
    int overlay;
    void (*idle)(void);
    u32 screen_index;
    u32 camp;
    int story;
    /* its Preferences page's tables (see above) and its tabs' titles */
    u32 entries, lists, pages;
    int (*tab_title)(int list, unsigned tab, int c);
    /* its Panels page: the status page's gate (Ov008/Ov025_TickMenuState's step 1 raises it, the teardown
     * lowers it; Mission Mode's menu runs that page too, so it is up there as well) and the menu's option that
     * opens it (Mission Mode's entry 102, the second; the story's first) */
    u32 page_b_live;
    int panels_option;
} Camp;

static const Camp camps[] = {
    {8, Ov008_RouteCommittedPageState, 0x02090f0cu, 0x02090f04u, 0, 0x02090598u, 0x0209051cu, 0x02090504u,
     Ov008_SelectTierIfActive, 0x02090f20u, 1},
    {25, Ov025_GetIdleHandler, 0x020b574cu, 0x020b5744u, 1, 0x020b4f64u, 0x020b4ee8u, 0x020b4ed0u,
     Ov025_SelectTierIfActive, 0x020b575cu, 0},
};
#define CAMPS ((int)(sizeof(camps) / sizeof(camps[0])))

/* The story's options: the menu's entry ids top to bottom and each one's description; then the two of
 * Holo-Missions (Normal Missions, Challenges) and of Files (Roxas's Diary, Enemy Profiles), which the
 * game's list shows in its place. */
static const int STORY_ENTRIES[KHDAYS_CAMP_OPTIONS] = {1, 2, 3, 4, 5, 6, 9};
static const int STORY_DESCRIPTIONS[KHDAYS_CAMP_OPTIONS] = {9, 10, 12, 15, 11, 16, 17};
static const int STORY_SUB_ENTRIES[2][2] = {{10, 11}, {12, 13}};
static const int STORY_SUB_DESCRIPTIONS[2][2] = {{18, 19}, {13, 14}};
/* the panel's animated offsets (Ov008_StatusPanel) */
#define OFFSET_Q12 0x1dcu
#define OFFSETS 0x1e0u

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysCampView shown;
static u32 seen_at;
static int seen;
static int last_leaving; /* for the trace */
/* the game's thread's own */
static KhdaysCampField fields[KHDAYS_CAMP_FIELDS];
static int trace = -1;
/* the Preferences page as last read, for the buttons (the game's thread): each setting's tab and
 * entry, each tab's entries the cursor stops on */
static struct {
    int on;
    u32 state, camp, seen_at; /* read last at that VBlank: gone after SEEN_FRAMES without */
    int tabs;
    int tab_of[KHDAYS_CAMP_SETTINGS], entry_of[KHDAYS_CAMP_SETTINGS];
    int stops[KHDAYS_CAMP_SECTIONS][KHDAYS_CAMP_SETTINGS], stop_count[KHDAYS_CAMP_SECTIONS];
    KhdaysCampPage page;
} config;
static volatile LONG page_drawn;
static volatile LONG files_drawn; /* the port drew the story's Files page (khdays_camp_set_files_drawn) */
static struct {
    int on, tab, entry, rest, presses;
    u16 held;
    int dir, repeat_count;
    u32 repeat_from;
} steer;
static int last_screen = -1, last_page = -1, last_entry = -1, last_tab = -1, last_config_entry = -1, last_notice = -1;
static int last_member_page[KHDAYS_CAMP_PLACES] = {-2, -2, -2, -2};

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

static int in_ram(u32 address)
{
    return khdays_game_ram(address); /* main RAM or the game's heaps */
}

static int tracing(void)
{
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_CAMP") != NULL;
    }
    return trace;
}

/* The game's fade: the upper screen's master brightness, the lower's while it is dark (the
 * one-screen picture's default, KOB_DEFAULT). */
static int16_t brightness(void)
{
    const u16 upper = IO16(0x006c), lower = IO16(0x106c);
    const u16 bright = (lower >> 14) == 2 ? lower : upper;
    const int mode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
    return (int16_t)(mode == 1 ? factor : mode == 2 ? -factor : 0);
}

/* ---- The status panel ------------------------------------------------------------------------- */

static volatile int preview_drawn;

void khdays_camp_set_preview_drawn(int drawn)
{
    preview_drawn = drawn;
}

unsigned int khdays_camp_field(void *panel, int field, unsigned int delta, int style, void *values, int animate)
{
    const u32 *v = (const u32 *)values;
    const u8 *p = (const u8 *)panel;
    u32 a, b = 0;
    (void)style;
    if (field < 0 || field >= KHDAYS_CAMP_FIELDS || v == NULL) {
        return delta;
    }
    a = v[0];
    switch (field) {
    case 7:
    case 9:
    case 0xb:
    case 0xd: {
        /* the value, with the panel's animated offset while it moves; capped as drawn */
        static const int offset_of[0xe] = {[7] = 0, [9] = 1, [0xb] = 2, [0xd] = 3};
        if (animate) {
            a += (u32)(int)*(const s16 *)(p + OFFSETS + 2u * (u32)offset_of[field]);
        }
        if ((int)a > (field == 0xd ? 0xff : 999)) {
            a = field == 0xd ? 0xff : 999;
        }
        break;
    }
    case 0xc:
        if (animate) {
            a += *(const u32 *)(p + OFFSET_Q12);
        }
        if ((int)a > 0x64000) {
            a = 0x64000;
        }
        a = (u32)((int)a >> 12);
        break;
    case 0x11: {
        /* the pair, its second capped while the game's count is over (Ov008_DrawStatusField) */
        const u32 state = rd32(GAME_STATE);
        b = v[1];
        if (in_ram(state) && rd8(state + ITEM_COUNTS + 1u) + 0xfu > 0x78u) {
            b = 0x78;
        }
        break;
    }
    case 0x12: b = v[1]; break;
    default: break;
    }
    fields[field].has = 1;
    fields[field].value[0] = a;
    fields[field].value[1] = b;
    /* the Panels page's preview (Ov025_PreviewDropSummary, Ov008's the same: the value as it is, the change in
     * `delta`); the page's own redraw of the stats as they are (Ov025_RefreshStatusPage, after a panel is dropped
     * or the cursor leaves where it fits) passes 0 for each field, the abilities' (5) too. Only the field drawn
     * changes: right after a preview the game redraws the page's tab (0x14) with `animate` set. */
    fields[field].delta = field == 0xc ? (int32_t)delta >> 12 : (int32_t)delta;
    if (tracing()) {
        khdays_log("camp: field 0x%x = %u %u (delta %u, raw 0x%08x 0x%08x, animate %d)", field, a, b, delta, v[0], v[1],
                   animate);
    }
    return preview_drawn ? 0u : delta;
}

int khdays_camp_preview_arrows(int delta)
{
    return preview_drawn ? 0 : delta;
}

/* ---- The game's menu -------------------------------------------------------------------------- */

/* The group's members, as Ov008_Menu_RefreshSlotPanel shows them. */
static void read_members(KhdaysCampView *v)
{
    const u32 heap = rd32(HEAP), session = rd32(SESSION);
    u32 mask;
    u16 named = 0;
    if (!in_ram(heap)) {
        return;
    }
    mask = rd16(heap + MASK);
    if (in_ram(session)) {
        named = rd16(session + NAMES_PRESENT);
    }
    for (int i = 0; i < KHDAYS_CAMP_PLACES; ++i) {
        KhdaysCampMember *m = &v->member[i];
        const u32 record = heap + RECORDS + (u32)i * RECORD_SIZE;
        const int page = rd8(record + RECORD_PAGE);
        m->present = ((mask >> i) & 1u) && page != PAGE_GONE;
        m->page = page; /* as the DS's tag shows it (Ov008_Menu_RefreshSlotPanel: the record's +3 alone) */
        m->character = -1;
        if (in_ram(session)) {
            const int c = (int)(s8)rd8(session + ROWS + (u32)i * ROW_SIZE + ROW_CHARACTER);
            m->character = (c >= 0 && c < KHDAYS_CAMP_CHARACTERS) || khdays_characters_known(c) ? c : -1;
            if ((named >> i) & 1u) {
                for (u32 k = 0; k < NAME_LENGTH; ++k) {
                    m->name[k] = rd16(session + NAMES + ((u32)i * NAME_LENGTH + k) * 2u);
                }
            }
        }
        if (tracing() && (m->present ? m->page : -1) != last_member_page[i]) {
            khdays_log("camp: member %d %s, page %d, character %d", i, m->present ? "there" : "not there", m->page,
                       m->character);
        }
        last_member_page[i] = m->present ? m->page : -1;
    }
}

/* Title Screen's question, its answers 7 and 8: the option stays Title Screen, the question in
 * its description's place. */
static int answer(int entry, KhdaysCampView *v)
{
    if (entry != ANSWER_YES && entry != ANSWER_NO) {
        return 0;
    }
    v->question = 1;
    v->yes = entry == ANSWER_YES;
    v->selected = TITLE_SCREEN;
    v->description = QUIT_QUESTION;
    return 1;
}

/* The option selected and its description: Mission Mode's entries 101-107 (the description the
 * game set, +0x5c4), the story's ids 1-6 and 9 and its options' own while no question is open;
 * either's question's answers; 0 otherwise (the game's screen shows). */
/* Mission Mode's description as Ov008_DrawMenuValue draws it: its record, but "stay waiting for
 * your mission" while the mission is accepted and the session not ready. */
static int mission_description(u32 state)
{
    if (GameState_IsFlagSet(MISSION_ACCEPTED) && !Ov008_IsSessionReady()) {
        return WAIT_FOR_MISSION;
    }
    return (int)rd16(state + DESCRIPTION);
}

static int selection(const Camp *c, u32 state, KhdaysCampView *v)
{
    const int entry = (int)rd32(state + ENTRY);
    if (!c->story) {
        const int notice = (int)rd16(state + NOTICE);
        v->notice = notice >= NOTICE_FIRST && notice <= NOTICE_LAST ? notice : -1;
        if (answer(entry, v)) {
            return 1;
        }
        if (entry == WAITING) {
            v->waiting = 1;
            v->selected = -1;
            v->description = mission_description(state);
            return 1;
        }
        if (entry < FIRST_OPTION || entry >= FIRST_OPTION + KHDAYS_CAMP_OPTIONS) {
            return 0;
        }
        v->selected = entry - FIRST_OPTION;
        v->description = mission_description(state);
        return 1;
    }
    if (rd32(state + STORY_QUESTION) != 0) {
        return answer(entry, v);
    }
    for (int i = 0; i < KHDAYS_CAMP_OPTIONS; ++i) {
        if (STORY_ENTRIES[i] == entry) {
            v->selected = i;
            v->description = STORY_DESCRIPTIONS[i];
            return 1;
        }
    }
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < 2; ++i) {
            if (STORY_SUB_ENTRIES[s][i] == entry) {
                v->sub = s + 1;
                v->selected = i;
                v->description = STORY_SUB_DESCRIPTIONS[s][i];
                return 1;
            }
        }
    }
    return 0;
}

/* A tab's title (see above): its heading's name, or the game's for its page. */
static int tab_title(const Camp *c, int list, int tab)
{
    const u32 page = c->pages + PAGE_SIZE * rd8(c->lists + LIST_SIZE * (u32)list + 3u + (u32)tab);
    const u32 first = c->entries + ENTRY_SIZE * rd16(page);
    if (rd8(first + 7u) == 0) {
        return rd8(first + 2u);
    }
    if (list != 0) {
        return c->tab_title(list, (unsigned)tab, 0);
    }
    /* the story's: the title of the same page in a list that has titles */
    for (int l = 1; l < LISTS; ++l) {
        const u32 other = c->lists + LIST_SIZE * (u32)l;
        for (int t = 0; t < rd8(other + 2u) && t < KHDAYS_CAMP_SECTIONS; ++t) {
            if (rd8(other + 3u + (u32)t) == rd8(c->lists + LIST_SIZE * (u32)list + 3u + (u32)tab)) {
                return c->tab_title(l, (unsigned)t, 0);
            }
        }
    }
    return TITLE_SETTINGS;
}

/* The Preferences page from the game's state and tables: 0 when it is not as expected. */
static int read_config(const Camp *c, u32 state, KhdaysCampView *v)
{
    KhdaysCampPage *pg = &v->page;
    const int list = rd16(state + CONFIG_LIST), tab = (s16)rd16(state + CONFIG_TAB);
    const int entry = (s16)rd16(state + CONFIG_ENTRY);
    u32 tabs;
    int last_title = -1;
    if (list < 0 || list >= LISTS) {
        return 0;
    }
    tabs = rd8(c->lists + LIST_SIZE * (u32)list + 2u);
    if (tabs == 0 || tabs > KHDAYS_CAMP_SECTIONS || tab < 0 || (u32)tab >= tabs) {
        return 0;
    }
    memset(pg, 0, sizeof(*pg));
    pg->chosen = -1;
    config.tabs = (int)tabs;
    for (u32 t = 0; t < tabs; ++t) {
        const u32 page = c->pages + PAGE_SIZE * rd8(c->lists + LIST_SIZE * (u32)list + 3u + t);
        const int first = rd16(page), count = rd16(page + 2u);
        const int title = tab_title(c, list, (int)t);
        config.stop_count[t] = 0;
        if (first < 0 || first + count > KHDAYS_CAMP_SETTINGS) {
            return 0;
        }
        if (title != last_title) {
            pg->title[pg->sections] = title;
            pg->first[pg->sections] = pg->settings;
            ++pg->sections;
            last_title = title;
        }
        if ((int)t == tab) {
            pg->section = pg->sections - 1;
        }
        for (int e = first; e < first + count; ++e) {
            const u32 at = c->entries + ENTRY_SIZE * (u32)e;
            KhdaysCampSetting *s = &pg->setting[pg->settings];
            const int values = rd8(at + 7u);
            if (values == 0) {
                continue; /* a heading: the section's title */
            }
            s->enabled = rd8(at + 6u) != 0;
            s->name = rd8(at + 2u);
            s->help = rd8(at + 3u);
            s->values = values < KHDAYS_CAMP_VALUES ? values : KHDAYS_CAMP_VALUES;
            for (int i = 0; i < s->values; ++i) {
                s->value_name[i] = rd8(at + 8u + 4u * (u32)i + 2u);
                s->value_help[i] = rd8(at + 8u + 4u * (u32)i + 3u);
            }
            s->value = (s16)rd16(state + CONFIG_VALUES + 2u * (u32)e);
            config.tab_of[pg->settings] = (int)t;
            config.entry_of[pg->settings] = e;
            config.stops[t][config.stop_count[t]++] = e;
            if ((int)t == tab && e == entry) {
                pg->chosen = pg->settings;
            }
            ++pg->settings;
            ++pg->count[pg->sections - 1];
        }
    }
    return 1;
}

/* Whether the entry `id` of the context's list is locked (see above): 0 when it is not there. */
static int entry_locked(u32 camp, int id)
{
    const u32 list = camp + ENTRY_LIST;
    const u32 link = rd16(list + 0xau), count = rd16(list + 8u); /* its numObjects */
    u32 at = rd32(list);
    for (u32 n = 0; n < count && in_ram(at); ++n) {
        if ((int)rd32(at + ENTRY_ID) == id) {
            return (rd8(at + ENTRY_ALTERNATE) & 1u) != 0;
        }
        at = rd32(at + link + 4u);
    }
    return 0;
}

/* Remember the root option, including while one of its nested pages is open; and the story's
 * Holo-Missions' and Files' own option (their two: entries 10/11, 12/13), for the next time each opens.
 * Holo-Missions is the story's entry 2 and Files its entry 3 (STORY_ENTRIES: Panels, Holo-Missions,
 * Files, Tutorials, Config, Save, Title Screen); entry 4 is Tutorials. */
static int remembered_entry[2] = {-1, -1};
static int remembered_sub[2] = {-1, -1};
#define STORY_HOLO_MISSIONS 2
#define STORY_FILES 3

/* Whether the Menu Overhaul is installed (runtime/mods/installed.h): its camp's reads and steering. */
static int overhaul(void)
{
    return khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL);
}

/* The story's option groups (Ov025_Hub_SelectMenuGroup's): 0 the root's seven, 1 Holo-Missions' two,
 * 2 Files' two; -1 another entry (the question's answers). */
static int story_group(int entry)
{
    if (entry == STORY_SUB_ENTRIES[0][0] || entry == STORY_SUB_ENTRIES[0][1]) {
        return 1;
    }
    if (entry == STORY_SUB_ENTRIES[1][0] || entry == STORY_SUB_ENTRIES[1][1]) {
        return 2;
    }
    return (entry >= 1 && entry <= 6) || entry == 9 ? 0 : -1;
}

void khdays_camp_remember_entry(int story, int entry)
{
    if (story && story_group(entry) > 0) {
        remembered_sub[story_group(entry) - 1] = entry;
        entry = story_group(entry) == 1 ? STORY_HOLO_MISSIONS : STORY_FILES;
    }
    if ((!story && entry >= FIRST_OPTION && entry < FIRST_OPTION + KHDAYS_CAMP_OPTIONS) ||
        (story && ((entry >= 1 && entry <= 6) || entry == 9))) {
        remembered_entry[story != 0] = entry;
    }
}

int khdays_camp_restore_entry(int story, int fallback)
{
    const Camp *c = &camps[story != 0];
    const int entry = remembered_entry[story != 0];
    const u32 camp = rd32(c->camp + 4u);
    if (!overhaul()) {
        return fallback;
    }
    return entry >= 0 && in_ram(camp) && !entry_locked(camp, entry) ? entry : fallback;
}

int khdays_camp_hover_at_start(void)
{
    return khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL);
}

extern int Ov025_GetContext(void);
extern unsigned int Ov025_FindEntryById(int context, int id);
extern void Ov025_SwapParamOverrides(int context, unsigned int entry);
extern void Ov025_Hub_OnEntryHover(unsigned int entry, unsigned int keys);

/* The story's menu's cursor taken to an entry as the menu's own start takes it (Ov025_Hub_InitEntries with
 * prepare.py's hook): the entry made the current one, its row hovered (the game's cursor there, its help
 * drawn) with no sound (no entry hovered before it). */
static void story_hover(u32 state, int id)
{
    const int context = Ov025_GetContext();
    const unsigned int entry = Ov025_FindEntryById(context, id);
    if (!in_ram(entry)) {
        return;
    }
    Ov025_SwapParamOverrides(context, entry);
    *(volatile u32 *)KHDAYS_W(state + ENTRY) = 0xffffffffu;
    Ov025_Hub_OnEntryHover(entry, 0x80u);
    if (tracing()) {
        khdays_log("camp: ov025 cursor back on entry %d", id);
    }
}

/* With the Menu Overhaul, each update of the story's menu (its screen 0, no question open). B in Holo-Missions'
 * or Files' two options closes them with Ov025_Hub_SelectMenuGroup(0), whose cursor goes to the option the
 * progress picks, not the one opened: it is put back on it. Opening one of the two, the cursor goes to the one
 * chosen there last. */
static void story_cursor(const Camp *c, int screen, u32 state)
{
    static int last_group = -2;
    int entry, group, want = -1;
    if (!c->story || screen != 0 || !in_ram(state) || rd32(state + STORY_QUESTION) != 0 ||
        !khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        last_group = -2;
        return;
    }
    entry = (int)rd32(state + ENTRY);
    group = story_group(entry);
    if (last_group > 0 && group == 0) {
        want = remembered_entry[1];
    } else if (last_group == 0 && group > 0) {
        want = remembered_sub[group - 1];
    }
    last_group = group;
    if (want >= 0 && want != entry && story_group(want) == group && !entry_locked(rd32(c->camp + 4u), want)) {
        story_hover(state, want);
    }
}

/* PlaySound (prepare.py's hook): with the Menu Overhaul, the presses the port gives Preferences on its way to a
 * setting (steer) make none of the game's cursor and page sounds -- one cursor sound is played for the move. */
int khdays_camp_navigation_sound(int kind, int sound)
{
    return !(config.on && steer.on && kind == 0 && (sound == 0 || sound == 2) &&
             khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL));
}

/* The rows shown's locks: the story's options or the two of its option open, Mission Mode's. */
static void read_locks(const Camp *c, u32 camp, KhdaysCampView *v)
{
    const int rows = v->sub != 0 ? 2 : KHDAYS_CAMP_OPTIONS;
    for (int i = 0; i < rows; ++i) {
        const int id = !c->story ? FIRST_OPTION + i : v->sub != 0 ? STORY_SUB_ENTRIES[v->sub - 1][i] : STORY_ENTRIES[i];
        v->locked[i] = entry_locked(camp, id);
    }
    if (tracing()) {
        static int last = -1;
        int bits = 0;
        for (int i = 0; i < rows; ++i) {
            bits |= v->locked[i] << i;
        }
        if (bits != last) {
            khdays_log("camp: ov%03d locked options %02x (bit 0 the first row)", c->overlay, (unsigned)bits);
            last = bits;
        }
    }
}

/* While the port takes the game's cursor to a setting (khdays_camp_buttons), the page shows that
 * setting as the chosen one: not the tabs and settings the game's cursor passes on its way. */
static void show_steer_target(KhdaysCampPage *pg)
{
    for (int n = 0; n < pg->settings; ++n) {
        if (config.tab_of[n] == steer.tab && config.entry_of[n] == steer.entry) {
            pg->chosen = n;
            for (int k = 0; k < pg->sections; ++k) {
                if (n >= pg->first[k] && n < pg->first[k] + pg->count[k]) {
                    pg->section = k;
                }
            }
            return;
        }
    }
}

/* ---- The story's Files pages ------------------------------------------------------------------- */

/* Roxas's Diary and Enemy Profiles: the story camp's screen 7, one page in two modes (Ov025_GoToPage7A /
 * 7B: the context's word 0x9768, 0 the diary, 1 the enemies). Its state (the screen's, page A:
 * Ov025_Reports_InitPage, 0x278 bytes): s16 the first row shown (+0), the row chosen (+2), the rows
 * (+4); A held (+0xc0: the cursor's row shows its other version); running (+0xc8, 1 from InitPage to
 * Reports_Leave); the entries (+0x1e8: 0x40 bytes each, Reports_LoadEntries -- u16 its id, the diary's
 * day, in bits 0-8 and bits 10-15 set while it is locked; +2 its title's length; +0xc its other version
 * or 0; +0x10 its title, UTF-16); the entries viewed on this visit and those seen before (+0x238, +0x248: bits, the first
 * the highest of the first word; Ov025_QueryItemFlags -- the NEW badge while not seen before, dimmed
 * once viewed). A diary row with another version is viewed as entry + 0x40 (its other version's). A
 * locked enemy's name is the game's "?" (the overlay's string 0x020b5270). */
#define FILES_SCREEN 7
#define FILES_MODE 0x9768u
#define FILES_TOP 0x0u
#define FILES_CURSOR 0x2u
#define FILES_COUNT 0x4u
#define FILES_HELD 0xc0u
#define FILES_RUNNING 0xc8u
#define FILES_ENTRIES 0x1e8u
#define FILES_ENTRY_SIZE 0x40u
#define FILES_OTHER 0xcu
#define FILES_TITLE 0x10u
#define FILES_TITLE_LENGTH 0x2u /* its characters (the texts follow one another in the table's pool) */
#define FILES_VIEWED 0x238u
#define FILES_SEEN 0x248u
#define FILES_BITS 128
#define FILES_LOCKED_NAME 0x020b5270u
#define STORY_FILES_INDEX 2 /* Files: the story's options' third (STORY_ENTRIES) */

/* the Files page as read at this update, for the enemies' model viewer (enemy_viewer.c): its rows' enemies, as
 * many as the page has */
static int files_on, files_mode, files_cursor, files_rows;
static int16_t *files_ids;
static size_t files_id_room;

/* the diary's entries with their rewritten version, as the page linked them when it was open last (its
 * own reckoning, by the last chapter's missions): 0 not known, 1 none, 2 one; as many as the diary has */
static uint8_t *diary_alternate;
static int diary_known; /* the entries diary_alternate holds */

/* The view's lists: the Files page's rows and their titles, the Panels page's description -- as an update reads
 * them (`building`), as the last view read keeps them (`kept`, for the menu closing), and as published with
 * the view (`published`, under `lock`). Each as long as the game's. */
typedef struct Lists {
    KhdaysCampFileRow *rows;
    size_t *title_at; /* each row's title's place in `titles` */
    int row_count, row_room;
    uint16_t *titles;
    size_t title_count, title_room;
    uint16_t *text;
    size_t text_count, text_room;
} Lists;
static Lists building, kept, published;

/* `*list` (`*room` items of `size` bytes) made room for `need`; 0 if no memory. */
static int reserve(void **list, size_t *room, size_t need, size_t size)
{
    size_t grown = *room > 0 ? *room : 16;
    void *more;
    if (need <= *room) {
        return 1;
    }
    while (grown < need) {
        grown *= 2;
    }
    more = realloc(*list, grown * size);
    if (more == NULL) {
        return 0;
    }
    *list = more;
    *room = grown;
    return 1;
}

static int reserve_rows(Lists *l, int need)
{
    size_t room = (size_t)l->row_room, at_room = (size_t)l->row_room;
    if (!reserve((void **)&l->rows, &room, (size_t)need, sizeof(*l->rows)) ||
        !reserve((void **)&l->title_at, &at_room, (size_t)need, sizeof(*l->title_at))) {
        return 0;
    }
    l->row_room = (int)(room < at_room ? room : at_room);
    return 1;
}

static void lists_clear(Lists *l)
{
    l->row_count = 0;
    l->title_count = 0;
    l->text_count = 0;
}

static int lists_copy(Lists *to, const Lists *from)
{
    lists_clear(to);
    if (!reserve_rows(to, from->row_count) ||
        !reserve((void **)&to->titles, &to->title_room, from->title_count, sizeof(*to->titles)) ||
        !reserve((void **)&to->text, &to->text_room, from->text_count + 1, sizeof(*to->text))) {
        return 0;
    }
    if (from->row_count > 0) {
        memcpy(to->rows, from->rows, (size_t)from->row_count * sizeof(*to->rows));
        memcpy(to->title_at, from->title_at, (size_t)from->row_count * sizeof(*to->title_at));
    }
    if (from->title_count > 0) {
        memcpy(to->titles, from->titles, from->title_count * sizeof(*to->titles));
    }
    if (from->text_count > 0) {
        memcpy(to->text, from->text, from->text_count * sizeof(*to->text));
    }
    to->text[from->text_count] = 0;
    to->row_count = from->row_count;
    to->title_count = from->title_count;
    to->text_count = from->text_count;
    return 1;
}

/* A title of the game's (`length` characters at most, a newline as a space) put after the list's titles,
 * NUL-ended: its place there. */
static size_t add_title(Lists *l, u32 text, int length)
{
    const size_t at = l->title_count;
    for (int n = 0; n < length && in_ram(text + 2u * (u32)n); ++n) {
        const u16 ch = rd16(text + 2u * (u32)n);
        if (ch == 0 || !reserve((void **)&l->titles, &l->title_room, l->title_count + 2, sizeof(*l->titles))) {
            break;
        }
        l->titles[l->title_count++] = ch == '\n' ? ' ' : ch;
    }
    if (reserve((void **)&l->titles, &l->title_room, l->title_count + 1, sizeof(*l->titles))) {
        l->titles[l->title_count++] = 0;
    }
    return at;
}

static int files_bit(u32 bits, int n)
{
    return (int)((rd32(bits + 4u * (u32)(n / 32)) >> (31 - n % 32)) & 1u);
}

/* The locked name's length: the game's "?", read to its NUL. */
#define FILES_LOCKED_LENGTH INT_MAX

/* diary_alternate made to hold `count` entries, the new ones not known; 0 if no memory. */
static int know_diary(int count)
{
    uint8_t *more;
    if (count <= diary_known) {
        return 1;
    }
    more = (uint8_t *)realloc(diary_alternate, (size_t)count);
    if (more == NULL) {
        return 0;
    }
    memset(more + diary_known, 0, (size_t)(count - diary_known));
    diary_alternate = more;
    diary_known = count;
    return 1;
}

static int read_files(u32 context, u32 state, KhdaysCampView *v)
{
    KhdaysCampFiles *f = &v->file;
    const u32 entries = rd32(state + FILES_ENTRIES);
    const int count = (s16)rd16(state + FILES_COUNT), top = (s16)rd16(state + FILES_TOP);
    const int cursor = (s16)rd16(state + FILES_CURSOR), mode = (int)rd32(context + FILES_MODE);
    const int held = rd32(state + FILES_HELD) != 0;
    /* running: 1 from InitPage; 0 again once B left it (Reports_Leave) while the game still shows it, fading
     * to the menu -- read on then (the same entries as read last), the window's screen sliding the two into
     * each other meanwhile; 0 before InitPage too, the words there the menu's (not read) */
    static u32 read_entries;
    const u32 running = rd32(state + FILES_RUNNING);
    if (running == 1) {
        read_entries = entries;
    } else if (running != 0 || entries != read_entries || (int)rd32(context + NEXT_SCREEN) != 0) {
        read_entries = 0;
        return 0;
    }
    if (!in_ram(entries) || (mode != 0 && mode != 1) || count < 0 || (count > 0 && (cursor < 0 || cursor >= count))) {
        return 0;
    }
    f->mode = mode;
    f->count = count;
    f->top = top;
    f->cursor = cursor;
    if (!reserve_rows(&building, count) || (mode == 0 && !know_diary(count))) {
        return 0;
    }
    /* the rows the game's memory holds, each entry where the table says */
    for (int i = 0; i < count && in_ram(entries + FILES_ENTRY_SIZE * (u32)i + FILES_ENTRY_SIZE - 1u); ++i) {
        KhdaysCampFileRow *r = &building.rows[building.row_count];
        const u32 e = entries + FILES_ENTRY_SIZE * (u32)i;
        const u16 id = rd16(e);
        const u32 other = rd32(e + FILES_OTHER);
        const int has_other = other != 0 && in_ram(other);
        const int n = mode == 0 && has_other ? i + 0x40 : i;
        memset(r, 0, sizeof(*r));
        r->locked = (int8_t)(mode == 1 && (id >> 10) != 0);
        r->number = (int16_t)(mode == 0 ? (int)(id & 0x1ffu) : i + 1);
        if (r->locked) {
            building.title_at[building.row_count] = add_title(&building, FILES_LOCKED_NAME, FILES_LOCKED_LENGTH);
        } else {
            /* the diary's row chosen shows its other version's title while A is held (Reports_DrawRowTexts) */
            const u32 titled = mode == 0 && held && i == cursor && has_other ? other : e;
            building.title_at[building.row_count] =
                add_title(&building, rd32(titled + FILES_TITLE), (int)rd16(titled + FILES_TITLE_LENGTH));
        }
        r->badge = -1;
        if (!r->locked && n < FILES_BITS) {
            const int known = files_bit(state + FILES_VIEWED, n) | files_bit(state + FILES_SEEN, n) << 1;
            r->badge = (int8_t)(known < 2 ? known : -1);
        }
        /* the game's A mark on the row (Reports_RefreshRowEntries' entry 0x65 + row): the diary's entry
         * with its rewritten version, an enemy met (its drops) */
        r->alternate = (int8_t)(mode == 0 ? has_other : !r->locked && has_other);
        if (mode == 0) {
            diary_alternate[i] = (uint8_t)(has_other ? 2 : 1);
        }
        ++building.row_count;
    }
    f->held = held;
    /* the rows' enemies for the model viewer: each one's class (its record's first byte), -1 not met */
    files_rows = 0;
    if (mode == 1 && reserve((void **)&files_ids, &files_id_room, (size_t)building.row_count, sizeof(*files_ids))) {
        files_rows = building.row_count;
        for (int i = 0; i < files_rows; ++i) {
            const u16 id = rd16(entries + FILES_ENTRY_SIZE * (u32)i);
            files_ids[i] = (int16_t)((id >> 10) == 0 ? (int)(id & 0x1ffu) : -1);
        }
    }
    files_cursor = cursor;
    files_on = running == 1;
    files_mode = mode;
    v->files = 1;
    v->selected = STORY_FILES_INDEX;
    v->description = STORY_SUB_DESCRIPTIONS[1][mode];
    if (tracing()) {
        static int last_mode = -1, last_count = -1, last_cursor = -1;
        if (mode != last_mode || count != last_count || cursor != last_cursor) {
            char first[48];
            int k = 0;
            const int row = cursor < building.row_count ? cursor : 0;
            if (building.row_count > 0) {
                const uint16_t *title = building.titles + building.title_at[row];
                for (; title[k] != 0 && k < (int)sizeof(first) - 1; ++k) {
                    first[k] = title[k] < 0x80 ? (char)title[k] : '?';
                }
            }
            first[k] = '\0';
            khdays_log("camp: files mode %d, %d rows, top %d, cursor %d: number %d \"%s\" badge %d", mode, count, top,
                       cursor, building.row_count > 0 ? building.rows[row].number : -1, first,
                       building.row_count > 0 ? building.rows[row].badge : -2);
            last_mode = mode;
            last_count = count;
            last_cursor = cursor;
        }
    }
    return 1;
}

/* ---- The story's Panels page ------------------------------------------------------------------ */

/* The story camp's screen 2 (Ov025's grid menu: page A its grid's context, Ov008MenuContext -- the grid and
 * the list on the lower screen; page B the status page on the upper one, Ov025_TickMenuState's). Shown by
 * the port once page B is up (its gate 0x020b575c, Ov025_TickMenuState's step 1); not while table B shows
 * another page there (4: the first visit's tutorial). While page B sets up (its step, +0x18, short of 7: its
 * slide's handler, Ov025_TickPageBTransition, not running yet) its upper screen shows the list as set up (its
 * layers not scrolled, its sprites where they are), the numbers chosen at step 3 and slid to at once when the
 * handler starts: the view is the numbers' then, and `panels_early` once its words are drawn (steps 1-2), for the
 * port to take the list from those pictures (the page opening, before X shows it). Page B's view (+0x44: 1 the numbers, 0 the list of what the panels give) and its slide
 * between the two (+0x38, in dots: Ov025_TickPageBTransition's sub BG scroll, a 100 ms tween to 192 or
 * 0 -- Ov025_ChangeMenuSelection). The lower screen's line (its box: row 0 of the grid's text panel,
 * Ov025_RepaintTextRow -- the cell's panel's description, the list's row's, a count) as the game wrote it
 * last (prepare.py's hook), copied then: the description's record goes away with the next. */
#define PANELS_SCREEN 2
#define PAGE_B 0x95a0u
#define PAGE_B_SLIDE 0x38u
#define PAGE_B_VIEW 0x44u
#define PAGE_B_SLIDE_MOST 192
#define PAGE_B_STEP 0x18u
#define PAGE_B_DRAWN 3   /* its words drawn (Ov025_DrawStatusPage at step 1, then step 2) */
#define PAGE_B_SLIDING 7 /* its slide's handler registered (step 6) and running */

static u32 text_row_self;
static uint16_t *text_row; /* NUL-ended, text_row_count units before it */
static size_t text_row_count, text_row_room;

void khdays_camp_text_row(unsigned int self, int row, unsigned int text, int style)
{
    size_t n = 0;
    (void)style;
    if (row != 0 || !reserve((void **)&text_row, &text_row_room, 1, sizeof(*text_row))) {
        return;
    }
    for (; text != 0 && in_ram(text + 2u * (u32)n); ++n) {
        const u16 ch = rd16(text + 2u * (u32)n);
        if (ch == 0 || !reserve((void **)&text_row, &text_row_room, n + 2, sizeof(*text_row))) {
            break;
        }
        text_row[n] = ch;
    }
    text_row[n] = 0;
    text_row_count = n;
    text_row_self = self;
}

static int read_panels(const Camp *c, u32 camp, u32 state, int page, KhdaysCampView *v)
{
    const u32 b = rd32(camp + PAGE_B);
    int slide, step;
    if (page != 0 || !in_ram(b) || rd32(c->page_b_live) == 0) {
        return 0;
    }
    step = (int)rd32(b + PAGE_B_STEP);
    slide = (int)rd32(b + PAGE_B_SLIDE);
    v->panels = 1;
    if (step < PAGE_B_SLIDING) {
        /* setting up: the numbers come (see above) */
        v->panels_view = 1;
        v->panels_slide = PAGE_B_SLIDE_MOST;
        v->panels_early = step >= PAGE_B_DRAWN;
    } else {
        v->panels_view = rd32(b + PAGE_B_VIEW) != 0;
        v->panels_slide = slide < 0 ? 0 : slide > PAGE_B_SLIDE_MOST ? PAGE_B_SLIDE_MOST : slide;
    }
    v->selected = c->panels_option;
    v->description = -1;
    if (text_row_self == state && text_row != NULL &&
        reserve((void **)&building.text, &building.text_room, text_row_count + 1, sizeof(*building.text))) {
        memcpy(building.text, text_row, (text_row_count + 1) * sizeof(*text_row));
        building.text_count = text_row_count;
    }
    if (tracing()) {
        static int last_view = -1, last_step = -1;
        static u32 last_self;
        if (v->panels_view != last_view || text_row_self != last_self ||
            (step != last_step && step <= PAGE_B_SLIDING)) {
            char line[64];
            int k = 0;
            for (; text_row != NULL && text_row[k] != 0 && k < (int)sizeof(line) - 1; ++k) {
                line[k] = text_row[k] < 0x80 ? (char)text_row[k] : '?';
            }
            line[k] = '\0';
            khdays_log("camp: panels step %d, view %d, slide %d, the line's panel %08x (the grid's %08x): \"%s\"", step,
                       v->panels_view, v->panels_slide, text_row_self, state, line);
            last_view = v->panels_view;
            last_self = text_row_self;
            last_step = step;
        }
    }
    return 1;
}

/* ---- Files' NEW on the story's menu ------------------------------------------------------------ */

/* What of Files has an entry never viewed, as its page would badge it (Reports_RefreshRowEntries: an
 * entry's bit clear in the seen words, which Reports_Teardown keeps in the game state's fields 0x45b
 * (the diary) and 0x3c6a (the enemies), eight 16-bit words 0x10 apart, the first bit the highest of
 * the first two words' u32 -- BitArray_TestBit). Its rows as Reports_LoadEntries / CountUnlocked make
 * them from the page's table (UI/cm/str/rpt_&.z, enm_&.z: a u32 count, then 0x14-byte records whose
 * first halfword holds the diary's day in bits 0-8; an enemy's id is the record's first byte): the
 * diary's entries from the first while each one's day is before the story's (359 in the last chapter,
 * field 0x44e), the next one's not after it, and the table has not started again (its second half,
 * the rewritten versions); an entry with its rewritten version linked (the last chapter's, by its
 * missions: the page's own reckoning) is viewed as entry + 0x40 -- known from the page once it was
 * open, else counted new only while neither is seen. The enemies: all the table's, new once met (the
 * game state's byte 0xa11 + id not 0: Ov025_IsFlagRowFree). */
typedef struct KhdaysRomReader KhdaysRomReader; /* rom.h */
KhdaysRomReader *khdays_rom_reader_new(void);
int khdays_rom_file_data(KhdaysRomReader *reader, const char *path, int entry, u8 **data, u32 *size);

#define DIARY_SEEN 0x45b
#define ENEMIES_SEEN 0x3c6a
#define SEEN_WORDS 8
#define CHAPTER 0x44e
#define LAST_CHAPTER 6
#define LAST_DAY 359
#define ENEMY_MET 0xa11u
#define REWRITTEN 0x40
#define TABLE_RECORD 0x14u

static struct {
    int tried;
    u32 count;
    u16 *first; /* each record's first halfword */
} tables[2];

static const u16 *files_table(int mode, u32 *count)
{
    static const char *const codes[] = {"en", "fr", "de", "it", "es"};
    static KhdaysRomReader *reader;
    if (!tables[mode].tried) {
        tables[mode].tried = 1;
        if (reader == NULL) {
            reader = khdays_rom_reader_new();
        }
        for (int i = 0; i < 5 && reader != NULL && tables[mode].first == NULL; ++i) {
            char path[48];
            u8 *data = NULL;
            u32 size = 0;
            snprintf(path, sizeof(path), "UI/cm/str/%s_%s.z", mode == 0 ? "rpt" : "enm", codes[i]);
            if (khdays_rom_file_data(reader, path, -1, &data, &size) && data != NULL && size >= 4u) {
#pragma warning(suppress : 6001) /* khdays_rom_file_data filled `size` bytes of it */
                const u32 n = (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
                /* as many records as the file holds after its count */
                if (n > 0 && (uint64_t)n * TABLE_RECORD <= (uint64_t)size - 4u &&
                    (tables[mode].first = (u16 *)malloc(n * sizeof(u16))) != NULL) {
                    for (u32 k = 0; k < n; ++k) {
                        tables[mode].first[k] = (u16)(data[4u + k * TABLE_RECORD] | data[5u + k * TABLE_RECORD] << 8);
                    }
                    tables[mode].count = n;
                }
            }
            free(data);
        }
        if (tables[mode].first == NULL) {
            khdays_log("camp: no %s table: Files' NEW not shown on the menu", mode == 0 ? "diary" : "enemy");
        }
    }
    *count = tables[mode].count;
    return tables[mode].first;
}

static int seen_bit(const u32 *words, int n)
{
    return n >= FILES_BITS || ((words[n / 32] >> (31 - n % 32)) & 1u) != 0;
}

static void seen_words(int field, u32 *words)
{
    for (int i = 0; i < SEEN_WORDS / 2; ++i) {
        const u32 low = (u32)GameState_GetField(field + 0x20 * i, 16) & 0xffffu;
        const u32 high = (u32)GameState_GetField(field + 0x20 * i + 0x10, 16) & 0xffffu;
        words[i] = low | high << 16; /* the u16 words as the page holds them, read as u32 */
    }
}

static int files_news(u32 game)
{
    int news = 0;
    u32 count = 0, words[SEEN_WORDS / 2];
    const u16 *t = files_table(0, &count);
    if (t != NULL) {
        const int last = GameState_GetField(CHAPTER, 3) == LAST_CHAPTER;
        const u32 day = last ? LAST_DAY : (u32)GameState_GetField(0, 9);
        u32 rows = 0, half = count;
        for (u32 k = 1; k < count; ++k) {
            if ((t[k] & 0x1ffu) < (t[k - 1] & 0x1ffu)) {
                half = k; /* the table starts again: the rewritten versions */
                break;
            }
        }
        while (rows < half && (t[rows] & 0x1ffu) < day && (rows + 1 >= count || (t[rows + 1] & 0x1ffu) <= day)) {
            ++rows;
        }
        seen_words(DIARY_SEEN, words);
        for (u32 j = 0; j < rows && !(news & 1); ++j) {
            const int alternate = j < (u32)diary_known ? diary_alternate[j] : 0; /* 0: not known yet */
            int rewritten = 0;
            for (u32 k = half; last && k < count && !rewritten; ++k) {
                rewritten = (t[k] & 0x1ffu) == (t[j] & 0x1ffu);
            }
            if (!rewritten || alternate == 1) {
                news |= !seen_bit(words, (int)j);
            } else if (alternate == 2) {
                news |= !seen_bit(words, (int)j + REWRITTEN);
            } else {
                news |= !seen_bit(words, (int)j) && !seen_bit(words, (int)j + REWRITTEN);
            }
        }
    }
    if ((t = files_table(1, &count)) != NULL) {
        seen_words(ENEMIES_SEEN, words);
        for (u32 i = 0; i < count && i < FILES_BITS; ++i) {
            if (rd8(game + ENEMY_MET + (t[i] & 0xffu)) != 0 && !seen_bit(words, (int)i)) {
                news |= 2;
                break;
            }
        }
    }
    return news;
}

/* B on the Files page (Reports_Leave) takes the game to its menu with the cursor on Files, its two
 * options closed: the port opens them (Ov025_Hub_SelectMenuGroup(2), as A on Files does) with the
 * cursor on the page's, as the menu was before the page. Once the menu runs (its entries made: the
 * cursor on one of its seven). */
extern void Ov025_Hub_SelectMenuGroup(unsigned int renderer, int group);
#define ALTERNATE_GROUP 0x94u /* the menu's: Holo-Missions' or Files' two shown */

static void story_back_to_files(const Camp *c, int screen, int page, u32 camp, u32 state)
{
    static int left = -1; /* the Files page's mode while it is on or the game goes from it to the menu */
    static int waited;
    if (!c->story || !khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return;
    }
    if (screen == FILES_SCREEN && in_ram(camp)) {
        const int mode = (int)rd32(camp + FILES_MODE);
        left = mode == 0 || mode == 1 ? mode : -1;
        waited = 0;
    } else if (screen == 0 && page == 0 && left >= 0 && in_ram(state)) {
        /* (the page's state is where the menu's goes: words of it until the menu has made its own) */
        const int entry = (int)rd32(state + ENTRY);
        if (story_group(entry) > 0 || ++waited > 240) {
            left = -1; /* the menu's two open already, or not come to be (some 4 seconds) */
        } else if (story_group(entry) == 0 && rd32(state + STORY_QUESTION) == 0 && rd32(state + ALTERNATE_GROUP) == 0) {
            const int wanted = STORY_SUB_ENTRIES[1][left];
            Ov025_Hub_SelectMenuGroup(state, 2); /* the cursor on Roxas's Diary */
            if (wanted != STORY_SUB_ENTRIES[1][0] && !entry_locked(camp, wanted)) {
                story_hover(state, wanted);
            }
            remembered_sub[1] = wanted;
            remembered_entry[1] = STORY_FILES;
            if (tracing()) {
                khdays_log("camp: ov025 back from Files: its two open, the cursor on %d", wanted);
            }
            left = -1;
        }
    } else if (screen != -1 && screen != FILES_SCREEN) {
        left = -1;
    }
}

/* The camp's save screen's index (savelist.c finds it in its screens' table), once per camp loaded. */
#define SAVE_OPTION 5 /* Save: the menu's sixth option, the story's and Mission Mode's */
static int menu_description[2] = {-1, -1};

static int save_screen_of(const Camp *c)
{
    static int found[2] = {-1, -1}, tries[2];
    const int k = c->story != 0;
    if (found[k] < 0 && tries[k] < 600) {
        ++tries[k];
        found[k] = khdays_savelist_camp_save_screen(c->overlay);
    }
    return found[k];
}

static int read_menu(const Camp *c, KhdaysCampView *v)
{
    const int screen = (int)rd32(c->screen_index), page = (int)rd32(c->screen_index + 4u);
    const u32 camp = rd32(c->camp + 4u);
    u32 state = 0, game = rd32(GAME_STATE);
    int entry = -1;
    memset(v, 0, sizeof(*v));
    v->notice = -1;
    if (in_ram(camp)) {
        state = rd32(camp + SCREEN_STATE);
    }
    story_back_to_files(c, screen, page, camp, state);
    story_cursor(c, page == 0 ? screen : -1, state);
    if (screen == 0 && in_ram(state)) {
        entry = (int)rd32(state + ENTRY);
    }
    if (tracing() && (screen != last_screen || page != last_page || entry != last_entry)) {
        khdays_log("camp: ov%03d screen %d, page %d, entry %d, %s %d", c->overlay, screen, page, entry,
                   c->story ? "question" : "description",
                   screen == 0 && in_ram(state)
                       ? (int)(c->story ? rd32(state + STORY_QUESTION) : rd16(state + DESCRIPTION))
                       : -1);
    }
    last_screen = screen;
    last_page = page;
    last_entry = entry;
    if (tracing() && !c->story && screen == 0 && in_ram(state) && (int)rd16(state + NOTICE) != last_notice) {
        last_notice = (int)rd16(state + NOTICE);
        khdays_log("camp: ov%03d notice %d", c->overlay, last_notice);
    }
    if (tracing() && screen == CONFIG_SCREEN && in_ram(state) &&
        ((s16)rd16(state + CONFIG_TAB) != last_tab || (s16)rd16(state + CONFIG_ENTRY) != last_config_entry)) {
        last_tab = (s16)rd16(state + CONFIG_TAB);
        last_config_entry = (s16)rd16(state + CONFIG_ENTRY);
        khdays_log("camp: ov%03d preferences, list %u, tab %d, entry %d, its value %d", c->overlay,
                   rd16(state + CONFIG_LIST), last_tab, last_config_entry,
                   last_config_entry >= 0 && last_config_entry < KHDAYS_CAMP_SETTINGS
                       ? (s16)rd16(state + CONFIG_VALUES + 2u * (u32)last_config_entry)
                       : -1);
    }
    config.on = 0;
    if (screen == CONFIG_SCREEN && page == 0 && in_ram(state) && in_ram(game) && read_config(c, state, v)) {
        v->config = 1;
        v->selected = PREFERENCES;
        config.on = 1;
        config.seen_at = khdays_events_vblank_count();
        config.state = state;
        config.camp = camp;
        config.page = v->page;
        if (steer.on) {
            show_steer_target(&v->page);
        }
    } else if (c->story && screen == FILES_SCREEN && in_ram(state) && in_ram(game) && in_ram(camp) && overhaul()) {
        if (!read_files(camp, state, v)) {
            return 0;
        }
    } else if (screen == PANELS_SCREEN && in_ram(state) && in_ram(game) && in_ram(camp) && overhaul()) {
        /* the Panels page, the story's and Mission Mode's (the same grid menu, ov025's and ov008's) */
        if (!read_panels(c, camp, state, page, v)) {
            return 0;
        }
    } else if (screen > 0 && overhaul() && screen == save_screen_of(c) && in_ram(game)) {
        /* the save screen: the port's list (savelist.c) in the camp's look; the menu's Save chosen */
        v->save = 1;
        v->selected = SAVE_OPTION;
        v->description = menu_description[c->story != 0];
    } else if (screen != 0 || page != 0 || !in_ram(state) || !in_ram(game) || !selection(c, state, v)) {
        return 0;
    } else if (c->story && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        v->files_new = files_news(game); /* (the Menu Overhaul's NEW badges) */
    }
    if (!v->save && !v->config && !v->files && !v->panels && v->selected == SAVE_OPTION && !v->question && !v->sub) {
        menu_description[c->story != 0] = v->description; /* (the save screen shows it) */
    }
    v->save_screen = save_screen_of(c);
    v->shown = 1;
    read_locks(c, camp, v);
    v->story = c->story;
    v->language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    v->master = brightness();
    v->munny = rd32(game + MUNNY);
    v->hearts = rd32(game + HEARTS);
    v->character = -1;
    if (c->story) {
        /* the story's: Roxas, unless the game gives its slot's character (Ov025_GetLocalPlayerCharacter) */
        const int ch = Ov025_GetLocalPlayerCharacter(0);
        v->day = GameState_GetField(0, 9);
        v->difficulty = GameState_GetField(0x40a, 2);
        v->character = ch > 0 && ch < KHDAYS_CAMP_CHARACTERS ? ch : ROXAS;
    } else {
        const u32 session = rd32(SESSION);
        v->self = Ov008_GetLocalPlayerIndex() & 0xff;
        if (in_ram(session) && v->self < 4) {
            const int ch = (int)(s8)rd8(session + ROWS + (u32)v->self * ROW_SIZE + ROW_CHARACTER);
            v->character = (ch >= 0 && ch < KHDAYS_CAMP_CHARACTERS) || khdays_characters_known(ch) ? ch : -1;
        }
        read_members(v);
    }
    {
        /* the panels' grid, the story's and Mission Mode's the same (the game state's +0xee0; the Panels page
         * keeps its own while it is open and writes it back as it closes) */
        v->panel_cells = (int)rd8(game + ITEM_COUNTS + 1u) + (int)PANEL_SLOTS_FREE;
        if (v->panel_cells > PANEL_SLOTS_MOST) {
            v->panel_cells = PANEL_SLOTS_MOST;
        }
        for (int p = 0; p < KHDAYS_CAMP_PANEL_PAGES; ++p) {
            for (int i = 0; i < KHDAYS_CAMP_PANEL_CELLS; ++i) {
                v->panel[p][i] = rd16(game + PANEL_GRID + 2u * (u32)(p * KHDAYS_CAMP_PANEL_CELLS + i));
            }
        }
    }
    memcpy(v->field, fields, sizeof(fields));
    return 1;
}

void khdays_camp_update_ran(uintptr_t update)
{
    KhdaysCampView v;
    const Camp *c = NULL;
    int loaded = 0;
    lists_clear(&building);
    for (int i = 0; i < CAMPS; ++i) {
        if (khdays_overlay_loaded(camps[i].overlay)) {
            loaded = 1;
            if (update == (uintptr_t)camps[i].idle) {
                c = &camps[i];
            }
        }
    }
    if (!loaded && (seen || fields[0x13].has || fields[7].has)) {
        memset(fields, 0, sizeof(fields)); /* a camp of another time draws its own */
    }
    if (c == NULL) {
        if (seen && khdays_events_vblank_count() - seen_at > SEEN_FRAMES) {
            seen = 0;
            AcquireSRWLockExclusive(&lock);
            memset(&shown, 0, sizeof(shown));
            lists_clear(&published);
            ReleaseSRWLockExclusive(&lock);
        }
        return;
    }
    {
        /* the menu closing: B at its root sets the camp's next screen to -1 (Ov025_MenuBack's
         * SetTargetSlot(-1, -1)); its last view stays published, marked, while the game takes it down
         * (a -1 counts once another was seen: the camp's start leaves its own there) */
        static int target_seen;
        static int last_target = KHDAYS_CAMP_NONE; /* for the trace */
        static KhdaysCampView last;
        const u32 context = rd32(c->camp + 4u);
        const int target = in_ram(context) ? (int)rd32(context + NEXT_SCREEN) : KHDAYS_CAMP_NONE;
        const int was_seen = seen;
        if (!was_seen) {
            target_seen = 0;
            last.shown = 0;
        }
        if (target >= 0) {
            target_seen = 1;
        }
        if (target != last_target && tracing()) {
            khdays_log("camp: ov%03d next screen %d", c->overlay, target);
        }
        last_target = target;
        files_on = 0;
        if (read_menu(c, &v)) {
            v.leaving = target_seen && target == -1;
            v.next = target;
            last = v;
            lists_copy(&kept, &building);
        } else if (target_seen && target == -1 && last.shown && overhaul()) {
            v = last;
            lists_copy(&building, &kept);
            v.leaving = 1;
            v.next = target;
            v.master = brightness();
        }
        if (v.leaving != last_leaving && tracing()) {
            khdays_log("camp: ov%03d %s", c->overlay, v.leaving ? "closing" : "not closing");
        }
        last_leaving = v.leaving;
    }
    if (c->story) {
        /* the Menu Overhaul's Enemy Profiles viewer: only while the port draws the Files page */
        const int viewer = files_on && InterlockedCompareExchange(&files_drawn, 0, 0) != 0 &&
                           khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL);
        khdays_enemy_viewer_update(viewer, viewer && files_mode == 1, files_cursor, files_rows, files_ids);
    }
    seen = 1;
    seen_at = khdays_events_vblank_count();
    AcquireSRWLockExclusive(&lock);
    shown = v;
    lists_copy(&published, &building);
    ReleaseSRWLockExclusive(&lock);
}

int khdays_camp_game_brightness(void)
{
    return brightness();
}

void khdays_camp_view(KhdaysCampView *out)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    ReleaseSRWLockShared(&lock);
    out->file.row = NULL;
    out->file.rows = 0;
    out->panel_text = NULL;
}

void khdays_camp_lists_point(KhdaysCampView *v, const KhdaysCampLists *lists)
{
    v->file.row = lists->row_count > 0 ? lists->rows : NULL;
    v->file.rows = lists->row_count;
    v->panel_text = lists->text_count > 0 ? lists->text : NULL;
}

void khdays_camp_view_lists(KhdaysCampView *out, KhdaysCampLists *lists)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    if (shown.shown) {
        size_t row_room = (size_t)lists->row_room;
        lists->row_count = 0;
        lists->text_count = 0;
        if (reserve((void **)&lists->rows, &row_room, (size_t)published.row_count, sizeof(*lists->rows)) &&
            reserve((void **)&lists->titles, &lists->title_room, published.title_count + 1, sizeof(*lists->titles)) &&
            reserve((void **)&lists->text, &lists->text_room, published.text_count + 1, sizeof(*lists->text))) {
            lists->row_room = (int)row_room;
            if (published.title_count > 0) {
                memcpy(lists->titles, published.titles, published.title_count * sizeof(*lists->titles));
            }
            lists->titles[published.title_count] = 0;
            for (int i = 0; i < published.row_count; ++i) {
                lists->rows[i] = published.rows[i];
                /* (a title the update had no room for: the empty one at the end) */
                lists->rows[i].title =
                    lists->titles +
                    (published.title_at[i] < published.title_count ? published.title_at[i] : published.title_count);
            }
            lists->row_count = published.row_count;
            if (published.text_count > 0) {
                memcpy(lists->text, published.text, published.text_count * sizeof(*lists->text));
            }
            lists->text[published.text_count] = 0;
            lists->text_count = published.text_count;
        }
    }
    ReleaseSRWLockShared(&lock);
    khdays_camp_lists_point(out, lists);
}

void khdays_camp_lists_free(KhdaysCampLists *lists)
{
    free(lists->rows);
    free(lists->titles);
    free(lists->text);
    memset(lists, 0, sizeof(*lists));
}

int khdays_camp_screen(int *story)
{
    for (int i = 0; i < CAMPS; ++i) {
        if (khdays_overlay_loaded(camps[i].overlay)) {
            *story = camps[i].story;
            return (int)rd32(camps[i].screen_index);
        }
    }
    *story = 0;
    return KHDAYS_CAMP_NONE;
}

void khdays_camp_font_scope(uint32_t font, char *scope, int size)
{
    if (size <= 0)
        return;
    scope[0] = 0;
    const struct {
        int overlay;
        u32 global;
    } shops[] = {{8, 0x02090facu}, {26, 0x02091368u}};
    for (int i = 0; i < 2; ++i) {
        if (!khdays_overlay_loaded(shops[i].overlay))
            continue;
        const u32 context = rd32(shops[i].global);
        if (in_ram(context) && font >= context + 0xc13cu && font <= context + 0xc154u &&
            (font - context - 0xc13cu) % 12u == 0) {
            snprintf(scope, (size_t)size, "shop");
            return;
        }
    }
    for (int i = 0; i < CAMPS; ++i) {
        if (!khdays_overlay_loaded(camps[i].overlay))
            continue;
        const u32 context = rd32(camps[i].camp + 4);
        if (!in_ram(context) || font < context + 0x9680u || font > context + 0x96a4u ||
            (font - context - 0x9680u) % 12u != 0)
            continue;
        const int screen = (int)rd32(camps[i].screen_index);
        if (screen == 7)
            snprintf(scope, (size_t)size, "camp.%s.screen.7.mode.%u", camps[i].story ? "story" : "mission",
                     rd32(context + 0x9768u));
        else
            snprintf(scope, (size_t)size, "camp.%s.screen.%d", camps[i].story ? "story" : "mission", screen);
        return;
    }
    /* Standalone camp save uses the same four slots in ov009's context. */
    if (khdays_overlay_loaded(9)) {
        const u32 context = rd32(0x020563e8u);
        if (in_ram(context) && font >= context + 0x9680u && font <= context + 0x96a4u &&
            (font - context - 0x9680u) % 12u == 0)
            snprintf(scope, (size_t)size, "camp.save");
    }
}

/* ---- The Preferences page's buttons ----------------------------------------------------------- */

void khdays_camp_set_page_drawn(int drawn)
{
    InterlockedExchange(&page_drawn, drawn ? 1 : 0);
}

void khdays_camp_set_files_drawn(int drawn)
{
    InterlockedExchange(&files_drawn, drawn ? 1 : 0);
}

/* ---- The story's menu's panels: Y turns their pages ------------------------------------------- */

static volatile LONG panels_drawn, panel_turns;

void khdays_camp_set_panels_drawn(int pages)
{
    InterlockedExchange(&panels_drawn, pages);
}

unsigned khdays_camp_panel_turns(void)
{
    return (unsigned)InterlockedCompareExchange(&panel_turns, 0, 0);
}

/* Y on the story's menu with its panels' grid of more than a page drawn: their page turned, the Panels page's
 * sound (Ov025_ShowGridPage's PlaySound(0, 2)); Y kept from the game there. */
static u16 panel_buttons(u16 buttons, u16 pressed)
{
    if (InterlockedCompareExchange(&panels_drawn, 0, 0) < 2 || !seen ||
        khdays_events_vblank_count() - seen_at > SEEN_FRAMES) {
        return buttons;
    }
    if (pressed & KHDAYS_KEY_Y) {
        InterlockedIncrement(&panel_turns);
        PlaySound(0, 2);
    }
    return (u16)(buttons & ~KHDAYS_KEY_Y);
}

/* The direction this frame: a press, then the camp's key repeat (savelist.c's). */
static int page_direction(u16 buttons)
{
    const int dir = (buttons & KHDAYS_KEY_UP) ? -1 : (buttons & KHDAYS_KEY_DOWN) ? 1 : 0;
    const u32 now = khdays_events_vblank_count();
    const int mode = GetFrameRateMode();
    const u32 speed = mode == 0 ? 2u : mode == 1 ? 3u : 1u;
    const u32 repeat = config.camp + CAMP_REPEAT;
    const u32 delay = in_ram(config.camp) ? (u32)rd16(repeat + REPEAT_DELAY) * speed : 0;
    const u32 interval = in_ram(config.camp) ? (u32)rd16(repeat + REPEAT_INTERVAL) * speed : 0;
    u32 elapsed;
    if (dir == 0) {
        steer.dir = 0;
        return 0;
    }
    if (dir != steer.dir) {
        steer.dir = dir;
        steer.repeat_from = now;
        steer.repeat_count = 0;
        return dir;
    }
    elapsed = now - steer.repeat_from;
    if (interval == 0 || elapsed < delay) {
        return 0;
    }
    elapsed -= delay;
    if (elapsed >= interval * (u32)steer.repeat_count) {
        steer.repeat_count = (int)(elapsed / interval + 1u);
        return dir;
    }
    return 0;
}

/* Where an entry is among its tab's stops, -1 none. */
static int stop_index(int tab, int entry)
{
    for (int i = 0; i < config.stop_count[tab]; ++i) {
        if (config.stops[tab][i] == entry) {
            return i;
        }
    }
    return -1;
}

static void steer_to(int setting)
{
    steer.on = 1;
    steer.tab = config.tab_of[setting];
    steer.entry = config.entry_of[setting];
    steer.rest = 0;
    /* the farthest way: round the tabs, then round a tab's stops */
    steer.presses = config.tabs + KHDAYS_CAMP_SETTINGS + 2;
}

/* A press of the game's on the way (a frame between two), 0 there or resting. */
static u16 steer_step(void)
{
    const int tab = (s16)rd16(config.state + CONFIG_TAB), entry = (s16)rd16(config.state + CONFIG_ENTRY);
    if ((tab == steer.tab && entry == steer.entry) || steer.presses <= 0 || tab < 0 || tab >= config.tabs) {
        steer.on = 0;
        return 0;
    }
    if (steer.rest) {
        steer.rest = 0;
        return 0;
    }
    steer.rest = 1;
    --steer.presses;
    if (tab != steer.tab) {
        const int d = (steer.tab - tab + config.tabs) % config.tabs;
        return d <= config.tabs / 2 ? KHDAYS_KEY_R : KHDAYS_KEY_L;
    }
    {
        const int n = config.stop_count[tab], at = stop_index(tab, entry), to = stop_index(tab, steer.entry);
        if (n == 0 || at < 0 || to < 0) {
            steer.on = 0;
            return 0;
        }
        return (to - at + n) % n <= n / 2 ? KHDAYS_KEY_DOWN : KHDAYS_KEY_UP;
    }
}

uint16_t khdays_camp_buttons(uint16_t buttons)
{
    buttons = khdays_enemy_viewer_buttons(buttons); /* the Enemy Profiles' model viewer: X, and all while open */
    const u16 pressed = (u16)(buttons & ~steer.held);
    const KhdaysCampPage *pg = &config.page;
    u16 out = buttons;
    steer.held = buttons;
    buttons = panel_buttons(buttons, pressed);
    if (!config.on || khdays_events_vblank_count() - config.seen_at > SEEN_FRAMES ||
        InterlockedCompareExchange(&page_drawn, 0, 0) == 0 || pg->chosen < 0) {
        steer.on = 0;
        steer.dir = 0;
        return buttons;
    }
    out = (u16)(buttons & ~(KHDAYS_KEY_UP | KHDAYS_KEY_DOWN | KHDAYS_KEY_L | KHDAYS_KEY_R));
    if (steer.on) {
        /* on the way the player's buttons wait */
        return steer_step();
    }
    {
        const int s = pg->section, first = pg->first[s], count = pg->count[s];
        const int dir = page_direction(buttons);
        int to = -1;
        if (dir != 0 && count > 0) {
            to = first + (pg->chosen - first + dir + count) % count; /* round the section */
        } else if (pressed & (KHDAYS_KEY_L | KHDAYS_KEY_R)) {
            const int next = (s + ((pressed & KHDAYS_KEY_R) ? 1 : pg->sections - 1)) % pg->sections;
            to = pg->count[next] > 0 ? pg->first[next] : -1;
        }
        if (to >= 0 && to != pg->chosen) {
            if (khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
                PlaySound(0, 0); /* the move's one sound (khdays_camp_navigation_sound) */
            }
            steer_to(to);
            return steer_step();
        }
    }
    return out;
}
