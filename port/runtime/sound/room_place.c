/* Where the player is (room_place.h). Layouts from the decompilation: the field's link context
 * (Ov002LinkCtx, data_ov002_0207fa10, overlay 2's BSS: Ov002_StepPeerObjectLoading) holds the player's
 * slot at +0xd and each of its four slots' room at +0x2f, an index into the world's room table at +4
 * (Ov002RosterTable, read from the world's archive mi/wd/wd_<code>); the music tracks' kinds are
 * data_02042124 (48 bytes: 1 a field's, 2 a battle's; SoundMgr_Update). */
#include "room_place.h"
#include "sound_bridge.h"
#include "../core/origin.h"
#include "../core/window.h"
#include "../core/session_log.h"
#include "../rooms/added.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

int khdays_overlay_loaded(int id); /* core/overlays.c */

#define LINK_CTX 0x0207fa10u    /* data_ov002_0207fa10: the field's Ov002LinkCtx * */
#define MUSIC_KINDS 0x02042124u /* data_02042124: each track's kind */
#define MUSIC_KIND_COUNT 48
#define FIELD_OVERLAY 2
#define ROOM_SLOTS 4

static int ram(u32 at, u32 size)
{
    return at != 0 && khdays_game_ram(at) && khdays_game_ram(at + size - 1);
}

static u32 rd32(u32 at)
{
    u32 v;
    memcpy(&v, KHDAYS_W(at), 4);
    return v;
}

int khdays_room_place(char *world, int world_size, int *room)
{
    static const char prefix[] = "mi/wd/wd_";
    const char *file = NULL, *code;
    u32 ctx, table, offset = 0, run = 0;
    signed char slot, index;
    size_t n;
    if (!khdays_overlay_loaded(FIELD_OVERLAY) || !ram(LINK_CTX, 4)) {
        return 0;
    }
    ctx = rd32(LINK_CTX);
    if (!ram(ctx, 0x33)) {
        return 0;
    }
    slot = *(const signed char *)KHDAYS_W(ctx + 0xd);
    if (slot < 0 || slot >= ROOM_SLOTS) {
        return 0;
    }
    index = *(const signed char *)KHDAYS_W(ctx + 0x2f + (u32)slot);
    table = rd32(ctx + 4);
    /* the world: the archive its room table was read from */
    if (index < 0 || !ram(table, 4) || !khdays_origin_of_ram(table, &file, &offset, &run) || file == NULL ||
        strncmp(file, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }
    code = file + sizeof(prefix) - 1;
    n = strcspn(code, "@.#/");
    if (n == 0 || (int)n >= world_size) {
        return 0;
    }
    memcpy(world, code, n);
    world[n] = '\0';
    *room = index;
    return 1;
}

void khdays_room_place_frame(void)
{
    static int present = -1;
    static char said_world[16];
    static int said_room = -2;
    static char folder[520]; /* a room a mod adds: its folder ("" the game's) */
    char world[16] = "";
    int room = -1;
    if (present < 0) {
        present = khdays_sound_bridge_room_music();
        if (present) {
            khdays_origin_want(); /* the world's archive named as it is read */
            khdays_sound_bridge_music_kinds((const unsigned char *)KHDAYS_W(MUSIC_KINDS), MUSIC_KIND_COUNT);
        }
    }
    if (!present) {
        return;
    }
    if (!khdays_room_place(world, (int)sizeof(world), &room)) {
        world[0] = '\0';
        room = -1;
    }
    if (room != said_room || strcmp(world, said_world) != 0) {
        if (room < 0 || !khdays_rooms_folder(world, room, folder, (int)sizeof(folder))) {
            folder[0] = '\0';
        }
        if (room >= 0) {
            khdays_log("sound: the player is in %s room %d%s%s", world, room, folder[0] != '\0' ? ", a mod's: " : "",
                       folder);
        }
        snprintf(said_world, sizeof(said_world), "%s", world);
        said_room = room;
    }
    khdays_sound_bridge_room(world, room, folder);
}
