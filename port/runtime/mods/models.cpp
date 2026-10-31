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
#include "names.h"
#include "room_animation.h"
#include "../characters/characters.h"
#include "secondary/model_link.h"

#include "khdays/assets/fbx.h"
#include "khdays/assets/gltf.h"
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
    bool room = false; // a room of a world its own mod replaces: drawn with HD graphics off too
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
    std::map<std::string, std::vector<Kh2Section>> kh2_sections;
    bool kh2_root_motion = false;
    bool kh2_ik_adjust = true; // the feet on the game's floor (IKAdjust)
    std::vector<int> kh2_bone;
    std::vector<int> kh2_weapon;      // per game node: the joint what the game attaches to it is put on (-1 none)
    std::vector<Mat> kh2_weapon_turn; // per game node: `weapon ... rotate`, about that joint's own axes
    // motions.txt's `ds_pose`: while the game's own animations pose it, unpaired joints turned from rest about
    // their heads; per game animation ("" every other), each joint and its turn (model space, at rest)
    std::map<std::string, std::vector<std::pair<std::size_t, Mat>>> ds_turns;
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
std::map<u32, std::string> animation_names; // by the J0AC's DS address
std::map<const void *, std::vector<std::pair<bool, Mat>>> node_results; // by render object: draw()'s per node
const void *drawn_object = nullptr; // the render object being drawn: which actor's model
Mat drawn_start = kIdentity;        // the position matrix as the draw starts: the view times the actor's world matrix
// the latest flush's base transform (khdays_g3d_base) and camera as the draw starts, and the base's kind
Mat drawn_base = kIdentity, drawn_camera = kIdentity;
int drawn_base_kind = 0;
bool drawn_has_camera = false;

// A section's KH2 motion as an actor plays it: its player (the pose it keeps) and its clock in KH2's
// ticks (60 a second).
struct Kh2Track {
    std::unique_ptr<khdays::assets::kh2::MotionPlayer> player;
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
    std::array<float, 3> fade_offset{}, shown_offset{}; // the fading pose's `offset`, and the one last shown
    float fade_ticks = 0.0F, fade_left = 0.0F, fade_frames = 0.0F;
    float looped_at = -1.0F; // a loop's fade: the frame the game set back from (-1 none)
    float last_frame = 0.0F;
    float step = 1.0F;
    std::uint64_t seen = 0;
    std::map<int, Kh2Track> tracks; // by section
};
struct Kh2Actor {
    std::uint64_t draws = 0;
    std::map<std::string, Kh2Animation> animations; // by game animation
};
std::map<const void *, Kh2Actor> kh2_actors; // by render object
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

// What `<model>.bones.txt` beside the model's file says (.gltf, .glb, .fbx or .mdlx: its name without its extension).
struct BonesFile {
    std::map<std::string, std::string> pairs;
    float scale = 1.0F;
    int cull = 0;
    std::vector<std::string> keep;  // the game's nodes whose own shapes are still drawn
    std::string setting;            // the HD replacement switch it follows
    float pose_degrees = kPoseDegrees; // the rest pose correction's threshold
    std::vector<LimbLine> limbs; // its two-bone limbs (`ik <upper> <lower> <end>`)
    std::vector<MeshScale> mesh_scales; // its joints' mesh scales
    std::map<std::string, std::string> aligns;
    std::map<std::string, std::array<float, 3>> places;
    std::array<std::string, 3> spine;
    std::vector<HideLine> hides;
};

void read_bones(const std::string &model_file, BonesFile &bones)
{
    auto &pairs = bones.pairs;
    auto &scale = bones.scale;
    auto &cull = bones.cull;
    auto &pose_degrees = bones.pose_degrees;
    auto &limbs = bones.limbs;
    auto &mesh_scales = bones.mesh_scales;
    auto &aligns = bones.aligns;
    auto &places = bones.places;
    auto &spine = bones.spine;
    auto &hides = bones.hides;
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
            scale = std::strtof(b.c_str(), nullptr);
            if (!(scale > 0.0F))
                scale = 1.0F;
        } else if (a == "keep") {
            for (std::string node = b; !node.empty(); node.clear(), words >> node)
                bones.keep.push_back(node);
        } else if (a == "switch") {
            bones.setting = b;
        } else if (a == "cull") {
            if (b == "back") {
                cull = 1;
            } else if (b == "none") {
                cull = 2;
            } else {
                std::fprintf(stderr, "models: %s: cull %s is neither back nor none; the line is ignored\n",
                             path.c_str(), b.c_str());
            }
        } else if (a == "pose_degrees") {
            char *end = nullptr;
            const float degrees = std::strtof(b.c_str(), &end);
            if (end != b.c_str() && *end == '\0' && degrees >= 0.0F) {
                pose_degrees = degrees;
            } else {
                std::fprintf(stderr, "models: %s: pose_degrees %s is not a number of degrees; the line is ignored\n",
                             path.c_str(), b.c_str());
            }
        } else if (a == "mesh_scale") {
            MeshScale s{b, {}};
            if (words >> s.by[0] >> s.by[1] >> s.by[2] && s.by[0] >= 0.0F && s.by[1] >= 0.0F && s.by[2] >= 0.0F) {
                mesh_scales.push_back(s);
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
                limbs.push_back(limb);
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
                places[b] = at;
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
                hides.push_back(std::move(h));
            } else {
                std::fprintf(stderr,
                             "models: %s: write `hide <joint>... from <top> to <bottom> [fade <length>]` (the model's "
                             "own centimetres at rest, the top above the bottom); the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "spine") {
            std::string end, word, hips;
            if (words >> end && (!(words >> word) || (word == "hips" && words >> hips))) {
                spine = {b, end, hips};
            } else {
                std::fprintf(stderr,
                             "models: %s: write `spine <base joint> <end joint> [hips <joint>]`; the line is "
                             "ignored\n",
                             path.c_str());
            }
        } else if (a == "align") {
            std::string kind;
            if (words >> kind && (kind == "foot" || kind == "hand")) {
                aligns[b] = kind;
            } else {
                std::fprintf(stderr, "models: %s: write `align <joint> foot|hand`; the line is ignored\n",
                             path.c_str());
            }
        } else {
            pairs[a] = b;
        }
    }
}

// The HD replacement switch (hud_settings.h) a bones.txt's `switch` names; -1 none (drawn always).
int setting_named(const std::string &name, const std::string &key)
{
    if (key.empty())
        return -1;
    for (int s = 0; s < KHDAYS_OPT_COUNT; ++s)
        if (key == khdays_hud_setting_keys[s])
            return s;
    std::fprintf(stderr, "models: %s: switch %s is no HD replacement's; drawn always\n", name.c_str(), key.c_str());
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
            std::fprintf(stderr, "models: %s: no node %s to keep\n", name.c_str(), node.c_str());
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
// A KH2 model's own motions: `<model>.motions.txt` beside the .mdlx, a line each -- `mset <file>` (beside
// it), `root_motion on|off` (off: the game places the model, KH2's root motion taken out of the bones as
// KH2 takes it out; on: drawn as the motion moves it) and `<game animation>[@<frame>] <mset slot>
// [blend <frames>]`, a section of the animation from that frame on; `skeleton <mdlx>` with `bone` and `ik`
// lines when the mset was made for another model. A game animation without a line, or a slot with no
// motion, keeps the game's (docs/KH2_MODELS.md).
// A motions.txt `ds_pose` line: bones turned from rest as a slot's pose turns them (its frame, 30 a second),
// or by degrees about each bone's own x, then y, then z; for the game animations named, else all
struct DsPoseRule {
    std::vector<int> bones;
    int slot = -1;
    float frame = 0.0F;
    std::array<float, 3> degrees{};
    std::vector<std::string> animations;
};

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
        kh2::MotionPlayer player(*model.kh2_skeleton);
        player.evaluate(*motion, frame * 2.0F, kIdentity, nullptr);
        std::vector<kh2::Mat4> &out = poses[key];
        if (model.kh2_retarget)
            model.kh2_retarget->apply(player.joints(), out);
        else
            out = player.joints();
        return &out;
    };
    const auto turn_only = [](Mat m) { // its rotation: the axes made unit, no translation
        for (std::size_t c = 0; c < 3; ++c) {
            const float l = length({m[c * 4], m[c * 4 + 1], m[c * 4 + 2]});
            for (std::size_t r = 0; r < 3 && l > 0.0F; ++r)
                m[c * 4 + r] /= l;
        }
        m[12] = m[13] = m[14] = 0.0F;
        return m;
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
                    say(bone + ": the model has no such bone; the line leaves it");
                continue;
            }
            const auto j = static_cast<std::size_t>(it - model.kh2_bone.begin());
            const Joint &joint = model.joints[j];
            if (joint.node >= 0 || joint.parent < 0) {
                if (told.insert(bone).second)
                    say(bone +
                        (joint.node >= 0 ? ": it follows a game node (bones.txt), which poses it"
                                         : ": it has no parent to turn from") +
                        "; the line leaves it");
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
                            " gives it no pose (no such motion, or no mset); the line leaves it");
                    continue;
                }
                // its turn from its parent in the pose, against the same at rest, in its own axes
                const Mat posed = mul(inverse(turn_only((*g)[static_cast<std::size_t>(pb)])),
                                      turn_only((*g)[static_cast<std::size_t>(b)]));
                const Mat rest = mul(inverse(turn_only(inverse(model.joints[p].inverse_bind))),
                                     turn_only(inverse(joint.inverse_bind)));
                turn = mul(inverse(rest), posed);
            }
            // about its head, in the space the joints' K take (the model at rest): B turn B^-1
            const Mat frame = mul(joint.bind, inverse(joint.inverse_bind));
            turns.emplace_back(j, mul(frame, mul(turn, inverse(frame))));
        }
    }
}

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
    std::string skeleton;                   // the mdlx the mset was made for, when not this one
    std::vector<std::pair<int, int>> bones; // this model's bone, the skeleton's
    std::vector<khdays::assets::kh2::Retarget::Limb> limbs;
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
        if (a == "mset") {
            mset = b;
        } else if (a == "root_motion") {
            model.kh2_root_motion = b == "on";
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
                    say("\"" + line + "\": the frame after @ is not a number; the line is ignored");
                    continue;
                }
                a.erase(at);
            }
            char *end = nullptr;
            section.slot = std::strtoul(b.c_str(), &end, 10);
            if (end == b.c_str() || *end != '\0') {
                say("\"" + line + "\": the slot is not a number; the line is ignored");
                continue;
            }
            std::string c, d;
            bool bad = false, to = false;
            while (!bad && words >> c) {
                if (c == "follow")
                    section.follow = true;
                else if (c == "start_loop")
                    section.start_loop = true;
                else if (c == "loop_blend")
                    bad = !(words >> d && number(d, section.loop_blend));
                else if (c == "offset") {
                    bad = !(words >> section.offset[0] >> section.offset[1] >> section.offset[2]);
                    section.offset_to = section.offset;
                } else if (c == "to")
                    bad = !(words >> section.offset_to[0] >> section.offset_to[1] >> section.offset_to[2]) || to;
                else
                    bad = !(c == "blend" && words >> d && number(d, section.blend));
                to = to || c == "to";
            }
            if (bad) {
                say("\"" + line +
                    "\": after the slot write `blend <frames>`, `loop_blend <frames>`, `follow`, `start_loop` or "
                    "`offset <x> <y> <z> [to <x> <y> <z>]` (centimetres); the line is ignored");
                continue;
            }
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
                "; the game's place is kept");
            continue;
        }
        model.kh2_weapon.resize(model.node_names.size(), -1);
        model.kh2_weapon_turn.resize(model.node_names.size(), kIdentity);
        model.kh2_weapon[static_cast<std::size_t>(g - model.node_names.begin())] =
            static_cast<int>(j - model.kh2_bone.begin());
        model.kh2_weapon_turn[static_cast<std::size_t>(g - model.node_names.begin())] = turn_of(weapon_degrees[node]);
    }
    if (mset.empty()) {
        if (!sections.empty())
            say("motions.txt names no mset (a line `mset <file>.mset`); the game's animations are played");
        resolve_ds_poses(model, ds_rules, say);
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
        const kh2::MotionSkeleton own = kh2::read_motion_skeleton(mdlx.data(), mdlx.size());
        if (skeleton.empty()) {
            model.kh2_skeleton = std::make_shared<kh2::MotionSkeleton>(own);
        } else { // the mset plays on its own model's bones, given to this one's
            const auto other = read(file.parent_path() / u8(skeleton));
            model.kh2_skeleton =
                std::make_shared<kh2::MotionSkeleton>(kh2::read_motion_skeleton(other.data(), other.size()));
            std::vector<int> source_of(own.joints.size(), -1);
            for (const auto &[t, s] : bones) {
                if (static_cast<std::size_t>(t) >= source_of.size())
                    throw std::runtime_error("bone " + std::to_string(t) + ": the model has " +
                                             std::to_string(own.joints.size()) + " bones");
                source_of[static_cast<std::size_t>(t)] = s;
            }
            model.kh2_retarget = std::make_shared<kh2::Retarget>(*model.kh2_skeleton, own, source_of, limbs);
        }
        model.kh2_motions = std::make_shared<kh2::MotionSet>(set.data(), set.size());
    } catch (const std::exception &e) {
        say(std::string("its motions cannot be read (") + e.what() + "); the game's animations are played");
        model.kh2_motions.reset();
        model.kh2_retarget.reset();
        resolve_ds_poses(model, ds_rules, say);
        return;
    }
    for (auto &[name, parts] : sections) {
        std::erase_if(parts, [&](const Kh2Section &s) {
            if (model.kh2_motions->motion(s.slot) != nullptr)
                return false;
            say(name + ": mset slot " + std::to_string(s.slot) + " holds no motion it plays; the game's is kept");
            return true;
        });
        if (parts.empty())
            continue;
        std::sort(parts.begin(), parts.end(), [](const Kh2Section &x, const Kh2Section &y) { return x.from < y.from; });
        if (parts.front().from > 0.0F)
            say(name + ": no section from frame 0; the game's animation plays before frame " +
                std::to_string(parts.front().from));
        model.kh2_sections[name] = std::move(parts);
    }
    resolve_ds_poses(model, ds_rules, say);
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
            std::fprintf(stderr, "models: %s: the game's model cannot be read (%s): the game's is drawn\n",
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
        std::fprintf(stderr, "models: %s cannot be read (%s): the game's is drawn\n", path.c_str(), e.what());
        khdays_log("models: %s cannot be read (%s): the game's is drawn", path.c_str(), e.what());
        return nullptr;
    }
    for (const auto &[texture, why] : mod.missing_textures) {
        std::fprintf(stderr, "models: %s: texture %s not found or not readable (%s); drawn white\n", path.c_str(),
                     texture.c_str(), why.c_str());
        khdays_log("models: %s: texture %s not found or not readable (%s); drawn white", path.c_str(), texture.c_str(),
                   why.c_str());
    }
    // a mod's character with a skeleton of its own: none of its joints follows a node of the game's
    const KhdaysCharacter *character = khdays_characters_of_model(name.c_str());
    const bool own_skeleton = character != nullptr && character->own_skeleton;
    if (!own_skeleton && (u8(path).extension() == ".fbx" || u8(path).extension() == ".FBX")) {
        mod.animations.clear(); /* an FBX model's takes are not its own animation */
    }
    BonesFile bones;
    read_bones(path, bones);
    const auto &pairs = bones.pairs;
    model->scale = bones.scale;
    model->cull = bones.cull;
    model->pose_degrees = bones.pose_degrees;
    const auto &limbs = bones.limbs;
    const auto &mesh_scales = bones.mesh_scales;
    const auto &aligns = bones.aligns;
    const auto &places = bones.places;
    const auto &spine_line = bones.spine;
    const auto &hides = bones.hides;
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
        const auto pair = pairs.find(in.name);
        const std::string &wanted = pair != pairs.end() ? pair->second : in.name;
        for (std::size_t g = 0; g < game.object_names.size() && !own_skeleton; ++g) {
            if (game.object_names[g] == wanted)
                out.node = static_cast<int>(g);
        }
        if (pair != pairs.end() && out.node < 0) {
            std::fprintf(stderr, "models: %s: no node %s for joint %s\n", name.c_str(), wanted.c_str(),
                         in.name.c_str());
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
        if (const auto al = aligns.find(joint.name); al != aligns.end()) {
            std::vector<std::array<float, 3>> own, its;
            const Mat &wg = rest[static_cast<std::size_t>(joint.node)];
            for (const auto &mesh : mod.model.meshes)
                for (const auto &v : mesh.vertices) {
                    std::size_t heaviest = 0;
                    for (std::size_t q = 1; q < 4; ++q)
                        if (v.weights[q] > v.weights[heaviest])
                            heaviest = q;
                    const auto it = joint_of_palette.find(v.joints[heaviest]);
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
    for (const auto &line : limbs) {
        const auto &names = line.joints;
        std::array<int, 3> limb{-1, -1, -1};
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < model->joints.size(); ++j) {
                if (model->joints[j].name == names[i])
                    limb[i] = static_cast<int>(j);
            }
        }
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
        const auto turn_only = [](Mat m) {
            for (std::size_t c = 0; c < 3; ++c) {
                const float l = length({m[c * 4], m[c * 4 + 1], m[c * 4 + 2]});
                for (std::size_t r = 0; r < 3 && l > 0.0F; ++r)
                    m[c * 4 + r] /= l;
            }
            m[12] = m[13] = m[14] = 0.0F;
            return m;
        };
        // the joint's frame at rest as the game's pose turns it (its correction), against its node's
        const Mat frame = turn_only(mul(joint.correction, mul(joint.bind, inverse(joint.inverse_bind))));
        joint.to_node = mul(inverse(frame), turn_only(rest[static_cast<std::size_t>(joint.node)]));
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
    for (const auto &[joint_name, by] : places) {
        std::size_t j = 0;
        while (j < model->joints.size() && model->joints[j].name != joint_name)
            ++j;
        if (j == model->joints.size() || model->joints[j].node < 0) {
            khdays_log("models: %s: place %s: no paired joint of that name; the line is ignored", name.c_str(),
                       joint_name.c_str());
            continue;
        }
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
        const auto named = [&](const std::string &n) {
            for (std::size_t j = 0; j < model->joints.size(); ++j)
                if (model->joints[j].name == n)
                    return static_cast<int>(j);
            return -1;
        };
        Spine sp;
        const int base = named(spine_line[0]);
        sp.end = named(spine_line[1]);
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
            sp.hips = named(spine_line[2]);
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
            khdays_log("models: %s: spine %s %s: not a chain to a paired joint; the line is ignored", name.c_str(),
                       spine_line[0].c_str(), spine_line[1].c_str());
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
    for (const MeshScale &s : mesh_scales) {
        std::size_t j = 0;
        while (j < model->joints.size() && model->joints[j].name != s.joint)
            ++j;
        if (j == model->joints.size()) {
            std::fprintf(stderr, "models: %s: mesh_scale %s: no such joint; the line is ignored\n", name.c_str(),
                         s.joint.c_str());
            khdays_log("models: %s: mesh_scale %s: no such joint; the line is ignored", name.c_str(), s.joint.c_str());
            continue;
        }
        Mat by = kIdentity; // 0 as a thousandth: its normals stay finite
        by[0] = std::fmax(s.by[0], 1e-3F);
        by[5] = std::fmax(s.by[1], 1e-3F);
        by[10] = std::fmax(s.by[2], 1e-3F);
        const Mat &ib = model->joints[j].inverse_bind;
        model->mesh_scales.emplace_back(j, mul(inverse(ib), mul(by, ib)));
    }

    // the meshes and their textures, each texture as its material takes its alpha (glTF alphaMode): kept;
    // all opaque for an opaque material; for a mask each texel drawn or not by the cutoff, opaque (a whole
    // body with soft cut-out edges came out seen through itself); where the file does not say (FBX),
    // translucent when any texel or vertex colour is part way (a room's shading in vertex colours black at
    // alpha 0.05 came out as a black floor)
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
    for (const HideLine &h : hides) {
        std::vector<std::uint8_t> joint_hidden(model->joints.size(), 0);
        for (const auto &want : h.joints) {
            std::size_t j = 0;
            while (j < model->joints.size() && model->joints[j].name != want)
                ++j;
            if (j == model->joints.size()) {
                khdays_log("models: %s: hide %s: no such joint; it is left out", name.c_str(), want.c_str());
                continue;
            }
            joint_hidden[j] = 1;
        }
        const float top = h.top * 0.01F, bottom = h.bottom * 0.01F, fade = h.fade * 0.01F;
        std::vector<Mesh> faded_meshes;
        std::size_t dropped = 0, faded = 0;
        for (Mesh &mesh : model->meshes) {
            // each vertex's alpha: 0 hidden, 1 shown, between in the fade below the bottom
            std::vector<float> alpha(mesh.vertices.size(), 1.0F);
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
                const auto &vx = mesh.vertices[v];
                std::size_t most = 0;
                for (std::size_t w = 1; w < 4; ++w)
                    if (vx.weights[w] > vx.weights[most])
                        most = w;
                const std::uint32_t j = vx.joints[most];
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
                // a corner hidden: none of it, but at the bottom edge, where it fades from clear (the top edge
                // cut: a faded hip showed through the coat)
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
    if (!mod.animations.empty()) {
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
                    khdays_log("models: %s: animation %s moves a node no skinned mesh follows: not played",
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
                Mat turn = kIdentity;
                for (int axis = 0; axis < 3; ++axis) {
                    const float a = character->weapon_turn[axis] * 0.0174532925F;
                    const float c = std::cos(a), sn = std::sin(a);
                    Mat r = kIdentity;
                    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                    r[static_cast<std::size_t>(u * 4 + u)] = c;
                    r[static_cast<std::size_t>(u * 4 + v)] = sn;
                    r[static_cast<std::size_t>(v * 4 + u)] = -sn;
                    r[static_cast<std::size_t>(v * 4 + v)] = c;
                    turn = mul(r, turn);
                }
                o.weapon_fix = mul(inverse(s), turn);
            } else {
                // held as the base holds it: turned from the bone's axes at rest to the base node's at rest (the
                // weapon at the bone itself: the two rests' arms are not where each other's are)
                Mat turn = mul(inverse(at_rest[static_cast<std::size_t>(o.weapon_bone)]),
                               rest[static_cast<std::size_t>(o.game_weapon_node)]);
                for (int c = 0; c < 3; ++c) {
                    const float l =
                        length({turn[static_cast<std::size_t>(c * 4)], turn[static_cast<std::size_t>(c * 4 + 1)],
                                turn[static_cast<std::size_t>(c * 4 + 2)]});
                    for (int r = 0; r < 3 && l > 1e-6F; ++r)
                        turn[static_cast<std::size_t>(c * 4 + r)] /= l;
                }
                turn[12] = turn[13] = turn[14] = 0.0F;
                o.weapon_fix = mul(inverse(s), turn);
            }
        } else if (character->weapon_bone != nullptr) {
            khdays_log("characters: %s: %s not found: the weapon stays where the base's hand is", character->identity,
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

// The movers Mover_ResolveFloor put on their floor, by owner (their render object is at owner + 0x24: the
// animation state at +4, func_ov022_020a3c78, its NNSG3dRenderObj at +0x20): the mover and the vblank
// it was last seen at.
struct Grounded {
    KhdaysRef mover = 0;
    u32 vblank = 0;
};
std::unordered_map<KhdaysRef, Grounded> grounds;

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
    if (!m.kh2_ik_adjust || m.kh2_skeleton == nullptr || m.kh2_skeleton->feet.empty() || drawn_object == nullptr ||
        !khdays_g3d_camera(camera)) {
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
            khdays_log("characters: %s: %s has no animation", o.character->identity, relative.c_str());
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
                khdays_log("characters: %s: %s has no animation named %s", o.character->identity,
                           relative.substr(0, mark).c_str(), wanted.c_str());
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
        khdays_log("characters: %s: %s cannot be read (%s)", o.character->identity, relative.c_str(), e.what());
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

GameAnimation game_animation(u32 anm)
{
    static std::map<u32, std::string> names; // by resource, as the game keeps them loaded
    GameAnimation out;
    if (anm == 0 || !khdays_game_ram(anm))
        return out;
    const auto frame = *static_cast<const s32 *>(KHDAYS_W(anm));
    const u32 resource = *static_cast<const u32 *>(KHDAYS_W(anm + 8));
    if (!khdays_game_ram(resource))
        return out;
    const u16 frames = *static_cast<const u16 *>(KHDAYS_W(resource + 4));
    auto it = names.find(resource);
    if (it == names.end()) {
        const char *file = nullptr;
        u32 offset = 0, run = 0;
        char name[64] = "";
        if (!khdays_origin_of_ram(resource, &file, &offset, &run) ||
            !khdays_names_animation(file, offset, name, static_cast<int>(sizeof(name))))
            std::snprintf(name, sizeof(name), "@%08x", resource);
        it = names.emplace(resource, name).first;
    }
    out.name = it->second;
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

// The own skeleton's nodes' places for this draw: the clip given to the animation the game plays, at its own speed
// from the point the game begins its animation at, while the game's animation moves or is held at its end (not in a
// hit's stop or the pause), past its end as its action's `loop` says (by default looped once the game's is seen to
// loop), and from that point again when the game begins its animation again; or with `time game` at the same point
// of the game's (stretched to the clip's length); a native plugin's clip instead while it plays one (kh.characters'
// play: from when it began, at its own speed); blended from the pose shown when the animation changes or begins
// again; its root's travel the game's (unless the action keeps the clip's).
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
                    khdays_log("characters: %s: the game loops %s (%.0f frames)", o.character->identity,
                               g.name.c_str(), static_cast<double>(g.frames));
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

// A KH2 model whose game animation has a KH2 motion: its joints posed by the motion (on `base`, the
// game's root node, scaled), each motion of the game's cross-fade at the game's weight and the
// game's own pose (skin, from its nodes) at the rest of it. A section's clock starts when the game's
// frame enters it and moves as that frame moves (its pauses and speeds kept, its loops counted on;
// a followed section's is the frame itself); past the motion's end it loops from its loop point
// (retFrame) or holds.
void play_kh2(Model &m, const Mat &base)
{
    namespace kh2 = khdays::assets::kh2;
    if (kh2_actors.size() > 64)
        kh2_actors.clear();
    Kh2Actor &actor = kh2_actors[drawn_object];
    ++actor.draws;
    struct Pose {
        float weight;
        const std::vector<kh2::Mat4> *joints; // the model's bones (retargeted when its mset is another's)
        const kh2::Motion *motion;
        float ticks;
        std::array<float, 3> offset; // KH2 centimetres
        FaceMotion face;
    };
    std::vector<Pose> poses;
    float total = 0.0F, game = 0.0F;
    const Floor ground = floor_of(m, base);
    for (const Playing &p : playing) {
        const float ratio = std::fmax(0.0F, std::fmin(1.0F, p.ratio));
        total += ratio;
        // its section at the game's frame: the last one starting at or before it
        const auto found = m.kh2_sections.find(p.name);
        int now = -1;
        if (found != m.kh2_sections.end())
            for (std::size_t s = 0; s < found->second.size() && found->second[s].from <= p.frame; ++s)
                now = static_cast<int>(s);
        if (now < 0) {
            game += ratio;
            continue;
        }
        const std::vector<Kh2Section> &sections = found->second;
        const Kh2Section &section = sections[static_cast<std::size_t>(now)];
        const kh2::Motion *motion = m.kh2_motions->motion(section.slot);
        Kh2Animation &a = actor.animations[p.name];
        const bool starts = a.seen + 1 != actor.draws;
        const bool enters = starts || now != a.section;
        const bool back = !starts && p.frame < a.last_frame; // the game set its frame back (a loop)
        float step = 0.0F;                                   // BCA frames since the last draw
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
            a.fade_motion = m.kh2_motions->motion(sections[static_cast<std::size_t>(a.section)].slot);
            a.fade_ticks = a.shown;
            a.fade_offset = a.shown_offset;
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
                if (name == p.name || other.seen + 1 < actor.draws || os == m.kh2_sections.end() ||
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
        } else if (enters) { // its clock from the section's first frame
            track.ticks = start + (p.frame - section.from) * 2.0F;
        } else {
            track.ticks += step * 2.0F; // BCA frames at 30 a second
        }
        a.section = now;
        float faded = 0.0F; // the weight of the pose fading out
        if (!a.fade.empty()) {
            a.fade_left -= step;
            if (a.fade_left > 0.0F && a.fade_motion != nullptr)
                faded = a.fade_left / a.fade_frames;
            else
                a.fade.clear();
        }
        if (!track.player)
            track.player = std::make_unique<kh2::MotionPlayer>(*m.kh2_skeleton);
        float ticks = track.ticks;
        const float end = motion->length_ticks(), from = motion->loop_ticks();
        if (ticks > end)
            ticks = from >= 0.0F && from < end ? from + std::fmod(ticks - from, end - from) : end;
        // IKAdjust's world: the model (KH2 centimetres, root motion taken out) to Days' world, y up
        const Mat taken = m.kh2_root_motion ? kIdentity : inverse(motion->root_motion(ticks));
        const Mat world = ground.on ? mul(ground.world, taken) : kIdentity;
        track.player->evaluate(*motion, ticks, world, ground.on ? &ground.ray : nullptr);
        if (m.kh2_retarget) // the mset's model's pose on this model's bones
            m.kh2_retarget->apply(track.player->joints(), track.joints);
        else
            track.joints = track.player->joints();
        a.shown = ticks;
        const FaceMotion face{m.kh2_motions->attributes(section.slot), ticks, enters ? -1.0F : track.face_ticks};
        track.face_ticks = ticks;
        // `offset`, gliding to its `to` over the motion's length
        const float along = motion->length_ticks() > 0.0F ? std::fmin(1.0F, ticks / motion->length_ticks()) : 0.0F;
        for (std::size_t i = 0; i < 3; ++i)
            a.shown_offset[i] = section.offset[i] + (section.offset_to[i] - section.offset[i]) * along;
        if (faded > 0.0F)
            poses.push_back({ratio * faded, &a.fade, a.fade_motion, a.fade_ticks, a.fade_offset, {}});
        poses.push_back({ratio * (1.0F - faded), &track.joints, motion, ticks, a.shown_offset, face});
    }
    if (poses.empty())
        return;
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
    // each pose's joints in metres (KH2's centimetres), the root motion taken out unless it is kept
    std::vector<std::vector<Mat>> placed;
    for (const Pose &pose : poses) {
        const Mat taken = m.kh2_root_motion ? kIdentity : inverse(pose.motion->root_motion(pose.ticks));
        std::vector<Mat> joints;
        for (const auto &g : *pose.joints) {
            Mat w = mul(taken, g);
            w[12] = (w[12] + pose.offset[0]) * 0.01F;
            w[13] = (w[13] + pose.offset[1]) * 0.01F;
            w[14] = (w[14] + pose.offset[2]) * 0.01F;
            joints.push_back(mul(base, w));
        }
        placed.push_back(std::move(joints));
    }
    for (std::size_t j = 0; j < m.joints.size() && j < m.kh2_bone.size(); ++j) {
        const int bone = m.kh2_bone[j];
        if (bone < 0)
            continue;
        Mat blended{};
        const float own = game / total;
        for (std::size_t i = 0; i < 16; ++i)
            blended[i] = skin[j][i] * own;
        for (std::size_t p = 0; p < poses.size(); ++p) {
            if (static_cast<std::size_t>(bone) >= placed[p].size())
                continue;
            const Mat s = mul(placed[p][static_cast<std::size_t>(bone)], m.joints[j].inverse_bind);
            const float wgt = poses[p].weight / total;
            for (std::size_t i = 0; i < 16; ++i)
                blended[i] += s[i] * wgt;
        }
        skin[j] = blended;
    }
}

// The face's frame (face.h), once a game frame: each block's slot held by what comes first -- its character's
// text typing (`face talk`), the game's pattern animation (`face ds`: its eyes closed) unless what it would cover
// is `face closed`, its speaker's last portrait (`face portrait`, held across boxes until the next of his), the
// KH2 motion's ranges (id 23) --, the motion's triggers passed (4 a default, 5 the file's back), then the frames
// since the last played. A block whose sprite changes is patched into each copy of its texture (KH2 sends the
// texture, then the sprites over it: kn::ModelSKL::setTexture)
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
    for (std::size_t j = 0; j < m.joints.size(); ++j) {
        const Joint &joint = m.joints[j];
        const Mat *w = moved_world(joint, moved);
        skin[j] = w != nullptr ? mul(scaled_root, mul(*w, joint.inverse_bind)) : mul(k[j], joint.bind);
    }
    // KH2 motions on the game's root node (its first: roxas_R, never animated), whatever bone 0 follows
    face_motion = {};
    if (m.kh2_motions && !m.kh2_sections.empty())
        play_kh2(m, scaled_root);
    play_face(m);
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
        for (const Playing &p : playing) {
            khdays::secondary::DrawAnimation a{p.name, -1, -1.0F, p.frame, p.frames, p.ratio};
            if (const auto s = m.kh2_sections.find(p.name); s != m.kh2_sections.end() && m.kh2_motions)
                for (const Kh2Section &section : s->second)
                    if (section.from <= p.frame) {
                        a.slot = static_cast<int>(section.slot);
                        a.section = section.from;
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
    // what the game attaches to a node (its weapons) on the joint a `weapon` line names, as it is drawn:
    // its saved matrix (MtxFx43, the same space as the nodes') written over
    for (const auto &[saved, node] : saved_joints) {
        if (node >= m.kh2_weapon.size() || m.kh2_weapon[node] < 0)
            continue;
        const auto j = static_cast<std::size_t>(m.kh2_weapon[node]);
        const Mat at = mul(joint_frame(j), node < m.kh2_weapon_turn.size() ? m.kh2_weapon_turn[node] : kIdentity);
        s32 fx[12];
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
    if (node_results.size() > 64)
        node_results.clear();
    auto &results = node_results[drawn_object];
    results.assign(nodes, {false, kIdentity});
    for (std::size_t g = 0; g < nodes && g < m.node_joint.size(); ++g) {
        if (g < m.kh2_weapon.size() && m.kh2_weapon[g] >= 0) {
            const auto j = static_cast<std::size_t>(m.kh2_weapon[g]);
            results[g] = {true, mul(joint_frame(j), g < m.kh2_weapon_turn.size() ? m.kh2_weapon_turn[g] : kIdentity)};
        } else if (m.node_joint[g] >= 0) {
            const auto j = static_cast<std::size_t>(m.node_joint[g]);
            results[g] = {true, mul(mul(joint_frame(j), m.joints[j].to_node), m.node_local[g])};
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
    // mod's room, one ID, came out as whatever it drew first (its sky over the rest). A material fading
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
// The name of the animation whose data the game has at `anm` (a J0AC, a PAT0's animation), "" unknown
const std::string &animation_name(u32 anm)
{
    auto known = animation_names.find(anm);
    if (known == animation_names.end()) {
        const char *file = nullptr;
        u32 offset = 0, run = 0;
        char name[64] = "";
        if (anm == 0 || !khdays_origin_of_ram(anm, &file, &offset, &run) ||
            !khdays_names_animation(file, offset, name, sizeof(name)))
            name[0] = '\0';
        known = animation_names.emplace(anm, name).first;
    }
    return known->second;
}

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
    if (depth == 0) {
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

// NNS_G3dGetResultMtx (prepare.py HOOKS): a replaced model's node as the model's joint carries it, in the
// matrices the game reads back after the draw (MtxFx43 position, MtxFx33 direction); 0 when not replaced
extern "C" int khdays_models_result_matrix(const void *render_obj, unsigned node, s32 *position, s32 *direction)
{
    const auto it = node_results.find(render_obj);
    if (it == node_results.end() || node >= it->second.size() || !it->second[node].first)
        return 0;
    const Mat &mtx = it->second[node].second;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r)
            position[c * 3 + r] = static_cast<s32>(std::lround(mtx[static_cast<std::size_t>(c * 4 + r)] * 4096.0F));
    for (int c = 0; c < 3; ++c) {
        const float l = length({mtx[static_cast<std::size_t>(c * 4)], mtx[static_cast<std::size_t>(c * 4 + 1)],
                                mtx[static_cast<std::size_t>(c * 4 + 2)]});
        for (int r = 0; r < 3; ++r)
            direction[c * 3 + r] =
                static_cast<s32>(std::lround(l > 0.0F ? mtx[static_cast<std::size_t>(c * 4 + r)] / l * 4096.0F : 0.0F));
    }
    return 1;
}

extern "C" void khdays_models_joint_saved(void *matrix, unsigned node)
{
    if (active != nullptr && depth == 1 && node < active->kh2_weapon.size() && active->kh2_weapon[node] >= 0)
        saved_joints.emplace_back(matrix, node);
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
            khdays_log("models: %s cannot be drawn at its place", path);
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
    // draw()'s state: its one node at the place, a material that lights nothing (its own colours)
    node_matrix.assign(1, kIdentity);
    std::copy(place, place + 16, node_matrix[0].begin());
    node_sent.assign(1, 1);
    material = {};
    material.attr = attr & ~0xfu;
    material.color[0] = material.color[1] = material.color[2] = 31;
    draw(*m);
    return 1;
}
