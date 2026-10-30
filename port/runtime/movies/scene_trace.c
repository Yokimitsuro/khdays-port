/* The field's event scenes (scene_trace.h): each one recognised as its skippable stretch begins and
 * played by the theater's HD videos when the map has it, the days' monologues with their voice, the
 * trace (KHDAYS_TRACE_SCENES) and the testing knobs.
 *
 * The field's root (data_ov002_0207fa00) holds the mission it opened (u16 at +0x8ba8, the file
 * mi/mi/%04d) and the script it runs there (u16 at +0x8baa, the script's name as "%d",
 * Ov002_FormatResultLine). The event scene's root is data_ov023_0208a784's second word while ov023
 * is loaded; its script runs on the game's script machine with its state at +0x4b88: the depth at
 * +0x124, the entries from +4 (0x48 bytes each: +0xc the script's start, +0x10 the next command),
 * the skip target at +0x584 with its depth at +0x580 (Game_ActionSetBranchTarget) and the skip
 * request at +0x588 (Game_UnwindActionStack jumps to the target when both are set). The event's
 * status halfword is at +0x8758c. */
#include "scene_trace.h"
#include "movie_hd.h"
#include "../../host/movies/movie_hd.h"
#include "../menus/newgame.h"
#include "scenes.h"
#include "../core/events.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../menus/mission_start.h"
#include "../mods/installed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int khdays_overlay_loaded(int id); /* runtime/core/overlays.c */
/* the game's own */
void GameState_SetField(int field, int bits, int value);
void SoundMgr_StopAllSePlayers(void);
int PauseMenu_GetMode(void); /* its pause's step, 0 when closed */

#define FIELD_ROOT 0x0207fa00u
#define FIELD_MISSION 0x8ba8u
#define FIELD_SCRIPT 0x8baau
#define EVENT_ROOT 0x0208a784u
#define EVENT_STATE 0x4b88u
#define EVENT_STATUS 0x8758cu
#define OV_EVENT 23
#define MISSION_FLAGS 0x0204c240u /* data_0204c240: its first byte's bit 2 Mission Mode; the field's record */
#define MISSION_MODE 4u
#define SCENE_CTL 0x0204bda8u  /* the scene controller: the pending scene at +0xc, its argument at +0x10 */
#define GAME_STATE 0x0204be18u /* gGameState: the game state's block */
#define GAME_STATE_SIZE 0x2000u

#define STATE_DEPTH 0x124u
#define STATE_ENTRIES 4u
#define ENTRY_SIZE 0x48u
#define ENTRY_START 0xcu
#define ENTRY_NEXT 0x10u
#define STATE_TARGET_DEPTH 0x580u
#define STATE_TARGET 0x584u
#define STATE_SKIP 0x588u

typedef struct EventView {
    int running;      /* ov023 loaded with a scene */
    uint32_t mission; /* the field's, else ~0 */
    uint32_t script;
    uint32_t status;
    uint32_t target; /* the skip target's reference, 0 none */
    uint32_t target_depth, skip, start, next;
} EventView;

static uint32_t u32_at(uint32_t address)
{
    return *(const volatile uint32_t *)KHDAYS_W(address);
}

static uint16_t u16_at(uint32_t address)
{
    return *(const volatile uint16_t *)KHDAYS_W(address);
}

static EventView view_now(void)
{
    EventView v = {0};
    const uint32_t field = u32_at(FIELD_ROOT);
    v.mission = v.script = ~0u;
    if (!khdays_overlay_loaded(OV_EVENT) || !khdays_game_ram(field)) {
        return v;
    }
    v.mission = u16_at(field + FIELD_MISSION);
    v.script = u16_at(field + FIELD_SCRIPT);
    {
        const uint32_t root = u32_at(EVENT_ROOT + 4u);
        if (khdays_game_ram(root)) {
            const uint32_t state = root + EVENT_STATE;
            const uint32_t entry = state + STATE_ENTRIES + u32_at(state + STATE_DEPTH) * ENTRY_SIZE;
            v.running = 1;
            v.status = u16_at(root + EVENT_STATUS);
            v.target = u32_at(state + STATE_TARGET);
            v.target_depth = u32_at(state + STATE_TARGET_DEPTH);
            v.skip = u32_at(state + STATE_SKIP);
            v.start = u32_at(entry + ENTRY_START);
            v.next = u32_at(entry + ENTRY_NEXT);
        }
    }
    return v;
}

static int tracing_scenes(void)
{
    static int tracing = -1;
    if (tracing < 0) {
        tracing = getenv("KHDAYS_TRACE_SCENES") != NULL;
    }
    return tracing;
}

/* Ov002_ConstructGameplayScene's record (prepare.py HOOKS): its argument, else data_0204c240; the
 * mission at +2, the event script at +4. KHDAYS_TEST_EVENT=<mission>/<script> puts that script in
 * place of the first field's; KHDAYS_TEST_DAY=<day> sets the story's day there. */
void khdays_scene_field_record(void *record, int from_argument)
{
    static int test = -1, mission, script;
    uint8_t *r = (uint8_t *)record;
    khdays_mission_start_record(record); /* a mod's mission.start (runtime/menus/mission_start.h) */
    if (test < 0) {
        const char *v = getenv("KHDAYS_TEST_EVENT");
        test = v != NULL && sscanf(v, "%d/%d", &mission, &script) == 2;
    }
    if (tracing_scenes()) {
        fprintf(stderr, "scene: f%u field record (%s):", khdays_events_vblank_count(),
                from_argument ? "the scene's argument" : "data_0204c240");
        for (int i = 0; i < 16; ++i) {
            fprintf(stderr, " %02x", r[i]);
        }
        fprintf(stderr, "\n");
    }
    if (test == 1) {
        *(uint16_t *)(r + 2) = (uint16_t)mission;
        *(uint16_t *)(r + 4) = (uint16_t)script;
        fprintf(stderr, "scene: KHDAYS_TEST_EVENT: the field opens mission %d with script %d\n", mission, script);
        test = 2;
    }
    {
        /* KHDAYS_TEST_DAY=<day>: the story's day (the game state's field 0, 9 bits: the field takes its
         * script number from it once built) set at the first field's construction */
        static int done;
        const char *v = getenv("KHDAYS_TEST_DAY");
        if (v != NULL && !done) {
            GameState_SetField(0, 9, atoi(v));
            fprintf(stderr, "scene: KHDAYS_TEST_DAY: day %d\n", atoi(v));
            done = 1;
        }
    }
}

/* Asks the game's own skip, as the pause's Skip does (sound effects stopped, the request at +0x588
 * set), from frame `skip_frame` on: for a scene the map skips, or KHDAYS_TEST_SKIP_AT frames into any
 * stretch. */
static uint32_t skip_state, skip_frame;

/* KHDAYS_TEST_SCENE=<slot>:<entry> (4:6 for EV_S#6): the first scene path the event's script holds
 * ("4:196" on day 7: Game_ActionLoadArenaResource loads entry 196 of the archive in slot 4) is
 * written over with this one where it fits, so the event plays that scene. */
static void test_scene(void *st)
{
    static int done = -1;
    const char *v;
    uint8_t *start, *data;
    uint32_t word;
    if (done < 0) {
        done = getenv("KHDAYS_TEST_SCENE") == NULL;
    }
    if (done || !khdays_overlay_loaded(OV_EVENT) || KHDAYS_REF(st) != u32_at(EVENT_ROOT + 4u) + EVENT_STATE) {
        return;
    }
    v = getenv("KHDAYS_TEST_SCENE");
    start = (uint8_t *)KHDAYS_W(u32_at(KHDAYS_REF(st) + STATE_ENTRIES + ENTRY_START) - 4u); /* depth 0's script */
    memcpy(&word, start, 4);
    data = start + word;
    for (int at = 0; at + 8 < 0x800; ++at) {
        size_t room = 0;
        if (data[at] < '0' || data[at] > '9' || data[at + 1] != ':' || (at > 0 && data[at - 1] != '\0')) {
            continue;
        }
        while (at + (int)room < 0x800 && data[at + room] != '\0') {
            ++room;
        }
        while (at + (int)room < 0x800 && data[at + room] == '\0' && ((at + room) & 3u) != 0) {
            ++room; /* the padding to the next word */
        }
        if (strlen(v) + 1 <= room) {
            fprintf(stderr, "scene: KHDAYS_TEST_SCENE: the event's \"%s\" becomes \"%s\"\n", (const char *)data + at,
                    v);
            memset(data + at, 0, room);
            memcpy(data + at, v, strlen(v));
            done = 1;
            return;
        }
    }
}

/* Game_RunActionScript's step (diag.c khdays_trace_script): `st` the machine's state. */
void khdays_scene_step(void *st)
{
    khdays_movie_hd_scene_step(KHDAYS_REF(st), st);
    test_scene(st);
    if (skip_state == 0u || KHDAYS_REF(st) != skip_state || khdays_events_vblank_count() < skip_frame) {
        return;
    }
    SoundMgr_StopAllSePlayers();
    *(volatile uint32_t *)((uint8_t *)st + STATE_SKIP) = 1u;
    fprintf(stderr, "scene: f%u the game's skip asked\n", khdays_events_vblank_count());
    skip_state = 0u;
}

/* The scene running now (its key), "" when none: set when a stretch begins, cleared when it ends. */
static char current[48];
static int monologue; /* current is a day's monologue (no event of its own) */
/* With the Menu Overhaul (runtime/mods/installed.h), the monologue's voice is heard with the theater's subtitles
 * of what it says: the DS's words (which say it differently) are hidden, the page turns by itself, and it ends
 * once the voice has. */
static int monologue_voiced;

const char *khdays_scene_current(void)
{
    return current;
}

/* The scenes' skip mark (GameState field 0x2484): a skipped scene sets it and the scenes after it skip
 * themselves while it is set. The port clears it after its own skip (HD videos watched), so the next
 * scene of a chain still plays. */
#define SKIP_MARK 0x2484
extern void GameState_SetField(int field, int bits, int value);
static int map_skip; /* the scene running now was skipped as the scene map says (not by the player) */

/* Game_ActionSetBranchTarget (prepare.py HOOKS): `state` the script machine's (a reference), the
 * operand the target's offset from the entry's script start, -1 clearing it. */
void khdays_scene_target(unsigned int state, int operand)
{
    const uint32_t depth = u32_at(state + STATE_DEPTH);
    const uint32_t entry = state + STATE_ENTRIES + depth * ENTRY_SIZE;
    if (operand == -1) {
        const int watched = khdays_movie_hd_scene_ended();
        if (watched || map_skip) {
            GameState_SetField(SKIP_MARK, 1, 0);
            khdays_log("scene %s: the game's mark of a skipped scene cleared (%s), so the scene after it plays",
                       current, watched ? "its HD videos were watched" : "the HD movie before it showed it");
        }
        map_skip = 0;
        current[0] = '\0';
    } else if (!khdays_scenes_recognise(u32_at(entry + ENTRY_START), current, (int)sizeof(current))) {
        current[0] = '\0';
    } else if (getenv("KHDAYS_TEST_SKIP_AT") != NULL) {
        /* a recognised scene's skip asked by the test (the movies' schedules keep theirs) */
        skip_state = state;
        skip_frame = khdays_events_vblank_count() + (uint32_t)atoi(getenv("KHDAYS_TEST_SKIP_AT"));
    } else if ((*(const volatile uint8_t *)KHDAYS_W(MISSION_FLAGS) & MISSION_MODE) == 0u) {
        /* the story's: the theater's videos in its place when the map gives them */
        const KhdaysSceneEntry *e = khdays_scene_map_find(khdays_scenes_map(), current);
        if (e != NULL && e->skip && khdays_movie_hd_last_shown(e->stems[0])) {
            skip_state = state; /* the HD movie before it showed it */
            skip_frame = khdays_events_vblank_count();
            map_skip = 1;
            khdays_log("scene %s: skipped, as %s before it showed it", current, e->stems[0]);
        } else if (e != NULL && !e->off && !e->skip) {
            khdays_movie_hd_scene(current, state, e->stems, e->from, e->until, e->stem_count);
        }
    }
    if (tracing_scenes()) {
        fprintf(stderr, "scene: f%u skip target %s +%x (depth %u, script at %08x, command at +%x)\n",
                khdays_events_vblank_count(), operand == -1 ? "cleared" : "set", operand == -1 ? 0u : (unsigned)operand,
                depth, u32_at(entry + ENTRY_START), u32_at(entry + ENTRY_NEXT) - u32_at(entry + ENTRY_START));
        if (operand != -1 && current[0] != '\0') {
            const KhdaysSceneEntry *e = khdays_scene_map_find(khdays_scenes_map(), current);
            fprintf(stderr, "scene: f%u start %s: ", khdays_events_vblank_count(), current);
            if (e == NULL) {
                fprintf(stderr, "ds (not in the map)\n");
            } else if (e->off) {
                fprintf(stderr, "ds (off)\n");
            } else if (e->skip) {
                fprintf(stderr, "%s\n",
                        khdays_movie_hd_last_shown(e->stems[0]) ? "skipped (the HD movie showed it)"
                                                                : "ds (its HD movie not shown)");
            } else {
                fprintf(stderr, "hd");
                for (int i = 0; i < e->stem_count; ++i) {
                    fprintf(stderr, " %s", e->stems[i]);
                }
                fprintf(stderr, "\n");
            }
        } else if (operand != -1) {
            fprintf(stderr, "scene: f%u start of a script none of the event archives holds\n",
                    khdays_events_vblank_count());
        }
    }
}

/* Ov007_SceneInit (prepare.py HOOKS): the day's monologue begins, its record `record` of ui/mnl's
 * texts (picked by the story's day: 0 the prologue, 1-7 days 7-13). Its key is mnl#<record>. */
void khdays_scene_monologue(int record)
{
    char key[32];
    const KhdaysSceneEntry *e;
    snprintf(key, sizeof(key), "mnl#%d", record);
    snprintf(current, sizeof(current), "%s", key);
    monologue = 1;
    e = khdays_scene_map_find(khdays_scenes_map(), key);
    if (tracing_scenes()) {
        fprintf(stderr, "scene: f%u start %s: %s\n", khdays_events_vblank_count(), key,
                e == NULL ? "ds (not in the map)"
                : e->off  ? "ds (off)"
                          : "the theater's voice over the ds");
    }
    monologue_voiced = 0;
    if (e != NULL && !e->off) {
        /* the clips are black: their sound alone, with their subtitles when they show */
        if (khdays_movie_hd_voice(key, e->stems, e->stem_count) && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) &&
            khdays_subtitles_shown() && khdays_movie_hd_subtitles_exist(e->stems[0])) {
            monologue_voiced = 1;
        }
    }
}

/* Ov007_SetupSubDisplay (prepare.py FILE_HOOKS): the DS's words and the page's arrow are left out while
 * the voice speaks them with its subtitles; Roxas's picture stays. */
int khdays_monologue_words_hidden(void)
{
    return monologue && monologue_voiced;
}

/* The monologue's A (turn the page): none while the voice speaks for it. */
int khdays_monologue_next(int pressed)
{
    return monologue && monologue_voiced ? 0 : pressed;
}

/* The monologue's START (leave it). With the Menu Overhaul the window's pause panel takes START: left once
 * Skip is chosen there, and by itself once the voice has said it all. */
int khdays_monologue_leave(int pressed)
{
    if (!monologue || !khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return pressed;
    }
    return khdays_monologue_skip_taken() || (monologue_voiced && !khdays_hd_voice_speaking());
}

int khdays_monologue_running(void)
{
    return monologue;
}

/* Ov007_RequestSceneChange (prepare.py HOOKS): the monologue ends, asking for the calendar. */
void khdays_scene_monologue_ends(void)
{
    if (tracing_scenes() && current[0] != '\0') {
        fprintf(stderr, "scene: f%u end %s\n", khdays_events_vblank_count(), current);
    }
    current[0] = '\0';
    monologue = 0;
    monologue_voiced = 0;
    khdays_movie_hd_voice_stop();
}

/* With KHDAYS_TEST_SCENE, the port leaves 30 frames after the scene asked for ends (the shared
 * archive's, slot 4: EV_S#<entry>) or, for another slot, after the event that played it closes. */
static void test_scene_frame(uint32_t frame)
{
    static int armed = -1, started, seen;
    static uint32_t closed;
    static char wanted[48];
    EventView now;
    if (armed < 0) {
        const char *v = getenv("KHDAYS_TEST_SCENE");
        armed = v != NULL;
        if (v != NULL && v[0] == '4' && v[1] == ':') {
            snprintf(wanted, sizeof(wanted), "EV_S#%s", v + 2);
        }
    }
    if (!armed) {
        return;
    }
    now = view_now();
    if (wanted[0] != '\0' && strcmp(current, wanted) == 0) {
        seen = 1;
    }
    if (now.running && now.status != 0u && !(seen && strcmp(current, wanted) != 0)) {
        started = 1;
        closed = 0u;
    } else if (started && closed == 0u) {
        closed = frame;
    } else if (started && frame >= closed + 30u) {
        fprintf(stderr, "scene: f%u KHDAYS_TEST_SCENE: the scene ended; leaving\n", frame);
        fflush(stderr);
        _Exit(0);
    }
}

/* KHDAYS_SCENE_STATE=<file>: when the field's event closes, the game's state (the block gGameState
 * refers to, 0x2000 bytes) written to <file>.<n>, to compare a scene's HD course with the game's own. */
static void state_at_close(uint32_t frame)
{
    static int running, n;
    const int now = view_now().running;
    const char *path = getenv("KHDAYS_SCENE_STATE");
    if (path != NULL && running && !now) {
        const uint32_t state = u32_at(GAME_STATE);
        char name[512];
        FILE *f;
        snprintf(name, sizeof(name), "%s.%d", path, n++);
        if (khdays_game_ram(state) && (f = fopen(name, "wb")) != NULL) {
            fwrite(KHDAYS_W(state), 1, GAME_STATE_SIZE, f);
            fclose(f);
            fprintf(stderr, "scene: f%u the game's state at the event's close written to %s\n", frame, name);
        }
    }
    running = now;
}

static void trace_frame(uint32_t frame);

void khdays_scene_trace_frame(uint32_t frame)
{
    test_scene_frame(frame);
    state_at_close(frame);
    if (khdays_movie_hd_scene_paused()) {
        /* the game's pause over a scene's paused video: open, and whether its Skip set the request */
        const uint32_t st = khdays_movie_hd_scene_state();
        khdays_movie_hd_scene_frame(PauseMenu_GetMode() != 0, st != 0u && u32_at(st + STATE_SKIP) != 0u);
    }
    if (current[0] != '\0' && !monologue && !view_now().running) {
        current[0] = '\0'; /* the event closed without clearing its target */
        khdays_movie_hd_scene_ended();
    }
    if (tracing_scenes()) {
        trace_frame(frame);
    }
}

/* KHDAYS_TRACE_SCENES, once a frame: what changed of the field's record and script, the scene
 * controller, the event, its target and the game's movie. */
static void trace_frame(uint32_t frame)
{
    static int first = 1;
    static EventView was;
    static int was_movie;
    EventView now;
    int movie;
    if (first) {
        was.mission = was.script = ~0u;
        first = 0;
    }
    now = view_now();
    {
        /* the record the field is built from when its scene has no argument
         * (Ov002_ConstructGameplayScene): the mode byte, the mission at +2, the script at +4 */
        static uint32_t record[2] = {~0u, ~0u};
        const uint32_t r0 = u32_at(MISSION_FLAGS), r1 = u32_at(MISSION_FLAGS + 4u);
        if (r0 != record[0] || r1 != record[1]) {
            fprintf(stderr, "scene: f%u record mode %02x mission %u script %u (+6 %04x)\n", frame, r0 & 0xffu, r0 >> 16,
                    r1 & 0xffffu, r1 >> 16);
            record[0] = r0;
            record[1] = r1;
        }
    }
    {
        /* the scene controller: the pending scene and its argument */
        static uint32_t pending = ~0u, argument = ~0u;
        const uint32_t p = u32_at(SCENE_CTL + 0xcu), a = u32_at(SCENE_CTL + 0x10u);
        if (p != pending || a != argument) {
            fprintf(stderr, "scene: f%u pending scene %u argument %08x", frame, p, a);
            if (khdays_game_ram(a)) {
                fprintf(stderr, " -> %08x %08x %08x", u32_at(a), u32_at(a + 4u), u32_at(a + 8u));
            }
            fprintf(stderr, "\n");
            pending = p;
            argument = a;
        }
    }
    {
        /* the field's mission and script number from its construction on (ov002 loaded) */
        static uint32_t field_script = ~0u;
        const uint32_t field = u32_at(FIELD_ROOT);
        const uint32_t s = khdays_overlay_loaded(2) && khdays_game_ram(field)
                               ? (uint32_t)u16_at(field + FIELD_MISSION) << 16 | u16_at(field + FIELD_SCRIPT)
                               : ~0u;
        if (s != field_script) {
            fprintf(stderr, "scene: f%u field mission/script %u/%d\n", frame, s >> 16, (int)(int16_t)(s & 0xffffu));
            field_script = s;
        }
    }
    if (now.mission != was.mission || now.script != was.script) {
        if (now.mission == ~0u) {
            fprintf(stderr, "scene: f%u no event script\n", frame);
        } else {
            fprintf(stderr, "scene: f%u script %04u/%u\n", frame, now.mission, now.script);
        }
    }
    if (now.status != was.status) {
        fprintf(stderr, "scene: f%u status %04x\n", frame, now.status);
    }
    if (now.target != was.target || now.skip != was.skip) {
        if (now.target != 0u) {
            /* the target in the script of the entry it was set from: the entry now when the depths
             * agree (a called routine runs deeper) */
            fprintf(stderr, "scene: f%u target %08x depth %u (+%x in the script at %08x) skip %u\n", frame, now.target,
                    now.target_depth, now.target - now.start, now.start, now.skip);
        } else {
            fprintf(stderr, "scene: f%u target none (at +%x of %08x) skip %u\n", frame, now.next - now.start, now.start,
                    now.skip);
        }
    }
    movie = khdays_movie_game_playing();
    if (movie != was_movie) {
        fprintf(stderr, "scene: f%u game movie %s\n", frame, movie ? "starts" : "ends");
        was_movie = movie;
    }
    was = now;
    fflush(stderr);
}
