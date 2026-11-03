// A model's secondary motion cooked once at load (docs/SECONDARY_MOTION.md): the configuration
// resolved against the model's neutral skeleton and skinned mesh into particles bound to bone-local
// points, constraints, collision samples, colliders and the bones rebuilt from them. Immutable and
// shared by every actor drawing the model; nothing here knows KH2's formats or the game's memory.
#pragma once

#include "config.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace khdays::assets {
struct ImportedModel;
}

namespace khdays::secondary {

// The model's skeleton at rest: names, parents (before their children or not), rest world matrices
// in the model's units (metres).
struct SkeletonDesc {
    std::vector<std::string> names;
    std::vector<int> parent;
    std::vector<Mat4> rest;
};

// Its skinned triangles at rest, model space: each vertex's bones (skeleton indices, -1 none) and weights.
struct MeshDesc {
    struct Vertex {
        Vec3 position;
        std::array<int, 4> bones{-1, -1, -1, -1};
        std::array<float, 4> weights{};
    };
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> triangles; // 3 indices each
};

// The neutral skeleton and mesh of an imported model (glTF, FBX, KH2 .mdlx alike).
void describe(const assets::ImportedModel &model, SkeletonDesc &skeleton, MeshDesc &mesh);

// The resolved settings of one particle, with the group's mode and policy.
struct ParamBlock {
    std::array<float, kParamCount> v{};
    Mode mode = Mode::simulated;
    AuthoredPolicy policy = AuthoredPolicy::exact;
    float get(Param p) const
    {
        return v[static_cast<std::size_t>(p)];
    }
};

// A control's reference shape on its group's reference bone, riding up to three body bones (thighs)
// when they come up into it: its point and its bone's frame in each one's rest space; for each ridden
// bone the segment it is measured against (head to end, that bone's space), the control's distance from
// it at rest and the most the bone may carry (`follow_bone`'s weight).
struct ReferenceBlend {
    int count = 1;
    std::array<int, 4> bone{};
    std::array<float, 4> weight{};
    std::array<Vec3, 4> point{};
    std::array<Frame, 4> frame{};
    std::array<Vec3, 4> end{};
    std::array<float, 4> gap{};
    std::array<float, 4> always{}; // carried in every pose (follow_bone `always` by rest nearness)
};

struct Particle {
    int bone = -1; // a control: the bone whose point it is; an anchor: the bone it rides
    Vec3 offset;   // in that bone's space (its rest frame, model units)
    int group = 0;
    bool dynamic = false;
    float inv_mass = 0.0F;
    float depth = 0.0F;        // 0 at the anchor, 1 at the free end
    int params = 0;            // into Rig::params
    int anchor = -1;           // dynamic: the particle at its bone's head (the chain's previous point or an anchor)
    int root = -1;             // dynamic: its strip's anchor (tethers measure from it)
    int carrier = -1;          // an anchor riding a simulated bone: that bone's index in Rig::bones_out, else -1
    Vec3 reference;            // in the group's reference bone space (its first `attach`), model units
    int blend = -1;            // or carried by several bones: Rig::blends
    std::uint32_t collide = 0; // collider tag bits
    int self_group = -1;
    float thickness = 0.01F;
    int left = -1, right = -1; // row neighbours (surfaces), for a bone's twist
    float tether = 0.0F;       // rest path length from `root`
};

enum class Link : std::uint8_t { stretch, shear, bend, anchor, row }; // stretch: along a strip or chain
struct Distance {
    int a = 0, b = 0;
    float rest = 0.0F;
    Link kind = Link::stretch;
};

struct Face {
    int a = 0, b = 0, c = 0;
};

struct Collider {
    std::string name;
    ShapeKind kind = ShapeKind::sphere;
    int bone = -1, end_bone = -1;
    Vec3 offset, end_offset; // each in its bone's space; a capsule without an end bone: end = offset + x * length
    float radius = 0.0F, end_radius = 0.0F;
    std::uint32_t tags = 0;
    bool rest_side = true; // contact keeps the rest side, else the side a control came from
};

// A bone rebuilt from its particle: rotation swung from its reference onto the solved point, twist
// from its row neighbours, translation = point - R * S * offset.
struct SimBone {
    int bone = -1;
    int particle = -1;
    int head = -1;       // the particle at its head when it is an anchor of a chain (or -1: carried)
    int parent_sim = -1; // the nearest simulated ancestor, an index into bones_out (-1: none)
    int group = 0;
    Frame reference; // its rest (or settled) frame in its group's reference bone space
};

// A bone below a simulated one that is not simulated itself: carried by it as the animation places it.
struct Carried {
    int bone = -1;
    int by = -1; // index into bones_out
};

struct GroupRig {
    std::string name;
    GroupKind kind = GroupKind::chain;
    int reference_bone = -1;           // the first `attach` (hair: the tufts' common parent)
    int first = 0, last = 0;           // its particles
    int first_bone = 0, last_bone = 0; // its bones_out
    Wind wind;
    int wind_bone = -1; // a `wind bone <b>`'s bone
    bool settle = false;
    int settle_steps = 0;
    bool twist_from_neighbours = true;
    int self_group = -1;
    Mode mode = Mode::simulated;
    AuthoredPolicy policy = AuthoredPolicy::exact;
    std::uint32_t shell_push = 0; // `shell_push`: the collider tags pushing `cutscene_sim`'s shell
    float shell_push_from = 0.0F;
    bool environment = true;   // `environment off`: the room's collision leaves it alone
    bool clip_world = false;   // `clip_world`: the room left out inside its volume (model_link.cpp clip_volumes)
    float clip_inset = 0.0F;   // metres
    bool outward_axis = false; // `outward axis`: its axis, in its reference bone's rest space
    Vec3 outward_origin, outward_dir;
};

struct ControlRig {
    const ControlConfig *config = nullptr;
    std::vector<int> particles; // the particles it governs
    std::vector<int> groups;
    ParamSet params;
    int wind_bone = -1;
};

struct Rig {
    Config config;
    SkeletonDesc skeleton;
    std::vector<Particle> particles;
    std::vector<ParamBlock> params;
    std::vector<Distance> distances;
    std::vector<Face> faces;
    std::vector<std::array<int, 2>> edges; // collision edges (structural and shear)
    std::vector<Collider> colliders;
    std::vector<SimBone> bones_out; // parents first
    std::vector<Carried> carried;   // parents first
    std::vector<GroupRig> groups;
    std::vector<ControlRig> controls;
    std::vector<ReferenceBlend> blends;
    std::vector<int> needed; // the bones whose animated frames the solver reads, ascending
    // self-collision: pairs never tested (neighbours), and pairs close at rest held at least their rest distance
    std::vector<std::uint64_t> self_excluded; // sorted keys (a << 32 | b), a < b
    std::vector<std::pair<std::uint64_t, float>> self_closest;
    // particle-collider pairs a control starts inside, left out (collider index << 32 | particle)
    std::vector<std::uint64_t> collider_excluded;
    // face-collider pairs a body passes through at rest (a face across the inside): collider << 32 | face
    std::vector<std::uint64_t> face_excluded;
    // the authored clearance of each collider-control (c * particles + i) and collider-face (c * faces + f)
    // pair: its core's distance at rest in the collider bone's units (0: none). Contact holds a pair no
    // further apart than that, so a garment hangs as authored around a collider fitted larger than the body
    std::vector<float> point_clearance, face_clearance;
    std::vector<std::string> tag_names; // collider tags by bit
    int wind_bone = -1;                 // the global wind's bone (`wind bone <b>`)
    float model_units = 1.0F;           // metres a model unit (the importer's metres: 1)
    std::string summary;                // counts, for the log
};

// Cook the configuration against the model. Errors (bad bones, degenerate constraints, a bone two
// groups claim, rigs without the freedom asked of them) are reported in `out`; with any, the result
// is not usable (has_errors).
Rig cook(const Config &config, const SkeletonDesc &skeleton, const MeshDesc &mesh, Diagnostics &out);

// `clip_world`: a group's closed volume (vertices: its particles, then its top and bottom rings' middles), its
// faces wound outward at rest
struct ClipVolume {
    std::string group;
    std::vector<int> particles;
    std::vector<int> top, bottom; // the rings (vertex indices), round the body
    std::vector<std::array<int, 3>> faces;
    float inset = 0.0F; // metres
    std::size_t columns = 0, open_edges = 0;
};
// Each `clip_world` group's volume from its particles' rest places (`rest`, one a particle); none with fewer than
// three strips
std::vector<ClipVolume> clip_volumes_of(const Rig &rig, const std::vector<Vec3> &rest);

// A bone named in the file, or -1 (with a diagnostic).
int resolve_bone(const SkeletonDesc &skeleton, const BoneRef &ref, const std::string &file, Diagnostics &out);

// A pair's contact distance: the collider's full one, or its authored clearance where that is nearer,
// never under `floor`
inline float pair_reach(float full, float clearance, float floor)
{
    return clearance > 0.0F ? std::fmin(full, std::fmax(clearance, floor)) : full;
}

inline std::uint64_t pair_key(int a, int b)
{
    if (a > b)
        std::swap(a, b);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) | static_cast<std::uint32_t>(b);
}

} // namespace khdays::secondary
