#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/ui_layout.h"

namespace {

int failures = 0;

void expect(const bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void put_u32(std::vector<std::uint8_t>& bytes,
             const std::size_t offset,
             const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

}  // namespace

int main() {
    using khdays::assets::kUiElementSize;
    using khdays::assets::kUiUnset;

    // Two records shaped like the shipped UI/cm/cm_save.ui: the game's first
    // two save rows, id 1 at (29, 26) and id 2 at (29, 62), kind 13, with the
    // +0x0c..+0x1c slots left at the unset sentinel as they are in the file.
    std::vector<std::uint8_t> blob(kUiElementSize * 2U, 0U);
    for (std::size_t i = 0U; i < 2U; ++i) {
        const std::size_t base = i * kUiElementSize;
        put_u32(blob, base + 0x00U, static_cast<std::uint32_t>(i + 1U));
        put_u32(blob, base + 0x08U, 13U);
        put_u32(blob, base + 0x0cU, kUiUnset);
        for (std::size_t s = 0U; s < 4U; ++s) {
            put_u32(blob, base + 0x40U + s * 4U, kUiUnset);
        }
        put_u32(blob, base + 0x30U, 29U << 12U);
        put_u32(blob, base + 0x34U, (i == 0U ? 26U : 62U) << 12U);
    }

    const auto layout =
        khdays::assets::decode_ui_layout(blob.data(), blob.size());
    expect(layout.elements.size() == 2U, "element count from blob length");
    expect(layout.elements[0].id == 1 && layout.elements[1].id == 2,
           "elementId decoded from +0x00");
    expect(layout.elements[0].keys_default[0].has_value()
               && *layout.elements[0].keys_default[0] == 13,
           "keysDefault[0] decoded from +0x08");
    expect(!layout.elements[0].keys_default[1].has_value(),
           "a negative key decodes to no value");
    expect(layout.elements[0].x == 29.0F && layout.elements[0].y == 26.0F,
           "row 0 position (point30, 20.12 fixed point)");
    expect(layout.elements[1].y == 62.0F, "row 1 y -- the game's 36px pitch");
    for (const auto& neighbour : layout.elements[0].neighbours) {
        expect(!neighbour.has_value(),
               "unset neighbour ids decode to no value");
    }
    expect(layout.elements[0].raw[21] == 0U, "raw words are preserved");

    // Neighbours are read from +0x40..+0x4c, the words the linker
    // (func_ov008_02053bcc) resolves into the widget's +0x88..+0x94 pointers.
    {
        std::vector<std::uint8_t> linked(kUiElementSize, 0U);
        for (std::size_t s = 0U; s < 4U; ++s) {
            put_u32(linked, 0x40U + s * 4U,
                    static_cast<std::uint32_t>(10 + s));
        }
        const auto one =
            khdays::assets::decode_ui_layout(linked.data(), linked.size());
        for (std::size_t s = 0U; s < 4U; ++s) {
            expect(one.elements[0].neighbours[s].has_value()
                       && *one.elements[0].neighbours[s]
                              == static_cast<std::int32_t>(10 + s),
                   "neighbour id round-trips");
        }
    }

    // A negative coordinate must survive as negative: ov000 seeds the save
    // rows' off-screen start positions at -256 .. -512 in 20.12.
    {
        std::vector<std::uint8_t> negative(kUiElementSize, 0U);
        put_u32(negative, 0x30U, 0xFFF00000U);  // -256.0
        const auto one =
            khdays::assets::decode_ui_layout(negative.data(), negative.size());
        expect(one.elements.size() == 1U, "single record");
        expect(one.elements[0].x == -256.0F, "negative 20.12 stays negative");
    }

    // A length that is not a whole number of records is refused rather than
    // silently truncated -- the count is derived from it.
    {
        bool threw = false;
        std::vector<std::uint8_t> ragged(kUiElementSize + 7U, 0U);
        try {
            khdays::assets::decode_ui_layout(ragged.data(), ragged.size());
        } catch (const std::runtime_error&) {
            threw = true;
        }
        expect(threw, "ragged length is rejected");
    }

    if (failures == 0) {
        std::cout << "ui_layout tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
