#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <map>

#include "khdays/assets/animation.h"  // SkeletalAnimation
#include "khdays/assets/battle_hud.h"  // Ov002PlayerGauge
#include "khdays/assets/cell.h"       // AnimBank
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/material_animation.h"  // MaterialColorAnimation
#include "khdays/assets/mesh.h"       // NeutralModel
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

// Load a standalone KAPH/D2KP sprite container such as ov004's localized
// `UI/cal/cl_hrt_&.pobj.z`. Nintendo LZ10/LZ11 compression is handled here.
std::optional<SpriteSet> load_sprite_container(const char* game_path);

// Load ov004's ten 16x32 textured digit quads from `UI/cal/[0-9]_a.pak.z`.
// The scene projects these to the original approximately 4x8-pixel glyphs.
std::optional<std::array<khdays::assets::DecodedTexture, 10>>
load_calendar_digits();

// Load a screen's `.ui` element layout, resolved through the mod/vfs chain like
// every other resource (so `mods/<Mod>/files/UI/cm/cm_save.ui.z` overrides it).
std::optional<khdays::assets::UiLayout> load_ui_layout(const char* game_path);

// Compose the boot/publisher logo the game shows first (ttl.p2 sub-file 1: an
// NCLR + NCGR + NSCR full-screen image).
std::optional<khdays::assets::DecodedTexture> load_boot_logo();

// The title logo as an animatable 3D model plus its NSBCA. The DS renders this
// model (ttl.p2 sub-file 0, a KAPH holding a BMD0 + a BCA0) and plays the
// animation (func_ov000_0204d7c8 loads it, func_ov000_02059f50 renders 3D). A
// scene poses `model` per frame with sample_animation + compute_palette, then
// flattens it with compose_flat_model(model, textures, ...).
struct TitleLogoModel {
    khdays::assets::NeutralModel model;
    std::map<std::string, khdays::assets::DecodedTexture> textures;
    khdays::assets::SkeletalAnimation animation;  // frame_count == 0 if none
    // The KAPH's slot-2 BMA0, which ov000 plays alongside the BCA0 (node
    // tracks 0 and 2): it fades the materials' polygon alpha.
    std::optional<khdays::assets::MaterialColorAnimation> material_animation;
};

// Neutral reconstruction of ov012's `op/op.p2` resources. The base archive
// supplies fourteen palettes and two main-screen planes per card; the selected
// language sub-file supplies a third, localized plane for the first thirteen.
struct OpeningArtwork final {
    std::array<khdays::assets::DecodedTexture, 14> base;
    std::array<khdays::assets::DecodedTexture, 14> accent;
    std::array<khdays::assets::DecodedTexture, 13> localized;
};

// Neutral pieces of ov002's local-player HUD. The primary gauge is kept as an
// empty strip plus its palette so the game can reproduce the original dynamic
// 4bpp compositor. The remaining pieces are already-decoded RGBA overlays.
struct BattleHudArtwork final {
    khdays::assets::Ov002PlayerGauge player_gauge;
    khdays::assets::DecodedTexture player_label;
    khdays::assets::DecodedTexture hp_label;
    khdays::assets::DecodedTexture roxas_portrait;
};

// Load the primary HUD pieces from UI/btl/main.p2. Returns null when the user
// has not supplied the game data or the expected ov002 resource layout is not
// present.
std::optional<BattleHudArtwork> load_battle_hud_artwork();

// Load ov002's localized primary command page. `localized_main_game_path` is
// UI/btl/<lang>/main.p2, while `command_table_game_path` is the matching
// cmd.s.z. The layout and palette still come from the base UI/btl/main.p2.
std::optional<khdays::assets::Ov002CommandMenuArtwork>
load_ov002_command_menu(
    const char* localized_main_game_path,
    const char* command_table_game_path,
    const char* font_game_path);

// `language_subfile` is the game's own language selector: 1=en, 2=fr, 3=de,
// 4=it, 5=es. Returns null when op.p2 is absent or structurally incomplete.
std::optional<OpeningArtwork> load_opening_artwork(
    std::size_t language_subfile);

std::optional<TitleLogoModel> load_title_logo_model();

// Compose one background layer from a D2KP UI pack: extract the P2 sub-file,
// parse the typed pack, and compose screen[screen] with tiles[tiles_index] and
// palette[palette_index] into an opaque RGBA image.
std::optional<khdays::assets::DecodedTexture> load_ui_background(
    const char* game_path,
    std::size_t subfile,
    std::size_t screen,
    std::size_t tiles_index,
    std::size_t palette_index);

// Character data the game streams over a BG layer's tiles after loading them:
// the NCGR in `game_path` sub-file `subfile` is copied into the layer's
// character VRAM at `char_byte_offset` (relative to the layer's char base), so
// it replaces the tiles from that point on. ov000 does this to localize the
// boot legal screen (func_ov000_0204de30) and the title illustration
// (func_ov000_0204e0c8).
struct CharacterPatch final {
    std::string game_path;
    std::size_t subfile = 0;
    std::size_t char_byte_offset = 0;
};

// load_ui_background with `patch` applied to the tiles before composing.
std::optional<khdays::assets::DecodedTexture> load_ui_background(
    const char* game_path,
    std::size_t subfile,
    std::size_t screen,
    std::size_t tiles_index,
    std::size_t palette_index,
    const CharacterPatch& patch);

// Decode a tile-only D2KP UI sub-file to a transparent atlas. Battle UI packs
// such as UI/btl/main.p2 carry their CHR/PLT here and assemble the final HUD at
// runtime rather than storing an NSCR tilemap.
std::optional<khdays::assets::DecodedTexture> load_ui_tile_atlas(
    const char* game_path,
    std::size_t subfile,
    std::size_t tiles_index = 0,
    std::size_t palette_index = 0);

// Render a UTF-16 (game-encoded) string in a named game font resolved through
// the VFS (mod overrides applied by the resource font loader).
std::optional<khdays::assets::DecodedTexture> render_ui_text(
    const char* font_game_path, const std::u16string& text);

}  // namespace khdays::resource
