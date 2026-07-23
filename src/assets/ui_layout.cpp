#include "khdays/assets/ui_layout.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/message.h"  // lz_decompress

namespace khdays::assets {

namespace {

std::uint32_t read_u32(const std::uint8_t* data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

// 20.12 fixed point, signed: the game stores off-screen start positions as
// negative values (ov000's save screen seeds four of them at -256 .. -512).
float from_fx20_12(std::uint32_t value) {
    return static_cast<float>(static_cast<std::int32_t>(value))
        / 4096.0F;
}

}  // namespace

UiLayout decode_ui_layout(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size == 0U) {
        throw std::runtime_error("empty .ui resource");
    }
    // The game ships these LZ-compressed as `.ui.z`, and khdays::vfs::read
    // hands back raw bytes by design, so decompression belongs here rather than
    // only in the path overload -- otherwise a caller reading through the VFS
    // gets a compressed blob and a confusing "not a whole number of records".
    if (data[0] == 0x10U || data[0] == 0x11U) {
        const std::vector<std::uint8_t> packed(data, data + size);
        const auto unpacked = lz_decompress(packed);
        return decode_ui_layout(unpacked.data(), unpacked.size());
    }
    if (size % kUiElementSize != 0U) {
        throw std::runtime_error(
            "'.ui' size " + std::to_string(size)
            + " is not a whole number of " + std::to_string(kUiElementSize)
            + "-byte element records");
    }

    UiLayout layout;
    const std::size_t count = size / kUiElementSize;
    layout.elements.reserve(count);

    for (std::size_t i = 0U; i < count; ++i) {
        const std::uint8_t* record = data + i * kUiElementSize;
        UiElement element;
        for (std::size_t w = 0U; w < element.raw.size(); ++w) {
            element.raw[w] = read_u32(record, w * 4U);
        }
        const auto signed_word = [&element](const std::size_t index) {
            return static_cast<std::int32_t>(element.raw[index]);
        };
        // A key/value/neighbour is "unset" when negative: the instantiator
        // tests `>= 0` rather than comparing against a sentinel, so anything
        // negative means absent, not just -1.
        const auto optional_word =
            [&signed_word](const std::size_t index) -> std::optional<std::int32_t> {
            const std::int32_t value = signed_word(index);
            return value >= 0 ? std::optional<std::int32_t>{value} : std::nullopt;
        };

        element.id = signed_word(0);                  // +0x00
        element.group = signed_word(1);               // +0x04
        for (std::size_t k = 0U; k < 2U; ++k) {
            element.keys_default[k] = optional_word(2U + k);    // +0x08/+0x0c
            element.values[k] = optional_word(4U + k);          // +0x10/+0x14
            element.keys_alternate[k] = optional_word(6U + k);  // +0x18/+0x1c
            element.tween_from[k] = from_fx20_12(element.raw[8U + k]);   // +0x20
            element.tween_extra[k] = from_fx20_12(element.raw[10U + k]); // +0x28
        }
        element.x = from_fx20_12(element.raw[12]);    // +0x30
        element.y = from_fx20_12(element.raw[13]);    // +0x34
        for (std::size_t n = 0U; n < 4U; ++n) {
            element.neighbours[n] = optional_word(16U + n);  // +0x40..+0x4c
        }
        element.flags = element.raw[20];              // +0x50
        element.priority = signed_word(21);           // +0x54
        layout.elements.push_back(element);
    }
    return layout;
}

UiLayout decode_ui_layout(const std::filesystem::path& input_path) {
    std::ifstream stream{input_path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error(
            "cannot open .ui resource: " + input_path.string());
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    std::vector<std::uint8_t> data(
        end > 0 ? static_cast<std::size_t>(end) : 0U);
    stream.seekg(0, std::ios::beg);
    if (!data.empty()) {
        stream.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    }
    return decode_ui_layout(data.data(), data.size());
}

}  // namespace khdays::assets
