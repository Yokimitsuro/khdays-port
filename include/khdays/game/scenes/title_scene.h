#pragma once

#include <optional>

#include "khdays/assets/cell.h"  // Animator
#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game::scenes {

// Scene 7 (ov06): the title screen. Draws the game font title text and a live
// NANR-animated emblem, plays the title theme, and on Start requests the main
// menu (scene 0x13) — mirroring ov06's top state polling Start.
class TitleScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    std::optional<khdays::assets::DecodedTexture> title_;
    std::optional<khdays::assets::DecodedTexture> subtitle_;
    std::optional<khdays::assets::DecodedTexture> prompt_;
    std::optional<khdays::resource::SpriteSet> sprites_;
    khdays::assets::Animator emblem_;
};

}  // namespace khdays::game::scenes
