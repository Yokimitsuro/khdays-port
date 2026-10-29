/* The port's save list (savelist.h), over two of the game's lists: the title's list of files (ov000,
 * its context *data_ov000_0205ac24, its update one of nine states: data_ov000_0205a710) and the
 * camps' save screens (ov025's and ov008's, the same code: data_ov025_020b4e2c's hooks, their state
 * at the camp's context's +0x959c). Each field read here, and the states' meanings:
 * docs/RUNTIME_MAP.md section 15.4. */
#include "savelist.h"
#include "autosave.h"

#include "../input/input.h"
#include "../core/io.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../mods/installed.h"
#include "../missions/missions.h"

#include "khdays_ds_symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#undef PlaySound /* windows.h's (winmm's), not the game's */

extern int GetLanguage(void);
extern int PlaySound(int bank, int sound);
extern int GetFrameRateMode(void);
int khdays_overlay_loaded(int id); /* overlays.c */
int khdays_language_wanted(void);  /* language.c */
u32 khdays_events_vblank_count(void);

extern void NNS_FndInitList(KhdaysRef list, unsigned short offset);
extern KhdaysRef NNS_FndAllocFromDefaultExpHeapEx(unsigned int size, int align);
extern int NNSi_FndFreeFromDefaultHeap(KhdaysRef block);
extern int MsgDb_IsLoaded(int id);
extern int MsgDb_LoadDb(int id, int mode);
extern int ResSlot_Release_2(int id);

/* A save's level, as each overlay's own code counts it for its records (Ov000_RecordLoadResult,
 * Ov025_BuildSaveSlotEntry): the panels' grid built and counted, its count plus one. */
typedef struct Counting {
    void (*init)(KhdaysRef record, KhdaysRef zero);
    void (*grid)(KhdaysRef record, KhdaysRef work, KhdaysRef list, KhdaysRef ids);
    void (*count)(KhdaysRef record, KhdaysRef work, KhdaysRef list, int unused);
    void (*release)(KhdaysRef record, KhdaysRef work, KhdaysRef list);
    KhdaysRef (*finish)(KhdaysRef record);
    int (*type)(int which);
} Counting;

extern void Ov000_InitRecordContext(KhdaysRef record, KhdaysRef zero);
extern void Ov000_BuildMenuGrid(KhdaysRef record, KhdaysRef work, KhdaysRef list, KhdaysRef ids);
extern void Ov000_RebuildViewAndCountCells(KhdaysRef record, KhdaysRef work, KhdaysRef list, int unused);
extern void Ov000_ReleaseHandleGridAndList(KhdaysRef record, KhdaysRef work, KhdaysRef list);
extern KhdaysRef func_ov000_02058360(KhdaysRef record);
extern int Ov000_LookupTypeCode(int which);
extern void Ov025_InitRecordContext(KhdaysRef record, KhdaysRef zero);
extern void Ov025_BuildMenuGrid(KhdaysRef record, KhdaysRef work, KhdaysRef list, KhdaysRef ids);
extern void Ov025_RebuildViewAndCountCells(KhdaysRef record, KhdaysRef work, KhdaysRef list, int unused);
extern void Ov025_ReleaseHandleGridAndList(KhdaysRef record, KhdaysRef work, KhdaysRef list);
extern KhdaysRef func_ov025_02087254(KhdaysRef record);
extern int Ov025_LookupTypeCode(int which);

static const Counting title_counting = {
    Ov000_InitRecordContext,        Ov000_BuildMenuGrid, Ov000_RebuildViewAndCountCells,
    Ov000_ReleaseHandleGridAndList, func_ov000_02058360, Ov000_LookupTypeCode};
static const Counting story_counting = {
    Ov025_InitRecordContext,        Ov025_BuildMenuGrid, Ov025_RebuildViewAndCountCells,
    Ov025_ReleaseHandleGridAndList, func_ov025_02087254, Ov025_LookupTypeCode};

extern void Ov008_InitRecordContext(KhdaysRef record, KhdaysRef zero);
extern void Ov008_BuildMenuGrid(KhdaysRef record, KhdaysRef work, KhdaysRef list, KhdaysRef ids);
extern void Ov008_RebuildViewAndCountCells(KhdaysRef record, KhdaysRef work, KhdaysRef list, int unused);
extern void Ov008_ReleaseHandleGridAndList(KhdaysRef record, KhdaysRef work, KhdaysRef list);
extern KhdaysRef func_ov008_02053464(KhdaysRef record);
extern int Ov008_LookupTypeCode(int which);
static const Counting mission_counting = {
    Ov008_InitRecordContext,        Ov008_BuildMenuGrid, Ov008_RebuildViewAndCountCells,
    Ov008_ReleaseHandleGridAndList, func_ov008_02053464, Ov008_LookupTypeCode};

/* The game's own redraws of its lists' rows after a record changes, and its cursors' moves (see "The
 * game's own lists in pages") */
extern void Ov000_RefreshSelectionGroupDraw(void);
extern void Ov000_UpdateNumberDisplays(void);
extern void Ov000_PlaceCursorByMode(int enabled, int mode);
extern void Ov025_SaveMenu_RefreshRows(KhdaysRef menu);
extern void Ov025_RefreshSaveRowDigits(void);
extern void Ov025_PlaceElementByVariant(KhdaysRef menu, int old, int now);
extern void Ov008_SaveMenu_RefreshRows(KhdaysRef menu);
extern void Ov008_RefreshSaveRowDigits(KhdaysRef menu);
extern void Ov008_PlaceElementByVariant(KhdaysRef menu, int old, int now);
extern int GameState_GetField(int field, int bits);
/* the save screen's prompt and its points' entries (SaveMenuConfirm's "no", SaveMenu_OnBack's phase 1) */
extern void Ov025_SetMenuEntriesVisible(int shown, int prompt);
extern void Ov025_SaveMenuDrawPrompt(KhdaysRef menu, int phase);
extern void Ov008_SetMenuEntriesVisible(int shown, int prompt);
extern void Ov008_SaveMenuDrawPrompt(KhdaysRef menu, int phase);

static void story_row_digits(KhdaysRef menu)
{
    (void)menu; /* ov025's takes none (Ov025_RefreshSaveRowDigits) */
    Ov025_RefreshSaveRowDigits();
}

/* The camps whose save screens the port's list stands for: ov025's (the story's) and ov008's (Mission
 * Mode's), the same code in each. Their addresses: the word naming the running screen, the screens'
 * handler table, the save screen's hooks in it, and the word holding the camp's context. */
typedef struct Camp {
    int overlay;
    u32 running, table, save_screen, context;
    const Counting *counting;
    /* its save screen's redraw of its rows and their digits after a save (SaveMenuTick), and its
     * cursor's placing (StepSelectionForward) */
    void (*refresh_rows)(KhdaysRef menu);
    void (*refresh_digits)(KhdaysRef menu);
    void (*place)(KhdaysRef menu, int old, int now);
    /* its prompt's and entries' redraw, to go back to the slots after a save (back_to_slots) */
    void (*entries_visible)(int shown, int prompt);
    void (*draw_prompt)(KhdaysRef menu, int phase);
} Camp;
static const Camp camps[] = {
    {25, 0x020b574cu, 0x020b4ab0u, 0x020b4e2cu, 0x020b5748u, &story_counting, Ov025_SaveMenu_RefreshRows,
     story_row_digits, Ov025_PlaceElementByVariant, Ov025_SetMenuEntriesVisible, Ov025_SaveMenuDrawPrompt},
    {8, 0x02090f0cu, 0x0209009cu, 0x02090460u, 0x02090f08u, &mission_counting, Ov008_SaveMenu_RefreshRows,
     Ov008_RefreshSaveRowDigits, Ov008_PlaceElementByVariant, Ov008_SetMenuEntriesVisible, Ov008_SaveMenuDrawPrompt},
};
#define CAMPS ((int)(sizeof(camps) / sizeof(camps[0])))

#define OV_TITLE 0
#define TITLE_SCENE 1
#define SCENE_OBJECT 0x0u
#define SCENE_CURRENT 0x8u
#define OBJECT_HEAP 0x20u
#define GAME_STATE 0x0204be18u /* gGameState: the game's state, a reference */
#define STATE_MENU_IDS 0xee0u  /* the panels' grid in the state (Ov000_RecordLoadResult's menuIds) */

/* the title's list's context (see above) */
#define GUEST_SHOWN 0x4accu
#define STATE 0x4ad0u
#define SECOND 0x4ad4u
#define CHECK_STEP 0x4ae0u
#define PAGE 0x4b08u
#define RECORDS 0x4b10u
#define RECORD_SIZE 0x20u
#define RECORD_HOLDS 0x10u
#define YES 0x4b70u
#define CARD_ERROR 0x6a48u
#define GUEST_PAGE 3
/* the title's key repeat (Ov000_TickBootTeardown's KeyRepeat_Step: the title's context's +0x4be8) */
#define TITLE_REPEAT 0x4be8u

/* the camps' save screens (see above and `camps`) */
#define CAMP_PAGE 0x959cu
#define CAMP_REPEAT 0x963eu
#define SAVE_SLOT 0x0u
#define SAVE_PHASE 0x4u
#define SAVE_OPENING_SLOT 0x8u
#define SAVE_OPENING 0xcu
#define SAVE_ENTRIES 0x10u
#define SAVE_ENTRY_SIZE 0x1cu
#define SAVE_ENTRY_HOLDS 0x10u
#define SAVE_COUNTER 0x1ef8u  /* the highest save counter of the slots read */
#define FIELD_SAVE_SLOT 0xc98 /* the slot saved to last: the camp's cursor starts there */
#define SAVE_YES 0x64u
#define SAVE_SLOT_X 0x140u /* the three slots' x (8 bytes each): the others parked away while it asks */
#define SAVE_ERROR 0x23cu
#define SAVE_ERROR_KIND 0x240u
#define SAVE_READING 0x244u /* the slot read's step (a byte) */
enum { SAVE_PICK, SAVE_ASK, SAVE_SAVING, SAVE_READING_AGAIN, SAVE_DONE, SAVE_FAILED };

/* KeyRepeat_Update's: its delay at +2, its interval at +4, in VBlanks at frame rate mode 2 */
#define REPEAT_DELAY 2u
#define REPEAT_INTERVAL 4u

/* the strings' records the screens draw: UI/load/lrd_&.s.z (Ov000_DrawLoadPageText) and
 * UI/cm/str/sav_&.s.z (Ov025_SaveMenuDrawPrompt) */
enum {
    LRD_LOAD = 3,
    LRD_DELETE = 4,
    LRD_DELETE_WARNING = 5,
    LRD_DELETING = 9,
    LRD_DELETED = 10,
    LRD_BROKEN_DELETE = 14,
    LRD_NETWORK = 16,
    LRD_GUEST = 17,
    LRD_GUEST_NOTE = 18,
    LRD_WRITE_ERROR = 19,
    LRD_READ_ERROR = 20,
    LRD_ERROR_NOTE = 21,
    LRD_SURE = 22
};
enum { SAV_OVERWRITE = 2, SAV_CREATE = 3, SAV_SAVED = 7, SAV_WRITE_ERROR = 10, SAV_SAVING = 11, SAV_READ_ERROR = 13 };

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysSaveView shown; /* under the lock */

static int active; /* the screen shown last frame (KHDAYS_SAVELIST_*) */
static int cursor;
static int current_file = KHDAYS_PLAYED_NONE; /* the file this game was loaded from or saved to last */
static int played_level; /* the level of the file the title loaded, as its list counted it (0 not known) */
static u16 held, handed;      /* the buttons held last frame; the ones given to the game */
static u32 steer_field;       /* the game's cursor the port takes to a place before a button, 0 none */
static int steer_to;
static u16 steer_button;
static int steer_rest; /* a frame with no direction between two presses */
static int repeat_dir; /* the direction held: -1 up, 1 down, 0 none */
static u32 repeat_from, repeat_count;
static KhdaysSaveRow rows[KHDAYS_SLOT_COUNT];
static u32 rows_changed, rows_generation;

static u32 rd32(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

static void wr32(u32 address, u32 value)
{
    *(volatile u32 *)KHDAYS_W(address) = value;
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

int khdays_savelist_available(int lang)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    char relative[64], path[1024];
    if (lang < 1 || lang > 5) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/save/%s/save.txt", codes[lang]);
    return khdays_mods_find(relative, path, (int)sizeof(path));
}

static int in_title(void)
{
    return khdays_overlay_loaded(OV_TITLE) && rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) == TITLE_SCENE;
}

/* The title's list's context, or 0 when it does not run. */
static u32 title_list(void)
{
    u32 ctx;
    if (!in_title()) {
        return 0;
    }
    ctx = rd32(KHDAYS_DS_data_ov000_0205ac24);
    return in_ram(ctx) && rd32(ctx + STATE) <= 8u ? ctx : 0;
}

/* The title's own context (its key repeat), or 0. */
static u32 title_context(void)
{
    const u32 object = rd32(KHDAYS_DS_gSceneCtl + SCENE_OBJECT);
    const u32 context = in_ram(object) ? rd32(object + OBJECT_HEAP) : 0;
    return in_ram(context) ? context : 0;
}

/* The camp whose overlay is there and its context, or NULL. */
static const Camp *camp_of(u32 *ctx)
{
    for (int c = 0; c < CAMPS; ++c) {
        if (khdays_overlay_loaded(camps[c].overlay)) {
            *ctx = rd32(camps[c].context);
            if (in_ram(*ctx)) {
                return &camps[c];
            }
        }
    }
    *ctx = 0;
    return NULL;
}

/* The camp's save screen's state, or 0 when it does not run. */
static u32 camp_save(const Camp *camp, u32 ctx)
{
    const int index = camp != NULL ? (int)rd32(camp->running) : -1;
    u32 menu;
    if (index < 0 || index > 64 || rd32(camp->table + 8u * (u32)index) != camp->save_screen) {
        return 0;
    }
    menu = rd32(ctx + CAMP_PAGE);
    return in_ram(menu) && rd32(menu + SAVE_PHASE) <= SAVE_FAILED ? menu : 0;
}

/* The level of the save in `state`, counted by the game's own code (the overlay's) with its state
 * under gGameState for that while. `work` is a block of the game's heap: the state's copy, then
 * the list, the grid's work and the record's context. */
static u16 level_of(const Counting *c, const u8 *state, KhdaysRef work)
{
    const KhdaysRef copy = work, list = work + 0x1cb0u, grid = list + 0x10u, record = grid + 0x1e0u;
    const u32 saved = rd32(GAME_STATE);
    u16 level;
    memcpy(KHDAYS_W(copy), state, KHDAYS_STATE_BYTES);
    wr32(GAME_STATE, copy);
    NNS_FndInitList(list, 0x28);
    c->init(record, 0);
    c->grid(record, grid, list, copy + STATE_MENU_IDS);
    c->count(record, grid, list, 0);
    level = (u16)(rd32(record + 0x78u) + 1u);
    c->release(record, grid, list);
    c->finish(record);
    wr32(GAME_STATE, saved);
    return level;
}

/* Every file's row made again. */
static void make_rows(const Counting *c)
{
    const int type = c->type(0);
    const int dbs[] = {type, 0x15, 0x1b, 0x14, 0x13}; /* the grid's message databases, loaded once */
    int loaded[5] = {0};
    KhdaysRef work = 0;
    rows_generation = khdays_slots_generation();
    for (int f = 0; f < KHDAYS_SLOT_COUNT; ++f) {
        const u8 *state;
        KhdaysSaveRow *r = &rows[f];
        memset(r, 0, sizeof(*r));
        r->state = (uint8_t)khdays_slots_state(f, &state);
        if (r->state != KHDAYS_SLOT_SAVED) {
            continue;
        }
        if (work == 0) {
            for (int d = 0; d < 5; ++d) {
                if (!MsgDb_IsLoaded(dbs[d])) {
                    MsgDb_LoadDb(dbs[d], 0xe);
                    loaded[d] = 1;
                }
            }
            work = NNS_FndAllocFromDefaultExpHeapEx(0x1cb0u + 0x10u + 0x1e0u + 0x100u, 4);
        }
        r->seconds = (u32)state[0] | (u32)state[1] << 8 | (u32)state[2] << 16 | (u32)state[3] << 24;
        r->munny = (u32)state[8] | (u32)state[9] << 8 | (u32)state[10] << 16 | (u32)state[11] << 24;
        r->day = (uint16_t)khdays_slots_field(state, 0, 9);
        r->difficulty = (uint8_t)khdays_slots_field(state, 0x40a, 2);
        r->face = (uint8_t)khdays_slots_field(state, 0xc4b, 2);
        r->crown = khdays_slots_field(state, 0x44e, 3) == 6;
        r->counter = khdays_slots_field(state, 0xc77, 16) << 16 | khdays_slots_field(state, 0xc87, 16);
        r->level = work != 0 ? level_of(c, state, work) : 0;
    }
    if (work != 0) {
        NNSi_FndFreeFromDefaultHeap(work);
    }
    for (int d = 4; d >= 0; --d) {
        if (loaded[d]) {
            ResSlot_Release_2(dbs[d]);
        }
    }
    ++rows_changed;
}

/* The direction the port's cursor takes this frame: a press, then the game's repeat (the
 * screen's KeyRepeat: `repeat`, 0 none). */
static int direction(u16 buttons, u32 repeat)
{
    const int dir = (buttons & KHDAYS_KEY_UP) ? -1 : (buttons & KHDAYS_KEY_DOWN) ? 1 : 0;
    const u32 now = khdays_events_vblank_count();
    const int mode = GetFrameRateMode();
    const u32 speed = mode == 0 ? 2u : mode == 1 ? 3u : 1u;
    const u32 delay = repeat != 0 ? (u32) * (volatile u16 *)KHDAYS_W(repeat + REPEAT_DELAY) * speed : 0;
    const u32 interval = repeat != 0 ? (u32) * (volatile u16 *)KHDAYS_W(repeat + REPEAT_INTERVAL) * speed : 0;
    u32 elapsed;
    if (dir == 0) {
        repeat_dir = 0;
        return 0;
    }
    if (dir != repeat_dir) {
        repeat_dir = dir;
        repeat_from = now;
        repeat_count = 0;
        return dir;
    }
    elapsed = now - repeat_from;
    if (interval == 0 || elapsed < delay) {
        return 0;
    }
    elapsed -= delay;
    if (elapsed >= interval * repeat_count) {
        repeat_count = elapsed / interval + 1;
        return dir;
    }
    return 0;
}

/* The game's cursor (the word at steer_field) taken to steer_to, a press at a time with a
 * frame between, then the button given: 0 while on the way. Over `places` places with
 * wrapping, DOWN going up by one, UP down by one; `up_only` when the direction each key
 * takes is not known, UP only (it reaches any place of three within two presses). */
static u16 steer(int places, int up_only)
{
    const int at = (int)rd32(steer_field);
    if (at == steer_to) {
        const u16 b = steer_button;
        steer_field = 0;
        handed = b;
        return b;
    }
    if (steer_rest) {
        steer_rest = 0;
        return 0;
    }
    steer_rest = 1;
    if (up_only) {
        return KHDAYS_KEY_UP;
    }
    return (u16)((steer_to - at + places) % places <= places / 2 ? KHDAYS_KEY_DOWN : KHDAYS_KEY_UP);
}

static void start_steer(u32 field, int to, u16 button)
{
    steer_field = field;
    steer_to = to;
    steer_button = button;
    steer_rest = 0;
}

/* The port's cursor moved over `rows_in` rows, with the game's sound. */
static void move_cursor(u16 buttons, u32 repeat, int rows_in)
{
    const int dir = direction(buttons, repeat);
    if (dir != 0) {
        cursor = (cursor + dir + rows_in) % rows_in;
        PlaySound(0, 0);
    }
}

/* ---- The title's list ------------------------------------------------------------------------ */

static u16 title_buttons(u32 ctx, u16 buttons)
{
    const int state = (int)rd32(ctx + STATE);
    const u16 pressed = (u16)(buttons & ~held);
    const int guest = (*(volatile u8 *)KHDAYS_W(ctx + GUEST_SHOWN)) != 0;
    const int rows_in = KHDAYS_SLOT_COUNT + (guest ? 1 : 0);
    const u32 title = title_context();
    u16 give = 0;
    if (active != KHDAYS_SAVELIST_LOAD) {
        active = KHDAYS_SAVELIST_LOAD;
        cursor = khdays_slots_shown();
        handed = 0;
        steer_field = 0;
        repeat_dir = 0;
        make_rows(&title_counting);
    } else if (state == 2 && khdays_slots_generation() != rows_generation) {
        make_rows(&title_counting); /* a file deleted */
    }
    if (state != 2 || (short)rd32(ctx + CARD_ERROR) != 0) {
        /* the game's questions, its reading and deleting, its fades: its buttons */
        handed = 0;
        steer_field = 0;
        return buttons;
    }
    if (steer_field != 0) {
        return steer(GUEST_PAGE + (guest ? 1 : 0), 0);
    }
    move_cursor(buttons, title != 0 ? title + TITLE_REPEAT : 0, rows_in);
    if (guest && (pressed & KHDAYS_KEY_Y)) {
        /* the port's: Y (the list takes no Y) plays as the guest, wherever the cursor is; the guest's row
         * comes after the 99 files: the cursor there, A on it */
        cursor = KHDAYS_SLOT_COUNT;
        start_steer(ctx + PAGE, GUEST_PAGE, KHDAYS_KEY_A);
        return steer(GUEST_PAGE + 1, 0);
    }
    if (pressed & (KHDAYS_KEY_A | KHDAYS_KEY_X)) {
        const u16 button = (pressed & KHDAYS_KEY_A) ? KHDAYS_KEY_A : KHDAYS_KEY_X;
        if (cursor >= KHDAYS_SLOT_COUNT) {
            /* the guest's row: A takes the game's cursor to it (X there does nothing) */
            if (button == KHDAYS_KEY_A) {
                start_steer(ctx + PAGE, GUEST_PAGE, KHDAYS_KEY_A);
                return steer(GUEST_PAGE + 1, 0);
            }
        } else if (rows[cursor].state == KHDAYS_SLOT_EMPTY) {
            PlaySound(0, 4); /* as the game does on an empty slot */
        } else {
            /* the file under the game's slot 1, its record told what the file holds, its cursor there */
            khdays_slots_show(cursor);
            wr32(ctx + RECORDS + RECORD_HOLDS, rows[cursor].state);
            start_steer(ctx + PAGE, 0, button);
            return steer(GUEST_PAGE + (guest ? 1 : 0), 0);
        }
    }
    if (pressed & KHDAYS_KEY_B) {
        give |= KHDAYS_KEY_B;
    }
    /* a button given stays the game's while it is held */
    handed = (u16)((handed & buttons) | give);
    return (u16)(buttons & handed);
}

static void title_view(u32 ctx, KhdaysSaveView *v)
{
    const int state = (int)rd32(ctx + STATE);
    const int second = rd32(ctx + SECOND) != 0;
    const int guest = (int)rd32(ctx + PAGE) >= GUEST_PAGE;
    const u32 error = rd32(ctx + CARD_ERROR);
    const u32 holds = rd32(ctx + RECORDS + RECORD_HOLDS);
    v->screen = KHDAYS_SAVELIST_LOAD;
    v->words = KHDAYS_WORDS_LOAD;
    v->guest = (*(volatile u8 *)KHDAYS_W(ctx + GUEST_SHOWN)) != 0;
    v->yes = rd32(ctx + YES) != 0;
    if ((error & 0xffffu) != 0) {
        v->ask[0] = (error >> 16) != 0 ? LRD_WRITE_ERROR : LRD_READ_ERROR;
        v->ask[1] = LRD_ERROR_NOTE;
    } else if (state == 1 || state == 2) {
        v->help = 1;
    } else if (state == 3) {
        v->buttons = 2;
        if (second) {
            v->ask[0] = LRD_NETWORK;
        } else if (guest) {
            v->ask[0] = LRD_GUEST;
            v->ask[1] = LRD_GUEST_NOTE;
        } else {
            v->ask[0] = LRD_LOAD;
        }
    } else if (state == 4) {
        v->ask[0] = rd32(ctx + CHECK_STEP) == 2 ? LRD_DELETED : LRD_DELETING;
    } else if (state == 5) {
        v->buttons = 2;
        if (second) {
            v->ask[0] = LRD_SURE;
        } else if (holds == KHDAYS_SLOT_BROKEN) {
            v->ask[0] = LRD_BROKEN_DELETE;
        } else {
            v->ask[0] = LRD_DELETE;
            v->ask[1] = LRD_DELETE_WARNING;
        }
    }
}

/* ---- The camp's save screen ------------------------------------------------------------------- */

/* The file the port's cursor starts on: the one this game came from or was saved to, else the
 * first with no save. */
static int start_file(void)
{
    if (current_file >= 0) {
        return current_file;
    }
    for (int f = 0; f < KHDAYS_SLOT_COUNT; ++f) {
        if (khdays_slots_state(f, NULL) == KHDAYS_SLOT_EMPTY) {
            return f;
        }
    }
    return 0;
}

/* After a save the DS's screen waits on its "saved" (phase 4) for A or B, both of which leave it. With the
 * Menu Overhaul A or a direction there takes it back to its slots, as its "no" to the question does
 * (Ov025_SaveMenuConfirm, SaveMenu_OnBack's phase 1), and B still leaves. Whether `pressed` asks for that. */
static int after_save(u32 menu, int opening, int phase, u16 pressed)
{
    return opening >= 3 && phase == SAVE_DONE && rd32(menu + SAVE_ERROR) == 0 &&
           (pressed & (KHDAYS_KEY_A | KHDAYS_KEY_UP | KHDAYS_KEY_DOWN)) != 0 &&
           khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL);
}

static void back_to_slots(const Camp *camp, u32 menu)
{
    camp->entries_visible(1, 0);
    for (u32 i = 0; i < KHDAYS_GAME_SLOTS; ++i) {
        wr32(menu + SAVE_SLOT_X + 8u * i, 0);
    }
    camp->draw_prompt(menu, SAVE_PICK);
    wr32(menu + SAVE_PHASE, SAVE_PICK);
    khdays_log("save list: back to the slots after the save");
}

static u16 camp_buttons(const Camp *camp, u32 ctx, u32 menu, u16 buttons)
{
    const int phase = (int)rd32(menu + SAVE_PHASE);
    const int opening = (int)rd32(menu + SAVE_OPENING);
    const u16 pressed = (u16)(buttons & ~held);
    if (active != KHDAYS_SAVELIST_SAVE) {
        const int latest = khdays_slots_latest();
        active = KHDAYS_SAVELIST_SAVE;
        cursor = start_file();
        handed = 0;
        steer_field = 0;
        repeat_dir = 0;
        /* the file saved last under the game's slot 1 while the screen opens: its save counter
         * is the one the game's next save goes on from (Ov025_SaveMenuInitStep, BeginSaveToSlot) */
        if (latest >= 0) {
            khdays_slots_show(latest);
        }
        if (opening != 1 || rd32(menu + SAVE_OPENING_SLOT) != 0 || *(volatile u8 *)KHDAYS_W(menu + SAVE_READING) != 0) {
            khdays_log("save list: the camp's save screen was found late (its opening at step %d)", opening);
        }
        make_rows(camp->counting);
    } else if ((phase == SAVE_PICK || phase == SAVE_DONE) && khdays_slots_generation() != rows_generation) {
        make_rows(camp->counting); /* a file saved */
    }
    if (after_save(menu, opening, phase, pressed)) {
        back_to_slots(camp, menu);
        handed = 0;
        steer_field = 0;
        repeat_dir = 0;
        if (pressed & KHDAYS_KEY_A) {
            PlaySound(0, 1);
        } else {
            move_cursor(buttons, ctx + CAMP_REPEAT, KHDAYS_SLOT_COUNT);
        }
        return (u16)(buttons & ~(KHDAYS_KEY_A | KHDAYS_KEY_UP | KHDAYS_KEY_DOWN));
    }
    if (opening < 3 || phase != SAVE_PICK || rd32(menu + SAVE_ERROR) != 0) {
        handed = 0;
        steer_field = 0;
        return buttons;
    }
    if (steer_field != 0) {
        return steer(3, 1);
    }
    move_cursor(buttons, ctx + CAMP_REPEAT, KHDAYS_SLOT_COUNT);
    if (pressed & KHDAYS_KEY_A) {
        /* the file under the game's slot 1, its entry told what the file holds (the question:
         * overwrite or create), the game's cursor there */
        khdays_slots_show(cursor);
        wr32(menu + SAVE_ENTRIES + SAVE_ENTRY_HOLDS, rows[cursor].state);
        start_steer(menu + SAVE_SLOT, 0, KHDAYS_KEY_A);
        return steer(3, 1);
    }
    handed = (u16)((handed & buttons) | (pressed & KHDAYS_KEY_B));
    return (u16)(buttons & handed);
}

static void camp_view(u32 menu, KhdaysSaveView *v)
{
    const int phase = (int)rd32(menu + SAVE_PHASE);
    v->screen = KHDAYS_SAVELIST_SAVE;
    v->words = KHDAYS_WORDS_SAVE;
    v->yes = rd32(menu + SAVE_YES) != 0;
    if (rd32(menu + SAVE_ERROR) != 0 || phase == SAVE_FAILED) {
        v->ask[0] = rd32(menu + SAVE_ERROR_KIND) != 0 ? SAV_WRITE_ERROR : SAV_READ_ERROR;
    } else if (phase == SAVE_PICK) {
        v->help = rd32(menu + SAVE_OPENING) >= 3;
    } else if (phase == SAVE_ASK) {
        v->buttons = 2;
        v->ask[0] = rd32(menu + SAVE_ENTRIES + SAVE_ENTRY_HOLDS) != KHDAYS_SLOT_EMPTY ? SAV_OVERWRITE : SAV_CREATE;
    } else if (phase == SAVE_SAVING || phase == SAVE_READING_AGAIN) {
        v->ask[0] = SAV_SAVING;
    } else if (phase == SAVE_DONE) {
        v->ask[0] = SAV_SAVED;
    }
}

/* ---- The game's own lists in pages -------------------------------------------------------------- */

/* Without the list's pieces and with [save] files = 99, the game's own lists page through the files
 * (game slot g is file page_top + g): the port writes the records of the files a page shows and calls
 * the game's own redraws; the cursor moves inside a page are the game's, and at its edges the port
 * moves the page, with the game's sound, wrapping past the last file. docs/RUNTIME_MAP.md 15.4. */

static volatile LONG all_files = 1;     /* [save] files: the 99 (1) or the DS's three (0) */
static int page_top = -1;               /* the first file of the page shown, -1 none */
static int counted;                     /* the camp's save counter given the highest of all the files */
static u16 taken;                       /* the direction keys the port took at a page's edge (page_step) */
static u16 page_swallowed;              /* A kept from a camp's game while held (camp_page_buttons) */

void khdays_savelist_set_settings(int files, int autosave)
{
    InterlockedExchange(&all_files, files != 0);
    khdays_autosave_set_mode(autosave);
}

int khdays_savelist_file_number(int slot)
{
    const int top = khdays_slots_page();
    return (top >= 0 ? top : 0) + slot + 1;
}

/* The page with `file` on row `row`, where the files allow. */
static int page_for(int file, int row)
{
    const int last = KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS, top = file - row;
    return top < 0 ? 0 : top > last ? last : top;
}

static void show_page(int top)
{
    page_top = top;
    khdays_slots_show_page(top);
}

/* The title's three records from the page's files, and the game's redraw. */
static void title_records(u32 ctx)
{
    for (u32 g = 0; g < KHDAYS_GAME_SLOTS; ++g) {
        const KhdaysSaveRow *r = &rows[page_top + (int)g];
        const u32 at = ctx + RECORDS + g * RECORD_SIZE;
        wr16(at, r->day);
        wr16(at + 2u, r->level);
        wr32(at + 4u, r->munny);
        wr32(at + 8u, r->difficulty);
        wr32(at + 0xcu, r->seconds);
        wr32(at + RECORD_HOLDS, r->state);
        wr32(at + RECORD_HOLDS + 4u, r->state); /* its latched copy, the one the row's text reads */
        wr32(at + 0x18u, r->crown);
        wr32(at + 0x1cu, r->face);
    }
    Ov000_RefreshSelectionGroupDraw();
    Ov000_UpdateNumberDisplays();
}

/* A camp's three entries from the page's files (its crown and face the other way round), and its redraw. */
static void camp_entries(const Camp *camp, u32 menu)
{
    for (u32 g = 0; g < KHDAYS_GAME_SLOTS; ++g) {
        const KhdaysSaveRow *r = &rows[page_top + (int)g];
        const u32 at = menu + SAVE_ENTRIES + g * SAVE_ENTRY_SIZE;
        wr16(at, r->day);
        wr16(at + 2u, r->level);
        wr32(at + 4u, r->munny);
        wr32(at + 8u, r->difficulty);
        wr32(at + 0xcu, r->seconds);
        wr32(at + SAVE_ENTRY_HOLDS, r->state);
        wr32(at + 0x14u, r->crown);
        wr32(at + 0x18u, r->face);
    }
    camp->refresh_rows(menu);
    camp->refresh_digits(menu);
}

/* Where a step `dir` at the page's edge takes the page and the cursor from row `row` (the guest's
 * row after the files' when `guest`: the title's in Mission Mode); *to_row -1 when the cursor stays. */
static int edge_step(int row, int dir, int guest, int *to_row)
{
    const int last = KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS;
    *to_row = -1;
    if (row == KHDAYS_GAME_SLOTS) { /* the guest's row: on to the first file, or back to the last */
        *to_row = dir > 0 ? 0 : KHDAYS_GAME_SLOTS - 1;
        return dir > 0 ? 0 : last;
    }
    if (dir > 0) {
        if (page_top < last) {
            return page_top + 1;
        }
        *to_row = guest ? KHDAYS_GAME_SLOTS : 0;
        return guest ? page_top : 0;
    }
    if (page_top > 0) {
        return page_top - 1;
    }
    *to_row = guest ? KHDAYS_GAME_SLOTS : KHDAYS_GAME_SLOTS - 1;
    return guest ? page_top : last;
}

/* The keys the port takes from the game this frame on a list whose cursor is on row `row` (the
 * guest's after the files' when `guest`): a press at the page's edge is the port's, and every step of
 * that hold until the key is let go (the game, given the key again while it is held, would take it as
 * a new press). *step 1 when this frame steps: *to_top and *to_row (-1: stays) where to. */
static u16 page_step(u16 buttons, int row, int guest, int dir, int *step, int *to_top, int *to_row)
{
    const u16 keys = KHDAYS_KEY_UP | KHDAYS_KEY_DOWN;
    const u16 edge = (u16)((row == KHDAYS_GAME_SLOTS - 1 ? KHDAYS_KEY_DOWN : 0) | (row == 0 ? KHDAYS_KEY_UP : 0) |
                           (row == KHDAYS_GAME_SLOTS ? keys : 0));
    *step = 0;
    *to_top = page_top;
    *to_row = -1;
    taken = (u16)((taken & buttons) | (buttons & edge & keys));
    if (dir == 0 || !((dir > 0 ? KHDAYS_KEY_DOWN : KHDAYS_KEY_UP) & taken)) {
        return taken;
    }
    *step = 1;
    if (row < KHDAYS_GAME_SLOTS && row + dir >= 0 && row + dir < KHDAYS_GAME_SLOTS) {
        *to_row = row + dir;
    } else {
        *to_top = edge_step(row, dir, guest, to_row);
    }
    return taken;
}

static u16 title_page_buttons(u32 ctx, u16 buttons)
{
    const int state = (int)rd32(ctx + STATE);
    const int guest = (*(volatile u8 *)KHDAYS_W(ctx + GUEST_SHOWN)) != 0;
    const int row = (int)rd32(ctx + PAGE);
    const u32 title = title_context();
    const int dir = direction(buttons, title != 0 ? title + TITLE_REPEAT : 0);
    int step, to_top, to_row;
    u16 take;
    if (active != KHDAYS_SAVELIST_LOAD) {
        active = KHDAYS_SAVELIST_LOAD;
        make_rows(&title_counting);
    } else if (state == 2 && khdays_slots_generation() != rows_generation) {
        make_rows(&title_counting); /* a file deleted */
    }
    if (state != 2 || (short)rd32(ctx + CARD_ERROR) != 0) {
        taken = 0;
        return buttons;
    }
    if (guest && ((buttons & ~held) & KHDAYS_KEY_Y)) {
        /* the port's, as on its HD list: Y (the list takes none) plays as the guest from anywhere --
         * the cursor on the guest's row, then the game's A */
        wr32(ctx + PAGE, (u32)KHDAYS_GAME_SLOTS);
        Ov000_PlaceCursorByMode(1, KHDAYS_GAME_SLOTS);
        PlaySound(0, 0);
        return (u16)((buttons & ~KHDAYS_KEY_Y) | KHDAYS_KEY_A);
    }
    take = page_step(buttons, row, guest, dir, &step, &to_top, &to_row);
    if (step) {
        if (to_top != page_top) {
            show_page(to_top);
            title_records(ctx);
        }
        if (to_row >= 0) {
            wr32(ctx + PAGE, (u32)to_row);
            Ov000_PlaceCursorByMode(1, to_row);
        }
        PlaySound(0, 0);
    }
    return (u16)(buttons & ~take);
}

static u16 camp_page_buttons(const Camp *camp, u32 ctx, u32 menu, u16 buttons)
{
    const int phase = (int)rd32(menu + SAVE_PHASE);
    const int opening = (int)rd32(menu + SAVE_OPENING);
    const int row = (int)rd32(menu + SAVE_SLOT);
    const int dir = direction(buttons, ctx + CAMP_REPEAT);
    int step, to_top, to_row;
    u16 take;
    if (active != KHDAYS_SAVELIST_SAVE) {
        active = KHDAYS_SAVELIST_SAVE;
        counted = 0;
        make_rows(camp->counting);
    } else if ((phase == SAVE_PICK || phase == SAVE_DONE) && khdays_slots_generation() != rows_generation) {
        make_rows(camp->counting); /* a file saved */
    }
    if (opening >= 2 && !counted) {
        /* the counter the game's next save goes on from: the highest of all the files, not only of
         * the three it read (Ov025_SaveMenuInitStep, BeginSaveToSlot) */
        const u32 highest = khdays_slots_highest_counter();
        if (highest > rd32(menu + SAVE_COUNTER)) {
            wr32(menu + SAVE_COUNTER, highest);
        }
        counted = 1;
    }
    /* (A kept from the game while held after it took the screen back to its slots: the game would take
     * it as a new press on a slot) */
    page_swallowed &= buttons;
    if (after_save(menu, opening, phase, (u16)(buttons & ~held))) {
        back_to_slots(camp, menu);
        taken = 0;
        page_swallowed = (u16)(buttons & KHDAYS_KEY_A);
        if (buttons & ~held & KHDAYS_KEY_A) {
            PlaySound(0, 1);
        }
        /* a direction reaches the game the next picture, a new press on its slots */
        return (u16)(buttons & ~(KHDAYS_KEY_A | KHDAYS_KEY_UP | KHDAYS_KEY_DOWN));
    }
    if (opening < 3 || phase != SAVE_PICK || rd32(menu + SAVE_ERROR) != 0) {
        taken = 0;
        return (u16)(buttons & ~page_swallowed);
    }
    /* UP steps the slot back, DOWN on (the screen's hooks: StepSelectionBackward, ...Forward) */
    take = page_step(buttons, row, 0, dir, &step, &to_top, &to_row);
    if (step) {
        if (to_top != page_top) {
            show_page(to_top);
            camp_entries(camp, menu);
        }
        if (to_row >= 0) {
            wr32(menu + SAVE_SLOT, (u32)to_row);
            camp->place(menu, row, to_row);
        }
        PlaySound(0, 0);
    }
    return (u16)(buttons & ~(take | page_swallowed));
}

/* The page view while the title or a camp's menu runs, and the game's lists' buttons; 0 when
 * neither runs. */
static int pages(u16 buttons, u16 *out)
{
    u32 ctx = 0;
    const int title_there = in_title();
    const Camp *in_camp = title_there ? NULL : camp_of(&ctx);
    const u32 title = title_there ? title_list() : 0;
    const u32 camp = in_camp != NULL ? camp_save(in_camp, ctx) : 0;
    if (!title_there && in_camp == NULL) {
        if (page_top >= 0) {
            page_top = -1;
            khdays_slots_set_port_view(0);
        }
        return 0;
    }
    if (page_top < 0) {
        /* the title's on the file saved last (the game's cursor starts on its three's latest); a
         * camp's on the file being played, on the row the game's cursor starts on (field 0xc98) */
        const int latest = khdays_slots_latest();
        if (title_there) {
            show_page(page_for(latest >= 0 ? latest : 0, 0));
        } else {
            const int file = current_file >= 0 ? current_file : latest >= 0 ? latest : 0;
            show_page(page_for(file, GameState_GetField(FIELD_SAVE_SLOT, 2)));
        }
    }
    khdays_slots_show_page(page_top);
    if (title != 0) {
        *out = title_page_buttons(title, buttons);
    } else if (camp != 0) {
        *out = camp_page_buttons(in_camp, ctx, camp, buttons);
    } else if (active != KHDAYS_SAVELIST_NONE) {
        active = KHDAYS_SAVELIST_NONE;
    }
    return 1;
}

/* ---- The file being played ------------------------------------------------------------------- */

/* Whichever list shows (the port's, the pages or the DS's three), the file the title loads (its state 6,
 * the guest's row past the files' three) and the file a camp's save screen writes (its phase SAVE_SAVING)
 * are the file being played; back on the title, none until one is loaded (a new game has none). */
static int title_was, title_state_was = -1, camp_phase_was = -1;

static void follow_played(void)
{
    const int title_there = in_title();
    const u32 title = title_list();
    u32 ctx = 0;
    const Camp *in_camp = title_there ? NULL : camp_of(&ctx);
    const u32 save = in_camp != NULL ? camp_save(in_camp, ctx) : 0;
    if (title_there && !title_was) {
        current_file = KHDAYS_PLAYED_NONE;
        played_level = 0;
        khdays_missions_loaded(KHDAYS_PLAYED_NONE);
    }
    title_was = title_there;
    if (title != 0) {
        const int state = (int)rd32(title + STATE);
        if (state == 6 && title_state_was != 6) {
            const int row = (int)rd32(title + PAGE);
            current_file = row >= GUEST_PAGE ? KHDAYS_PLAYED_GUEST : khdays_slots_file_of(row);
            /* (the guest game starts anew, at level 1) */
            played_level = current_file == KHDAYS_PLAYED_GUEST                     ? 1
                           : current_file >= 0 && current_file < KHDAYS_SLOT_COUNT ? rows[current_file].level
                                                                                   : 0;
            if (current_file == KHDAYS_PLAYED_GUEST) {
                khdays_log("save list: the guest game begins (no file)");
            } else {
                khdays_log("save list: file %d loaded", current_file + 1);
            }
            khdays_missions_loaded(current_file);
        }
        title_state_was = state;
    } else {
        title_state_was = -1;
    }
    if (save != 0) {
        const int phase = (int)rd32(save + SAVE_PHASE);
        if (phase == SAVE_SAVING && camp_phase_was != SAVE_SAVING) {
            current_file = khdays_slots_file_of((int)rd32(save + SAVE_SLOT));
        }
        camp_phase_was = phase;
    } else {
        camp_phase_was = -1;
    }
}

int khdays_savelist_played_level(void)
{
    return played_level;
}

int khdays_savelist_played_file(void)
{
    return current_file;
}

void khdays_savelist_set_played_file(int file)
{
    current_file = file;
}

int khdays_savelist_camp_menu(int *screen, int *entry)
{
    u32 ctx = 0, menu;
    const Camp *in_camp = in_title() ? NULL : camp_of(&ctx);
    *screen = *entry = -1;
    if (in_camp == NULL) {
        return 0;
    }
    *screen = (int)rd32(in_camp->running);
    /* its menu's state, the entry under its cursor (runtime/menus/camp.c: SCREEN_STATE, ENTRY) */
    menu = rd32(ctx + CAMP_PAGE);
    if (*screen != 0 || !in_ram(menu)) {
        return 0;
    }
    *entry = (int)rd32(menu + 0x88u);
    return (*entry >= 1 && *entry <= 9) || (*entry >= 101 && *entry <= 107) ? in_camp->overlay : 0;
}

int khdays_savelist_camp_save_screen(int overlay)
{
    for (int i = 0; i < CAMPS; ++i) {
        if (camps[i].overlay != overlay || !khdays_overlay_loaded(overlay)) {
            continue;
        }
        for (u32 index = 0; index < 16; ++index) {
            if (rd32(camps[i].table + 8u * index) == camps[i].save_screen) {
                return (int)index;
            }
        }
    }
    return -1;
}

/* ---- The two together ------------------------------------------------------------------------- */

static void publish(u32 title, u32 camp)
{
    KhdaysSaveView v;
    const u16 bright = IO16(0x106c); /* MASTER_BRIGHT, engine B: the lower screen's */
    const int mode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
    memset(&v, 0, sizeof(v));
    v.language = language();
    v.cursor = cursor;
    v.chosen = khdays_slots_shown();
    v.ask[0] = v.ask[1] = -1;
    v.ask_file = khdays_slots_shown() + 1;
    v.master = (int16_t)(mode == 1 ? factor : mode == 2 ? -factor : 0);
    v.rows_changed = rows_changed;
    memcpy(v.rows, rows, sizeof(rows));
    if (title != 0) {
        title_view(title, &v);
    } else if (camp != 0) {
        camp_view(camp, &v);
    }
    AcquireSRWLockExclusive(&lock);
    shown = v;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_savelist_view(KhdaysSaveView *out)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    ReleaseSRWLockShared(&lock);
}

uint16_t khdays_savelist_buttons(uint16_t buttons)
{
    const int available = khdays_savelist_available(language());
    u16 paged = buttons;
    follow_played();
    if (!available && all_files != 0 && pages(buttons, &paged)) {
        held = buttons;
        return paged;
    }
    if (page_top >= 0) {
        page_top = -1; /* the pages left (the setting or the list's pieces changed) */
    }
    const u32 title = available ? title_list() : 0;
    u32 ctx = 0;
    const Camp *in_camp = available ? camp_of(&ctx) : NULL;
    const u32 camp = title == 0 ? camp_save(in_camp, ctx) : 0;
    u16 out = buttons;
    /* the port's view of the files while its list can show: on the title (its check of the files
     * for CONTINUE, its list) and in a camp's menu (its save screen) */
    khdays_slots_set_port_view(available && (in_title() || in_camp != NULL));
    if (title != 0) {
        out = title_buttons(title, buttons);
    } else if (camp != 0) {
        out = camp_buttons(in_camp, ctx, camp, buttons);
    } else if (active != KHDAYS_SAVELIST_NONE) {
        active = KHDAYS_SAVELIST_NONE;
        handed = 0;
        steer_field = 0;
    }
    if (title != 0 || camp != 0 || shown.screen != KHDAYS_SAVELIST_NONE) {
        publish(title, camp);
    }
    held = buttons;
    return out;
}
