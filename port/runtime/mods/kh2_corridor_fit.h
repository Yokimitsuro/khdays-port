// How KH2's Dark Corridor stands in Days's oval (kh2_corridors.h): its axes, size and units in the oval's, and when
// it closes. Measured, not chosen (docs/RUNTIME_MAP.md, "The Dark Corridors"); no game state, so the tests run it
// (port/tests/kh2_corridor_test.cpp).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "khdays/assets/kh2_effect.h"

namespace khdays::kh2_corridor {

namespace kh2 = khdays::assets::kh2;
using Matrix = kh2::EffectMatrix; // row vectors, as the port's float matrices lie (g3d.h khdays_g3d_matrices)

// KH2's oval, its models at the hold, reaches 430 of the effect's units above the effect's floor (its y 0, y pointing
// down; 300 across; khdays-kh2-effect-dump --extent). Days's oval as it is seen reaches 3.7 of its model's units above
// its floor (2.4 across): its darkness on mission 6's frames against the frame before it, the screen's rows taken to
// the model's heights through the oval's own matrices (KHDAYS_TRACE_CORRIDORS=2) -- its pictures fade out well inside
// its model, whose smoke lines reach 7.556.
inline constexpr float kKh2OvalTop = 430.0F;
inline constexpr float kDaysOvalTop = 3.7F;

// Days's oval's own floor shadow, the flat smog_d of mi/ob/06.z E000_xx_000 (1.1 across), lies 0.135 of its units
// above the oval's floor; its smaller flat smog_c lie at 0.039 and 0.083. The oval stands sunk in the ground: in
// mission 6 its floor is at the world's -0.090 and the characters beside it stand at 0.000 (KHDAYS_TRACE_CORRIDORS=3),
// so smog_d is the one seen. KH2's shadow (discs and rings at its y 0) would lie on its floor, under the game's floor,
// which hides it (both drawn nearer only): KH2's floor goes on Days's floor shadow, its top on Days's seen top.
inline constexpr float kDaysFloorLift = 0.135F;
// Days's units a KH2 unit at the oval's own scale: KH2's floor-to-top over Days's (2.5 across)
inline constexpr float kDaysPerKh2 = (kDaysOvalTop - kDaysFloorLift) / kKh2OvalTop;

// Days's oval's own closing clip: mi/ob/06.z's oval plays E000_xx_000 (opening, 70 frames), E010_xx_000 (holding,
// 60) and E020_xx_000 (closing, 61; the oval seen until its 53rd), and is gone at its end (traced in mission 6).
// KH2's corridor closes within it, so as to end with it. 09.z's (E010 alone) and 07.z's (one clip of 167 frames,
// E_100_xx_000) have none: theirs close when the oval is no longer drawn.
inline constexpr const char *kClosingClip = "E020_xx_000";

// Days's units a KH2 unit for an oval drawn at `view` (its model to the view): the fit times the model's own scale
// and the camera's (the view's rows' mean length), taken out of `view` so that KH2's sprites take it too.
inline float corridor_units(Matrix &view)
{
    float length = 0.0F;
    for (std::size_t r = 0; r < 3; ++r) {
        const float *row = &view[r * 4];
        length += std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]) / 3.0F;
    }
    if (!(length > 1e-6F) || !std::isfinite(length))
        length = 1.0F;
    for (std::size_t r = 0; r < 3; ++r)
        for (std::size_t k = 0; k < 3; ++k)
            view[r * 4 + k] /= length;
    return kDaysPerKh2 * length;
}

// The oval at `view` (its model to the view) turned about its own up so that its front (+z) looks at the camera (the
// view's origin) across the floor, as KH2's corridors are seen, from the front (the maintainer's choice, 2026-11-07);
// its place, up and size kept, never mirrored. Left as it is when the camera is straight above or below it.
inline void face_camera(Matrix &view)
{
    float length[3];
    for (std::size_t r = 0; r < 3; ++r)
        length[r] = std::sqrt(view[r * 4] * view[r * 4] + view[r * 4 + 1] * view[r * 4 + 1] +
                              view[r * 4 + 2] * view[r * 4 + 2]);
    if (!(length[0] > 1e-6F && length[1] > 1e-6F && length[2] > 1e-6F))
        return;
    float up[3], front[3], to[3];
    for (std::size_t k = 0; k < 3; ++k) {
        up[k] = view[4 + k] / length[1];
        front[k] = view[8 + k] / length[2];
        to[k] = -view[12 + k];
    }
    const float along = to[0] * up[0] + to[1] * up[1] + to[2] * up[2];
    for (std::size_t k = 0; k < 3; ++k)
        to[k] -= along * up[k];
    const float n = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    if (!(n > 1e-4F * std::sqrt(view[12] * view[12] + view[13] * view[13] + view[14] * view[14])) || !(n > 0.0F))
        return;
    for (float &v : to)
        v /= n;
    // its x as its own was to its up and front: up x front, or the other way for a mirrored model
    const float was[3] = {up[1] * front[2] - up[2] * front[1], up[2] * front[0] - up[0] * front[2],
                          up[0] * front[1] - up[1] * front[0]};
    const float hand = was[0] * view[0] + was[1] * view[1] + was[2] * view[2] < 0.0F ? -1.0F : 1.0F;
    const float side[3] = {up[1] * to[2] - up[2] * to[1], up[2] * to[0] - up[0] * to[2], up[0] * to[1] - up[1] * to[0]};
    for (std::size_t k = 0; k < 3; ++k) {
        view[k] = hand * side[k] * length[0];
        view[8 + k] = to[k] * length[2];
    }
}

// KH2's camera for a corridor in an oval drawn at `view` (made unit by corridor_units) and `projection`. The
// effect's axes are the oval's turned half a turn about z: its y points down and its front -- where KH2's camera
// looks into the corridor through the opening its depth-only mask leaves (the mask a plane at z 0 holed in the
// oval's shape, the corridor's bowl behind it at -z) -- is its +z as Days's oval's is (the game's camera sees the
// oval from its +z); x turns with them. KH2's view space is Days's turned half a turn about x (y down, z forward).
// The effect runs in its own units: its world the identity, its view the oval's -- in KH2's axes, shrunk by `units`
// so that its sprites, sized in view space by the world's scale, keep KH2's size -- and its projection Days's,
// back in Days's axes and units; what it draws comes out of the clip space divided by `units` (multiply it back).
inline kh2::EffectCamera corridor_camera(const Matrix &view, const Matrix &projection, float units)
{
    static constexpr float model[3] = {-1.0F, -1.0F, 1.0F}; // the effect's axes to the oval's
    static constexpr float turn[3] = {1.0F, -1.0F, -1.0F};  // Days's view's to KH2's
    kh2::EffectCamera c;
    c.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    c.view = {};
    for (std::size_t r = 0; r < 3; ++r)
        for (std::size_t k = 0; k < 3; ++k)
            c.view[r * 4 + k] = model[r] * view[r * 4 + k] * turn[k];
    // the effect's floor raised by kDaysFloorLift along its up (-y), in KH2's units
    const float lift = kDaysFloorLift / kDaysPerKh2;
    for (std::size_t k = 0; k < 3; ++k)
        c.view[12 + k] = view[12 + k] / units * turn[k] - lift * c.view[4 + k];
    c.view[15] = 1.0F;
    for (std::size_t k = 0; k < 4; ++k) {
        for (std::size_t r = 0; r < 3; ++r)
            c.projection[r * 4 + k] = turn[r] * projection[r * 4 + k];
        c.projection[12 + k] = projection[12 + k] / units;
    }
    return c;
}

// How many ticks the effect takes to end once told to stop from its hold (after its first full run), played
// without a camera that matters; bounded by its own length and its layers' starts and lives, 0 if it does not end
// within them.
inline std::uint32_t closing_ticks(const std::shared_ptr<const kh2::EffectBlock> &block)
{
    const kh2::EffectEmitter &e = block->emitters.front();
    std::int64_t bound = e.length; // its layers' starts and lives that come (not 0x70000000 on)
    for (const auto &layer : e.layers) {
        if (layer.start >= 0 && layer.start < 0x70000000)
            bound += layer.start;
        if (layer.life > 0 && layer.life < 0x70000000)
            bound += layer.life;
    }
    kh2::EffectInstance fx(block, 0, 0);
    kh2::EffectCamera none;
    none.world = none.view = none.projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (std::int32_t t = 0; t < e.length / 0x1000; ++t)
        fx.tick(none, nullptr);
    fx.stop();
    for (std::int64_t t = 1; t <= bound / 0x1000 + 1; ++t) {
        fx.tick(none, nullptr);
        if (fx.done())
            return static_cast<std::uint32_t>(t);
    }
    return 0;
}

// Whether a corridor whose oval's closing clip has `left` of the game's frames left (each `ticks_per_frame` of
// KH2's) closes now: when its closing takes as long as the clip has left, both ending together.
inline bool closes_now(float left, unsigned ticks_per_frame, std::uint32_t close_ticks)
{
    return left >= 0.0F && left * static_cast<float>(ticks_per_frame) <= static_cast<float>(close_ticks);
}

} // namespace khdays::kh2_corridor
