#include "khdays/game/gameplay_camera.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace khdays::game {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTau = 2.0F * kPi;
constexpr float kInvFx = 1.0F / 4096.0F;

std::int32_t fx_mul(const std::int32_t a, const std::int32_t b) {
    return static_cast<std::int32_t>(
        (static_cast<std::int64_t>(a) * b + 0x800) >> 12);
}

std::int32_t approach_fx(
    const std::int32_t current,
    const std::int32_t target,
    const std::int32_t snap,
    const std::int32_t rate) {
    const std::int32_t delta = target - current;
    if (std::abs(delta) <= snap) {
        return target;
    }
    return current + fx_mul(delta, rate);
}

std::uint16_t approach_angle(
    const std::uint16_t current, const std::uint16_t target,
    const std::int32_t rate) {
    const auto delta = static_cast<std::int16_t>(target - current);
    if (delta == 0) {
        return current;
    }
    std::int32_t step = fx_mul(delta, rate);
    if (step == 0) {
        step = delta > 0 ? 1 : -1;
    }
    return static_cast<std::uint16_t>(current + step);
}

}  // namespace

std::array<float, 3> resolve_gameplay_camera_collision(
    const khdays::assets::CollisionModel& collision,
    const std::array<float, 3>& focus,
    const std::array<float, 3>& wanted_eye) {
    if (!collision.valid) {
        return wanted_eye;
    }

    constexpr float kSphereRadius = 0x500 * kInvFx;
    constexpr float kClearance = 0xc00 * kInvFx;
    constexpr float kProbeOffset = 0x800 * kInvFx;
    constexpr float kVerticalProbe = 0xa000 * kInvFx;
    GameplayCamera::Vec3 eye = wanted_eye;

    // func_ov002_0204ea58 keeps whichever of its ray/sphere/contact queries
    // reaches the nearest point before the requested camera position.
    const auto ray = khdays::assets::cast_segment(
        collision, focus, wanted_eye);
    const auto sphere = khdays::assets::sweep_sphere(
        collision, focus, wanted_eye, kSphereRadius);
    float best_fraction = 1.0F;
    if (ray.hit && ray.fraction < best_fraction) {
        eye = ray.point;
        best_fraction = ray.fraction;
    }
    if (sphere.hit && sphere.fraction < best_fraction) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            eye[axis] =
                focus[axis] + (wanted_eye[axis] - focus[axis])
                * sphere.fraction;
        }
        best_fraction = sphere.fraction;
    }

    // Its reverse ray catches a requested eye that started inside geometry.
    if (best_fraction >= 1.0F) {
        const auto reverse = khdays::assets::cast_segment(
            collision, wanted_eye, focus);
        if (reverse.hit && reverse.fraction < 1.0F) {
            eye = reverse.point;
        }
    }

    // Probe ten units above from half a unit below the eye and preserve the
    // original 0.75-unit ceiling clearance.
    const GameplayCamera::Vec3 ceiling_start{
        eye[0], eye[1] - kProbeOffset, eye[2]};
    const GameplayCamera::Vec3 ceiling_end{
        eye[0], ceiling_start[1] + kVerticalProbe, eye[2]};
    const auto ceiling = khdays::assets::cast_segment(
        collision, ceiling_start, ceiling_end);
    if (ceiling.hit && ceiling.point[1] - eye[1] < kClearance) {
        eye[1] = std::min(eye[1], ceiling.point[1] - kClearance);
    }

    // The matching downward ray lifts the eye by 0xc00 along the surface
    // normal when it is too near the floor or lies on the wrong side.
    const GameplayCamera::Vec3 floor_start{
        eye[0], eye[1] + kProbeOffset, eye[2]};
    const GameplayCamera::Vec3 floor_end{
        eye[0], floor_start[1] - kVerticalProbe, eye[2]};
    const auto floor = khdays::assets::cast_segment(
        collision, floor_start, floor_end);
    if (floor.hit) {
        const GameplayCamera::Vec3 from_floor{
            eye[0] - floor.point[0],
            eye[1] - floor.point[1],
            eye[2] - floor.point[2]};
        const float floor_distance = std::sqrt(
            from_floor[0] * from_floor[0]
            + from_floor[1] * from_floor[1]
            + from_floor[2] * from_floor[2]);
        const float normal_dot = from_floor[0] * floor.normal[0]
            + from_floor[1] * floor.normal[1]
            + from_floor[2] * floor.normal[2];
        if (floor_distance < kClearance || normal_dot < 0.0F) {
            for (std::size_t axis = 0; axis < 3U; ++axis) {
                eye[axis] =
                    floor.point[axis] + floor.normal[axis] * kClearance;
            }
        }
    }

    // Unless a special camera mode suppresses it, ov002 never permits less
    // than 0xc00 of horizontal separation from the focus.
    const float flat_x = eye[0] - focus[0];
    const float flat_z = eye[2] - focus[2];
    const float flat_length = std::sqrt(flat_x * flat_x + flat_z * flat_z);
    if (flat_length < kClearance) {
        GameplayCamera::Vec3 direction{
            wanted_eye[0] - focus[0],
            wanted_eye[1] - focus[1],
            wanted_eye[2] - focus[2]};
        const float length = std::sqrt(
            direction[0] * direction[0] + direction[1] * direction[1]
            + direction[2] * direction[2]);
        if (length > 0.000001F) {
            for (std::size_t axis = 0; axis < 3U; ++axis) {
                eye[axis] = focus[axis] + direction[axis] / length * kClearance;
            }
            if (ceiling.hit) {
                eye[1] = std::min(
                    eye[1], ceiling.point[1] - kClearance);
            }
        }
    }
    return eye;
}

void GameplayCamera::reset(
    const Vec3& actor_position, const std::uint16_t yaw) {
    // func_02023c60: sin/cos for a 60-degree vertical FOV, 4:3 aspect,
    // near 1.0 and far 1000.0. func_ov002_0204fdc4 starts at distance 3.0;
    // the ordinary tick then approaches selector-0's 5.0 + 0.75 clearance.
    camera_.fov_y = kPi / 3.0F;
    camera_.near_z = 1.0F;
    camera_.far_z = 1000.0F;
    yaw_ = yaw;
    target_yaw_ = yaw;
    distance_ = 0x3000;
    target_distance_ = 0x5c00;
    eye_height_ = 0x1a00;
    target_eye_height_ = 0x1a00;
    // Selector 0 scale (1.0) plus the 0x4cd world-class adjustment used by
    // Twilight Town's actor entry in Ov002_GetCameraDistance.
    focus_offset_ = 0x14cd;
    target_focus_offset_ = 0x14cd;
    focus_ = actor_position;
    focus_[1] += static_cast<float>(focus_offset_) * kInvFx;
    rebuild_camera({});
}

void GameplayCamera::update(
    const Input& input,
    const Vec3& actor_position,
    const CollisionProbe& collision_probe) {
    // func_ov002_0204d170 maps DS R (0x100) to input bit 4 and subtracts
    // func_ov002_02050a08 returns 0x480 in the port's 60 Hz display mode.
    // DS R subtracts it and L adds it; pressing both cancels both branches.
    const bool right = input.held(Button::R);
    const bool left = input.held(Button::L);
    if (right && !left) {
        target_yaw_ = static_cast<std::uint16_t>(target_yaw_ - 0x480U);
    } else if (left && !right) {
        target_yaw_ = static_cast<std::uint16_t>(target_yaw_ + 0x480U);
    }

    eye_height_ = approach_fx(
        eye_height_, target_eye_height_, 0x100, 0x900);
    focus_offset_ = approach_fx(
        focus_offset_, target_focus_offset_, 0x100, 0x900);

    const std::int32_t distance_delta = target_distance_ - distance_;
    if (std::abs(distance_delta) < 0x100) {
        distance_ = target_distance_;
    } else {
        // The original uses 0x400 while opening the distance and a faster
        // yaw-dependent rate while pulling it in. Selector zero opens here.
        const std::int32_t rate = distance_ < target_distance_ ? 0x400 : 0x800;
        distance_ += fx_mul(distance_delta, rate);
    }
    yaw_ = approach_angle(yaw_, target_yaw_, 0x800);

    Vec3 wanted_focus = actor_position;
    wanted_focus[1] += static_cast<float>(focus_offset_) * kInvFx;
    const Vec3 horizontal{
        wanted_focus[0] - focus_[0], 0.0F,
        wanted_focus[2] - focus_[2]};
    const float horizontal_length = std::sqrt(
        horizontal[0] * horizontal[0] + horizontal[2] * horizontal[2]);
    if (horizontal_length > 0.00390625F) {
        const float step = horizontal_length * (0x480 * kInvFx);
        focus_[0] += horizontal[0] / horizontal_length * step;
        focus_[2] += horizontal[2] / horizontal_length * step;
    } else {
        focus_[0] = wanted_focus[0];
        focus_[2] = wanted_focus[2];
    }
    const float vertical_delta = wanted_focus[1] - focus_[1];
    if (std::fabs(vertical_delta) > 0.00390625F) {
        focus_[1] += vertical_delta * (0x900 * kInvFx);
    } else {
        focus_[1] = wanted_focus[1];
    }

    rebuild_camera(collision_probe);
}

void GameplayCamera::rebuild_camera(
    const CollisionProbe& collision_probe) {
    const float distance = static_cast<float>(distance_) * kInvFx;
    const float height = static_cast<float>(eye_height_) * kInvFx;
    const float horizontal = std::max(
        0.75F, std::sqrt(std::max(0.0F, distance * distance - height * height)));
    const float angle = yaw_radians();
    Vec3 wanted_eye{
        focus_[0] + std::sin(angle) * horizontal,
        focus_[1] + height,
        focus_[2] + std::cos(angle) * horizontal};

    // The horizontal/vertical construction should already have full length,
    // but ov002 normalises and re-scales it; retain that operation and its
    // protection against accumulated floating-point drift at the boundary.
    const float dx = wanted_eye[0] - focus_[0];
    const float dy = wanted_eye[1] - focus_[1];
    const float dz = wanted_eye[2] - focus_[2];
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length > 0.000001F) {
        const float scale = distance / length;
        wanted_eye = {
            focus_[0] + dx * scale,
            focus_[1] + dy * scale,
            focus_[2] + dz * scale};
    }
    camera_.target = focus_;
    camera_.eye = collision_probe
        ? collision_probe(focus_, wanted_eye)
        : wanted_eye;
}

float GameplayCamera::yaw_radians() const {
    return static_cast<float>(yaw_) * (kTau / 65536.0F);
}

}  // namespace khdays::game
