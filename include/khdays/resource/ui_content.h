#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "khdays/assets/cell.h"       // AnimBank
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/tex0.h"       // DecodedTexture

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
};

// Load one sprite pack: a P2 sub-file holding a single NCLR + NCGR + NCER (+
// optional NANR), e.g. res.p2 sub-files 1..3 or ttl.p2 sub-file 2.
std::optional<SpriteSet> load_sprite_set(const char* game_path,
                                         std::size_t subfile);

// Compose the boot/publisher logo the game shows first (ttl.p2 sub-file 1: an
// NCLR + NCGR + NSCR full-screen image).
std::optional<khdays::assets::DecodedTexture> load_boot_logo();

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
