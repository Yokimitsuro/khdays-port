#pragma once

#include <array>
#include <optional>

#include "khdays/assets/cell.h"
#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"

namespace khdays::game {
struct DualScreenLayout;
}

namespace khdays::game::scenes {

// Scene 5 (ov004): the calendar interstitial that commits a selected story day
// and hands it to scene 2. 0x190 enters the day-255 prologue through selector
// 400; 0x191 is the second control value and changes selector 255 into day 7.
class DayTransitionScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    void draw_sprite(Renderer& renderer, const DualScreenLayout& layout,
                     int object, int x, int y, float scale = 1.0F) const;

    std::optional<std::array<khdays::assets::DecodedTexture, 10>> digits_;
    std::optional<khdays::resource::SpriteSet> ornaments_;
    std::array<khdays::assets::Animator, 3> ornament_animators_;
    int requested_day_ = 0;
    int selected_day_ = 0;
    int displayed_day_ = 0;
    int frame_ = 0;
};

}  // namespace khdays::game::scenes
