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
 * before that, the damage Days worked out for it (`amount`), its kinds (the hit packet's high flags: Days's elements, a
 * bit each) kept for the hit and its element's share taken (1:311), kept from taking it under the least health its
 * script set (1:231): the amount to take. */
void khdays_kh2_enemies_hit(void *actor, int hp_before);
int khdays_kh2_enemies_damage(void *actor, int amount, int kinds);

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

/* Ov022_TranslateCommandToAction (prepare.py's join): whether a KH2 enemy's script took the players' control away
 * (1:42 of type 0): every player locked as Days's own lock locks them. */
int khdays_kh2_enemies_control_off(void);

/* Ov002_CollectNearbySpots (prepare.py's join): whether a KH2 enemy's prize drain (1:187) takes every prize on the
 * field however far, this frame. */
int khdays_kh2_enemies_prize_drain(void);

/* ui/reaction_command.cpp: whether an enemy's own pictures draw the player's command menu (1:359: Zexion's book in his
 * Lexicon) -- its .a.us's path (UTF-8) into `archive`, holding the IMGD and SEQD named "comd", and its HD picture's
 * mod path without the extension (kh2/fx/<name>/-<n>: numbered after its effects' pages) into `picture`; 0 none. */
int khdays_kh2_enemies_command_image(char *archive, int archive_room, char *picture, int picture_room);

/* Ov022_UpdateCommandInput (prepare.py's joins): whether this A press on the deck chooses a command of the menu a KH2
 * enemy's script gives a free player (1:237: Zexion's Lexicon's pages; the runtime sends it), Days's own deck slot then
 * not used; and at the command's commit, that press's command made none (kind 7, argument 0: 1 returned). */
int khdays_kh2_enemies_menu_takes_a(int pressed);
int khdays_kh2_enemies_menu_commit(signed char *kind, unsigned short *arg);

/* ui/reaction_command.cpp, drawing that menu as KH2's own (four slots, an enemy's own pictures over the deck): how many
 * slots it has (0 none shown) and its own cursor -- a slot on or back as Days's deck steps a row, A then choosing the
 * slot it is on --, asked each VBlank it is drawn; a slot's command's name (UTF-16, how many letters, 0 none). */
int khdays_kh2_enemies_menu_slots(int *cursor);
int khdays_kh2_enemies_menu_slot(int slot, unsigned short *out, int room);

/* Days's lock-on mark's point for the enemy `actor` locked on (Ov022_UpdateSelectionMarker's): a KH2 enemy's aim point
 * (its 0x307 shape: the Soldier's chest, Cerberus's heads) in the game's memory, else `point` as it was. DS addresses. */
unsigned khdays_kh2_enemies_lock_point(unsigned actor, unsigned point);

/* khdays_models_draw_begin (runtime/mods/models.cpp): when `render_obj` is a KH2 enemy's shell's model, its KH2 model
 * is drawn in its place now and 1 is returned (the shell's own shapes left out). */
int khdays_kh2_enemies_draw_begin(const void *resource, const void *render_obj);

/* Ov002_BuildPanelLabels (prepare.py's join): the field's label of enemy class `enemy_class` (one for each class the
 * mission has; the targeted enemy's shown) -- KH2's name in the game's language when every row of the class in the
 * mission stands in for one KH2 enemy: its UTF-16 letters into `out` (at most `room`, then a 0), how many; 0 for any
 * other class (Days's own name drawn). */
int khdays_kh2_enemies_label(int enemy_class, unsigned short *out, int room);

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

/* KH2's lights a KH2 enemy's script has on (a boss darkening the field: Xaldin, Demyx, the Groundshaker's red): what
 * the room's colours (runtime/mods/models.cpp, a mission world's room) and the other models' are multiplied by, 1 each
 * as they are (up to 2); 0 when no light changes either. A light ends with the area, or when no KH2 enemy is left to
 * fade it. */
int khdays_kh2_enemies_light(float room[3], float objects[3]);

/* Whether a KH2 enemy's script asked KH2's camera to its place at once (1:193, ObjCamera::setWarp: Xaldin's Jump)
 * since the last asking: runtime/camera/kh2_camera_hooks.c's next settle takes no easing. 1 once. */
int khdays_kh2_enemies_camera_warp(void);

#ifdef __cplusplus
}

#include <cstddef>
#include <string>
#include <vector>

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

// A timer or counter of the field's KH2 mission (its header's operation 4 or 5: the PS3 build's MISSION2D::TABLE,
// {its start, its most, its warning, index << 24 | align << 16 | its picture's BAR entry + 1}) as KH2 keeps it
struct MissionPiece final {
    int kind = 0;           // 4 a timer (dk::Timer, dk::TIMER), 5 a counter (MISSION_COUNT, dk::COUNTER), 6 a gauge
                            // (MISSION_GAUGE, dk::MISSION_GAUGE)
    int index = 0;          // its number among its kind (4:5's first argument)
    int entry = -1;         // its picture's BAR entry (an IMGD; its sequence the next entry), -1 none shown
    int align = 0;          // KH2's INFO_BASE::ALIGN: 0 left, 1 right, 2 the middle
    int value = 0;          // a timer's time (KH2's frames, 60 a second), a counter's count, a gauge's level
    float level = 0.0F;     // a gauge's level (MISSION_GAUGE's + 0x2c, a float)
    int most = 0;           // a counter's most; a timer's end (its frames: counting down, its start's; up, its most's)
    int warning = 0;        // its warning (MISSION2D's: seconds for a timer)
    bool with_most = false; // a counter shown "count / most" (its most's bit 30: MISSION_COUNT's type 1)
    bool down = false;      // a timer counting down (its start above 0)
};
// The field's KH2 mission as its pieces show it (ui/kh2_mission_2d.cpp): its file (the mod's kh2/msn/<name>.bar), a
// number another for each mission set going, the KH2 ticks it has run (60 a second), whether its pieces are up (4:14,
// 4:15; down at its end), its timers and counters in its header's order. False without one
struct MissionView final {
    std::string file;
    unsigned serial = 0, ticks = 0;
    bool shown = false;
    std::vector<MissionPiece> pieces;
};
bool mission_view(MissionView &out);
} // namespace khdays::kh2_enemies
#endif

#endif
