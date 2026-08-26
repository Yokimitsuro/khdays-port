#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "khdays/assets/mesh.h"  // NeutralModel, posed_position
#include "khdays/assets/tex0.h"  // DecodedTexture

// A neutral, perspective 3D scene rasterizer.
//
// The port's game loop draws through a 2D `Renderer` (blit RGBA at x,y), while
// the 3D model viewer is a separate SDL GPU path that owns its own window and
// event loop -- two different SDL APIs that cannot share a window. This renders
// a scene to an RGBA image instead, so the same result can be blitted by the
// windowed backend and by the headless one that produces the project's
// verification snapshots.
//
// It is deliberately CPU-side and engine-independent: no SDL, no DS formats,
// only the neutral mesh and texture types. At the DS's own 256x192 the cost is
// small, and a GPU fast path can replace it later behind the same call.
namespace khdays::assets {

// A right-handed camera looking down its own -Z.
struct Camera3D final {
    std::array<float, 3> eye{0.0F, 0.0F, 1.0F};
    std::array<float, 3> target{0.0F, 0.0F, 0.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    float fov_y = 0.9F;  // vertical field of view, radians
    float near_z = 0.05F;
    float far_z = 500.0F;
};

// One model to draw. Instances share a world space -- which is what the game's
// own room data does: a room's geometry sub-files (e.g. `tw_03_1` and
// `tw_03_2`) are authored in the same coordinates, so drawing them together
// with one depth buffer reassembles the room.
struct ModelInstance final {
    const NeutralModel* model = nullptr;
    const std::map<std::string, DecodedTexture>* textures = nullptr;
};

// Axis-aligned bounds over every instance's posed vertices. `valid` is false
// when the scene has no vertices.
struct SceneBounds final {
    std::array<float, 3> min{0.0F, 0.0F, 0.0F};
    std::array<float, 3> max{0.0F, 0.0F, 0.0F};
    std::array<float, 3> center{0.0F, 0.0F, 0.0F};
    float radius = 0.0F;
    bool valid = false;
};
SceneBounds scene_bounds(const std::vector<ModelInstance>& instances);

// Place a camera that frames the whole scene, orbiting its centre at `yaw` and
// `pitch` (radians). `distance_scale` pushes the camera further out (>1) or
// closer in (<1). Returns a default camera for an empty scene.
Camera3D frame_scene(
    const std::vector<ModelInstance>& instances,
    float yaw,
    float pitch,
    float distance_scale = 1.0F);

// Rasterize the scene to a `width` x `height` RGBA image with a transparent
// backdrop.
//
// Perspective-correct in texture coordinates and depth-buffered, so instances
// interpenetrate correctly. Two deliberate simplifications, both visible only
// in edge cases: triangles crossing the near plane are dropped whole rather
// than clipped, and a translucent pixel blends without writing depth, so
// translucent surfaces are not sorted against each other.
DecodedTexture render_scene(
    const std::vector<ModelInstance>& instances,
    const Camera3D& camera,
    int width,
    int height);

}  // namespace khdays::assets
