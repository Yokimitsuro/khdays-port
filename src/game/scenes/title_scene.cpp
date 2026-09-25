#include "khdays/game/scenes/title_scene.h"

#include <algorithm>
#include <cstdlib>

#include "khdays/game/draw.h"
#include "khdays/game/settings.h"

namespace khdays::game::scenes {

namespace {

constexpr char kTitleTheme[] = "Title_BGM_PCM8";  // the title BGM (SDAT stream)

// The port has no save system yet, so neither the save-dependent CARGAR row
// (ctx loadAvailable) nor the cleared-save third root row
// (ctx extraOptionAvailable) exists.
constexpr bool kLoadAvailable = false;
constexpr bool kExtraOptionAvailable = false;

// Counter thresholds shared by func_ov000_0204e270 and func_ov000_0204e5b0.
constexpr int kFadeStart = 0x3c;
constexpr int kFadeEnd = 0x5c;
constexpr int kSettled = 0x79;  // where a skip or a page change leaves it

// func_ov000_0204cac0 / 0204cc90 object positions (20.12 in the ROM).
constexpr int kOptionX = 0;
constexpr std::array<int, 2> kOptionY{0x74, 0x90};
constexpr int kCursorX = 0x8;
constexpr std::array<int, 2> kCursorY{0x84, 0xa0};

constexpr auto kConfirm = static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(Button::A)
    | static_cast<std::uint16_t>(Button::Start));
constexpr auto kCancel = static_cast<std::uint16_t>(Button::B);

bool pressed_any(const Input& in, const std::uint16_t buttons) {
    return (in.pressed & buttons) != 0U;
}

}  // namespace

void TitleScene::on_enter(SceneManager& manager) {
    top_.load();
    // The bottom illustration is screen 3 / tiles 1 / palette 1; for the
    // non-English variants func_ov000_0204e0c8 streams ttl_&.p2 sub-file 2 over
    // its tiles from char offset 0x9000.
    illustration_ = language() == Language::English
        ? khdays::resource::load_ui_background("ttl/ttl.p2", 1, 3, 1, 1)
        : khdays::resource::load_ui_background(
              "ttl/ttl.p2", 1, 3, 1, 1,
              khdays::resource::CharacterPatch{
                  localized_path("ttl/ttl_&.p2"), 2U, 0x9000U});
    // func_ov000_0204cac0: ttl_&.p2 sub-file 1 when the region resource exists,
    // else (English, which has no ttl_en.p2) ttl.p2 sub-file 2.
    buttons_ = language() == Language::English
        ? khdays::resource::load_sprite_set("ttl/ttl.p2", 2)
        : khdays::resource::load_sprite_set(
              localized_path("ttl/ttl_&.p2").c_str(), 1);
    if (buttons_) {
        cursor_ = khdays::assets::Animator(buttons_->animations, 0);
    }
    if (auto* music = manager.music()) {
        music->play_music(kTitleTheme);
    }
}

int TitleScene::row_count(const int page) const {
    // func_ov000_0204d244: data_ov000_0205a6b0 = {2, 2, 2}, with 3 rows on the
    // root page when the extra option exists and 1 on the story page without
    // a save.
    if (page == 0) {
        return kExtraOptionAvailable ? 3 : 2;
    }
    if (page == 1) {
        return kLoadAvailable ? 2 : 1;
    }
    return 2;
}

std::array<int, 2> TitleScene::page_objects() const {
    // func_ov000_0204cc90 without the extra option: page 0 shows objects 1/2,
    // a sub-page 3/4 (4 only with a save) or, in the alternate layout, 8/9.
    if (page_ == 0) {
        return {1, 2};
    }
    if (alt_layout_) {
        return {8, 9};
    }
    return {3, kLoadAvailable ? 4 : -1};
}

void TitleScene::update(SceneManager& manager) {
    const Input& in = manager.input();
    // The pad sampler and the object manager run every frame whatever the
    // state; only the menu state consumes the repeat mask.
    const std::uint16_t repeated = repeat_.update(in);
    cursor_.tick();
    switch (state_) {
        case State::Intro:
            update_intro(in);
            break;
        case State::Menu:
            update_menu(in, repeated);
            break;
        case State::PageChange:
            update_page_change();
            break;
        case State::Leave:
            update_leave(manager);
            break;
    }
}

void TitleScene::update_intro(const Input& in) {
    const int last_frame = std::max(0, top_.logo_frames() - 1);
    // func_ov000_0204d338: a pressed A or Start skips to the settled end.
    if (pressed_any(in, kConfirm)) {
        counter_ = kSettled;
        logo_frame_ = last_frame;
        sub_brightness_ = 0;
        obj_weight_ = 16;
        state_ = State::Menu;
        return;
    }
    const int c = counter_;
    logo_frame_ = std::min(c, last_frame);
    if (c < kFadeStart) {
        sub_brightness_ = 0x10;
    } else if (c <= kFadeEnd) {
        sub_brightness_ = 0x10 - (c - kFadeStart) / 2;
        obj_weight_ = (c - kFadeStart) / 2;
    } else {
        sub_brightness_ = 0;
        obj_weight_ = 16;
    }
    // After drawing frame c the logo advances one frame; the state ends once
    // that reaches the animation's last frame (func_0202a928), without
    // counting further.
    if (c + 1 >= last_frame) {
        state_ = State::Menu;
        return;
    }
    ++counter_;
}

void TitleScene::update_menu(const Input& in, const std::uint16_t repeated) {
    // Cursor input waits until no key is held (the pad's held mask is 0).
    if (input_ready_) {
        const int rows = row_count(page_);
        int row = cursor_row_[static_cast<std::size_t>(page_)];
        if ((repeated & static_cast<std::uint16_t>(Button::Up)) != 0U) {
            --row;
        }
        if ((repeated & static_cast<std::uint16_t>(Button::Down)) != 0U) {
            ++row;
        }
        if (row < 0) {
            row = rows - 1;
        }
        if (row >= rows) {
            row = 0;
        }
        cursor_row_[static_cast<std::size_t>(page_)] = row;
    } else if (in.down == 0U) {
        input_ready_ = true;
    }

    if (counter_ < kFadeStart) {
        sub_brightness_ = 0x10;
        ++counter_;
    } else if (counter_ <= kFadeEnd) {
        sub_brightness_ = 0x10 - (counter_ - kFadeStart) / 2;
        obj_weight_ = (counter_ - kFadeStart) / 2;
        ++counter_;
    } else {
        sub_brightness_ = 0;
        obj_weight_ = 16;
    }

    const int row = cursor_row_[static_cast<std::size_t>(page_)];
    if (pressed_any(in, kConfirm)) {
        if (page_ == 0) {
            if (row == 0) {
                counter_ = 0;
                if (kLoadAvailable) {
                    cursor_row_[1] = 1;
                }
                alt_layout_ = false;
                page_delta_ += 1;
                state_ = State::PageChange;
            } else if (row == 1) {
                counter_ = 0;
                alt_layout_ = true;
                page_delta_ = 2;
                state_ = State::PageChange;
            }
        } else {
            counter_ = 0;
            if (row == 0) {
                next_ = alt_layout_ ? Next::SaveFile : Next::NewGame;
                state_ = State::Leave;
            } else if (row == 1) {
                next_ = Next::SaveFile;
                state_ = State::Leave;
            }
        }
    } else if (pressed_any(in, kCancel) && page_ != 0) {
        page_delta_ -= 1;
        if (page_ > 1) {
            page_delta_ -= 1;
        }
        counter_ = 0;
        state_ = State::PageChange;
    }
}

void TitleScene::update_page_change() {
    ++counter_;
    if (counter_ == 4) {
        page_ += page_delta_;
        page_delta_ = 0;
    }
    if (counter_ <= 4) {
        obj_weight_ = 16 - counter_ * 4;
    } else if (counter_ < 8) {
        obj_weight_ = counter_ * 4 - 16;
    } else {
        counter_ = kSettled;
        state_ = State::Menu;
    }
}

void TitleScene::update_leave(SceneManager& manager) {
    if (counter_ <= 16) {
        sub_brightness_ = -counter_;
        ++counter_;
        return;
    }
    sub_brightness_ = -16;
    if (next_ == Next::SaveFile) {
        manager.change_scene(kSceneSaveFile);
        return;
    }
    // NUEVA PARTIDA continues into the difficulty select (func_ov000_0204f610)
    // and, once one is confirmed (which raises ctx+0x4c40), its hand-off fade
    // func_ov000_0204ebe4 -- neither is ported yet. They end in
    // func_ov000_0204ef34, which for the story layout runs
    // func_ov000_0204ee24: it reads the day counter (func_020235d0(0, 9)),
    // and 0x191 -- what func_ov000_02054c50 leaves for a new game -- clears
    // the mission descriptor, sets mission 10000 once ov028's checks pass and
    // requests scene 11, the ov012 opening; any other day goes to scene 5
    // (ov004) with the day as its argument.
    const std::uint32_t day = manager.state().day();
    auto& session = manager.mission_session();
    if (day == 0x191U) {
        session.reset_word = 0U;
        session.state = 0U;
        session.mission_id = 0x2710U;
        manager.change_scene(kSceneOpening, 0);
    } else {
        manager.change_scene(kSceneDayTransition, static_cast<int>(day));
    }
}

void TitleScene::draw_object(Renderer& r, const DualScreenLayout& layout,
                             const int cell, const int x, const int y,
                             const int alpha) const {
    if (!buttons_ || cell < 0
        || static_cast<std::size_t>(cell) >= buttons_->cells.size()) {
        return;
    }
    const auto& origin = buttons_->cell_origins[static_cast<std::size_t>(cell)];
    draw_overlay(r, layout, buttons_->cells[static_cast<std::size_t>(cell)],
                 x + origin[0], y + origin[1], /*bottom=*/true, alpha);
}

void TitleScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);

    // Top: during the intro BG1 is off until the counter reaches 0x3c, then
    // its palette walks back from white; afterwards it stays at level 0.
    const bool intro = state_ == State::Intro;
    const bool show_bg = !intro || counter_ >= kFadeStart;
    const int level = !intro || counter_ < kFadeStart
        ? 0
        : std::max(0, 0x10 - (counter_ - kFadeStart) / 2);
    draw_screen_dynamic(r, layout, top_.compose(logo_frame_, show_bg, level),
                        /*bottom=*/false);

    // Bottom: the illustration (BG1), then the OBJ layer blended over it by
    // the sub engine's BLDALPHA weight, then the master brightness.
    if (illustration_) {
        draw_screen(r, layout, *illustration_, /*bottom=*/true);
    }
    const int obj_alpha = std::clamp(obj_weight_, 0, 16) * 255 / 16;
    if (buttons_ && obj_alpha > 0) {
        const auto objects = page_objects();
        const int row = cursor_row_[static_cast<std::size_t>(page_)];
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const int object = objects[i];
            if (object < 0
                || static_cast<std::size_t>(object)
                       >= buttons_->animations.animations.size()) {
                continue;
            }
            const auto& steps =
                buttons_->animations.animations[static_cast<std::size_t>(object)]
                    .steps;
            const std::size_t frame = static_cast<int>(i) == row ? 1U : 0U;
            if (frame < steps.size()) {
                draw_object(r, layout, steps[frame].cell, kOptionX,
                            kOptionY[i], obj_alpha);
            }
        }
        const auto slot = static_cast<std::size_t>(std::clamp(row, 0, 1));
        draw_object(r, layout, cursor_.current_cell(), kCursorX,
                    kCursorY[slot], obj_alpha);
    }
    if (sub_brightness_ != 0) {
        // Master brightness: positive = toward white, negative = black.
        const int amount = std::min(16, std::abs(sub_brightness_));
        const auto a = static_cast<std::uint8_t>(amount * 255 / 16);
        const std::uint8_t shade = sub_brightness_ > 0 ? 255U : 0U;
        draw_screen_fill(r, layout, /*bottom=*/true, shade == 255U, a);
    }
}

}  // namespace khdays::game::scenes
