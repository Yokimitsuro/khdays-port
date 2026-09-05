#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "khdays/assets/tex0.h"

namespace khdays::assets {

// Neutral source for ov002's primary HP gauge. The empty 80x8 strip comes
// from UI/btl/main.p2; the palette is retained because ov002 paints palette
// indices directly into that strip while the gauge changes.
struct Ov002PlayerGauge final {
    DecodedTexture empty;
    std::array<std::array<std::uint8_t, 4>, 16> palette{};
};

// Neutral source pieces for ov002's three-entry primary command list. The
// localized header and row frames come from UI/btl/<lang>/main.p2; labels are
// rendered from that language's cmd.s.z with the game's small battle font.
struct Ov002CommandMenuArtwork final {
    DecodedTexture header;
    DecodedTexture idle_row;
    DecodedTexture selected_row;
    std::array<DecodedTexture, 3> labels;
};

// Reproduce ov002's 77-cell primary gauge. The game converts HP to cells with
// integer division, keeps one cell for a non-zero value, and writes each cell
// right-to-left with the six-pixel shade ramp {6,6,5,5,4,3}.
DecodedTexture compose_ov002_player_gauge(
    const Ov002PlayerGauge& source,
    std::uint16_t current,
    std::uint16_t maximum);

// Assemble the default command page exactly where func_ov002_0205ad5c places
// it: a 96x16 header followed by three 88x16 rows. Selected text is inset two
// tiles and white (palette 15); inactive text is inset one tile and grey
// (palette 14).
DecodedTexture compose_ov002_command_menu(
    const Ov002CommandMenuArtwork& source,
    std::size_t selected);

// Advance the primary cursor the way func_ov002_0205d658 does: inspect only
// the other slots, skip entries marked unavailable, and wrap at three.
std::size_t advance_ov002_command(
    std::size_t current,
    const std::array<bool, 3>& available);

}  // namespace khdays::assets
