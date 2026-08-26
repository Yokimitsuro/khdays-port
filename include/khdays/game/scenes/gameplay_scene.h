#pragma once

#include <optional>
#include <vector>

#include "khdays/assets/scene3d.h"
#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/world.h"

namespace khdays::game::scenes {

// Scene 2 (ov002): gameplay. Both Story and Mission mode run here — the menu
// (ov008) assembles a config and, on confirm (action 8), switches to this scene
// (func_ov008_0204dc48 → func_02020a78(2, 0)).
//
// This now draws a real stage room in 3D: a `mi/wd/wd_<code>` world archive is
// loaded through `khdays::resource::load_world_room` and rasterized by
// `khdays::assets::render_scene` into the top screen. It is a long way from the
// gameplay overlay — nothing here is the DS's own scene setup — but it is the
// first time the frame loop draws the game's own geometry rather than a
// placeholder.
//
// Three things are deliberately the port's own, not the game's, and are marked
// as such where they appear: which room is shown, how the camera is placed, and
// which sub-files count as the room's geometry.
class GameplayScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    std::optional<khdays::resource::LoadedRoom> room_;
    std::optional<khdays::assets::DecodedTexture> marker_;
    khdays::assets::DecodedTexture view_;
    float yaw_ = 0.6F;
    float pitch_ = 0.45F;
    int frame_ = 0;
};

}  // namespace khdays::game::scenes
