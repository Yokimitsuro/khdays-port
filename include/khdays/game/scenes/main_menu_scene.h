#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game::scenes {

// Scene 0x13 / 19 (ov08): the Mission Mode main menu. The DS ctor
// func_ov008_0204db2c brings up the display, loads UI/mlt/res.p2, and runs the
// character-select state (func_ov008_0204dd24 → cursor func_ov008_0204da6c).
// This native scene reproduces that content and interaction from the real
// decoded assets: the roster portraits (res.p2 pack 2), the cursor sprite
// (pack 1) and a background layer (pack 0), plus the real roster names
// (UI/mlt/mlt_<lang>.s.z) in the game font, with a movable cursor. (The DS OBJ
// cell engine func_02032xxx that positions the sprites is not ported.)
class MainMenuScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    static constexpr int kCols = 7;
    static constexpr int kColW = 32;
    static constexpr int kRowH = 70;
    static constexpr int kGridX = 16;
    static constexpr int kGridY = 26;
    static constexpr std::size_t kRosterMax = 14;
    static constexpr int kConfirmFadeFrames = 30;  // ov008 confirm fade (0x1e)

    void load_labels();

    std::optional<khdays::assets::DecodedTexture> background_;
    std::optional<khdays::resource::SpriteSet> roster_;
    std::optional<khdays::resource::SpriteSet> bars_;
    std::vector<std::size_t> portraits_;
    std::vector<std::optional<khdays::assets::DecodedTexture>> names_;
    std::optional<khdays::assets::DecodedTexture> header_;
    int selected_ = 0;
    int frame_ = 0;
    int confirm_fade_ = 0;  // counts down the confirm fade; 0 = not confirming
};

}  // namespace khdays::game::scenes
