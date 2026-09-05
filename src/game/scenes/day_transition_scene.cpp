#include "khdays/game/scenes/day_transition_scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "khdays/game/draw.h"
#include "khdays/game/settings.h"

namespace khdays::game::scenes {

namespace {

// ov004 uses OS ticks (523,656 Hz): 0x11942b = 2.2 s and 0x6646d =
// 0.8 s. Its first state is a sixteen-step master-brightness fade.
constexpr int kFadeInFrames = 16;
constexpr int kSetupFrames = 1;
constexpr int kHoldFrames = 132;
constexpr int kFadeOutFrames = 48;
constexpr int kCompleteFrames = 1;
constexpr int kFadeOutStart = kFadeInFrames + kSetupFrames + kHoldFrames;
constexpr int kTotalFrames =
    kFadeOutStart + kFadeOutFrames + kCompleteFrames;

float ornament_scale(const int motion_frames) {
    int value = 0;
    int step = 0x171;
    for (int frame = 0; frame < motion_frames && value < 0x1000; ++frame) {
        value = std::min(0x1000, value + step);
        step = step > 0x14 ? step - 0x10 : 0x14;
    }
    return static_cast<float>(value) / 4096.0F;
}

}  // namespace

void DayTransitionScene::on_enter(SceneManager& manager) {
    requested_day_ = manager.current_arg();
    // func_ov004_0204fa44 + func_ov004_02050174: these are control
    // selectors. 0x190 draws 255 but keeps 400 as the mission script selector;
    // 0x191 starts at 255 and commits 7 when the calendar finishes.
    selected_day_ = requested_day_ == 0x191 ? 7 : requested_day_;
    displayed_day_ = (requested_day_ == 0x190 || requested_day_ == 0x191)
                         ? 0xff
                         : std::clamp(requested_day_, 0, 999);
    digits_ = khdays::resource::load_calendar_digits();
    ornaments_ = khdays::resource::load_sprite_container(
        localized_path("UI/cal/cl_hrt_&.pobj.z").c_str());
    if (ornaments_) {
        for (std::size_t i = 0; i < ornament_animators_.size(); ++i) {
            ornament_animators_[i] =
                khdays::assets::Animator(ornaments_->animations, i);
        }
    }
    if (auto* music = manager.music()) {
        music->stop_music();
    }
}

void DayTransitionScene::update(SceneManager& manager) {
    ++frame_;
    for (auto& animator : ornament_animators_) {
        animator.tick();
    }
    if (frame_ >= kTotalFrames) {
        // func_ov004_0204fcb4 commits the selected day, resets the boot-mode
        // state and requests scene 2 with argument zero.
        manager.state().set_day(static_cast<std::uint32_t>(selected_day_));
        auto& session = manager.mission_session();
        session.mission_id = selected_day_ == 0x165 ? 0x2711U : 0x2710U;
        session.reset_word = 0U;
        session.state = 0U;
        manager.change_scene(kSceneGameplay, 0);
    }
}

void DayTransitionScene::draw_sprite(
    Renderer& renderer, const DualScreenLayout& layout, const int object,
    const int x, const int y, const float scale) const {
    if (!ornaments_ || scale <= 0.0F || object < 0
        || static_cast<std::size_t>(object) >= ornament_animators_.size()) {
        return;
    }
    int cell = ornament_animators_[static_cast<std::size_t>(object)].current_cell();
    if (cell < 0) {
        cell = object;
    }
    if (cell < 0 || static_cast<std::size_t>(cell) >= ornaments_->cells.size()) {
        return;
    }
    const auto& image = ornaments_->cells[static_cast<std::size_t>(cell)];
    const auto origin = ornaments_->cell_origins[static_cast<std::size_t>(cell)];
    const int width = std::max(1, static_cast<int>(std::lround(
                                      image.width * scale * layout.scale)));
    const int height = std::max(1, static_cast<int>(std::lround(
                                       image.height * scale * layout.scale)));
    const int px = layout.bottom_x
        + static_cast<int>(std::lround((x + origin[0] * scale) * layout.scale));
    const int py = layout.bottom_y
        + static_cast<int>(std::lround((y + origin[1] * scale) * layout.scale));
    renderer.draw_image(image.rgba.data(), image.width, image.height,
                        px, py, width, height);
}

void DayTransitionScene::render(SceneManager&, Renderer& renderer) {
    renderer.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(renderer);

    // GX_SetDispSelect(1) puts ov004's main-engine calendar on the bottom LCD;
    // the top LCD remains black.
    if (digits_) {
        const std::string value = std::to_string(displayed_day_);
        // func_ov004_020506cc + 02050934 project the three 16x32 digit
        // textures to about 4x8 pixels, centred at x=131/135/139, y=89.
        const int first_x = 139 - static_cast<int>(value.size() - 1U) * 4;
        for (std::size_t i = 0; i < value.size(); ++i) {
            const int digit = value[i] - '0';
            if (digit < 0 || digit > 9) {
                continue;
            }
            const auto& image = (*digits_)[static_cast<std::size_t>(digit)];
            renderer.draw_image(
                image.rgba.data(), image.width, image.height,
                layout.bottom_x + (first_x + static_cast<int>(i) * 4 - 2)
                    * layout.scale,
                layout.bottom_y + 89 * layout.scale,
                4 * layout.scale, 8 * layout.scale);
        }
    }

    const int motion_frames = std::max(0, frame_ - kFadeInFrames);
    const int heart_y = std::min(138, 120 + motion_frames * 0xb36 / 0x1000);
    draw_sprite(renderer, layout, 0, 128, heart_y);
    draw_sprite(renderer, layout, 1, 128, 96,
                ornament_scale(motion_frames));
    draw_sprite(renderer, layout, 2, 100, 88);

    int black_alpha = 0;
    if (frame_ < kFadeInFrames) {
        black_alpha = (kFadeInFrames - frame_) * 255 / kFadeInFrames;
    } else if (frame_ >= kFadeOutStart) {
        black_alpha = (frame_ - kFadeOutStart) * 255
            / kFadeOutFrames;
    }
    if (black_alpha > 0) {
        renderer.fill_overlay(Color{
            0, 0, 0,
            static_cast<std::uint8_t>(std::clamp(black_alpha, 0, 255))});
    }
}

}  // namespace khdays::game::scenes
