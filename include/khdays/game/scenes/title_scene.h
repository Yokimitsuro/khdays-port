#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "khdays/assets/cell.h"  // Animator
#include "khdays/assets/tex0.h"
#include "khdays/game/input.h"   // KeyRepeat
#include "khdays/game/scene.h"
#include "khdays/game/scenes/title_top_screen.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game {
struct DualScreenLayout;  // defined in draw.h
}  // namespace khdays::game

namespace khdays::game::scenes {

// The title screen and its menu levels -- part of ov000 (scene 1) on the DS,
// which runs them as a chain of object states after the boot logos. Each state
// here is the port of one ov000 function, and the frame counter is its heap[0]:
//
//   Intro      func_ov000_0204e270  the 3D logo animates over white; from
//              counter 0x3c the 2D title emerges from white under it and the
//              bottom screen comes up from white. Ends when the logo animation
//              reaches its last frame, or at once on A/Start.
//   Menu       func_ov000_0204e5b0  Up/Down (with key repeat, wrapping) move
//              the cursor; A/Start confirm, B cancels.
//   PageChange func_ov000_0204e9a4  an 8-frame OBJ/BG1 cross-fade, swapping
//              the menu level on its 4th frame.
//   Leave      func_ov000_0204f51c / func_ov000_0204ef84  the bottom screen
//              fades to black over 17 frames and the next screen starts.
//
// Menu levels (page, and altLayout on page 2):
//   page 0  MODO HISTORIA / MODO MISION
//   page 1  NUEVA PARTIDA / CARGAR          (CARGAR only when a save exists)
//   page 2  UN JUGADOR / MULTIJUGADOR
// The options and the cursor are OBJ cells animated by the pack's NANR: object
// i plays animation i -- options rest on frame 0 (grey) or 1 (red, selected),
// the cursor (object 0) loops its own animation.
//
// Not reproduced yet, and left out rather than guessed: the menu sound effects
// (func_02033b78 ids 0 move, 1 confirm, 3 cancel -- the id-to-SDAT mapping is
// not traced), the 105-second attract hand-off to ov012, and the third root
// row that only a cleared save enables.
class TitleScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    enum class State { Intro, Menu, PageChange, Leave };
    enum class Next { NewGame, SaveFile };

    void update_intro(const Input& input);
    void update_menu(const Input& input, std::uint16_t repeated);
    void update_page_change();
    void update_leave(SceneManager& manager);
    // How many rows page `page` has (func_ov000_0204d244's table).
    int row_count(int page) const;
    // Object ids shown on the current page, top row first.
    std::array<int, 2> page_objects() const;
    // Draw OBJ cell `cell` positioned at (x, y) plus the cell's own origin.
    void draw_object(Renderer& r, const DualScreenLayout& layout, int cell,
                     int x, int y, int alpha) const;

    TitleTopScreen top_;
    std::optional<khdays::assets::DecodedTexture> illustration_;  // bottom BG1
    std::optional<khdays::resource::SpriteSet> buttons_;  // localized OBJ pack
    khdays::assets::Animator cursor_;  // object 0's looping animation

    State state_ = State::Intro;
    Next next_ = Next::NewGame;
    int counter_ = 0;     // ov000 heap[0]
    int logo_frame_ = 0;  // the logo animation frame on screen
    int page_ = 0;
    int page_delta_ = 0;  // pending level change, applied mid-fade
    std::array<int, 3> cursor_row_{};
    bool alt_layout_ = false;
    bool input_ready_ = false;
    // Sub-engine OBJ blend weight (0..16) and master brightness, as the
    // current state last set them.
    int obj_weight_ = 0;
    int sub_brightness_ = 0x10;
    KeyRepeat repeat_{30, 8};  // ov000's KeyRepeat{15, 4} in frame-rate mode 0
};

}  // namespace khdays::game::scenes
