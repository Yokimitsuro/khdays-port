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

struct DrawInput {
    const void *actor = nullptr;       // the drawn render object
    Mat4 root = kIdentity4;            // the model's units to view space (the game's root node, scaled)
    Mat4 start = kIdentity4;           // the position matrix as the draw started (view x the actor's world)
    const float *projection = nullptr; // view to clip, for the debug drawing
    std::vector<DrawAnimation> animations;
};

// The skinning matrices `skin` (per model joint, view space) of one draw rebuilt where the simulation
// moves them. Does nothing without a camera yet, or while the actor is frozen.
void apply_model_secondary(ModelSecondary &m, const DrawInput &in, std::vector<Mat4> &skin);

} // namespace khdays::secondary
