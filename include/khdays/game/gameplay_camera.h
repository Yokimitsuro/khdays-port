#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "khdays/assets/collision.h"
#include "khdays/assets/scene3d.h"
#include "khdays/game/input.h"

namespace khdays::game {

// Neutral counterpart of func_ov002_0204ea58. The query order, 0x500 sphere
// radius and 0xc00 floor/near-focus clearances come from the decompiled code.
std::array<float, 3> resolve_gameplay_camera_collision(
    const khdays::assets::CollisionModel& collision,
    const std::array<float, 3>& focus,
    const std::array<float, 3>& wanted_eye);

// Native form of ov002's ordinary gameplay camera state. Quantities that the
// original advances in 20.12 or binary-angle units stay in those units here;
// conversion to the neutral renderer happens only at the output boundary.
class GameplayCamera final {
public:
    using Vec3 = std::array<float, 3>;
    using CollisionProbe = std::function<Vec3(
        const Vec3& focus, const Vec3& wanted_eye)>;

    void reset(const Vec3& actor_position, std::uint16_t yaw = 0x8000U);
    void update(
        const Input& input,
        const Vec3& actor_position,
        const CollisionProbe& collision_probe = {});

    const khdays::assets::Camera3D& camera() const { return camera_; }
    float yaw_radians() const;

    // Fixed-point state is exposed read-only for deterministic tests and the
    // developer-room diagnostics.
    std::uint16_t yaw() const { return yaw_; }
    std::uint16_t target_yaw() const { return target_yaw_; }
    std::int32_t distance_fx() const { return distance_; }
    std::int32_t target_distance_fx() const { return target_distance_; }
    std::int32_t eye_height_fx() const { return eye_height_; }
    std::int32_t focus_offset_fx() const { return focus_offset_; }

private:
    void rebuild_camera(const CollisionProbe& collision_probe);

    khdays::assets::Camera3D camera_;
    Vec3 focus_{0.0F, 0.0F, 0.0F};
    std::uint16_t yaw_ = 0x8000U;
    std::uint16_t target_yaw_ = 0x8000U;
    std::int32_t distance_ = 0x3000;
    std::int32_t target_distance_ = 0x5c00;
    std::int32_t eye_height_ = 0x1a00;
    std::int32_t target_eye_height_ = 0x1a00;
    std::int32_t focus_offset_ = 0x14cd;
    std::int32_t target_focus_offset_ = 0x14cd;
};

}  // namespace khdays::game
