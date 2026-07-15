#include "khdays/game/scenes/title_scene.h"

#include "khdays/game/draw.h"

namespace khdays::game::scenes {

namespace {
constexpr char kTitleFont[] = "text/font_eu_10all.nftr";
constexpr char kTitleTheme[] = "ThemeXIII";  // SDAT sequence for the title BGM
constexpr int kOptionBarRed = 2;   // res.p2 pack 1: red (selected) option bar
constexpr int kOptionBarGray = 6;  // res.p2 pack 1: gray (unselected) option bar
}  // namespace

void TitleScene::on_enter(SceneManager& manager) {
    // The title's two screens live in ttl.p2 sub-file 1 (a D2KP background pack):
    // screen 7 / tiles 3 / palette 0 = the KINGDOM HEARTS logo, and screen 3 /
    // tiles 1 / palette 1 = the character illustration.
    logo_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 7, 3, 0);
    illustration_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 3, 1, 1);
    bars_ = khdays::resource::load_sprite_set("UI/mlt/res.p2", 1);
    label_story_ = khdays::resource::render_ui_text(kTitleFont, u"MODO HISTORIA");
    label_mission_ = khdays::resource::render_ui_text(kTitleFont, u"MODO MISION");
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

    // MODO HISTORIA / MODO MISION on the bottom screen: the selected option on a
    // red bar, the other on a gray bar, with the label text on top.
    const khdays::assets::DecodedTexture* labels[2] = {
        label_story_ ? &*label_story_ : nullptr,
        label_mission_ ? &*label_mission_ : nullptr};
    for (int i = 0; i < 2; ++i) {
        const int bar_y = 150 + i * 20;
        if (bars_) {
            const int cell = (i == selected_) ? kOptionBarRed : kOptionBarGray;
            if (cell >= 0
                && static_cast<std::size_t>(cell) < bars_->cells.size()) {
                draw_overlay(r, layout, bars_->cells[cell], 4, bar_y,
                             /*bottom=*/true);
            }
        }
        if (labels[i] != nullptr) {
            draw_overlay(r, layout, *labels[i], 24, bar_y + 4, /*bottom=*/true);
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
