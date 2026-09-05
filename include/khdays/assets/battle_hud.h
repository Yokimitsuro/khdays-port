#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

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

// Profile-derived command data consumed by ov002. func_02035cac copies a
// 0x7e-byte block into the runtime player record: 24 little-endian
// {key, quantity} entries followed by 15 two-byte magic records.
// Keeping that byte contract here lets a future save/profile loader hand the
// original data to gameplay without leaking the DS runtime record layout.
struct Ov002LoadoutEntry final {
    std::uint16_t key = 0U;
    std::uint16_t quantity = 0U;
};

struct Ov002MagicCounter final {
    std::uint8_t current = 0U;
    std::uint8_t secondary = 0U;
};

struct Ov002PanelLoadout final {
    static constexpr std::size_t kEntryCount = 24U;
    static constexpr std::size_t kMagicCount = 15U;
    static constexpr std::size_t kPackedSize =
        kEntryCount * 4U + kMagicCount * 2U;

    std::array<Ov002LoadoutEntry, kEntryCount> entries{};
    std::array<Ov002MagicCounter, kMagicCount> magic{};
};

// State supplied beside the 0x7e profile block when func_ov002_02069d40
// constructs the live panel. The profile block alone cannot answer command
// availability: enabled magic comes from func_020358f4, while item keys are
// filtered through the panel's slot table and enabled mask.
struct Ov002CommandAvailabilityContext final {
    std::uint16_t visible_magic_mask = 0U;
    std::array<std::uint16_t, Ov002PanelLoadout::kEntryCount> item_keys{};
    std::uint32_t enabled_item_mask = 0U;
    // List 2 is populated from the separate 18-entry runtime table. Its exact
    // predicate is evaluated by the session builder, outside the packed block.
    bool supplemental_items_available = false;
};

enum class Ov002CommandPage : std::uint8_t {
    Primary,
    Attack,
    Magic,
    Items,
};

// Decode the block allocated by func_02035c28 and copied by func_02035cac.
// Returns an empty optional for any size other than the exact 0x7e bytes.
std::optional<Ov002PanelLoadout> decode_ov002_panel_loadout(
    std::span<const std::uint8_t> packed);

// Reproduce the single-player branches of func_ov002_0205a638 and
// func_ov002_0205a7b8 from the profile block plus the runtime filters that
// func_ov002_02069d40 supplies separately. Attack is always present.
std::array<bool, 3> ov002_command_availability(
    const Ov002PanelLoadout& loadout,
    const Ov002CommandAvailabilityContext& context);

// A-button activation from func_ov002_0205dae4, reduced to the primary page's
// externally meaningful outcome. Unavailable rows remain on Primary.
Ov002CommandPage activate_ov002_command(
    std::size_t selected,
    const std::array<bool, 3>& available);

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
