// A mod's models drawn instead of the game's (models.h): matrices column-major for column vectors;
// each joint drawn with K_j = T(head now) * linear(D_G) * C_j * T(-head_j), D_G = M_G * inverse(W_G)
// for the node G it follows and C_j its rest pose correction; faces culled as the file says unless
// <model>.bones.txt says otherwise (docs/RENDERING.md, "Mods' graphics"). A replaced room plays its own
// animation (room_animation.cpp) by the game's frames, stopped while the game is paused.

#include "models.h"

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

namespace {

using Mat = std::array<float, 16>;
constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// The rest pose correction leaves a joint as it is when the turn it would
// take is smaller than this: such differences are the two models' shapes,
// not their poses (a T-pose's arms are 70-90 degrees from arms-down).
constexpr float kPoseDegrees = 20.0F;
constexpr int kCuffSurfaces = 6; // the `cuff_pose` surface baked at this many shares of the way

Mat mul(const Mat &a, const Mat &b)
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
Mat inverse(const Mat &m)
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

std::array<float, 3> point(const Mat &m, const std::array<float, 3> &p)
{
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

std::array<float, 3> vec(const Mat &m, const std::array<float, 3> &v)
{
    return {m[0] * v[0] + m[4] * v[1] + m[8] * v[2], m[1] * v[0] + m[5] * v[1] + m[9] * v[2],
            m[2] * v[0] + m[6] * v[1] + m[10] * v[2]};
}

std::array<float, 3> translation(const Mat &m)
{
    return {m[12], m[13], m[14]};
}

float length(const std::array<float, 3> &v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// The shortest turn taking direction `from` to direction `to`, and its angle
// in degrees.
Mat arc(std::array<float, 3> from, std::array<float, 3> to, float &degrees)
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
Mat place(const std::array<float, 3> &at, const Mat &d, const Mat &correction, const std::array<float, 3> &head)
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
Mat moved_by(const std::array<float, 3> &by)
{
    Mat t = kIdentity;
    t[12] = by[0];
    t[13] = by[1];
    t[14] = by[2];
    return t;
}

// About `at`: `a` turned onto `b` and lengthened along it to `b`'s length (T(at) x along x turn x T(-at))
Mat turned_onto(const std::array<float, 3> &at, const std::array<float, 3> &a, const std::array<float, 3> &b)
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
    int weapon_bone = -1;       // its node the weapon follows
    int game_weapon_node = -1;  // the base model's node the game puts the weapon on
    Mat weapon_fix = kIdentity; // the bone's rest to that node's rest
};

Mat compose(const Trs &x); // a node's local matrix

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

int trace = -1;
int draws; // for KHDAYS_TRACE_MODELS=2

// A joint animation the drawn model plays (NNSG3dAnmObj on the render object's anmJnt chain): its
// BCA0 name, frame (BCA frames), blend weight (0..1; two during the game's cross-fade) and length.
struct Playing {
    std::string name;
    float frame = 0.0F;
    float ratio = 1.0F;
    float frames = 0.0F; // the J0AC's u16 at +4
};
std::vector<Playing> playing;
std::vector<std::string> materials_playing; // the texture pattern animations on its anmMat chain (the eyes)
// by the J0AC's DS address, with the file and place it was named from: the game loads another character's file over it
struct AnimationName {
    std::string file;
    u32 offset = 0;
    std::string name;
};
std::map<u32, AnimationName> animation_names;
std::map<const void *, std::vector<std::pair<bool, Mat>>> node_results; // by render object: draw()'s per node
const void *drawn_object = nullptr; // the render object being drawn: which actor's model
const void *begun_object = nullptr; // the outermost render object begun, replaced or not (khdays_models_draw_end)
// every render object the game draws, replaced or not, and the vblank it was last drawn: whether what is attached
// to a hand shows, whatever the path that draws it
std::unordered_map<KhdaysRef, u32> drawn_lately;
Mat drawn_start = kIdentity; // the position matrix as the draw starts: the view times the actor's world matrix
// the latest flush's base transform (khdays_g3d_base) and camera as the draw starts, and the base's kind
Mat drawn_base = kIdentity, drawn_camera = kIdentity;
int drawn_base_kind = 0;
bool drawn_has_camera = false;

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
std::map<const void *, Kh2Actor> kh2_actors; // by render object
constexpr unsigned kActorGap = 6U;           // game frames undrawn after which an actor starts afresh

// This draw's actor, started afresh when its render object now draws another model or has not drawn for kActorGap: a
// tail or a held pose from before would show on what it draws now. In game frames, which a pause and a stall don't count.
Kh2Actor &actor_of(const Model &m)
{
    if (kh2_actors.size() > 64)
        kh2_actors.clear();
    Kh2Actor &actor = kh2_actors[drawn_object];
    const unsigned now = khdays_link_frame();
    if (actor.model != &m || now - actor.drawn_frame > kActorGap) {
        actor = Kh2Actor{};
        actor.model = &m;
    }
    actor.drawn_frame = now;
    return actor;
}
// The KH2 motion with the most weight in this draw, for the face: its attributes, its time and the time it
// showed the draw before (-1: it starts)
struct FaceMotion {
    const khdays::assets::kh2::MotionAttributes *attributes = nullptr;
    float ticks = 0.0F, before = -1.0F;
};
FaceMotion face_motion;
// the dialogue (khdays_models_dialogue_*): its speaker's portrait (-1 none) and the VBlank a letter last typed
int dialogue_speaker = -1;
u32 dialogue_typed = 0;
bool dialogue_typing = false;

std::filesystem::path u8(const std::string &path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

bool tracing()
{
    if (trace < 0) {
        const char *v = std::getenv("KHDAYS_TRACE_MODELS");
        trace = v != nullptr && *v != '\0' && *v != '0' ? (std::atoi(v) > 1 ? 2 : 1) : 0;
    }
    return trace != 0;
}

// KHDAYS_SECONDARY_TRACE (docs/SECONDARY_MOTION.md)
bool secondary_tracing()
{
    static const bool on = [] {
        const char *v = std::getenv("KHDAYS_SECONDARY_TRACE");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    return on;
}

// The joint of that name, -1 none
int joint_named(const Model &model, const std::string &name)
{
    for (std::size_t j = 0; j < model.joints.size(); ++j)
        if (model.joints[j].name == name)
            return static_cast<int>(j);
    return -1;
}

// Which of a vertex's four joints carries it most
std::size_t heaviest_weight(const khdays::assets::NeutralVertex &v)
{
    std::size_t most = 0;
    for (std::size_t w = 1; w < 4; ++w)
        if (v.weights[w] > v.weights[most])
            most = w;
    return most;
}

// A frame's axes made unit, no translation
Mat axes_of(const Mat &m)
{
    std::array<float, 3> scale{};
    return khdays::models::frame_detail::turn_of(m, scale);
}

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
    std::array<std::string, 3> spine;
    std::vector<HideLine> hides;
    std::vector<TwistLine> twists;
    std::string held;
    bool held_rest = false;
    std::vector<CuffLine> cuffs;
    std::vector<CuffPoseLine> cuff_pose;
    khdays::models::CuffFit::Settings cuff_settings;
};

BonesFile read_bones(const std::string &model_file)
{
    BonesFile bones;
    const auto dot = model_file.find_last_of('.');
    const std::string path = (dot != std::string::npos ? model_file.substr(0, dot) : model_file) + ".bones.txt";
    std::ifstream in(u8(path));
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        std::string a, b;
        if (!(words >> a >> b))
            continue;
        if (a == "scale") {
            bones.scale = std::strtof(b.c_str(), nullptr);
            if (!(bones.scale > 0.0F))
                bones.scale = 1.0F;
        } else if (a == "keep") {
            for (std::string node = b; !node.empty(); node.clear(), words >> node)
                bones.keep.push_back(node);
        } else if (a == "switch") {
            bones.setting = b;
        } else if (a == "cull") {
            if (b == "back") {
                bones.cull = 1;
            } else if (b == "none") {
                bones.cull = 2;
            } else {
                std::fprintf(stderr, "models: %s: cull %s: write `cull back` or `cull none`; the line is ignored\n",
                             path.c_str(), b.c_str());
            }
        } else if (a == "pose_degrees") {
            char *end = nullptr;
            const float degrees = std::strtof(b.c_str(), &end);
            if (end != b.c_str() && *end == '\0' && degrees >= 0.0F) {
                bones.pose_degrees = degrees;
            } else {
                std::fprintf(
                    stderr,
                    "models: %s: pose_degrees %s: write `pose_degrees <degrees, 0 or more>`; the line is ignored\n",
                    path.c_str(), b.c_str());
            }
        } else if (a == "mesh_scale") {
            MeshScale s{b, {}};
            if (words >> s.by[0] >> s.by[1] >> s.by[2] && s.by[0] >= 0.0F && s.by[1] >= 0.0F && s.by[2] >= 0.0F) {
                bones.mesh_scales.push_back(s);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `mesh_scale <joint> <x> <y> <z>` (each 0 or more, in the joint's own "
                             "axes); the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "ik") {
            LimbLine limb;
            limb.joints[0] = b;
            std::string x;
            bool ok = static_cast<bool>(words >> limb.joints[1] >> limb.joints[2]);
            if (ok && words >> x) {
                limb.exact = x == "exact";
                ok = limb.exact;
                if (ok && words >> limb.offset[0])
                    ok = static_cast<bool>(words >> limb.offset[1] >> limb.offset[2]);
            }
            if (ok) {
                bones.limbs.push_back(limb);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `ik <upper joint> <lower joint> <end joint> [exact [<x> <y> <z>]]` "
                             "(the offset in centimetres); the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "place") {
            std::array<float, 3> at{};
            if (words >> at[0] && !(words >> at[1] >> at[2])) {
                std::fprintf(stderr,
                             "models: %s: write `place <joint> [<x> <y> <z>]` (the offset in the game's centimetres); "
                             "the line is ignored\n",
                             path.c_str());
            } else {
                bones.places[b] = at;
            }
        } else if (a == "hide") {
            HideLine h;
            h.joints.push_back(b);
            std::string w;
            bool ok = false;
            while (words >> w) {
                if (w == "from") {
                    ok = static_cast<bool>(words >> h.top) && static_cast<bool>(words >> w) && w == "to" &&
                         static_cast<bool>(words >> h.bottom) && h.bottom < h.top;
                    if (ok && words >> w)
                        ok = w == "fade" && static_cast<bool>(words >> h.fade) && h.fade >= 0.0F;
                    break;
                }
                h.joints.push_back(w);
            }
            if (ok) {
                bones.hides.push_back(std::move(h));
            } else {
                std::fprintf(stderr,
                             "models: %s: write `hide <joint>... from <top> to <bottom> [fade <length>]` (the model's "
                             "own centimetres at rest, the top above the bottom); the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "spine") {
            std::string end, word, hips;
            if (words >> end && (!(words >> word) || (word == "hips" && words >> hips))) {
                bones.spine = {b, end, hips};
            } else {
                std::fprintf(stderr,
                             "models: %s: write `spine <base joint> <end joint> [hips <joint>]`; the line is "
                             "ignored\n",
                             path.c_str());
            }
        } else if (a == "twist") {
            TwistLine t;
            t.joints[0] = b;
            bool ok = static_cast<bool>(words >> t.joints[1] >> t.joints[2] >> t.joints[3]);
            if (ok && !(words >> t.share))
                t.share = 1.0F;
            if (ok && t.share >= 0.0F && t.share <= 1.0F) {
                bones.twists.push_back(t);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `twist <joint> <by joint> <axis from joint> <axis to joint> "
                             "[<share, 0 to 1>]`; the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "cuff_fit") {
            // cuff_fit <clearance cm> <most open degrees> <shortest> <neighbours> <band from> <band to> <most shift cm>
            float clearance = 0.0F;
            khdays::models::CuffFit::Settings given = bones.cuff_settings;
            float most = 0.0F, slide = 0.0F;
            std::istringstream first(b);
            if ((first >> clearance) &&
                (words >> most >> given.shortest >> given.neighbours >> given.band_from >> given.band_to >> slide)) {
                given.most_shift = slide * 0.01F;
                given.clearance = clearance * 0.01F;
                given.most_open = most * 0.0174532925F;
                bones.cuff_settings = given;
            } else {
                std::fprintf(
                    stderr,
                    "models: %s: write `cuff_fit <clearance cm> <most open degrees> <shortest> <neighbours> <band "
                    "from> <band to> <most shift cm>`; the line is ignored\n",
                    path.c_str());
            }
        } else if (a == "cuff_thumb") {
            // cuff_thumb <more clearance cm> [<most open degrees>]: the last flap of each `cuff`
            float clearance = 0.0F, most = 0.0F;
            std::istringstream first(b);
            if (first >> clearance) {
                words >> most;
                bones.cuff_settings.own_flap = khdays::models::CuffFit::kFlaps - 1;
                bones.cuff_settings.own_clearance = clearance * 0.01F;
                bones.cuff_settings.own_most_open = most * 0.0174532925F;
            } else {
                std::fprintf(stderr,
                             "models: %s: write `cuff_thumb <more clearance cm> [<most open degrees>]`; the line is "
                             "ignored\n",
                             path.c_str());
            }
        } else if (a == "cuff_pose") {
            // cuff_pose <the hand's bend degrees> [<its pivot past its head, cm>], toward each `cuff`'s third flap;
            // `cuff_pose_bone`s say the pose
            std::istringstream first(b);
            float pivot = 0.0F;
            if (!(first >> bones.cuff_settings.pose_bend) || bones.cuff_settings.pose_bend <= 0.0F) {
                bones.cuff_settings.pose_bend = 0.0F;
                std::fprintf(
                    stderr,
                    "models: %s: write `cuff_pose <the hand's bend, degrees> [<its pivot past its head, cm>]`; "
                    "the line is ignored\n",
                    path.c_str());
            } else if (words >> pivot) {
                bones.cuff_settings.pose_pivot = pivot * 0.01F;
            }
        } else if (a == "cuff_pose_bone") {
            CuffPoseLine p;
            p.joint = b;
            if (words >> p.degrees[0] >> p.degrees[1] >> p.degrees[2] >> p.scale[0] >> p.scale[1] >> p.scale[2])
                bones.cuff_pose.push_back(p);
            else
                std::fprintf(stderr,
                             "models: %s: write `cuff_pose_bone <joint> <x> <y> <z degrees> <scale x> <y> <z>`; the "
                             "line is ignored\n",
                             path.c_str());
        } else if (a == "cuff") {
            CuffLine c;
            c.joints[0] = b;
            bool ok = true;
            for (std::size_t i = 1; i < c.joints.size(); ++i)
                ok = ok && static_cast<bool>(words >> c.joints[i]);
            if (ok)
                bones.cuffs.push_back(c);
            else
                std::fprintf(
                    stderr,
                    "models: %s: write `cuff <root joint> <hand joint> <flap> <flap> <flap> <flap>`, the flaps "
                    "in order round the ring; the line is ignored\n",
                    path.c_str());
        } else if (a == "swing") {
            TwistLine t;
            t.swing = true;
            t.joints[0] = b;
            bool ok = static_cast<bool>(words >> t.joints[1] >> t.joints[2]);
            t.joints[3] = t.joints[2];
            if (ok && !(words >> t.share))
                t.share = 1.0F;
            else if (ok)
                words >> t.except;
            if (ok && t.share >= 0.0F && t.share <= 1.0F) {
                bones.twists.push_back(t);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `swing <joint> <by joint> <frame joint> [<share, 0 to 1> [<except "
                             "toward joint>]]`; the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "held") { // held <joint>: a weapon posed by the weapon motions of the body holding it
            bones.held = b;
            std::string how;
            bones.held_rest = words >> how && how == "rest"; // ... rest: its rest is the holding bone's, no motions
        } else if (a == "align") {
            std::string kind;
            if (words >> kind && (kind == "foot" || kind == "hand")) {
                bones.aligns[b] = kind;
            } else {
                std::fprintf(stderr, "models: %s: write `align <joint> foot|hand`; the line is ignored\n",
                             path.c_str());
            }
        } else {
            bones.pairs[a] = b;
        }
    }
    return bones;
}

// The HD replacement switch (hud_settings.h) a bones.txt's `switch` names; -1 none (drawn always).
int setting_named(const std::string &name, const std::string &key)
{
    if (key.empty())
        return -1;
    for (int s = 0; s < KHDAYS_OPT_COUNT; ++s)
        if (key == khdays_hud_setting_keys[s])
            return s;
    std::fprintf(
        stderr,
        "models: %s: switch %s is no HD replacement's; name a key of hud_settings.h in bones.txt; drawn always\n",
        name.c_str(), key.c_str());
    return -1;
}

// The game's own shapes a bones.txt's `keep` keeps drawn: each whose vertices all hang from the nodes named (their
// lists' places in the model).
std::set<std::uint32_t> kept_lists(const std::string &name, const khdays::assets::NeutralModel &game,
                                   const std::vector<std::string> &keep)
{
    std::set<std::uint32_t> nodes, lists;
    for (const auto &node : keep) {
        const auto at = std::find(game.object_names.begin(), game.object_names.end(), node);
        if (at == game.object_names.end())
            std::fprintf(
                stderr,
                "models: %s: no node %s to keep; check the `keep` line against the game's names (--dump-graphics)\n",
                name.c_str(), node.c_str());
        else
            nodes.insert(static_cast<std::uint32_t>(at - game.object_names.begin()));
    }
    if (nodes.empty())
        return lists;
    for (const auto &mesh : game.meshes) {
        bool kept = !mesh.vertices.empty();
        for (const auto &v : mesh.vertices) {
            const auto entry = static_cast<std::size_t>(v.joints[0]);
            const auto &from = entry < game.palette_bones.size() ? game.palette_bones[entry]
                                                                 : decltype(game.palette_bones)::value_type{};
            kept = kept && !from.empty() &&
                   std::all_of(from.begin(), from.end(), [&](const auto &b) { return nodes.count(b.first) != 0; });
        }
        if (kept)
            lists.insert(mesh.list_offset);
    }
    return lists;
}

// The model's size at rest: each vertex where its joints' binds put it (an unbound one at the model's scale).
void set_rest_size(Model &model)
{
    const float k = model.scale;
    model.low = {INFINITY, INFINITY, INFINITY};
    model.high = {-INFINITY, -INFINITY, -INFINITY};
    for (const Mesh &mesh : model.meshes)
        for (const auto &v : mesh.vertices) {
            std::array<float, 3> p{0, 0, 0};
            float total = 0.0F;
            for (int w = 0; w < 4; ++w) {
                const float weight = v.weights[static_cast<std::size_t>(w)];
                const std::uint32_t jt = v.joints[static_cast<std::size_t>(w)];
                if (weight <= 0.0F || jt >= model.joints.size())
                    continue;
                const auto q = point(model.joints[jt].bind, v.position);
                for (int c = 0; c < 3; ++c)
                    p[static_cast<std::size_t>(c)] += weight * q[static_cast<std::size_t>(c)];
                total += weight;
            }
            if (total <= 0.0F)
                p = {v.position[0] * k, v.position[1] * k, v.position[2] * k};
            else
                for (auto &c : p)
                    c /= total;
            for (std::size_t c = 0; c < 3; ++c) {
                model.low[c] = std::fmin(model.low[c], p[c]);
                model.high[c] = std::fmax(model.high[c], p[c]);
            }
        }
}

// A motions.txt `ds_pose` line: bones turned from rest as a slot's pose turns them (its frame, 30 a second),
// or by degrees about each bone's own x, then y, then z; for the game animations named, else all
struct DsPoseRule {
    std::vector<int> bones;
    int slot = -1;
    float frame = 0.0F;
    std::array<float, 3> degrees{};
    std::vector<std::string> animations;
};

// A KH2 bone's frame (centimetres) in metres, as the model's joints are
Mat in_metres(Mat g)
{
    g[12] *= 0.01F;
    g[13] *= 0.01F;
    g[14] *= 0.01F;
    return g;
}

// A KH2 mset slot made for the model's own bones (an `own` mset's, or every slot without a `skeleton`)
bool own_slot(const Model &m, std::size_t slot)
{
    if (!m.kh2_retarget)
        return true;
    for (const auto &[from, to] : m.kh2_own_slots)
        if (slot >= from && slot < to)
            return true;
    return false;
}

// The skeleton a slot's motion plays on, and its pose given to the model's bones (retargeted unless its own)
const khdays::assets::kh2::MotionSkeleton &skeleton_of(const Model &m, std::size_t slot)
{
    return own_slot(m, slot) ? *m.kh2_own_skeleton : *m.kh2_skeleton;
}

void pose_on_model(const Model &m, std::size_t slot, const std::vector<khdays::assets::kh2::Mat4> &played,
                   std::vector<khdays::assets::kh2::Mat4> &out)
{
    if (own_slot(m, slot))
        out = played;
    else
        m.kh2_retarget->apply(played, out);
}

// A track's player for a slot, made anew when the slot plays on another skeleton than its last
void ready_player(const Model &m, Kh2Track &track, std::size_t slot)
{
    const khdays::assets::kh2::MotionSkeleton &skeleton = skeleton_of(m, slot);
    if (!track.player || track.skeleton != &skeleton) {
        track.player = std::make_unique<khdays::assets::kh2::MotionPlayer>(skeleton);
        track.skeleton = &skeleton;
    }
}

// A turn of `degrees` about x, then y, then z (each about the fixed axes of the frame it is applied in)
Mat turn_of(const std::array<float, 3> &degrees)
{
    Mat turn = kIdentity;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float r = degrees[axis] * 0.0174532925F, c = std::cos(r), s = std::sin(r);
        const std::size_t u = (axis + 1) % 3, v = (axis + 2) % 3;
        Mat one = kIdentity;
        one[u * 4 + u] = c;
        one[v * 4 + v] = c;
        one[u * 4 + v] = s;
        one[v * 4 + u] = -s;
        turn = mul(one, turn);
    }
    return turn;
}

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

std::size_t hand_pose_index(const std::string &name)
{
    using namespace khdays::models;
    return name == "relaxed" ? kHandRelaxed
           : name == "open"  ? kHandOpen
           : name == "fist"  ? kHandFist
           : name == "flat"  ? kHandFlat
           : name == "grip"  ? kHandGrip
                             : kHandPoses;
}

// The rules as each joint's turn per game animation (Model::ds_turns): later lines win for a bone, and an
// animation's own lines over the default's
void resolve_ds_poses(Model &model, const std::vector<DsPoseRule> &rules,
                      const std::function<void(const std::string &)> &say)
{
    namespace kh2 = khdays::assets::kh2;
    std::map<std::string, std::map<int, std::size_t>> scopes; // animation -> bone -> rule
    for (std::size_t i = 0; i < rules.size(); ++i) {
        const std::vector<std::string> names =
            rules[i].animations.empty() ? std::vector<std::string>{""} : rules[i].animations;
        for (const std::string &a : names)
            for (const int b : rules[i].bones)
                scopes[a][b] = i;
    }
    if (const auto all = scopes.find(""); all != scopes.end())
        for (auto &[a, bones] : scopes)
            if (!a.empty())
                for (const auto &[b, i] : all->second)
                    bones.emplace(b, i); // the default's, unless the animation's own lines name the bone
    std::map<std::pair<int, float>, std::vector<kh2::Mat4>> poses; // a slot's pose on this model's bones
    const auto pose_of = [&](int slot, float frame) -> const std::vector<kh2::Mat4> * {
        const auto key = std::make_pair(slot, frame);
        if (const auto f = poses.find(key); f != poses.end())
            return &f->second;
        const kh2::Motion *motion =
            model.kh2_motions ? model.kh2_motions->motion(static_cast<std::size_t>(slot)) : nullptr;
        if (motion == nullptr)
            return nullptr;
        std::vector<kh2::Mat4> &out = poses[key];
        try {
            kh2::MotionPlayer player(skeleton_of(model, static_cast<std::size_t>(slot)));
            player.evaluate(*motion, frame * 2.0F, kIdentity, nullptr);
            pose_on_model(model, static_cast<std::size_t>(slot), player.joints(), out);
        } catch (const std::exception &e) {
            say("ds_pose: mset slot " + std::to_string(slot) + " cannot be played (" + e.what() +
                "); replace the mset or name another slot");
            out.clear();
        }
        return &out;
    };
    std::set<std::string> told;
    for (const auto &[a, bones] : scopes) {
        auto &turns = model.ds_turns[a];
        for (const auto &[b, i] : bones) {
            const DsPoseRule &rule = rules[i];
            const std::string bone = "ds_pose: bone " + std::to_string(b);
            const auto it = std::find(model.kh2_bone.begin(), model.kh2_bone.end(), b);
            if (it == model.kh2_bone.end()) {
                if (told.insert(bone).second)
                    say(bone + ": the model has no such bone; check the line's numbers; the line leaves it");
                continue;
            }
            const auto j = static_cast<std::size_t>(it - model.kh2_bone.begin());
            const Joint &joint = model.joints[j];
            if (joint.node >= 0 || joint.parent < 0) {
                if (told.insert(bone).second)
                    say(bone +
                        (joint.node >= 0 ? ": it follows a game node (bones.txt), which poses it"
                                         : ": it has no parent to turn from") +
                        "; take it off the line; the line leaves it");
                continue;
            }
            const auto p = static_cast<std::size_t>(joint.parent);
            Mat turn = turn_of(rule.degrees);
            if (rule.slot >= 0) {
                const std::vector<kh2::Mat4> *g = pose_of(rule.slot, rule.frame);
                const int pb = model.kh2_bone[p];
                if (g == nullptr || pb < 0 || static_cast<std::size_t>(b) >= g->size() ||
                    static_cast<std::size_t>(pb) >= g->size()) {
                    if (told.insert(bone + " slot").second)
                        say(bone + ": mset slot " + std::to_string(rule.slot) +
                            " gives it no pose (no such motion, or no mset); name a slot with a motion; the line "
                            "leaves it");
                    continue;
                }
                // its turn from its parent in the pose, against the same at rest, in its own axes
                const Mat posed = mul(inverse(axes_of((*g)[static_cast<std::size_t>(pb)])),
                                      axes_of((*g)[static_cast<std::size_t>(b)]));
                const Mat rest =
                    mul(inverse(axes_of(inverse(model.joints[p].inverse_bind))), axes_of(inverse(joint.inverse_bind)));
                turn = mul(inverse(rest), posed);
            }
            // about its head, in the space the joints' K take (the model at rest): B turn B^-1
            const Mat frame = mul(joint.bind, inverse(joint.inverse_bind));
            turns.emplace_back(j, mul(frame, mul(turn, inverse(frame))));
        }
    }
}

void resolve_hands(Model &model, const std::vector<HandRule> &rules, const std::vector<HandPoseRule> &poses,
                   const std::vector<HandBodyRule> &body, const std::function<void(const std::string &)> &say)
{
    using khdays::models::kHandPoses;
    const auto joint_of = [&](int bone) {
        const auto it = std::find(model.kh2_bone.begin(), model.kh2_bone.end(), bone);
        return it != model.kh2_bone.end() ? static_cast<int>(it - model.kh2_bone.begin()) : -1;
    };
    const auto frame_of = [&](std::size_t j) {
        return mul(model.joints[j].bind, inverse(model.joints[j].inverse_bind));
    };
    const auto under = [&](int j, std::size_t top) {
        for (; j >= 0; j = model.joints[static_cast<std::size_t>(j)].parent)
            if (static_cast<std::size_t>(j) == top)
                return true;
        return false;
    };
    static const char *const kPoseNames[] = {"relaxed", "open", "fist", "flat", "grip"};
    for (const HandRule &rule : rules) {
        const int hand = joint_of(rule.hand), forearm = joint_of(rule.forearm);
        if (hand < 0 || forearm < 0 || model.joints[static_cast<std::size_t>(forearm)].parent < 0) {
            say("hand: the model has no bone " + std::to_string(hand < 0 ? rule.hand : rule.forearm) +
                " or it has no parent; check the `hand` line's bone numbers; the line is ignored");
            continue;
        }
        const float back = length(rule.back);
        if (back <= 0.0F) {
            say("hand " + std::to_string(rule.hand) +
                ": `back` has no length; write the axis out of the back of the hand in its own axes; the line is ignored");
            continue;
        }
        Model::Hand h;
        h.joint = static_cast<std::size_t>(hand);
        h.forearm = static_cast<std::size_t>(forearm);
        h.upper = static_cast<std::size_t>(model.joints[h.forearm].parent);
        h.node = model.joints[h.joint].node;
        h.frame = frame_of(h.joint);
        h.forearm_frame = frame_of(h.forearm);
        h.back = {rule.back[0] / back, rule.back[1] / back, rule.back[2] / back};
        const auto &hh = model.joints[h.joint].head, &fh = model.joints[h.forearm].head;
        const std::array<float, 3> way = {hh[0] - fh[0], hh[1] - fh[1], hh[2] - fh[2]};
        const float reach = length(way);
        if (reach <= 0.0F) {
            say("hand " + std::to_string(rule.hand) +
                ": the hand and forearm bones are in one place; name the bones at the elbow and wrist; the line is ignored");
            continue;
        }
        h.forearm_way = vec(inverse(axes_of(h.forearm_frame)), {way[0] / reach, way[1] / reach, way[2] / reach});
        std::vector<int> fingers = rule.fingers;
        std::sort(fingers.begin(), fingers.end(), [&](int a, int b) { return joint_of(a) < joint_of(b); });
        for (const int bone : fingers) {
            const int j = joint_of(bone);
            if (j < 0 || model.joints[static_cast<std::size_t>(j)].node >= 0 ||
                model.joints[static_cast<std::size_t>(j)].parent < 0) {
                say("hand: finger bone " + std::to_string(bone) +
                    (j < 0 ? ": the model has no such bone; check the `hand` line's numbers"
                           : ": it follows a game node (bones.txt) or has no parent; take it off the `hand` line") +
                    "; the bone is left out");
                continue;
            }
            Model::HandFinger f;
            f.joint = static_cast<std::size_t>(j);
            f.parent = static_cast<std::size_t>(model.joints[f.joint].parent);
            f.frame = frame_of(f.joint);
            f.frame_inverse = inverse(f.frame);
            f.poses.fill({0.0F, 0.0F, 0.0F, 1.0F});
            for (const HandPoseRule &pose : poses)
                if (std::find(pose.bones.begin(), pose.bones.end(), bone) != pose.bones.end())
                    f.poses[pose.pose] = khdays::models::frame_detail::quat_of(turn_of(pose.degrees));
            h.fingers.push_back(f);
        }
        // each finger: a run of joints from one whose parent is no finger joint; the first (in the model's order) is
        // taken as the thumb. Its spheres at its joints that are apart, and past its last by half that bone
        const auto is_finger = [&](std::size_t j) {
            return std::any_of(h.fingers.begin(), h.fingers.end(), [&](const auto &f) { return f.joint == j; });
        };
        for (std::size_t i = 0; i < h.fingers.size(); ++i) {
            if (is_finger(h.fingers[i].parent)) {
                h.digits.back().last = i;
                continue;
            }
            Model::HandDigit digit;
            digit.first = digit.last = i;
            h.digits.push_back(digit);
        }
        for (std::size_t n = 0; n < h.digits.size(); ++n) {
            Model::HandDigit &digit = h.digits[n];
            digit.thumb = n == 0;
            std::array<float, 3> last = model.joints[h.fingers[digit.first].joint].head;
            std::array<float, 3> along{};
            digit.spheres.emplace_back(h.fingers[digit.first].joint, last);
            for (std::size_t i = digit.first + 1; i <= digit.last; ++i) {
                const auto &at = model.joints[h.fingers[i].joint].head;
                const std::array<float, 3> d = {at[0] - last[0], at[1] - last[1], at[2] - last[2]};
                if (length(d) < 0.001F)
                    continue;
                along = d;
                last = at;
                digit.spheres.emplace_back(h.fingers[i].joint, at);
            }
            digit.spheres.emplace_back(
                h.fingers[digit.last].joint,
                std::array<float, 3>{last[0] + along[0] * 0.8F, last[1] + along[1] * 0.8F, last[2] + along[2] * 0.8F});
        }
        // the palm: the wrist and the fingers' roots (all but the thumb's), what the arm moves out of things
        std::array<float, 3> sum = hh;
        float n = 1.0F;
        h.samples.push_back({hh[0], hh[1], hh[2], 0.025F});
        for (std::size_t d = 1; d < h.digits.size(); ++d) {
            const auto &root = model.joints[h.fingers[h.digits[d].first].joint].head;
            sum = {sum[0] + root[0], sum[1] + root[1], sum[2] + root[2]};
            n += 1.0F;
            h.samples.push_back({root[0], root[1], root[2], 0.012F});
        }
        h.palm = {sum[0] / n, sum[1] / n, sum[2] / n};
        h.samples.push_back({h.palm[0], h.palm[1], h.palm[2], 0.022F});
        for (std::size_t j = 0; j < model.joints.size(); ++j) {
            const int at = static_cast<int>(j);
            if (under(at, h.joint))
                h.hand_joints.push_back(j);
            else if (under(at, h.forearm))
                h.forearm_joints.push_back(j);
            else if (under(at, h.upper))
                h.upper_joints.push_back(j);
        }
        model.hands.push_back(std::move(h));
    }
    if (model.hands.empty())
        return;
    for (std::size_t p = 0; p < kHandPoses; ++p)
        if (std::none_of(poses.begin(), poses.end(), [&](const HandPoseRule &r) { return r.pose == p; }))
            say(std::string("hand: no `hand_pose ") + kPoseNames[p] + "` lines: that pose is the fingers at rest");
    // a game node on a hand: under the hand's node, or carried by a joint under the hand's (its `weapon` line)
    model.node_hand.assign(model.node_names.size(), -1);
    for (std::size_t g = 0; g < model.node_hand.size(); ++g)
        for (std::size_t i = 0; i < model.hands.size(); ++i) {
            bool on = g < model.kh2_weapon.size() && model.kh2_weapon[g] >= 0 &&
                      under(model.kh2_weapon[g], model.hands[i].joint);
            int a = static_cast<int>(g);
            for (std::size_t up = 0; !on && a >= 0 && up < model.node_parent.size(); ++up) {
                on = a == model.hands[i].node;
                a = model.node_parent[static_cast<std::size_t>(a)];
            }
            if (on)
                model.node_hand[g] = static_cast<int>(i);
        }
    // the body's parts: their vertices as they stand at rest
    for (const HandBodyRule &rule : body) {
        const int first = joint_of(rule.bones.front()), from = joint_of(rule.from), to = joint_of(rule.to);
        if (first < 0 || from < 0 || to < 0) {
            say("hand_body: a bone of the line is not in the model; check its numbers; the line is ignored");
            continue;
        }
        std::vector<khdays::models::HandVec> points;
        for (const Mesh &mesh : model.meshes)
            for (const auto &v : mesh.vertices) {
                const std::uint32_t j = v.joints[heaviest_weight(v)];
                if (j < model.kh2_bone.size() &&
                    std::find(rule.bones.begin(), rule.bones.end(), model.kh2_bone[j]) != rule.bones.end())
                    points.push_back(point(model.joints[j].bind, v.position));
            }
        Model::HandBody part;
        part.joint = static_cast<std::size_t>(first);
        part.margin = rule.margin * 0.01F;
        part.shape.bake(model.joints[static_cast<std::size_t>(from)].head,
                        model.joints[static_cast<std::size_t>(to)].head, points);
        if (points.empty())
            say("hand_body: no vertices are carried by bone " + std::to_string(rule.bones.front()) +
                " and the others of its line; check their numbers");
        model.hand_body.push_back(std::move(part));
    }
}

// bones.txt's `cuff` lines: the cuff's surface and flaps at rest, and the hand's vertices that must stay inside it
void resolve_cuffs(Model &model, const std::vector<CuffLine> &lines, const std::vector<CuffPoseLine> &pose_lines,
                   const std::string &name)
{
    using namespace khdays::models;
    const auto subtree = [&](std::size_t top) {
        std::vector<std::size_t> out;
        std::vector<char> under(model.joints.size(), 0);
        for (std::size_t j = 0; j < model.joints.size(); ++j) {
            const int up = model.joints[j].parent;
            under[j] = j == top || (up >= 0 && under[static_cast<std::size_t>(up)]) ? 1 : 0;
            if (under[j])
                out.push_back(j);
        }
        return out;
    };
    for (const CuffLine &line : lines) {
        std::array<int, 6> at{};
        bool found = true;
        for (std::size_t i = 0; i < at.size(); ++i) {
            at[i] = joint_named(model, line.joints[i]);
            if (at[i] < 0) {
                khdays_log("models: %s: cuff: no joint %s; check its name in bones.txt; the line is ignored",
                           name.c_str(), line.joints[i].c_str());
                found = false;
            }
        }
        if (!found)
            continue;
        Model::Cuff cuff;
        cuff.root = static_cast<std::size_t>(at[0]);
        cuff.hand = static_cast<std::size_t>(at[1]);
        std::vector<int> flap_of(model.joints.size(), -1);
        std::array<HandVec, CuffFit::kFlaps> hinges{}, tips{};
        std::array<std::size_t, CuffFit::kFlaps> tip_joint{};
        for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
            const auto top = static_cast<std::size_t>(at[f + 2]);
            cuff.flap_joints[f] = subtree(top);
            hinges[f] = model.joints[top].head;
            // its end: the joint furthest from its root; what it carries bends there
            float furthest = -1.0F;
            for (const std::size_t j : cuff.flap_joints[f]) {
                flap_of[j] = static_cast<int>(f);
                const float l = length(hand_sub(model.joints[j].head, hinges[f]));
                if (l > furthest) {
                    furthest = l;
                    tip_joint[f] = j;
                }
            }
            tips[f] = model.joints[tip_joint[f]].head;
            cuff.tip_joints[f] = subtree(tip_joint[f]);
        }
        cuff.slides = subtree(
            model.joints[cuff.root].parent >= 0 ? static_cast<std::size_t>(model.joints[cuff.root].parent) : cuff.root);
        std::vector<char> ring(model.joints.size(), 0); // what the cuff's surface is: the flaps and the root's own
        for (const std::size_t j : subtree(cuff.root))
            ring[j] = 1;
        if (const int up = model.joints[cuff.root].parent; up >= 0)
            ring[static_cast<std::size_t>(up)] = 1;
        std::vector<char> on_hand(model.joints.size(), 0);
        for (const std::size_t j : subtree(static_cast<std::size_t>(at[1])))
            on_hand[j] = 1;
        std::vector<CuffFit::Triangle> triangles;
        std::vector<std::array<std::size_t, 3>> triangle_joints; // each corner's joint
        std::set<std::array<int, 3>> taken; // the hand's vertices, thinned to a centimetre apart at rest
        for (const Mesh &mesh : model.meshes) {
            for (const auto &v : mesh.vertices) {
                const std::uint32_t j = v.joints[heaviest_weight(v)];
                if (j >= on_hand.size() || !on_hand[j])
                    continue;
                const auto rest = point(model.joints[j].bind, v.position);
                const std::array<int, 3> cell = {static_cast<int>(std::floor(rest[0] * 100.0F)),
                                                 static_cast<int>(std::floor(rest[1] * 100.0F)),
                                                 static_cast<int>(std::floor(rest[2] * 100.0F))};
                if (taken.insert(cell).second)
                    cuff.points.emplace_back(static_cast<std::size_t>(j), v.position);
            }
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                std::array<std::size_t, 3> joints{};
                bool all_ring = true, any_flap = false;
                CuffFit::Triangle triangle;
                for (std::size_t i = 0; i < 3; ++i) {
                    const auto &v = mesh.vertices[mesh.indices[t + i]];
                    const std::uint32_t j = v.joints[heaviest_weight(v)];
                    all_ring = all_ring && j < ring.size() && ring[j];
                    const int flap = j < flap_of.size() ? flap_of[j] : -1;
                    any_flap = any_flap || flap >= 0;
                    joints[i] = j;
                    if (j < model.joints.size())
                        triangle.at[i] = point(model.joints[j].bind, v.position);
                    triangle.flap[i] = flap;
                }
                if (all_ring && any_flap) {
                    triangles.push_back(triangle);
                    triangle_joints.push_back(joints);
                }
            }
        }
        cuff.fit.bake(model.joints[cuff.root].head, hinges, tips, triangles);
        if (model.cuff_settings.pose_bend > 0.0F && !pose_lines.empty() && cuff.fit.ready) {
            // the flaps' bones, parents first: a rest frame is the joint's axes at its head, in the model's own space
            const auto frame_at = [&](std::size_t j) {
                Mat f = axes_of(inverse(model.joints[j].inverse_bind));
                f[12] = model.joints[j].head[0];
                f[13] = model.joints[j].head[1];
                f[14] = model.joints[j].head[2];
                return f;
            };
            std::vector<int> among(model.joints.size(), -1);
            const int ring_joint = model.joints[cuff.root].parent;
            for (std::size_t j = 0; j < model.joints.size(); ++j) {
                if (flap_of[j] < 0 && j != cuff.root && static_cast<int>(j) != ring_joint)
                    continue;
                CuffPoseBone bone;
                bone.joint = j;
                const int up = model.joints[j].parent;
                bone.up = up >= 0 ? among[static_cast<std::size_t>(up)] : -1;
                bone.up_rest = up >= 0 ? frame_at(static_cast<std::size_t>(up)) : kIdentity;
                bone.local = mul(inverse(bone.up_rest), frame_at(j));
                bone.back = inverse(frame_at(j));
                for (const CuffPoseLine &p : pose_lines)
                    if (p.joint == model.joints[j].name) {
                        bone.turn = frame_detail::quat_of(turn_of(p.degrees));
                        bone.scale = p.scale;
                    }
                among[j] = static_cast<int>(cuff.pose.size());
                cuff.pose.push_back(bone);
            }
            if (ring_joint >= 0)
                cuff.pose_ring = among[static_cast<std::size_t>(ring_joint)];
            for (std::size_t f = 0; f < CuffFit::kFlaps; ++f)
                for (const std::size_t j : cuff.flap_joints[f])
                    cuff.flap_pose[f].push_back(among[j]);
            const Mat hand_axes = axes_of(inverse(model.joints[cuff.hand].inverse_bind));
            cuff.hand_way = hand_unit({hand_axes[0], hand_axes[1], hand_axes[2]});
            const HandVec back = cuff.fit.out[2];
            cuff.toward = hand_unit(hand_add(back, cuff.hand_way, -hand_dot(back, cuff.hand_way)));
            std::vector<Mat> moved;
            for (int s = 0; s < kCuffSurfaces; ++s) {
                cuff_pose_at(cuff.pose, static_cast<float>(s) / static_cast<float>(kCuffSurfaces - 1), moved);
                const auto at_share = [&](std::size_t j, const HandVec &p) {
                    return among[j] >= 0 ? point(moved[static_cast<std::size_t>(among[j])], p) : p;
                };
                std::array<HandVec, CuffFit::kFlaps> posed_hinges{}, posed_tips{};
                for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
                    posed_hinges[f] = at_share(static_cast<std::size_t>(at[f + 2]), hinges[f]);
                    posed_tips[f] = at_share(tip_joint[f], tips[f]);
                }
                std::vector<CuffFit::Triangle> posed = triangles;
                for (std::size_t t = 0; t < posed.size(); ++t)
                    for (std::size_t i = 0; i < 3; ++i)
                        posed[t].at[i] = at_share(triangle_joints[t][i], posed[t].at[i]);
                cuff.surfaces.emplace_back();
                cuff.surfaces.back().bake(at_share(cuff.root, model.joints[cuff.root].head), posed_hinges, posed_tips,
                                          posed);
            }
        }
        if (!cuff.fit.ready || cuff.points.empty()) {
            khdays_log(
                "models: %s: cuff %s: %zu triangles and %zu of the hand's vertices: check the line's joints; the line "
                "is ignored",
                name.c_str(), line.joints[0].c_str(), triangles.size(), cuff.points.size());
            continue;
        }
        khdays_log("models: %s: cuff %s fitted round %zu of %s's vertices (%zu triangles, %.1f to %.1f cm along)",
                   name.c_str(), line.joints[0].c_str(), cuff.points.size(), line.joints[1].c_str(), triangles.size(),
                   static_cast<double>(cuff.fit.from * 100.0F), static_cast<double>(cuff.fit.to * 100.0F));
        model.cuffs.push_back(std::move(cuff));
    }
}

// A KH2 model's own motions: `<model>.motions.txt` beside the .mdlx (docs/KH2_MODELS.md, "Motions"): the mset, the
// sections of the game's animations that play its slots, and what its other lines ask for. A game animation without a
// line, or a slot with no motion, keeps the game's.
void load_kh2_motions(Model &model, const std::string &path)
{
    namespace kh2 = khdays::assets::kh2;
    const auto file = u8(path);
    if (file.extension() != ".mdlx" && file.extension() != ".MDLX")
        return;
    const auto list = file.parent_path() / (file.stem().u8string() + u8".motions.txt");
    std::ifstream in(list);
    if (!in)
        return;
    const auto say = [&](const std::string &what) {
        std::fprintf(stderr, "models: %s: %s\n", path.c_str(), what.c_str());
        khdays_log("models: %s: %s", path.c_str(), what.c_str());
    };
    std::string mset, line;
    std::map<std::string, std::vector<Kh2Section>> sections;
    std::vector<std::pair<std::string, int>> weapons;           // the game's joint, the KH2 bone
    std::map<std::string, std::array<float, 3>> weapon_degrees; // its `rotate`, by the game's joint
    std::vector<DsPoseRule> ds_rules;
    std::vector<HandRule> hand_rules;
    std::vector<HandPoseRule> hand_poses;
    std::vector<HandBodyRule> hand_body;
    std::string skeleton;                   // the mdlx the mset was made for, when not this one
    std::vector<std::pair<int, int>> bones; // this model's bone, the skeleton's
    std::vector<khdays::assets::kh2::Retarget::Limb> limbs;
    struct Extra {
        std::string file,
            entry; // `motions <file> [<entry>] at <slot> [own]`: an entry of a BAR of sets (GAMEOVER.mset)
        std::size_t at = 0;
        bool own = false; // made for this model's bones, not the `skeleton`'s
    };
    std::vector<Extra> extras;
    bool mset_own = false; // `mset <file> own`
    struct Carry {         // `carry <bone> <by bone> at <slot>`
        int bone = 0, by = 0;
        std::size_t at = 0;
    };
    std::vector<Carry> carries;
    std::string weapon_mset, weapon_mdlx; // `weapon_motions <mset> <mdlx> <bone>`
    int weapon_bone = -1;
    const auto number = [](const std::string &s, float &out) {
        char *end = nullptr;
        out = std::strtof(s.c_str(), &end);
        return end != s.c_str() && *end == '\0' && out >= 0.0F;
    };
    // a bone number or a range of them (`171-215`)
    const auto span = [](const std::string &s, int &from, int &to) {
        char *end = nullptr;
        const long a = std::strtol(s.c_str(), &end, 10);
        long b = a;
        if (end != s.c_str() && *end == '-') {
            const char *rest = end + 1;
            b = std::strtol(rest, &end, 10);
            if (end == rest)
                return false;
        }
        if (end == s.c_str() || *end != '\0' || a < 0 || b < a)
            return false;
        from = static_cast<int>(a);
        to = static_cast<int>(b);
        return true;
    };
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        std::string a, b;
        if (!(words >> a >> b))
            continue;
        // `walk|camp <game animation>[@<frame>] <slot> ...`: a line of its own, kept as the animation's under its own
        // name (`<animation>#walk`, `#camp`): played while the port's walk is on, or in the camp menu
        bool walk_line = false;
        if (a == "walk" || a == "camp") {
            std::string slot;
            if (!(words >> slot)) {
                say("\"" + line + "\": write `" + a + " <game animation> <slot> ...`; the line is ignored");
                continue;
            }
            walk_line = a == "walk";
            const auto at = b.find('@');
            a = b.substr(0, at) + "#" + a + (at != std::string::npos ? b.substr(at) : std::string());
            b = slot;
        }
        if (a == "mset") {
            std::string w;
            mset = b;
            mset_own = static_cast<bool>(words >> w) && w == "own";
            if (!w.empty() && w != "own")
                say("\"" + line + "\": write `mset <file> [own]`; read as `mset " + b + "`");
        } else if (a == "carry") { // carry <bone> <by bone> at <slot>
            Carry c;
            std::string by, w, at;
            int to = 0, slot = 0;
            const bool ok = static_cast<bool>(words >> by >> w >> at) && w == "at" && span(b, c.bone, to) &&
                            to == c.bone && span(by, c.by, to) && to == c.by && span(at, slot, to) && to == slot;
            c.at = static_cast<std::size_t>(slot);
            if (ok)
                carries.push_back(c);
            else
                say("\"" + line +
                    "\": write `carry <bone> <by bone> at <slot>` (the slot an `own` mset's); the line is ignored");
        } else if (a == "weapon_motions") { // weapon_motions <mset> <mdlx> <bone>
            std::string mdlx, bone;
            char *end = nullptr;
            if (!(words >> mdlx >> bone) || (weapon_bone = static_cast<int>(std::strtol(bone.c_str(), &end, 10)),
                                             end == bone.c_str() || *end || weapon_bone < 0)) {
                say("\"" + line +
                    "\": write `weapon_motions <mset> <mdlx> <bone>` (the bone: the weapon itself); "
                    "the line is ignored");
                weapon_bone = -1;
            } else {
                weapon_mset = b;
                weapon_mdlx = mdlx;
            }
        } else if (a == "motions") { // motions <file> [<entry>] at <slot> [own]
            Extra x{b, {}, 0, false};
            std::string w, v, o;
            if (words >> w && w != "at") {
                x.entry = w;
                words >> w;
            }
            char *end = nullptr;
            if (w != "at" || !(words >> v) || (x.at = std::strtoul(v.c_str(), &end, 10), end == v.c_str() || *end) ||
                (words >> o && o != "own"))
                say("\"" + line + "\": write `motions <file> [<entry>] at <first slot> [own]`; the line is ignored");
            else {
                x.own = o == "own";
                extras.push_back(x);
            }
        } else if (a == "root_motion") {
            model.kh2_root_motion = b == "on";
        } else if (a == "idle") { // idle <game animation>...
            std::string w;
            model.kh2_idle.push_back(b);
            while (words >> w)
                model.kh2_idle.push_back(w);
        } else if (a == "landing") {
            model.kh2_landing = b;
        } else if (a == "air") { // air <game animation> <slot> [land <slot>] [finish <slot>...] [blend <frames>]
            Kh2Section air, land;
            std::string slot, w, v;
            bool ok = static_cast<bool>(words >> slot);
            float number_of = 0.0F;
            ok = ok && number(slot, number_of) && number_of >= 0.0F;
            air.slot = static_cast<std::size_t>(number_of);
            air.realtime = true;
            bool has_land = false, finishing = false;
            while (ok && words >> w) {
                float n = 0.0F;
                if (w == "land" && words >> v && number(v, n) && n >= 0.0F) {
                    land.slot = static_cast<std::size_t>(n);
                    has_land = true;
                    finishing = false;
                } else if (w == "blend" && words >> v && number(v, n)) {
                    air.blend = land.blend = n;
                    finishing = false;
                } else if (w == "finish") {
                    land.finish = finishing = true;
                } else if (finishing && number(w, n) && n >= 0.0F) {
                    land.then.push_back(static_cast<std::size_t>(n));
                } else {
                    ok = false;
                }
            }
            if (!ok || (land.finish && !has_land)) {
                say("\"" + line +
                    "\": write `air <game animation> <slot> [land <slot>] [finish <slot>...] [blend <frames>]` "
                    "(`finish` after a `land`); the line is ignored");
                continue;
            }
            Kh2Air &entry = model.kh2_air[b];
            entry.air = {air};
            entry.land.clear();
            if (has_land)
                entry.land.push_back(land);
        } else if (a == "ik_adjust") {
            model.kh2_ik_adjust = b != "off";
        } else if (a == "skeleton") {
            skeleton = b;
        } else if (a == "bone") {
            std::string c;
            int t0 = 0, t1 = 0, s0 = 0, s1 = 0;
            if (!(words >> c) || !span(b, t0, t1) || !span(c, s0, s1) || t1 - t0 != s1 - s0) {
                say("\"" + line +
                    "\": write `bone <this model's bone> <the skeleton's bone>`, or two ranges as long (`bone 171-215 "
                    "115-159`); the line is ignored");
                continue;
            }
            for (int i = 0; i <= t1 - t0; ++i)
                bones.emplace_back(t0 + i, s0 + i);
        } else if (a == "ik") {
            std::string c, d, r;
            float u = 0.0F, l = 0.0F, e = 0.0F, reach = -1.0F;
            bool ok = (words >> c >> d) && number(b, u) && number(c, l) && number(d, e), lengths = false;
            while (ok && words >> r) // [<bone it reaches with>] [lengths]
                if (r == "lengths")
                    lengths = true;
                else
                    ok = reach < 0.0F && number(r, reach);
            if (!ok) {
                say("\"" + line +
                    "\": write `ik <upper bone> <lower bone> <end bone> [<bone it reaches with>] [lengths]`; the line "
                    "is ignored");
                continue;
            }
            limbs.push_back(
                {static_cast<int>(u), static_cast<int>(l), static_cast<int>(e), static_cast<int>(reach), lengths});
        } else if (a == "weapon") {
            std::string bone, r;
            float n = 0.0F;
            std::array<float, 3> by{};
            if (!(words >> bone) || !number(bone, n) ||
                (words >> r && !(r == "rotate" && words >> by[0] >> by[1] >> by[2]))) {
                say("\"" + line +
                    "\": write `weapon <the game's joint> <KH2 bone> [rotate <x> <y> <z>]`; the line is ignored");
                continue;
            }
            weapons.emplace_back(b, static_cast<int>(n));
            weapon_degrees[b] = by;
        } else if (a == "face") {
            std::string w;
            bool ok = true;
            if (b == "talk") {
                ok = static_cast<bool>(words >> model.face.talk);
            } else if (b == "speaker") {
                int from = 0, to = 0;
                while (ok && words >> w) {
                    ok = span(w, from, to);
                    for (int x = from; ok && x <= to; ++x)
                        model.face.speakers.push_back(x);
                }
            } else if (b == "ds") {
                int slot = 0;
                ok = words >> w >> slot && slot >= 0;
                if (ok)
                    model.face.ds.emplace_back(w, slot);
            } else if (b == "portrait") {
                FacePortrait shown;
                ok = static_cast<bool>(words >> shown.portrait);
                while (ok && words >> w) {
                    if (w == "talk") {
                        ok = words >> shown.talk && shown.talk >= 0;
                        continue;
                    }
                    char *end = nullptr;
                    const long n = std::strtol(w.c_str(), &end, 10);
                    ok = w == "last" || (end != w.c_str() && *end == '\0' && n >= 0);
                    shown.slots.push_back(w == "last" ? -2 : static_cast<int>(n));
                }
                ok = ok && (!shown.slots.empty() || shown.talk >= 0);
                if (ok)
                    model.face.portraits.push_back(std::move(shown));
            } else if (b == "closed") {
                int from = 0, to = 0;
                ok = false;
                while (words >> w) {
                    ok = span(w, from, to);
                    if (!ok)
                        break;
                    for (int x = from; x <= to; ++x)
                        model.face.closed.push_back(x);
                }
            } else if (b == "ranges") {
                ok = static_cast<bool>(words >> w) && (w == "on" || w == "off");
                model.face.ranges = w != "off";
            } else {
                ok = false;
            }
            if (!ok)
                say("\"" + line +
                    "\": write `face talk <slot>`, `face speaker <portrait>...`, `face ds <pattern animation> <slot>`, "
                    "`face portrait <portrait> [last|<slot>...] [talk <slot>]`, `face closed <slot>...` or `face ranges on|off`; the line is "
                    "ignored");
        } else if (a == "hand") {
            // hand <hand bone> <forearm bone> <finger bones> back <x> <y> <z>
            HandRule rule;
            int to = 0, from = 0, last = 0;
            std::string fingers, w;
            const bool ok = span(b, rule.hand, to) && to == rule.hand &&
                            static_cast<bool>(words >> rule.forearm >> fingers >> w) && w == "back" &&
                            static_cast<bool>(words >> rule.back[0] >> rule.back[1] >> rule.back[2]) &&
                            span(fingers, from, last);
            if (!ok) {
                say("\"" + line +
                    "\": write `hand <hand bone> <forearm bone> <finger bones> back <x> <y> <z>`; the line is ignored");
                continue;
            }
            for (int x = from; x <= last; ++x)
                rule.fingers.push_back(x);
            hand_rules.push_back(std::move(rule));
        } else if (a == "hand_pose") {
            // hand_pose relaxed|open|fist|flat|grip rotate <x> <y> <z> <bones>
            HandPoseRule rule;
            rule.pose = hand_pose_index(b);
            std::string w;
            bool ok = rule.pose < khdays::models::kHandPoses && static_cast<bool>(words >> w) && w == "rotate" &&
                      static_cast<bool>(words >> rule.degrees[0] >> rule.degrees[1] >> rule.degrees[2]);
            while (ok && words >> w) {
                int from = 0, to = 0;
                ok = span(w, from, to);
                for (int x = from; ok && x <= to; ++x)
                    rule.bones.push_back(x);
            }
            if (!ok || rule.bones.empty()) {
                say("\"" + line +
                    "\": write `hand_pose relaxed|open|fist|flat|grip rotate <x> <y> <z> <bones>`; the line is ignored");
                continue;
            }
            hand_poses.push_back(std::move(rule));
        } else if (a == "hand_bend" || a == "hand_fist") {
            // hand_bend <from degrees> <to degrees>: the open pose's share of the wrist's bend;
            // hand_fist <from> <to>: the fist's share of the hand's speed against the body
            std::string w;
            float from = 0.0F, to = 0.0F;
            if (number(b, from) && (words >> w) && number(w, to)) {
                (a == "hand_bend" ? model.hand_bend_from : model.hand_fist_from) = from;
                (a == "hand_bend" ? model.hand_bend_to : model.hand_fist_to) = to;
            } else {
                say("\"" + line + "\": write `" + a + " <from> <to>`; the line is ignored");
            }
        } else if (a == "hand_grip_fade" || a == "hand_lag" || a == "hand_finger_link") {
            if (!number(b, a == "hand_lag"           ? model.hand_lag
                           : a == "hand_finger_link" ? model.hand_finger_link
                                                     : model.hand_grip_fade))
                say("\"" + line + "\": write `" + a + " <number>`; the line is ignored");
        } else if (a == "hand_walk") {
            // hand_walk <body bone> <from> <to> <share>: the fist's share of how fast that bone travels
            int bone = 0, to_bone = 0;
            float from = 0.0F, to = 0.0F, share = 0.0F;
            if (span(b, bone, to_bone) && (words >> from >> to >> share)) {
                model.hand_walk_joint = bone;
                model.hand_walk_from = from;
                model.hand_walk_to = to;
                model.hand_walk_share = share;
            } else {
                say("\"" + line + "\": write `hand_walk <body bone> <from> <to> <share>`; the line is ignored");
            }
        } else if (a == "hand_contact") {
            // hand_contact <near> <far> <most speed>: the palm's distance to a surface for its flat pose, all to none
            std::string far, fast;
            float near = 0.0F, f = 0.0F, v = 0.0F;
            if (number(b, near) && (words >> far >> fast) && number(far, f) && number(fast, v)) {
                model.hand_contact_near = near;
                model.hand_contact_far = f;
                model.hand_contact_speed = v;
            } else {
                say("\"" + line + "\": write `hand_contact <near> <far> <most speed>`; the line is ignored");
            }
        } else if (a == "hand_body") {
            // hand_body <bones> axis <from bone> <to bone> [margin <cm>]
            HandBodyRule rule;
            std::string w = b;
            bool ok = true;
            do {
                if (w == "axis")
                    break;
                int from = 0, to = 0;
                ok = span(w, from, to);
                for (int x = from; ok && x <= to; ++x)
                    rule.bones.push_back(x);
            } while (ok && words >> w);
            ok = ok && w == "axis" && !rule.bones.empty() && static_cast<bool>(words >> rule.from >> rule.to);
            if (ok && words >> w)
                ok = w == "margin" && static_cast<bool>(words >> rule.margin);
            if (!ok) {
                say("\"" + line +
                    "\": write `hand_body <bones> axis <from bone> <to bone> [margin <cm>]`; the line is ignored");
                continue;
            }
            hand_body.push_back(std::move(rule));
        } else if (a == "hand_anim") {
            // hand_anim relaxed|open|fist|flat|grip left|right|both <game animation>...
            const std::size_t pose = hand_pose_index(b);
            std::string side, name;
            if (pose >= khdays::models::kHandPoses || !(words >> side) ||
                (side != "left" && side != "right" && side != "both") || !(words >> name)) {
                say("\"" + line +
                    "\": write `hand_anim relaxed|open|fist|flat|grip left|right|both <game animation>...`; the line is ignored");
                continue;
            }
            do {
                auto &sides = model.hand_anim.try_emplace(name, std::array<int, 2>{-1, -1}).first->second;
                if (side != "right")
                    sides[0] = static_cast<int>(pose);
                if (side != "left")
                    sides[1] = static_cast<int>(pose);
            } while (words >> name);
        } else if (a == "ds_pose") {
            // ds_pose <slot>[@<frame>] | rotate <x> <y> <z>, then bones (`135-156`), then [for <animation>...]
            DsPoseRule rule;
            bool ok = true;
            if (b == "rotate") {
                ok = static_cast<bool>(words >> rule.degrees[0] >> rule.degrees[1] >> rule.degrees[2]);
            } else {
                const auto at = b.find('@');
                float slot = 0.0F;
                ok = number(b.substr(0, at), slot) && (at == std::string::npos || number(b.substr(at + 1), rule.frame));
                rule.slot = static_cast<int>(slot);
            }
            bool names = false;
            std::string w;
            while (ok && words >> w) {
                int from = 0, to = 0;
                if (names)
                    rule.animations.push_back(w);
                else if (w == "for")
                    names = true;
                else if (span(w, from, to))
                    for (int x = from; x <= to; ++x)
                        rule.bones.push_back(x);
                else
                    ok = false;
            }
            if (!ok || rule.bones.empty() || (names && rule.animations.empty())) {
                say("\"" + line +
                    "\": write `ds_pose <mset slot>[@<frame>] <bones> [for <game animation>...]` or `ds_pose "
                    "rotate <x> <y> <z> <bones> [for ...]`; the line is ignored");
                continue;
            }
            ds_rules.push_back(std::move(rule));
        } else {
            Kh2Section section;
            const auto at = a.find('@');
            if (at != std::string::npos) {
                if (!number(a.substr(at + 1), section.from)) {
                    say("\"" + line +
                        "\": the frame after @ is not a number; write `<game animation>@<BCA frame> <slot>`; the line is ignored");
                    continue;
                }
                a.erase(at);
            }
            if (b == "blend") { // <game animation> blend <frames>: its fade into an idle, its own animation kept
                std::string v;
                float frames = 0.0F;
                if (at != std::string::npos || !(words >> v) || !number(v, frames) || frames < 0.0F) {
                    say("\"" + line + "\": write `<game animation> blend <frames>`; the line is ignored");
                    continue;
                }
                model.kh2_idle_blend[a] = frames;
                continue;
            }
            char *end = nullptr;
            section.slot = std::strtoul(b.c_str(), &end, 10);
            if (end == b.c_str() || *end != '\0') {
                say("\"" + line +
                    "\": the slot is not a number; write `<game animation> <mset slot>`; the line is ignored");
                continue;
            }
            std::string c, d;
            bool bad = false, to = false;
            std::vector<std::string> rest;
            while (words >> c)
                rest.push_back(c);
            std::istringstream options;
            for (std::size_t r = 0; r < rest.size() && !bad; ++r) {
                if (rest[r] != "finish")
                    continue;
                section.finish = true; // finish [<slot>...]: its slots taken out of the options
                std::size_t q = r + 1;
                for (; q < rest.size(); ++q) {
                    char *slot_end = nullptr;
                    const unsigned long slot = std::strtoul(rest[q].c_str(), &slot_end, 10);
                    if (slot_end == rest[q].c_str() || *slot_end != '\0')
                        break;
                    section.then.push_back(slot);
                }
                rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(r),
                           rest.begin() + static_cast<std::ptrdiff_t>(q));
                break;
            }
            {
                std::string joined;
                for (const std::string &w : rest)
                    joined += w + " ";
                options.str(joined);
            }
            while (!bad && options >> c) {
                if (c == "follow")
                    section.follow = true;
                else if (c == "cut")
                    section.cut = true;
                else if (c == "rotate") {
                    std::array<float, 3> degrees{};
                    bad = !(options >> degrees[0] >> degrees[1] >> degrees[2]);
                    section.turn = turn_of(degrees);
                } else if (c == "start_loop")
                    section.start_loop = true;
                else if (c == "loop_blend")
                    bad = !(options >> d && number(d, section.loop_blend));
                else if (c == "offset") {
                    bad = !(options >> section.offset[0] >> section.offset[1] >> section.offset[2]);
                    section.offset_to = section.offset;
                } else if (c == "to")
                    bad = !(options >> section.offset_to[0] >> section.offset_to[1] >> section.offset_to[2]) || to;
                else
                    bad = !(c == "blend" && options >> d && number(d, section.blend));
                to = to || c == "to";
            }
            if (bad) {
                say("\"" + line +
                    "\": after the slot write `blend <frames>`, `loop_blend <frames>`, `follow`, `start_loop`, `cut`, "
                    "`rotate <x> <y> <z>` (degrees), `finish [<slot>...]` or `offset <x> <y> <z> [to <x> <y> <z>]` (centimetres); the line is ignored");
                continue;
            }
            section.realtime = section.realtime || walk_line; // a walk at its own pace, not the game's slowed run
            auto &parts = sections[a];
            std::erase_if(parts, [&](const Kh2Section &s) { return s.from == section.from; });
            parts.push_back(section);
        }
    }
    // each joint's KH2 bone: the importer names them kh2_<bone>
    model.kh2_bone.assign(model.joints.size(), -1);
    for (std::size_t j = 0; j < model.joints.size(); ++j) {
        const std::string &n = model.joints[j].name;
        if (n.rfind("kh2_", 0) == 0)
            model.kh2_bone[j] = std::atoi(n.c_str() + 4);
    }
    // what the game attaches to a joint of its model (its weapons), on a KH2 bone's joint instead
    for (const auto &[node, bone] : weapons) {
        const auto g = std::find(model.node_names.begin(), model.node_names.end(), node);
        const auto j = std::find(model.kh2_bone.begin(), model.kh2_bone.end(), bone);
        if (g == model.node_names.end() || j == model.kh2_bone.end()) {
            say("weapon " + node + " " + std::to_string(bone) + ": " +
                (g == model.node_names.end() ? "the game's model has no joint " + node
                                             : "the model has no bone " + std::to_string(bone)) +
                "; check the `weapon` line; the game's place is kept");
            continue;
        }
        model.kh2_weapon.resize(model.node_names.size(), -1);
        model.kh2_weapon_turn.resize(model.node_names.size(), kIdentity);
        model.kh2_weapon[static_cast<std::size_t>(g - model.node_names.begin())] =
            static_cast<int>(j - model.kh2_bone.begin());
        model.kh2_weapon_turn[static_cast<std::size_t>(g - model.node_names.begin())] = turn_of(weapon_degrees[node]);
    }
    // a `place`d joint holding its own node's weapon: turned so the line's frame on it is the node's own (its place
    // already is), so a fade between a KH2 motion and the game's animation turns the weapon the short way
    for (std::size_t g = 0; g < model.kh2_weapon.size() && g < model.node_rest_inverse.size(); ++g) {
        if (model.kh2_weapon[g] < 0)
            continue;
        Joint &joint = model.joints[static_cast<std::size_t>(model.kh2_weapon[g])];
        if (joint.node != static_cast<int>(g) || !joint.placed)
            continue;
        // the pose turns it by the node's turn from rest (d) times this: d C W T = d N, N the node's rest turn
        const Mat rest = axes_of(mul(joint.bind, inverse(joint.inverse_bind)));
        const Mat node_rest = axes_of(inverse(model.node_rest_inverse[g]));
        joint.correction = mul(node_rest, inverse(mul(rest, model.kh2_weapon_turn[g])));
        joint.to_node = mul(inverse(axes_of(mul(joint.correction, rest))), node_rest);
    }
    if (mset.empty()) {
        if (!sections.empty())
            say("motions.txt names no mset (a line `mset <file>.mset`); the game's animations are played");
        resolve_ds_poses(model, ds_rules, say);
        resolve_hands(model, hand_rules, hand_poses, hand_body, say);
        return;
    }
    try {
        const auto read = [](const std::filesystem::path &p) {
            std::ifstream f(p, std::ios::binary);
            if (!f) {
                const auto name = p.filename().u8string();
                throw std::runtime_error("cannot open " + std::string(name.begin(), name.end()));
            }
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        };
        const auto mdlx = read(file);
        const auto set = read(file.parent_path() / u8(mset));
        auto own = std::make_shared<kh2::MotionSkeleton>(kh2::read_motion_skeleton(mdlx.data(), mdlx.size()));
        model.kh2_own_skeleton = own;
        model.kh2_own_slots.clear();
        auto motions = std::make_shared<kh2::MotionSet>(set.data(), set.size());
        if (mset_own)
            model.kh2_own_slots.emplace_back(0, motions->slots());
        // `motions`: another mset's slots from a slot on; with an entry, that entry of it, a set of its own
        for (const Extra &x : extras) {
            const auto bytes = read(file.parent_path() / u8(x.file));
            const std::size_t before = motions->slots();
            if (x.entry.empty()) {
                motions->add(x.at, kh2::MotionSet(bytes.data(), bytes.size()));
            } else {
                bool found = false;
                for (const kh2::BarEntry &e : kh2::bar_entries(bytes.data(), bytes.size()))
                    if (!found && e.name == x.entry && kh2::is_bar(bytes.data(), bytes.size(), e.offset)) {
                        motions->add(x.at, kh2::MotionSet(bytes.data() + e.offset, e.size));
                        found = true;
                    }
                if (!found)
                    throw std::runtime_error(x.file + " has no set named " + x.entry);
            }
            if (x.own && motions->slots() > before)
                model.kh2_own_slots.emplace_back(x.at, motions->slots());
        }
        if (skeleton.empty()) {
            model.kh2_skeleton = own;
            if (!carries.empty())
                say("`carry` places a bone in the `skeleton`'s motions, and there is no `skeleton` line; add one or take it off; ignored");
        } else { // the mset plays on its own model's bones, given to this one's
            const auto other = read(file.parent_path() / u8(skeleton));
            model.kh2_skeleton =
                std::make_shared<kh2::MotionSkeleton>(kh2::read_motion_skeleton(other.data(), other.size()));
            std::vector<int> source_of(own->joints.size(), -1);
            for (const auto &[t, s] : bones) {
                if (static_cast<std::size_t>(t) >= source_of.size())
                    throw std::runtime_error("bone " + std::to_string(t) + ": the model has " +
                                             std::to_string(own->joints.size()) + " bones");
                source_of[static_cast<std::size_t>(t)] = s;
            }
            auto retarget = std::make_shared<kh2::Retarget>(*model.kh2_skeleton, *own, source_of, limbs);
            // `carry`: the bone on the other where an `own` slot's first frame has it (both unscaled)
            for (const Carry &c : carries) {
                const kh2::Motion *motion = motions->motion(c.at);
                const bool in_own = std::any_of(model.kh2_own_slots.begin(), model.kh2_own_slots.end(),
                                                [&](const auto &r) { return c.at >= r.first && c.at < r.second; });
                const std::size_t n = own->joints.size();
                if (motion == nullptr || !in_own || static_cast<std::size_t>(c.bone) >= n ||
                    static_cast<std::size_t>(c.by) >= n) {
                    say("carry " + std::to_string(c.bone) + " " + std::to_string(c.by) + " at " + std::to_string(c.at) +
                        ": " +
                        (motion == nullptr || !in_own ? "no motion of an `own` mset in that slot"
                                                      : "the model has " + std::to_string(n) + " bones") +
                        "; check the line; it is ignored");
                    continue;
                }
                kh2::MotionPlayer player(*own);
                player.evaluate(*motion, 0.0F, kIdentity, nullptr);
                const auto unscaled = [](Mat m) {
                    for (std::size_t col = 0; col < 3; ++col) {
                        const float l = length({m[col * 4], m[col * 4 + 1], m[col * 4 + 2]});
                        for (std::size_t r = 0; r < 3 && l > 0.0F; ++r)
                            m[col * 4 + r] /= l;
                    }
                    return m;
                };
                const auto &g = player.joints();
                try {
                    retarget->carry(c.bone, c.by,
                                    mul(inverse(unscaled(g[static_cast<std::size_t>(c.by)])),
                                        unscaled(g[static_cast<std::size_t>(c.bone)])));
                } catch (const std::runtime_error &e) {
                    say(std::string(e.what()) + "; check its bones; the `carry` line is ignored");
                }
            }
            model.kh2_retarget = std::move(retarget);
        }
        model.kh2_motions = std::move(motions);
        // `weapon_motions`: the weapon's mset and skeleton, and its held pose (slot 0's first frame)
        if (!weapon_mset.empty()) {
            const auto wset = read(file.parent_path() / u8(weapon_mset));
            const auto wmdl = read(file.parent_path() / u8(weapon_mdlx));
            auto skeleton_of =
                std::make_shared<kh2::MotionSkeleton>(kh2::read_motion_skeleton(wmdl.data(), wmdl.size()));
            auto weapon = std::make_shared<kh2::MotionSet>(wset.data(), wset.size());
            if (static_cast<std::size_t>(weapon_bone) >= skeleton_of->joints.size())
                throw std::runtime_error(weapon_mdlx + " has " + std::to_string(skeleton_of->joints.size()) +
                                         " bones, no bone " + std::to_string(weapon_bone));
            if (weapon->motion(0) == nullptr)
                throw std::runtime_error(weapon_mset + " has no motion in slot 0 (the held pose)");
            kh2::MotionPlayer held(*skeleton_of);
            held.evaluate(*weapon->motion(0), 0.0F, kIdentity, nullptr);
            model.weapon_rest.clear();
            for (const auto &g : held.joints())
                model.weapon_rest.push_back(in_metres(g));
            model.weapon_motions = std::move(weapon);
            model.weapon_skeleton = std::move(skeleton_of);
            model.weapon_blade = weapon_bone;
        }
    } catch (const std::exception &e) {
        say(std::string("its motions cannot be read (") + e.what() +
            "); check the files motions.txt names; the game's animations are played");
        model.kh2_motions.reset();
        model.kh2_retarget.reset();
        model.kh2_own_slots.clear();
        model.weapon_motions.reset();
        resolve_ds_poses(model, ds_rules, say);
        resolve_hands(model, hand_rules, hand_poses, hand_body, say);
        return;
    }
    for (auto &[name, parts] : sections) {
        std::erase_if(parts, [&](const Kh2Section &s) {
            if (model.kh2_motions->motion(s.slot) != nullptr)
                return false;
            say(name + ": mset slot " + std::to_string(s.slot) +
                " holds no motion it plays; name another in motions.txt; the game's is kept");
            return true;
        });
        for (Kh2Section &s : parts) {
            std::erase_if(s.then, [&](std::size_t slot) {
                if (model.kh2_motions->motion(slot) != nullptr)
                    return false;
                say(name + ": `finish` slot " + std::to_string(slot) +
                    " holds no motion it plays; name another in motions.txt; left out");
                return true;
            });
            if (s.finish && model.kh2_idle.empty())
                say(name +
                    ": `finish` with no `idle` line (the game's idles it plays out into): it never plays out; add one");
        }
        if (parts.empty())
            continue;
        std::sort(parts.begin(), parts.end(), [](const Kh2Section &x, const Kh2Section &y) { return x.from < y.from; });
        if (parts.front().from > 0.0F)
            say(name + ": no section from frame 0; the game's animation plays before frame " +
                std::to_string(parts.front().from));
        model.kh2_sections[name] = std::move(parts);
    }
    for (auto it = model.kh2_air.begin(); it != model.kh2_air.end();) {
        Kh2Air &air = it->second;
        const auto plays = [&](std::size_t slot) { return model.kh2_motions->motion(slot) != nullptr; };
        if (!plays(air.air.front().slot)) {
            say("air " + it->first + ": mset slot " + std::to_string(air.air.front().slot) +
                " holds no motion it plays; name another; the line is ignored");
            it = model.kh2_air.erase(it);
            continue;
        }
        if (!air.land.empty() && !plays(air.land.front().slot)) {
            say("air " + it->first + ": its land slot " + std::to_string(air.land.front().slot) +
                " holds no motion it plays; name another; landing keeps the landing's");
            air.land.clear();
        }
        if (!air.land.empty())
            std::erase_if(air.land.front().then, [&](std::size_t slot) { return !plays(slot); });
        if (!air.land.empty() && model.kh2_landing.empty())
            say("air " + it->first +
                ": `land` with no `landing` line (the game's landing animation): never played; add one");
        ++it;
    }
    resolve_ds_poses(model, ds_rules, say);
    resolve_hands(model, hand_rules, hand_poses, hand_body, say);
    khdays_log("models: %s: %zu of the game's animations play KH2 motions from %s (root motion %s)", path.c_str(),
               model.kh2_sections.size(), mset.c_str(), model.kh2_root_motion ? "on" : "off");
}

// `align`'s turn, at rest, of a part's points (each about its joint's head) onto the game's: a foot about the
// vertical by the way to its front fifth (both rests stand); a hand by the way to its far fifth (the fingers)
// and its flat (the least spread), the flat's sign nearest the model's
Mat align_part(bool foot, const std::vector<std::array<float, 3>> &own, const std::vector<std::array<float, 3>> &its,
               float &degrees)
{
    using V = std::array<float, 3>;
    degrees = 0.0F;
    if (own.size() < 8 || its.size() < 8)
        return kIdentity;
    const auto unit = [](V v) {
        const float l = length(v);
        return l > 0.0F ? V{v[0] / l, v[1] / l, v[2] / l} : v;
    };
    // the mean of the fifth of the points that rank highest by `key`
    const auto top = [](const std::vector<V> &p, const auto &key) {
        std::vector<float> keys;
        for (const V &v : p)
            keys.push_back(key(v));
        std::vector<float> sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        const float cut = sorted[sorted.size() * 4 / 5];
        V sum{};
        int n = 0;
        for (std::size_t i = 0; i < p.size(); ++i)
            if (keys[i] >= cut) {
                for (std::size_t a = 0; a < 3; ++a)
                    sum[a] += p[i][a];
                ++n;
            }
        return V{sum[0] / static_cast<float>(n), sum[1] / static_cast<float>(n), sum[2] / static_cast<float>(n)};
    };
    Mat r = kIdentity;
    if (foot) {
        const auto front = [](const V &v) { return v[2]; };
        const V a = top(own, front), b = top(its, front);
        const float turn = std::atan2(b[0], b[2]) - std::atan2(a[0], a[2]);
        r[0] = r[10] = std::cos(turn);
        r[8] = std::sin(turn);
        r[2] = -std::sin(turn);
        degrees = std::fabs(turn) * 57.2957795F;
        return r;
    }
    const auto far = [](const V &v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; };
    // the least-spread axis of the points about their mean (Jacobi on the covariance)
    const auto flat = [](const std::vector<V> &p) {
        V mean{};
        for (const V &v : p)
            for (std::size_t a = 0; a < 3; ++a)
                mean[a] += v[a] / static_cast<float>(p.size());
        double cov[3][3] = {};
        for (const V &v : p)
            for (std::size_t a = 0; a < 3; ++a)
                for (std::size_t b = 0; b < 3; ++b)
                    cov[a][b] += static_cast<double>(v[a] - mean[a]) * static_cast<double>(v[b] - mean[b]);
        double e[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int sweep = 0; sweep < 30; ++sweep)
            for (int pq = 0; pq < 3; ++pq) {
                const int P = pq == 2 ? 1 : 0, Q = pq == 0 ? 1 : 2;
                if (std::fabs(cov[P][Q]) < 1e-18)
                    continue;
                const double th = 0.5 * std::atan2(2.0 * cov[P][Q], cov[Q][Q] - cov[P][P]);
                const double c = std::cos(th), s = std::sin(th);
                for (int x = 0; x < 3; ++x) {
                    const double xp = cov[x][P], xq = cov[x][Q];
                    cov[x][P] = c * xp - s * xq;
                    cov[x][Q] = s * xp + c * xq;
                }
                for (int x = 0; x < 3; ++x) {
                    const double px = cov[P][x], qx = cov[Q][x];
                    cov[P][x] = c * px - s * qx;
                    cov[Q][x] = s * px + c * qx;
                }
                for (int x = 0; x < 3; ++x) {
                    const double xp = e[x][P], xq = e[x][Q];
                    e[x][P] = c * xp - s * xq;
                    e[x][Q] = s * xp + c * xq;
                }
            }
        int least = 0;
        for (int a = 1; a < 3; ++a)
            if (cov[a][a] < cov[least][least])
                least = a;
        return V{static_cast<float>(e[0][least]), static_cast<float>(e[1][least]), static_cast<float>(e[2][least])};
    };
    const auto frame = [&](V f, V n) { // columns: the way, the flat made square to it, their cross
        f = unit(f);
        const float along = n[0] * f[0] + n[1] * f[1] + n[2] * f[2];
        n = unit(V{n[0] - f[0] * along, n[1] - f[1] * along, n[2] - f[2] * along});
        const V c{f[1] * n[2] - f[2] * n[1], f[2] * n[0] - f[0] * n[2], f[0] * n[1] - f[1] * n[0]};
        Mat m = kIdentity;
        for (std::size_t a = 0; a < 3; ++a) {
            m[a] = f[a];
            m[4 + a] = n[a];
            m[8 + a] = c[a];
        }
        return m;
    };
    const V fa = top(own, far), fb = top(its, far);
    V na = flat(own), nb = flat(its);
    if (na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] < 0.0F)
        nb = V{-nb[0], -nb[1], -nb[2]};
    r = mul(frame(fb, nb), inverse(frame(fa, na)));
    degrees = std::acos(std::fmax(-1.0F, std::fmin(1.0F, (r[0] + r[5] + r[10] - 1.0F) * 0.5F))) * 57.2957795F;
    return r;
}

std::unique_ptr<Model> load(const std::string &name, const std::string &path, const char *file, u32 offset)
{
    namespace assets = khdays::assets;
    auto model = std::make_unique<Model>();
    model->name = name;
    model->path = path;

    // the game's model: its nodes' names, parents and rest matrices
    std::vector<unsigned char> bmd0;
    std::size_t index = 0;
    assets::NeutralModel game;
    if (file == nullptr) {
        // a model drawn at a place of the port's (khdays_models_draw_placed): one node, its place
        game.object_names = {"place"};
        game.object_matrices = {kIdentity};
        game.object_parents = {-1};
    } else {
        try {
            if (!khdays_names_model_bmd0(file, offset, bmd0, index))
                throw std::runtime_error("not in its file");
            game = assets::decode_model_geometry(bmd0.data(), bmd0.size(), index);
        } catch (const std::exception &e) {
            std::fprintf(
                stderr,
                "models: %s: the game's model cannot be read (%s): the game's is drawn; check the mod's folder name against --dump-graphics\n",
                name.c_str(), e.what());
            return nullptr;
        }
    }
    const std::vector<Mat> rest = game.skinning != nullptr
                                      ? assets::compute_bone_world_matrices(*game.skinning, game.object_matrices)
                                      : game.object_matrices;
    for (const auto &w : rest)
        model->node_rest_inverse.push_back(inverse(w));
    model->node_parent = game.object_parents;
    model->node_parent.resize(rest.size(), -1);
    model->node_names = game.object_names;
    model->node_names.resize(rest.size());

    // the mod's model: glTF (.gltf, .glb), FBX or KH2's (.mdlx), the same neutral form
    assets::ImportedModel mod;
    try {
        const auto extension = u8(path).extension();
        if (extension == ".fbx" || extension == ".FBX")
            mod = assets::import_fbx(u8(path));
        else if (extension == ".mdlx" || extension == ".MDLX")
            mod = assets::kh2::import_mdlx(u8(path));
        else
            mod = assets::import_gltf(u8(path));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "models: %s cannot be read (%s): export it again; the game's is drawn\n", path.c_str(),
                     e.what());
        khdays_log("models: %s cannot be read (%s): export it again; the game's is drawn", path.c_str(), e.what());
        return nullptr;
    }
    for (const auto &[texture, why] : mod.missing_textures) {
        std::fprintf(stderr,
                     "models: %s: texture %s not found or not readable (%s); put it beside the model; drawn white\n",
                     path.c_str(), texture.c_str(), why.c_str());
        khdays_log("models: %s: texture %s not found or not readable (%s); put it beside the model; drawn white",
                   path.c_str(), texture.c_str(), why.c_str());
    }
    for (const std::string &note : mod.notes) {
        std::fprintf(stderr, "models: %s: %s\n", path.c_str(), note.c_str());
        khdays_log("models: %s: %s", path.c_str(), note.c_str());
    }
    // a mod's character with a skeleton of its own: none of its joints follows a node of the game's
    const KhdaysCharacter *character = khdays_characters_of_model(name.c_str());
    const bool own_skeleton = character != nullptr && character->own_skeleton;
    if (!own_skeleton && (u8(path).extension() == ".fbx" || u8(path).extension() == ".FBX")) {
        mod.animations.clear(); /* an FBX model's takes are not its own animation */
    }
    const BonesFile bones = read_bones(path);
    model->scale = bones.scale;
    model->cull = bones.cull;
    model->pose_degrees = bones.pose_degrees;
    const auto &spine_line = bones.spine;
    model->held_name = bones.held;
    model->held_rest = bones.held_rest;
    model->cuff_settings = bones.cuff_settings;
    model->setting = setting_named(name, bones.setting);
    model->kept_lists = kept_lists(name, game, bones.keep);
    // a mod's character's [model] scale over its bones file's, whatever its skeleton (as the studio shows it)
    if (character != nullptr && character->model_scale > 0.0F) {
        model->scale = character->model_scale;
    }
    const float k = model->scale;

    // the joints, parents first; each to the node it follows
    const std::size_t n = mod.joints.size();
    std::vector<int> order, placed(n, -1);
    for (std::size_t pass = 0; order.size() < n && pass <= n; ++pass) {
        for (std::size_t j = 0; j < n; ++j) {
            const int p = mod.joints[j].parent;
            if (placed[j] < 0 && (p < 0 || placed[static_cast<std::size_t>(p)] >= 0)) {
                placed[j] = static_cast<int>(order.size());
                order.push_back(static_cast<int>(j));
            }
        }
    }
    std::map<std::uint32_t, int> joint_of_palette;
    for (const int j : order) {
        const auto &in = mod.joints[static_cast<std::size_t>(j)];
        Joint out;
        out.name = in.name;
        out.parent = in.parent >= 0 ? placed[static_cast<std::size_t>(in.parent)] : -1;
        const auto pair = bones.pairs.find(in.name);
        const std::string &wanted = pair != bones.pairs.end() ? pair->second : in.name;
        for (std::size_t g = 0; g < game.object_names.size() && !own_skeleton; ++g) {
            if (game.object_names[g] == wanted)
                out.node = static_cast<int>(g);
        }
        if (pair != bones.pairs.end() && out.node < 0) {
            std::fprintf(stderr, "models: %s: no node %s for joint %s; check the pair in bones.txt\n", name.c_str(),
                         wanted.c_str(), in.name.c_str());
        }
        Mat world, ibm;
        std::copy(in.world.begin(), in.world.end(), world.begin());
        std::copy(in.inverse_bind.begin(), in.inverse_bind.end(), ibm.begin());
        Mat s = kIdentity;
        s[0] = s[5] = s[10] = k;
        out.bind = mul(s, mul(world, ibm));
        out.room_node = in.node;
        out.inverse_bind = ibm;
        const auto h = translation(world);
        out.head = {h[0] * k, h[1] * k, h[2] * k};
        joint_of_palette[in.palette_index] = static_cast<int>(model->joints.size());
        model->joints.push_back(std::move(out));
    }

    // its secondary motion, cooked against the file's skeleton and skinned mesh before the meshes are taken
    {
        std::vector<int> joint_of_bone(n, -1);
        std::vector<Mat> inverse_binds;
        for (std::size_t j = 0; j < n; ++j)
            joint_of_bone[j] = placed[j];
        for (const Joint &joint : model->joints)
            inverse_binds.push_back(joint.inverse_bind);
        model->secondary = khdays::secondary::load_model_secondary(path, name, mod, joint_of_bone, inverse_binds);
    }

    // the rest pose correction, from the roots down
    auto descends = [&](int j, int from) {
        for (int up = j; up >= 0; up = model->joints[static_cast<std::size_t>(up)].parent) {
            if (up == from)
                return true;
        }
        return false;
    };
    for (std::size_t j = 0; j < model->joints.size(); ++j) {
        Joint &joint = model->joints[j];
        joint.correction =
            joint.parent >= 0 ? model->joints[static_cast<std::size_t>(joint.parent)].correction : kIdentity;
        if (joint.node < 0)
            continue;
        const auto hg = translation(rest[static_cast<std::size_t>(joint.node)]);
        int child = -1, children = 0;
        for (std::size_t c = 0; c < model->joints.size(); ++c) {
            const int cn = model->joints[c].node;
            if (cn >= 0 && model->node_parent[static_cast<std::size_t>(cn)] == joint.node && c != j &&
                descends(static_cast<int>(c), static_cast<int>(j))) {
                child = static_cast<int>(c);
                ++children;
            }
        }
        if (children == 1) {
            const Joint &to = model->joints[static_cast<std::size_t>(child)];
            const auto hc = translation(rest[static_cast<std::size_t>(to.node)]);
            const auto ds = vec(joint.correction,
                                {to.head[0] - joint.head[0], to.head[1] - joint.head[1], to.head[2] - joint.head[2]});
            const std::array<float, 3> dg{hc[0] - hg[0], hc[1] - hg[1], hc[2] - hg[2]};
            if (length(ds) > 1e-5F && length(dg) > 1e-5F) {
                float degrees = 0.0F;
                const Mat r = arc(ds, dg, degrees);
                joint.degrees = degrees;
                if (degrees > model->pose_degrees) {
                    joint.correction = mul(r, joint.correction);
                }
                if (tracing()) {
                    std::fprintf(stderr, "models: %s: %s follows %s, %.1f degrees from its pose%s\n", name.c_str(),
                                 joint.name.c_str(), game.object_names[static_cast<std::size_t>(joint.node)].c_str(),
                                 degrees, degrees > model->pose_degrees ? ", turned" : "");
                }
            }
        } else if (tracing()) {
            std::fprintf(stderr, "models: %s: %s follows %s\n", name.c_str(), joint.name.c_str(),
                         game.object_names[static_cast<std::size_t>(joint.node)].c_str());
        }
        // `align`: an end (a hand, a foot) turned whole onto the game's at rest by its shape (the way above only
        // turns a bone toward its one paired child, never about itself; ends have none)
        if (const auto al = bones.aligns.find(joint.name); al != bones.aligns.end()) {
            std::vector<std::array<float, 3>> own, its;
            const Mat &wg = rest[static_cast<std::size_t>(joint.node)];
            for (const auto &mesh : mod.model.meshes)
                for (const auto &v : mesh.vertices) {
                    const auto it = joint_of_palette.find(v.joints[heaviest_weight(v)]);
                    if (it == joint_of_palette.end())
                        continue;
                    const int c = it->second;
                    if (c != static_cast<int>(j) &&
                        !(model->joints[static_cast<std::size_t>(c)].node < 0 && descends(c, static_cast<int>(j))))
                        continue;
                    own.push_back(
                        vec(joint.correction, {v.position[0] * k - joint.head[0], v.position[1] * k - joint.head[1],
                                               v.position[2] * k - joint.head[2]}));
                }
            for (const auto &mesh : game.meshes)
                for (const auto &v : mesh.vertices) {
                    const std::uint32_t pal = v.joints[0];
                    if (pal >= game.palette.size() || pal >= game.palette_bones.size() ||
                        game.palette_bones[pal].empty())
                        continue;
                    auto bone = game.palette_bones[pal].front();
                    for (const auto &bw : game.palette_bones[pal])
                        if (bw.second > bone.second)
                            bone = bw;
                    if (static_cast<int>(bone.first) != joint.node)
                        continue;
                    Mat pm;
                    std::copy(game.palette[pal].begin(), game.palette[pal].end(), pm.begin());
                    const auto at = point(pm, v.position);
                    its.push_back({at[0] - wg[12], at[1] - wg[13], at[2] - wg[14]});
                }
            float degrees = 0.0F;
            const Mat r = align_part(al->second == "foot", own, its, degrees);
            joint.correction = mul(r, joint.correction);
            khdays_log("models: %s: %s turned %.1f degrees onto %s at rest (align %s; %zu and %zu vertices)",
                       name.c_str(), joint.name.c_str(), static_cast<double>(degrees),
                       game.object_names[static_cast<std::size_t>(joint.node)].c_str(), al->second.c_str(), own.size(),
                       its.size());
        }
        bool identity = true;
        for (int i = 0; i < 16; ++i)
            identity = identity && std::fabs(joint.correction[i] - kIdentity[i]) < 1e-6F;
        joint.exact = identity && std::fabs(joint.head[0] - hg[0]) < 1e-4F &&
                      std::fabs(joint.head[1] - hg[1]) < 1e-4F && std::fabs(joint.head[2] - hg[2]) < 1e-4F;
    }

    // the two-bone limbs: three paired joints, each below the one before
    for (const auto &line : bones.limbs) {
        const auto &names = line.joints;
        std::array<int, 3> limb{-1, -1, -1};
        for (std::size_t i = 0; i < 3; ++i)
            limb[i] = joint_named(*model, names[i]);
        bool ok = limb[0] >= 0 && limb[1] >= 0 && limb[2] >= 0 && descends(limb[1], limb[0]) &&
                  descends(limb[2], limb[1]) && limb[1] != limb[0] && limb[2] != limb[1];
        for (std::size_t i = 0; ok && i < 3; ++i)
            ok = model->joints[static_cast<std::size_t>(limb[i])].node >= 0;
        if (!ok) {
            std::fprintf(stderr,
                         "models: %s: ik %s %s %s: each must be a paired joint below the one before; the line is "
                         "ignored\n",
                         name.c_str(), names[0].c_str(), names[1].c_str(), names[2].c_str());
            khdays_log("models: %s: ik %s %s %s: each must be a paired joint below the one before; the line is ignored",
                       name.c_str(), names[0].c_str(), names[1].c_str(), names[2].c_str());
            continue;
        }
        const float cm = 0.01F * k; // the offset's centimetres in the model's (scaled) units
        model->limbs.push_back({limb, line.exact, {line.offset[0] * cm, line.offset[1] * cm, line.offset[2] * cm}});
    }

    // what the game attaches to its nodes, carried by the model's joints: each node's frame on the joint of its
    // own node, else of its nearest ancestor's with one, as it sits under that node at rest
    for (Joint &joint : model->joints) {
        if (joint.node < 0)
            continue;
        // the joint's frame at rest as the game's pose turns it (its correction), against its node's
        const Mat frame = axes_of(mul(joint.correction, mul(joint.bind, inverse(joint.inverse_bind))));
        joint.to_node = mul(inverse(frame), axes_of(rest[static_cast<std::size_t>(joint.node)]));
    }
    model->node_joint.assign(rest.size(), -1);
    model->node_local.assign(rest.size(), kIdentity);
    for (std::size_t g = 0; g < rest.size(); ++g)
        for (int a = static_cast<int>(g); a >= 0 && model->node_joint[g] < 0;
             a = a == model->node_parent[static_cast<std::size_t>(a)] ? -1
                                                                      : model->node_parent[static_cast<std::size_t>(a)])
            for (std::size_t j = 0; j < model->joints.size(); ++j)
                if (model->joints[j].node == a) {
                    model->node_joint[g] = static_cast<int>(j);
                    model->node_local[g] = mul(inverse(rest[static_cast<std::size_t>(a)]), rest[g]);
                    break;
                }
    for (const Mat &w : rest)
        model->node_rest_place.push_back(translation(w));

    // `place`: joints on their game nodes' own places; the bone above each (when that joint's only paired child)
    // stretched to reach it, with the joints that ride it
    const auto paired_above = [&](int j) {
        int a = model->joints[static_cast<std::size_t>(j)].parent;
        while (a >= 0 && model->joints[static_cast<std::size_t>(a)].node < 0)
            a = model->joints[static_cast<std::size_t>(a)].parent;
        return a;
    };
    for (const auto &[joint_name, by] : bones.places) {
        const int found = joint_named(*model, joint_name);
        if (found < 0 || model->joints[static_cast<std::size_t>(found)].node < 0) {
            khdays_log("models: %s: place %s: no paired joint of that name; pair it in bones.txt; the line is ignored",
                       name.c_str(), joint_name.c_str());
            continue;
        }
        const auto j = static_cast<std::size_t>(found);
        model->joints[j].placed = true;
        model->joints[j].place_offset = {by[0] * 0.01F, by[1] * 0.01F, by[2] * 0.01F};
    }
    for (std::size_t c = 0; c < model->joints.size(); ++c) {
        if (!model->joints[c].placed)
            continue;
        const int p = paired_above(static_cast<int>(c));
        int children = 0;
        for (std::size_t o = 0; o < model->joints.size(); ++o)
            if (model->joints[o].node >= 0 && paired_above(static_cast<int>(o)) == p)
                ++children;
        if (p < 0 || children != 1)
            continue; // a bone with several paired children (the pelvis) is left; its mesh takes up the difference
        Stretch st;
        st.joint = p;
        st.child = static_cast<int>(c);
        for (std::size_t o = 0; o < model->joints.size(); ++o)
            if (static_cast<int>(o) == p || (model->joints[o].node < 0 && paired_above(static_cast<int>(o)) == p &&
                                             descends(static_cast<int>(o), p)))
                st.riders.push_back(static_cast<int>(o));
        model->stretches.push_back(std::move(st));
    }
    std::sort(model->stretches.begin(), model->stretches.end(),
              [](const Stretch &x, const Stretch &y) { return x.joint < y.joint; });

    // `spine`: its chain, the node carrying its base, the pelvis it swings
    if (!spine_line[0].empty()) {
        Spine sp;
        const int base = joint_named(*model, spine_line[0]);
        sp.end = joint_named(*model, spine_line[1]);
        bool ok = base >= 0 && sp.end >= 0 && model->joints[static_cast<std::size_t>(sp.end)].node >= 0;
        for (int a = ok ? model->joints[static_cast<std::size_t>(sp.end)].parent : -1; a >= 0;
             a = model->joints[static_cast<std::size_t>(a)].parent) {
            sp.chain.insert(sp.chain.begin(), a);
            if (a == base)
                break;
        }
        ok = ok && !sp.chain.empty() && sp.chain.front() == base;
        for (std::size_t c = 1; ok && c <= sp.chain.size() && sp.base_node < 0; ++c)
            sp.base_node = model->joints[static_cast<std::size_t>(c < sp.chain.size() ? sp.chain[c] : sp.end)].node;
        ok = ok && sp.base_node >= 0;
        for (sp.root = base; ok && model->joints[static_cast<std::size_t>(sp.root)].parent >= 0;)
            sp.root = model->joints[static_cast<std::size_t>(sp.root)].parent;
        if (ok && !spine_line[2].empty()) {
            sp.hips = joint_named(*model, spine_line[2]);
            sp.pelvis =
                sp.hips >= 0
                    ? (model->joints[static_cast<std::size_t>(sp.hips)].node >= 0 ? sp.hips : paired_above(sp.hips))
                    : -1;
            ok = sp.pelvis >= 0 && sp.pelvis != sp.root &&
                 std::find(sp.chain.begin(), sp.chain.end(), sp.pelvis) == sp.chain.end();
        }
        if (ok) {
            model->spine = std::move(sp);
        } else {
            std::fprintf(stderr,
                         "models: %s: spine %s %s: the end must be a paired joint below the base, and `hips` a "
                         "joint whose paired joint above is off the chain; the line is ignored\n",
                         name.c_str(), spine_line[0].c_str(), spine_line[1].c_str());
            khdays_log("models: %s: spine %s %s: not a chain to a paired joint; check its joints; the line is ignored",
                       name.c_str(), spine_line[0].c_str(), spine_line[1].c_str());
        }
    }

    // the cloth's `game_shape`: the game model at rest, the node each bone follows, the scale
    if (model->secondary) {
        khdays::secondary::GameMesh gm;
        gm.scale = k;
        gm.node_parent = model->node_parent;
        for (const auto &at : model->node_rest_place)
            gm.node_place.push_back({at[0], at[1], at[2]});
        gm.node_of_bone.assign(mod.joints.size(), -1);
        for (std::size_t b = 0; b < mod.joints.size(); ++b)
            if (placed[b] >= 0)
                gm.node_of_bone[b] = model->joints[static_cast<std::size_t>(placed[b])].node;
        for (const auto &mesh : game.meshes) {
            const auto base = static_cast<std::uint32_t>(gm.positions.size());
            for (const auto i : mesh.indices)
                gm.triangles.push_back(base + i);
            for (const auto &v : mesh.vertices) {
                const std::uint32_t pal = v.joints[0];
                if (pal >= game.palette.size() || pal >= game.palette_bones.size()) { // kept: the triangles count it
                    gm.positions.push_back({});
                    gm.weights.push_back({{{-1, 0.0F}, {-1, 0.0F}, {-1, 0.0F}, {-1, 0.0F}}});
                    continue;
                }
                Mat pm;
                std::copy(game.palette[pal].begin(), game.palette[pal].end(), pm.begin());
                const auto at = point(pm, v.position);
                std::array<std::pair<int, float>, 4> w{};
                for (auto &x : w)
                    x = {-1, 0.0F};
                std::size_t n_w = 0;
                for (const auto &bw : game.palette_bones[pal])
                    if (n_w < 4)
                        w[n_w++] = {static_cast<int>(bw.first), bw.second};
                std::sort(w.begin(), w.end(), [](const auto &x, const auto &y) { return x.second > y.second; });
                gm.positions.push_back({at[0], at[1], at[2]});
                gm.weights.push_back(w);
            }
        }
        khdays::secondary::set_game_mesh(*model->secondary, gm);
    }

    // the mesh scales: the joint's vertices scaled about its rest place in its own axes (B S B^-1 at rest,
    // B its rest frame); the bones (its children, the cloth's colliders) keep their sizes
    for (const MeshScale &s : bones.mesh_scales) {
        const int found = joint_named(*model, s.joint);
        if (found < 0) {
            std::fprintf(stderr, "models: %s: mesh_scale %s: no such joint; check its name; the line is ignored\n",
                         name.c_str(), s.joint.c_str());
            khdays_log("models: %s: mesh_scale %s: no such joint; check its name; the line is ignored", name.c_str(),
                       s.joint.c_str());
            continue;
        }
        const auto j = static_cast<std::size_t>(found);
        Mat by = kIdentity; // 0 as a thousandth: its normals stay finite
        by[0] = std::fmax(s.by[0], 1e-3F);
        by[5] = std::fmax(s.by[1], 1e-3F);
        by[10] = std::fmax(s.by[2], 1e-3F);
        const Mat &ib = model->joints[j].inverse_bind;
        model->mesh_scales.emplace_back(j, mul(inverse(ib), mul(by, ib)));
    }

    // the twists: the joints named, the turned joint's parent, its descendants (parents come first)
    for (const TwistLine &line : bones.twists) {
        std::array<std::size_t, 4> at{};
        bool found = true;
        for (std::size_t i = 0; i < 4 && found; ++i) {
            const int named = joint_named(*model, line.joints[i]);
            found = named >= 0;
            at[i] = found ? static_cast<std::size_t>(named) : 0U;
            if (!found) {
                std::fprintf(stderr, "models: %s: twist: no joint %s; check its name; the line is ignored\n",
                             name.c_str(), line.joints[i].c_str());
                khdays_log("models: %s: twist: no joint %s; check its name; the line is ignored", name.c_str(),
                           line.joints[i].c_str());
            }
        }
        if (!found || model->joints[at[0]].parent < 0)
            continue;
        Twist t;
        t.joint = at[0];
        t.by = at[1];
        t.parent = static_cast<std::size_t>(model->joints[at[0]].parent);
        t.to = at[3];
        t.share = line.share;
        t.swing = line.swing;
        if (!line.except.empty()) {
            const int named = joint_named(*model, line.except);
            if (named < 0) {
                khdays_log("models: %s: swing: no joint %s; check its name in bones.txt; it swings every way",
                           name.c_str(), line.except.c_str());
            } else {
                const auto &a = model->joints[at[0]].head, &b = model->joints[static_cast<std::size_t>(named)].head;
                t.except = true;
                t.except_way = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            }
        }
        const Mat from = inverse(model->joints[at[2]].inverse_bind), to = inverse(model->joints[at[3]].inverse_bind);
        const std::array<float, 3> d{to[12] - from[12], to[13] - from[13], to[14] - from[14]};
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!line.swing && l < 1e-6F) {
            std::fprintf(
                stderr,
                "models: %s: twist: %s and %s are in one place (no axis); name two joints apart; the line is ignored\n",
                name.c_str(), line.joints[2].c_str(), line.joints[3].c_str());
            continue;
        }
        if (!line.swing)
            t.axis = {d[0] / l, d[1] / l, d[2] / l};
        std::vector<char> under(model->joints.size(), 0);
        for (std::size_t j = 0; j < model->joints.size(); ++j) {
            const int up = model->joints[j].parent;
            under[j] = j == t.joint || (up >= 0 && under[static_cast<std::size_t>(up)]) ? 1 : 0;
            if (under[j])
                t.turned.push_back(j);
        }
        model->twists.push_back(std::move(t));
    }

    // the meshes and their textures, each texture as its material takes its alpha (glTF alphaMode): kept; all opaque for
    // an opaque material; for a mask each texel drawn or not by the cutoff, opaque; where the file does not say (FBX),
    // translucent when any texel or vertex colour is part way
    using AlphaMode = assets::NeutralMesh::AlphaMode;
    std::map<std::string, const assets::DecodedTexture *> texture_of;
    for (const auto &[key, texture] : mod.textures)
        texture_of[key] = &texture;
    struct Made {
        int replacement = -1, w = 0, h = 0;
        bool translucent = false;
    };
    std::map<std::string, Made> made;
    // the face's blocks (a KH2 model's eyes and mouth): the textures under them made changing
    std::set<std::string> face_keys;
    for (auto &a : mod.texture_animations) {
        if (a.sprites.empty() || a.scripts.empty())
            continue;
        face_keys.insert(a.texture);
        auto block = std::make_unique<FaceBlock>();
        block->animation = std::move(a);
        const auto &slots = block->animation.slot_script;
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i] == static_cast<int>(block->animation.scripts.size()) - 1)
                block->last_slot = block->animation.first_slot + static_cast<int>(i);
        block->player = std::make_unique<khdays::face::Player>(
            block->animation, static_cast<std::uint32_t>(0x9E3779B9U * (model->face_blocks.size() + 1)));
        model->face_blocks.push_back(std::move(block));
    }
    auto texture_for = [&](const std::string &key, AlphaMode mode, float cutoff) -> const Made * {
        const auto t = texture_of.find(key);
        if (t == texture_of.end())
            return nullptr;
        const std::string tag = mode == AlphaMode::opaque ? "@opaque"
                                : mode == AlphaMode::mask ? "@mask" + std::to_string(cutoff)
                                                          : "";
        if (const auto m = made.find(key + tag); m != made.end())
            return &m->second;
        const auto &texture = *t->second;
        // kept for as long as the port runs: the renderer reads it from any thread
        const std::size_t bytes = texture.rgba.size();
        auto *pixels = static_cast<std::uint8_t *>(std::malloc(bytes));
        if (pixels == nullptr)
            return nullptr;
        std::memcpy(pixels, texture.rgba.data(), bytes);
        bool partial = false;
        const auto least = static_cast<std::uint8_t>(std::fmin(255.0F, std::fmax(0.0F, std::ceil(cutoff * 255.0F))));
        for (std::size_t i = 3; i < bytes; i += 4) {
            if (mode == AlphaMode::opaque)
                pixels[i] = 255;
            if (mode == AlphaMode::mask)
                pixels[i] = pixels[i] >= least ? 255 : 0;
            partial = partial || (pixels[i] != 0 && pixels[i] != 255);
        }
        Made m;
        if (face_keys.count(key)) { // changing: the face's sprites patched in as they play
            FaceTexture f;
            f.key = key;
            f.width = texture.width;
            f.height = texture.height;
            f.picture.assign(pixels, pixels + bytes);
            f.alpha = mode == AlphaMode::opaque ? 1 : mode == AlphaMode::mask ? 2 : 0;
            f.least = least;
            std::free(pixels);
            f.replacement = khdays_textures_add_changing((name + "/" + key + tag + "@face").c_str(), texture.width,
                                                         texture.height, 0);
            if (f.replacement < 0)
                return nullptr;
            std::memcpy(khdays_textures_change_begin(f.replacement), f.picture.data(), bytes);
            khdays_textures_change_end(f.replacement);
            m.replacement = f.replacement;
            model->face_textures.push_back(std::move(f));
        } else {
            m.replacement =
                khdays_textures_add((name + "/" + key + tag).c_str(), texture.width, texture.height, pixels);
        }
        m.w = texture.width;
        m.h = texture.height;
        m.translucent = mode == AlphaMode::blend || (mode == AlphaMode::unknown && partial);
        return &(made[key + tag] = m);
    };
    std::size_t vertices = 0, triangles = 0;
    for (auto &in : mod.model.meshes) {
        Mesh out;
        out.shadow_sprite = in.material_name == "ef_htd" || in.material_name == "kage00";
        out.indices = std::move(in.indices);
        out.vertices = std::move(in.vertices);
        for (auto &v : out.vertices) {
            for (auto &jt : v.joints) {
                const auto it = joint_of_palette.find(jt);
                jt = it != joint_of_palette.end() ? static_cast<std::uint32_t>(it->second) : 0xffffffffU;
            }
        }
        out.double_sided = model->cull == 1 ? false : model->cull == 2 ? true : in.double_sided;
        if (const Made *t = texture_for(in.texture_name, in.alpha_mode, in.alpha_cutoff)) {
            out.replacement = t->replacement;
            out.tex_w = static_cast<float>(t->w);
            out.tex_h = static_cast<float>(t->h);
            out.translucent = t->translucent;
        }
        bool partial_vertex = false;
        for (const auto &v : out.vertices)
            partial_vertex = partial_vertex || (v.color[3] >= 8 && v.color[3] < 248);
        if (in.alpha_mode == AlphaMode::blend || in.alpha_mode == AlphaMode::unknown)
            out.translucent = out.translucent || in.alpha_mode == AlphaMode::blend || partial_vertex;
        // (a file that does not say, with its vertex alphas all 0 or all 255: painted without alpha, not cut)
        out.vertex_alpha = in.alpha_mode == AlphaMode::opaque                       ? VertexAlpha::ignored
                           : in.alpha_mode == AlphaMode::unknown && !partial_vertex ? VertexAlpha::ignored
                           : out.translucent                                        ? VertexAlpha::blended
                                                                                    : VertexAlpha::cut;
        out.cutoff = in.alpha_mode == AlphaMode::mask ? in.alpha_cutoff : 0.5F;
        out.scroll = in.scroll;
        out.sky = in.sky ? 1 : 0;
        for (const auto &[picture, length] : in.texture_frames) {
            Mesh::Picture shown;
            if (const Made *t = texture_for(picture, in.alpha_mode, in.alpha_cutoff)) {
                shown = {t->replacement, static_cast<float>(t->w), static_cast<float>(t->h), t->translucent};
            }
            out.frames.emplace_back(picture, length);
            out.frame_pictures.push_back(shown);
        }
        out.frames_loop = in.texture_frames_loop;
        out.frames_loop_from = in.texture_frames_loop_from;
        model->own_motion = model->own_motion || in.scroll[0] != 0.0F || in.scroll[1] != 0.0F || !out.frames.empty();
        vertices += out.vertices.size();
        triangles += out.indices.size() / 3;
        model->meshes.push_back(std::move(out));
    }
    // `hide`: the vertices these joints carry most, between those heights at rest, not drawn, and faded back in
    // below over `fade` (a gradient into the coat's dark inside). A triangle all hidden is dropped; one in the fade
    // goes to a blended mesh of its own (the same picture), the rest kept as they were: the whole body blended
    // would be drawn translucent, its parts covering each other wrongly
    for (const HideLine &h : bones.hides) {
        std::vector<std::uint8_t> joint_hidden(model->joints.size(), 0);
        for (const auto &want : h.joints) {
            const int j = joint_named(*model, want);
            if (j < 0) {
                khdays_log("models: %s: hide %s: no such joint; check its name; it is left out", name.c_str(),
                           want.c_str());
                continue;
            }
            joint_hidden[static_cast<std::size_t>(j)] = 1;
        }
        const float top = h.top * 0.01F, bottom = h.bottom * 0.01F, fade = h.fade * 0.01F;
        std::vector<Mesh> faded_meshes;
        std::size_t dropped = 0, faded = 0;
        for (Mesh &mesh : model->meshes) {
            // each vertex's alpha: 0 hidden, 1 shown, between in the fade below the bottom
            std::vector<float> alpha(mesh.vertices.size(), 1.0F);
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
                const auto &vx = mesh.vertices[v];
                const std::uint32_t j = vx.joints[heaviest_weight(vx)];
                if (j >= joint_hidden.size() || !joint_hidden[j] || vx.position[1] > top)
                    continue;
                if (vx.position[1] >= bottom)
                    alpha[v] = 0.0F;
                else if (fade > 0.0F && vx.position[1] > bottom - fade)
                    alpha[v] = (bottom - vx.position[1]) / fade;
            }
            std::vector<std::uint32_t> kept, fading;
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const float a0 = alpha[mesh.indices[t]], a1 = alpha[mesh.indices[t + 1]],
                            a2 = alpha[mesh.indices[t + 2]];
                auto &to = a0 >= 1.0F && a1 >= 1.0F && a2 >= 1.0F ? kept : fading;
                // a corner hidden: none of it, but at the bottom edge, where it fades from clear
                const float highest = std::fmax(mesh.vertices[mesh.indices[t]].position[1],
                                                std::fmax(mesh.vertices[mesh.indices[t + 1]].position[1],
                                                          mesh.vertices[mesh.indices[t + 2]].position[1]));
                if ((a0 <= 0.0F && a1 <= 0.0F && a2 <= 0.0F) ||
                    ((a0 <= 0.0F || a1 <= 0.0F || a2 <= 0.0F) && highest > bottom + fade)) {
                    ++dropped;
                    continue;
                }
                to.insert(to.end(), mesh.indices.begin() + static_cast<std::ptrdiff_t>(t),
                          mesh.indices.begin() + static_cast<std::ptrdiff_t>(t + 3));
            }
            if (!fading.empty()) {
                Mesh f = mesh; // its picture and settings; its own vertices, their alpha the fade's
                f.indices = std::move(fading);
                for (std::size_t v = 0; v < f.vertices.size(); ++v)
                    f.vertices[v].color[3] = static_cast<std::uint8_t>(
                        std::lround(std::fmin(1.0F, alpha[v]) * static_cast<float>(f.vertices[v].color[3])));
                f.vertex_alpha = VertexAlpha::blended;
                f.translucent = true;
                faded += f.indices.size() / 3;
                faded_meshes.push_back(std::move(f));
            }
            mesh.indices = std::move(kept);
        }
        for (Mesh &f : faded_meshes)
            model->meshes.push_back(std::move(f));
        khdays_log("models: %s: hide: %zu triangles not drawn, %zu fading", name.c_str(), dropped, faded);
    }
    // the sky layers in KH2's painting order: opaque and cut-out meshes first, then blended ones
    int sky_layer = 0;
    for (const bool blended : {false, true})
        for (Mesh &mesh : model->meshes)
            if (mesh.sky != 0 && mesh.translucent == blended)
                mesh.sky = ++sky_layer;
    // its own animation: the nodes its skinned meshes' joints are (with their parents) move; one that moves
    // another node would move a mesh made still on loading, which is not played
    if (!own_skeleton && !mod.animations.empty()) { // (an own skeleton keeps its nodes, below, and plays its clips)
        std::vector<char> moving(mod.nodes.size(), 0);
        for (const Joint &joint : model->joints) {
            for (int up = joint.node < 0 ? joint.room_node : -1;
                 up >= 0 && static_cast<std::size_t>(up) < mod.nodes.size() && !moving[static_cast<std::size_t>(up)];
                 up = mod.nodes[static_cast<std::size_t>(up)].parent)
                moving[static_cast<std::size_t>(up)] = 1;
        }
        for (const auto &a : mod.animations) {
            for (const auto &track : a.tracks) {
                if (track.node < 0 || static_cast<std::size_t>(track.node) >= moving.size() ||
                    !moving[static_cast<std::size_t>(track.node)]) {
                    khdays_log(
                        "models: %s: animation %s moves a node no skinned mesh follows: not played; take it out of the file",
                        name.c_str(), a.name.c_str());
                    break;
                }
            }
        }
        model->nodes = std::move(mod.nodes);
        model->animations = std::move(mod.animations);
        model->own_motion = true;
    }
    if (own_skeleton) {
        model->own = std::make_unique<OwnSkeleton>();
        OwnSkeleton &o = *model->own;
        o.character = character;
        for (const auto &node : mod.nodes)
            o.rest.push_back({node.translation, node.rotation, node.scale});
        for (const Joint &joint : model->joints) {
            if (joint.parent < 0 && joint.room_node >= 0) {
                o.root = joint.room_node;
                break;
            }
        }
        model->nodes = std::move(mod.nodes);
        // its weapon: the game's on its base's hand node, moved onto its own bone (the two at rest the same). The
        // players' overlays name it: ro_w_tg_R, mi_w_tg_R, so_w_tg00, ve_w_tg (the left hand's ro_w_tg_L is not it)
        auto ends = [](const std::string &s, const char *end) {
            const std::size_t n = std::strlen(end);
            return s.size() > n && s.compare(s.size() - n, n, end) == 0;
        };
        for (std::size_t g = 0; g < model->node_names.size() && o.game_weapon_node < 0; ++g) {
            const std::string &node_name = model->node_names[g];
            if (character->base_weapon_node != nullptr
                    ? node_name == character->base_weapon_node
                    : ends(node_name, "_w_tg_R") || ends(node_name, "_w_tg00") || ends(node_name, "_w_tg"))
                o.game_weapon_node = static_cast<int>(g);
        }
        // its bone named by weapon_bone; without one, its node named as the base's (a skeleton made like the game's)
        const std::string bone = character->weapon_bone != nullptr ? character->weapon_bone
                                 : o.game_weapon_node >= 0
                                     ? model->node_names[static_cast<std::size_t>(o.game_weapon_node)]
                                     : std::string();
        for (std::size_t i = 0; !bone.empty() && i < model->nodes.size(); ++i) {
            if (model->nodes[i].name == bone)
                o.weapon_bone = static_cast<int>(i);
        }
        if (o.weapon_bone >= 0 && o.game_weapon_node >= 0) {
            std::vector<Mat> at_rest(model->nodes.size(), kIdentity);
            for (std::size_t i = 0; i < model->nodes.size(); ++i) {
                const int parent = model->nodes[i].parent;
                const Mat local = compose(o.rest[i]);
                at_rest[i] = parent >= 0 && static_cast<std::size_t>(parent) < i
                                 ? mul(at_rest[static_cast<std::size_t>(parent)], local)
                                 : local;
            }
            Mat s = kIdentity;
            s[0] = s[5] = s[10] = k;
            if (character->has_weapon_turn) {
                // the bone's own axes, unscaled, turned (x, then y, then z)
                const auto *by = character->weapon_turn;
                o.weapon_fix = mul(inverse(s), turn_of({by[0], by[1], by[2]}));
            } else {
                // held as the base holds it: turned from the bone's axes at rest to the base node's at rest (the
                // weapon at the bone itself: the two rests' arms are not where each other's are)
                const Mat turn = axes_of(mul(inverse(at_rest[static_cast<std::size_t>(o.weapon_bone)]),
                                             rest[static_cast<std::size_t>(o.game_weapon_node)]));
                o.weapon_fix = mul(inverse(s), turn);
            }
        } else if (character->weapon_bone != nullptr) {
            khdays_log(
                "characters: %s: %s not found; check the name in its description: the weapon stays where the base's hand is",
                character->identity,
                o.weapon_bone < 0 ? (std::string("its bone ") + character->weapon_bone).c_str()
                : character->base_weapon_node != nullptr
                    ? (std::string("the base's node ") + character->base_weapon_node).c_str()
                    : "the base's weapon node (base_weapon_node names it)");
        }
        khdays_log("models: %s: %s's own skeleton (%zu nodes), its %d clips by the base's animations", name.c_str(),
                   character->identity, model->nodes.size(), character->action_count);
    }
    if (model->own_motion) {
        std::size_t scrolling = 0, framed = 0, sky = 0;
        for (const Mesh &mesh : model->meshes) {
            scrolling += mesh.scroll[0] != 0.0F || mesh.scroll[1] != 0.0F ? 1 : 0;
            framed += mesh.frames.empty() ? 0 : 1;
            sky += mesh.sky != 0 ? 1 : 0;
        }
        khdays_log("models: %s: its own animation: %zu moving parts' animations, %zu scrolling and %zu changing "
                   "textures, %zu sky meshes",
                   name.c_str(), model->animations.size(), scrolling, framed, sky);
    }
    std::size_t translucent = 0;
    for (const Mesh &mesh : model->meshes)
        translucent += mesh.translucent ? 1 : 0;
    std::fprintf(
        stderr,
        "models: %s drawn instead from %s (%zu meshes, %zu of them translucent, %zu vertices, %zu triangles, %zu joints%s)\n",
        name.c_str(), path.c_str(), model->meshes.size(), translucent, vertices, triangles, model->joints.size(),
        model->cull == 1   ? ", back faces culled"
        : model->cull == 2 ? ", both sides drawn"
                           : "");
    // the session log's diagnosis of a model that comes out wrong (KHDAYS_TRACE_MODELS with
    // the session log on): each joint, the node it follows, its turn from that node's pose,
    // and each node's place at rest
    if (tracing() && khdays_log_on()) {
        khdays_log("models: %s drawn instead from %s (%zu joints, %zu game nodes, scale %.3f)", name.c_str(),
                   path.c_str(), model->joints.size(), rest.size(), static_cast<double>(k));
        for (const Joint &joint : model->joints) {
            khdays_log(
                "models:   joint %s -> %s%s, %.0f degrees, head %.2f %.2f %.2f", joint.name.c_str(),
                joint.node >= 0 ? model->node_names[static_cast<std::size_t>(joint.node)].c_str() : "(its parent)",
                joint.exact ? " exactly" : "", static_cast<double>(joint.degrees), static_cast<double>(joint.head[0]),
                static_cast<double>(joint.head[1]), static_cast<double>(joint.head[2]));
        }
        for (std::size_t g = 0; g < rest.size(); ++g) {
            const Mat &w = rest[g];
            khdays_log("models:   node %zu %s (parent %d) at rest %.2f %.2f %.2f, scale %.3f %.3f %.3f", g,
                       model->node_names[g].c_str(), model->node_parent[g], static_cast<double>(w[12]),
                       static_cast<double>(w[13]), static_cast<double>(w[14]),
                       static_cast<double>(length({w[0], w[1], w[2]})), static_cast<double>(length({w[4], w[5], w[6]})),
                       static_cast<double>(length({w[8], w[9], w[10]})));
        }
    }
    set_rest_size(*model);
    resolve_cuffs(*model, bones.cuffs, bones.cuff_pose, name);
    load_kh2_motions(*model, path);
    return model;
}

// --- The draws -------------------------------------------------------------------------

std::unordered_map<std::string, std::unique_ptr<Model>> models; // by "<file>@<offset>"; null: not replaced
Model *active;
int depth;
Model *outer[8]; // the draws a draw's callbacks start: theirs are the game's
int shapes;
const unsigned char *model_start; // the draw's model (its NNSG3dResMdl): its shapes' lists are from here
KhdaysG3dMaterial material;
std::vector<Mat> node_matrix;
std::vector<char> node_sent;
std::vector<std::pair<void *, unsigned>> saved_joints; // the draw's matrices saved for attachments, by node
float kh2_game_share = 1.0F; // this draw: the game's own pose's share of a KH2 model's (play_kh2; 1 without)
bool kh2_pose_cut = false;   // this draw: a KH2 motion took the whole pose with no fade (play_kh2): the cloth's cue
extern "C" int khdays_controls_walk_share(unsigned node); // runtime/input/controls_hooks.c: the port's walk
extern "C" int khdays_overlay_loaded(int id);             // runtime/core/overlays.c
// this draw: `weapon_motions`' pose of the weapon (each bone's frame, metres; empty: none) and the share of it held
// as Days holds it (the game's own pose and the slots the weapon has no motion for)
std::vector<Mat> kh2_weapon_pose;
float kh2_weapon_share = 1.0F;
// the last weapon a body held with `weapon_motions`: its pose, the turn from the game's weapon frame (what the
// game draws it at) to the weapon's root, and the vblank of that body's draw (a `held` weapon's model, bones.txt)
struct HeldWeapon {
    std::vector<Mat> pose;
    Mat from_game = kIdentity;
    u32 vblank = 0;
};
HeldWeapon held_weapon;
// the last bone a `weapon` line held something on (`held <joint> rest`): the turn from it to the game's weapon frame,
// its rest (metres) and the vblank of that body's draw
struct HoldingBone {
    Mat from_game = kIdentity;
    Mat rest = kIdentity;
    u32 vblank = 0;
};
HoldingBone holding_bone;
// this draw: each game animation's KH2 slot and its section's first frame, `air` and `land` ones included
std::map<std::string, std::pair<std::size_t, float>> kh2_played;

// The movers Mover_ResolveFloor put on their floor, by owner (their render object is at owner + 0x24: the
// animation state at +4, func_ov022_020a3c78, its NNSG3dRenderObj at +0x20): the mover and the vblank
// it was last seen at.
struct Grounded {
    KhdaysRef mover = 0;
    u32 vblank = 0;
};
std::unordered_map<KhdaysRef, Grounded> grounds;

// The battle actors' states (Ov022_EnterActorState), by scene node as `grounds` (khdays_models_actor_state): the
// state, the vblank it entered it at, and the vblank it last left the damage in the air at
constexpr int kAirDamageState = 5;  // damage in the air, a launch included (Ov022_ResolveGuardBreakState)
constexpr int kLandingState = 0x16; // landing (Ov022_EnterActorState: animation 4, b_ro_A004)
constexpr u32 kLandedOutOf = 8;     // vblanks: landing this soon after state 5 is landing out of it
struct ActorState {
    int state = -1;
    u32 entered = 0, left_air = 0;
};
std::unordered_map<KhdaysRef, ActorState> actor_states;

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
std::map<std::string, FloorTrace> floor_traces;

void trace_floor(const Model &m, const std::string &state)
{
    if (!tracing())
        return;
    FloorTrace &t = floor_traces[m.name];
    if (t.said != state)
        khdays_log("models: %s: IKAdjust %s", m.name.c_str(), state.c_str());
    t.said = state;
}

Floor floor_of(const Model &m, const Mat &base)
{
    Floor f;
    float camera[16];
    const bool feet = (m.kh2_skeleton != nullptr && !m.kh2_skeleton->feet.empty()) ||
                      (m.kh2_own_skeleton != nullptr && !m.kh2_own_skeleton->feet.empty() && !m.kh2_own_slots.empty());
    if (!m.kh2_ik_adjust || !feet || drawn_object == nullptr || !khdays_g3d_camera(camera)) {
        trace_floor(m, "off: ik_adjust off, no feet in the model or no camera yet");
        return f;
    }
    const KhdaysRef object = KHDAYS_REF(drawn_object);
    const auto found = grounds.find(object - 0x24U);
    if (found == grounds.end() || khdays_events_vblank_count() - found->second.vblank > 4U) {
        if (tracing()) { // the actors settled lately near its render object, to find the link to its mover
            std::string near;
            for (const auto &[record, g] : grounds) {
                const long long off = static_cast<long long>(object) - static_cast<long long>(record);
                if (khdays_events_vblank_count() - g.vblank > 4U || off < -0x4000 || off > 0x4000)
                    continue;
                char one[64];
                std::snprintf(one, sizeof(one), " %08x (render object - record = %lld)", record, off);
                near += one;
            }
            trace_floor(m, found == grounds.end() ? "off: no mover's owner at render object - 0x24; near:" +
                                                        (near.empty() ? std::string(" none") : near)
                                                  : "off: its mover was not put on a floor lately");
        }
        return f;
    }
    const auto *mover = static_cast<const unsigned char *>(KHDAYS_W(found->second.mover));
    KhdaysRef record = 0;
    s32 speed = 0;
    u16 index = 0;
    std::memcpy(&record, mover, 4);
    std::memcpy(&speed, mover + 0x38, 4);
    std::memcpy(&index, mover + 0x46, 2);
    const bool on_floor = speed == static_cast<s32>(0x80000000U); // Mover_ResolveFloor: landed
    if (record != found->first || static_cast<std::int16_t>(index) < 0 || !on_floor) {
        trace_floor(m, !on_floor ? "off: its actor is not on the floor" : "off: its mover changed");
        return f;
    }
    // the locals the game's functions read are passed by reference: the game thread's stack must be
    // in the game's memory (it is, as the game's own casts pass theirs)
    int here = 0;
    if (static_cast<std::uintptr_t>(reinterpret_cast<const unsigned char *>(&here) - khdays_window) >= 0x100000000ULL) {
        trace_floor(m, "off: the game thread's stack is outside the game's memory");
        return f;
    }
    trace_floor(m, "on");
    Mat view_of_world;
    std::copy(camera, camera + 16, view_of_world.begin());
    Mat cm = kIdentity; // KH2's centimetres to the model's metres
    cm[0] = cm[5] = cm[10] = 0.01F;
    f.world = mul(inverse(view_of_world), mul(base, cm)); // Days' world is y up, as the solver's
    FloorTrace *t = tracing() ? &floor_traces[m.name] : nullptr;
    if (t != nullptr && ++t->draws % 60U == 0U) {
        s32 at[3] = {};
        std::memcpy(at, static_cast<const unsigned char *>(KHDAYS_W(record)) + 0xa8, sizeof(at));
        khdays_log("models: %s: IKAdjust %u rays, %u hits; actor at %.2f %.2f %.2f, model at %.2f %.2f %.2f; "
                   "last ray %.2f %.2f %.2f to %.2f %.2f %.2f, hit %.2f %.2f %.2f (Days' world)",
                   m.name.c_str(), t->rays, t->hits, static_cast<double>(at[0]) / 4096.0,
                   static_cast<double>(at[1]) / 4096.0, static_cast<double>(at[2]) / 4096.0,
                   static_cast<double>(f.world[12]), static_cast<double>(f.world[13]), static_cast<double>(f.world[14]),
                   static_cast<double>(t->from[0]), static_cast<double>(t->from[1]), static_cast<double>(t->from[2]),
                   static_cast<double>(t->to[0]), static_cast<double>(t->to[1]), static_cast<double>(t->to[2]),
                   static_cast<double>(t->hit[0]), static_cast<double>(t->hit[1]), static_cast<double>(t->hit[2]));
        t->rays = t->hits = 0;
    }
    f.ray = [record, index, t](const std::array<float, 3> &from, const std::array<float, 3> &to,
                               std::array<float, 3> &hit) {
        if (t != nullptr) {
            ++t->rays;
            t->from = from;
            t->to = to;
        }
        struct Vec {
            s32 x, y, z;
        };
        struct Params { // CollCastParams
            KhdaysRef origin, direction;
            s32 radius;
            u16 unit, flags;
            KhdaysRef exclude;
        };
        const auto fx = [](float v) { return static_cast<s32>(std::lround(v * 4096.0F)); };
        Vec origin{fx(from[0]), fx(from[1]), fx(from[2])};
        Vec direction{fx(to[0] - from[0]), fx(to[1] - from[1]), fx(to[2] - from[2])};
        Vec at{};
        Params params{KHDAYS_REF(&origin), KHDAYS_REF(&direction), 0, 0, 0, record};
        const KhdaysRef world = GetTrackEntryBase(index);
        if (world == 0)
            return false;
        const KhdaysRef h = Collision_RunRayCast(world, KHDAYS_REF(&params));
        if (h == 0)
            return false;
        s32 distance = 0;
        std::memcpy(&distance, static_cast<const unsigned char *>(KHDAYS_W(h)) + 12, 4);
        Vec3ScaleAddQ27(distance, KHDAYS_REF(&direction), KHDAYS_REF(&origin), KHDAYS_REF(&at));
        hit = {static_cast<float>(at.x) / 4096.0F, static_cast<float>(at.y) / 4096.0F,
               static_cast<float>(at.z) / 4096.0F};
        if (t != nullptr) {
            ++t->hits;
            t->hit = hit;
        }
        return true;
    };
    f.on = true;
    return f;
}

std::vector<Mat> d, k, skin;
u32 drawn_animation; // the render object's joint animation (an NNSG3dAnmObj), 0 none
u32 next_animation;  // the one khdays_models_draw_animation gave, for the draw that follows
std::vector<KhdaysG3dClipVertex> clip;
std::vector<std::array<khdays::secondary::Vec3, 3>> clip_triangles; // `clip_world`'s volumes this draw

// --- A mod's character's own skeleton ------------------------------------------------------

Mat compose(const Trs &x)
{
    const float qx = x.r[0], qy = x.r[1], qz = x.r[2], qw = x.r[3];
    Mat m = kIdentity;
    m[0] = (1 - 2 * (qy * qy + qz * qz)) * x.s[0];
    m[1] = (2 * (qx * qy + qz * qw)) * x.s[0];
    m[2] = (2 * (qx * qz - qy * qw)) * x.s[0];
    m[4] = (2 * (qx * qy - qz * qw)) * x.s[1];
    m[5] = (1 - 2 * (qx * qx + qz * qz)) * x.s[1];
    m[6] = (2 * (qy * qz + qx * qw)) * x.s[1];
    m[8] = (2 * (qx * qz + qy * qw)) * x.s[2];
    m[9] = (2 * (qy * qz - qx * qw)) * x.s[2];
    m[10] = (1 - 2 * (qx * qx + qy * qy)) * x.s[2];
    m[12] = x.t[0];
    m[13] = x.t[1];
    m[14] = x.t[2];
    return m;
}

// `a` to `b` by `w` (0-1): positions and scales straight, rotations normalised on the short way.
Trs mix(const Trs &a, const Trs &b, float w)
{
    Trs out;
    for (int c = 0; c < 3; ++c) {
        out.t[static_cast<std::size_t>(c)] = a.t[static_cast<std::size_t>(c)] +
                                             (b.t[static_cast<std::size_t>(c)] - a.t[static_cast<std::size_t>(c)]) * w;
        out.s[static_cast<std::size_t>(c)] = a.s[static_cast<std::size_t>(c)] +
                                             (b.s[static_cast<std::size_t>(c)] - a.s[static_cast<std::size_t>(c)]) * w;
    }
    float dot = 0.0F;
    for (int c = 0; c < 4; ++c)
        dot += a.r[static_cast<std::size_t>(c)] * b.r[static_cast<std::size_t>(c)];
    const float sign = dot < 0.0F ? -1.0F : 1.0F;
    float len = 0.0F;
    for (int c = 0; c < 4; ++c) {
        const auto i = static_cast<std::size_t>(c);
        out.r[i] = a.r[i] + (sign * b.r[i] - a.r[i]) * w;
        len += out.r[i] * out.r[i];
    }
    len = std::sqrt(len);
    for (int c = 0; c < 4 && len > 1e-8F; ++c)
        out.r[static_cast<std::size_t>(c)] /= len;
    return out;
}

// The clip's pose at `t` seconds over `pose` (the nodes its tracks do not move keep theirs).
void apply_clip(const Clip &source, float t, std::vector<Trs> &pose)
{
    using Path = khdays::assets::ImportedTrack::Path;
    for (std::size_t i = 0; i < source.animation.tracks.size(); ++i) {
        const auto &track = source.animation.tracks[i];
        const int node = source.node_of_track[i];
        const std::size_t keys = track.times.size();
        if (node < 0 || static_cast<std::size_t>(node) >= pose.size() || keys == 0)
            continue;
        const std::size_t n = track.path == Path::rotation ? 4 : 3;
        if (track.values.size() < keys * n)
            continue;
        const std::size_t after =
            static_cast<std::size_t>(std::upper_bound(track.times.begin(), track.times.end(), t) - track.times.begin());
        const std::size_t k0 = after == 0 ? 0 : after - 1, k1 = after < keys ? after : keys - 1;
        const float span = track.times[k1] - track.times[k0];
        const float w =
            track.step || span <= 0.0F ? 0.0F : std::fmin(1.0F, std::fmax(0.0F, (t - track.times[k0]) / span));
        Trs a, b;
        Trs &out = pose[static_cast<std::size_t>(node)];
        if (track.path == Path::rotation) {
            std::copy(&track.values[k0 * 4], &track.values[k0 * 4] + 4, a.r.begin());
            std::copy(&track.values[k1 * 4], &track.values[k1 * 4] + 4, b.r.begin());
            out.r = mix(a, b, w).r;
        } else {
            std::array<float, 3> v{};
            for (std::size_t c = 0; c < 3; ++c)
                v[c] = track.values[k0 * 3 + c] + (track.values[k1 * 3 + c] - track.values[k0 * 3 + c]) * w;
            (track.path == Path::translation ? out.t : out.s) = v;
        }
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
    const V hip = point(k[j0], a.head), knee_was = point(k[j0], b.head), end_was = point(k[j1], c.head);
    const float l0 = length(sub(knee_was, hip)), l1 = length(sub(end_was, knee_was));
    const V hd = translation(node_matrix[static_cast<std::size_t>(a.node)]),
            kd = translation(node_matrix[static_cast<std::size_t>(b.node)]),
            ed = translation(node_matrix[static_cast<std::size_t>(c.node)]);
    const float game = length(sub(kd, hd)) + length(sub(ed, kd));
    if (l0 < kTiny || l1 < kTiny || game < kTiny)
        return;
    const float s = (l0 + l1) / game;
    // `exact`: to the game's end node itself (its offset turned as that node is), else the game's limb scaled
    const V to = limb_line.exact ? sub(add(ed, vec(d[static_cast<std::size_t>(c.node)], limb_line.offset), 1.0F), hip)
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
    k[j0] = about(hip, arc(sub(knee_was, hip), sub(knee, hip), degrees), k[j0]);
    for (std::size_t j = j0 + 1; j < m.joints.size(); ++j) {
        bool below = false;
        for (int up = m.joints[j].parent; up >= 0 && !below; up = m.joints[static_cast<std::size_t>(up)].parent)
            below = up == static_cast<int>(j0);
        if (!below)
            continue;
        k[j] = pose(j);
        if (j == j1) {
            const V at = point(k[j1], b.head);
            k[j1] = about(at, arc(sub(point(k[j1], c.head), at), sub(end, at), degrees), k[j1]);
        }
    }
}

// The character's clip `relative`, read the first time it is asked for: its file's first animation, or the one a
// "#<name>" after the file names, its tracks matched to the model's nodes by name.
Clip &clip_of(OwnSkeleton &o, const std::vector<khdays::assets::ImportedNode> &nodes, const std::string &relative)
{
    auto it = o.clips.find(relative);
    if (it != o.clips.end())
        return it->second;
    Clip &made = o.clips[relative];
    char path[1024];
    if (!khdays_characters_file(o.character, relative.c_str(), path, static_cast<int>(sizeof(path))))
        return made;
    try {
        const auto file = u8(path);
        const auto extension = file.extension();
        auto clip_file = extension == ".fbx" || extension == ".FBX" ? khdays::assets::import_fbx(file)
                                                                    : khdays::assets::import_gltf(file);
        if (clip_file.animations.empty()) {
            khdays_log("characters: %s: %s has no animation; name a file with one in its description",
                       o.character->identity, relative.c_str());
            return made;
        }
        const char *const at = khdays_characters_clip_mark(relative.c_str());
        const auto mark = at != nullptr ? static_cast<std::size_t>(at - relative.c_str()) : std::string::npos;
        auto chosen = clip_file.animations.begin();
        if (mark != std::string::npos) {
            const std::string wanted = relative.substr(mark + 1);
            chosen = std::find_if(clip_file.animations.begin(), clip_file.animations.end(),
                                  [&](const khdays::assets::ImportedAnimation &a) { return a.name == wanted; });
            if (chosen == clip_file.animations.end()) {
                khdays_log("characters: %s: %s has no animation named %s; check the name after # in its description",
                           o.character->identity, relative.substr(0, mark).c_str(), wanted.c_str());
                return made;
            }
        }
        made.animation = std::move(*chosen);
        std::size_t matched = 0;
        for (const auto &track : made.animation.tracks) {
            int found = -1;
            if (track.node >= 0 && static_cast<std::size_t>(track.node) < clip_file.nodes.size()) {
                const std::string &wanted = clip_file.nodes[static_cast<std::size_t>(track.node)].name;
                for (std::size_t n = 0; n < nodes.size() && found < 0; ++n) {
                    if (nodes[n].name == wanted)
                        found = static_cast<int>(n);
                }
            }
            made.node_of_track.push_back(found);
            matched += found >= 0 ? 1 : 0;
        }
        made.ok = matched > 0;
        khdays_log("characters: %s: %s read: %.2f s, %zu of %zu tracks on the model's nodes", o.character->identity,
                   relative.c_str(), static_cast<double>(made.animation.duration), matched,
                   made.animation.tracks.size());
    } catch (const std::exception &e) {
        khdays_log("characters: %s: %s cannot be read (%s); export it again", o.character->identity, relative.c_str(),
                   e.what());
    }
    return made;
}

// The game's joint animation `anm` (an NNSG3dAnmObj: its frame in 20.12 at +0, its NNSG3dResJntAnm at +8, whose
// frames are a u16 at +4): its name (its BCA0's), how far it is (0-1) and its frame.
struct GameAnimation {
    std::string name;
    float phase = 0.0F;
    float frame = 0.0F;
    float frames = 0.0F; // its length
};

// The name of the animation whose data the game has at `anm` (a J0AC, a PAT0's animation), "" unknown
const std::string &animation_name(u32 anm)
{
    static const std::string unknown;
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (anm == 0 || !khdays_origin_of_ram(anm, &file, &offset, &run))
        return unknown;
    AnimationName &known = animation_names[anm];
    if (known.file != file || known.offset != offset) {
        char name[64] = "";
        if (!khdays_names_animation(file, offset, name, static_cast<int>(sizeof(name))))
            name[0] = '\0';
        known = {file, offset, name};
    }
    return known.name;
}

GameAnimation game_animation(u32 anm)
{
    GameAnimation out;
    if (anm == 0 || !khdays_game_ram(anm))
        return out;
    const auto frame = *static_cast<const s32 *>(KHDAYS_W(anm));
    const u32 resource = *static_cast<const u32 *>(KHDAYS_W(anm + 8));
    if (!khdays_game_ram(resource))
        return out;
    const u16 frames = *static_cast<const u16 *>(KHDAYS_W(resource + 4));
    out.name = animation_name(resource);
    if (out.name.empty()) {
        char unnamed[16];
        std::snprintf(unnamed, sizeof(unnamed), "@%08x", resource);
        out.name = unnamed;
    }
    out.frame = static_cast<float>(frame) / 4096.0F;
    out.frames = static_cast<float>(frames);
    out.phase = frames > 0 ? std::fmin(1.0F, std::fmax(0.0F, out.frame / frames)) : 0.0F;
    return out;
}

// The vblanks (60 a second) a clip at its own speed goes on after the game's animation last moved: past them, with
// the game's animation short of its end, the game holds it (a hit's stop), and the clip stands with it.
constexpr u32 kStillVblanks = 4;
// The game's animation back to an earlier frame from its last few: it loops; from elsewhere it began it again.
constexpr float kWrapFrames = 3.0F;

// The point of a clip `length` long at `t` seconds of its own time: past its end, held there or looped back to
// second `from` of it.
float clip_time(float t, float length, bool loops, float from)
{
    if (length <= 0.0F)
        return 0.0F;
    if (!loops || t < length)
        return std::fmin(t, length);
    from = std::fmin(std::fmax(from, 0.0F), length);
    return length - from <= 0.0F ? length : from + std::fmod(t - from, length - from);
}

// The own skeleton's nodes' places for this draw: the clip given to the animation the game plays, at its own speed while
// the game's animation moves or is held at its end, past its end as its action's `loop` says (else looped once the game's
// is seen to loop); or with `time game` at the same point of the game's; a native plugin's clip while it plays one (kh.
// characters' play); blended from the pose shown when the animation changes or begins again; its root's travel the
// game's unless the action keeps the clip's.
std::vector<Mat> own_pose(Model &m)
{
    OwnSkeleton &o = *m.own;
    unsigned since = 0;
    int loop = 0;
    const KhdaysCharacterAction *played = khdays_characters_played(o.character, &since, &loop);
    const GameAnimation g = played != nullptr ? GameAnimation{played->game} : game_animation(drawn_animation);
    const KhdaysCharacterAction *action =
        played != nullptr ? played : khdays_characters_action(o.character, g.name.c_str());
    const char *file = action != nullptr ? action->clip : o.character->fallback;
    const Clip *chosen = file != nullptr ? &clip_of(o, m.nodes, file) : nullptr;
    const float length = chosen != nullptr && chosen->ok ? chosen->animation.duration : 0.0F;
    const u32 now = khdays_events_vblank_count();
    bool again = false; // the game began its animation again
    if (played != nullptr) {
        o.clock = static_cast<double>(now - since) / 60.0;
        o.clock_vblank = o.moved_vblank = now;
    } else if (g.name != o.playing) {
        o.clock = static_cast<double>(g.phase * length);
        o.clock_vblank = o.moved_vblank = now;
        o.last_frame = g.frame;
    } else {
        if (g.frame != o.last_frame) {
            if (g.frame < o.last_frame && o.last_frame + kWrapFrames >= g.frames) {
                if (o.looping.insert(g.name).second && tracing() && khdays_log_on())
                    khdays_log("characters: %s: the game loops %s (%.0f frames)", o.character->identity, g.name.c_str(),
                               static_cast<double>(g.frames));
            } else if (g.frame < o.last_frame) {
                o.clock = static_cast<double>(g.phase * length);
                again = true;
            }
            o.last_frame = g.frame;
            o.moved_vblank = now;
        }
        const bool held = now - o.moved_vblank > kStillVblanks && g.frame + 1.5F < g.frames;
        if (!again && !held && !khdays_link_paused())
            o.clock += static_cast<double>(now - o.clock_vblank) / 60.0;
        o.clock_vblank = now;
    }
    std::vector<Trs> pose = o.rest;
    if (chosen != nullptr) {
        if (played != nullptr && !loop && (!chosen->ok || o.clock >= length))
            khdays_characters_play_none(o.character->number); // played once: the game's again from the next draw
        if (chosen->ok && action != nullptr && action->game_time && played == nullptr) {
            apply_clip(*chosen, g.phase * length, pose);
        } else if (chosen->ok) {
            const int mode = action != nullptr && played == nullptr ? action->loop : KHDAYS_CHARACTER_LOOP_GAME;
            const bool loops = played != nullptr                   ? loop != 0
                               : mode == KHDAYS_CHARACTER_LOOP_YES ? true
                               : mode == KHDAYS_CHARACTER_LOOP_NO  ? false
                                                                   : o.looping.count(g.name) != 0;
            apply_clip(*chosen,
                       clip_time(static_cast<float>(o.clock), length, loops,
                                 mode == KHDAYS_CHARACTER_LOOP_YES ? action->loop_from : 0.0F),
                       pose);
        }
    }
    if (o.root >= 0 && static_cast<std::size_t>(o.root) < pose.size() && (action == nullptr || !action->root_keep)) {
        const auto r = static_cast<std::size_t>(o.root);
        pose[r].t[0] = o.rest[r].t[0];
        pose[r].t[2] = o.rest[r].t[2];
    }
    if (g.name != o.playing || again) {
        if (again) {
        } else if (action == nullptr && !g.name.empty() && o.told.insert(g.name).second)
            khdays_log("characters: %s: no clip for %s: %s", o.character->identity, g.name.c_str(),
                       o.character->fallback != nullptr ? "its fallback plays" : "its rest pose");
        else if (tracing() && khdays_log_on())
            khdays_log("characters: %s plays %s (%.0f frames, from frame %.1f) with %s", o.character->identity,
                       g.name.c_str(), static_cast<double>(g.frames), static_cast<double>(g.frame),
                       file != nullptr ? file : "its rest pose");
        o.from = o.shown;
        o.playing = g.name;
        o.blend_start = now;
        o.blend_frames =
            static_cast<u32>((action != nullptr ? action->blend_ms : KHDAYS_CHARACTER_BLEND_MS) * 60 / 1000);
    }
    if (now - o.blend_start < o.blend_frames && o.from.size() == pose.size()) {
        const float w = static_cast<float>(now - o.blend_start) / static_cast<float>(o.blend_frames);
        for (std::size_t n = 0; n < pose.size(); ++n)
            pose[n] = mix(o.from[n], pose[n], w);
    }
    o.shown = pose;
    std::vector<Mat> world(pose.size(), kIdentity);
    for (std::size_t n = 0; n < pose.size(); ++n) {
        const int parent = n < m.nodes.size() ? m.nodes[n].parent : -1;
        const Mat local = compose(pose[n]);
        world[n] = parent >= 0 && static_cast<std::size_t>(parent) < n
                       ? mul(world[static_cast<std::size_t>(parent)], local)
                       : local;
    }
    return world;
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
    const auto at = [&](int frame, int of) { return point(k[ju(frame)], joints[ju(of)].head); };
    std::vector<std::uint8_t> fixed(joints.size(), 0);
    const auto repose = [&] {
        for (std::size_t j = 0; j < joints.size(); ++j)
            if (!fixed[j])
                k[j] = pose(j);
    };
    const int base = sp.chain.front(), last = sp.chain.back();
    const V base_at = point(d[ju(sp.base_node)], joints[ju(base)].head);
    k[ju(sp.root)] = mul(moved_by(sub(base_at, at(base, base))), k[ju(sp.root)]);
    fixed[ju(sp.root)] = 1;
    repose();
    if (sp.pelvis >= 0) {
        const Joint &pj = joints[ju(sp.pelvis)];
        k[ju(sp.pelvis)] =
            place(pj.parent >= 0 ? at(pj.parent, sp.pelvis) : base_at, d[ju(sp.base_node)], pj.correction, pj.head);
        const V hp = at(sp.pelvis, sp.pelvis);
        k[ju(sp.pelvis)] = mul(
            turned_onto(hp, sub(at(sp.pelvis, sp.hips), hp), sub(point(d[ju(pj.node)], joints[ju(sp.hips)].head), hp)),
            k[ju(sp.pelvis)]);
        fixed[ju(sp.pelvis)] = 1;
        repose();
    }
    for (const int c : sp.chain)
        fixed[ju(c)] = 1;
    const V target = point(d[ju(joints[ju(sp.end)].node)], joints[ju(sp.end)].head);
    const auto turn_from = [&](std::size_t from, const V &pivot, const Mat &r) {
        const Mat x = mul(moved_by(pivot), mul(r, moved_by({-pivot[0], -pivot[1], -pivot[2]})));
        for (std::size_t c = from; c < sp.chain.size(); ++c)
            k[ju(sp.chain[c])] = mul(x, k[ju(sp.chain[c])]);
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
            k[ju(sp.chain[c])] = mul(
                moved_by(q), mul(along, mul(moved_by({-heads[c][0], -heads[c][1], -heads[c][2]}), k[ju(sp.chain[c])])));
        }
    }
    repose();
}

// A room's joint that its own animation moves: its node's matrix now (node_worlds), else nullptr.
const Mat *moved_world(const Joint &joint, const std::vector<Mat> &moved)
{
    if (joint.node >= 0 || joint.room_node < 0 || static_cast<std::size_t>(joint.room_node) >= moved.size())
        return nullptr;
    return &moved[static_cast<std::size_t>(joint.room_node)];
}

// The hands something is attached to in this draw (a bit per Model::hands). The drawn object's scene node (its render
// object at +0x20) lists its children: first at +0xc0, next at +0xc4, parent at +0xbc, the joint at +0xc8
// (SceneNode_AttachToModelJoint: Ov023_CmdAttachWeapons, Ov023_Cmd_ParentEntityToEntity). A child counts while its own
// render object was drawn lately.
unsigned held_hands(const Model &m)
{
    const auto word = [](KhdaysRef a, u32 &out) {
        if (!khdays_game_ram(a) || !khdays_game_ram(a + 3U))
            return false;
        std::memcpy(&out, KHDAYS_W(a), 4);
        return true;
    };
    if (drawn_object == nullptr)
        return 0U;
    const u32 now = khdays_events_vblank_count();
    const KhdaysRef node = KHDAYS_REF(drawn_object) - 0x20U;
    unsigned held = 0U;
    u32 child = 0U;
    if (!word(node + 0xc0U, child))
        return 0U;
    for (int n = 0; child != 0U && n < 32; ++n) {
        u32 parent = 0U, joint = 0U, next = 0U;
        if (!word(child + 0xbcU, parent) || parent != node || !word(child + 0xc8U, joint) || !word(child + 0xc4U, next))
            break; // not a scene node's list (a render object the game keeps elsewhere)
        const std::size_t g = joint & 0xffffU;
        const auto seen = drawn_lately.find(child + 0x20U);
        if (seen != drawn_lately.end() && now - seen->second <= 6U && g < m.node_hand.size()) {
            if (m.node_hand[g] >= 0) {
                held |= 1U << static_cast<unsigned>(m.node_hand[g]);
            } else if (tracing()) {
                static std::set<std::string> told;
                if (told.insert(m.name + "/" + m.node_names[g]).second)
                    khdays_log("models: %s: something is attached to %s, not on a hand", m.name.c_str(),
                               m.node_names[g].c_str());
            }
        }
        child = next;
    }
    return held;
}

constexpr float kPalmOver = 0.022F; // the palm's middle over what it rests on

// How deep a sphere is in the room, world space, and its way out (the push adds up as faces are met); a face only
// counts while `from` (further up the arm) stands in front of it, so a wall the arm is already through is left alone.
float room_depth(const std::vector<khdays::secondary::Triangle> &room, std::array<float, 3> p, float radius,
                 const std::array<float, 3> &from, std::array<float, 3> &push)
{
    namespace sc = khdays::secondary;
    using khdays::models::hand_add;
    float deepest = 0.0F;
    const sc::Vec3 elbow{from[0], from[1], from[2]};
    for (const sc::Triangle &t : room) {
        const std::array<float, 3> at = hand_add(p, push);
        const sc::Vec3 x{at[0], at[1], at[2]};
        if (sc::dot(elbow - t.a, t.n) < 0.0F)
            continue;
        const float side = sc::dot(x - t.a, t.n);
        if (side >= radius || side < -0.2F)
            continue;
        const sc::Vec3 q = sc::closest_on_triangle(x, t.a, t.b, t.c);
        const float dist = sc::length(x - q);
        if (dist >= radius && side >= 0.0F)
            continue;
        if (side < 0.0F && sc::length((x - t.n * side) - q) > 1e-3F)
            continue; // behind its plane, past its edge
        const float deep = side >= 0.0F ? radius - dist : radius - side;
        const sc::Vec3 way = side >= 0.0F && dist > 1e-5F ? (x - q) * (1.0F / dist) : t.n;
        push = hand_add(push, {way.x, way.y, way.z}, deep);
        deepest = std::fmax(deepest, deep);
    }
    return deepest;
}

// How deep a point (the K's output) is in the body, and its way out. The arms move, the parts do not: their K's inverses
// are worked out once a draw (pose_hands)
std::vector<Mat> hand_body_inverse;
float body_depth(const Model &m, const std::array<float, 3> &p, float radius, std::array<float, 3> &way,
                 float margins = 1.0F)
{
    float deepest = 0.0F;
    for (std::size_t b = 0; b < m.hand_body.size(); ++b) {
        const Model::HandBody &part = m.hand_body[b];
        const std::array<float, 3> local = point(hand_body_inverse[b], p);
        std::array<float, 3> out{};
        const float deep = part.shape.depth(local, part.margin * margins + radius, out);
        if (deep > deepest) {
            deepest = deep;
            way = khdays::models::hand_unit(vec(k[part.joint], out));
        }
    }
    return deepest;
}

// motions.txt's `hand`: after the arms have reached, each hand moved out of the body and the room, turned onto a surface
// it rests on, and its fingers between their poses, each lifted off what it would go into
void pose_hands(const Model &m)
{
    using namespace khdays::models;
    namespace sc = khdays::secondary;
    float camera[16];
    if (!khdays_g3d_camera(camera))
        return;
    hand_body_inverse.resize(m.hand_body.size());
    for (std::size_t b = 0; b < m.hand_body.size(); ++b)
        hand_body_inverse[b] = inverse(k[m.hand_body[b].joint]);
    Mat view;
    std::copy(camera, camera + 16, view.begin());
    const Mat world = inverse(view);
    Kh2Actor &actor = actor_of(m);
    const u32 now = khdays_events_vblank_count();
    const u32 gap = now - actor.hand_vblank;
    // a body newly drawn takes its shares at once: nothing shows closing the hand
    const bool fresh = actor.hand_states.size() != m.hands.size() || gap > 12U;
    actor.hand_vblank = now;
    actor.hand_states.resize(m.hands.size());
    const float dt = static_cast<float>(gap) / 60.0F;
    const unsigned held = held_hands(m);
    const bool report = tracing() && (fresh || held != actor.hands_held || now / 30U != (now - gap) / 30U);
    actor.hands_held = held;
    // how fast the body travels: a cut's jump does not count
    std::array<float, 3> body_way{};
    float body_speed = 0.0F;
    if (m.hand_walk_joint >= 0) {
        const auto it = std::find(m.kh2_bone.begin(), m.kh2_bone.end(), m.hand_walk_joint);
        if (it != m.kh2_bone.end()) {
            const auto j = static_cast<std::size_t>(it - m.kh2_bone.begin());
            const auto at = point(world, point(k[j], m.joints[j].head));
            if (!fresh && gap > 0U) {
                body_way = {(at[0] - actor.hand_body_at[0]) / dt, (at[1] - actor.hand_body_at[1]) / dt,
                            (at[2] - actor.hand_body_at[2]) / dt};
                body_speed = std::sqrt(body_way[0] * body_way[0] + body_way[2] * body_way[2]);
                if (body_speed > 12.0F) {
                    body_speed = 0.0F;
                    body_way = {};
                }
            }
            if (gap > 0U || fresh)
                actor.hand_body_at = at;
        }
    }
    // the animation with the most weight, for `hand_anim`
    const std::array<int, 2> *forced = nullptr;
    {
        float most = 0.0F;
        for (const Playing &p : playing)
            if (p.ratio > most) {
                most = p.ratio;
                const auto f = m.hand_anim.find(p.name);
                forced = f != m.hand_anim.end() ? &f->second : nullptr;
            }
    }
    std::vector<sc::Triangle> room;
    for (std::size_t i = 0; i < m.hands.size(); ++i) {
        const Model::Hand &h = m.hands[i];
        Kh2Actor::HandState &state = actor.hand_states[i];
        const auto head = [&](std::size_t j) { return point(k[j], m.joints[j].head); };
        // the wrist's bend and the hand in the world, as the animation left them
        const Mat axes = axes_of(mul(k[h.joint], h.frame));
        const auto forearm_way = vec(axes_of(mul(k[h.forearm], h.forearm_frame)), h.forearm_way);
        const float bend = hand_bend_degrees(vec(inverse(axes), forearm_way), h.back);
        // near the `cuff_pose`'s full bend the hand shows its pose as authored: no fist, no lag
        const float pose_hold =
            m.cuff_settings.pose_bend > 0.0F ? cuff_pose_hold(bend / m.cuff_settings.pose_bend) : 0.0F;
        const float pose_straight =
            m.cuff_settings.pose_bend > 0.0F ? cuff_pose_straight(bend / m.cuff_settings.pose_bend) : 0.0F;
        const auto wrist_view = head(h.joint), elbow_view = head(h.forearm), shoulder_view = head(h.upper);
        const auto wrist = point(world, wrist_view), elbow = point(world, elbow_view);
        const auto back = vec(world, vec(axes, h.back));
        const auto palm_view = point(k[h.joint], h.palm);
        const auto palm = point(world, palm_view);
        std::array<float, 3> moving{};
        if (!fresh && gap > 0U)
            moving = {(wrist[0] - state.at[0]) / dt, (wrist[1] - state.at[1]) / dt, (wrist[2] - state.at[2]) / dt};
        if (hand_length(moving) > 12.0F) // a cut
            moving = {};
        if (gap > 0U || fresh)
            state.at = wrist;
        const auto against_body = hand_sub(moving, body_way);
        const float speed = hand_length(against_body);
        // the room about the palm: the surface it could rest on
        room.clear();
        sc::Aabb box;
        const float r = m.hand_contact_far + 0.2F;
        box.add({palm[0] - r, palm[1] - r, palm[2] - r});
        box.add({palm[0] + r, palm[1] + r, palm[2] + r});
        sc::gather_room(box, room);
        const bool holds = ((held >> i) & 1U) != 0U;
        float contact_share = 0.0F;
        std::array<float, 3> surface_at{};
        bool found = false;
        std::array<float, 3> normal{0.0F, 1.0F, 0.0F};
        if (!holds) {
            float best = m.hand_contact_far + (state.touching ? 0.03F : 0.0F);
            const float least_facing = state.touching ? -0.4F : -0.55F;
            const sc::Vec3 at{palm[0], palm[1], palm[2]}, from{elbow[0], elbow[1], elbow[2]};
            for (const sc::Triangle &t : room) {
                if (sc::dot(from - t.a, t.n) < 0.0F)
                    continue;
                // the palm's way out (against the back of the hand) toward the face
                const float faces = -(back[0] * t.n.x + back[1] * t.n.y + back[2] * t.n.z);
                if (faces > least_facing)
                    continue;
                const float side = sc::dot(at - t.a, t.n);
                // straight under the palm: a nearer edge beside it (a pillow, the bed's frame) is another height
                const sc::Vec3 q = sc::closest_on_triangle(at, t.a, t.b, t.c);
                if (sc::length((at - t.n * side) - q) > 0.04F)
                    continue;
                const float dist = std::fabs(side);
                if (dist < best) {
                    best = dist;
                    found = true;
                    surface_at = {t.a.x, t.a.y, t.a.z};
                    normal = {t.n.x, t.n.y, t.n.z};
                    contact_share = 1.0F - hand_share(dist, m.hand_contact_near, m.hand_contact_far);
                }
            }
            contact_share *= 1.0F - hand_share(speed, m.hand_contact_speed, m.hand_contact_speed * 2.0F);
        }
        // the surface eased as the palm crosses from one face to another: the hand glides over the step
        if (found) {
            const float height = hand_dot(surface_at, normal);
            if (fresh || !state.touching) {
                state.surface = normal;
                state.surface_height = height;
            } else if (gap > 0U) {
                const float kept = std::exp(-dt / 0.12F);
                state.surface =
                    hand_unit(hand_add(hand_add({0.0F, 0.0F, 0.0F}, normal, 1.0F - kept), state.surface, kept));
                state.surface_height = height + (state.surface_height - height) * kept;
            }
        }
        state.touching = found;
        // the shares
        HandShares target{};
        target[kHandOpen] = hand_share(bend, m.hand_bend_from, m.hand_bend_to);
        target[kHandFist] = std::fmax(hand_share(speed, m.hand_fist_from, m.hand_fist_to),
                                      m.hand_walk_share * hand_share(body_speed, m.hand_walk_from, m.hand_walk_to)) *
                            (1.0F - pose_hold);
        target[kHandFlat] = contact_share;
        target[kHandGrip] = holds ? 1.0F : 0.0F;
        if (forced != nullptr && (*forced)[i] >= 0 && !holds) { // the animation says; the grip still wins
            const int p = (*forced)[i];
            target[kHandOpen] = p == kHandOpen ? 1.0F : 0.0F;
            target[kHandFist] = p == kHandFist ? 1.0F : 0.0F;
            target[kHandFlat] = p == kHandFlat ? 1.0F : 0.0F;
            target[kHandGrip] = p == kHandGrip ? 1.0F : 0.0F;
        }
        if (fresh) {
            state.shares = target;
            state.push = {};
            state.digit_bend.fill(bend);
            state.travel = 0.0F;
        } else {
            state.shares[kHandOpen] = hand_fade(state.shares[kHandOpen], target[kHandOpen], dt, 0.12F, 0.25F);
            state.shares[kHandFist] = hand_fade(state.shares[kHandFist], target[kHandFist], dt, 0.12F, 0.35F);
            state.shares[kHandFlat] = hand_fade(state.shares[kHandFlat], target[kHandFlat], dt, 0.12F, 0.25F);
            state.shares[kHandGrip] =
                hand_fade(state.shares[kHandGrip], target[kHandGrip], dt, m.hand_grip_fade, m.hand_grip_fade);
        }
        const float flat = state.shares[kHandFlat];
        // the hand turned so its palm lies on the surface, by the flat pose's share, at most 50 degrees
        if (flat > 0.001F) {
            const auto back_view = vec(axes, h.back);
            const auto wanted = vec(view, state.surface);
            const float cosine = std::clamp(hand_dot(back_view, wanted), -1.0F, 1.0F);
            const float degrees = std::acos(cosine) * 57.2957795F;
            const float share = flat * std::fmin(1.0F, 50.0F / std::fmax(degrees, 1e-3F));
            const auto toward =
                hand_unit(hand_add(hand_add({0.0F, 0.0F, 0.0F}, back_view, 1.0F - share), wanted, share));
            const Mat turn = turned_onto(head(h.joint), back_view, toward);
            for (const std::size_t j : h.hand_joints)
                k[j] = mul(turn, k[j]);
        }
        // the wrist's move: out of the body and the room; onto a surface it rests on, the palm as it now lies
        std::array<float, 3> push{};
        if (flat > 0.001F) {
            const auto turned = point(world, point(k[h.joint], h.palm));
            const float over = hand_dot(turned, state.surface) - state.surface_height;
            push = hand_add(push, state.surface, (kPalmOver - over) * flat);
        }
        const auto sample_at = [&](const std::array<float, 4> &s, const std::array<float, 3> &by) {
            return hand_add(point(world, point(k[h.joint], {s[0], s[1], s[2]})), by);
        };
        for (int round = 0; round < 3; ++round) {
            for (const auto &s : h.samples) {
                const auto at = sample_at(s, push);
                std::array<float, 3> way{};
                const float deep = body_depth(m, point(view, at), s[3], way);
                if (deep > 0.0F)
                    push = hand_add(push, vec(world, way), deep);
            }
            for (const auto &s : h.samples) {
                std::array<float, 3> moved{};
                const float deep = room_depth(room, sample_at(s, push), s[3], elbow, moved);
                if (deep > 0.0F)
                    push = hand_add(push, moved);
            }
        }
        if (const float l = hand_length(push); l > 0.2F)
            push = hand_add({0.0F, 0.0F, 0.0F}, push, 0.2F / l);
        // by a spring: the wrist never jumps and never overshoots, however what pushes it changes
        if (fresh) {
            state.push = push;
            state.push_speed = {};
        } else if (gap > 0U) {
            for (std::size_t c = 0; c < 3; ++c)
                hand_spring(state.push[c], state.push_speed[c], push[c], 22.0F, dt);
        }
        // the arm bent to it: the upper arm and forearm swung, the hand moved whole
        if (hand_length(state.push) > 1e-4F) {
            HandVec moved = hand_add(wrist_view, vec(view, state.push));
            const HandVec new_elbow = hand_arm_elbow(shoulder_view, elbow_view, wrist_view, moved);
            const Mat swing =
                turned_onto(shoulder_view, hand_sub(elbow_view, shoulder_view), hand_sub(new_elbow, shoulder_view));
            const Mat fore = mul(moved_by(hand_sub(new_elbow, elbow_view)),
                                 turned_onto(elbow_view, hand_sub(wrist_view, elbow_view), hand_sub(moved, new_elbow)));
            const Mat whole = moved_by(hand_sub(moved, wrist_view));
            for (const std::size_t j : h.upper_joints)
                k[j] = mul(swing, k[j]);
            for (const std::size_t j : h.forearm_joints)
                k[j] = mul(fore, k[j]);
            for (const std::size_t j : h.hand_joints)
                k[j] = mul(whole, k[j]);
        }
        // the fingers lag the wrist's turn and the hand's travel: curled as it bends back or moves that way
        if (fresh || gap > 0U) {
            const float kept = fresh ? 0.0F : std::exp(-dt / 0.09F);
            state.travel = hand_dot(against_body, back) + (state.travel - hand_dot(against_body, back)) * kept;
            for (std::size_t n = 0; n < state.digit_bend.size(); ++n) {
                const float lag = 0.04F + 0.012F * static_cast<float>(n); // the index first, the little finger last
                state.digit_bend[n] = fresh ? bend : bend + (state.digit_bend[n] - bend) * std::exp(-dt / lag);
            }
        }
        // each finger: its pose, and how far toward the flat one it must open to clear what it would go into
        const auto pose_digit = [&](const Model::HandDigit &digit, float open, float curl) {
            HandShares shares = state.shares;
            for (std::size_t f = digit.first; f <= digit.last; ++f) {
                const Model::HandFinger &finger = h.fingers[f];
                Quat q = hand_pose_turn(finger.poses, shares);
                const float straight = std::fmax(open, pose_straight * (1.0F - shares[kHandGrip]));
                if (straight > 0.0F)
                    q = hand_mix(q, finger.poses[kHandFlat], straight);
                if (!digit.thumb && f > digit.first)
                    q = hand_curled(q, curl);
                k[finger.joint] =
                    mul(k[finger.parent], mul(finger.frame, mul(hand_turn_matrix(q), finger.frame_inverse)));
            }
        };
        const auto digit_depth = [&](const Model::HandDigit &digit) {
            float deepest = 0.0F;
            for (const auto &[j, rest] : digit.spheres) {
                const auto at_view = point(k[j], rest);
                std::array<float, 3> way{}, moved{};
                // the body's own surface: the parts' margins are for the wrist, or a hand hanging by the thigh opens
                deepest = std::fmax(deepest, body_depth(m, at_view, 0.009F, way, 0.0F));
                deepest = std::fmax(deepest, room_depth(room, point(world, at_view), 0.011F, elbow, moved));
            }
            return deepest;
        };
        const std::size_t digits = std::min(h.digits.size(), state.digit_open.size());
        const auto curl_of = [&](std::size_t n) {
            return std::clamp(m.hand_lag * (bend - state.digit_bend[n]) + state.travel * 6.0F, -14.0F, 14.0F) *
                   (1.0F - state.shares[kHandGrip]) * (1.0F - pose_hold);
        };
        std::array<float, 5> need{};
        for (std::size_t n = 0; n < digits; ++n) {
            const Model::HandDigit &digit = h.digits[n];
            const float curl = curl_of(n);
            // the grip stays on what it holds: nothing lifts it
            float open = 0.0F;
            if (state.shares[kHandGrip] < 0.5F) {
                pose_digit(digit, 0.0F, curl);
                if (digit_depth(digit) > 0.0F) {
                    float low = 0.0F, high = 1.0F;
                    for (int step = 0; step < 6; ++step) {
                        const float mid = (low + high) * 0.5F;
                        pose_digit(digit, mid, curl);
                        (digit_depth(digit) > 0.0F ? low : high) = mid;
                    }
                    open = high;
                }
            }
            need[n] = open;
        }
        // the four fingers give way together, the thumb alone; eased both ways: pushed open as they meet something,
        // not put there
        const std::array<float, 5> open = hand_linked_open(need, m.hand_finger_link);
        for (std::size_t n = 0; n < digits; ++n) {
            float &opened = state.digit_open[n];
            if (fresh)
                opened = open[n];
            else if (gap > 0U)
                opened = open[n] + (opened - open[n]) * std::exp(-dt / (open[n] > opened ? 0.06F : 0.2F));
            pose_digit(h.digits[n], opened, curl_of(n));
        }
        if (report) {
            // how deep in anything the hand is left
            float after = 0.0F;
            for (const auto &s : h.samples) {
                const auto at = point(k[h.joint], {s[0], s[1], s[2]});
                std::array<float, 3> way{}, moved{};
                after = std::fmax(after, body_depth(m, at, s[3], way));
                after = std::fmax(after, room_depth(room, point(world, at), s[3], elbow, moved));
            }
            khdays_log(
                "models: %s: hand %zu: bend %.0f degrees, speed %.2f, open %.2f, fist %.2f, flat %.2f, grip %.2f%s, "
                "wrist moved %.1f cm, %.1f cm deep",
                m.name.c_str(), i, static_cast<double>(bend), static_cast<double>(speed),
                static_cast<double>(state.shares[kHandOpen]), static_cast<double>(state.shares[kHandFist]),
                static_cast<double>(state.shares[kHandFlat]), static_cast<double>(state.shares[kHandGrip]),
                holds ? ", holding" : "", static_cast<double>(hand_length(state.push) * 100.0F),
                static_cast<double>(after * 100.0F));
        }
    }
}

// bones.txt's `cuff`: this draw's fit of each cuff round its hand's vertices, eased, with its sway, onto the flaps' bones
void pose_cuffs(const Model &m)
{
    using namespace khdays::models;
    if (m.cuffs.empty())
        return;
    float camera[16];
    Mat world = kIdentity;
    const bool has_camera = khdays_g3d_camera(camera) != 0;
    if (has_camera) {
        Mat view;
        std::copy(camera, camera + 16, view.begin());
        world = inverse(view);
    }
    Kh2Actor &actor = actor_of(m);
    const u32 now = khdays_events_vblank_count();
    const u32 gap = now - actor.cuff_vblank;
    const bool fresh = actor.cuff_motions.size() != m.cuffs.size() || gap > 12U;
    actor.cuff_vblank = now;
    actor.cuff_motions.resize(m.cuffs.size());
    const float dt = static_cast<float>(gap) / 60.0F;
    static std::vector<HandVec> points;
    static std::vector<Mat> posed;
    for (std::size_t c = 0; c < m.cuffs.size(); ++c) {
        const Model::Cuff &cuff = m.cuffs[c];
        CuffMotion &motion = actor.cuff_motions[c];
        const Mat before = mul(skin[cuff.root], inverse(m.joints[cuff.root].bind));
        const Mat before_back = inverse(before);
        points.resize(cuff.points.size());
        for (std::size_t p = 0; p < cuff.points.size(); ++p)
            points[p] = point(before_back, point(skin[cuff.points[p].first], cuff.points[p].second));
        // how far into its pose: from the hand's bend, eased; the fit answers on the surface baked nearest there
        if (!cuff.surfaces.empty()) {
            const HandVec now_way =
                vec(before_back, vec(mul(skin[cuff.hand], inverse(m.joints[cuff.hand].bind)), cuff.hand_way));
            const float wanted =
                cuff_pose_share(cuff.hand_way, cuff.toward, hand_unit(now_way), m.cuff_settings.pose_bend);
            if (fresh) {
                motion.pose = wanted;
                motion.pose_speed = 0.0F;
            } else if (dt > 0.0F) {
                hand_spring(motion.pose, motion.pose_speed, wanted, 30.0F, dt);
            }
            motion.pose = std::clamp(motion.pose, 0.0F, 1.0F);
            cuff_pose_at(cuff.pose, motion.pose, posed);
        }
        // the cuff stands off round the hand as in the pose: what it fits round is the hand from there
        const HandVec pose_slide = cuff.surfaces.empty()
                                       ? HandVec{}
                                       : cuff_pose_slide(cuff.hand_way, cuff.toward, m.cuff_settings.pose_pivot,
                                                         motion.pose, m.cuff_settings.pose_bend);
        for (HandVec &p : points)
            p = hand_sub(p, pose_slide);
        const float along = cuff.surfaces.empty() ? 0.0F : motion.pose * static_cast<float>(cuff.surfaces.size() - 1);
        const std::size_t near_a = static_cast<std::size_t>(along),
                          near_b = cuff.surfaces.empty() ? 0U : std::min(near_a + 1U, cuff.surfaces.size() - 1U);
        const float between = along - static_cast<float>(near_a);
        const CuffFit &fit_a = cuff.surfaces.empty() ? cuff.fit : cuff.surfaces[near_a],
                      &fit_b = cuff.surfaces.empty() ? cuff.fit : cuff.surfaces[near_b];
        const float hold = cuff.surfaces.empty() ? 0.0F : cuff_pose_hold(motion.pose);
        const CuffFit::Target target =
            cuff_target_mix(cuff.surfaces.empty() ? cuff.fit.fit(points, m.cuff_settings)
                                                  : cuff_target_mix(fit_a.fit(points, m.cuff_settings),
                                                                    fit_b.fit(points, m.cuff_settings), between),
                            CuffFit::Target{}, hold);
        // the wrist's acceleration throws each flap; its weight hangs it
        const auto at = point(world, point(before, cuff.fit.center));
        HandVec accel{};
        if (!fresh && gap > 0U) {
            const HandVec moving = {(at[0] - motion.at[0]) / dt, (at[1] - motion.at[1]) / dt,
                                    (at[2] - motion.at[2]) / dt};
            if (hand_length(moving) < 12.0F) { // else a cut
                const float kept = std::exp(-dt / 0.05F);
                const HandVec eased = {moving[0] + (motion.moving[0] - moving[0]) * kept,
                                       moving[1] + (motion.moving[1] - moving[1]) * kept,
                                       moving[2] + (motion.moving[2] - moving[2]) * kept};
                accel = {(eased[0] - motion.moving[0]) / dt, (eased[1] - motion.moving[1]) / dt,
                         (eased[2] - motion.moving[2]) / dt};
                motion.moving = eased;
            } else {
                motion.moving = {};
            }
        } else if (fresh) {
            motion.moving = {};
        }
        if (fresh || gap > 0U)
            motion.at = at;
        CuffFit::Shares push{}, droop{};
        for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
            const HandVec out = hand_unit(vec(world, vec(before, cuff.fit.out[f])));
            push[f] = std::clamp(hand_dot(accel, out), -60.0F, 60.0F);
            droop[f] = has_camera ? -out[1] * 0.09F : 0.0F;
        }
        cuff_motion_step(motion, target, push, droop, dt, fresh);
        // the whole cuff slid, then its flaps from there
        const Mat slide = mul(before, mul(moved_by(hand_add(motion.shift, pose_slide)), before_back));
        for (const std::size_t j : cuff.slides)
            skin[j] = mul(slide, skin[j]);
        const Mat frame = mul(slide, before);
        const Mat back = inverse(frame);
        // the ring's own change goes on last, in its frame, which a `swing` has left apart from the root's: the flaps
        // take the rest of theirs in the root's
        Mat ring_now = kIdentity, ring_undone = kIdentity;
        if (cuff.pose_ring >= 0) {
            const std::size_t ring = cuff.pose[static_cast<std::size_t>(cuff.pose_ring)].joint;
            ring_now = mul(skin[ring], inverse(m.joints[ring].bind));
            ring_undone = inverse(posed[static_cast<std::size_t>(cuff.pose_ring)]);
        }
        for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
            const float open = motion.open[f] + motion.sway[f] * (1.0F - hold),
                        bend = std::clamp((motion.sway[f] - motion.sway_late[f]) * 1.2F, -0.2F, 0.2F) * (1.0F - hold);
            // the flap as it now stands in its pose, between the two surfaces either side
            const auto mixed = [&](const HandVec &a, const HandVec &b) { return hand_add(a, hand_sub(b, a), between); };
            const HandVec along_now = hand_unit(mixed(fit_a.along[f], fit_b.along[f]));
            const HandVec out_mixed = mixed(fit_a.out[f], fit_b.out[f]);
            const HandVec out_now = hand_unit(hand_add(out_mixed, along_now, -hand_dot(out_mixed, along_now)));
            const HandVec tip_now = mixed(fit_a.tip[f], fit_b.tip[f]);
            const HandVec way = hand_add(hand_add({0.0F, 0.0F, 0.0F}, along_now, std::cos(open) * motion.length[f]),
                                         out_now, std::sin(open) * motion.length[f]);
            const Mat flap = turned_onto(mixed(fit_a.hinge[f], fit_b.hinge[f]), along_now, way);
            const Mat tip =
                turned_onto(tip_now, along_now,
                            hand_add(hand_add({0.0F, 0.0F, 0.0F}, along_now, std::cos(bend)), out_now, std::sin(bend)));
            for (std::size_t n = 0; n < cuff.flap_joints[f].size(); ++n) {
                const std::size_t j = cuff.flap_joints[f][n];
                const bool far =
                    std::find(cuff.tip_joints[f].begin(), cuff.tip_joints[f].end(), j) != cuff.tip_joints[f].end();
                const Mat rest_to_fit = far ? mul(flap, tip) : flap;
                const int in_pose = cuff.surfaces.empty() ? -1 : cuff.flap_pose[f][n];
                const Mat all = in_pose >= 0
                                    ? mul(ring_undone, mul(rest_to_fit, posed[static_cast<std::size_t>(in_pose)]))
                                    : rest_to_fit;
                skin[j] = mul(frame, mul(all, mul(back, skin[j])));
            }
        }
        if (cuff.pose_ring >= 0) {
            const Mat ring_move =
                mul(ring_now, mul(posed[static_cast<std::size_t>(cuff.pose_ring)], inverse(ring_now)));
            for (const std::size_t j : cuff.slides)
                skin[j] = mul(ring_move, skin[j]);
        }
    }
}

// The picture a mesh shows after `frames` game frames: its own, or the one its changing texture is on.
Mesh::Picture picture_now(const Mesh &mesh, std::uint64_t frames)
{
    Mesh::Picture now{mesh.replacement, mesh.tex_w, mesh.tex_h, mesh.translucent};
    if (!mesh.frames.empty()) {
        const Mesh::Picture &p =
            mesh.frame_pictures[khdays::room::frame_at(mesh.frames, mesh.frames_loop, mesh.frames_loop_from, frames)];
        if (p.replacement >= 0)
            now = {p.replacement, p.w, p.h, mesh.translucent || p.translucent};
    }
    return now;
}

// Triangle `t`'s polygon attributes, from the mesh's `attr` and its corners' vertex alpha; false when
// the triangle is not drawn (under a mask's cutoff, or blended down to nothing).
bool triangle_attr(const Mesh &mesh, std::size_t t, uint32_t attr, uint32_t &out)
{
    out = attr;
    if (mesh.vertex_alpha == VertexAlpha::ignored)
        return true;
    const float alpha = (static_cast<float>(mesh.vertices[mesh.indices[t]].color[3]) +
                         static_cast<float>(mesh.vertices[mesh.indices[t + 1]].color[3]) +
                         static_cast<float>(mesh.vertices[mesh.indices[t + 2]].color[3])) /
                        (3.0F * 255.0F);
    if (mesh.vertex_alpha == VertexAlpha::cut)
        return alpha >= mesh.cutoff;
    // blended: the vertex alphas are blended across the polygon; a fully clear one is skipped
    return alpha > 0.0F;
}

// Says once that a motion cannot be played: its data is read at draw time, so a cut-short file throws only here
void say_bad_motion(const Model &m, const char *set, int slot, const char *what)
{
    static std::set<std::string> told;
    if (!told.insert(m.path + "/" + set + "/" + std::to_string(slot)).second)
        return;
    std::fprintf(stderr,
                 "models: %s: %s slot %d cannot be played (%s); replace the file or take the slot off motions.txt; the "
                 "game's pose is drawn for it\n",
                 m.path.c_str(), set, slot, what);
    khdays_log("models: %s: %s slot %d cannot be played (%s); replace the file or take the slot off motions.txt; the "
               "game's pose is drawn for it",
               m.path.c_str(), set, slot, what);
}

// A slot's pose at `ticks` on the model's bones (`track.joints`) and the root motion taken out of it; false when the
// motion cannot be played.
bool play_track(const Model &m, Kh2Track &track, std::size_t slot, const khdays::assets::kh2::Motion &motion,
                float ticks, const Floor &ground, Mat &taken)
{
    try {
        ready_player(m, track, slot);
        // IKAdjust's world: the model (KH2 centimetres, root motion taken out) to Days' world, y up
        taken = m.kh2_root_motion ? kIdentity : inverse(motion.root_motion(ticks));
        const Mat world = ground.on ? mul(ground.world, taken) : kIdentity;
        track.player->evaluate(motion, ticks, world, ground.on ? &ground.ray : nullptr);
        pose_on_model(m, slot, track.player->joints(), track.joints); // the mset's model's pose on this one
        return true;
    } catch (const std::exception &e) {
        say_bad_motion(m, "its mset", static_cast<int>(slot), e.what());
        return false;
    }
}

// A KH2 model whose game animation has a KH2 motion: its joints posed by the motion (on `base`, the game's root node,
// scaled), each motion of the game's cross-fade at the game's weight and the game's own pose (skin) at the rest of it. A
// section's clock starts when the game's frame enters it and moves as that frame moves (a followed section's is the
// frame itself); past the motion's end it loops from its loop point (retFrame) or holds.
void play_kh2(Model &m, const Mat &base)
{
    namespace kh2 = khdays::assets::kh2;
    namespace kh2_clock = khdays::kh2_clock;
    Kh2Actor &actor = actor_of(m);
    ++actor.draws;
    if (m.kh2_rest.size() != m.joints.size()) {
        m.kh2_rest.resize(m.joints.size());
        for (std::size_t j = 0; j < m.joints.size(); ++j)
            m.kh2_rest[j] = inverse(m.joints[j].inverse_bind);
    }
    struct Pose {
        float weight;
        const std::vector<kh2::Mat4> *joints; // the model's bones (retargeted when its mset is another's)
        const kh2::Motion *motion;
        float ticks;
        std::array<float, 3> offset; // KH2 centimetres
        FaceMotion face;
        Mat turn = kIdentity;  // `rotate`
        int slot = -1;         // its mset slot (the weapon's motion plays the same)
        Mat taken = kIdentity; // its root motion taken out
    };
    std::vector<Pose> poses;
    float total = 0.0F, game = 0.0F;
    const Floor ground = floor_of(m, base);
    // the game's frames this draw: its animations' largest forward step (a `finish` tail's clock), and whether one of
    // its `idle` animations plays
    const auto is_idle = [&](const std::string &name) {
        return std::find(m.kh2_idle.begin(), m.kh2_idle.end(), name) != m.kh2_idle.end();
    };
    float game_step = 0.0F;
    bool any_idle = false;
    for (const Playing &p : playing) {
        const auto was = actor.frames.find(p.name);
        if (was != actor.frames.end() && p.frame > was->second)
            game_step = std::fmax(game_step, p.frame - was->second);
        any_idle = any_idle || (p.ratio > 0.0F && is_idle(p.name));
    }
    actor.frames.clear();
    for (const Playing &p : playing)
        actor.frames[p.name] = p.frame;
    // the game's state for this actor (khdays_models_actor_state): damage in the air, or a landing straight out of it
    const auto known = actor_states.find(drawn_object != nullptr ? KHDAYS_REF(drawn_object) - 0x24U : 0U);
    const ActorState *game_state = known != actor_states.end() ? &known->second : nullptr;
    if (game_state != nullptr && !m.said_states && !m.kh2_air.empty()) {
        m.said_states = true;
        khdays_log("models: %s: the game's actor states reach its draws (state %d): its `air` lines play",
                   m.name.c_str(), game_state->state);
    }
    const bool in_air = game_state != nullptr && game_state->state == kAirDamageState;
    const bool landed_out = game_state != nullptr && khdays::kh2_moves::landed_out_of_air(
                                                         game_state->state == kLandingState, game_state->entered,
                                                         game_state->left_air, kLandedOutOf);
    // `air`: an animation playing its air slot keeps it while the game fades it out after (into the landing, or the
    // fall it recovers into); played anew (its weight rising) or gone, it is the ground's again
    const auto was = [&](const std::string &name) {
        const auto r = actor.ratios.find(name);
        return r != actor.ratios.end() ? r->second : 0.0F;
    };
    {
        std::set<std::string> airborne;
        for (const Playing &p : playing)
            if (p.ratio > 0.0F && m.kh2_air.contains(p.name) &&
                (in_air || (actor.in_air.contains(p.name) && p.ratio <= was(p.name) + 1e-4F)))
                airborne.insert(p.name);
        actor.in_air = std::move(airborne);
    }
    // `cut`: an animation the game fades into (its weight rising) takes the whole pose from its first draw; the
    // game's fade into it depends on the animation before (Ov002_GetModeBlendFrames: a cut from some, a fade from
    // others)
    Playing *cutting = nullptr;
    for (Playing &p : playing) {
        const auto found = m.kh2_sections.find(p.name);
        if (!(p.ratio > 0.0F) || p.ratio >= 0.999F || p.ratio < was(p.name) - 1e-4F || found == m.kh2_sections.end())
            continue;
        const Kh2Section *now = nullptr; // its section at the game's frame
        for (const Kh2Section &section : found->second)
            if (section.from <= p.frame)
                now = &section;
        if (now != nullptr && now->cut)
            cutting = &p;
    }
    // the game cutting into an animation other than its idles (its whole weight at its first draw: a hit, the death)
    // or `cut` taking the pose: a `finish` tail or an idle fade goes at once, as the game's pose does
    bool game_cut = cutting != nullptr;
    for (const Playing &p : playing)
        game_cut = game_cut || (p.ratio >= 0.999F && !(was(p.name) > 0.0F) && !is_idle(p.name));
    if (game_cut) {
        actor.tail.stop();
        actor.held_fade.stop();
    }
    // `walk` lines: the game's weight on its animations with one, at the last draw and now (the walk's mix below)
    float walk_was = 0.0F, walk_now = 0.0F;
    for (const Playing &p : playing)
        if (m.kh2_sections.contains(p.name + "#walk")) {
            walk_was = std::fmax(walk_was, was(p.name));
            walk_now = std::fmax(walk_now, p.ratio);
        }
    actor.ratios.clear();
    for (const Playing &p : playing)
        actor.ratios[p.name] = p.ratio;
    // KHDAYS_SECONDARY_TRACE: what this draw played, a line per draw (the game's weights, then what they became)
    const bool trace_kh2 = secondary_tracing();
    std::string traced;
    char part[192];
    if (trace_kh2)
        for (const Playing &p : playing) {
            std::snprintf(part, sizeof(part), " %s f%.1f w%.2f", p.name.c_str(), static_cast<double>(p.frame),
                          static_cast<double>(p.ratio));
            traced += part;
        }
    if (cutting != nullptr)
        for (Playing &p : playing)
            p.ratio = &p == cutting ? 1.0F : 0.0F;
    bool landing_plays = false;
    for (const Playing &p : playing)
        landing_plays = landing_plays || (p.ratio > 0.0F && p.name == m.kh2_landing);
    if (!landing_plays)
        actor.landing_from.clear();
    else if (landed_out && actor.landing_from.empty())
        actor.landing_from = actor.air_from; // kept while the landing plays, whatever state follows
    // an `air` slot's clock: the vblanks since the last draw (60 a second, as KH2's ticks), none while paused
    const u32 vblank = khdays_events_vblank_count();
    const float realtime = actor.vblank != 0 && !khdays_link_paused()
                               ? static_cast<float>(std::min<u32>(vblank - actor.vblank, 8U))
                               : 0.0F;
    actor.vblank = vblank;
    Kh2Finish finishing; // the `finish` section the game plays now, before going on to an idle
    float finishing_ratio = 0.0F;
    std::string main; // the animation the game plays most, and its fade into an idle (`blend`)
    float main_ratio = -1.0F, main_blend = 0.0F;
    // `camp` lines: while a camp menu draws the model (overlay 25, or its twin 8: runtime/menus/camp.c's camps)
    const bool in_camp = khdays_overlay_loaded(25) != 0 || khdays_overlay_loaded(8) != 0;
    // `walk` lines: the port's walk (the stick pushed short of running, controls_hooks.c) takes each animation with
    // one into its walk motion, eased in and out over the line's `blend` frames (a cut without)
    float walk_blend = 0.0F;
    bool any_walk = false;
    for (const Playing &p : playing)
        if (const auto w = m.kh2_sections.find(p.name + "#walk"); w != m.kh2_sections.end() && !w->second.empty()) {
            any_walk = true;
            walk_blend = std::fmax(walk_blend, w->second.front().blend);
        }
    const bool walks =
        any_walk && drawn_object != nullptr && khdays_controls_walk_share(KHDAYS_REF(drawn_object) - 0x24U) > 0;
    // eased only while the game plays the animation whole: fading it in (from the idle, a landing) the game's fade
    // is the change, and fading it out (no steer then) the mix is kept
    const float walk_step = walk_blend > 0.0F ? realtime * 0.5F / walk_blend : 1.0F; // game frames, 2 vblanks each
    const bool walk_fading = walk_now < walk_was - 1e-4F;
    if (!any_walk || !(walk_now > 0.0F))
        actor.walk = 0.0F;
    else if (!walk_fading && (walk_now < 0.999F || !(walk_was > 0.0F)))
        actor.walk = walks ? 1.0F : 0.0F;
    else if (!walk_fading)
        actor.walk = walks ? std::fmin(1.0F, actor.walk + walk_step) : std::fmax(0.0F, actor.walk - walk_step);
    struct Play {
        const Playing *p;
        float ratio;
        bool walk; // its `walk` line's sections
    };
    std::vector<Play> plays;
    for (const Playing &p : playing) {
        if (actor.walk > 0.0F && m.kh2_sections.contains(p.name + "#walk")) {
            plays.push_back({&p, p.ratio * (1.0F - actor.walk), false});
            plays.push_back({&p, p.ratio * actor.walk, true});
        } else {
            plays.push_back({&p, p.ratio, false});
        }
    }
    for (const Play &play : plays) {
        const Playing &p = *play.p;
        const float ratio = std::fmax(0.0F, std::fmin(1.0F, play.ratio));
        total += ratio;
        // its sections: `air`'s in the game's damage in the air, `land`'s landing straight out of it, else its own
        const std::vector<Kh2Section> *list = nullptr;
        std::string key = p.name;
        const auto air = m.kh2_air.find(p.name);
        const auto landing = m.kh2_air.find(actor.landing_from);
        const auto found = m.kh2_sections.find(p.name);
        const auto camp = in_camp ? m.kh2_sections.find(p.name + "#camp") : m.kh2_sections.end();
        if (play.walk) {
            list = &m.kh2_sections.find(p.name + "#walk")->second;
            key += "#walk";
        } else if (actor.in_air.contains(p.name) && air != m.kh2_air.end()) {
            list = &air->second.air;
            key += "#air";
            actor.air_from = p.name;
        } else if (p.name == m.kh2_landing && landing != m.kh2_air.end() && !landing->second.land.empty()) {
            list = &landing->second.land;
            key += "#land";
        } else if (camp != m.kh2_sections.end()) {
            list = &camp->second;
            key += "#camp";
        } else if (found != m.kh2_sections.end()) {
            list = &found->second;
        }
        // its section at the game's frame: the last one starting at or before it
        int now = -1;
        if (list != nullptr)
            for (std::size_t s = 0; s < list->size() && (*list)[s].from <= p.frame; ++s)
                now = static_cast<int>(s);
        if (ratio > main_ratio) {
            const auto own = m.kh2_idle_blend.find(p.name);
            main = p.name;
            main_ratio = ratio;
            // (a walk line's `blend` is its crossing with the run alone: the run fades into the idle with the game)
            main_blend = own != m.kh2_idle_blend.end() ? own->second
                         : now >= 0 && !play.walk      ? (*list)[static_cast<std::size_t>(now)].blend
                                                       : 0.0F;
        }
        if (now < 0) {
            game += ratio;
            if (trace_kh2) {
                std::snprintf(part, sizeof(part), " | %s game's w%.2f", p.name.c_str(), static_cast<double>(ratio));
                traced += part;
            }
            continue;
        }
        const std::vector<Kh2Section> &sections = *list;
        const Kh2Section &section = sections[static_cast<std::size_t>(now)];
        const kh2::Motion *motion = m.kh2_motions->motion(section.slot);
        Kh2Animation &a = actor.animations[key];
        const bool starts = a.seen + 1 != actor.draws;
        // the game set its frame back: counted on when the KH2 motion loops (it loops itself), else the section
        // starts over (a damage animation played again); a followed section's frame is its time either way
        const bool own_loop = motion->loop_ticks() >= 0.0F && motion->loop_ticks() < motion->length_ticks();
        const kh2_clock::FrameMove move =
            starts ? kh2_clock::FrameMove::on : kh2_clock::frame_move(a.last_frame, p.frame, own_loop);
        const bool back = move != kh2_clock::FrameMove::on;
        const bool enters = starts || now != a.section || (move == kh2_clock::FrameMove::restart && !section.follow);
        float step = 0.0F; // BCA frames since the last draw
        if (!starts) {
            step = p.frame - a.last_frame;
            if (step > 0.0F)
                a.step = step;
            else if (back)
                step = a.step; // one step on (the fall's loop goes back to 16)
        }
        const float before = a.last_frame;
        a.last_frame = p.frame;
        a.seen = actor.draws;
        // a cut (a new section, or a followed frame set back: the loop's blend): fade from the pose last shown
        const bool loops = !enters && back && section.follow;
        const float blend = loops && section.loop_blend >= 0.0F ? section.loop_blend : section.blend;
        const auto shown = a.tracks.find(a.section);
        if (starts) {
            a.fade.clear();
        } else if ((enters || loops) && blend > 0.0F && shown != a.tracks.end() && !shown->second.joints.empty() &&
                   static_cast<std::size_t>(a.section) < sections.size()) {
            a.fade = shown->second.joints;
            a.fade_slot = static_cast<int>(sections[static_cast<std::size_t>(a.section)].slot);
            a.fade_motion = m.kh2_motions->motion(static_cast<std::size_t>(a.fade_slot));
            a.fade_ticks = a.shown;
            a.fade_offset = a.shown_offset;
            a.fade_turn = a.shown_turn;
            a.fade_left = a.fade_frames = blend;
            a.looped_at = loops ? before : -1.0F;
        } else if (!a.fade.empty() && a.looped_at >= 0.0F && (enters || p.frame > a.looped_at)) {
            // the game let go of the loop (past where it set back): the loop's fade ends within `blend`
            const float weight = a.fade_left / a.fade_frames;
            a.fade_left = section.blend;
            a.fade_frames = section.blend / weight;
            a.looped_at = -1.0F;
            if (!(section.blend > 0.0F))
                a.fade.clear();
        }
        Kh2Track &track = a.tracks[now];
        // `start_loop`: from the motion's loop point (its start and loop are one motion; the game's are two). When
        // another game animation was playing this slot up to now (the start, cut or fading into this one), the
        // motion carries on from its time instead: the start is shorter than the motion's run up to its loop
        float start = section.start_loop ? std::fmax(0.0F, motion->loop_ticks()) : 0.0F;
        if (section.start_loop && enters && !section.follow)
            for (const auto &[name, other] : actor.animations) {
                const auto os = m.kh2_sections.find(name);
                if (name == key || other.seen + 1 < actor.draws || os == m.kh2_sections.end() ||
                    static_cast<std::size_t>(other.section) >= os->second.size() ||
                    os->second[static_cast<std::size_t>(other.section)].slot != section.slot)
                    continue;
                // seen last draw: one of its steps on; seen this draw (fading together): its time now
                start = other.shown + (other.seen == actor.draws ? 0.0F : other.step * 2.0F);
                break;
            }
        if (section.follow) { // the game's frame, the section stretched onto the motion
            const float to = static_cast<std::size_t>(now) + 1 < sections.size()
                                 ? sections[static_cast<std::size_t>(now) + 1].from
                                 : p.frames;
            track.ticks = start + (to > section.from ? (p.frame - section.from) / (to - section.from) *
                                                           (motion->length_ticks() - start)
                                                     : 0.0F);
        } else if (enters) { // its clock from the section's first frame (an `air` slot's from its start)
            track.ticks = start + (section.realtime ? 0.0F : (p.frame - section.from) * 2.0F);
        } else if (section.realtime) { // the game's frames: Days holds the damage on a frame while in the air
            track.ticks += realtime;
        } else {
            track.ticks += step * 2.0F; // BCA frames at 30 a second
        }
        a.section = now;
        kh2_played[p.name] = {section.slot, section.from};
        // a cut: the whole pose a new motion's (or one started over), nothing fading from the last
        if (enters && ratio >= 0.999F && a.fade.empty())
            kh2_pose_cut = true;
        float faded = 0.0F; // the weight of the pose fading out
        if (!a.fade.empty()) {
            a.fade_left -= section.realtime ? realtime * 0.5F : step; // BCA frames
            if (a.fade_left > 0.0F && a.fade_motion != nullptr)
                faded = a.fade_left / a.fade_frames;
            else
                a.fade.clear();
        }
        float ticks = track.ticks;
        const float end = motion->length_ticks(), from = motion->loop_ticks();
        if (ticks > end)
            ticks = from >= 0.0F && from < end ? from + std::fmod(ticks - from, end - from) : end;
        Mat taken = kIdentity;
        if (!play_track(m, track, section.slot, *motion, ticks, ground, taken)) { // the game's pose in its place
            game += ratio;
            kh2_played.erase(p.name);
            continue;
        }
        a.shown = ticks;
        if (trace_kh2) {
            std::snprintf(part, sizeof(part), " | %s slot %zu t%.1f w%.2f%s%s", key.c_str(), section.slot,
                          static_cast<double>(ticks), static_cast<double>(ratio), starts ? " starts" : "",
                          enters && !starts ? " enters" : "");
            traced += part;
        }
        const FaceMotion face{m.kh2_motions->attributes(section.slot), ticks, enters ? -1.0F : track.face_ticks};
        track.face_ticks = ticks;
        // `offset`, gliding to its `to` over the motion's length
        const float along = motion->length_ticks() > 0.0F ? std::fmin(1.0F, ticks / motion->length_ticks()) : 0.0F;
        for (std::size_t i = 0; i < 3; ++i)
            a.shown_offset[i] = section.offset[i] + (section.offset_to[i] - section.offset[i]) * along;
        a.shown_turn = section.turn;
        // KHDAYS_SECONDARY_TRACE: on a section's first draw, where the joints the game drives are in its pose and in
        // the motion's (cm, the model's root space), to tell which way each lies (`rotate`)
        if (trace_kh2 && enters) {
            std::string where;
            const Mat to_root = inverse(base);
            for (std::size_t j = 0; j < m.joints.size() && j < m.kh2_bone.size(); ++j) {
                const int bone = m.kh2_bone[j];
                if (m.joints[j].node < 0 || bone < 0 || static_cast<std::size_t>(bone) >= track.joints.size())
                    continue;
                const Mat g = mul(to_root, mul(skin[j], m.kh2_rest[j]));
                const Mat kh = mul(section.turn, mul(taken, track.joints[static_cast<std::size_t>(bone)]));
                std::snprintf(
                    part, sizeof(part), " %s game (%.0f %.0f %.0f) kh2 (%.0f %.0f %.0f);", m.joints[j].name.c_str(),
                    static_cast<double>(g[12] * 100.0F), static_cast<double>(g[13] * 100.0F),
                    static_cast<double>(g[14] * 100.0F), static_cast<double>(kh[12] + section.offset[0]),
                    static_cast<double>(kh[13] + section.offset[1]), static_cast<double>(kh[14] + section.offset[2]));
                where += part;
            }
            khdays_log("kh2 trace: %s %s starts slot %zu:%s", m.name.c_str(), key.c_str(), section.slot, where.c_str());
        }
        if (faded > 0.0F)
            poses.push_back(
                {ratio * faded, &a.fade, a.fade_motion, a.fade_ticks, a.fade_offset, {}, a.fade_turn, a.fade_slot});
        poses.push_back({ratio * (1.0F - faded), &track.joints, motion, ticks, a.shown_offset, face, a.shown_turn,
                         static_cast<int>(section.slot)});
        if (section.finish && !any_idle && !is_idle(p.name) && ratio > finishing_ratio) {
            finishing = {p.name, section.slot, section.then, section.blend, ticks};
            finishing_ratio = ratio;
        }
    }
    // `finish`: the game went on to its idles before the motion ended (and plays nothing else): the motion's rest
    // and the `finish` slots play out on the game's frames, then fade into the idle's; anything else drops them
    bool idle_only = true, from_seen = false;
    for (const Playing &p : playing) {
        const bool fading_out =
            actor.tail.active() && p.name == actor.tail_from && p.ratio <= actor.tail_from_ratio + 1e-4F;
        if (actor.tail.active() && p.name == actor.tail_from && p.ratio > 0.0F) {
            from_seen = fading_out;
            actor.tail_from_ratio = p.ratio;
        }
        idle_only = idle_only && (!(p.ratio > 0.0F) || is_idle(p.name) || p.name == actor.finish.name || fading_out);
    }
    if (!from_seen)
        actor.tail_from.clear();
    if (actor.tail.active()) {
        actor.tail.step(game_step, idle_only);
    } else if (any_idle && idle_only && !actor.finish.name.empty()) {
        std::vector<khdays::kh2_tail::Piece> pieces;
        const auto add = [&](std::size_t slot, float from) {
            if (const kh2::Motion *motion = m.kh2_motions->motion(slot))
                pieces.push_back({static_cast<int>(slot), from, motion->length_ticks()});
        };
        add(actor.finish.slot, actor.finish.ticks);
        for (const std::size_t slot : actor.finish.then)
            add(slot, 0.0F);
        actor.tail.start(std::move(pieces), actor.finish.blend);
        actor.tail_from = actor.finish.name;
        actor.tail_from_ratio = 1.0F;
        for (const Playing &p : playing)
            if (p.name == actor.tail_from)
                actor.tail_from_ratio = p.ratio;
        actor.tail_piece = 0;
        actor.tail_track = {};
        actor.tail_faded = {};
        actor.finish = {};
    }
    if (!finishing.name.empty())
        actor.finish = std::move(finishing);
    else if (!idle_only)
        actor.finish = {};
    if (actor.tail.active()) { // its pieces at their share, the rest of the pose at what is left
        if (actor.tail.piece() != actor.tail_piece) {
            actor.tail_faded = std::move(actor.tail_track);
            actor.tail_track = {};
            actor.tail_piece = actor.tail.piece();
        }
        const auto evaluate = [&](Kh2Track &t, int slot, float ticks) -> const kh2::Motion * {
            const kh2::Motion *motion = slot >= 0 ? m.kh2_motions->motion(static_cast<std::size_t>(slot)) : nullptr;
            if (motion == nullptr)
                return nullptr;
            ticks = std::fmin(ticks, motion->length_ticks());
            Mat taken = kIdentity;
            if (!play_track(m, t, static_cast<std::size_t>(slot), *motion, ticks, ground, taken))
                return nullptr;
            t.ticks = ticks;
            return motion;
        };
        const float share = actor.tail.weight(), under = actor.tail.faded_weight();
        const float whole = total > 0.0F ? total : 1.0F;
        for (Pose &pose : poses)
            pose.weight *= 1.0F - share;
        game *= 1.0F - share;
        if (const kh2::Motion *motion = evaluate(actor.tail_track, actor.tail.slot(), actor.tail.ticks())) {
            const int slot = actor.tail.slot();
            const FaceMotion face{m.kh2_motions->attributes(static_cast<std::size_t>(slot)), actor.tail_track.ticks,
                                  actor.tail_track.face_ticks};
            actor.tail_track.face_ticks = actor.tail_track.ticks;
            poses.push_back({share * (1.0F - under) * whole,
                             &actor.tail_track.joints,
                             motion,
                             actor.tail_track.ticks,
                             {},
                             face,
                             kIdentity,
                             slot});
        }
        if (under > 0.0F)
            if (const kh2::Motion *motion =
                    evaluate(actor.tail_faded, actor.tail.faded_slot(), actor.tail.faded_ticks()))
                poses.push_back({share * under * whole,
                                 &actor.tail_faded.joints,
                                 motion,
                                 actor.tail_faded.ticks,
                                 {},
                                 {},
                                 kIdentity,
                                 actor.tail.faded_slot()});
        total = game;
        for (const Pose &pose : poses)
            total += pose.weight;
    }
    // `blend` into an idle: when the game goes on to one of its idles from an animation with an idle fade (its own
    // `<animation> blend` line or its section's `blend`), the pose last shown fades into the game's over those frames
    // (the game's own fade not counted; a `finish` tail fades itself); anything else the game plays ends it
    const auto into_idle = [&]() {
        if (actor.held_fade.weight() > 0.0F) {
            bool other = actor.tail.active();
            for (const Playing &p : playing) {
                if (!(p.ratio > 0.0F) || is_idle(p.name))
                    continue;
                if (p.name == actor.held_from && p.ratio <= actor.held_from_ratio + 1e-4F) // fading out, not anew
                    actor.held_from_ratio = p.ratio;
                else
                    other = true;
            }
            actor.held_fade.step(game_step, other);
        } else if (actor.held_fade.starts(any_idle, actor.had_idle, actor.main.empty() || is_idle(actor.main),
                                          actor.main_blend, actor.tail.active()) &&
                   actor.shown.size() == skin.size()) {
            actor.held = actor.shown;
            actor.held_fade.start(actor.main_blend);
            actor.held_from = actor.main;
            actor.held_from_ratio = 1.0F;
            for (const Playing &p : playing)
                if (p.name == actor.held_from)
                    actor.held_from_ratio = p.ratio;
        }
        // the pose held relative to the model's root (where the game puts it now), mixed as frames
        const float w = actor.held_fade.weight();
        if (w > 0.0F && actor.held.size() == skin.size())
            for (std::size_t j = 0; j < skin.size(); ++j) {
                const Mat both[2] = {mul(base, actor.held[j]), mul(skin[j], m.kh2_rest[j])};
                const float share[2] = {w, 1.0F - w};
                skin[j] = mul(khdays::models::blend_frames(both, share, 2), m.joints[j].inverse_bind);
            }
        const Mat from_root = inverse(base);
        actor.shown.resize(skin.size());
        for (std::size_t j = 0; j < skin.size(); ++j)
            actor.shown[j] = mul(from_root, mul(skin[j], m.kh2_rest[j]));
        actor.main = main;
        actor.main_blend = main_blend;
        actor.had_idle = any_idle;
        if (trace_kh2)
            khdays_log("kh2 trace: %s v%u state %d;%s%s; idle fade %.2f, tail %.2f%s", m.name.c_str(), vblank,
                       game_state != nullptr ? game_state->state : -1, traced.c_str(),
                       cutting != nullptr ? " (cut)" : "", static_cast<double>(actor.held_fade.weight()),
                       static_cast<double>(actor.tail.active() ? actor.tail.weight() : 0.0F),
                       kh2_pose_cut ? " (a pose cut)" : "");
    };
    // each pose's root motion taken out unless it is kept; one that cannot say its own counts as the game's pose
    for (std::size_t i = 0; i < poses.size();) {
        try {
            poses[i].taken = m.kh2_root_motion ? kIdentity : inverse(poses[i].motion->root_motion(poses[i].ticks));
            ++i;
        } catch (const std::exception &e) {
            say_bad_motion(m, "its mset", poses[i].slot, e.what());
            game += poses[i].weight;
            poses.erase(poses.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    if (poses.empty()) {
        into_idle();
        return;
    }
    const Pose *most = &poses.front();
    for (const Pose &pose : poses)
        if (pose.weight > most->weight)
            most = &pose;
    if (most->weight >= game)
        face_motion = most->face;
    if (!(total > 0.0F)) {
        total = poses.back().weight = 1.0F;
        game = 0.0F;
    }
    kh2_game_share = game / total;
    // `weapon_motions`: each pose's slot played on the weapon at its time, mixed as the poses are; a slot the weapon
    // has no motion for counts as the game's own pose, held as Days holds it (the held pose, and its share)
    if (m.weapon_motions) {
        if (actor.weapon_skeleton != m.weapon_skeleton.get()) {
            actor.weapon_players.clear();
            actor.weapon_skeleton = m.weapon_skeleton.get();
        }
        std::vector<std::vector<Mat>> played;
        std::vector<float> shares;
        float days = game;
        for (const Pose &pose : poses) {
            const kh2::Motion *motion =
                pose.slot >= 0 ? m.weapon_motions->motion(static_cast<std::size_t>(pose.slot)) : nullptr;
            if (motion == nullptr) {
                days += pose.weight;
                continue;
            }
            auto &player = actor.weapon_players[pose.slot];
            if (!player)
                player = std::make_unique<kh2::MotionPlayer>(*m.weapon_skeleton);
            std::vector<Mat> bones;
            try {
                player->evaluate(*motion, std::fmin(pose.ticks, motion->length_ticks()), kIdentity, nullptr);
                for (const auto &g : player->joints())
                    bones.push_back(in_metres(g));
            } catch (const std::exception &e) { // held as Days holds it, as a slot with no motion
                say_bad_motion(m, "its weapon's mset", pose.slot, e.what());
                days += pose.weight;
                continue;
            }
            played.push_back(std::move(bones));
            shares.push_back(pose.weight / total);
        }
        kh2_weapon_share = days / total;
        kh2_weapon_pose.assign(m.weapon_rest.size(), kIdentity);
        std::vector<Mat> frames;
        std::vector<float> weights;
        for (std::size_t b = 0; b < m.weapon_rest.size(); ++b) {
            frames.assign(1, m.weapon_rest[b]);
            weights.assign(1, kh2_weapon_share);
            for (std::size_t p = 0; p < played.size(); ++p)
                if (b < played[p].size()) {
                    frames.push_back(played[p][b]);
                    weights.push_back(shares[p]);
                }
            kh2_weapon_pose[b] = khdays::models::blend_frames(frames.data(), weights.data(), frames.size());
        }
    }
    // each pose's joints in metres (KH2's centimetres)
    std::vector<std::vector<Mat>> placed;
    for (const Pose &pose : poses) {
        std::vector<Mat> joints;
        for (const auto &g : *pose.joints) {
            Mat w = mul(pose.turn, mul(pose.taken, g));
            w[12] = (w[12] + pose.offset[0]) * 0.01F;
            w[13] = (w[13] + pose.offset[1]) * 0.01F;
            w[14] = (w[14] + pose.offset[2]) * 0.01F;
            joints.push_back(mul(base, w));
        }
        placed.push_back(std::move(joints));
    }
    // each joint's frames mixed (turns the shorter way round), the game's own pose at its share, on its parent's mixed
    // frame as each pose holds it from its parent (docs/KH2_MODELS.md, "Into the idle")
    const std::size_t count = std::min(m.joints.size(), m.kh2_bone.size());
    std::vector<Mat> game_frame(count), mixed(count);
    std::vector<char> is_mixed(count, 0);
    for (std::size_t j = 0; j < count; ++j)
        game_frame[j] = mul(skin[j], m.kh2_rest[j]);
    std::vector<Mat> frames;
    std::vector<float> weights;
    for (std::size_t j = 0; j < count; ++j) {
        const int bone = m.kh2_bone[j];
        if (bone < 0)
            continue;
        const int parent = m.joints[j].parent;
        const bool on_parent = parent >= 0 && static_cast<std::size_t>(parent) < j && is_mixed[parent] != 0;
        const auto b = static_cast<std::size_t>(bone);
        const auto pb = on_parent ? static_cast<std::size_t>(m.kh2_bone[static_cast<std::size_t>(parent)]) : 0U;
        frames.assign(1, on_parent ? mul(inverse(game_frame[static_cast<std::size_t>(parent)]), game_frame[j])
                                   : game_frame[j]);
        weights.assign(1, game / total);
        for (std::size_t p = 0; p < poses.size(); ++p) {
            if (b >= placed[p].size() || pb >= placed[p].size())
                continue;
            frames.push_back(on_parent ? mul(inverse(placed[p][pb]), placed[p][b]) : placed[p][b]);
            weights.push_back(poses[p].weight / total);
        }
        const Mat frame = khdays::models::blend_frames(frames.data(), weights.data(), frames.size());
        mixed[j] = on_parent ? mul(mixed[static_cast<std::size_t>(parent)], frame) : frame;
        is_mixed[j] = 1;
        skin[j] = mul(mixed[j], m.joints[j].inverse_bind);
    }
    into_idle();
}

// The face's frame (face.h), once a game frame: each block's slot held by what comes first -- its character's text typing
// (`face talk`), the game's pattern animation (`face ds`) unless what it would cover is `face closed`, its speaker's last
// portrait (`face portrait`), the KH2 motion's ranges (id 23) --, the motion's triggers passed (4 a default, 5 the file's
// back), then the frames since the last played. A block whose sprite changes is patched into each copy of its texture (KH2
// sends the texture, then the sprites over it: kn::ModelSKL::setTexture)
void play_face(Model &m)
{
    if (m.face_blocks.empty())
        return;
    const u32 now = khdays_events_vblank_count();
    const u32 frames = m.face_clock ? now - m.face_vblank : 1U;
    if (m.face_clock && frames == 0)
        return; // drawn again this frame: another actor, or a part
    if (m.face_clock && frames > 300U)
        m.face_portrait = -1; // not drawn for 5 seconds: another scene
    m.face_clock = true;
    m.face_vblank = now;
    if (std::find(m.face.speakers.begin(), m.face.speakers.end(), dialogue_speaker) != m.face.speakers.end())
        m.face_portrait = dialogue_speaker;
    if (khdays_link_paused())
        return;
    const FacePortrait *portrait = nullptr;
    for (const FacePortrait &shown : m.face.portraits)
        if (shown.portrait == m.face_portrait)
            portrait = &shown;
    const int talk = portrait != nullptr && portrait->talk >= 0 ? portrait->talk : m.face.talk;
    const bool talking =
        talk >= 0 && dialogue_typing && now - dialogue_typed <= 6U &&
        std::find(m.face.speakers.begin(), m.face.speakers.end(), dialogue_speaker) != m.face.speakers.end();
    int ds_slot = -1;
    for (const auto &[pattern, slot] : m.face.ds)
        if (std::find(materials_playing.begin(), materials_playing.end(), pattern) != materials_playing.end())
            ds_slot = slot;
    const auto *attributes = m.face.ranges ? face_motion.attributes : nullptr;
    const float ticks = face_motion.ticks, before = face_motion.before;
    for (auto &block : m.face_blocks) {
        khdays::face::Player &player = *block->player;
        int held = -1;
        if (attributes != nullptr) {
            for (const auto &t : attributes->triggers) {
                const auto f = static_cast<float>(t.frame);
                const bool passed = before < 0.0F     ? f <= ticks
                                    : ticks >= before ? f > before && f <= ticks
                                                      : f > before || f <= ticks; // looped
                if (passed && t.id == 4 && !t.params.empty())
                    player.set_default(t.params[0]);
                else if (passed && t.id == 5)
                    player.reset_default();
            }
            for (const auto &r : attributes->ranges)
                if (r.id == 23 && !r.params.empty() && static_cast<float>(r.start) <= ticks &&
                    ticks <= static_cast<float>(r.end) && player.knows(r.params[0]))
                    held = r.params[0];
        }
        if (portrait != nullptr)
            for (const int slot : portrait->slots) {
                const int s = slot == -2 ? block->last_slot : slot;
                if (s >= 0 && player.knows(s))
                    held = s;
            }
        if (ds_slot >= 0 && player.knows(ds_slot) &&
            std::find(m.face.closed.begin(), m.face.closed.end(), held) == m.face.closed.end())
            held = ds_slot;
        if (talking && player.knows(talk))
            held = talk;
        if (held >= 0)
            player.set(held);
        player.play(static_cast<float>(std::min(frames, 30U))); // (a long gap: not drawn, or loading)
    }
    for (FaceTexture &t : m.face_textures) {
        bool changed = false;
        for (const auto &block : m.face_blocks)
            changed = changed || (block->animation.texture == t.key && block->player->sprite() != block->shown);
        if (!changed)
            continue;
        auto *texels = reinterpret_cast<std::uint8_t *>(khdays_textures_change_begin(t.replacement));
        for (const auto &block : m.face_blocks) {
            const auto &a = block->animation;
            if (a.texture != t.key)
                continue;
            const int sprite = block->player->sprite();
            for (int y = std::max(0, -a.y); y < a.height && a.y + y < t.height; ++y)
                for (int x = std::max(0, -a.x); x < a.width && a.x + x < t.width; ++x) {
                    const std::size_t at = 4 * (static_cast<std::size_t>(a.y + y) * static_cast<std::size_t>(t.width) +
                                                static_cast<std::size_t>(a.x + x));
                    if (sprite < 0) {
                        std::memcpy(texels + at, t.picture.data() + at, 4);
                        continue;
                    }
                    const std::uint8_t *px = a.sprites[static_cast<std::size_t>(sprite)].data() +
                                             4 * (static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width) +
                                                  static_cast<std::size_t>(x));
                    std::memcpy(texels + at, px, 3);
                    texels[at + 3] = t.alpha == 1 ? 255 : t.alpha == 2 ? (px[3] >= t.least ? 255 : 0) : px[3];
                }
        }
        khdays_textures_change_end(t.replacement);
    }
    for (auto &block : m.face_blocks)
        block->shown = block->player->sprite();
}

// `held <joint> rest`: a weapon made at rest where the bone holding it is at rest (B_EX390's baked keyblade bones and
// W_EX010's bone 2), so it moves as that bone has moved from rest. `root` is the game's weapon frame, the `weapon`
// line's turn on the bone; the joint, those above it and those under it not on a node of the game's are carried.
void hold_on_bone(Model &m, const Mat &root)
{
    const auto held =
        std::find_if(m.joints.begin(), m.joints.end(), [&](const Joint &j) { return j.name == m.held_name; });
    if (held == m.joints.end())
        return;
    if (!m.held_rest_checked) {
        m.held_rest_checked = true;
        const auto own = translation(inverse(held->inverse_bind)), bone = translation(holding_bone.rest);
        const float off = length({own[0] - bone[0], own[1] - bone[1], own[2] - bone[2]});
        if (off > 0.01F)
            khdays_log(
                "models: %s: `held %s rest`: its rest is %.1f cm from the holding bone's, so it sits that far off "
                "the hand; give it `held %s` with the body's `weapon_motions`, or a model made at that bone's rest",
                m.name.c_str(), m.held_name.c_str(), static_cast<double>(off * 100.0F), m.held_name.c_str());
    }
    Mat by = kIdentity;
    by[0] = by[5] = by[10] = m.scale;
    const Mat on_bone = mul(root, mul(holding_bone.from_game, mul(by, held->inverse_bind)));
    std::vector<char> carried(m.joints.size(), 0);
    for (int up = static_cast<int>(held - m.joints.begin()); up >= 0;
         up = m.joints[static_cast<std::size_t>(up)].parent)
        carried[static_cast<std::size_t>(up)] = 1;
    for (std::size_t j = 0; j < m.joints.size(); ++j) {
        const int parent = m.joints[j].parent;
        if (carried[j] || (m.joints[j].node < 0 && parent >= 0 && carried[static_cast<std::size_t>(parent)])) {
            carried[j] = 1;
            skin[j] = on_bone;
        }
    }
}

void draw(Model &m)
{
    float pos[16], vecm[16], proj_f[16];
    khdays_g3d_matrices(pos, vecm, proj_f);
    Mat proj;
    std::copy(proj_f, proj_f + 16, proj.begin());
    const std::size_t nodes = m.node_rest_inverse.size();

    // D per node; one the draw did not send takes its parent's (the first sent for a root)
    d.assign(nodes, kIdentity);
    int first = -1;
    for (std::size_t g = 0; g < nodes; ++g) {
        if (node_sent[g] && first < 0)
            first = static_cast<int>(g);
    }
    if (first < 0)
        return;
    std::vector<char> done(nodes, 0);
    for (std::size_t pass = 0; pass <= nodes; ++pass) {
        for (std::size_t g = 0; g < nodes; ++g) {
            if (done[g])
                continue;
            const int p = m.node_parent[g];
            if (node_sent[g]) {
                d[g] = mul(node_matrix[g], m.node_rest_inverse[g]);
            } else if (p >= 0 && static_cast<std::size_t>(p) < nodes && p != static_cast<int>(g)) {
                if (!done[static_cast<std::size_t>(p)])
                    continue;
                d[g] = d[static_cast<std::size_t>(p)];
            } else {
                d[g] = mul(node_matrix[static_cast<std::size_t>(first)],
                           m.node_rest_inverse[static_cast<std::size_t>(first)]);
            }
            done[g] = 1;
        }
    }
    const Mat &root = d[static_cast<std::size_t>(first)];
    // the session log's diagnosis: how many of its nodes a draw sends (the first draws, by
    // frame: several in one frame are draws of parts), and at two moments each node's
    // matrix the game sent, as the drawn picture's (the camera's) and its turn and scale
    // from rest (D)
    ++m.draws;
    if (trace != 0 && khdays_log_on() && (m.draws <= 6 || m.draws == 120 || m.draws == 600)) {
        int sent = 0;
        for (std::size_t g = 0; g < nodes; ++g)
            sent += node_sent[g] ? 1 : 0;
        khdays_log("models: %s draw %d: %d of %zu nodes sent, the first %d; the game's material: POLYGON_ATTR %08x",
                   m.name.c_str(), m.draws, sent, nodes, first, material.attr);
        for (std::size_t g = 0; (m.draws == 120 || m.draws == 600) && g < nodes; ++g) {
            const Mat &M = node_matrix[g];
            const Mat &D = d[g];
            khdays_log(
                "models:   node %zu %s%s: sent at %.2f %.2f %.2f scale %.3f %.3f %.3f; D scale %.3f %.3f %.3f at %.2f %.2f %.2f",
                g, m.node_names[g].c_str(), node_sent[g] ? "" : " (not sent)", static_cast<double>(M[12]),
                static_cast<double>(M[13]), static_cast<double>(M[14]), static_cast<double>(length({M[0], M[1], M[2]})),
                static_cast<double>(length({M[4], M[5], M[6]})), static_cast<double>(length({M[8], M[9], M[10]})),
                static_cast<double>(length({D[0], D[1], D[2]})), static_cast<double>(length({D[4], D[5], D[6]})),
                static_cast<double>(length({D[8], D[9], D[10]})), static_cast<double>(D[12]),
                static_cast<double>(D[13]), static_cast<double>(D[14]));
        }
    }
    if (trace == 2 && draws++ % 300 == 0) {
        // each node's turn from its rest, after the model's first node's (the camera's and placing's)
        Mat r = root;
        r[12] = r[13] = r[14] = 0.0F;
        const Mat ri = inverse(r);
        std::fprintf(stderr, "models: %s, draw %d:", m.name.c_str(), draws - 1);
        for (std::size_t g = 0; g < nodes; ++g) {
            Mat l = d[g];
            l[12] = l[13] = l[14] = 0.0F;
            const Mat q = mul(ri, l);
            const float tr = q[0] / length({q[0], q[1], q[2]}) + q[5] / length({q[4], q[5], q[6]}) +
                             q[10] / length({q[8], q[9], q[10]});
            std::fprintf(stderr, " %zu:%.0f", g,
                         std::acos(std::fmax(-1.0F, std::fmin(1.0F, (tr - 1.0F) / 2.0F))) * 57.2957795F);
        }
        std::fprintf(stderr, "\n");
    }

    // its own animation's clock: the game's frames since its first draw, standing while the game is paused
    std::vector<Mat> moved;
    if (m.own) {
        moved = m.own->world_now;
    } else if (m.own_motion) {
        const u32 now = khdays_events_vblank_count();
        if (m.clock_started && !khdays_link_paused())
            m.frames += now - m.last_vblank;
        m.clock_started = true;
        m.last_vblank = now;
        if (!m.animations.empty())
            moved = khdays::room::node_worlds(m.nodes, m.animations, static_cast<double>(m.frames) / 60.0);
    }
    Mat scale = kIdentity;
    scale[0] = scale[5] = scale[10] = m.scale;
    const Mat scaled_root = mul(root, scale);

    // each joint, parents first; `ds_pose` turns by the game's animation with the most weight that plays no
    // KH2 motion (its own lines, else the default's)
    std::vector<const Mat *> ds_turn(m.joints.size(), nullptr);
    if (!m.ds_turns.empty()) {
        const Playing *game_anim = nullptr;
        for (const Playing &p : playing) {
            const auto s = m.kh2_sections.find(p.name);
            const bool kh2 =
                m.kh2_motions && s != m.kh2_sections.end() && !s->second.empty() && s->second.front().from <= p.frame;
            if (!kh2 && (game_anim == nullptr || p.ratio > game_anim->ratio))
                game_anim = &p;
        }
        auto turns = game_anim != nullptr ? m.ds_turns.find(game_anim->name) : m.ds_turns.end();
        if (turns == m.ds_turns.end())
            turns = m.ds_turns.find("");
        if (turns != m.ds_turns.end())
            for (const auto &[j, turn] : turns->second)
                ds_turn[j] = &turn;
    }
    k.resize(m.joints.size());
    skin.resize(m.joints.size());
    const auto pose = [&](std::size_t j) {
        const Joint &joint = m.joints[j];
        const Mat *parent = joint.parent >= 0 ? &k[static_cast<std::size_t>(joint.parent)] : nullptr;
        if (joint.node >= 0 && joint.exact)
            return d[static_cast<std::size_t>(joint.node)];
        if (joint.node >= 0) {
            const Mat &dg = d[static_cast<std::size_t>(joint.node)];
            if (joint.placed) { // on the game's node itself (its offset turned as the node is)
                const auto &at = m.node_rest_place[static_cast<std::size_t>(joint.node)];
                return place(point(dg, {at[0] + joint.place_offset[0], at[1] + joint.place_offset[1],
                                        at[2] + joint.place_offset[2]}),
                             dg, joint.correction, joint.head);
            }
            return place(point(parent != nullptr ? *parent : dg, joint.head), dg, joint.correction, joint.head);
        }
        const Mat &carried = parent != nullptr ? *parent : root;
        return ds_turn[j] != nullptr ? mul(carried, *ds_turn[j]) : carried;
    };
    for (std::size_t j = 0; j < m.joints.size(); ++j)
        k[j] = pose(j);
    if (m.spine.end >= 0)
        bend_spine(m, pose);
    for (const auto &limb : m.limbs) {
        bool placed = false;
        for (const int j : limb.joints)
            placed = placed || m.joints[static_cast<std::size_t>(j)].placed;
        if (!placed) // a placed limb is where the game's is: nothing to reach
            reach(m, limb, pose);
    }
    // the bones above placed joints swung and stretched along themselves to reach them
    for (const Stretch &st : m.stretches) {
        const auto p = static_cast<std::size_t>(st.joint), c = static_cast<std::size_t>(st.child);
        const auto hp = point(k[p], m.joints[p].head), rigid = point(k[p], m.joints[c].head),
                   actual = point(k[c], m.joints[c].head);
        const Mat x = turned_onto(hp, {rigid[0] - hp[0], rigid[1] - hp[1], rigid[2] - hp[2]},
                                  {actual[0] - hp[0], actual[1] - hp[1], actual[2] - hp[2]});
        for (const int r : st.riders)
            k[static_cast<std::size_t>(r)] = mul(x, k[static_cast<std::size_t>(r)]);
    }
    if (!m.hands.empty())
        pose_hands(m);
    for (std::size_t j = 0; j < m.joints.size(); ++j) {
        const Joint &joint = m.joints[j];
        const Mat *w = moved_world(joint, moved);
        skin[j] = w != nullptr ? mul(scaled_root, mul(*w, joint.inverse_bind)) : mul(k[j], joint.bind);
    }
    // bones.txt's `held`: a weapon's bones from its root to the weapon itself where the body holding it puts them
    // (its `weapon_motions` pose, from the frame the game draws the weapon at), the bones under them carried
    // (a chain, for the cloth); a body unseen for a second leaves the model as the game's nodes place it
    const u32 held_now = khdays_events_vblank_count();
    const bool on_bone = !m.held_name.empty() && m.held_rest && held_now - holding_bone.vblank < 60U;
    if (on_bone) {
        hold_on_bone(m, root);
    } else if (!m.held_name.empty() && !m.held_rest && !held_weapon.pose.empty() &&
               held_now - held_weapon.vblank < 60U) {
        if (m.held_bone.size() != m.joints.size()) { // the held joint and those above it, by their KH2 bones
            m.held_bone.assign(m.joints.size(), -1);
            for (std::size_t j = 0; j < m.joints.size(); ++j)
                if (m.joints[j].name == m.held_name)
                    for (int up = static_cast<int>(j); up >= 0; up = m.joints[static_cast<std::size_t>(up)].parent) {
                        const std::string &name = m.joints[static_cast<std::size_t>(up)].name;
                        if (name.rfind("kh2_", 0) == 0)
                            m.held_bone[static_cast<std::size_t>(up)] = std::atoi(name.c_str() + 4);
                    }
        }
        Mat by = kIdentity;
        by[0] = by[5] = by[10] = m.scale;
        std::vector<char> moved_joint(m.joints.size(), 0);
        for (std::size_t j = 0; j < m.joints.size(); ++j) {
            const int b = m.held_bone[j], parent = m.joints[j].parent;
            if (b >= 0 && static_cast<std::size_t>(b) < held_weapon.pose.size()) {
                skin[j] = mul(mul(root, mul(held_weapon.from_game, held_weapon.pose[static_cast<std::size_t>(b)])),
                              mul(by, m.joints[j].inverse_bind));
                moved_joint[j] = 1;
            } else if (m.joints[j].node < 0 && parent >= 0 && moved_joint[static_cast<std::size_t>(parent)]) {
                skin[j] = skin[static_cast<std::size_t>(parent)];
                moved_joint[j] = 1;
            }
        }
    }
    // KH2 motions on the game's root node (its first: roxas_R, never animated), whatever bone 0 follows
    face_motion = {};
    kh2_game_share = 1.0F;
    kh2_pose_cut = false;
    kh2_played.clear();
    kh2_weapon_pose.clear();
    kh2_weapon_share = 1.0F;
    if (m.kh2_motions && !m.kh2_sections.empty())
        play_kh2(m, scaled_root);
    play_face(m);
    // bones.txt's `twist`: those joints (and the ones under them) turned with the other joint's twist, before the
    // cloth reads the pose
    for (const Twist &t : m.twists) {
        namespace kh2 = khdays::assets::kh2;
        const auto rest = [&](std::size_t j) { return inverse(m.joints[j].inverse_bind); };
        const auto now = [&](std::size_t j) { return mul(skin[j], rest(j)); };
        if (t.swing) {
            const Mat frame_now = mul(axes_of(now(t.to)), inverse(axes_of(rest(t.to))));
            const auto kept = vec(frame_now, vec(axes_of(rest(t.by)), {1.0F, 0.0F, 0.0F}));
            auto gone = vec(axes_of(now(t.by)), {1.0F, 0.0F, 0.0F});
            if (t.except) {
                // from the joint's own frame: a `twist` has already turned it against `frame`
                const auto way = vec(mul(axes_of(now(t.joint)), inverse(axes_of(rest(t.joint)))), t.except_way);
                gone = khdays::models::cuff_swing_way(kept, gone,
                                                      khdays::models::hand_unit(khdays::models::hand_add(
                                                          way, kept, -khdays::models::hand_dot(way, kept))));
            }
            const auto toward = khdays::models::hand_unit(khdays::models::hand_add(
                khdays::models::hand_add({0.0F, 0.0F, 0.0F}, kept, 1.0F - t.share), gone, t.share));
            const Mat at = now(t.joint);
            const Mat turn = turned_onto({at[12], at[13], at[14]}, kept, toward);
            for (const std::size_t j : t.turned)
                skin[j] = mul(turn, skin[j]);
            continue;
        }
        const Mat parent_rest = rest(t.parent), parent_now = now(t.parent), to = now(t.to);
        const float angle = t.share * kh2::twist_about(parent_rest, rest(t.by), parent_now, now(t.by), t.axis);
        const Mat turn = kh2::twist_turn(parent_rest, parent_now, t.axis, angle, {to[12], to[13], to[14]});
        for (const std::size_t j : t.turned)
            skin[j] = mul(turn, skin[j]);
    }
    pose_cuffs(m);
    // secondary motion over the finished pose (the game's or the KH2 motions', blended), before skinning
    if (m.secondary) {
        khdays::secondary::DrawInput in;
        in.actor = drawn_object;
        in.root = scaled_root;
        in.start = drawn_start;
        in.base = drawn_base;
        in.base_kind = drawn_base_kind;
        in.camera = drawn_camera;
        in.has_camera = drawn_has_camera;
        in.projection = proj_f;
        in.game_nodes = &d;
        in.pose_cut = kh2_pose_cut;
        for (const Playing &p : playing) {
            khdays::secondary::DrawAnimation a{p.name, -1, -1.0F, p.frame, p.frames, p.ratio};
            if (const auto played = kh2_played.find(p.name); played != kh2_played.end()) {
                a.slot = static_cast<int>(played->second.first);
                a.section = played->second.second;
            }
            in.animations.push_back(std::move(a));
        }
        khdays::secondary::apply_model_secondary(*m.secondary, in, skin);
    }
    // a joint's frame as drawn, without bones.txt's `scale`: what the game attaches to it keeps the game's size
    Mat unscale = kIdentity;
    unscale[0] = unscale[5] = unscale[10] = m.scale > 0.0F ? 1.0F / m.scale : 1.0F;
    const auto joint_frame = [&](std::size_t j) {
        return mul(mul(skin[j], inverse(m.joints[j].inverse_bind)), unscale);
    };
    // the frame a weapon is drawn at on joint j for node g: the line's turn; with `weapon_motions` the weapon itself
    // turned in the hand as its motion turns it from its held pose (slot 0's first frame: the line's turn alone,
    // KH2's idle), and the turn from it to the weapon's root kept for a `held` weapon model
    const std::vector<Mat> &weapon_pose = kh2_weapon_pose.empty() ? m.weapon_rest : kh2_weapon_pose;
    const float weapon_share = m.weapon_motions && !kh2_weapon_pose.empty() ? kh2_weapon_share : kh2_game_share;
    const auto hold = [&](std::size_t j, std::size_t g) {
        const Mat turn = g < m.kh2_weapon_turn.size() ? m.kh2_weapon_turn[g] : kIdentity;
        holding_bone = {inverse(turn), inverse(m.joints[j].inverse_bind), khdays_events_vblank_count()};
        if (!m.weapon_motions || static_cast<std::size_t>(m.weapon_blade) >= weapon_pose.size())
            return mul(joint_frame(j), turn);
        const auto b = static_cast<std::size_t>(m.weapon_blade);
        const Mat weapon = mul(weapon_pose[b], mul(inverse(m.weapon_rest[b]), turn));
        held_weapon.pose = weapon_pose;
        held_weapon.from_game = inverse(weapon);
        held_weapon.vblank = khdays_events_vblank_count();
        return mul(joint_frame(j), weapon);
    };
    // what the game attaches to a node (its weapons) on the joint a `weapon` line names, as it is drawn:
    // its saved matrix (MtxFx43, the same space as the nodes') written over
    for (const auto &[saved, node] : saved_joints) {
        if (node >= m.kh2_weapon.size() || m.kh2_weapon[node] < 0)
            continue;
        const auto j = static_cast<std::size_t>(m.kh2_weapon[node]);
        s32 fx[12];
        std::memcpy(fx, saved, sizeof(fx)); // the game's own frame for it, as it saved it
        Mat game = kIdentity;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r)
                game[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(fx[c * 3 + r]) / 4096.0F;
        const Mat at = khdays::models::attached_frame(hold(j, node), game, weapon_share);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r)
                fx[c * 3 + r] = static_cast<s32>(std::lround(at[static_cast<std::size_t>(c * 4 + r)] * 4096.0F));
        std::memcpy(saved, fx, sizeof(fx));
    }
    // the session log's diagnosis, whole: each node's matrix sent and each joint's K, to
    // pose the mod's model again offline (columns 0-3 of the 3x4)
    if (trace != 0 && khdays_log_on() && (m.draws == 120 || m.draws == 600)) {
        auto line = [](const Mat &a) {
            char text[400];
            std::snprintf(text, sizeof(text), "%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f",
                          static_cast<double>(a[0]), static_cast<double>(a[1]), static_cast<double>(a[2]),
                          static_cast<double>(a[4]), static_cast<double>(a[5]), static_cast<double>(a[6]),
                          static_cast<double>(a[8]), static_cast<double>(a[9]), static_cast<double>(a[10]),
                          static_cast<double>(a[12]), static_cast<double>(a[13]), static_cast<double>(a[14]));
            return std::string(text);
        };
        for (std::size_t g = 0; g < nodes; ++g) {
            khdays_log("models:   M %zu %s", g, line(node_matrix[g]).c_str());
        }
        for (std::size_t j = 0; j < m.joints.size(); ++j) {
            khdays_log("models:   K %zu %s %s", j, m.joints[j].name.c_str(), line(k[j]).c_str());
        }
        // a room's own moving parts: where each moved joint is in the file's space at this frame
        for (const Joint &joint : m.joints) {
            if (const Mat *w = moved_world(joint, moved)) {
                khdays_log("models:   moved %s at %.1f %.1f %.1f, frame %llu", joint.name.c_str(),
                           static_cast<double>((*w)[12]), static_cast<double>((*w)[13]), static_cast<double>((*w)[14]),
                           static_cast<unsigned long long>(m.frames));
            }
        }
    }
    // what the game attaches to its nodes (NNS_G3dGetResultMtx after the draw): each node's frame where the model's
    // joint is (a `weapon` line's turn for its node), for result_matrix
    if (drawn_object != nullptr) { // (a model at a place of the port's has no render object)
        if (node_results.size() > 64)
            node_results.clear();
        auto &results = node_results[drawn_object];
        results.assign(nodes, {false, kIdentity});
        for (std::size_t g = 0; g < nodes && g < m.node_joint.size(); ++g) {
            if (g < m.kh2_weapon.size() && m.kh2_weapon[g] >= 0) {
                const auto j = static_cast<std::size_t>(m.kh2_weapon[g]);
                results[g] = {true, khdays::models::attached_frame(hold(j, g), node_matrix[g], weapon_share)};
            } else if (m.node_joint[g] >= 0) {
                const auto j = static_cast<std::size_t>(m.node_joint[g]);
                results[g] = {true, mul(mul(joint_frame(j), m.joints[j].to_node), m.node_local[g])};
            }
        }
    }
    // bones.txt's mesh scales, last: the cloth and the weapons were placed on the unscaled bones
    for (const auto &[j, by] : m.mesh_scales)
        if (j < skin.size())
            skin[j] = mul(skin[j], by);
    const Mat unskinned = scaled_root; // a mesh with no skin: placed with the model's first node

    // `clip_world`: the coat's closed volume, not drawn; the room inside it is left out, this model's own
    // polygons drawn there (docs/RENDERING.md)
    clip_triangles.clear();
    if (m.secondary)
        khdays::secondary::clip_volumes(*m.secondary, skin, clip_triangles);
    for (const auto &t : clip_triangles) {
        KhdaysG3dClipVertex tri[3] = {};
        for (int c = 0; c < 3; ++c) {
            const auto &p = t[static_cast<std::size_t>(c)];
            KhdaysG3dClipVertex &o = tri[c];
            o.x = proj[0] * p.x + proj[4] * p.y + proj[8] * p.z + proj[12];
            o.y = proj[1] * p.x + proj[5] * p.y + proj[9] * p.z + proj[13];
            o.z = proj[2] * p.x + proj[6] * p.y + proj[10] * p.z + proj[14];
            o.w = proj[3] * p.x + proj[7] * p.y + proj[11] * p.z + proj[15];
            o.a = 1.0F;
        }
        khdays_g3d_clip_polygon(tri);
    }
    khdays_g3d_set_clip_owner(!clip_triangles.empty());

    // the polygons' attributes: the game's material's, the faces the mesh's. A mesh drawn whole (OPAQUE or
    // MASK) is drawn opaque when the game's material is all but opaque: the rooms are drawn at alpha 30 of
    // 31, and a translucent pixel takes no other translucent polygon of the same ID (the DS's rule), so a
    // mod's room, one ID, would show only what it drew first. A material fading
    // further keeps its alpha.
    const uint32_t tex = (7u << 26) | (7u << 23) | (7u << 20) | (1u << 17) | (1u << 16); // 1024x1024, repeated
    const uint32_t game_alpha = (material.attr >> 16) & 31u;
    for (const Mesh &mesh : m.meshes) {
        if (mesh.shadow_sprite && khdays_shadow_has_sprite())
            continue;
        // its picture now, and how far its texture has scrolled
        const Mesh::Picture picture = picture_now(mesh, m.frames);
        const float tex_w = picture.w, tex_h = picture.h;
        const auto offset = khdays::room::scroll_offset(mesh.scroll, m.frames, tex_w, tex_h);
        uint32_t attr = (material.attr & ~0xc0u) | (mesh.double_sided ? 0xc0u : 0x80u);
        if (!picture.translucent && game_alpha >= 30u)
            attr = (attr & ~(31u << 16)) | (31u << 16);
        clip.resize(mesh.vertices.size());
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            const auto &v = mesh.vertices[i];
            std::array<float, 3> p{0, 0, 0}, nrm{0, 0, 0};
            float total = 0.0F;
            for (int w = 0; w < 4; ++w) {
                const float weight = v.weights[static_cast<std::size_t>(w)];
                if (weight <= 0.0F)
                    continue;
                const std::uint32_t jt = v.joints[static_cast<std::size_t>(w)];
                const Mat &s = jt < skin.size() ? skin[jt] : unskinned;
                // a sky stays around the camera: turned with the room, not moved
                const auto q = mesh.sky != 0 ? vec(s, v.position) : point(s, v.position);
                const auto n = vec(s, v.normal);
                for (int c = 0; c < 3; ++c) {
                    p[static_cast<std::size_t>(c)] += weight * q[static_cast<std::size_t>(c)];
                    nrm[static_cast<std::size_t>(c)] += weight * n[static_cast<std::size_t>(c)];
                }
                total += weight;
            }
            if (total <= 0.0F) {
                p = mesh.sky != 0 ? vec(unskinned, v.position) : point(unskinned, v.position);
                nrm = vec(unskinned, v.normal);
            } else if (std::fabs(total - 1.0F) > 1e-4F) {
                for (int c = 0; c < 3; ++c)
                    p[static_cast<std::size_t>(c)] /= total;
            }
            KhdaysG3dClipVertex &o = clip[i];
            o.x = proj[0] * p[0] + proj[4] * p[1] + proj[8] * p[2] + proj[12];
            o.y = proj[1] * p[0] + proj[5] * p[1] + proj[9] * p[2] + proj[13];
            o.z = proj[2] * p[0] + proj[6] * p[1] + proj[10] * p[2] + proj[14];
            o.w = proj[3] * p[0] + proj[7] * p[1] + proj[11] * p[2] + proj[15];
            const float ln = length(nrm);
            float rgb[3];
            if (ln > 1e-6F) {
                const float unit[3] = {nrm[0] / ln, nrm[1] / ln, nrm[2] / ln};
                khdays_g3d_light(&material, unit, rgb);
            } else {
                const float up[3] = {0.0F, 0.0F, 0.0F};
                khdays_g3d_light(&material, up, rgb);
            }
            o.r = rgb[0] * static_cast<float>(v.color[0]) / 255.0F;
            o.g = rgb[1] * static_cast<float>(v.color[1]) / 255.0F;
            o.b = rgb[2] * static_cast<float>(v.color[2]) / 255.0F;
            o.s = (v.texcoord[0] + offset[0]) / tex_w * 1024.0F * 16.0F;
            o.t = (v.texcoord[1] + offset[1]) / tex_h * 1024.0F * 16.0F;
            o.a = mesh.vertex_alpha == VertexAlpha::blended ? static_cast<float>(v.color[3]) / 255.0F : 1.0F;
        }
        // the session log's diagnosis: where the mesh falls on the screen (x/w and y/w: -1..1
        // the view), and how near (w)
        if (trace != 0 && khdays_log_on() && (m.draws == 120 || m.draws == 600) && !clip.empty()) {
            float x0 = 1e30F, x1 = -1e30F, y0 = 1e30F, y1 = -1e30F, w0 = 1e30F, w1 = -1e30F;
            int behind = 0;
            for (const auto &o : clip) {
                if (o.w <= 0.0F) {
                    ++behind;
                    continue;
                }
                x0 = std::fmin(x0, o.x / o.w), x1 = std::fmax(x1, o.x / o.w);
                y0 = std::fmin(y0, o.y / o.w), y1 = std::fmax(y1, o.y / o.w);
                w0 = std::fmin(w0, o.w), w1 = std::fmax(w1, o.w);
            }
            khdays_log(
                "models:   mesh of %zu vertices: x/w %.2f..%.2f, y/w %.2f..%.2f, w %.3f..%.3f, %d behind; proj %s",
                clip.size(), static_cast<double>(x0), static_cast<double>(x1), static_cast<double>(y0),
                static_cast<double>(y1), static_cast<double>(w0), static_cast<double>(w1), behind,
                [&] {
                    std::string t;
                    char one[24];
                    for (int i = 0; i < 16; ++i) {
                        std::snprintf(one, sizeof(one), "%s%.4f", i ? " " : "",
                                      static_cast<double>(proj[static_cast<std::size_t>(i)]));
                        t += one;
                    }
                    return t;
                }()
                    .c_str());
        }
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const KhdaysG3dClipVertex tri[3] = {clip[mesh.indices[t]], clip[mesh.indices[t + 1]],
                                                clip[mesh.indices[t + 2]]};
            uint32_t tri_attr;
            if (triangle_attr(mesh, t, attr, tri_attr))
                khdays_g3d_mod_polygon(tri, 3, tri_attr, tex, picture.replacement,
                                       picture.translucent || mesh.vertex_alpha == VertexAlpha::blended, mesh.sky);
        }
    }
    khdays_g3d_set_clip_owner(0);
}

// Whether `path` (a mod's model replacing `name`, `mi/wd/wd_<code>#<member>/mdl/<model>`)
// is in the mod that also replaces the world's archive (`files/mi/wd/wd_<code>`).
bool room_of_replaced_world(const std::string &name, const char *path)
{
    const auto hash = name.find('#');
    if (name.rfind("mi/wd/wd_", 0) != 0 || hash == std::string::npos)
        return false;
    char archive[260];
    if (!khdays_mods_find(("files/" + name.substr(0, hash)).c_str(), archive, sizeof(archive)))
        return false;
    // mods/<Mod>/files/mi/wd/wd_<code>: the mod is four folders up
    const std::filesystem::path mod = u8(archive).parent_path().parent_path().parent_path().parent_path();
    std::error_code ec;
    return std::filesystem::equivalent(mod / "graphics" / (u8(name).string() + u8(path).extension().string()), u8(path),
                                       ec);
}

// The joint animations `render_obj` plays: NNSG3dRenderObj anmJnt (+0x10), each NNSG3dAnmObj's
// frame (+0, fx32), ratio (+4, fx32), resAnm (+8: its J0AC) and next (+0x10).
void read_playing(const void *render_obj)
{
    playing.clear();
    materials_playing.clear();
    if (render_obj == nullptr)
        return;
    u32 mat = 0; // anmMat (+0x08): the material animations, the eyes' pattern among them
    std::memcpy(&mat, static_cast<const unsigned char *>(render_obj) + 0x08, 4);
    for (int n = 0; mat != 0 && n < 8; ++n) {
        const auto *a = static_cast<const unsigned char *>(KHDAYS_W(mat));
        u32 anm = 0;
        std::memcpy(&anm, a + 8, 4);
        materials_playing.push_back(animation_name(anm));
        std::memcpy(&mat, a + 0x10, 4);
    }
    u32 obj = 0;
    std::memcpy(&obj, static_cast<const unsigned char *>(render_obj) + 0x10, 4);
    for (int n = 0; obj != 0 && n < 8; ++n) {
        const auto *a = static_cast<const unsigned char *>(KHDAYS_W(obj));
        s32 frame = 0, ratio = 0;
        u32 anm = 0, next = 0;
        std::memcpy(&frame, a, 4);
        std::memcpy(&ratio, a + 4, 4);
        std::memcpy(&anm, a + 8, 4);
        std::memcpy(&next, a + 0x10, 4);
        const std::string &name = animation_name(anm);
        u16 frames = 0;
        if (anm != 0)
            std::memcpy(&frames, static_cast<const unsigned char *>(KHDAYS_W(anm)) + 4, 2);
        playing.push_back({name, static_cast<float>(frame) / 4096.0F, static_cast<float>(ratio) / 4096.0F,
                           static_cast<float>(frames)});
        obj = next;
    }
}

} // namespace

extern "C" void khdays_walk_model_draw(unsigned model_resource); /* runtime/input/walk.h */
extern "C" void khdays_walk_model_draw_end(void);

extern "C" void khdays_models_draw_begin(const void *resource, const void *render_obj)
{
    if (render_obj != nullptr)
        drawn_lately[KHDAYS_REF(render_obj)] = khdays_events_vblank_count();
    if (depth == 0) {
        begun_object = render_obj;
        khdays_g3d_draw_begin(resource); /* the polygons' key */
        if (resource != nullptr) {
            khdays_walk_model_draw(KHDAYS_REF(resource)); /* the Walk mod: the player's weapon to the hand */
        }
    }
    if (depth > 0) {
        if (depth < 8)
            outer[depth] = active;
        ++depth;
        active = nullptr;
        return;
    }
    ++depth;
    drawn_animation = next_animation;
    next_animation = 0;
    const bool hd = khdays_textures_hd() != 0;
    if (resource == nullptr) {
        return;
    }
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (!khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run)) { // its DS address
        return;
    }
    // HD off: only a mission world's rooms and the mods' characters (their own folders, ba/ch/c<number>/)
    if (!hd && std::strstr(file, "mi/wd/wd_") == nullptr && std::strncmp(file, "ba/ch/c", 7) != 0) {
        return;
    }
    char key[160];
    std::snprintf(key, sizeof(key), "%s@%x", file, offset);
    auto it = models.find(key);
    if (it == models.end()) {
        char name[160], relative[200], path[260];
        std::unique_ptr<Model> loaded;
        const bool named = khdays_names_model(file, offset, name, sizeof(name)) != 0;
        if (named && khdays_characters_model(name, path, sizeof(path))) {
            loaded = load(name, path, file, offset);
            if (loaded) {
                loaded->character = true;
                khdays_log("models: %s drawn from %s, a mod's character", name, path);
            }
        } else if (named) {
            /* glTF first, then binary glTF, FBX and KH2's */
            for (const char *extension : {".gltf", ".glb", ".fbx", ".mdlx"}) {
                std::snprintf(relative, sizeof(relative), "graphics/%s%s", name, extension);
                if (khdays_mods_find(relative, path, sizeof(path))) {
                    loaded = load(name, path, file, offset);
                    if (loaded && room_of_replaced_world(name, path)) {
                        loaded->room = true;
                        khdays_log(
                            "models: %s drawn from %s, its world replaced by the same mod (with HD graphics on or off)",
                            name, path);
                    }
                    break;
                }
            }
            if (tracing()) {
                std::fprintf(stderr, "models: %s drawn%s\n", name, loaded ? ", replaced" : "");
            }
        }
        it = models.emplace(key, std::move(loaded)).first;
    }
    active = it->second.get();
    if (active != nullptr && !hd && !active->room && !active->character) {
        active = nullptr;
    }
    if (active != nullptr && active->setting >= 0 && !khdays_hud_setting(active->setting)) {
        active = nullptr; /* its switch off: the game's own */
    }
    model_start = static_cast<const unsigned char *>(resource);
    if (active != nullptr && active->own) {
        active->own->world_now = own_pose(*active); /* its pose before its nodes: its weapon's */
    }
    if (active != nullptr) {
        shapes = 0;
        node_matrix.assign(active->node_rest_inverse.size(), kIdentity);
        node_sent.assign(active->node_rest_inverse.size(), 0);
        saved_joints.clear();
        read_playing(render_obj);
        drawn_object = render_obj;
        {
            float start[16], vecm[16], proj[16];
            khdays_g3d_matrices(start, vecm, proj);
            std::copy(start, start + 16, drawn_start.begin());
            float base[16], camera[16];
            drawn_base_kind = khdays_g3d_base(base);
            std::copy(base, base + 16, drawn_base.begin());
            drawn_has_camera = khdays_g3d_camera(camera) != 0;
            if (drawn_has_camera)
                std::copy(camera, camera + 16, drawn_camera.begin());
        }
        if (tracing()) { // what to name in <model>.motions.txt, when it changes
            static std::string said;
            std::string now = active->name + ":";
            for (const Playing &p : playing)
                now += " " + (p.name.empty() ? std::string("(unnamed)") : p.name);
            if (now != said)
                khdays_log("models: %s", now.c_str());
            said = now;
        }
    }
}

extern "C" void khdays_models_draw_animation(unsigned animation)
{
    next_animation = animation;
}

extern "C" void khdays_models_node(unsigned node, int sent)
{
    if (active == nullptr || !sent || node >= node_matrix.size()) {
        return;
    }
    float pos[16], vecm[16], proj[16];
    khdays_g3d_matrices(pos, vecm, proj);
    std::copy(pos, pos + 16, node_matrix[node].begin());
    node_sent[node] = 1;
}

extern "C" void khdays_models_node_placed(unsigned node, int sent)
{
    if (active == nullptr || !active->own || !sent || static_cast<int>(node) != active->own->game_weapon_node)
        return;
    const OwnSkeleton &o = *active->own;
    if (o.weapon_bone < 0 || static_cast<std::size_t>(o.weapon_bone) >= o.world_now.size())
        return;
    // the model's place: its first node drawn so far, as the joints' (draw)
    int first = -1;
    for (std::size_t g = 0; g < node_sent.size() && first < 0; ++g) {
        if (node_sent[g])
            first = static_cast<int>(g);
    }
    if (first < 0)
        return;
    Mat s = kIdentity;
    s[0] = s[5] = s[10] = active->scale;
    const Mat root =
        mul(node_matrix[static_cast<std::size_t>(first)], active->node_rest_inverse[static_cast<std::size_t>(first)]);
    const Mat hand = mul(mul(root, s), mul(o.world_now[static_cast<std::size_t>(o.weapon_bone)], o.weapon_fix));
    // MTX_MODE 2 (the position and vector matrices), MTX_LOAD_4x4: the DS's rows are these columns
    const uint32_t mode = 2;
    khdays_g3d_command(0x10, &mode);
    uint32_t words[16];
    for (int i = 0; i < 16; ++i)
        words[i] =
            static_cast<uint32_t>(static_cast<int32_t>(std::lround(hand[static_cast<std::size_t>(i)] * 4096.0F)));
    khdays_g3d_command(0x16, words);
}

// Ov022_EnterActorState (through controls_hooks.c's khdays_controls_actor_entered_state): an actor's state as it entered it,
// by its scene node (actor +0x20, whose render object is at +0x24 as the ground's link has it), when it entered it,
// and when it last left state 5 (damage in the air, a launch included: Ov022_ResolveGuardBreakState)
extern "C" void khdays_models_actor_state(int actor, int state)
{
    const auto a = static_cast<KhdaysRef>(actor);
    if (a == 0 || !khdays_game_ram(a + 0x20) || !khdays_game_ram(a + 0x23))
        return;
    KhdaysRef node = 0;
    std::memcpy(&node, KHDAYS_W(a + 0x20), 4);
    if (node == 0)
        return;
    if (actor_states.size() > 64)
        actor_states.clear();
    ActorState &s = actor_states[node];
    const u32 now = khdays_events_vblank_count();
    if (s.state == kAirDamageState && state != kAirDamageState)
        s.left_air = now;
    s.state = state;
    s.entered = now;
}

extern "C" void khdays_models_actor_ground(void *mover)
{
    KhdaysRef owner = 0;
    std::memcpy(&owner, mover, 4);
    if (owner == 0)
        return;
    if (grounds.size() > 256)
        grounds.clear();
    grounds[owner] = {KHDAYS_REF(mover), khdays_events_vblank_count()};
}

// NNS_G3dGetResultMtx (prepare.py HOOKS): a replaced model's node as the model's joint carries it, in the matrices the
// game reads back after the draw (MtxFx43 position, MtxFx33 direction; either may be null); 0 when not replaced
extern "C" int khdays_models_result_matrix(const void *render_obj, unsigned node, s32 *position, s32 *direction)
{
    const auto it = node_results.find(render_obj);
    if (it == node_results.end() || node >= it->second.size() || !it->second[node].first)
        return 0;
    const Mat &mtx = it->second[node].second;
    if (position != nullptr)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r)
                position[c * 3 + r] = static_cast<s32>(std::lround(mtx[static_cast<std::size_t>(c * 4 + r)] * 4096.0F));
    if (direction != nullptr)
        for (int c = 0; c < 3; ++c) {
            const float l = length({mtx[static_cast<std::size_t>(c * 4)], mtx[static_cast<std::size_t>(c * 4 + 1)],
                                    mtx[static_cast<std::size_t>(c * 4 + 2)]});
            for (int r = 0; r < 3; ++r)
                direction[c * 3 + r] = static_cast<s32>(
                    std::lround(l > 0.0F ? mtx[static_cast<std::size_t>(c * 4 + r)] / l * 4096.0F : 0.0F));
        }
    return 1;
}

extern "C" void khdays_models_joint_saved(void *matrix, unsigned node)
{
    if (active != nullptr && depth == 1 && node < active->kh2_weapon.size() && active->kh2_weapon[node] >= 0)
        saved_joints.emplace_back(matrix, node);
}

// The same, the matrix given by its address in the game's memory (the camp menus' model: Ov025/Ov008
// RefreshMatchingMatrices keep it in their object, reached through plain addresses)
extern "C" void khdays_models_joint_saved_at(unsigned matrix, unsigned node)
{
    if (khdays_game_ram(matrix) && khdays_game_ram(matrix + 0x2fU))
        khdays_models_joint_saved(KHDAYS_W(matrix), node);
}

extern "C" int khdays_models_shape(const void *list, unsigned int size)
{
    if (active == nullptr) {
        return 0;
    }
    if (shapes++ == 0) {
        khdays_g3d_material(&material);
    }
    // a shape of a node the mod's model keeps: the game's own, sent
    if (!active->kept_lists.empty() && model_start != nullptr && list != nullptr &&
        active->kept_lists.count(static_cast<std::uint32_t>(static_cast<const unsigned char *>(list) - model_start)) !=
            0) {
        return 0;
    }
    if (khdays_shadow_sprite_active())
        return 0; // retain the original flat shadow until its animated replacement is ready
    // the list's matrix commands run, its drawing left out: packed words of up
    // to four command numbers, each command's parameters after them in order
    const auto *words = static_cast<const uint32_t *>(list);
    const unsigned int count = list != nullptr ? size / 4U : 0U;
    for (unsigned int i = 0; i < count;) {
        const uint32_t packed = words[i++];
        for (int b = 0; b < 4; ++b) {
            const unsigned command = (packed >> (8 * b)) & 0xffU;
            const int n = khdays_g3d_param_count(command);
            if (n < 0)
                continue; // no command (0: a free slot of the word)
            if (i + static_cast<unsigned int>(n) > count)
                return 1;
            if (command >= 0x10U && command <= 0x1cU)
                khdays_g3d_command(command, &words[i]);
            i += static_cast<unsigned int>(n);
        }
    }
    return 1;
}

extern "C" void khdays_models_dialogue_speaker(int portrait)
{
    dialogue_speaker = portrait;
    dialogue_typing = false;
}

extern "C" void khdays_models_dialogue_typed(void)
{
    dialogue_typed = khdays_events_vblank_count();
    dialogue_typing = true;
}

extern "C" void khdays_models_draw_end(void)
{
    if (depth <= 0) {
        return;
    }
    if (--depth != 0) {
        active = depth < 8 ? outer[depth] : nullptr;
        return;
    }
    khdays_walk_model_draw_end();
    // its polygons inside the game model's draw: keyed by the draw for the
    // pictures between lists (g3d_interp.c), not among the loose ones, whose
    // number moves with every effect's
    if (active != nullptr && shapes > 0) {
        draw(*active);
    } else if (active == nullptr) {
        node_results.erase(begun_object); /* drawn as the game's: what is attached to it reads the game's nodes */
    }
    khdays_g3d_draw_end();
    active = nullptr;
}

namespace {
// the models drawn at places of the port's, by their file; null: one that cannot be read
std::unordered_map<std::string, std::unique_ptr<Model>> placed_models;

Model *placed(const char *path)
{
    auto it = placed_models.find(path);
    if (it == placed_models.end()) {
        auto model = load(path, path, nullptr, 0);
        if (!model) {
            khdays_log("models: %s cannot be drawn at its place; the message above says why it cannot be read", path);
        } else if (tracing()) {
            std::fprintf(stderr, "models: %s drawn at its place (%zu meshes)\n", path, model->meshes.size());
        }
        it = placed_models.emplace(path, std::move(model)).first;
    }
    return it->second.get();
}
} // namespace

extern "C" int khdays_models_placed_bounds(const char *path, float low[3], float high[3])
{
    const Model *m = placed(path);
    if (m == nullptr || m->meshes.empty())
        return 0;
    for (std::size_t c = 0; c < 3; ++c) {
        low[c] = m->low[c];
        high[c] = m->high[c];
    }
    return 1;
}

extern "C" int khdays_models_draw_placed(const char *path, const float place[16], unsigned int attr)
{
    Model *m = placed(path);
    if (m == nullptr || active != nullptr)
        return 0;
    // draw()'s state: its one node at the place, a material that lights nothing (its own colours), no render object and
    // nothing of the last game model's draw
    drawn_object = nullptr;
    playing.clear();
    materials_playing.clear();
    saved_joints.clear();
    node_matrix.assign(1, kIdentity);
    std::copy(place, place + 16, node_matrix[0].begin());
    node_sent.assign(1, 1);
    material = {};
    material.attr = attr & ~0xfu;
    material.color[0] = material.color[1] = material.color[2] = 31;
    draw(*m);
    return 1;
}
