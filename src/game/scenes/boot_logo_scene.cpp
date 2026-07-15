#include "khdays/game/scenes/boot_logo_scene.h"

#include <algorithm>

#include "khdays/game/draw.h"
#include "khdays/resource/ui_content.h"

namespace khdays::game::scenes {

void BootLogoScene::on_enter(SceneManager&) {
    // The boot screens live in ttl.p2 sub-file 1 (a D2KP background pack). Top
    // screens are tiles 2 / palette 2; bottom screens tiles 0 / palette 0.
    top_[0] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 4, 2, 2);  // Disney
    top_[1] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 5, 2, 2);  // h.a.n.d.
    top_[2] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 6, 2, 2);  // legal
    bottom_[0] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 0, 0, 0);  // Square Enix
    bottom_[1] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 1, 0, 0);  // MobiClip
    bottom_[2] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 2, 0, 0);  // Licensed by Nintendo
}

void BootLogoScene::update(SceneManager& manager) {
    ++frame_;
    const auto& in = manager.input();
    if (in.just_pressed(Button::A) || in.just_pressed(Button::Start)
        || frame_ >= kTotalFrames) {
        manager.change_scene(kSceneTitle);
    }
}

void BootLogoScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{255, 255, 255, 255});  // the boot logos sit on white
    const int pair = std::min(kPairs - 1, frame_ / kPairFrames);
    const int local = frame_ - pair * kPairFrames;
    const auto layout = dual_screen_layout(r);
    if (top_[pair]) {
        draw_screen(r, layout, *top_[pair], /*bottom=*/false);
    }
    if (bottom_[pair]) {
        draw_screen(r, layout, *bottom_[pair], /*bottom=*/true);
    }

    // Fade to/from white at the ends of each pair's window.
    int fade = 0;
    if (local < kFadeFrames) {
        fade = 255 * (kFadeFrames - local) / kFadeFrames;
    } else if (local >= kPairFrames - kFadeFrames) {
        fade = 255 * (local - (kPairFrames - kFadeFrames)) / kFadeFrames;
    }
    if (fade > 0) {
        r.fill_overlay(Color{255, 255, 255, static_cast<std::uint8_t>(fade)});
    }
}

}  // namespace khdays::game::scenes
