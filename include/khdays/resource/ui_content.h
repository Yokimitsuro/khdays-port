#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "khdays/assets/cell.h"       // AnimBank
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/tex0.h"       // DecodedTexture
#include "khdays/assets/ui_layout.h"  // UiLayout

// UI-content loaders: resolve a game path through the VFS, decode the DS 2D
// resources it holds, and return neutral RGBA the engine can draw. These sit in
// the resource layer (not the engine) so scenes depend only on neutral forms,
// never on the DS container/format details. Every loader returns std::nullopt
// when the data is absent, so scenes degrade gracefully without game data.
namespace khdays::resource {

// A decoded OBJ sprite set: every NCER cell pre-rendered to RGBA, plus the NANR
// animations, so a scene can play them via khdays::assets::Animator.
struct SpriteSet final {
    std::vector<khdays::assets::DecodedTexture> cells;
    khdays::assets::AnimBank animations;

    // Where each rendered cell's top-left sits relative to the cell's own
    // origin, i.e. the minimum (x, y) over its OAM pieces. render_cell()
    // collapses the pieces into a bitmap anchored at that minimum, so the
    // offset has to be carried separately: the DS places a piece at
    // (cell_position + piece.x), and a cell whose pieces straddle the origin
    // therefore starts left of / above where it is positioned. Aligned with
    // `cells`; add it to a cell's screen position before drawing.
    std::vector<std::array<int, 2>> cell_origins;
};

// Load one sprite pack: a P2 sub-file holding a single NCLR + NCGR + NCER (+
// optional NANR), e.g. res.p2 sub-files 1..3 or ttl.p2 sub-file 2.
std::optional<SpriteSet> load_sprite_set(const char* game_path,
                                         std::size_t subfile);

// Load a screen's `.ui` element layout, resolved through the mod/vfs chain like
// every other resource (so `mods/<Mod>/files/UI/cm/cm_save.ui.z` overrides it).
std::optional<khdays::assets::UiLayout> load_ui_layout(const char* game_path);

// Compose the boot/publisher logo the game shows first (ttl.p2 sub-file 1: an
// NCLR + NCGR + NSCR full-screen image).
std::optional<khdays::assets::DecodedTexture> load_boot_logo();

// Compose the real title logo: ttl.p2 sub-file 0 is a KAPH pack holding the
// "title" 3D model — a few flat textured quads (KINGDOM HEARTS, 358/2 Days, the
// heart and crown). This decodes the model and its textures and composites them
// to a 2D image, so the title shows the actual logo instead of a flat backdrop.
// The KINGDOM HEARTS 358/2 Days logo (the ttl.p2 KAPH/BMD0 model, flattened).
// `over_white` composites it on an opaque white top screen (the default, as the
// save screen uses it); false keeps the logo's own alpha so it can be overlaid
// on another background (the title draws it over the s7 top-screen BG, which
// carries Disney/SQUARE ENIX and the illustration but not the "358/2 Days"
// subtitle -- that subtitle lives in this model).
std::optional<khdays::assets::DecodedTexture> load_title_logo(
    bool over_white = true);

// Compose one background layer from a D2KP UI pack: extract the P2 sub-file,
// parse the typed pack, and compose screen[screen] with tiles[tiles_index] and
// palette[palette_index] into an opaque RGBA image.
std::optional<khdays::assets::DecodedTexture> load_ui_background(
    const char* game_path,
    std::size_t subfile,
    std::size_t screen,
    std::size_t tiles_index,
    std::size_t palette_index);

// Render a UTF-16 (game-encoded) string in a named game font resolved through
// the VFS (mod overrides applied by the resource font loader).
std::optional<khdays::assets::DecodedTexture> render_ui_text(
    const char* font_game_path, const std::u16string& text);

}  // namespace khdays::resource
