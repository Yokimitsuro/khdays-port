#include "khdays/game/scenes/save_file_scene.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "khdays/assets/cell.h"
#include "khdays/game/draw.h"
#include "khdays/game/settings.h"
#include "khdays/resource/loader.h"

namespace khdays::game::scenes {

namespace {

// The screen's own resources, named by ov000's data (0x0205aab0 / 0x0205aaa0).
constexpr char kLayoutPath[] = "UI/cm/cm_save.ui.z";
constexpr char kSpritePack[] = "UI/cm/cmo_&.p2";
constexpr std::size_t kSpriteSubfile = 3;  // the sub-file ov000 binds here

// The three save rows and the guest bar, in the order the game's 4x8 id table
// lists them. Rows 0..2 take the 8px lift; row 3 (the guest bar) does not.
constexpr std::array<std::int32_t, 3> kSaveRowIds{1, 2, 3};

// Hidden on entry by FUN_arm9_ov000__0204fdac: two id tables (2 ids at
// 0x0205a6bc, 7 ids at 0x0205a6f4) plus ids 0x3c and 0x11 individually.
constexpr std::array<std::int32_t, 10> kHiddenIds{
    20, 21,                       // table at 0x0205a6bc
    11, 12, 13, 14, 15, 17, 61,   // table at 0x0205a6f4
    60,                           // id 0x3c
};

}  // namespace

bool SaveFileScene::hidden_on_entry(const std::int32_t id) {
    return std::find(kHiddenIds.begin(), kHiddenIds.end(), id)
        != kHiddenIds.end();
}

void SaveFileScene::on_enter(SceneManager&) {
    logo_ = khdays::resource::load_title_logo();
    layout_ = khdays::resource::load_ui_layout(kLayoutPath);
    sprites_ = khdays::resource::load_sprite_set(
        khdays::game::localized_path(kSpritePack).c_str(), kSpriteSubfile);
    selected_ = 0;
    frame_ = 0;
}

void SaveFileScene::update(SceneManager& manager) {
    ++frame_;
    const auto& in = manager.input();

    // The nav walker (func_ov000_020552b4) does not wrap within a group, so
    // moving off either end of the list does nothing.
    if (in.just_pressed(Button::Up) && selected_ > 0) {
        --selected_;
    }
    if (in.just_pressed(Button::Down)
        && selected_ + 1 < static_cast<int>(kSaveRowIds.size())) {
        ++selected_;
    }
    // The DS goes on to the character select once a file is chosen, and back to
    // the title on cancel. Which rows are selectable at all is a save-system
    // question the port cannot answer yet, so every row accepts.
    if (in.just_pressed(Button::A) || in.just_pressed(Button::Start)) {
        manager.change_scene(kSceneMainMenu);
    } else if (in.just_pressed(Button::B)) {
        manager.change_scene(kSceneTitle);
    }
}

void SaveFileScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);

    // The DS draws this on the touch screen, with the front-end's KH logo still
    // on the top one.
    if (logo_) {
        draw_screen(r, layout, *logo_, /*bottom=*/false);
    }
    if (!layout_ || !sprites_) {
        return;  // no game data: nothing invented to stand in for it
    }

    const auto blit = [&](const khdays::assets::DecodedTexture& t, int vx,
                          int vy) {
        draw_overlay(r, layout, t, vx, vy, /*bottom=*/true);
    };

    for (const auto& element : layout_->elements) {
        if (hidden_on_entry(element.id)) {
            continue;
        }
        const auto& key = element.keys_default[0];
        if (!key.has_value() || *key < 0) {
            continue;
        }
        const auto animation = static_cast<std::size_t>(*key);
        if (animation >= sprites_->animations.animations.size()) {
            continue;
        }
        const auto& steps = sprites_->animations.animations[animation].steps;
        if (steps.empty()) {
            continue;
        }
        // Draw the animation's first cell. Playing the steps needs to know what
        // each frame *means* here (several are per-row state, not motion), and
        // that is not established -- so this shows the resting frame rather
        // than animating something the game may never animate.
        const auto cell = static_cast<std::size_t>(steps.front().cell);
        if (cell >= sprites_->cells.size()) {
            continue;
        }

        float y = element.y;
        if (std::find(kSaveRowIds.begin(), kSaveRowIds.end(), element.id)
            != kSaveRowIds.end()) {
            y -= kRowLift;
        }
        // The DS positions a cell by its origin, not by its bitmap's corner,
        // so add the cell's own offset (negative for a cell whose pieces
        // straddle the origin -- the difficulty label is 64 wide at x=220 and
        // only fits the 256px screen once its offset is applied).
        const auto& origin = sprites_->cell_origins[cell];
        blit(sprites_->cells[cell],
             static_cast<int>(element.x) + origin[0],
             static_cast<int>(y) + origin[1]);
    }

    // Deliberately absent, because the game data does not say and nothing here
    // will guess:
    //   - what marks the selected row: the DS draws it blue, and a savestate
    //     OBJ-layer capture (tools/savestate_obj) settles where it comes from --
    //     the selected slot is NOT blue in the OBJ layer, so the highlight is a
    //     BG-layer effect, not a plate cell or palette (the plates are black,
    //     every OBJ palette index 0). Reproducing it needs the BG layer, which
    //     the port's front-end does not model here yet.
    //   - the per-row contents (day number, difficulty, play time). Those are
    //     filled from save data the port has no reader for; their elements are
    //     laid out here but show their placeholder cell. (A savestate with a
    //     real file confirms the shape: header "DÍA <n>", the difficulty word in
    //     yellow, and the character face -- all save-data-driven.)
}

}  // namespace khdays::game::scenes
