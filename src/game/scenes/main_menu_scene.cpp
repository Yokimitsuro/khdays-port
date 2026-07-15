#include "khdays/game/scenes/main_menu_scene.h"

#include <algorithm>
#include <exception>

#include "khdays/game/draw.h"
#include "khdays/resource/loader.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::game::scenes {

namespace {
constexpr char kMenuFont[] = "text/font_eu_08.nftr";
constexpr char kMenuTheme[] = "Entrymulti";  // SDAT sequence for the menu BGM
}  // namespace

void MainMenuScene::on_enter(SceneManager& manager) {
    background_ = khdays::resource::load_ui_background("UI/mlt/res.p2", 0, 1, 0, 0);
    roster_ = khdays::resource::load_sprite_set("UI/mlt/res.p2", 2);
    bars_ = khdays::resource::load_sprite_set("UI/mlt/res.p2", 1);
    // The selectable portraits are the ~24x64 face cells of the roster pack; the
    // playable roster is the 14 members named in mlt_<lang> (strings 11..24),
    // which fill the 2x7 grid exactly.
    if (roster_) {
        for (std::size_t i = 0;
             i < roster_->cells.size() && portraits_.size() < kRosterMax; ++i) {
            const auto& c = roster_->cells[i];
            if (c.width >= 16 && c.width <= 40 && c.height >= 48
                && c.height <= 72) {
                portraits_.push_back(i);
            }
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
    const int n = static_cast<int>(portraits_.size());
    if (n > 0) {
        if (in.just_pressed(Button::Right)) {
            selected_ = (selected_ + 1) % n;
        }
        if (in.just_pressed(Button::Left)) {
            selected_ = (selected_ + n - 1) % n;
        }
        if (in.just_pressed(Button::Down)) {
            selected_ = std::min(n - 1, selected_ + kCols);
        }
        if (in.just_pressed(Button::Up)) {
            selected_ = std::max(0, selected_ - kCols);
        }
    }
    // B returns to the title (the menu's cancel path).
    if (in.just_pressed(Button::B)) {
        manager.change_scene(kSceneTitle);
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

    // Background layer (dark UI panel) scaled to the 256x192 virtual screen.
    if (background_) {
        r.draw_image(background_->rgba.data(), background_->width,
                     background_->height, ox, oy, 256 * s, 192 * s);
    }
    // Header ("Select a character.").
    if (header_) {
        blit(*header_, (256 - header_->width) / 2, 6);
    }

    // Roster portraits in a grid, cursor over the selected one.
    for (std::size_t i = 0; i < portraits_.size(); ++i) {
        const auto& cell = roster_->cells[portraits_[i]];
        const int col = static_cast<int>(i) % kCols;
        const int row = static_cast<int>(i) / kCols;
        const int px = kGridX + col * kColW + (kColW - cell.width) / 2;
        const int py = kGridY + row * kRowH;
        blit(cell, px, py);
        if (static_cast<int>(i) == selected_ && bars_ && !bars_->cells.empty()) {
            // A cursor arrow bobbing above the selected portrait — the native
            // form of the ov008 cursor object whose frame tracks the highlighted
            // option (func_ov008_0204da6c).
            const int bob = (frame_ / 8) % 2;
            blit(bars_->cells[0], px + (cell.width - 16) / 2, py - 14 - bob);
        }
    }

    // Selected character's name on a highlight bar at the bottom.
    if (bars_ && bars_->cells.size() > 2) {
        blit(bars_->cells[2], (256 - bars_->cells[2].width) / 2, 168);
    }
    if (selected_ >= 0 && selected_ < static_cast<int>(names_.size())
        && names_[selected_]) {
        blit(*names_[selected_], (256 - names_[selected_]->width) / 2, 172);
    }
}

void MainMenuScene::load_labels() {
    const auto table = khdays::vfs::resolve("UI/mlt/mlt_en.s.z");
    if (!table) {
        return;
    }
    try {
        const auto strings = khdays::resource::load_string_table(*table);
        // Roster names live at strings[11..24] (Xemnas..Xion); the header
        // "Select a character." is string 51.
        for (std::size_t i = 0; i < portraits_.size(); ++i) {
            const std::size_t si = 11 + i;
            names_.push_back(
                si < strings.size()
                    ? khdays::resource::render_ui_text(kMenuFont, strings[si])
                    : std::nullopt);
        }
        if (strings.size() > 51) {
            header_ = khdays::resource::render_ui_text(kMenuFont, strings[51]);
        }
    } catch (const std::exception&) {
    }
}

}  // namespace khdays::game::scenes
