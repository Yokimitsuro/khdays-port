#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "khdays/assets/mods.h"
#include "khdays/assets/audio.h"
#include "khdays/assets/tex0.h"

namespace khdays::assets {

struct MobiClipCoefficientTable final {
    std::array<std::uint16_t, 4096> lookup{};
    std::array<std::uint8_t, 256> residue{};
};

using MobiClipCoefficientTables = std::array<MobiClipCoefficientTable, 2>;

class ModsVideoDecoder final {
public:
    ModsVideoDecoder(std::vector<std::uint8_t> container,
                     MobiClipCoefficientTables coefficient_tables);
    ~ModsVideoDecoder();
    ModsVideoDecoder(ModsVideoDecoder&&) noexcept;
    ModsVideoDecoder& operator=(ModsVideoDecoder&&) noexcept;
    ModsVideoDecoder(const ModsVideoDecoder&) = delete;
    ModsVideoDecoder& operator=(const ModsVideoDecoder&) = delete;

    const ModsInfo& info() const;
    double frames_per_second() const;
    std::size_t frame_index() const;
    bool finished() const;
    bool decode_next(bool ds_exact = true);
    const DecodedTexture& frame() const;
    const DecodedAudio& audio_chunk() const;
    void reset();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace khdays::assets
