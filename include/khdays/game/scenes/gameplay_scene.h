#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "khdays/assets/animation.h"
#include "khdays/assets/collision.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"
#include "khdays/game/playable_controller.h"
#include "khdays/game/scene.h"
#include "khdays/resource/loader.h"
#include "khdays/resource/world.h"

namespace khdays::game::scenes {

// Scene 2 (ov002): the first playable technical slice. It deliberately keeps
// its objective separate from canonical mission logic: the room, collision,
// Roxas model and animation are real game resources, while the reach-the-marker
// goal is a development harness proving they work together natively.
class GameplayScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    std::optional<float> ground_height(float x, float z) const;
    void update_animation();

    std::vector<khdays::resource::RoomModel> room_;
    std::optional<khdays::resource::LoadedModel> player_;
    std::map<std::string, khdays::assets::DecodedTexture> player_textures_;
    std::optional<khdays::resource::LoadedModel> weapon_;
    std::map<std::string, khdays::assets::DecodedTexture> weapon_textures_;
    std::optional<khdays::assets::SkeletalAnimation> idle_animation_;
    std::optional<khdays::assets::SkeletalAnimation> walk_animation_;
    khdays::assets::CollisionModel collision_;
    khdays::assets::NeutralModel goal_model_;
    khdays::assets::DecodedTexture scene_frame_;

    std::optional<khdays::assets::DecodedTexture> controls_text_;
    std::optional<khdays::assets::DecodedTexture> complete_text_;
    std::optional<khdays::assets::DecodedTexture> error_text_;
    std::array<float, 16> weapon_bone_transform_{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};

    PlayableController controller_{0.0F, 5.0F, 0.0F, 8.0F,
                                   3.14159265358979323846F};
    float animation_frame_ = 0.0F;
    int frame_ = 0;
    bool animation_was_moving_ = false;
    bool weapon_attached_ = false;
    bool ready_ = false;
};

}  // namespace khdays::game::scenes
