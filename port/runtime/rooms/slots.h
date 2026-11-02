/* Which of the field's four loaded slots holds each room of the world (-1: none), kept by the port instead of in the
 * game's 24 bytes at +0x17 of the ov002 link context: a world of more rooms than 24 (rooms a mod adds) would write past
 * them (Ov002_LoadPeerIntoSlot's aSlotMappings[nPeer]). Joins in port/tools/prepare.py route the game's five uses here
 * (docs/RUNTIME_MAP.md, "A world's rooms"). */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The slot room `room` is loaded in, -1 when none (or past the rooms known). */
int khdays_room_slot(int room);
/* Room `room` loaded in `slot` (-1: no longer); the table grows to hold it. */
void khdays_room_slot_set(int room, int slot);
/* Every room in none (the game's fills with 0xff). */
void khdays_room_slots_clear(void);

#ifdef __cplusplus
}
#endif
