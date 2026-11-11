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

/* Ov107_AiState_ApplyHit (prepare.py's joins): the hit taken through its class's handler, its HP before it; and
 * before that, the damage Days worked out for it (`amount`) kept from taking it under the least health its script
 * set (1:231): the amount to take. */
void khdays_kh2_enemies_hit(void *actor, int hp_before);
int khdays_kh2_enemies_damage(void *actor, int amount);

/* Ov107_AiState_ApplyHit and Ov107_AiState_HitShapeOverlap (prepare.py's joins): whether a KH2 enemy's script keeps it
 * from being hit (YS::OBJ's FLAG_NOHIT_ATTACK) and from touching objects -- no push, no touch (FLAG_NOHIT_OBJ): the Mole
 * Driller under the ground (1:70 its flags 1 and 3, 1:71 back). 0 for any other object. */
int khdays_kh2_enemies_untouched(void *actor);
int khdays_kh2_enemies_no_contact(void *actor);

/* Ov107_ProcessObjectTick (prepare.py's join): a KH2 player's blow on it to land now as one hit (the last of
 * Cerberus's reaction command), its damage in `amount`; 0 none. */
int khdays_kh2_enemies_blow(void *actor, int *amount);

/* Ov022_ActorUpdate (prepare.py's join): whether a KH2 enemy holds the player's actor `actor` (its Days control and
 * commands stand still), put where the enemy's script and motion have it. */
int khdays_kh2_enemies_holding(void *actor);

/* Days's lock-on mark's point for the enemy `actor` locked on (Ov022_UpdateSelectionMarker's): a KH2 enemy's aim point
 * (its 0x307 shape: the Soldier's chest, Cerberus's heads) in the game's memory, else `point` as it was. DS addresses. */
unsigned khdays_kh2_enemies_lock_point(unsigned actor, unsigned point);

/* khdays_models_draw_begin (runtime/mods/models.cpp): when `render_obj` is a KH2 enemy's shell's model, its KH2 model
 * is drawn in its place now and 1 is returned (the shell's own shapes left out). */
int khdays_kh2_enemies_draw_begin(const void *resource, const void *render_obj);

/* Whether the KH2 enemy kind `name` can be made now: the mod there, the kind covered (the missions' check for a
 * `kh2:<name>` with no stand-in, registry.h khdays_missions_set_kh2_ready). */
int khdays_kh2_enemies_ready(const char *name);

/* Slot_Spawn (prepare.py's join): whether a positioned sound the game starts (its sequence archive and entry, its
 * place: three fx32) is one of a stand-in's own at a KH2 enemy's place, not to be heard -- KH2's own sounds play. */
int khdays_kh2_enemies_quiet(unsigned archive, unsigned entry, const void *place);

/* Ov107_Spawner_FindFreeActor (prepare.py's join): whether `actor` (a DS address) may serve slot `slot` of `spawner`:
 * made for a row with the same KH2 enemy as the slot's row, or none for both. Rows of one stand-in class share the
 * spawners' kinds entry, so Days alone would take any of the class's actors (docs/RUNTIME_MAP.md section 37). */
int khdays_kh2_enemies_fits(unsigned spawner, int slot, unsigned actor);

/* The camera of a KH2 enemy's event sequence playing for this game's player (runtime/camera/kh2_camera_hooks.c): its
 * eye and look-at in Days's world, half its vertical field of view (0: the camera's own kept); 0 when none shows. */
int khdays_kh2_enemies_event_camera(float eye[3], float at[3], float *fov_half);

/* The screen's trail such a sequence has on (runtime/display/display.c, into the frame package's `trail`): its alpha,
 * its quad's width and height over the view's, its turn in radians; 0 when none. */
int khdays_kh2_enemies_screen_trail(float out[4]);

#ifdef __cplusplus
}

#include <cstddef>

namespace khdays::assets::kh2 {
class Motion;
struct MotionSkeleton;
} // namespace khdays::assets::kh2

namespace khdays::kh2_enemies {
// A KH2 enemy's motion one of Days's players plays while the enemy holds it (its reaction command), for the player's
// KH2 model (runtime/mods): the motion made for KH2's player's bones (`skeleton`, P_EX100's), its weapon's, its time
// (KH2's ticks); its root's travel is in the player's place. `started` tells one motion from the next (each 1:29 a new
// number), and `share` is how much of the pose is this motion's while 1:29's blend runs (from the pose before; 1 none)
struct HeldMotion final {
    const khdays::assets::kh2::Motion *motion = nullptr, *weapon = nullptr;
    const khdays::assets::kh2::MotionSkeleton *skeleton = nullptr;
    float ticks = 0.0F;
    unsigned started = 0;
    float share = 1.0F;
};
// The player whose model's scene node is `node` (a DS address): its motion now, false when no KH2 enemy holds it
bool held_motion(unsigned node, HeldMotion &out);
// The weapon of the player whose model's scene node is `node` as his KH2 model draws it while an enemy holds him: its
// `count` bones (W_EX010's, 16 floats each, column-major) in his motion's space (KH2's cm, y up, his root's travel taken
// out), this frame. His weapon's effects (its swing's trail) follow them; without them KH2's own keyblade on his hand's
// bone (W_EX010 on P_EX100's 178)
void held_weapon_drawn(unsigned node, const float *bones, std::size_t count);
} // namespace khdays::kh2_enemies
#endif

#endif
