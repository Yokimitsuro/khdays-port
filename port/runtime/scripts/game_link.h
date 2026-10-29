/* The game's side of the mods' interface: every name the interface gives reads, changes or calls the
 * game through these, by the decompilation's names (game_link.c). The tests give their own. */
#ifndef KHDAYS_SCRIPTS_GAME_LINK_H
#define KHDAYS_SCRIPTS_GAME_LINK_H

#include <stddef.h>

#define KHDAYS_LINK_ITEMS 0x8d0  /* gGameState->aItemCount's entries */
#define KHDAYS_LINK_FLAGS 0x4000 /* flags from *gGameState + 0x10 up to aItemCount at +0x810 */
#define KHDAYS_LINK_MUNNY_MAX 999999
#define KHDAYS_LINK_GIVEN_UP 0x2087 /* set by Ov002_RetireSubFlow on giving up (runtime/menus/retry.c) */

enum KhdaysLinkMenu { KHDAYS_LINK_PAUSE, KHDAYS_LINK_CAMP };

/* Where the mods are and the player's saves folder (their data, the mods' log). */
void khdays_link_folders(const char **mods, const char **user);

/* The game's frame count (the object list's), and whether it is paused (gPauseMode). */
unsigned khdays_link_frame(void);
int khdays_link_paused(void);

/* The current scene (gSceneCtl) and its argument. */
unsigned khdays_link_scene(unsigned *argument);

/* Where the game is, for the names of version 2 that act only there (api_v2.h): the field (scene 2 with
 * the field's and the battle's overlays), a camp (Mission Mode's, scene 19 with ov008; the story's,
 * scene 5 with ov025), the title (scene 1). */
enum KhdaysWhere { KHDAYS_WHERE_ELSEWHERE, KHDAYS_WHERE_FIELD, KHDAYS_WHERE_CAMP, KHDAYS_WHERE_TITLE };
int khdays_link_where(void);

/* The game's state (gGameState's reference), 0 while there is none: the state names and the item
 * actions need it. */
unsigned khdays_link_state(void);

int khdays_link_munny(void);
void khdays_link_set_munny(int value);

/* The item counts (KHDAYS_LINK_ITEMS bytes into `counts`); 0 without the game's state. */
int khdays_link_items(unsigned char *counts);
/* An item's count set, with its owned flag when above 0 (Ov008_AddItemCount's rule). */
void khdays_link_set_item(int id, int count);

int khdays_link_flag(int n);
void khdays_link_set_flag(int n, int on);

/* The current mission (data_0204c23c) while the field is the scene; -1 otherwise. */
int khdays_link_mission(void);

/* The local player's battle actor is GetEntryField20ByIndex(Session_GetLocalPlayerIndex()): the field
 * players' table (*(data_ov022_020b2e78 + 4), 0xc bytes an entry, +4 a record whose +0x20 is the actor),
 * read with the battle (ov022) loaded. Its position is at +0x48c (20.12 fixed point), its HP at +0x12
 * and maximum at +0x16 (Ov022_ActorSetHp's entity). Each returns 0 without the actor. */
int khdays_link_position(float at[3]);
int khdays_link_player_hp(int *hp, int *max);
/* The local player put at `at` (the field's units) by the game's own routine for it
 * (func_ov022_02088218: its actor moved with its child, its position +0x48c, its motion reset after the
 * move); NULL when done, else why not. */
const char *khdays_link_set_position(const float at[3]);
/* The game's low-HP predicate, including ability 0x42's exception. */
int khdays_link_player_low_hp(void);
/* Live HUD order, including mission mode: sheet icon and the actor belonging to a displayed slot. */
int khdays_link_hud_portrait(int slot, int *icon, int *hp, int *maximum, int *low_hp);
/* The vanilla limit threshold in HP, including ability, use count and battle settings; 0 disabled. */
int khdays_link_player_limit_hp(void);
/* The native LIMIT label's armed state, independent of its flashing tile artwork. */
int khdays_link_limit_available(void);
/* Active lower-panel target resource; negative native selection means the ordinary world panel. */
int khdays_link_mission_target(int *entry, int *primary);
/* Original limit clock and entry HP, before the DS HUD rounds its position to pixels. */
struct KhdaysLinkLimitClock {
    int elapsed, outward, start_hp, step;
};
int khdays_link_player_limit_clock(struct KhdaysLinkLimitClock *clock);
/* Through Ov022_ActorSetHp, as the game's own hits and cures do; NULL when done, else why not ("no player", or
 * "not now" while the field loads: its panel, whose gauge the routine sets, is not there yet). */
const char *khdays_link_set_player_hp(int hp);

/* The same for any seat's player (the native plugins' kh.actors): seat `seat`'s battle actor (one of the game's four
 * seats; 0 without one), the seat this copy plays; a player actor's place, its character (+0xc, the base's for a
 * mod's character); seat `seat` placed; a player actor's HP set. */
unsigned khdays_link_seat_actor(int seat);
int khdays_link_local_seat(void);
void khdays_link_actor_position(unsigned actor, float at[3]);
int khdays_link_actor_kind(unsigned actor);
const char *khdays_link_place_seat(int seat, const float at[3]);
const char *khdays_link_set_actor_hp(unsigned actor, int hp);
/* The damage of a player actor's hit of factor `scale` (fx32) as its combo's hits get it (Ov022_ComputeStrengthDamage:
 * its strength, bonuses and abilities); -1 without the battle. */
int khdays_link_strength_damage(unsigned actor, int scale);

/* An entity Ov022_ActorSetHp was given: its HP and maximum (nHp +0x12, nHpMax +0x16). */
void khdays_link_entity_hp(const void *entity, int *hp, int *max);

/* Whether that entity is the local player's actor. */
int khdays_link_is_player(const void *entity);

/* The hit being dealt (Ov022_ReceiveHit's join): its source, NULL once dealt. Its attacker: the enemy's
 * actor into `*actor` and its kind returned; -1 when the hit is no enemy's or none is being dealt. */
void khdays_link_hit_from(void *source);
int khdays_link_attacker(unsigned *actor);

/* The game's language (1-5, the firmware's: English .. Spanish): the one just chosen, else the game's. */
int khdays_link_language(void);

/* Whether the title's menu waits for the player (its scene object's update Ov000_TickMenuLoop). */
int khdays_link_title_idle(void);

/* Whether the game over (ov027) is up. */
int khdays_link_game_over(void);

/* Whether the results being recorded record the mission cleared (Ov005_CommitMissionResults' rule). */
int khdays_link_results_cleared(void);

/* The mission ended as the pause's Give Up does (PauseMenu_ConfirmFrame's yes: Ov002_PauseMissionScene and
 * game field 0x20ef), on to the results and the camp; NULL when done, else why not. */
const char *khdays_link_leave_mission(void);

/* Mission file `number` (mi/mi/<number>) started from Mission Mode's camp, by the camp's own presses
 * (runtime/menus/mission_start.h); NULL when on its way, else why not. */
const char *khdays_link_start_mission(int number);

/* The game's actions, each through its own routine; NULL when done, else the reason it was not. */

/* PlaySound(archive, entry), the sequence archive's entry found by its names in the sound archive. */
const char *khdays_link_play_sound(const char *sequence_archive, const char *entry);

/* Ov002_ShowNoticeText with `text` (`length` UTF-16 units) in a block of the game's heap: the field's
 * notice, of which the game takes the first only ("busy" after it). */
const char *khdays_link_show_text(const unsigned short *text, int length);

/* Whether a menu is open: the pause, the camp. */
int khdays_link_menu_open(int menu);

/* A guest in a multiplayer session: the host alone changes the game. */
int khdays_link_guest(void);

/* The game's memory by its references (docs/SCRIPTING.md, "The game's memory"): `size` bytes from
 * `address`, all of them in main RAM, the headers' heaps, the data area or the heaps' region, else 0
 * and nothing read or written. A heaps' granule the game never touched reads as 0 and is given no
 * memory by a read; a write gives it memory, as the game's own first touch does. */
int khdays_link_reachable(unsigned address, unsigned long long size);
int khdays_link_read(unsigned address, void *out, size_t size);
int khdays_link_write(unsigned address, const void *in, size_t size);

/* Whether overlay `id` is loaded and running (its data then in main RAM). */
int khdays_link_overlay_loaded(int id);

#endif
