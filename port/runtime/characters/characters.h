/* The mods' characters in the running game: read from each mod's characters/ at the start (registry.h), given
 * numbers past the game's, their data folders ba/ch/<code>/ served (the mod's files, else copies of their base's),
 * and answered where the game's joins ask (port/tools/prepare.py): a new number is its base's where the game picks
 * behaviour or one of its tables, its own where it opens data. With no such mod nothing of the game changes.
 * docs/RUNTIME_MAP.md, "Characters of mods". */
#ifndef KHDAYS_CHARACTERS_H
#define KHDAYS_CHARACTERS_H

#include "registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* After the ROM is assembled, before the game starts: the characters read, checked, numbered, their folders
 * added. */
void khdays_characters_start(void);

/* Once the session log is open: what the start found, told there and in the mods' log. */
void khdays_characters_report(void);

/* The game's joins: whether `number` (a place's character in the select) is a new character's; the new
 * character of seat `slot` (0-3) when it has one and `kind` is its base (or below 0), else `kind`; the game
 * character a number plays as (a game number is its own). */
int khdays_characters_known(int number);
int khdays_characters_of(int slot, int kind);
int khdays_characters_base(int number);

/* The select wrote seat `seat` from the place on character `place` (-1: emptied): a new character's seat
 * gets its base's number in the game, its own kept here. */
void khdays_characters_seat(int seat, int place);

/* The camp's place made from seat `seat` (its select's own number `entry`, from the seat's base): the
 * seat's new character's number when it has one, else `entry`. */
int khdays_characters_place(int seat, int entry);

/* The game's scene now `scene` (at each change): the title forgets the seats (the story never has them). */
void khdays_characters_scene(int scene);

/* The select's place (its own numbers, 0-18) a character is shown as by the game's select screens: its base's
 * for a new one, else `id` itself. */
int khdays_characters_menu_entry(int id);

/* The mod's character seat `seat` (0-3) was given by the select, NULL for one of the game's (forms.h: its form). */
const KhdaysCharacter *khdays_characters_seat_character(int seat);

/* The numbered characters, in their numbers' order (the select's). */
int khdays_characters_count(void);
const KhdaysCharacter *khdays_characters_at(int index);
const KhdaysCharacter *khdays_characters_numbered(int number);

/* A file of the character's folder (`relative`: its description's reference) on disk, UTF-8, into `path`. */
int khdays_characters_file(const KhdaysCharacter *c, const char *relative, char *path, int size);

/* For the models (runtime/mods/models.cpp): the character whose own folder holds the game model `name`, NULL
 * for any other. */
const KhdaysCharacter *khdays_characters_of_model(const char *name);

/* For the multiplayer rooms (runtime/scripts/scripts.c): numbered character `index` (from 0; 0 past the last), in
 * the numbers' order -- its part of the gameplay mods' fingerprint (SHA-256 over its identity, its description and
 * its data/ files: what plays; its model, clips and sounds are each player's own), its name and its identity. */
int khdays_characters_room_entry(int index, unsigned char digest[32], const char **name, const char **identity);

/* The damage factor (fx32) of a move of seat `seat`'s actor, which plays as `kind`, that its base's own code makes
 * (Ov022_ScaleRowValues's join: the player overlays' Limits and specials): a new character's [limit] damage
 * times `factor`, else `factor`. */
int khdays_characters_own_moves(int seat, int kind, int factor);

/* Seat `seat`'s actor `actor`, which plays as `kind`, hits with attack `attack` (its record's number in cm.b:
 * Ov022_RunComboContacts's join, each frame of it): told to the native plugins listening to its character, and
 * under KHDAYS_TRACE_CHARACTERS when it changes, for a new character's actor, so that its author can name the
 * attacks in [moveset]. */
void khdays_characters_attack(const void *actor, int seat, int kind, int attack);

/* The game starts the sounds of seat `seat`'s actor, which plays as `kind` (Ov022_PlayEntityVoice's join), until
 * it is called again with -1. */
void khdays_characters_speaker(int seat, int kind);

/* For the sounds (runtime/sound/sound_mods.c): what the new character whose actor starts the game's sound `game`
 * now makes of it -- 0 nothing (the game's plays, or a sound mod's), 1 its own file (`path`, `name` what the
 * traces call it), -1 silence. */
int khdays_characters_speaker_sound(const char *game, char *path, int size, char *name, int name_size);

/* For the sounds: whether any character has [sounds]; its files one by one (`index` from 0, 0 past the last; an
 * empty `path` for one that cannot be named), as khdays_characters_speaker_sound gives them. */
int khdays_characters_have_sounds(void);
int khdays_characters_sound_file(int index, char *path, int size, char *name, int name_size);

/* A native plugin's clip (kh.characters' play, runtime/plugins/characters.c): numbered character `number` (one with
 * its own skeleton) drawn playing its [actions] clip `action` from vblank `now`, looped or once; 0 when it has no
 * own skeleton or no such action. Its clip stopped. For the models: the clip played on `c`, NULL none, from when and
 * whether looped. */
int khdays_characters_play(int number, const char *action, int loop, unsigned now);
void khdays_characters_play_none(int number);
const KhdaysCharacterAction *khdays_characters_played(const KhdaysCharacter *c, unsigned *since, int *loop);

/* For the models (runtime/mods/models.cpp): the model file drawn for the game model `name` ("ba/ch/c16/def.p.z/
 * mdl/roxas_b", or one of its weapons, "ba/ch/c100/w_.p2#0/mdl/ro_w13600", by its [weapons]), a model of a
 * character's own folder, into `path`; 0 for any other. */
int khdays_characters_model(const char *name, char *path, int size);

#ifdef __cplusplus
}
#endif

#endif
