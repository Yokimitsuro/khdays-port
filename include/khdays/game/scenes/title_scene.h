#pragma once

#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game::scenes {

// Scene 7 (ov06): the title screen, which is also the main menu. Two DS screens
// from ttl.p2: the KINGDOM HEARTS 358/2 Days logo on top, the character
// illustration on the bottom with the MODO HISTORIA / MODO MISION options
// (selected one on a red bar). Up/Down move the cursor; confirming MODO MISION
// goes to the Mission Mode menu (scene 0x13). Plays the title theme.
class TitleScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    std::optional<khdays::assets::DecodedTexture> logo3d_;        // top: white+logo
    std::optional<khdays::assets::DecodedTexture> illustration_;  // bottom screen
    std::optional<khdays::resource::SpriteSet> buttons_;  // localized option textures
    int selected_ = 0;  // 0 = MODO HISTORIA, 1 = MODO MISION
    int frame_ = 0;     // for the fade-in
};

}  // namespace khdays::game::scenes
