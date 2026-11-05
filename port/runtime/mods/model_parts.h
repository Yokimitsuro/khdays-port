// The parts of a mod's model and its draws that models.cpp and model_*.cpp share (models.h is the port's
// side): matrices, a model as loaded from its files, its actor's motion state, the draw's state.

#pragma once

#include "models.h"
#include "../rooms/added.h" /* C++ in part (std::function): outside the extern "C" below */

extern "C" {
#include "../../gfx/g3d.h"
#include "../../gfx/character_shadow.h"
#include "../core/origin.h"
#include "../core/events.h" // khdays_events_vblank_count
#include "../core/rom.h"    // khdays_mods_find
#include "../core/session_log.h"
#include "../scripts/game_link.h" // khdays_link_paused
#include "textures.h"
#include "../core/window.h"
#include "../../host/app/hud_settings.h" // a model's switch (bones.txt)

/* The game's floor ray (Mover_ResolveFloor's downward casts): its collision world by index, the first
 * hit of a segment (CollCastParams: origin, direction, radius, direction-is-unit, flags, exclude) and
 * the point at a hit's distance along it. */
KhdaysRef GetTrackEntryBase(int index);
KhdaysRef Collision_RunRayCast(KhdaysRef world, KhdaysRef params);
void Vec3ScaleAddQ27(s32 distance, KhdaysRef direction, KhdaysRef origin, KhdaysRef out);
}
#include "face.h"
#include "attached_frame.h"
#include "cuff_fit.h"
#include "hand_pose.h"
#include "kh2_clock.h"
#include "kh2_moves.h"
#include "kh2_tail.h"
#include "names.h"
#include "room_animation.h"
#include "../characters/characters.h"
#include "secondary/model_link.h"

#include "khdays/assets/fbx.h"
#include "khdays/assets/gltf.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_mdlx.h"
#include "khdays/assets/kh2_motion.h"
#include "khdays/assets/mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace khdays::model_parts {

using Mat = std::array<float, 16>;
constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// The rest pose correction leaves a joint as it is when the turn it would
// take is smaller than this: such differences are the two models' shapes,
// not their poses (a T-pose's arms are 70-90 degrees from arms-down).
constexpr float kPoseDegrees = 20.0F;
constexpr int kCuffSurfaces = 6; // the `cuff_pose` surface baked at this many shares of the way

struct Joint {
    std::string name;
    int parent = -1;    // in Model::joints, which lists parents first
    int node = -1;      // the game's node it follows
    bool exact = false; // at its node's rest place, uncorrected: drawn with D_G
    std::array<float, 3> head{};
    float degrees = 0.0F; // its pose's turn from its node's (the session log's diagnosis)
    Mat correction = kIdentity;
    Mat bind = kIdentity; // the joint's vertices to the model at rest: scale * world * inverse bind
    int room_node = -1;   // its node in the file, which the model's own animation moves
    Mat inverse_bind = kIdentity;
    // its frame to its game node's at rest (a turn): the node's frame where the joint is (result_matrix)
    Mat to_node = kIdentity;
    // bones.txt's `place`: on its game node's own place (plus `place_offset`, metres, the node's rest axes), not
    // where its parent carries it
    bool placed = false;
    std::array<float, 3> place_offset{};
    // `place ... ride`: a paired joint (a weapon's hand; the nearest above unless named), left as it is: this one is
    // moved by how far that joint is from its own node, so it stands from the KH2 hand as the game's node from the
    // game's. Posed last, with what hangs from it (`after_rides`), once that joint is where it ends up
    int rides = -1;
    bool after_rides = false;
    // what a mix of poses takes this joint from (KH2's kn::Motion::calc): past helper bones (no_envelope); those
    // from their own parents
    int mix_parent = -1;
    // a mod's character's paired joint, not `exact`: carried from its parent, then moved as far as its node travels
    // beyond where the node of its nearest paired joint above (`travel_from`; -1: the model's root) carries it (the
    // hips' rise and fall), at the joint's `size` to the game's (its length from that joint against the nodes'); what
    // the game attaches to its node is that far from it at that size too
    bool travels = false;
    int travel_from = -1;
    float size = 1.0F;
};

// A bone stretched to reach its one paired child where `place` puts that child: the joint, the child, and the
// joints that ride it (the joint and its unpaired descendants above the child)
struct Stretch {
    int joint = -1, child = -1;
    std::vector<int> riders;
};

// bones.txt's `spine`: the chain from its base to its end bent so the base is where the game's node of the
// chain's first paired joint carries it and the end where its own node does; `hips`: the pelvis (that joint's
// nearest paired joint above) turned as the base's node, then swung so the joint is where the pelvis's own
// node carries it
struct Spine {
    int root = -1;          // the top joint above the base: moved with it
    int base_node = -1;     // the game node carrying the base
    std::vector<int> chain; // the base to the end's parent
    int end = -1;
    int pelvis = -1, hips = -1;
};

// bones.txt's `ik`: the upper, lower and end joint; `exact`: the end reaches the game's end node itself (plus
// `offset`, model units, its rest axes, turned with that node), not the game's limb scaled to the model's
struct Limb {
    std::array<int, 3> joints{-1, -1, -1};
    bool exact = false;
    std::array<float, 3> offset{};
};

// bones.txt's `twist`: the joint and the joints under it turned about the way between two joints (at rest, as its parent
// carries it) by a share of the other joint's twist against that parent. `swing`: swung about its head by a share of how
// far the other joint's x has swung off where `frame` would carry it, leaving out any swing toward the `except` joint
// (the side a `cuff_pose` answers).
struct Twist {
    std::size_t joint = 0, by = 0, parent = 0, to = 0;
    float share = 1.0F;
    bool swing = false;
    bool except = false;
    std::array<float, 3> except_way{}; // model space at rest
    std::array<float, 3> axis{};       // model space at rest
    std::vector<std::size_t> turned;
};

// How a mesh's vertex colours' alpha counts (glTF: it multiplies the texture's): not at all (an opaque
// material); a triangle under the cutoff left out (a mask); the triangle's polygon alpha lowered by it
// (blended). Per triangle, by its corners' mean: the DS's alpha is a polygon's, not a vertex's.
enum class VertexAlpha { ignored, cut, blended };

struct Mesh {
    std::vector<khdays::assets::NeutralVertex> vertices; // joints: Model::joints indices, -1 none
    std::vector<std::uint32_t> indices;
    int replacement = -1;
    float tex_w = 1.0F, tex_h = 1.0F;
    bool double_sided = false, translucent = false;
    bool shadow_sprite = false;
    VertexAlpha vertex_alpha = VertexAlpha::ignored;
    float cutoff = 0.5F;
    // its own moving texture: texels added a game frame; pictures in turn (the textures made for them,
    // aligned with `frames`)
    std::array<float, 2> scroll{0.0F, 0.0F};
    struct Picture {
        int replacement = -1;
        float w = 1.0F, h = 1.0F;
        bool translucent = false;
    };
    std::vector<std::pair<std::string, int>> frames;
    std::vector<Picture> frame_pictures;
    bool frames_loop = true;
    std::size_t frames_loop_from = 0;
    // 0, or the sky layer it is (1 is painted first), drawn around the camera behind everything
    int sky = 0;
    // for leaving it out where the view does not reach it (box_meshes): whether all its vertices follow one
    // joint (`box_joint`, kNoJoint the model's first node), the box round them there, and how many of its
    // triangles are drawn (triangle_attr)
    static constexpr std::uint32_t kNoJoint = 0xffffffffU;
    bool boxed = false;
    std::uint32_t box_joint = kNoJoint;
    std::array<float, 3> box_low{0.0F, 0.0F, 0.0F}, box_high{0.0F, 0.0F, 0.0F};
    std::uint32_t drawn_triangles = 0;
};

// A mod's character with a skeleton of its own (runtime/characters): each node's translation, rotation (x y z w)
// and scale, its clips (by their files) and what it showed last.
struct Trs {
    std::array<float, 3> t{0.0F, 0.0F, 0.0F};
    std::array<float, 4> r{0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 3> s{1.0F, 1.0F, 1.0F};
};

struct Clip {
    bool ok = false;
    khdays::assets::ImportedAnimation animation; // the file's first
    std::vector<int> node_of_track;              // each track's node in the model (by name), -1 none
};

struct OwnSkeleton {
    const KhdaysCharacter *character = nullptr;
    std::vector<Trs> rest;             // each node's
    int root = -1;                     // the skeleton's top joint's node: its travel is the game's
    std::map<std::string, Clip> clips; // by file, as the description names it
    std::string playing;               // the game's animation shown
    std::vector<Trs> shown, from;      // the pose shown last, and the one a new animation blends from
    u32 blend_start = 0, blend_frames = 0;
    // a clip at its own speed: its time (seconds while the game's animation moves), the vblank it was last moved on
    // and the one the game's animation last moved on, that animation's frame then; the game's animations seen to
    // loop (their clips loop too)
    double clock = 0.0;
    u32 clock_vblank = 0, moved_vblank = 0;
    float last_frame = 0.0F;
    std::set<std::string> looping;
    std::set<std::string> told; // the animations told to have no clip
    std::vector<Mat> world_now; // this draw's nodes' places (own_pose)
    // its weapons: each base model's node the game puts one on (a hand's), its own node that weapon follows, and the
    // turn from that bone's rest to the base node's
    struct Held {
        int game_node = -1;
        int bone = -1;
        Mat fix = kIdentity;
    };
    std::vector<Held> held;
};

// The KH2 motion a game animation plays from its frame `from` (BCA frames) until the next section's,
// cross-faded in over `blend` frames from the pose before. `follow`: the motion's time is the game's
// frame, stretched from the section onto the motion, so the game's holds and loops are the motion's;
// a frame the game sets back cross-fades over `loop_blend` frames (-1: `blend`'s).
struct Kh2Section {
    float from = 0.0F;
    std::size_t slot = 0;
    float blend = 0.0F;
    float loop_blend = -1.0F;
    bool follow = false;
    bool start_loop = false; // its clock starts at the motion's loop point, not its first tick
    // the whole pose moved (KH2 centimetres, the model's axes): `offset`, gliding to `offset_to` over the motion
    std::array<float, 3> offset{}, offset_to{};
    // `finish [<slot>...]`: when the game goes on to one of its `idle` animations before the motion ends, the rest of
    // it plays, then those slots, then the idle's (kh2_tail.h)
    bool finish = false;
    std::vector<std::size_t> then;
    bool realtime = false; // its clock runs with the game's frames, not its animation's (`air`: Days holds a frame)
    bool cut = false;      // `cut`: it takes the whole pose at once where the game fades into it (the death)
    // `rotate <x> <y> <z>`: the whole pose turned about the model's origin (degrees, x then y then z, the model's
    // axes), before `offset`: a motion lying another way than the game's (KH2's game over)
    Mat turn = kIdentity;
};

// motions.txt's `air <game animation> <slot> [land <slot>] [finish <slot>...] [blend <frames>]`: while the game
// plays that animation in its damage-in-the-air state (5, a launch included), the slot loops in its place; landing
// straight out of it, the land slot plays in place of the `landing` animation's motion, then the `finish` slots
struct Kh2Air {
    std::vector<Kh2Section> air;  // one section, from frame 0
    std::vector<Kh2Section> land; // one section (none without `land`)
};

// A KH2 face's eyes or mouth (face.h) and the sprite last patched in (-2 none yet)
struct FaceBlock {
    khdays::assets::ImportedTextureAnimation animation;
    std::unique_ptr<khdays::face::Player> player;
    int shown = -2;
    int last_slot = -1; // the slot of its last script (`face portrait ... last`)
};
// A texture faces are drawn over, as one alpha tag made it: changing, its picture kept to put back
struct FaceTexture {
    std::string key;
    int replacement = -1, width = 0, height = 0;
    std::vector<std::uint8_t> picture; // RGBA, alpha as made
    int alpha = 0;                     // 0 kept, 1 opaque, 2 cut at `least`
    std::uint8_t least = 0;
};
// motions.txt's `face portrait` line: slots held while that portrait is its speaker's (-2: each block's last
// script) and the talk slot then in place of `face talk` (-1 that one)
struct FacePortrait {
    int portrait = -1;
    std::vector<int> slots;
    int talk = -1;
};
// motions.txt's `face` lines: what holds a face's slots besides its motions' ranges
struct FaceConfig {
    int talk = -1;                               // `face talk <slot>`: while its character's text types
    std::vector<int> speakers;                   // `face speaker <portrait>...`: the dialogue's portraits that are it
    std::vector<std::pair<std::string, int>> ds; // `face ds <pattern animation> <slot>`: while the game plays it
    bool ranges = true;                          // `face ranges off`: the motions' ranges and triggers left out
    // `face portrait <portrait> [last|<slot>...] [talk <slot>]`: slots held while that portrait shows (each on the
    // block that has it) and its talk. A held shape has no blink; its blinking variant, when the face has one, is a
    // slot of its own (N_EX830's eyes: 13, 15, 17, 19)
    std::vector<FacePortrait> portraits;
    std::vector<int> closed; // `face closed <slot>...`: eyes already closed, which `face ds` leaves as they are
};

struct Model {
    std::string name;
    std::string path; // its file
    std::vector<Joint> joints;
    std::vector<Mesh> meshes;
    float scale = 1.0F;
    int cull = 0;                       // bones.txt's `cull`: 0 the file's own, 1 back faces culled, 2 both sides drawn
    std::set<std::uint32_t> kept_lists; // bones.txt's `keep`: the game's shapes still drawn (their lists' places)
    int setting = -1;                   // bones.txt's `switch`: the HD replacement it follows (hud_settings.h)
    float pose_degrees = kPoseDegrees;  // bones.txt's `pose_degrees`: the rest pose correction's threshold
    std::vector<Limb> limbs;            // bones.txt's `ik`: its two-bone limbs, reaching as the nodes do
    // bones.txt's `mesh_scale`: a joint and its skin matrix's last factor, its vertices scaled in its own axes
    std::vector<std::pair<std::size_t, Mat>> mesh_scales;
    std::vector<Twist> twists; // bones.txt's `twist`
    // bones.txt's `cuff`: a sleeve cuff fitted round its hand (cuff_fit.h)
    struct Cuff {
        std::size_t root = 0, hand = 0;
        khdays::models::CuffFit fit;
        // bones.txt's `cuff_pose`: the flaps' bones, what each flap joint is among them (-1 none), the surface baked at
        // even shares of the way to the pose, and the hand's way and the way it bends to reach it, at rest
        std::vector<khdays::models::CuffPoseBone> pose;
        int pose_ring = -1; // the root's parent among them: the sleeve's ring, which scaling shrinks the coat's forearm
        std::array<std::vector<int>, khdays::models::CuffFit::kFlaps> flap_pose;
        std::vector<khdays::models::CuffFit> surfaces;
        std::array<float, 3> hand_way{}, toward{};
        std::array<std::vector<std::size_t>, khdays::models::CuffFit::kFlaps> flap_joints, tip_joints;
        std::vector<std::pair<std::size_t, std::array<float, 3>>>
            points; // the hand's: a joint, a vertex as it holds it
        std::vector<std::size_t>
            slides; // what slides with it: its root's parent, the ring its edge is made of, and under
    };
    std::vector<Cuff> cuffs;
    khdays::models::CuffFit::Settings cuff_settings;
    // bones.txt's `held <joint>`: a weapon whose bones from its root to that joint (the weapon itself) the body
    // holding it poses with its `weapon_motions`; per joint whether it is one of them, and its KH2 bone
    std::string held_name;
    bool held_rest = false;         // `held <joint> rest`: the joint at rest is the holding bone at rest
    bool held_rest_checked = false; // (said once if it is not)
    bool held_own_turn = false; // `held <joint> rest <x> <y> <z>`: no holding bone, the game's frame this turn off it
    Mat held_from_game = kIdentity;
    std::vector<int> held_bone;
    // motions.txt's `weapon_motions <mset> <mdlx> <bone>`: what this body holds plays that mset's slots as its
    // own motions play theirs, on that mdlx's bones; <bone> is the weapon itself. Its held pose under the game's
    // own animations: slot 0's first frame (each bone's frame, metres)
    std::shared_ptr<const khdays::assets::kh2::MotionSet> weapon_motions;
    std::shared_ptr<const khdays::assets::kh2::MotionSkeleton> weapon_skeleton;
    int weapon_blade = -1;
    std::vector<Mat> weapon_rest;
    std::vector<Mat> node_rest_inverse; // the game's model, per node
    std::vector<int> node_parent;
    std::vector<std::string> node_names;
    // per game node: the joint that carries it (its own, else its nearest ancestor's with one) and its rest frame
    // under that joint's node (result_matrix: what the game attaches to a node goes where the model's joint is)
    std::vector<int> node_joint;
    std::vector<Mat> node_local;
    std::vector<std::array<float, 3>> node_rest_place; // each game node's head at rest (model space)
    std::vector<Stretch> stretches;                    // `place`'s bones stretched to their placed child, parents first
    Spine spine;                                       // `spine` (end -1: none)
    int draws = 0;                                     // for the session log's diagnosis
    bool room = false;                  // a room of a world its own mod replaces: drawn with HD graphics off too
    std::array<float, 3> low{}, high{}; // its size at rest (a placed model's, khdays_models_placed_bounds)
    bool character = false;             // a mod's character's (runtime/characters): the same
    std::unique_ptr<OwnSkeleton> own;   // its own skeleton, posed by its clips (else following the game's nodes)
    // a mod's character's: its hands' weapons' size to the game's ([model] weapon_scale) and the game nodes they are
    // on (its base's hands); with a paired skeleton, the frames the game keeps of its nodes for what it attaches (its
    // weapons) put where the model's joints carry those nodes
    float weapon_scale = 1.0F;
    std::vector<char> weapon_node;
    bool attached_on_joints = false;
    // its own animation (a room's): the file's nodes and animations, and its clock in game frames,
    // counted from its first draw while the game is not paused
    std::vector<khdays::assets::ImportedNode> nodes;
    std::vector<khdays::assets::ImportedAnimation> animations;
    bool own_motion = false;
    bool clock_started = false;
    std::uint64_t frames = 0;
    u32 last_vblank = 0;
    // a KH2 model's own motions (<model>.motions.txt): the mset, the sections of the game's animations
    // that play its motions (by first frame), each joint's KH2 bone (-1 none) and the joint of bone 0
    std::shared_ptr<const khdays::assets::kh2::MotionSet> kh2_motions;
    std::shared_ptr<const khdays::assets::kh2::MotionSkeleton>
        kh2_skeleton;                                                  // the mset's model's (its own, else `skeleton`)
    std::shared_ptr<const khdays::assets::kh2::Retarget> kh2_retarget; // from the `skeleton`'s bones to the model's
    // `own` msets' slots, [from, to): made for this model's own bones (kh2_own_skeleton), played without the retarget
    std::shared_ptr<const khdays::assets::kh2::MotionSkeleton> kh2_own_skeleton;
    std::vector<std::pair<std::size_t, std::size_t>> kh2_own_slots;
    std::map<std::string, std::vector<Kh2Section>> kh2_sections;
    std::vector<std::string> kh2_idle; // motions.txt's `idle`: the game's idles a `finish` plays out into
    // `<game animation> blend <frames>` (no slot): its fade into an idle, for the game's own animations
    std::map<std::string, float> kh2_idle_blend;
    std::string kh2_landing;               // `landing`: the game's landing animation (`air`'s `land`)
    std::map<std::string, Kh2Air> kh2_air; // `air`, by the game's animation
    bool said_states = false;              // the game's actor states were seen reaching its draws
    std::vector<Mat> kh2_rest;             // per joint: inverse(inverse_bind), skin to world (made on first use)
    bool kh2_root_motion = false;
    bool kh2_ik_adjust = true; // the feet on the game's floor (IKAdjust)
    std::vector<int> kh2_bone;
    std::vector<int> kh2_weapon;      // per game node: the joint what the game attaches to it is put on (-1 none)
    std::vector<Mat> kh2_weapon_turn; // per game node: `weapon ... rotate`, about that joint's own axes
    // motions.txt's `ds_pose`: while the game's own animations pose it, unpaired joints turned from rest about
    // their heads; per game animation ("" every other), each joint and its turn (model space, at rest)
    std::map<std::string, std::vector<std::pair<std::size_t, Mat>>> ds_turns;
    // motions.txt's `hand` lines (docs/KH2_MODELS.md, "Hands that respond"); they win over `ds_pose`
    struct HandFinger {
        std::size_t joint = 0, parent = 0;
        Mat frame = kIdentity, frame_inverse = kIdentity; // the joint's rest frame, as the joints' K take it
        khdays::models::HandPoseTurns poses{};
    };
    // a finger: its joints in `Hand::fingers`, and where its spheres sit (a joint's rest head, in that joint's K)
    struct HandDigit {
        std::size_t first = 0, last = 0;
        bool thumb = false;
        std::vector<std::pair<std::size_t, std::array<float, 3>>> spheres;
    };
    struct Hand {
        std::size_t joint = 0, forearm = 0, upper = 0;
        int node = -1;
        Mat frame = kIdentity, forearm_frame = kIdentity;
        std::array<float, 3> forearm_way{}; // from the forearm's head to the hand's, in the forearm's axes
        std::array<float, 3> back{};        // out of the back of the hand, in its axes
        std::vector<HandFinger> fingers;    // parents first
        std::vector<HandDigit> digits;
        std::vector<std::size_t> upper_joints, forearm_joints, hand_joints; // what each bone of the arm carries alone
        std::array<float, 3> palm{};                                        // its middle at rest
        std::vector<std::array<float, 4>> samples;                          // what stays out: rest places and radii
    };
    std::vector<Hand> hands;
    std::vector<int> node_hand; // per game node: the hand what is attached to it is held by (-1 none)
    struct HandBody {
        std::size_t joint = 0; // what carries it
        khdays::models::HandBodyShape shape;
        float margin = 0.0F;
    };
    std::vector<HandBody> hand_body;
    float hand_bend_from = 10.0F, hand_bend_to = 45.0F; // degrees: the open pose's share of the wrist's bend
    float hand_grip_fade = 0.15F;                       // seconds
    float hand_finger_link = 0.8F; // how much of a finger's opening for collision each finger beside it takes
    float hand_fist_from = 1.5F, hand_fist_to = 3.0F; // the hand's speed against the body: the fist's share
    int hand_walk_joint = -1;                         // `hand_walk`: the body's joint, how fast it travels
    float hand_walk_from = 0.5F, hand_walk_to = 1.5F, hand_walk_share = 1.0F;
    float hand_contact_near = 0.07F, hand_contact_far = 0.13F, hand_contact_speed = 0.8F; // of the palm to a surface
    float hand_lag = 0.6F;                                                                // of the wrist's turn lagging
    // `hand_anim`: per game animation, each hand's pose whatever the rules say (-1 none)
    std::map<std::string, std::array<int, 2>> hand_anim;
    // its secondary motion (<model>.secondary.txt: cloth, cords, hair), on top of whatever poses it
    khdays::secondary::ModelSecondaryPtr secondary;
    // its face (one for all its actors), on the game's clock
    std::vector<std::unique_ptr<FaceBlock>> face_blocks;
    std::vector<FaceTexture> face_textures;
    FaceConfig face;
    u32 face_vblank = 0;
    bool face_clock = false;
    int face_portrait = -1; // the last of its speaker's portraits shown, held until the next (-1 none)
};

inline int trace = -1;
inline int draws; // for KHDAYS_TRACE_MODELS=2

// A joint animation the drawn model plays (NNSG3dAnmObj on the render object's anmJnt chain): its
// BCA0 name, frame (BCA frames), blend weight (0..1; two during the game's cross-fade) and length.
struct Playing {
    std::string name;
    float frame = 0.0F;
    float ratio = 1.0F;
    float frames = 0.0F; // the J0AC's u16 at +4
};
inline std::vector<Playing> playing;
inline std::vector<std::string> materials_playing; // the texture pattern animations on its anmMat chain (the eyes)
// by the J0AC's DS address, with the file and place it was named from: the game loads another character's file over it
struct AnimationName {
    std::string file;
    u32 offset = 0;
    std::string name;
};
inline std::map<u32, AnimationName> animation_names;
inline std::map<const void *, std::vector<std::pair<bool, Mat>>> node_results; // by render object: draw()'s per node
inline const void *drawn_object = nullptr; // the render object being drawn: which actor's model
inline const void *begun_object =
    nullptr; // the outermost render object begun, replaced or not (khdays_models_draw_end)
// every render object the game draws, replaced or not, and the vblank it was last drawn: whether what is attached
// to a hand shows, whatever the path that draws it
inline std::unordered_map<KhdaysRef, u32> drawn_lately;

inline Mat drawn_start = kIdentity; // the position matrix as the draw starts: the view times the actor's world matrix
// the latest flush's base transform (khdays_g3d_base) and camera as the draw starts, and the base's kind
inline Mat drawn_base = kIdentity, drawn_camera = kIdentity;
inline int drawn_base_kind = 0;
inline bool drawn_has_camera = false;

// A section's KH2 motion as an actor plays it: its player (the pose it keeps) and its clock in KH2's
// ticks (60 a second).
struct Kh2Track {
    std::unique_ptr<khdays::assets::kh2::MotionPlayer> player;
    const khdays::assets::kh2::MotionSkeleton *skeleton = nullptr; // the player's
    float ticks = 0.0F;
    std::vector<khdays::assets::kh2::Mat4> joints; // the pose shown: the model's bones
    float face_ticks = -1.0F;                      // the motion time last shown, for its triggers (-1 none)
};
// A game animation an actor plays: its section and the motion time it last showed, the pose fading out
// (kept as it was last shown) with the frames left of the fade, its frame and forward step at its last
// draw and `seen` the actor's draw it was last in.
struct Kh2Animation {
    int section = 0;
    float shown = 0.0F;
    std::vector<khdays::assets::kh2::Mat4> fade;
    const khdays::assets::kh2::Motion *fade_motion = nullptr;
    int fade_slot = -1;                                 // its slot
    std::array<float, 3> fade_offset{}, shown_offset{}; // the fading pose's `offset`, and the one last shown
    Mat fade_turn = kIdentity, shown_turn = kIdentity;  // the same for `rotate`
    float fade_ticks = 0.0F, fade_left = 0.0F, fade_frames = 0.0F;
    float looped_at = -1.0F; // a loop's fade: the frame the game set back from (-1 none)
    float last_frame = 0.0F;
    float step = 1.0F;
    std::uint64_t seen = 0;
    std::map<int, Kh2Track> tracks; // by section
};
// A `finish` section the game played before going on to its idles, and its motion played out after it (kh2_tail.h)
struct Kh2Finish {
    std::string name; // the game animation (empty: none)
    std::size_t slot = 0;
    std::vector<std::size_t> then;
    float blend = 0.0F;
    float ticks = 0.0F; // the motion time it last showed
};
struct Kh2Actor {
    const Model *model = nullptr; // what it draws (actor_of)
    unsigned drawn_frame = 0;     // the game frame of its last draw (actor_of)
    std::uint64_t draws = 1;      // from 1: no animation's `seen` follows it, so a first draw is a start
    std::map<std::string, Kh2Animation> animations; // by game animation
    std::map<std::string, float> frames;            // each game animation's frame at the draw it was last in
    Kh2Finish finish;
    khdays::kh2_tail::Tail tail;
    // the game animation it plays out while it still fades out into the idle, and its weight at the last draw: once
    // gone or rising again (played anew), it is the game leaving its idles
    std::string tail_from;
    float tail_from_ratio = 0.0F;
    // `air`: the game animation whose air slot played last, and the one whose land slot the landing plays now
    std::string air_from, landing_from;
    std::set<std::string> in_air; // the game animations playing their air slot, in the air or fading out after it
    float walk = 0.0F;            // `walk` lines: how far into its walk motions it is (0 none, 1 wholly)
    std::map<std::string, float> ratios; // each game animation's weight at the last draw, as the game gave it
    u32 vblank = 0;                      // the vblank of its last draw (an `air` slot's clock)
    // `blend` into an idle: the pose shown at the last draw (its skin), the animation the game played most then,
    // its fade into an idle and whether an idle played; the pose fading out, its share, its frames and its source
    std::vector<Mat> shown;
    std::string main;
    float main_blend = 0.0F;
    bool had_idle = false;
    std::vector<Mat> held;
    khdays::kh2_moves::IdleFade held_fade;
    float held_from_ratio = 0.0F;
    std::string held_from;
    Kh2Track tail_track, tail_faded; // the piece playing and the one before it fading out
    std::size_t tail_piece = 0;
    // `weapon_motions`: the weapon's motion players, by slot (a player keeps its pose from one draw to the next)
    std::map<int, std::unique_ptr<khdays::assets::kh2::MotionPlayer>> weapon_players;
    const khdays::assets::kh2::MotionSkeleton *weapon_skeleton = nullptr; // the skeleton they play on
    // `hand`: each hand's state from draw to draw
    struct HandState {
        khdays::models::HandShares shares{};
        std::array<float, 3> push_speed{}; // the wrist's move's spring's speed
        bool touching = false;             // a surface under the palm
        float surface_height = 0.0F;       // that surface along its way out, eased across seams
        std::array<float, 3> push{};       // the wrist moved, in the world
        std::array<float, 3> at{};         // where it was, in the world
        std::array<float, 3> surface{};    // the surface's way out while on it
        float travel = 0.0F;               // its way along the back of the hand a second
        std::array<float, 5> digit_bend{}; // each finger's wrist's bend, lagging
        std::array<float, 5> digit_open{}; // each finger lifted off what it would go into
    };
    std::vector<HandState> hand_states;
    std::array<float, 3> hand_body_at{};
    u32 hand_vblank = 0;
    unsigned hands_held = 0;
    std::vector<khdays::models::CuffMotion> cuff_motions; // `cuff`: each one's state
    u32 cuff_vblank = 0;
};
inline std::map<const void *, Kh2Actor> kh2_actors; // by render object
constexpr unsigned kActorGap = 6U;                  // game frames undrawn after which an actor starts afresh
// The KH2 motion with the most weight in this draw, for the face: its attributes, its time and the time it
// showed the draw before (-1: it starts)
struct FaceMotion {
    const khdays::assets::kh2::MotionAttributes *attributes = nullptr;
    float ticks = 0.0F, before = -1.0F;
};
inline FaceMotion face_motion;
// the dialogue (khdays_models_dialogue_*): its speaker's portrait (-1 none) and the VBlank a letter last typed
inline int dialogue_speaker = -1;
inline u32 dialogue_typed = 0;
inline bool dialogue_typing = false;

// A joint's mesh scale (`mesh_scale <joint> <x> <y> <z>`).
struct MeshScale {
    std::string joint;
    std::array<float, 3> by{};
};
// bones.txt's `hide <joint>... from <top> to <bottom> [fade <length>]` (the model's own centimetres, at rest)
struct HideLine {
    std::vector<std::string> joints;
    float top = 0.0F, bottom = 0.0F, fade = 0.0F;
};
struct LimbLine {
    std::array<std::string, 3> joints;
    bool exact = false;
    std::array<float, 3> offset{}; // centimetres
};
struct CuffLine { // cuff <root> <hand> <flap> <flap> <flap> <flap>: the flaps in order round the ring
    std::array<std::string, 6> joints;
};
struct CuffPoseLine { // cuff_pose_bone <joint> <x> <y> <z> <scale x> <y> <z>: its change from its parent at the pose
    std::string joint;
    std::array<float, 3> degrees{}, scale{1.0F, 1.0F, 1.0F};
};

struct TwistLine {
    bool swing = false;
    std::string except;                // `swing`: the joint it does not swing toward
    std::array<std::string, 4> joints; // the joint, the one it turns with, the axis's from and to
    float share = 1.0F;
};

// What `<model>.bones.txt` beside the model's file says (.gltf, .glb, .fbx or .mdlx: its name without its extension).
struct BonesFile {
    std::map<std::string, std::string> pairs;
    float scale = 1.0F;
    int cull = 0;
    std::vector<std::string> keep;      // the game's nodes whose own shapes are still drawn
    std::string setting;                // the HD replacement switch it follows
    float pose_degrees = kPoseDegrees;  // the rest pose correction's threshold
    std::vector<LimbLine> limbs;        // its two-bone limbs (`ik <upper> <lower> <end>`)
    std::vector<MeshScale> mesh_scales; // its joints' mesh scales
    std::map<std::string, std::string> aligns;
    std::map<std::string, std::array<float, 3>> places;
    std::map<std::string, std::string> places_riding; // `place <joint> ride [<bone>]`: see Joint::rides
    std::array<std::string, 3> spine;
    std::vector<HideLine> hides;
    std::vector<TwistLine> twists;
    std::string held;
    bool held_rest = false, held_own_turn = false;
    std::array<float, 3> held_turn{};
    std::vector<CuffLine> cuffs;
    std::vector<CuffPoseLine> cuff_pose;
    khdays::models::CuffFit::Settings cuff_settings;
};

// A motions.txt `ds_pose` line: bones turned from rest as a slot's pose turns them (its frame, 30 a second),
// or by degrees about each bone's own x, then y, then z; for the game animations named, else all
struct DsPoseRule {
    std::vector<int> bones;
    int slot = -1;
    float frame = 0.0F;
    std::array<float, 3> degrees{};
    std::vector<std::string> animations;
};

// motions.txt's `hand <hand bone> <forearm bone> <finger bones> back <x> <y> <z>`
struct HandRule {
    int hand = 0, forearm = 0;
    std::vector<int> fingers;
    std::array<float, 3> back{};
};
// `hand_pose relaxed|open|fist|flat|grip rotate <x> <y> <z> <bones>`
struct HandPoseRule {
    std::size_t pose = 0;
    std::array<float, 3> degrees{};
    std::vector<int> bones;
};
// `hand_body <bones> axis <from bone> <to bone> [margin <cm>]`: a part of the body the hands stay out of
struct HandBodyRule {
    std::vector<int> bones;
    int from = 0, to = 0;
    float margin = 0.0F;
};

// --- The draws -------------------------------------------------------------------------

inline std::unordered_map<std::string, std::unique_ptr<Model>> models; // by "<file>@<offset>"; null: not replaced
inline Model *active;
inline int depth;
inline std::vector<Model *> outer; // by depth, the draws a draw's callbacks start: theirs are the game's
inline int shapes;
inline const unsigned char *model_start; // the draw's model (its NNSG3dResMdl): its shapes' lists are from here
inline KhdaysG3dMaterial material;
inline std::vector<Mat> node_matrix;
inline std::vector<char> node_sent;
inline std::vector<std::pair<void *, unsigned>> saved_joints; // the draw's matrices saved for attachments, by node
inline float kh2_game_share = 1.0F; // this draw: the game's own pose's share of a KH2 model's (play_kh2; 1 without)
inline bool kh2_pose_cut =
    false; // this draw: a KH2 motion took the whole pose with no fade (play_kh2): the cloth's cue
extern "C" int khdays_controls_walk_share(unsigned node); // runtime/input/controls_hooks.c: the port's walk
extern "C" int khdays_overlay_loaded(int id);             // runtime/core/overlays.c
// this draw: `weapon_motions`' pose of the weapon (each bone's frame, metres; empty: none) and the share of it held
// as Days holds it (the game's own pose and the slots the weapon has no motion for)
inline std::vector<Mat> kh2_weapon_pose;
inline float kh2_weapon_share = 1.0F;
// the last weapon a body held with `weapon_motions`: its pose, the turn from the game's weapon frame (what the
// game draws it at) to the weapon's root, and the vblank of that body's draw (a `held` weapon's model, bones.txt)
struct HeldWeapon {
    std::vector<Mat> pose;
    Mat from_game = kIdentity;
    u32 vblank = 0;
};
inline HeldWeapon held_weapon;
// the last bone a `weapon` line held something on (`held <joint> rest`): the turn from it to the game's weapon frame,
// its rest (metres) and the vblank of that body's draw
struct HoldingBone {
    Mat from_game = kIdentity;
    Mat rest = kIdentity;
    u32 vblank = 0;
};
inline HoldingBone holding_bone;
// this draw: each game animation's KH2 slot and its section's first frame, `air` and `land` ones included
inline std::map<std::string, std::pair<std::size_t, float>> kh2_played;

// The movers Mover_ResolveFloor put on their floor, by owner (their render object is at owner + 0x24: the
// animation state at +4, func_ov022_020a3c78, its NNSG3dRenderObj at +0x20): the mover and the vblank
// it was last seen at.
struct Grounded {
    KhdaysRef mover = 0;
    u32 vblank = 0;
};
inline std::unordered_map<KhdaysRef, Grounded> grounds;

// The battle actors' states (Ov022_EnterActorState), by scene node as `grounds` (khdays_models_actor_state): the
// state, the vblank it entered it at, and the vblank it last left the damage in the air at
constexpr int kAirDamageState = 5;  // damage in the air, a launch included (Ov022_ResolveGuardBreakState)
constexpr int kLandingState = 0x16; // landing (Ov022_EnterActorState: animation 4, b_ro_A004)
constexpr u32 kLandedOutOf = 8;     // vblanks: landing this soon after state 5 is landing out of it
struct ActorState {
    int state = -1;
    u32 entered = 0, left_air = 0;
};
inline std::unordered_map<KhdaysRef, ActorState> actor_states;

// The floor a drawn actor's KH2 feet are put on (IKAdjust): only while the game has it on the floor
// (its mover's vertical speed 0x80000000; KH2's YS::MOTION::make_ik_adjust 0x45992c: while the object
// touches the ground), `world` the model's centimetres to Days' world (DS units, y up as the solver
// takes it), and the ray onto the actor's floor (the game's collision, the actor itself left out).
struct Floor {
    bool on = false;
    Mat world = kIdentity;
    khdays::assets::kh2::GroundRay ray;
};

// KHDAYS_TRACE_MODELS: a model's floor state when it changes, and now and then its rays (Days' world)
// beside its actor's place (the mover owner's +0xa8) and its own.
struct FloorTrace {
    std::string said;
    unsigned draws = 0, rays = 0, hits = 0;
    std::array<float, 3> from{}, to{}, hit{};
};
inline std::map<std::string, FloorTrace> floor_traces;

inline std::vector<Mat> node_move, joint_place,
    skin;                   // per game node its move from rest (D_G); per joint its place (K_j) and its skinning matrix
inline u32 drawn_animation; // the render object's joint animation (an NNSG3dAnmObj), 0 none
inline u32 next_animation;  // the one khdays_models_draw_animation gave, for the draw that follows
inline std::vector<KhdaysG3dClipVertex> clip;
inline std::vector<unsigned char> clip_past; // each of `clip`'s vertices: the view's sides it lies past (past_sides)
inline std::vector<std::array<khdays::secondary::Vec3, 3>> clip_triangles; // `clip_world`'s volumes this draw

// The game's joint animation `anm` (an NNSG3dAnmObj: its frame in 20.12 at +0, its NNSG3dResJntAnm at +8, whose
// frames are a u16 at +4): its name (its BCA0's), how far it is (0-1) and its frame.
struct GameAnimation {
    std::string name;
    float phase = 0.0F;
    float frame = 0.0F;
    float frames = 0.0F; // its length
};

// The vblanks (60 a second) a clip at its own speed goes on after the game's animation last moved: past them, with
// the game's animation short of its end, the game holds it (a hit's stop), and the clip stands with it.
constexpr u32 kStillVblanks = 4;
// The game's animation back to an earlier frame from its last few: it loops; from elsewhere it began it again.
constexpr float kWrapFrames = 3.0F;

constexpr float kPalmOver = 0.022F; // the palm's middle over what it rests on

// How deep a point (the K's output) is in the body, and its way out. The arms move, the parts do not: their K's inverses
// are worked out once a draw (pose_hands)
inline std::vector<Mat> hand_body_inverse;

// --- What model_*.cpp and models.cpp define ---

// model_bones.cpp
BonesFile read_bones(const std::string &model_file);
int setting_named(const std::string &name, const std::string &key);
std::set<std::uint32_t> kept_lists(const std::string &name, const khdays::assets::NeutralModel &game,
                                   const std::vector<std::string> &keep);
void set_rest_size(Model &model);
Mat turn_of(const std::array<float, 3> &degrees);
std::size_t hand_pose_index(const std::string &name);
void resolve_ds_poses(Model &model, const std::vector<DsPoseRule> &rules,
                      const std::function<void(const std::string &)> &say);
void resolve_hands(Model &model, const std::vector<HandRule> &rules, const std::vector<HandPoseRule> &poses,
                   const std::vector<HandBodyRule> &body, const std::function<void(const std::string &)> &say);
void resolve_cuffs(Model &model, const std::vector<CuffLine> &lines, const std::vector<CuffPoseLine> &pose_lines,
                   const std::string &name);

// model_kh2_motions.cpp
Mat in_metres(Mat g);
bool own_slot(const Model &m, std::size_t slot);
const khdays::assets::kh2::MotionSkeleton &skeleton_of(const Model &m, std::size_t slot);
void pose_on_model(const Model &m, std::size_t slot, const std::vector<khdays::assets::kh2::Mat4> &played,
                   std::vector<khdays::assets::kh2::Mat4> &out);
void ready_player(const Model &m, Kh2Track &track, std::size_t slot);
void load_kh2_motions(Model &model, const std::string &path);
void say_bad_motion(const Model &m, const char *set, int slot, const char *what);
bool play_track(const Model &m, Kh2Track &track, std::size_t slot, const khdays::assets::kh2::Motion &motion,
                float ticks, const Floor &ground, Mat &taken);
void play_kh2(Model &m, const Mat &base);

// model_load.cpp
Mat align_part(bool foot, const std::vector<std::array<float, 3>> &own, const std::vector<std::array<float, 3>> &its,
               float &degrees);
std::unique_ptr<Model> load(const std::string &name, const std::string &path, const char *file, u32 offset);

// model_own_skeleton.cpp
Mat compose(const Trs &x);
Trs mix(const Trs &a, const Trs &b, float w);
void apply_clip(const Clip &source, float t, std::vector<Trs> &pose);
Clip &clip_of(OwnSkeleton &o, const std::vector<khdays::assets::ImportedNode> &nodes, const std::string &relative);
const std::string &animation_name(u32 anm);
GameAnimation game_animation(u32 anm);
float clip_time(float t, float length, bool loops, float from);
std::vector<Mat> own_pose(Model &m);
// Each node's place from `pose` (each one's own, under its parent's), whatever order the file lists them in.
std::vector<Mat> own_places(const std::vector<khdays::assets::ImportedNode> &nodes, const std::vector<Trs> &pose);

// model_hands.cpp
const Mat *moved_world(const Joint &joint, const std::vector<Mat> &moved);
unsigned held_hands(const Model &m);
float room_depth(const std::vector<khdays::secondary::Triangle> &room, std::array<float, 3> p, float radius,
                 const std::array<float, 3> &from, std::array<float, 3> &push);
float body_depth(const Model &m, const std::array<float, 3> &p, float radius, std::array<float, 3> &way,
                 float margins = 1.0F);
void pose_hands(const Model &m);
void pose_cuffs(const Model &m);

// models.cpp
void trace_floor(const Model &m, const std::string &state);
Floor floor_of(const Model &m, const Mat &base);
Mesh::Picture picture_now(const Mesh &mesh, std::uint64_t frames);
bool triangle_attr(const Mesh &mesh, std::size_t t, uint32_t attr, uint32_t &out);
void play_face(Model &m);
void hold_on_bone(Model &m, const Mat &root);
void draw(Model &m);
bool room_of_replaced_world(const std::string &name, const char *path);
void read_playing(const void *render_obj);
void forget_gone(u32 now);

// --- Small helpers ---

inline Mat mul(const Mat &a, const Mat &b)
{
    Mat r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k)
                s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    }
    return r;
}

// The general 4x4 inverse (cofactors); the identity when singular.
inline Mat inverse(const Mat &m)
{
    Mat inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12F)
        return kIdentity;
    for (auto &v : inv)
        v /= det;
    return inv;
}

inline std::array<float, 3> point(const Mat &m, const std::array<float, 3> &p)
{
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

inline std::array<float, 3> vec(const Mat &m, const std::array<float, 3> &v)
{
    return {m[0] * v[0] + m[4] * v[1] + m[8] * v[2], m[1] * v[0] + m[5] * v[1] + m[9] * v[2],
            m[2] * v[0] + m[6] * v[1] + m[10] * v[2]};
}

inline std::array<float, 3> translation(const Mat &m)
{
    return {m[12], m[13], m[14]};
}

inline float length(const std::array<float, 3> &v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// The shortest turn taking direction `from` to direction `to`, and its angle
// in degrees.
inline Mat arc(std::array<float, 3> from, std::array<float, 3> to, float &degrees)
{
    const float lf = length(from), lt = length(to);
    for (int i = 0; i < 3; ++i) {
        from[i] /= lf;
        to[i] /= lt;
    }
    const float c = std::fmax(-1.0F, std::fmin(1.0F, from[0] * to[0] + from[1] * to[1] + from[2] * to[2]));
    std::array<float, 3> axis{from[1] * to[2] - from[2] * to[1], from[2] * to[0] - from[0] * to[2],
                              from[0] * to[1] - from[1] * to[0]};
    degrees = std::acos(c) * 57.2957795F;
    const float s = length(axis);
    if (s < 1e-6F)
        return kIdentity; // the same way (or exactly opposite: left alone)
    for (auto &a : axis)
        a /= s;
    const float t = 1.0F - c, x = axis[0], y = axis[1], z = axis[2];
    return {t * x * x + c,
            t * x * y + s * z,
            t * x * z - s * y,
            0, // column 0
            t * x * y - s * z,
            t * y * y + c,
            t * y * z + s * x,
            0, // column 1
            t * x * z + s * y,
            t * y * z - s * x,
            t * z * z + c,
            0, // column 2
            0,
            0,
            0,
            1};
}

// K = T(at) * linear(d) * correction * T(-head)
inline Mat place(const std::array<float, 3> &at, const Mat &d, const Mat &correction, const std::array<float, 3> &head)
{
    Mat l = d;
    l[12] = l[13] = l[14] = 0.0F;
    Mat k = mul(l, correction);
    const auto h = vec(k, head);
    k[12] = at[0] - h[0];
    k[13] = at[1] - h[1];
    k[14] = at[2] - h[2];
    return k;
}

// T(by)
inline Mat moved_by(const std::array<float, 3> &by)
{
    Mat t = kIdentity;
    t[12] = by[0];
    t[13] = by[1];
    t[14] = by[2];
    return t;
}

// About `at`: `a` turned onto `b` and lengthened along it to `b`'s length (T(at) x along x turn x T(-at))
inline Mat turned_onto(const std::array<float, 3> &at, const std::array<float, 3> &a, const std::array<float, 3> &b)
{
    const float la = length(a), lb = length(b);
    if (la < 1e-6F || lb < 1e-6F)
        return kIdentity;
    float degrees = 0.0F;
    const Mat turn = arc(a, b, degrees);
    Mat along = kIdentity; // (s - 1) u u^T + I
    const float sc = lb / la - 1.0F;
    for (std::size_t col = 0; col < 3; ++col)
        for (std::size_t row = 0; row < 3; ++row)
            along[col * 4 + row] += sc * (b[row] / lb) * (b[col] / lb);
    return mul(moved_by(at), mul(along, mul(turn, moved_by({-at[0], -at[1], -at[2]}))));
}

// This draw's actor, started afresh when its render object now draws another model or has not drawn for kActorGap: a
// tail or a held pose from before would show on what it draws now. In game frames, which a pause and a stall don't count.
inline Kh2Actor &actor_of(const Model &m)
{
    Kh2Actor &actor = kh2_actors[drawn_object]; // (one undrawn for kActorGap is forgotten: forget_gone)
    const unsigned now = khdays_link_frame();
    if (actor.model != &m || now - actor.drawn_frame > kActorGap) {
        actor = Kh2Actor{};
        actor.model = &m;
    }
    actor.drawn_frame = now;
    return actor;
}

inline std::filesystem::path u8(const std::string &path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

inline bool tracing()
{
    if (trace < 0) {
        const char *v = std::getenv("KHDAYS_TRACE_MODELS");
        trace = v != nullptr && *v != '\0' && *v != '0' ? (std::atoi(v) > 1 ? 2 : 1) : 0;
    }
    return trace != 0;
}

// KHDAYS_SECONDARY_TRACE (docs/SECONDARY_MOTION.md)
inline bool secondary_tracing()
{
    static const bool on = [] {
        const char *v = std::getenv("KHDAYS_SECONDARY_TRACE");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    return on;
}

// The joint of that name, -1 none
inline int joint_named(const Model &model, const std::string &name)
{
    for (std::size_t j = 0; j < model.joints.size(); ++j)
        if (model.joints[j].name == name)
            return static_cast<int>(j);
    return -1;
}

// Which of a vertex's four joints carries it most
inline std::size_t heaviest_weight(const khdays::assets::NeutralVertex &v)
{
    std::size_t most = 0;
    for (std::size_t w = 1; w < 4; ++w)
        if (v.weights[w] > v.weights[most])
            most = w;
    return most;
}

// A frame's axes made unit, no translation
inline Mat axes_of(const Mat &m)
{
    std::array<float, 3> scale{};
    return khdays::models::frame_detail::turn_of(m, scale);
}

// Each element of a list of the game's linked at +`next` (`size` bytes each), from `first` to its end (0), while
// `visit` returns true: a link out of the game's memory ends it, and one that loops back (garbage) ends where it laps
// a second pointer going at half its pace.
template <typename Visit> void each_linked(KhdaysRef first, u32 next, u32 size, Visit visit)
{
    auto link = [&](KhdaysRef at) {
        KhdaysRef to = 0;
        std::memcpy(&to, KHDAYS_W(at + next), 4);
        return to;
    };
    KhdaysRef slow = first;
    for (KhdaysRef at = first, walked = 0; at != 0 && khdays_game_ram(at) && khdays_game_ram(at + size - 1);
         at = link(at), ++walked) {
        if (walked > 0 && walked % 2 == 0 && (slow = link(slow)) == at)
            break;
        if (!visit(at))
            break;
    }
}

// An `ik` limb (bones.txt) bent so its end joint reaches as the game's nodes reach: their way from the
// upper node scaled to the model's limb, bent in the plane the game's middle node bends in -- the
// law of cosines about an up vector, as KH2's Motion::calcIKJoint (0xf56a0) bends its chains. The
// upper and lower joints are swung to it, keeping the twist their nodes give them; the joints below
// them are placed again (`pose`).
template <typename Pose> void reach(const Model &m, const Limb &limb_line, const Pose &pose)
{
    using V = std::array<float, 3>;
    const auto sub = [](const V &a, const V &b) { return V{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    const auto add = [](const V &a, const V &b, float s) {
        return V{a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s};
    };
    const auto dot = [](const V &a, const V &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto about = [](const V &at, const Mat &r, const Mat &m0) { // T(at) * r * T(-at) * m0
        Mat t = kIdentity, back = kIdentity;
        t[12] = at[0];
        t[13] = at[1];
        t[14] = at[2];
        back[12] = -at[0];
        back[13] = -at[1];
        back[14] = -at[2];
        return mul(t, mul(r, mul(back, m0)));
    };
    constexpr float kTiny = 1e-5F;
    const std::array<int, 3> &limb = limb_line.joints;
    const auto j0 = static_cast<std::size_t>(limb[0]), j1 = static_cast<std::size_t>(limb[1]),
               j2 = static_cast<std::size_t>(limb[2]);
    const Joint &a = m.joints[j0], &b = m.joints[j1], &c = m.joints[j2];
    const V hip = point(joint_place[j0], a.head), knee_was = point(joint_place[j0], b.head),
            end_was = point(joint_place[j1], c.head);
    const float l0 = length(sub(knee_was, hip)), l1 = length(sub(end_was, knee_was));
    const V hd = translation(node_matrix[static_cast<std::size_t>(a.node)]),
            kd = translation(node_matrix[static_cast<std::size_t>(b.node)]),
            ed = translation(node_matrix[static_cast<std::size_t>(c.node)]);
    const float game = length(sub(kd, hd)) + length(sub(ed, kd));
    if (l0 < kTiny || l1 < kTiny || game < kTiny)
        return;
    const float s = (l0 + l1) / game;
    // `exact`: to the game's end node itself (its offset turned as that node is), else the game's limb scaled
    const V to = limb_line.exact
                     ? sub(add(ed, vec(node_move[static_cast<std::size_t>(c.node)], limb_line.offset), 1.0F), hip)
                     : add(V{}, sub(ed, hd), s),
            pole = add(V{}, sub(kd, hd), s);
    float dist = length(to);
    if (dist < kTiny)
        return;
    const V dir{to[0] / dist, to[1] / dist, to[2] / dist};
    dist = std::fmax(std::fabs(l0 - l1) * 1.001F + kTiny, std::fmin(dist, (l0 + l1) * 0.999F));
    // the bend's side: the game's middle node, else the limb's own knee
    V side = add(pole, dir, -dot(pole, dir));
    if (length(side) < kTiny * 100.0F) {
        const V own = sub(knee_was, hip);
        side = add(own, dir, -dot(own, dir));
    }
    const float ls = length(side);
    if (ls < kTiny)
        return;
    const float along = (l0 * l0 - l1 * l1 + dist * dist) / (2.0F * dist);
    const float out = std::sqrt(std::fmax(0.0F, l0 * l0 - along * along));
    const V knee = add(add(hip, dir, along), side, out / ls), end = add(hip, dir, dist);
    float degrees = 0.0F;
    joint_place[j0] = about(hip, arc(sub(knee_was, hip), sub(knee, hip), degrees), joint_place[j0]);
    for (std::size_t j = j0 + 1; j < m.joints.size(); ++j) {
        bool below = false;
        for (int up = m.joints[j].parent; up >= 0 && !below; up = m.joints[static_cast<std::size_t>(up)].parent)
            below = up == static_cast<int>(j0);
        if (!below)
            continue;
        joint_place[j] = pose(j);
        if (j == j1) {
            const V at = point(joint_place[j1], b.head);
            joint_place[j1] =
                about(at, arc(sub(point(joint_place[j1], c.head), at), sub(end, at), degrees), joint_place[j1]);
        }
    }
}

// `spine` (Spine), on the pose pass's k: the base moved where the game's node carries it (the whole body with
// it); the pelvis turned as that node and swung onto the hips; the chain, as the game's turns pose it, turned
// about the base onto the end's place and its bones lengthened alike to reach it. The joints off the chain posed
// again from their parents after each (paired ones keep the game's turn).
template <typename Pose> void bend_spine(const Model &m, const Pose &pose)
{
    using V = std::array<float, 3>;
    const Spine &sp = m.spine;
    const auto &joints = m.joints;
    const auto sub = [](const V &a, const V &b) { return V{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    const auto ju = [](int j) { return static_cast<std::size_t>(j); };
    const auto at = [&](int frame, int of) { return point(joint_place[ju(frame)], joints[ju(of)].head); };
    std::vector<std::uint8_t> fixed(joints.size(), 0);
    const auto repose = [&] {
        for (std::size_t j = 0; j < joints.size(); ++j)
            if (!fixed[j])
                joint_place[j] = pose(j);
    };
    const int base = sp.chain.front(), last = sp.chain.back();
    const V base_at = point(node_move[ju(sp.base_node)], joints[ju(base)].head);
    joint_place[ju(sp.root)] = mul(moved_by(sub(base_at, at(base, base))), joint_place[ju(sp.root)]);
    fixed[ju(sp.root)] = 1;
    repose();
    if (sp.pelvis >= 0) {
        const Joint &pj = joints[ju(sp.pelvis)];
        joint_place[ju(sp.pelvis)] = place(pj.parent >= 0 ? at(pj.parent, sp.pelvis) : base_at,
                                           node_move[ju(sp.base_node)], pj.correction, pj.head);
        const V hp = at(sp.pelvis, sp.pelvis);
        joint_place[ju(sp.pelvis)] = mul(turned_onto(hp, sub(at(sp.pelvis, sp.hips), hp),
                                                     sub(point(node_move[ju(pj.node)], joints[ju(sp.hips)].head), hp)),
                                         joint_place[ju(sp.pelvis)]);
        fixed[ju(sp.pelvis)] = 1;
        repose();
    }
    for (const int c : sp.chain)
        fixed[ju(c)] = 1;
    const V target = point(node_move[ju(joints[ju(sp.end)].node)], joints[ju(sp.end)].head);
    const auto turn_from = [&](std::size_t from, const V &pivot, const Mat &r) {
        const Mat x = mul(moved_by(pivot), mul(r, moved_by({-pivot[0], -pivot[1], -pivot[2]})));
        for (std::size_t c = from; c < sp.chain.size(); ++c)
            joint_place[ju(sp.chain[c])] = mul(x, joint_place[ju(sp.chain[c])]);
    };
    // the chain as the game's turns pose it, turned about the base onto the end's place and every bone lengthened
    // or shortened alike to reach it: the game's curve kept (bending it further folded it over, a jump)
    const V p0 = at(base, base);
    float degrees = 0.0F;
    turn_from(0, p0, arc(sub(at(last, sp.end), p0), sub(target, p0), degrees));
    const float reach = length(sub(at(last, sp.end), p0));
    if (reach > 1e-6F) {
        const float s = length(sub(target, p0)) / reach;
        std::vector<V> heads;
        for (const int c : sp.chain)
            heads.push_back(at(c, c));
        heads.push_back(at(last, sp.end));
        for (std::size_t c = 0; c < sp.chain.size(); ++c) {
            V u{};
            for (std::size_t n = c + 1; n < heads.size(); ++n) {
                const V w = sub(heads[n], heads[c]);
                const float lw = length(w);
                if (lw > 1e-6F) {
                    u = {w[0] / lw, w[1] / lw, w[2] / lw};
                    break;
                }
            }
            Mat along = kIdentity; // (s - 1) u u^T + I
            for (std::size_t col = 0; col < 3; ++col)
                for (std::size_t row = 0; row < 3; ++row)
                    along[col * 4 + row] += (s - 1.0F) * u[row] * u[col];
            const V q{p0[0] + (heads[c][0] - p0[0]) * s, p0[1] + (heads[c][1] - p0[1]) * s,
                      p0[2] + (heads[c][2] - p0[2]) * s};
            joint_place[ju(sp.chain[c])] = mul(
                moved_by(q),
                mul(along, mul(moved_by({-heads[c][0], -heads[c][1], -heads[c][2]}), joint_place[ju(sp.chain[c])])));
        }
    }
    repose();
}

} // namespace khdays::model_parts
