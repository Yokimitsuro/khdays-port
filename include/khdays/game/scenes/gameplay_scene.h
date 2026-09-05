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
#include "khdays/resource/ui_content.h"
#include "khdays/resource/world.h"

namespace khdays::game::scenes {

// Scene 2 (ov002): story mission entry plus the first playable technical slice.
// Mission 10000 follows its named day bundles and MobiClips; after the currently
// reconstructed script prefix, the room/collision/Roxas reach-the-marker goal
// remains a deliberately separate development harness.
class GameplayScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;
    void on_exit(SceneManager& manager) override;

private:
    void load_playable_harness();
    void finish_story_movie(SceneManager& manager);
    std::optional<float> ground_height(float x, float z) const;
    void update_animation();
    void update_battle_hud();

    std::vector<khdays::resource::RoomModel> room_;
    std::optional<khdays::resource::LoadedModel> player_;
    std::map<std::string, khdays::assets::DecodedTexture> player_textures_;
    std::optional<khdays::resource::LoadedModel> weapon_;
    std::map<std::string, khdays::assets::DecodedTexture> weapon_textures_;
    std::optional<khdays::assets::SkeletalAnimation> idle_animation_;
    std::optional<khdays::assets::SkeletalAnimation> walk_animation_;
    std::optional<khdays::resource::BattleHudArtwork> battle_hud_;
    std::optional<khdays::assets::Ov002CommandMenuArtwork>
        command_menu_artwork_;
    khdays::assets::DecodedTexture player_gauge_;
    khdays::assets::DecodedTexture command_menu_;
    khdays::assets::CollisionModel collision_;
    khdays::assets::NeutralModel goal_model_;
    khdays::assets::DecodedTexture scene_frame_;
    VideoPlayer* video_player_ = nullptr;
    VideoFrame video_frame_{};

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
    int movie_exit_fade_ = 0;
    std::uint16_t hud_hp_ = 0xffffU;
    std::uint16_t hud_max_hp_ = 0xffffU;
    std::uint32_t story_day_ = 0U;
    std::optional<std::uint32_t> stored_day_after_movie_;
    std::optional<int> calendar_request_after_movie_;
    bool animation_was_moving_ = false;
    bool weapon_attached_ = false;
    bool ready_ = false;
    bool story_movie_ = false;
    bool movie_exiting_ = false;
};

}  // namespace khdays::game::scenes
