/* The game's side of the native plugins' actors and characters (kh.actors, kh.characters: actors.c, characters.c):
 * what the port's joins tell them. */
#ifndef KHDAYS_PLUGINS_ACTORS_H
#define KHDAYS_PLUGINS_ACTORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* An enemy defeated, retired or settled once sent away (enemies_link.c): its handles end, before the game makes
 * the same actor stand again. */
void khdays_actors_ended(unsigned actor);

/* Every actor's handles end: an area's end (Ov002_ReleaseResources), a change of scene. */
void khdays_actors_area_left(void);

/* Seat `seat`'s actor `actor` starts or goes on with attack `attack` (Ov022_RunComboContacts's join, each frame of
 * it: runtime/characters/characters.c). */
void khdays_plugins_character_attack(unsigned actor, int seat, int attack);

/* The game's scene changed to `scene` (runtime.c): the actors' handles end, the characters' actors leave. */
void khdays_plugins_scene(int scene);

#ifdef __cplusplus
}
#endif

#endif
