/* The services the HAL calls into on the port's side (see runtime.h): the
 * runtime's start, the reports of what the HAL lacks, and each frame's
 * traces and scene report (khdays_host_frame). */
#include "../../hal/hal.h"
#include "../../host/app/host.h"
#include "clock.h"
#include "window.h"
#include "../display/display.h"
#include "events.h"
#include "game_heap.h"
#include "../input/input.h"
#include "files.h"
#include "io.h"
#include "rom.h"
#include "runtime.h"
#include "session_log.h"
#include "../movies/scene_trace.h"
#include "../characters/characters.h"
#include "../plugins/actors.h"
#include "../plugins/plugins.h"
#include "../missions/missions.h"

#include <stdio.h>
#include <stdlib.h>

extern void khdays_threads_init(void);

void khdays_hal_unimplemented(const char *what)
{
    fprintf(stderr, "khdays-native: reached an unimplemented routine: %s\n", what);
    fflush(stderr);
    exit(3);
}

void khdays_abi_gap(const char *what)
{
    fprintf(stderr, "khdays-native: reached a call the ROM makes differently: %s\n", what);
    fflush(stderr);
    exit(10);
}

int khdays_runtime_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!khdays_rom_init()) {
        return 0;
    }
    khdays_missions_start();   /* the mods' new missions: their files in the ROM before anything reads it */
    khdays_characters_start(); /* the mods' characters: their folders in the ROM too */
    khdays_input_init();
    khdays_display_init();
    if (!khdays_host_start()) {
        return 0;
    }
    khdays_boot_environment();
    khdays_io_reset();
    khdays_io_guard_init();
    khdays_threads_init();
    khdays_clock_init();
    if (getenv("KHDAYS_TEST_IPC") != NULL) {
        /* an IPC register read at its DS address, as a game access the port does not reach: the guard stops
         * the game, naming it (io_guard.c) */
        (void)*(volatile u16 *)0x04000180;
    }
    if (getenv("KHDAYS_TEST_DS_ADDRESS") != NULL) {
        /* a main RAM read at its DS address, as a port access not routed through the window: the guard stops
         * the game, naming it */
        (void)*(volatile u32 *)0x02000000;
    }
    return 1;
}

/* r9 as the ARM9 leaves it, where the decomp's C reads a local the ROM keeps in r9 (prepare.py
 * ABI_FIXES): Obj_UpdateAll sets it to each update's result. A read before the frame's first update
 * stops, naming the reader. */
static int rom_r9, rom_r9_known;

void khdays_rom_r9_unknown(void)
{
    rom_r9_known = 0;
}

void khdays_rom_r9_set(int value)
{
    rom_r9 = value;
    rom_r9_known = 1;
}

int khdays_rom_r9(const char *reader)
{
    if (!rom_r9_known) {
        khdays_abi_gap(reader);
    }
    return rom_r9;
}

static int in_game_ram(u32 p)
{
    return khdays_game_ram(p); /* main RAM or the game's heaps */
}

/* KHDAYS_TRACE_PLAYER[=N]: logs the local player's position every N frames (30 by default). The
 * actors' table is at *(data_ov022_020b2e78 + 4), 0xc bytes an entry; the position is at +0x48c. */
static void trace_player(u32 frame)
{
    static int every = -1;
    u32 base, entry, actor;
    if (every < 0) {
        const char *v = getenv("KHDAYS_TRACE_PLAYER");
        every = v == NULL ? 0 : atoi(v) > 0 ? atoi(v) : 30;
    }
    if (every == 0 || frame % (u32)every != 0) {
        return;
    }
    base = *(const volatile u32 *)KHDAYS_W(0x020b2e7c);
    for (u32 i = 0; i < 8 && in_game_ram(base); ++i) {
        entry = *(const volatile u32 *)KHDAYS_W(base + i * 0xc + 4);
        actor = in_game_ram(entry) ? *(const volatile u32 *)KHDAYS_W(entry + 0x20) : 0;
        if (in_game_ram(actor)) {
            const volatile s32 *pos = (const volatile s32 *)KHDAYS_W(actor + 0x48c);
            fprintf(stderr, "player: frame %u, entry %u, actor %08x at (%.2f, %.2f, %.2f)\n", frame, i, actor,
                    pos[0] / 4096.0, pos[1] / 4096.0, pos[2] / 4096.0);
        }
    }
}

/* KHDAYS_TRACE_HEAP: the game's two heaps every 60 frames (where, size, use, free bytes, largest free
 * block) and how much the PC has given. Handles: main data_0204c044, sub data_0204c030, the current
 * one at gCurrentHeap (0x0204c028). A head's start and end at +0x18 and +0x1c, its free blocks from
 * +0x24 (size +4, next +0xc). */
static void trace_heap(u32 frame)
{
    static int trace = -1;
    static const struct {
        u32 handle;
        const char *name;
    } heaps[2] = {{0x0204c044u, "main heap"}, {0x0204c030u, "sub heap"}};
    u32 current;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_HEAP") != NULL;
    }
    if (!trace || frame % 60 != 0) {
        return;
    }
    current = *(const volatile u32 *)KHDAYS_W(0x0204c028);
    for (int k = 0; k < 2; ++k) {
        KhdaysHeapFigures f;
        if (!khdays_game_heap_figures(*(const volatile u32 *)KHDAYS_W(heaps[k].handle), &f)) {
            continue;
        }
        fprintf(stderr, "heap: frame %u, %s%s, %08x..%08x, %u bytes, %u used, %u free in %u blocks, the largest %u\n",
                frame, heaps[k].name, current == heaps[k].handle ? ", current" : "", f.start, f.end, f.end - f.start,
                f.end - f.start - f.free, f.free, f.blocks, f.largest);
    }
    if (!khdays_game_heap_ds_memory()) {
        fprintf(stderr, "memory: frame %u, %llu bytes of the game's heaps committed\n", frame,
                khdays_heaps_committed());
    }
}

/* KHDAYS_TRACE_MISSION in the field: the chests and the time limit when they change. Root
 * *data_ov002_0207fa00; the chests' class is the module slot at root +0x8d82 (32 words at +0x17c of
 * *0x0207fa24), its pieces from +0x54, +0x4e apart, +0x50 of them (a claimed one's flags +0x12, point
 * +0x1c, contents +0x1b6, slot +0x1ba). The time limit (ms) at root +0x8d04, the clock's start at
 * +0x8d00. */
static void trace_chests_and_time(u32 frame)
{
    static u32 last_chests = ~0u;
    static s32 last_limit = -2;
    static int last_id = -2, last_mode = -1;
    const u32 root = *(const volatile u32 *)KHDAYS_W(0x0207fa00);
    const u32 module = *(const volatile u32 *)KHDAYS_W(0x0207fa24);
    /* the mode flags: with bit 1 set no chest and no emblem is put out */
    const int mode = *(const volatile u8 *)KHDAYS_W(0x0204c240);
    u32 cls = 0, claimed = 0, count = 0, stride = 0, base = 0;
    s8 id;
    s32 limit;
    if (!in_game_ram(root)) {
        return;
    }
    id = *(const volatile s8 *)KHDAYS_W(root + 0x8d82);
    if (mode != last_mode || id != last_id) {
        last_mode = mode;
        last_id = id;
        fprintf(stderr, "mission: frame %u, mode flags %02x, chests' module slot %d\n", frame, mode, id);
    }
    limit = *(const volatile s32 *)KHDAYS_W(root + 0x8d04);
    if (limit != last_limit) {
        last_limit = limit;
        fprintf(stderr, "mission: frame %u, time limit %d ms (clock started at %d ms)\n", frame, limit,
                *(const volatile s32 *)KHDAYS_W(root + 0x8d00));
    }
    if (id >= 0 && id < 32 && in_game_ram(module)) {
        cls = *(const volatile u32 *)KHDAYS_W(module + 0x17c + (u32)id * 4);
    }
    if (in_game_ram(cls)) {
        stride = *(const volatile u16 *)KHDAYS_W(cls + 0x4e);
        count = *(const volatile u16 *)KHDAYS_W(cls + 0x50);
        base = *(const volatile u32 *)KHDAYS_W(cls + 0x54);
        for (u32 i = 0; in_game_ram(base) && i < count; ++i) {
            claimed += *(const volatile u16 *)KHDAYS_W(base + i * stride + 0x12) != 0;
        }
    }
    if (claimed == last_chests) {
        return;
    }
    last_chests = claimed;
    fprintf(stderr, "mission: frame %u, chests' class %08x (module slot %d), %u of %u placed\n", frame, cls, id,
            claimed, count);
    for (u32 i = 0; i < count && claimed > 0; ++i) {
        const u32 piece = base + i * stride;
        const volatile s32 *pos = (const volatile s32 *)KHDAYS_W(piece + 0x1c);
        if (*(const volatile u16 *)KHDAYS_W(piece + 0x12) == 0) {
            continue;
        }
        fprintf(stderr, "chest: frame %u, piece %u, slot %u, contents %u, at (%.3f, %.3f, %.3f)\n", frame, i,
                *(const volatile u8 *)KHDAYS_W(piece + 0x1ba), *(const volatile u16 *)KHDAYS_W(piece + 0x1b6),
                pos[0] / 4096.0, pos[1] / 4096.0, pos[2] / 4096.0);
    }
}

/* KHDAYS_TRACE_MISSION: the field's pickups (*data_ov002_0207fa28's stage, Ov002_FindFreeSpotId: a bit per id,
 * MSB first, set while free; ids 0-31 the band an emblem takes, Ov002_TakeEntryOfKind1; where each was last put at
 * +0x2528) and the chests opened (the root's +0x8d80, Ov002_NoteSeatAnswer's marks), when they change. */
static void trace_pickups(u32 frame)
{
    static u32 last_taken = ~0u, last_opened = ~0u;
    const u32 stage = *(const volatile u32 *)KHDAYS_W(0x0207fa28 + 4);
    const u32 root = *(const volatile u32 *)KHDAYS_W(0x0207fa00);
    const u32 bits = in_game_ram(stage) ? *(const volatile u32 *)KHDAYS_W(stage + 0x5c) : 0;
    const u32 taken = in_game_ram(bits) ? ~*(const volatile u32 *)KHDAYS_W(bits) : 0;
    const u32 opened = in_game_ram(root) ? *(const volatile u16 *)KHDAYS_W(root + 0x8d80) : 0;
    if (opened != last_opened) {
        last_opened = opened;
        fprintf(stderr, "mission: frame %u, chests opened %04x\n", frame, opened);
    }
    if (taken == last_taken) {
        return;
    }
    last_taken = taken;
    fprintf(stderr, "mission: frame %u, pickups of the low band taken %08x\n", frame, taken);
    for (u32 id = 0; id < 32; ++id) {
        if (taken & 0x80000000u >> id) {
            const volatile s32 *at = (const volatile s32 *)KHDAYS_W(stage + 0x2528 + 12 * id);
            fprintf(stderr, "pickup: frame %u, id %u at (%.3f, %.3f, %.3f)\n", frame, id, at[0] / 4096.0,
                    at[1] / 4096.0, at[2] / 4096.0);
        }
    }
}

/* KHDAYS_TRACE_MISSION in the field: the state fields the missions' scripts steer their areas, objective and
 * dark corridor by, when they change. */
extern int GameState_GetField(int field, int bits);

static void trace_mission_fields(u32 frame)
{
    static const struct {
        int field, bits;
    } fields[] = {{8320, 5}, {8326, 1}, {8327, 1}, {8834, 1},  {8835, 1}, {8837, 1}, {8839, 1},
                  {8840, 1}, {8841, 1}, {8842, 1}, {8862, 10}, {8872, 1}, {8419, 1}};
    static int last[sizeof(fields) / sizeof(fields[0])];
    static int known;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const int v = GameState_GetField(fields[i].field, fields[i].bits);
        if (known && v != last[i]) {
            fprintf(stderr, "mission: frame %u, state[%d:%d] %d -> %d\n", frame, fields[i].field, fields[i].bits,
                    last[i], v);
        }
        last[i] = v;
    }
    known = 1;
}

/* KHDAYS_TRACE_MISSION: what a mission puts in the field, to compare with the studio's mission
 * editor: chests and time limit (above) and spawners. The object context *data_ov002_0207fa14: its
 * slots (0x18 bytes, the object first) at +0x44, how many at +0x49; an object's script position at
 * +0x104, its tag at +0x4a; the slot's group at +0x14; its enemies' places (Ov107_Spawner_Run's: 0xc bytes from
 * +0x50, how many at +0x4c, the second byte's bit 1 set while an actor is bound to it). */
static void trace_mission(u32 frame, int in_field)
{
    static int trace = -1;
    static u32 last_ctx = ~0u, last_count = ~0u, last_bound = ~0u;
    u32 ctx, slots, count, bound = 0;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_MISSION") != NULL;
    }
    if (!trace) {
        return;
    }
    if (in_field) {
        trace_mission_fields(frame);
    }
    if (frame % 60 != 0) {
        return;
    }
    if (in_field) {
        trace_chests_and_time(frame);
        trace_pickups(frame);
    }
    ctx = *(const volatile u32 *)KHDAYS_W(0x0207fa14);
    slots = in_game_ram(ctx) ? *(const volatile u32 *)KHDAYS_W(ctx + 0x44) : 0;
    count = in_game_ram(ctx) ? *(const volatile u8 *)KHDAYS_W(ctx + 0x49) : 0;
    for (u32 i = 0; in_game_ram(slots) && i < count; ++i) {
        const u32 object = *(const volatile u32 *)KHDAYS_W(slots + i * 0x18);
        for (s16 p = 0; in_game_ram(object) && p < *(const volatile s16 *)KHDAYS_W(object + 0x4c); ++p) {
            bound = bound * 3 + ((*(const volatile u8 *)KHDAYS_W(object + 0x50 + 0xc * (u32)p + 1) >> 1) & 1) + 1;
        }
    }
    if (ctx == last_ctx && count == last_count && bound == last_bound) {
        return;
    }
    last_ctx = ctx;
    last_count = count;
    last_bound = bound;
    fprintf(stderr, "spawners: frame %u, context %08x, slots %08x, %u of them\n", frame, ctx, slots, count);
    if (!in_game_ram(slots)) {
        return;
    }
    for (u32 i = 0; i < count; ++i) {
        const u32 object = *(const volatile u32 *)KHDAYS_W(slots + i * 0x18);
        const s8 group = *(const volatile s8 *)KHDAYS_W(slots + i * 0x18 + 0x14);
        if (!in_game_ram(object)) {
            continue;
        }
        const volatile s32 *pos = (const volatile s32 *)KHDAYS_W(object + 0x104);
        const s16 places = *(const volatile s16 *)KHDAYS_W(object + 0x4c);
        int with_actor = 0;
        for (s16 p = 0; p < places; ++p) {
            with_actor += (*(const volatile u8 *)KHDAYS_W(object + 0x50 + 0xc * (u32)p + 1) >> 1) & 1;
        }
        fprintf(stderr, "spawner: frame %u, slot %u, group %d, tag %d, at (%.3f, %.3f, %.3f), %d of %d enemies out\n",
                frame, i, group, *(const volatile s16 *)KHDAYS_W(object + 0x4a), pos[0] / 4096.0, pos[1] / 4096.0,
                pos[2] / 4096.0, with_actor, places);
        {
            const u32 task = *(const volatile u32 *)KHDAYS_W(object + 0xf4);
            fprintf(
                stderr,
                "spawner: frame %u, slot %u: task %08x armed %d, timers %d %d, rounds %d, flags %04x, near %d, node %08x\n",
                frame, i, task, in_game_ram(task) ? (int)(*(const volatile u16 *)KHDAYS_W(task + 0xc) & 1) : -1,
                *(const volatile s32 *)KHDAYS_W(object + 0xfc), *(const volatile s32 *)KHDAYS_W(object + 0x100),
                *(const volatile s32 *)KHDAYS_W(object + 0xf8), *(const volatile u16 *)KHDAYS_W(object + 0x48),
                *(const volatile s32 *)KHDAYS_W(object + 0x114), *(const volatile u32 *)KHDAYS_W(object + 0x40));
        }
        const u32 owner = *(const volatile u32 *)KHDAYS_W(object + 4);
        const u32 kinds = in_game_ram(owner) ? *(const volatile u32 *)KHDAYS_W(owner + 4) : 0;
        for (s16 p = 0; in_game_ram(kinds) && p < places; ++p) {
            const u8 key = *(const volatile u8 *)KHDAYS_W(object + 0x50 + 0xc * (u32)p);
            fprintf(stderr, "spawner: frame %u, slot %u, place %d: kind %u, at most %u out, %u out\n", frame, i, p, key,
                    *(const volatile u8 *)KHDAYS_W(*(const volatile u32 *)KHDAYS_W(kinds + 0x9c) + key),
                    *(const volatile u8 *)KHDAYS_W(*(const volatile u32 *)KHDAYS_W(kinds + 0xa0) + key));
        }
    }
}

unsigned int khdays_runtime_scene(void)
{
    /* the scene controller at 0x0204bda8: object, table entry, current id, pending id, argument */
    return *(const volatile u32 *)KHDAYS_W(0x0204bda8 + 8);
}

void khdays_host_frame(void)
{
    /* the scene controller at 0x0204bda8: object, table entry, current id, pending id, argument */
    const volatile u32 *scene = (const volatile u32 *)KHDAYS_W(0x0204bda8);
    static u32 reported_scene = ~0u;
    const u32 frame = khdays_events_vblank_count();
    static u32 logged_scene = ~0u;
    khdays_log_frame(frame);
    if (scene[2] != logged_scene) {
        khdays_log("scene %d -> %u (argument %u)", logged_scene == ~0u ? -1 : (int)logged_scene, scene[2], scene[4]);
        logged_scene = scene[2];
        khdays_characters_scene((int)scene[2]);
        khdays_plugins_scene((int)scene[2]);
    }
    khdays_plugins_frame(); /* the window asked them to stop: on this, the game's thread */
    khdays_scene_trace_frame(frame);
    trace_player(frame);
    trace_heap(frame);
    trace_mission(frame, scene[2] == 2);
    if (frame % 60 == 0) {
        khdays_files_log_reads();
    }
    /* once a second, and at each change of scene: a short one, or one the
     * game spends away from this point (a movie), is seen all the same */
    if (frame % 60 == 0 || scene[2] != reported_scene) {
        reported_scene = scene[2];
        fprintf(stderr, "khdays-native: frame %u, scene %u (object %08x, pending %u arg %u)\n", frame, scene[2],
                scene[0], scene[3], scene[4]);
        fflush(stderr);
    }
}
