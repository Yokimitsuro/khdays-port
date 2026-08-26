#include "khdays/game/scenes/gameplay_scene.h"

#include "khdays/game/draw.h"
#include "khdays/resource/ui_content.h"

namespace khdays::game::scenes {

namespace {
constexpr char kFont[] = "text/font_eu_10all.nftr";
constexpr int kFadeIn = 30;
}  // namespace

void GameplayScene::on_enter(SceneManager&) {
    // A placeholder marker for the not-yet-ported gameplay overlay.
    marker_ = khdays::resource::render_ui_text(kFont, u"GAMEPLAY (ov002)");
}

void GameplayScene::update(SceneManager& manager) {
    ++frame_;
    // Cancel back to the title for now (real gameplay is a later phase).
    if (manager.input().just_pressed(Button::B)) {
        manager.change_scene(kSceneTitle);
    }
}

void GameplayScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    if (marker_) {
        draw_centered(r, *marker_, 2);
    }
    if (frame_ < kFadeIn) {
        const int a = 255 * (kFadeIn - frame_) / kFadeIn;
        r.fill_overlay(Color{0, 0, 0, static_cast<std::uint8_t>(a)});
    }
}

}  // namespace khdays::game::scenes
