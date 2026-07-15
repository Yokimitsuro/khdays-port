#include "khdays/game/scenes/title_scene.h"

#include "khdays/game/draw.h"

namespace khdays::game::scenes {

namespace {
constexpr char kTitleTheme[] = "Title_BGM_PCM8";  // the title BGM (SDAT stream)
// Localized option textures in ttl_<lang>.p2 sub-file 1 (red = selected):
// cell 4/5 = MODO HISTORIA, cell 6/7 = MODO MISION.
constexpr int kBtnStorySel = 4;
constexpr int kBtnStoryOff = 5;
constexpr int kBtnMissionSel = 6;
constexpr int kBtnMissionOff = 7;
}  // namespace

void TitleScene::on_enter(SceneManager& manager) {
    // The title's two screens live in ttl.p2 sub-file 1 (a D2KP background pack):
    // screen 7 / tiles 3 / palette 0 = the KINGDOM HEARTS logo, and screen 3 /
    // tiles 1 / palette 1 = the character illustration. The MODO HISTORIA / MODO
    // MISION options are the real localized OBJ textures from ttl_<lang>.p2.
    logo_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 7, 3, 0);
    illustration_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 3, 1, 1);
    buttons_ = khdays::resource::load_sprite_set("ttl/ttl_es.p2", 1);
    if (auto* music = manager.music()) {
        music->play_music(kTitleTheme);
    }
}

void TitleScene::update(SceneManager& manager) {
    ++frame_;
    const auto& in = manager.input();
    if (in.just_pressed(Button::Down)) {
        selected_ = 1;
    }
    if (in.just_pressed(Button::Up)) {
        selected_ = 0;
    }
    if (in.just_pressed(Button::A) || in.just_pressed(Button::Start)) {
        // MODO MISION → the Mission Mode menu (scene 0x13, ov08). MODO HISTORIA
        // (New Game / Load) is a later scene, so it stays for now.
        if (selected_ == 1) {
            manager.change_scene(kSceneMainMenu);
        }
    }
}

void TitleScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);

    if (logo_) {
        draw_screen(r, layout, *logo_, /*bottom=*/false);
    }
    if (illustration_) {
        draw_screen(r, layout, *illustration_, /*bottom=*/true);
    }

    // MODO HISTORIA / MODO MISION on the bottom screen: the real localized OBJ
    // textures, red for the selected option and gray for the other.
    if (buttons_) {
        const int cells[2] = {
            selected_ == 0 ? kBtnStorySel : kBtnStoryOff,
            selected_ == 1 ? kBtnMissionSel : kBtnMissionOff};
        for (int i = 0; i < 2; ++i) {
            const int cell = cells[i];
            if (cell >= 0
                && static_cast<std::size_t>(cell) < buttons_->cells.size()) {
                // Bottom-left, both buttons fully on-screen (24px tall each).
                draw_overlay(r, layout, buttons_->cells[cell], 0, 142 + i * 24,
                             /*bottom=*/true);
            }
        }
    }

    // Fade in from white on entry (the title enters by a fade in the game).
    constexpr int kFadeIn = 24;
    if (frame_ < kFadeIn) {
        const int a = 255 * (kFadeIn - frame_) / kFadeIn;
        r.fill_overlay(Color{255, 255, 255, static_cast<std::uint8_t>(a)});
    }
}

}  // namespace khdays::game::scenes
