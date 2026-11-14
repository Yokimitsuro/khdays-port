/* Where the player is, for the music a mod gives a room (sound_mods.h, "rooms/"): the world and the room of
 * the field's link context, told to the sound each VBlank. */
#ifndef KHDAYS_ROOM_PLACE_H
#define KHDAYS_ROOM_PLACE_H

/* The game's thread, each VBlank: the player's world and room (none outside the field), and the kinds of the
 * game's music tracks, to the sound (khdays_sound_bridge_room). Nothing without a mod's sounds/rooms. */
void khdays_room_place_frame(void);

/* The player's world (its code, `mi/wd/wd_<code>`) and room (its index in the world's room table, as the
 * studio numbers them): 0 when not known (not in the field). */
int khdays_room_place(char *world, int world_size, int *room);

#endif
