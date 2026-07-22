#include "khdays/assets/ui_layout.h"

#include <fstream>
#include <stdexcept>
#include <string>

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
        element.id = element.raw[0];
        element.kind = element.raw[2];
        element.x = from_fx20_12(element.raw[12]);   // +0x30
        element.y = from_fx20_12(element.raw[13]);   // +0x34
        for (std::size_t s = 0U; s < element.slots.size(); ++s) {
            const std::uint32_t value = element.raw[3U + s];  // +0x0c..+0x1c
            if (value != kUiUnset) {
                element.slots[s] = value;
            }
        }
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
    if (!data.empty() && (data[0] == 0x10U || data[0] == 0x11U)) {
        data = lz_decompress(data);
    }
    return decode_ui_layout(data.data(), data.size());
}

}  // namespace khdays::assets
