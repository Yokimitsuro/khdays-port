// A model's secondary-motion configuration: `<model>.secondary.txt` beside the model, versioned,
// line by line (docs/SECONDARY_MOTION.md has the format, units, defaults and precedence). Read and
// validated here into neutral form; bone names are resolved later, against the model (rig.h).
#pragma once

#include "vecmath.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace khdays::secondary {

constexpr int kFormatVersion = 1;

// Every tunable number of a group, a bone or an animation control. Lengths are kept in metres and
// angles in radians once read; the file gives lengths in its `units` and angles in degrees.
enum class Param : int {
    mass,               // kg a control
    stretch_compliance, // m/N: structural edges (0: inextensible)
    shear_compliance,   // m/N: across a face's diagonals
    bend_compliance,    // m/N: two edges apart, along strips and around rows
    damping,            // 1/s: velocity relative to the target lost (exp(-damping * dt))
    follow_frequency,   // Hz: the pull toward the target shape (0: none)
    follow_damping,     // ratio of critical damping of that pull
    gravity_scale,      // of the global gravity
    max_distance,       // m from the target a control may stray (0: no limit)
    tether_slack,       // fraction: a control's reach past its rest distance from its anchor
    swing_limit,        // rad: from the target direction about the bone's head (pi: none)
    twist_limit,        // rad: about its own axis, from the target's (pi: none)
    inertia,            // 0..1: how much of the attachment's acceleration it feels
    guided_inertia,     // 0..1: the same in guided mode (the authored motion carries its own sway)
    max_speed,          // m/s relative to the target
    thickness,          // m: collision radius of controls, edges and faces
    friction,           // 0..1
    wind_drag,          // 1/s: how quickly it takes up the air's velocity
    wind_lift,          // 0..1: of the air's push across a surface's normal kept (surfaces)
    row_compliance,     // m/N: links around a surface's rows, between strips (a skirt's flare)
    max_strain,         // fraction a strip's links (and anchors) may stretch under contact
    row_max_strain,     // fraction row and shear links may stretch (KH2's coats flare their rows)
    smoothing,          // s: the drawn shape eased toward the solved one (in its reference bone's space)
    follow_bone_scale,  // 0..1: of its follow_bone shares it takes (how much the legs carry it)
    drape,              // 0 or 1: its target swung out of the body colliders down its strip (Simulation::drape)
    draw_guard,         // 0 or 1: a smoothed drawn face cutting into the body moved out (guard_drawn_faces)
    face_friction,      // 0..1: friction of a face resting on a body collider (0: it slides freely)
    rotate_only,        // 0 or 1: a rebuilt bone only turns toward its point, its head kept where its parent puts it
    no_sim,             // 0..1: of the way onto its target each tick (its bones and the legs' carry), unsimulated
    game_shape,         // 0..1: its target taken that far from the replaced game model's own surface (its coat)
    cutscene_sim,       // 0 or 1: simulated about the game model's coat (its target there), never inside the shell
                        // those targets make; only its group's colliders touch it (no room, not itself)
    leg_carry,          // 0..1: the coat's back below its hips on the game model carried that much by both legs
    count
};
constexpr int kParamCount = static_cast<int>(Param::count);

enum class Unit { none, length, angle };
struct ParamInfo {
    const char *name;
    float fallback; // the default, in internal units
    float lo, hi;   // valid range, internal units
    Unit unit;
};
const ParamInfo &param_info(Param p);
bool param_named(const std::string &name, Param &out);

// A set of parameters, each given or not. `hem` values (surfaces, chains) are reached at the free
// end: a control's value runs from the plain one at its anchor to the hem's by its depth.
struct ParamSet {
    std::array<float, kParamCount> value{};
    std::array<float, kParamCount> hem{};
    std::array<float, kParamCount> curve{}; // the hem value's ease: depth^curve (0: 1, linear)
    std::bitset<kParamCount> given, hem_given;
    // `over`'s given values replace these
    void merge(const ParamSet &over);
    float at(Param p, float depth) const;
};
ParamSet default_params();

enum class GroupKind { surface, chain, hair };
enum class Mode { simulated, guided, authored };
enum class AuthoredPolicy { exact, collision_corrected };

// A bone named in the file: a joint name, `path:a/b/c` (each a child of the one before), or for KH2
// models a bare number (kh2_<n>); `line` for messages.
struct BoneRef {
    std::string text;
    int line = 0;
};

enum class WindSpace { world, actor, bone };
struct Wind {
    bool given = false;
    WindSpace space = WindSpace::world;
    BoneRef bone;
    Vec3 direction{1.0F, 0.0F, 0.0F};
    float speed = 0.0F;      // m/s
    float gust = 0.0F;       // fraction of speed
    float frequency = 0.25F; // Hz of the gusts
    float turbulence = 0.0F; // fraction of speed, faster and across the direction
};

struct Optional {
    bool given = false;
};
struct ModeSetting : Optional {
    Mode mode = Mode::simulated;
};
struct PolicySetting : Optional {
    AuthoredPolicy policy = AuthoredPolicy::exact;
};

struct PresetConfig {
    std::string name, extends;
    ParamSet params;
    ModeSetting mode;
    PolicySetting policy;
    std::vector<std::string> collide; // collider tags
    bool collide_given = false;
    std::string self_collision;
    bool self_given = false;
    int line = 0;
    std::string file;
};

// A body bone a group's reference shape also rides, near it: thighs under a long coat.
struct FollowBone {
    BoneRef bone;
    float radius = 0.2F; // m: controls within this of the bone follow it, fully at the bone
    float weight = 0.5F; // at most, once the bone has come up into the control
    float always = 0.0F; // in every pose, by the control's rest nearness: the leg carries the coat's shape
};

struct BoneOverride {
    BoneRef bone;
    ParamSet params;
    ModeSetting mode;
};

struct GroupConfig {
    std::string name;
    GroupKind kind = GroupKind::chain;
    std::string preset;
    ParamSet params;
    ModeSetting mode;
    PolicySetting policy;
    std::vector<std::string> collide; // collider tags; none given: every collider
    bool collide_given = false;
    std::string self_collision; // groups sharing this name collide with each other (and themselves)
    bool self_given = false;
    // `shell_push <tag>... [from <0-1>]`: `cutscene_sim`'s shell pushed out of these colliders (from that far down
    // each, its start left out), never the cloth itself
    std::vector<std::string> shell_push;
    float shell_push_from = 0.0F;
    std::vector<BoneRef> attach;              // body bones the group hangs from (its reference frame first)
    std::vector<FollowBone> follow_bones;     // body bones its reference shape partly rides
    std::vector<std::vector<BoneRef>> strips; // surfaces: control sequences anchor-to-hem; chains: chains
    std::vector<std::vector<BoneRef>> rings;  // controls joined around (a cuff's circumference)
    bool mesh_topology = false;               // surfaces: neighbours from the triangles skinned to them
    std::vector<std::pair<BoneRef, BoneRef>> connect, disconnect;
    std::vector<BoneRef> exclude;
    std::vector<std::pair<BoneRef, Vec3>> offsets; // explicit control points, bone-local, metres
    std::vector<BoneOverride> overrides;
    bool settle = false;               // prepare a settled reference shape under gravity at load
    Vec3 hang{0.0F, -1.0F, 0.0F};      // startup hanging direction, model space at rest
    bool hang_given = false;           // chains: hang straight that way before settling
    int settle_steps = 240;            // at the simulation rate
    bool twist_from_neighbours = true; // surfaces: a control's twist from its row neighbours
    Wind wind;
    int line = 0;
};

enum class ShapeKind { sphere, capsule };
struct ColliderConfig {
    std::string name;
    ShapeKind kind = ShapeKind::sphere;
    BoneRef bone, end_bone; // a capsule: from `bone`'s point to `end_bone`'s (or `length` along x)
    bool end_given = false;
    Vec3 offset{}, end_offset{};
    float length = 0.0F;                  // metres along bone x when there is no end bone
    float radius = 0.0F, end_radius = -1; // metres; a tapered capsule's end radius
    bool fit = false;                     // radii fitted to the vertices skinned to the bone(s)
    float fit_percentile = 0.85F;
    float fit_scale = 1.0F;
    bool clear = false; // shrunk until every control it collides with starts outside it
    // the side a control or face belongs on: as at rest (the coat outside the legs whatever they do), or
    // the side it came from (arms: their rest place, a T-pose, says nothing about where the coat is)
    bool rest_side = true;
    std::vector<std::string> tags;
    int line = 0;
};

enum class TimeUnit { seconds, frames, ticks }; // frames: the game's animation frames (30 a second)

struct ControlConfig {
    std::string name;
    std::vector<std::string> animations;                            // the game's animation names; `*` any
    int slot = -1;                                                  // a KH2 mset slot it must play (-1 any)
    float section = -1.0F;                                          // a motions.txt section's first frame (-1 any)
    float from = 0.0F, to = std::numeric_limits<float>::infinity(); // within the animation
    TimeUnit unit = TimeUnit::seconds;
    bool loop = false; // `to` past the animation's end wraps (looping animations)
    std::vector<std::string> groups;
    std::vector<BoneRef> bones;
    float weight = 1.0F;
    float blend_in = 0.0F, blend_out = 0.0F; // seconds
    int priority = 0;
    ModeSetting mode;
    PolicySetting policy;
    ParamSet params;
    Wind wind;
    int line = 0;
};

struct LodConfig {
    float distance = 0.0F; // m from the camera at which it applies
    int substeps = 4, iterations = 2;
    bool self_collision = true, environment = true;
    bool frozen = false; // past this distance the animation is drawn as it is
};

struct Config {
    int version = 0;
    float units = 1.0F; // metres per file length unit
    float rate = 60.0F; // Hz
    int substeps = 4, iterations = 2;
    int max_catchup = 4;            // ticks run at most for one draw; more: the state is rebased
    float teleport_distance = 2.0F; // m the attachment may jump in one tick before a rebase
    float teleport_degrees = 100.0F;
    float idle_reset = 1.0F; // s unseen after which a returning actor starts afresh
    Vec3 gravity{0.0F, -9.81F, 0.0F};
    Wind wind;
    bool environment = true; // the room's collision
    float environment_thickness = 0.02F;
    bool environment_ground_only = false; // `environment ... ground`: the floors alone (no walls, ceilings, objects)
    int settle_iterations = 30;           // first-pose collision relaxation before the first result
    std::vector<LodConfig> lods;
    bool debug = false, profile = false;
    std::vector<PresetConfig> presets;
    std::vector<GroupConfig> groups;
    std::vector<ColliderConfig> colliders;
    std::vector<ControlConfig> controls;
    std::vector<std::filesystem::path> files; // every file read (for reloading)
};

// Problems found, each with its file and line; `error` ones make the configuration unusable.
struct Diagnostic {
    bool error = true;
    std::string file;
    int line = 0;
    std::string text;
};
using Diagnostics = std::vector<Diagnostic>;
std::string format(const Diagnostic &d);
bool has_errors(const Diagnostics &d);

// Reads `path` (and the files it includes); presets resolved, numbers range-checked. KH2 models
// (`kh2_bones`) take bare numbers as kh2_<n>.
Config read_config(const std::filesystem::path &path, Diagnostics &out);
Config parse_config(const std::string &text, const std::string &name, Diagnostics &out,
                    const std::filesystem::path &folder = {});

} // namespace khdays::secondary
