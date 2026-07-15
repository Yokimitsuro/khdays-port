#pragma once

#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"

namespace khdays::game::scenes {

// Scene 2 (ov002): gameplay. Both Story and Mission mode run here — the menu
// (ov008) assembles a config and, on confirm (action 8), switches to this scene
// (func_ov008_0204dc48 → func_02020a78(2, 0)). Porting the actual game is a
// later phase, so this is the recognisable game-owned state the boot→title→menu
// flow reaches (the Phase 4 exit condition): it fades in and shows a placeholder
// marker. The scene arg carries the menu's selection until the config is ported.
class GameplayScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    std::optional<khdays::assets::DecodedTexture> marker_;
    int frame_ = 0;
};

}  // namespace khdays::game::scenes
