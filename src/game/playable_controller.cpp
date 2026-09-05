#include "khdays/game/playable_controller.h"

#include <cmath>

namespace khdays::game {

namespace {
constexpr float kMoveSpeed = 0.08F;
constexpr float kCameraSpeed = 0.025F;
constexpr float kGoalRadius = 1.25F;
constexpr float kBodyRadius = 0.3125F;  // ov002 player cast radius: 0x500
constexpr float kBodyCentreY = 0.72F;
}  // namespace

PlayableController::PlayableController(
    const float spawn_x,
    const float spawn_z,
    const float goal_x,
    const float goal_z,
    const float camera_yaw)
    : spawn_x_(spawn_x),
      spawn_z_(spawn_z),
      goal_x_(goal_x),
      goal_z_(goal_z),
      camera_yaw_(camera_yaw) {
    state_.x = spawn_x_;
    state_.z = spawn_z_;
    state_.camera_yaw = camera_yaw_;
}

void PlayableController::reset(const GroundProbe& ground_probe) {
    state_ = {};
    state_.x = spawn_x_;
    state_.z = spawn_z_;
    state_.camera_yaw = camera_yaw_;
    if (const auto ground = ground_probe(state_.x, state_.z)) {
        state_.y = *ground;
    }
}

void PlayableController::update(
    const Input& input,
    const GroundProbe& ground_probe,
    const MotionProbe& motion_probe) {
    if (input.held(Button::L)) {
        state_.camera_yaw -= kCameraSpeed;
    }
    if (input.held(Button::R)) {
        state_.camera_yaw += kCameraSpeed;
    }

    float local_x = 0.0F;
    float local_z = 0.0F;
    if (input.held(Button::Left)) {
        local_x -= 1.0F;
    }
    if (input.held(Button::Right)) {
        local_x += 1.0F;
    }
    if (input.held(Button::Up)) {
        local_z -= 1.0F;
    }
    if (input.held(Button::Down)) {
        local_z += 1.0F;
    }

    state_.moving = local_x != 0.0F || local_z != 0.0F;
    if (state_.moving) {
        const float length = std::sqrt(local_x * local_x + local_z * local_z);
        local_x /= length;
        local_z /= length;

        const float c = std::cos(state_.camera_yaw);
        const float s = std::sin(state_.camera_yaw);
        const float world_x = c * local_x + s * local_z;
        const float world_z = -s * local_x + c * local_z;
        const float old_x = state_.x;
        const float old_z = state_.z;
        const auto attempt = [&](const float dx, const float dz) {
            const float candidate_x = state_.x + dx;
            const float candidate_z = state_.z + dz;
            const auto ground = ground_probe(candidate_x, candidate_z);
            if (!ground) {
                return false;
            }
            if (motion_probe
                && !motion_probe(
                    state_.x, state_.y + kBodyCentreY, state_.z,
                    candidate_x, *ground + kBodyCentreY, candidate_z,
                    kBodyRadius)) {
                return false;
            }
            state_.x = candidate_x;
            state_.y = *ground;
            state_.z = candidate_z;
            return true;
        };
        const float dx = world_x * kMoveSpeed;
        const float dz = world_z * kMoveSpeed;
        bool accepted = attempt(dx, dz);
        // Axis-separated retries provide a stable wall slide without allowing
        // a diagonal step to tunnel through a corner.
        if (!accepted && dx != 0.0F && dz != 0.0F) {
            accepted = attempt(dx, 0.0F) || attempt(0.0F, dz);
        }
        if (accepted) {
            const float moved_x = state_.x - old_x;
            const float moved_z = state_.z - old_z;
            state_.facing = std::atan2(moved_x, moved_z);
        } else {
            state_.moving = false;
        }
    }

    const float goal_dx = state_.x - goal_x_;
    const float goal_dz = state_.z - goal_z_;
    if (goal_dx * goal_dx + goal_dz * goal_dz
        <= kGoalRadius * kGoalRadius) {
        state_.completed = true;
    }
}

}  // namespace khdays::game
