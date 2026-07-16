#include "khdays/game/scenes/main_menu_scene.h"

#include <algorithm>
#include <array>
#include <exception>

#include "khdays/game/draw.h"
#include "khdays/resource/loader.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::game::scenes {

namespace {
constexpr char kMenuFont[] = "text/font_eu_08.nftr";
constexpr char kMenuTheme[] = "Entrymulti";  // SDAT sequence for the menu BGM

// The character-select grid, read straight from the live ov06 sub-engine OAM:
// 13 portraits of 24x64 in two horizontally-centered rows (7 then 6), column
// pitch 24. Each portrait is two stacked OBJ sprites on the DS; here it is one
// 24x64 cell.
constexpr int kCellW = 24;
constexpr int kCellH = 64;
constexpr std::array<int, 2> kRowY{32, 96};
constexpr std::array<int, 2> kRowN{7, 6};
constexpr std::array<int, 2> kRowX0{44, 56};  // (256 - N*24) / 2
constexpr int kColPitch = 24;

// Break a linear slot index into its (row, col) in the 7 + 6 grid.
void slot_rowcol(int index, int& row, int& col) {
    row = 0;
    if (index >= kRowN[0]) {
        index -= kRowN[0];
        row = 1;
    }
    col = index;
}

int slot_index(int row, int col) {
    return row == 0 ? col : kRowN[0] + col;
}

void slot_origin(int index, int& x, int& y) {
    int row = 0;
    int col = 0;
    slot_rowcol(index, row, col);
    x = kRowX0[static_cast<std::size_t>(row)] + col * kColPitch;
    y = kRowY[static_cast<std::size_t>(row)];
}
}  // namespace

void MainMenuScene::on_enter(SceneManager& manager) {
    background_ = khdays::resource::load_ui_background("UI/mlt/res.p2", 0, 1, 0, 0);
    roster_ = khdays::resource::load_sprite_set("UI/mlt/res.p2", 2);
    bars_ = khdays::resource::load_sprite_set("UI/mlt/res.p2", 1);
    // Slot i uses pack-2 cell (kPortraitGrey0 + i), or (kPortraitColour0 + i)
    // when it is the selected one, so we need both to exist.
    if (roster_) {
        while (roster_count_ < kRosterMax
               && kPortraitGrey0 + static_cast<std::size_t>(roster_count_)
                      < roster_->cells.size()) {
            ++roster_count_;
        }
    }
    load_labels();
    if (auto* music = manager.music()) {
        music->play_music(kMenuTheme);
    }
}

void MainMenuScene::update(SceneManager& manager) {
    ++frame_;
    const auto& in = manager.input();

    // Confirm executor: a confirm starts a 30-frame fade, then enters gameplay
    // (scene 2 = ov002). The action code and save-slot config are still being
    // decompiled; the transition itself is matched.
    if (confirm_fade_ > 0) {
        if (--confirm_fade_ == 0) {
            manager.change_scene(kSceneGameplay, selected_);
        }
        return;
    }

    if (roster_count_ > 0) {
        int row = 0;
        int col = 0;
        slot_rowcol(selected_, row, col);
        if (in.just_pressed(Button::Right)) {
            selected_ = (selected_ + 1) % roster_count_;
        }
        if (in.just_pressed(Button::Left)) {
            selected_ = (selected_ + roster_count_ - 1) % roster_count_;
        }
        if (in.just_pressed(Button::Down) && row == 0) {
            const int nc = std::min(col, kRowN[1] - 1);
            selected_ = std::min(roster_count_ - 1, slot_index(1, nc));
        }
        if (in.just_pressed(Button::Up) && row == 1) {
            selected_ = slot_index(0, std::min(col, kRowN[0] - 1));
        }
    }
    if (in.just_pressed(Button::A) || in.just_pressed(Button::Start)) {
        confirm_fade_ = kConfirmFadeFrames;  // -> gameplay
    }
    if (in.just_pressed(Button::B)) {
        manager.change_scene(kSceneTitle);  // cancel -> title
    }
}

void MainMenuScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{8, 10, 24, 255});
    const int s = std::max(1, std::min(r.width() / 256, r.height() / 192));
    const int ox = (r.width() - 256 * s) / 2;
    const int oy = (r.height() - 192 * s) / 2;
    const auto blit = [&](const khdays::assets::DecodedTexture& t, int vx,
                          int vy) {
        r.draw_image(t.rgba.data(), t.width, t.height, ox + vx * s, oy + vy * s,
                     t.width * s, t.height * s);
    };

    // Background layer scaled to the 256x192 virtual screen.
    if (background_) {
        r.draw_image(background_->rgba.data(), background_->width,
                     background_->height, ox, oy, 256 * s, 192 * s);
    }
    // Header banner ("Select a character.") across the top.
    if (header_) {
        blit(*header_, (256 - header_->width) / 2, 4);
    }

    // The roster grid: every portrait greyscale except the selected one, which
    // is drawn from the colour half of pack 2 — the DS does exactly this.
    for (int i = 0; i < roster_count_; ++i) {
        const std::size_t cell =
            (i == selected_ ? kPortraitColour0 : kPortraitGrey0)
            + static_cast<std::size_t>(i);
        if (cell >= roster_->cells.size()) {
            continue;
        }
        int px = 0;
        int py = 0;
        slot_origin(i, px, py);
        blit(roster_->cells[cell], px, py);
    }

    // Fade to black while confirming (the 30-frame confirm fade).
    if (confirm_fade_ > 0) {
        const int a = 255 * (kConfirmFadeFrames - confirm_fade_) / kConfirmFadeFrames;
        r.fill_overlay(Color{0, 0, 0, static_cast<std::uint8_t>(a)});
    }
}

void MainMenuScene::load_labels() {
    const auto table = khdays::vfs::resolve("UI/mlt/mlt_en.s.z");
    if (!table) {
        return;
    }
    try {
        const auto strings = khdays::resource::load_string_table(*table);
        // The header "Select a character." is string 51.
        if (strings.size() > 51) {
            header_ = khdays::resource::render_ui_text(kMenuFont, strings[51]);
        }
    } catch (const std::exception&) {
    }
}

}  // namespace khdays::game::scenes
