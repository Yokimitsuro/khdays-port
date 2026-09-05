#pragma once

#include <array>
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

// Reproduce ov002's 77-cell primary gauge. The game converts HP to cells with
// integer division, keeps one cell for a non-zero value, and writes each cell
// right-to-left with the six-pixel shade ramp {6,6,5,5,4,3}.
DecodedTexture compose_ov002_player_gauge(
    const Ov002PlayerGauge& source,
    std::uint16_t current,
    std::uint16_t maximum);

}  // namespace khdays::assets
