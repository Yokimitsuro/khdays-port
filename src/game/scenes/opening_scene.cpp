#include "khdays/game/scenes/opening_scene.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "khdays/game/draw.h"
#include "khdays/game/settings.h"

namespace khdays::game::scenes {

namespace {

// A compact semantic form of data_ov012_0205c310. Each row is one set of
// op.p2 planes and carries the exact VBlank deltas and durations used by the
// handler-3/handler-4 fades. The final card enables both planes together and
// has no localized third plane.
struct CardTiming final {
    int load;
    int base_in;
    int base_in_duration;
    int accent_in;
    int accent_in_duration;
    int accent_out;
    int accent_out_duration;
    int localized_in;
    int localized_in_duration;
    int card_out;
    int card_out_duration;
    bool combined;
};

constexpr std::array<CardTiming, 14> kTimeline{{
    {0x0efc, 0x0f00, 36, 0x0f24, 54, 0x10b8, 60, 0x110a, 50, 0x12dc, 60, false},
    {0x1318, 0x131c, 60, 0x1358, 72, 0x145c, 72, 0x14ae, 50, 0x15f4, 60, false},
    {0x1630, 0x1634, 60, 0x1670, 60, 0x1772, 60, 0x17c2, 50, 0x190c, 60, false},
    {0x1948, 0x194c, 60, 0x1988, 60, 0x1a7c, 60, 0x1acc, 50, 0x1c24, 60, false},
    {0x1c60, 0x1c64, 60, 0x1ca0, 60, 0x1d96, 60, 0x1de6, 50, 0x1f3e, 60, false},
    {0x1f7a, 0x1f7e, 60, 0x1fb0, 60, 0x20ae, 60, 0x20fe, 50, 0x2256, 60, false},
    {0x2292, 0x2296, 60, 0x22d2, 60, 0x23dc, 60, 0x2426, 50, 0x256e, 60, false},
    {0x25aa, 0x25ae, 60, 0x25ea, 60, 0x26ea, 60, 0x2738, 50, 0x2886, 60, false},
    {0x28c2, 0x28c6, 60, 0x2902, 60, 0x29fe, 60, 0x2a50, 50, 0x2b9e, 60, false},
    {0x2bda, 0x2bde, 60, 0x2c1a, 60, 0x2cf8, 60, 0x2d46, 50, 0x2eb6, 60, false},
    {0x2ef2, 0x2ef6, 60, 0x2f32, 60, 0x3040, 60, 0x308e, 50, 0x31ce, 60, false},
    {0x320a, 0x320e, 60, 0x324a, 60, 0x3336, 60, 0x3386, 50, 0x34ea, 60, false},
    {0x3a0a, 0x3a0e, 60, 0x3a4a, 60, 0x3b40, 60, 0x3b94, 60, 0x3cd8, 60, false},
    {0x413d, 0x4140, 70, 0x4140, 70, 0x42a0, 108, 0, 0, 0x42a0, 108, true},
}};

int language_subfile() {
    // DS firmware language values consumed directly by ov012/op.p2.
    switch (language()) {
    case Language::English: return 1;
    case Language::French: return 2;
    case Language::German: return 3;
    case Language::Italian: return 4;
    case Language::Spanish: return 5;
    }
    return 1;
}

int fade_step(const int frame, const int start, const int duration) {
    if (frame <= start) {
        return 0;
    }
    if (duration <= 0 || frame - start >= duration) {
        return 16;
    }
    return (frame - start) * 16 / duration;
}

int alpha_from_step(const int step) {
    return std::clamp(step, 0, 16) * 255 / 16;
}

void draw_solid_screen(Renderer& r, const DualScreenLayout& layout,
                       const bool bottom, const bool white, const int alpha = 255) {
    static constexpr std::uint8_t kWhite[4] = {255, 255, 255, 255};
    static constexpr std::uint8_t kBlack[4] = {0, 0, 0, 255};
    r.draw_image(white ? kWhite : kBlack, 1, 1, layout.screen_x(bottom),
                 layout.screen_y(bottom),
                 DualScreenLayout::kScreenW * layout.scale,
                 DualScreenLayout::kScreenH * layout.scale, alpha);
}

}  // namespace

void OpeningScene::on_enter(SceneManager& manager) {
    artwork_ = khdays::resource::load_opening_artwork(
        static_cast<std::size_t>(language_subfile()));
    if (auto* music = manager.music()) {
        music->stop_music();
    }
    player_ = manager.video();
    if (player_ != nullptr) {
        player_->play_video("mv/802.mods");
        playback_started_ = player_->video_playing();
    }
}

void OpeningScene::update(SceneManager& manager) {
    if (!playback_started_ || player_ == nullptr) {
        manager.change_scene(kSceneTitle);
        return;
    }

    if (manager.input().just_pressed(Button::Start) && !exiting_) {
        exiting_ = true;
        exit_fade_ = 0;
    }

    video_frame_ = player_->video_frame();
    ++timeline_frame_;

    if (exiting_) {
        if (++exit_fade_ >= 16) {
            manager.change_scene(kSceneTitle);
        }
        return;
    }
    if (!player_->video_playing()) {
        manager.change_scene(kSceneTitle);
    }
}

void OpeningScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);

    // ov012 clears POWCNT1's display-swap bit: main-engine op.p2 art is on the
    // top LCD, while ov024 is initialized only for the sub engine.
    draw_solid_screen(r, layout, /*bottom=*/false, /*white=*/true);
    draw_solid_screen(r, layout, /*bottom=*/true, /*white=*/false);

    if (artwork_) {
        std::size_t card_index = 0;
        bool found = false;
        for (std::size_t i = 0; i < kTimeline.size(); ++i) {
            if (timeline_frame_ >= kTimeline[i].load) {
                card_index = i;
                found = true;
            } else {
                break;
            }
        }
        if (found) {
            const auto& timing = kTimeline[card_index];
            const auto& base = artwork_->base[card_index];
            const auto& accent = artwork_->accent[card_index];
            if (timeline_frame_ >= timing.base_in) {
                draw_screen(r, layout, base, /*bottom=*/false);
                if (timing.combined) {
                    draw_screen(r, layout, accent, /*bottom=*/false);
                } else if (timeline_frame_ >= timing.accent_in
                           && timeline_frame_
                                  < timing.accent_out + timing.accent_out_duration) {
                    int accent_alpha = 255;
                    if (timeline_frame_
                        < timing.accent_in + timing.accent_in_duration) {
                        accent_alpha = alpha_from_step(fade_step(
                            timeline_frame_, timing.accent_in,
                            timing.accent_in_duration));
                    } else if (timeline_frame_ >= timing.accent_out) {
                        accent_alpha = 255 - alpha_from_step(fade_step(
                            timeline_frame_, timing.accent_out,
                            timing.accent_out_duration));
                    }
                    draw_screen(r, layout, accent, /*bottom=*/false,
                                accent_alpha);
                }

                if (!timing.combined && card_index < artwork_->localized.size()
                    && timeline_frame_ >= timing.localized_in) {
                    int localized_alpha = 255;
                    if (timeline_frame_
                        < timing.localized_in + timing.localized_in_duration) {
                        localized_alpha = alpha_from_step(fade_step(
                            timeline_frame_, timing.localized_in,
                            timing.localized_in_duration));
                    }
                    draw_screen(r, layout, artwork_->localized[card_index],
                                /*bottom=*/false, localized_alpha);
                }

                int white_alpha = 0;
                if (timeline_frame_ < timing.base_in + timing.base_in_duration) {
                    white_alpha = 255 - alpha_from_step(fade_step(
                        timeline_frame_, timing.base_in,
                        timing.base_in_duration));
                }
                if (timeline_frame_ >= timing.card_out) {
                    white_alpha = alpha_from_step(fade_step(
                        timeline_frame_, timing.card_out,
                        timing.card_out_duration));
                }
                if (white_alpha > 0) {
                    draw_solid_screen(r, layout, /*bottom=*/false,
                                      /*white=*/true, white_alpha);
                }
            }
        }
    }

    if (video_frame_.rgba != nullptr && video_frame_.width > 0
        && video_frame_.height > 0) {
        r.draw_image_dynamic(
            video_frame_.rgba, video_frame_.width, video_frame_.height,
            layout.bottom_x, layout.bottom_y,
            DualScreenLayout::kScreenW * layout.scale,
            160 * layout.scale);
    } else {
        // The original starts both master-brightness registers at -16; the sub
        // display becomes visible when ov024 reports its first frame.
        draw_solid_screen(r, layout, /*bottom=*/true, /*white=*/true);
    }

    if (exiting_) {
        const int alpha = std::clamp(exit_fade_, 0, 16) * 255 / 16;
        r.fill_overlay(Color{0, 0, 0, static_cast<std::uint8_t>(alpha)});
    }
}

void OpeningScene::on_exit(SceneManager&) {
    if (player_ != nullptr) {
        player_->stop_video();
    }
    player_ = nullptr;
    video_frame_ = {};
}

}  // namespace khdays::game::scenes
