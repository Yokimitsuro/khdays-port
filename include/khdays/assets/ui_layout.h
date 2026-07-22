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

struct UiElement final {
    std::uint32_t id = 0;      // +0x00 -- what FindEntryById(ctx, id) looks up
    std::uint32_t kind = 0;    // +0x08 -- element type; what each value draws is NOT established
    float x = 0.0F;            // +0x30, 20.12 fixed point
    float y = 0.0F;            // +0x34, 20.12 fixed point

    // The five slots at +0x0c..+0x1c. Every one of them is the unset sentinel in
    // cm_save.ui, so their meaning cannot be read off the shipped data; they are
    // exposed as raw words rather than guessed at. `none` when unset.
    std::array<std::optional<std::uint32_t>, 5> slots{};

    // All 22 words of the record, verbatim, for fields this decoder does not
    // claim to understand.
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
