#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game::scenes {

// The Mission Mode character-select — runtime-confirmed as **scene 7 = ov06**
// (a DeSmuME savestate at this screen reads curId=7). ov06's char-select
// manager (ctor arm9_ov006::0205454c) loads `UI/mlt/res.p2` (+ localized
// `res_&.p2`).
//
// The bottom screen is a grid of **13 portraits of 24x64**, read from the live
// sub-engine OAM. Each portrait is drawn as two stacked OBJ sprites (a 24x32
// top half at Y and a 24x32 bottom half at Y+32, same X, tile numbers 6 apart),
// laid out in two horizontally-centered rows:
//
//   row 0: 7 portraits, X = 44 + col*24, Y = 32
//   row 1: 6 portraits, X = 56 + col*24, Y = 96
//
// res.p2 pack 2 holds the portraits twice: cells 5..23 are the 19 colour
// versions and cells 24..42 the same 19 in greyscale (cell 43 is a "?"
// placeholder). The grid draws every portrait **greyscale except the selected
// one, which is colour** — confirmed in the OAM, where the selected slot's
// tiles sit in a different range from all the others.
class MainMenuScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    // res.p2 pack 2: the first colour portrait, the first greyscale one, and how
    // many the grid shows.
    static constexpr std::size_t kPortraitColour0 = 5;
    static constexpr std::size_t kPortraitGrey0 = 24;
    static constexpr int kRosterMax = 13;          // the grid's 7 + 6 slots
    static constexpr int kConfirmFadeFrames = 30;  // confirm fade (0x1e)

    void load_labels();

    std::optional<khdays::assets::DecodedTexture> background_;
    std::optional<khdays::resource::SpriteSet> roster_;
    std::optional<khdays::resource::SpriteSet> bars_;
    std::vector<std::optional<khdays::assets::DecodedTexture>> names_;
    std::optional<khdays::assets::DecodedTexture> header_;
    int roster_count_ = 0;  // how many slots we actually have portraits for
    int selected_ = 0;
    int frame_ = 0;
    int confirm_fade_ = 0;  // counts down the confirm fade; 0 = not confirming
};

}  // namespace khdays::game::scenes
