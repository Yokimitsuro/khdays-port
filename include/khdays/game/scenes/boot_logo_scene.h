#pragma once

#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"

namespace khdays::game::scenes {

// Scene 1 (ov000): the boot/publisher logo. Shows the real logo image composed
// from ttl.p2, then advances to the title after a short delay or a key press —
// the native form of ov000 requesting scene 7.
class BootLogoScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    std::optional<khdays::assets::DecodedTexture> logo_;
    int frames_ = 0;
};

}  // namespace khdays::game::scenes
