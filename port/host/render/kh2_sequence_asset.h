#pragma once

#include "khdays/assets/kh2_sequence.h"
#include "khdays/assets/texture.h"

#include <array>
#include <filesystem>
#include <string>

namespace khdays::render::kh2 {

struct SequenceAsset final {
    assets::kh2::SequenceData sequence;
    assets::DecodedTexture texture;
    std::array<int, 2> logical_size{};
};

// File resolution stays outside the parser/evaluator. The installer can later
// supply these same paths without changing sequence playback or rendering.
SequenceAsset load_sequence_asset(const std::filesystem::path &archive, const std::string &entry,
                                  const std::filesystem::path &texture);
SequenceAsset load_sequence_asset(const std::filesystem::path &archive, const std::string &image_entry,
                                  const std::string &sequence_entry, const std::filesystem::path &texture);

} // namespace khdays::render::kh2
