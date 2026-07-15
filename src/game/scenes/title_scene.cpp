#include "khdays/game/scenes/title_scene.h"

#include "khdays/game/draw.h"

namespace khdays::game::scenes {

namespace {
constexpr char kTitleFont[] = "text/font_eu_10.nftr";
constexpr char kTitleTheme[] = "ThemeXIII";  // SDAT sequence for the title BGM
}  // namespace

void TitleScene::on_enter(SceneManager& manager) {
    title_ = khdays::resource::render_ui_text(kTitleFont, u"KINGDOM HEARTS");
    subtitle_ = khdays::resource::render_ui_text(kTitleFont, u"358/2 Days");
    prompt_ = khdays::resource::render_ui_text(kTitleFont, u"Press Start");
    // The title/menu OBJ sprites (ttl.p2 sub-file 2) plus their NANR animations;
    // the port plays one natively through the Animator so the graphics move.
    sprites_ = khdays::resource::load_sprite_set("ttl/ttl.p2", 2);
    if (sprites_) {
        emblem_ = khdays::assets::Animator(sprites_->animations, 0);
    }
    if (auto* music = manager.music()) {
        music->play_music(kTitleTheme);
    }
}

void TitleScene::update(SceneManager& manager) {
    emblem_.tick(1);  // advance the animation one game frame
    // Title's top state (ov06 func_ov006_02056154) polls Start
    // (func_ov006_02051f6c) and on press requests scene 0x13 (the main menu).
    const auto& in = manager.input();
    if (in.just_pressed(Button::Start) || in.just_pressed(Button::A)) {
        manager.change_scene(kSceneMainMenu);
    }
}

void TitleScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{12, 20, 40, 255});
    if (title_) {
        draw_centered(r, *title_, 5, -70);
    }
    if (subtitle_) {
        draw_centered(r, *subtitle_, 4, 0);
    }
    // The live NANR sprite (a small animated emblem), scaled up.
    if (sprites_) {
        const int cell = emblem_.current_cell();
        if (cell >= 0
            && static_cast<std::size_t>(cell) < sprites_->cells.size()) {
            draw_centered(r, sprites_->cells[cell], 4, 60);
        }
    }
    if (prompt_) {
        draw_centered(r, *prompt_, 2, 120);
    }
}

}  // namespace khdays::game::scenes
