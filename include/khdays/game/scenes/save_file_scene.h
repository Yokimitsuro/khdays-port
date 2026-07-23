#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "khdays/assets/tex0.h"
#include "khdays/assets/ui_layout.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game::scenes {

// The save-file screen ("Seleccionar archivo."), which the DS shows between
// MODO MISIÓN > UN JUGADOR and the character select.
//
// Unlike the port's other screens, this one is not laid out from measurements:
// every element position comes from the game's own `UI/cm/cm_save.ui`, a flat
// array of 0x58-byte records that ov000 feeds to InstantiateAndLinkElements.
// A record carries an id, a group, content keys and a position in 20.12 fixed
// point. Each content key is an index into the NANR animation bank of
// `UI/cm/cmo_&.p2` sub-file 3, whose steps name the NCER cells to show -- so
// the artwork is looked up, not hardcoded.
//
// An earlier attempt at this screen was reverted for filling the gaps between
// measured values with invented ones. Nothing here is invented: what is not
// established is simply not drawn, and is listed in the .cpp.
class SaveFileScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    // ov000's setup (FUN_arm9_ov000__0204fdac) hides these ids on entry, via
    // two id tables plus three individual FindEntryById/SetEntrySlotsVisible
    // pairs. They stay hidden until game logic that the port does not have
    // reveals them.
    static bool hidden_on_entry(std::int32_t id);

    // The three save rows (ids 1..3) sit 8px higher than the .ui says: the same
    // setup subtracts 0x8000 (8.0 in 20.12) from rows 0..2 of its 4x8 id table,
    // and the guest bar (id 4, row 3) is excluded. Both halves match the OAM
    // capture -- rows at 18/54/90 against the file's 26/62/98, guest bar at 126
    // either way -- which is what makes this a reading and not a guess.
    static constexpr float kRowLift = 8.0F;

    std::optional<khdays::assets::UiLayout> layout_;
    std::optional<khdays::resource::SpriteSet> sprites_;
    int selected_ = 0;
    int frame_ = 0;
};

}  // namespace khdays::game::scenes
