// Secondary motion for the mods' models (models.cpp): a model's `<model>.secondary.txt` cooked at load,
// one simulation per drawn actor, run on each draw after the animation (the game's, or KH2's motions
// retargeted and blended) and before the skinning (docs/SECONDARY_MOTION.md, "In the game").
#pragma once

#include "vecmath.h"

#include <memory>
#include <string>
#include <vector>

namespace khdays::assets {
struct ImportedModel;
}

namespace khdays::secondary {

struct ModelSecondary;
struct ModelSecondaryDeleter {
    void operator()(ModelSecondary *m) const;
};
using ModelSecondaryPtr = std::unique_ptr<ModelSecondary, ModelSecondaryDeleter>;

// A model's secondary motion, when its file has one beside it (`<model>.secondary.txt`); null when it
// has none or it cannot be used (the diagnostics are logged). `joint_of_bone[b]`: the model's joint
// for imported joint b; `inverse_bind[j]`: joint j's inverse bind (model units to its bone).
ModelSecondaryPtr load_model_secondary(const std::string &model_file, const std::string &name,
                                       const assets::ImportedModel &model, const std::vector<int> &joint_of_bone,
                                       const std::vector<Mat4> &inverse_bind);

struct DrawAnimation {
    std::string name;
    int slot = -1;
    float section = -1.0F;
    float frame = 0.0F, frames = 0.0F; // the game's frames (30 a second)
    float weight = 1.0F;
};

// The game model a mod replaces, at rest: its vertices (model space) with their nodes and weights, its
// triangles, each node's parent and head, the node each of the mod's bones follows (-1 none) and the mod's
// `scale`
struct GameMesh {
    std::vector<Vec3> positions;
    std::vector<std::array<std::pair<int, float>, 4>> weights;
    std::vector<std::uint32_t> triangles; // 3 indices each
    std::vector<int> node_parent;
    std::vector<Vec3> node_place;
    std::vector<int> node_of_bone;
    float scale = 1.0F;
};

struct DrawInput {
    const void *actor = nullptr; // the drawn render object
    Mat4 root = kIdentity4;      // the model's units to view space (the game's root node, scaled)
    Mat4 start = kIdentity4;     // the position matrix as the draw started (view x the actor's world)
    Mat4 base = kIdentity4;      // the latest flush's base transform (gfx/g3d.h khdays_g3d_base) ...
    int base_kind = 0;           // ... and its kind there (0 none)
    Mat4 camera = kIdentity4;    // the latest flush's camera, world to view
    bool has_camera = false;
    const float *projection = nullptr; // view to clip, for the debug drawing
    std::vector<DrawAnimation> animations;
    const std::vector<Mat4> *game_nodes = nullptr; // the game model's nodes now from rest (view space), `game_shape`
};

// The skinning matrices `skin` (per model joint, view space) of one draw rebuilt where the simulation
// moves them. Does nothing without a camera yet, or while the actor is frozen.
void apply_model_secondary(ModelSecondary &m, const DrawInput &in, std::vector<Mat4> &skin);

// `game_shape`: in a group with follow bones (a coat on the legs, its front strip forked), the coat locked on the
// game model's coat (the triangles on the reference bone's node, those above and below it and the follow bones'
// nodes and their children, in pieces reaching the hips): the back, the sides (each side's middle strip), the
// strips in front of them and the forked front each laid down the game coat's edges from its top through the
// game's knee vertex (the fork: the slit's top) to its bottom (the slit's corners); the strips behind the sides
// on outward arcs between their side and the back. Kept for a reload.
void set_game_mesh(ModelSecondary &m, const GameMesh &game);

} // namespace khdays::secondary
