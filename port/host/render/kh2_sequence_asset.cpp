#include "kh2_sequence_asset.h"

#include "khdays/assets/dds.h"
#include "khdays/assets/image.h"
#include "khdays/assets/kh2_bar.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace khdays::render::kh2 {

SequenceAsset load_sequence_asset(const std::filesystem::path &archive, const std::string &entry,
                                  const std::filesystem::path &texture)
{
    return load_sequence_asset(archive, entry, entry, texture);
}

SequenceAsset load_sequence_asset(const std::filesystem::path &archive, const std::string &image_entry,
                                  const std::string &sequence_entry, const std::filesystem::path &texture)
{
    std::ifstream file(archive, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("KH2 sequence: cannot open archive " + archive.string());
    const auto length = file.tellg();
    if (length < 16 || length > 256 * 1024 * 1024)
        throw std::runtime_error("KH2 sequence: invalid archive size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("KH2 sequence: could not read archive " + archive.string());
    std::optional<assets::kh2::BarEntry> image, sequence;
    for (const auto &e : assets::kh2::bar_entries(bytes.data(), bytes.size())) {
        if (e.type == 0x18 && e.name == image_entry) {
            if (image)
                throw std::runtime_error("KH2 sequence: ambiguous image entry " + image_entry);
            image = e;
        }
        if (e.type == 0x19 && e.name == sequence_entry) {
            if (sequence)
                throw std::runtime_error("KH2 sequence: ambiguous sequence entry " + sequence_entry);
            sequence = e;
        }
    }
    if (!image || !sequence)
        throw std::runtime_error("KH2 sequence: missing IMGD " + image_entry + " or SEQD " + sequence_entry);
    if (image->size < 64 || sequence->size < 0x30)
        throw std::runtime_error("KH2 sequence: truncated IMGD or SEQD entry");
    SequenceAsset out;
    out.logical_size = assets::kh2::read_sequence_image_size(bytes.data() + image->offset, image->size);
    out.sequence = assets::kh2::read_sequence(bytes.data() + sequence->offset, sequence->size);
    auto extension = texture.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    out.texture = extension == ".dds" ? assets::read_dds(texture) : assets::read_image(texture);
    return out;
}

} // namespace khdays::render::kh2
