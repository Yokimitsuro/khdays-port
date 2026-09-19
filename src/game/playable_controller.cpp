#include "khdays/game/playable_controller.h"

#include <cmath>

namespace khdays::game {

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTau = 2.0F * kPi;
constexpr float kInvFx = 1.0F / 4096.0F;
constexpr float kGoalRadius = 1.25F;
constexpr float kBodyRadius = 0.3125F;  // ov002 player cast radius: 0x500
constexpr float kBodyCentreY = 0.72F;
// ov022 selects the larger values only in global mode 1, whose main loop
// waits two VBlanks. The native port updates once per 60 Hz VBlank, matching
// mode 0 and therefore its normal-rate branch.
constexpr std::int32_t kMoveRate = 0xc00;
constexpr std::int32_t kWalkSpeed = 0x333;
constexpr std::int32_t kStepEase = 0x80;
// func_ov022_02097d50 seeds vecMotion.y with 0x630. The shared placement
// routine func_0202da08 subtracts its default 0x80 gravity every 60 Hz tick;
// Ov022_StepJumpState caps normal falling recoil at -0x420.
constexpr std::int32_t kJumpVelocity = 0x630;
constexpr std::int32_t kGravity = 0x80;
constexpr std::int32_t kFallVelocity = -0x420;
constexpr std::int32_t kAirStepRate = 0x2a6;
constexpr std::int32_t kMotionKeep = 0xccd;
constexpr std::int32_t kJumpStartFrames = 3 * 0x1000;
constexpr std::int32_t kLandingFrames = 15 * 0x1000;
constexpr std::int32_t kNodeStepMove = 0xd00;
constexpr std::int32_t kJumpMotionShare = 0x333;

float fx_mul(const float value, const std::int32_t scale) {
    return value * (static_cast<float>(scale) * kInvFx);
}

std::uint16_t binary_angle(const float radians) {
    const float turns = radians / kTau;
    const auto units = static_cast<std::int64_t>(
        std::llround(turns * 65536.0F));
    return static_cast<std::uint16_t>(units);
}

std::uint16_t input_angle(const Input& input, bool& has_direction) {
    int angle = -1;
    int count = 0;
    if (input.held(Button::Up)) {
        angle = 0;
        ++count;
    } else if (input.held(Button::Down)) {
        angle = 0x8000;
        ++count;
    }
    if (input.held(Button::Left)) {
        angle += 0x4000;
        ++count;
    } else if (input.held(Button::Right)) {
        angle += input.held(Button::Up) ? 0x1c000 : 0xc000;
        ++count;
    }
    has_direction = count != 0;
    if (count > 1) {
        angle >>= 1;
    }
    return static_cast<std::uint16_t>(angle);
}

std::uint16_t clamp_motion_angle(
    const std::uint16_t current, const std::uint16_t target) {
    const std::uint16_t wrapped =
        static_cast<std::uint16_t>(target - current);
    const std::uint32_t distance = wrapped > 0x8000U
        ? 0x10000U - wrapped
        : wrapped;
    if (distance <= static_cast<std::uint32_t>(kMoveRate)
        || distance > 0x7000U) {
        return target;
    }
    return static_cast<std::uint16_t>(
        current + (wrapped > 0x8000U ? -kMoveRate : kMoveRate));
}
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
    motion_angle_ = binary_angle(camera_yaw_);
}

void PlayableController::reset(const GroundProbe& ground_probe) {
    state_ = {};
    state_.x = spawn_x_;
    state_.z = spawn_z_;
    state_.camera_yaw = camera_yaw_;
    step_rate_fx_ = 0;
    vertical_velocity_fx_ = 0;
    phase_frame_fx_ = 0;
    motion_x_ = 0.0F;
    motion_z_ = 0.0F;
    motion_angle_ = binary_angle(camera_yaw_);
    if (const auto ground = ground_probe(state_.x, state_.z)) {
        state_.y = *ground;
    }
}

void PlayableController::update(
    const Input& input,
    const GroundProbe& ground_probe,
    const MotionProbe& motion_probe) {
    bool has_direction = false;
    const std::uint16_t aim_angle = input_angle(input, has_direction);
    state_.moving = false;

    const auto attempt_horizontal = [&](const float dx, const float dz,
                                        const bool follow_ground) {
        const float candidate_x = state_.x + dx;
        const float candidate_z = state_.z + dz;
        const auto ground = ground_probe(candidate_x, candidate_z);
        if (follow_ground && !ground) {
            return false;
        }
        const float candidate_y = follow_ground && ground
            ? *ground
            : state_.y;
        if (motion_probe
            && !motion_probe(
                state_.x, state_.y + kBodyCentreY, state_.z,
                candidate_x, candidate_y + kBodyCentreY, candidate_z,
                kBodyRadius)) {
            return false;
        }
        state_.x = candidate_x;
        if (follow_ground) {
            state_.y = *ground;
        }
        state_.z = candidate_z;
        return true;
    };

    if (state_.locomotion == LocomotionPhase::Grounded) {
        state_.moving = has_direction;
    }
    if (state_.moving) {
        const std::uint16_t target_angle = static_cast<std::uint16_t>(
            aim_angle + binary_angle(state_.camera_yaw));
        motion_angle_ = clamp_motion_angle(motion_angle_, target_angle);
        if (step_rate_fx_ < kWalkSpeed) {
            // Ov022_SteerIdleByAim deliberately overshoots for one frame
            // before snapping back to nWalkSpeed on the following tick.
            step_rate_fx_ += kStepEase;
        } else {
            step_rate_fx_ = kWalkSpeed;
        }
        const float angle =
            static_cast<float>(motion_angle_) * (kTau / 65536.0F);
        const float speed = static_cast<float>(step_rate_fx_) * kInvFx;
        const float dx = -std::sin(angle) * speed;
        const float dz = -std::cos(angle) * speed;
        state_.facing = std::atan2(dx, dz);
        const float old_x = state_.x;
        const float old_z = state_.z;
        bool accepted = attempt_horizontal(dx, dz, true);
        // Axis-separated retries provide a stable wall slide without allowing
        // a diagonal step to tunnel through a corner. This is the remaining
        // neutral collision-response layer; aim, turn and step are ov022's.
        if (!accepted && dx != 0.0F && dz != 0.0F) {
            accepted = attempt_horizontal(dx, 0.0F, true)
                || attempt_horizontal(0.0F, dz, true);
        }
        if (accepted) {
            const float moved_x = state_.x - old_x;
            const float moved_z = state_.z - old_z;
            state_.facing = std::atan2(moved_x, moved_z);
            motion_x_ = moved_x;
            motion_z_ = moved_z;
        } else {
            motion_x_ = 0.0F;
            motion_z_ = 0.0F;
        }
    } else if (state_.locomotion == LocomotionPhase::Grounded) {
        // The idle branch clears nStepRate immediately rather than decaying it.
        step_rate_fx_ = 0;
        motion_x_ = 0.0F;
        motion_z_ = 0.0F;
    }

    if (state_.locomotion == LocomotionPhase::Grounded
        && input.just_pressed(Button::B)) {
        // DS B is the normal jump. func_ov022_02097d50 enters state 1 with a
        // three-frame anticipation clip and shares one fifth of ground motion.
        state_.locomotion = LocomotionPhase::JumpStart;
        phase_frame_fx_ = 0;
        vertical_velocity_fx_ = 0;
        motion_x_ = fx_mul(motion_x_, kJumpMotionShare);
        motion_z_ = fx_mul(motion_z_, kJumpMotionShare);
        state_.moving = false;
    } else if (state_.locomotion != LocomotionPhase::Grounded) {
        const bool in_air = state_.locomotion == LocomotionPhase::JumpStart
            || state_.locomotion == LocomotionPhase::Rising
            || state_.locomotion == LocomotionPhase::Falling;
        if (in_air) {
            if (state_.locomotion != LocomotionPhase::JumpStart
                && has_direction) {
                const std::uint16_t target_angle =
                    static_cast<std::uint16_t>(
                        aim_angle + binary_angle(state_.camera_yaw));
                motion_angle_ = clamp_motion_angle(
                    motion_angle_, target_angle);
                const float angle = static_cast<float>(motion_angle_)
                    * (kTau / 65536.0F);
                const float speed = static_cast<float>(kAirStepRate) * kInvFx;
                motion_x_ = -std::sin(angle) * speed;
                motion_z_ = -std::cos(angle) * speed;
                state_.facing = std::atan2(motion_x_, motion_z_);
                state_.moving = true;
            } else {
                motion_x_ = fx_mul(motion_x_, kMotionKeep);
                motion_z_ = fx_mul(motion_z_, kMotionKeep);
            }

            bool accepted = attempt_horizontal(motion_x_, motion_z_, false);
            if (!accepted && motion_x_ != 0.0F && motion_z_ != 0.0F) {
                accepted = attempt_horizontal(motion_x_, 0.0F, false)
                    || attempt_horizontal(0.0F, motion_z_, false);
            }
            if (!accepted) {
                motion_x_ = 0.0F;
                motion_z_ = 0.0F;
            }
        }

        phase_frame_fx_ += kNodeStepMove;
        if (state_.locomotion == LocomotionPhase::JumpStart
            && phase_frame_fx_ >= kJumpStartFrames) {
            state_.locomotion = LocomotionPhase::Rising;
            phase_frame_fx_ = 0;
            vertical_velocity_fx_ = kJumpVelocity;
        }

        if (state_.locomotion == LocomotionPhase::Rising
            || state_.locomotion == LocomotionPhase::Falling) {
            const auto ground = ground_probe(state_.x, state_.z);
            const float next_y = state_.y
                + static_cast<float>(vertical_velocity_fx_) * kInvFx;
            if (vertical_velocity_fx_ <= 0 && ground && next_y <= *ground) {
                state_.y = *ground;
                state_.locomotion = LocomotionPhase::Landing;
                phase_frame_fx_ = 0;
                vertical_velocity_fx_ = kFallVelocity;
                motion_x_ = 0.0F;
                motion_z_ = 0.0F;
                state_.moving = false;
            } else {
                state_.y = next_y;
                vertical_velocity_fx_ -= kGravity;
                if (vertical_velocity_fx_ < kFallVelocity) {
                    vertical_velocity_fx_ = kFallVelocity;
                }
                if (vertical_velocity_fx_ < 0) {
                    state_.locomotion = LocomotionPhase::Falling;
                }
            }
        } else if (state_.locomotion == LocomotionPhase::Landing
                   && phase_frame_fx_ >= kLandingFrames) {
            state_.locomotion = LocomotionPhase::Grounded;
            phase_frame_fx_ = 0;
            vertical_velocity_fx_ = 0;
            step_rate_fx_ = 0;
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
