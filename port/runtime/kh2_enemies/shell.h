/* KINGDOM HEARTS II's enemies in Days's field (docs/KH2_ENEMIES.md, "Days's side: the shell"): a Days enemy made from
 * a custom mission's KH2 row (missions.txt `kh2:<name>/<class>`) stands in for a KH2 enemy -- the waves make it,
 * count it, target it and hit it; its HP, its defeat and its rewards stay Days's -- while KH2's own script drives it:
 * its Days AI held (its tasks given no time, as the Stop magic holds one), its place moved by KH2's motions and moves
 * (the room's collision still Days's), its model KH2's, drawn in its place, its attacks KH2's motions' spheres hitting
 * Days's players as Days's enemies hit them. With the setting kh2_enemies off, without the mod or without HD graphics
 * (the port's own polygons are drawn only then), the stand-in plays itself. Main thread, from the game's joins. */
#ifndef KHDAYS_KH2_ENEMIES_SHELL_H
#define KHDAYS_KH2_ENEMIES_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Ov002_CreateActorFromMarker (runtime/scripts/enemies_link.c): the Days enemy `actor` was made from the mission's
 * enemy table's row `row`; a custom mission's KH2 row makes it a KH2 enemy's shell. */
void khdays_kh2_enemies_made(int row, void *actor);

/* Ov107_Spawner_OnActorRetired / Ov107_AiState_OnDefeat: the enemy gone (its KH2 actor with it; made again if it is
 * set going once more); Ov002_ReleaseResources: the area's all gone. */
void khdays_kh2_enemies_gone(void *actor);
void khdays_kh2_enemies_area_left(void);

/* Ov107_ProcessObjectTick (prepare.py's joins): its start, `delta` the field's step (0x1000 a frame); whether its Days
 * AI is held (its tasks given no time, its updates by distance not skipped). */
void khdays_kh2_enemies_tick(void *actor, int delta);
int khdays_kh2_enemies_held(void *actor);

/* Ov107_AiState_ApplyHit (prepare.py's joins): the hit taken through its class's handler, its HP before it. */
void khdays_kh2_enemies_hit(void *actor, int hp_before);

/* khdays_models_draw_begin (runtime/mods/models.cpp): when `render_obj` is a KH2 enemy's shell's model, its KH2 model
 * is drawn in its place now and 1 is returned (the shell's own shapes left out). */
int khdays_kh2_enemies_draw_begin(const void *resource, const void *render_obj);

/* Whether the KH2 enemy kind `name` can be made now: the mod there, the kind covered (the missions' check for a
 * `kh2:<name>` with no stand-in, registry.h khdays_missions_set_kh2_ready). */
int khdays_kh2_enemies_ready(const char *name);

/* Slot_Spawn (prepare.py's join): whether a positioned sound the game starts (its sequence archive and entry, its
 * place: three fx32) is one of a stand-in's own at a KH2 enemy's place, not to be heard -- KH2's own sounds play. */
int khdays_kh2_enemies_quiet(unsigned archive, unsigned entry, const void *place);

#ifdef __cplusplus
}
#endif

#endif
