#include "khdays/game/scenes/boot_logo_scene.h"

#include "khdays/game/draw.h"
#include "khdays/resource/ui_content.h"

namespace khdays::game::scenes {

void BootLogoScene::on_enter(SceneManager&) {
    logo_ = khdays::resource::load_boot_logo();
}

void BootLogoScene::update(SceneManager& manager) {
    ++frames_;
    const auto& in = manager.input();
    if (in.just_pressed(Button::A) || in.just_pressed(Button::Start)
        || frames_ >= 180) {
        manager.change_scene(kSceneTitle);
    }
}

void BootLogoScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    if (logo_) {
        const int sw = r.width() / logo_->width;
        const int sh = r.height() / logo_->height;
        int scale = sw < sh ? sw : sh;
        if (scale < 1) {
            scale = 1;
        }
        draw_centered(r, *logo_, scale);
    }
}

}  // namespace khdays::game::scenes
