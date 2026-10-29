/* The field's enemies as the mods' interface reaches them (api_enemies.c): the game's side, joined to
 * the routines that make, defeat and release them (prepare.py's HOOKS). docs/RUNTIME_MAP.md, "The field's
 * enemies". KHDAYS_TRACE_ENEMIES=1 traces what the joins see. */
#ifndef KHDAYS_SCRIPTS_ENEMIES_LINK_H
#define KHDAYS_SCRIPTS_ENEMIES_LINK_H

typedef struct KhdaysEnemyInfo {
    int kind, hp, max_hp, level;
    float at[3];
} KhdaysEnemyInfo;

/* The kinds this area makes (the rows of the mission's enemy table the area gave a place): up to `room`
 * of them into `out` (NULL to count); how many there are, 0 where the area has no enemies. */
int khdays_enemies_kinds(int *out, int room);

/* A kind's name as the decompilation names its enemy (enemy_names.c); NULL for none. How many kinds the
 * game has (ov107's table of their overlays). */
const char *khdays_enemies_name(int kind);
int khdays_enemies_kind_count(void);

/* Whether an enemy of `kind` can be made here now (NULL), else why not: the area makes it, or its
 * overlay is loaded or still fits among the six places the game keeps for the enemies' overlays
 * (Ov107_SpawnEntityClass, which stops the game past them). */
const char *khdays_enemies_can_make(int kind);

/* One enemy of `kind` made by the game's routine at `at` (the field's units), its level the area's for
 * that kind (the mission's for a kind the area does not make) plus `level`, its HP and attacks times `hp`
 * and `strength`, and set going; its actor, or 0 and why. */
unsigned khdays_enemies_make(int kind, const float at[3], int level, double hp, double strength, const char **why);

/* Sends a standing enemy away as a spawner does when the player leaves it (no defeat, no reward); 0
 * when it no longer stands. */
int khdays_enemies_remove(unsigned actor);

/* A standing enemy's kind, HP, level and place; 0 when it no longer stands. */
int khdays_enemies_info(unsigned actor, KhdaysEnemyInfo *out);

/* Every standing enemy of the area, a mod's or the mission's (ov107's actor manager: those set going and not yet
 * defeated or sent away), for the native plugins' kh.actors: up to `room` actors into `out` (NULL to count), how
 * many; whether `actor` is one of them, its state into `out` when not NULL. */
int khdays_enemies_list(unsigned *out, int room);
int khdays_enemies_state(unsigned actor, KhdaysEnemyInfo *out);

/* A standing enemy hit by a player's actor `attacker` at seat `seat` as the players' attacks hit (the packet
 * Ov022_SendSweepHit gives Ov107_AiState_ApplyHit): `damage` taken as it is, from `at`, with the kinds' bits
 * `kinds` (a status's) at `chance` (0-100), without its flinch when `no_flinch`; whether it took it. A standing enemy
 * moved to `at` (Ov107_MoveNodeAndRelayout); 0 when it no longer stands. */
int khdays_enemies_hit(unsigned enemy, unsigned attacker, int seat, int damage, const float at[3], unsigned kinds,
                       int chance, int no_flinch);
int khdays_enemies_move(unsigned enemy, const float at[3]);

/* The joins. */
void khdays_enemies_marker_made(int source, int row, void *actor, int id); /* Ov002_CreateActorFromMarker */
void khdays_enemies_class(int id);                                         /* Ov107_SpawnEntityClass */
int khdays_enemies_spawner_run(void *spawner, int op);                     /* Ov107_Spawner_Run: 1 to skip it */
void khdays_enemies_retired(void *actor);                                  /* Ov107_Spawner_OnActorRetired */
void khdays_enemies_defeated(void *actor);                                 /* Ov107_AiState_OnDefeat */
void khdays_enemies_area_left(void);                                       /* Ov002_ReleaseResources */

/* While `on`, the mission's own spawners set none of their idle enemies going (their op 0 skipped): the
 * mission's areas then never end by their enemies. Those standing stay. */
void khdays_enemies_hold_mission(int on);

/* The frame point (scripts.c): the enemies sent away settled, the field left. */
void khdays_enemies_frame(void);

#endif
