#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "khdays/game/input.h"

namespace khdays::game {

// Deterministic controller for the first playable vertical slice. It is kept
// independent of assets and rendering so movement/collision rules can be
// exercised headlessly. Coordinates are neutral world units; GroundProbe
// returns the floor height or nullopt when a step would leave walkable ground.
class PlayableController final {
public:
    enum class LocomotionPhase {
        Grounded,
        JumpStart,
        Rising,
        Falling,
        Landing,
    };

    struct State final {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        float facing = 0.0F;
        float camera_yaw = 0.0F;
        std::uint16_t hp = 100U;
        std::uint16_t max_hp = 100U;
        bool moving = false;
        bool completed = false;
        LocomotionPhase locomotion = LocomotionPhase::Grounded;
    };

    using GroundProbe =
        std::function<std::optional<float>(float x, float z)>;
    using MotionProbe = std::function<bool(
        float from_x, float from_y, float from_z,
        float to_x, float to_y, float to_z, float radius)>;

    PlayableController(
        float spawn_x = 0.0F,
        float spawn_z = -5.0F,
        float goal_x = 0.0F,
        float goal_z = -15.0F,
        float camera_yaw = 0.0F);

    void update(
        const Input& input,
        const GroundProbe& ground_probe,
        const MotionProbe& motion_probe = {});
    void reset(const GroundProbe& ground_probe);
    void set_camera_yaw(float yaw) { state_.camera_yaw = yaw; }

    const State& state() const { return state_; }
    float goal_x() const { return goal_x_; }
    float goal_z() const { return goal_z_; }
    std::int32_t step_rate_fx() const { return step_rate_fx_; }
    std::int32_t vertical_velocity_fx() const {
        return vertical_velocity_fx_;
    }
    std::uint16_t motion_angle() const { return motion_angle_; }
    bool grounded() const {
        return state_.locomotion == LocomotionPhase::Grounded;
    }

private:
    float spawn_x_ = 0.0F;
    float spawn_z_ = 0.0F;
    float goal_x_ = 0.0F;
    float goal_z_ = 0.0F;
    float camera_yaw_ = 0.0F;
    std::int32_t step_rate_fx_ = 0;
    std::int32_t vertical_velocity_fx_ = 0;
    std::int32_t phase_frame_fx_ = 0;
    float motion_x_ = 0.0F;
    float motion_z_ = 0.0F;
    std::uint16_t motion_angle_ = 0;
    State state_;
};

}  // namespace khdays::game
