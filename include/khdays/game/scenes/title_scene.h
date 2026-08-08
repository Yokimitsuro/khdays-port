#pragma once

#include <cstddef>
#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game {
struct DualScreenLayout;  // defined in draw.h
}  // namespace khdays::game

namespace khdays::game::scenes {

// Scene 1 (ov000): the title screen — and the whole front-end menu. Two DS
// screens from ttl.p2: the top screen is the 2D background s7_t3_p3 (the
// Disney + SQUARE ENIX logos above the KINGDOM HEARTS 358/2 Days logo, per the
// data_ov000_0205a9d4 pairing), the character illustration (s3_t1_p1) on the
// bottom. The title hosts EVERY menu level: the option pair swaps in place at
// the same two slots, (0,116) and (0,144) (positions read from the live ov000
// sub-engine OAM, 28px row pitch), sliding in with the game's page-scroll ease:
//
//   MODO HISTORIA / MODO MISION
//     MODO HISTORIA -> NUEVA PARTIDA / CARGAR   (CARGAR only when a save exists)
//     MODO MISION   -> UN JUGADOR / MULTIJUGADOR
//
// Up/Down move the cursor, A/Start confirm, B goes back a level. The options are
// the real localized OBJ textures from ttl_<lang>.p2. Plays the title theme.
class TitleScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    // Which option pair the title is currently showing.
    enum class Level { Root, Story, Mission };

    // One option: its localized cell in ttl_<lang>.p2 when selected (red bar)
    // and when not (gray bar).
    struct Option {
        int selected;
        int normal;
    };

    // Fill `out` with the current level's options; returns how many there are.
    std::size_t options(Option* out) const;
    void confirm(SceneManager& manager);
    // Start the option block sliding in from `from` px; it eases to rest (0).
    void begin_page_slide(float from);
    // Draw the pulsing selection square over the selected option row at `row_y`.
    void draw_selection_cursor(Renderer& r, const DualScreenLayout& layout,
                               int page_dx, int row_y) const;

    std::optional<khdays::assets::DecodedTexture> top_;    // top screen BG (s7)
    std::optional<khdays::assets::DecodedTexture> logo_;   // KH 358/2 Days overlay
    std::optional<khdays::assets::DecodedTexture> illustration_;  // bottom screen
    std::optional<khdays::resource::SpriteSet> buttons_;  // localized option textures
    Level level_ = Level::Root;
    int selected_ = 0;
    int frame_ = 0;  // for the fade-in
    // Horizontal ease of the option block when the page/level changes. The DS
    // eases each menu page toward its resting position (func_ov000_02050ec4:
    // close a quarter of the gap per frame, snap below 1/8 px). The easing math
    // is the game's; the slide distance (one screen width) and direction are the
    // port's rendition -- the exact per-page start positions are not measured.
    float page_x_ = 0.0F;
};

}  // namespace khdays::game::scenes
