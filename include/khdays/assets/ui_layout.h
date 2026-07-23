#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

// Decoder for the game's `.ui` screen-layout files (UI/**/*.ui, shipped
// LZ-compressed as `.ui.z`).
//
// A `.ui` is a flat array of fixed 0x58-byte element records with no header --
// the element count is not in the file, it is passed by the caller. The game
// loads one with (ov000/ov008, same shared code):
//
//     Ov000_LoadBlockProcessAndFree(ctx, "UI/cm/cm_save.ui.z", 0x24)
//       -> Archive_LoadFile(name, 0xe)
//       -> InstantiateAndLinkElements(ctx, blob, count)
//
// and InstantiateAndLinkElements walks the array **twice**: pass 1 instantiates
// every element, pass 2 links them -- two passes because a link may target an
// element pass 1 has not created yet.
//
// Only the fields below are decoded, because only they are established. Every
// record's 22 raw words are kept in `raw` so a consumer can look at the rest
// without this decoder inventing meanings for them.
namespace khdays::assets {

// Positions are stored as 20.12 fixed point. Sentinel -1 (0xffffffff) means
// "unset" and appears in the link/extra slots throughout the shipped files.
inline constexpr std::uint32_t kUiUnset = 0xFFFFFFFFU;

// Field names and offsets come from the decomp's own WidgetDescriptor, as read
// by func_ov008_020543b0 (instantiate) and func_ov008_02053bcc (link).
struct UiElement final {
    std::int32_t id = 0;        // +0x00 elementId -- what FindEntryById(ctx, id) looks up
    std::int32_t group = 0;     // +0x04 groupId -- the nav walker refuses to wrap within a group

    // +0x08/+0x0c and +0x18/+0x1c: two content keys each. The instantiator
    // picks `alternate` when descriptor flag bit 0 is set and either alternate
    // key is >= 0, else `def`; each non-negative key becomes an allocated slot.
    // A key indexes the screen's graphics; which cell of which pack it selects
    // is NOT established here.
    std::array<std::optional<std::int32_t>, 2> keys_default{};
    std::array<std::optional<std::int32_t>, 2> keys_alternate{};
    std::array<std::optional<std::int32_t>, 2> values{};   // +0x10/+0x14

    // Position, 20.12 fixed point. `point30` is the resting position the
    // instantiator always copies to the widget. When flag bit 2 is set the
    // element instead tweens from `point20` toward point30 over 0x3e8, with
    // `point28` as a second control point -- so a moving element has three.
    float x = 0.0F;             // +0x30 point30.x
    float y = 0.0F;             // +0x34 point30.y
    std::array<float, 2> tween_from{};    // +0x20 point20
    std::array<float, 2> tween_extra{};   // +0x28 point28

    // +0x40..+0x4c: the ids of the Up/Down/Left/Right neighbours. The linker
    // resolves each to a widget pointer at +0x88..+0x94, which is exactly what
    // the nav walker (func_ov000_020552b4) follows. Unset (-1) throughout every
    // shipped .ui, so these files carry positions, not the navigation graph.
    std::array<std::optional<std::int32_t>, 4> neighbours{};

    std::uint32_t flags = 0;    // +0x50 -- bit0 use-alternate, bit1 start hidden, bit2 tween
    std::int32_t priority = 0;  // +0x54 priorityBase; slot i gets priority + 1 - i

    // All 22 words of the record, verbatim, for anything above that is only
    // partially understood.
    std::array<std::uint32_t, 22> raw{};
};

struct UiLayout final {
    std::vector<UiElement> elements;
};

// Size of one element record. The file length must be an exact multiple of it.
inline constexpr std::size_t kUiElementSize = 0x58U;

// Decode a `.ui` blob. The count is derived from the blob length (size /
// 0x58); the game passes it explicitly instead, and for UI/cm/cm_save.ui the
// two agree exactly (3168 bytes = 36 * 0x58, and the game passes 0x24 = 36).
// Throws std::runtime_error when the length is not a whole number of records.
UiLayout decode_ui_layout(const std::uint8_t* data, std::size_t size);

// Same, from a file. LZ-compressed `.ui.z` input is decompressed first.
UiLayout decode_ui_layout(const std::filesystem::path& input_path);

}  // namespace khdays::assets
