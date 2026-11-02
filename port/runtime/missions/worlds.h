/* The worlds a new mission is played in: the game's (its world table, data_ov002_0207f0a4: twelve places,
 * ten worlds shipped). Plain C: no game, no system calls. */
#ifndef KHDAYS_MISSIONS_WORLDS_H
#define KHDAYS_MISSIONS_WORLDS_H

#define KHDAYS_GAME_WORLDS 12

/* A game world's index by its two letters ("tt" 0 ... "pi" 11); -1 for none or one the game does not ship
 * (hb, pi: no mi/wd/wd_<code>). */
int khdays_world_index(const char *code);

/* The two letters of game world `index` (0-11), NULL past them. */
const char *khdays_world_code(int index);

/* Whether `code` is one of the game's twelve worlds' codes, shipped or not (none a mod's world may take). */
int khdays_world_code_taken(const char *code);

/* Game world `index`'s chain of rooms' maps: its member of UI/btl/<language>/map.p2 (the game's table); -1 none. */
int khdays_world_map_chain(int index);

#endif
