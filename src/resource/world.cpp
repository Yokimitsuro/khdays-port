#include "khdays/resource/world.h"

#include <cstdint>
#include <exception>

#include "khdays/assets/graphics2d.h"  // parse_slot_container
#include "khdays/assets/mdl0.h"        // decode_model_geometry
#include "khdays/assets/message.h"     // extract_p2_subfile
#include "khdays/vfs/filesystem.h"

namespace khdays::resource {

namespace {

std::string archive_path(const std::string& world_code) {
    return "mi/wd/wd_" + world_code;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data,
                       const std::size_t offset) {
    if (offset + 4U > data.size()) {
        return 0U;
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

// The low 9 bits of the P2 header word at 0x02.
std::size_t subfile_count(const std::vector<std::uint8_t>& container) {
    if (container.size() < 4U) {
        return 0U;
    }
    return (static_cast<std::size_t>(container[2])
            | (static_cast<std::size_t>(container[3]) << 8U))
        & 0x1FFU;
}

}  // namespace

std::size_t world_room_count(const std::string& world_code) {
    try {
        const auto container = khdays::vfs::read(archive_path(world_code));
        const auto table = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 0);
        return table.empty() ? 0U : table[0];
    } catch (const std::exception&) {
        return 0U;
    }
}

std::optional<std::size_t> room_data_subfile(
    const std::string& world_code, const std::size_t room_index) {
    try {
        const auto container = khdays::vfs::read(archive_path(world_code));
        const auto table = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 0);
        if (table.empty() || room_index >= table[0]) {
            return std::nullopt;
        }
        // Sub-file 0: u8 room_count, three unknown bytes, then
        // u32 offsets[room_count] into the table itself.
        const auto entry =
            static_cast<std::size_t>(read_u32(table, 4U + room_index * 4U));
        if (entry + 4U > table.size()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(table[entry + 3U]);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::size_t> world_model_subfiles(const std::string& world_code) {
    std::vector<std::size_t> out;
    try {
        const auto container = khdays::vfs::read(archive_path(world_code));
        const std::size_t count = subfile_count(container);
        for (std::size_t index = 1U; index < count; ++index) {
            try {
                const auto blob = khdays::assets::extract_p2_subfile(
                    container.data(), container.size(), index);
                const auto pack = khdays::assets::parse_slot_container(
                    blob.data(), blob.size());
                if (pack.valid && !pack.slots[7].empty()) {
                    out.push_back(index);
                }
            } catch (const std::exception&) {
                // Skip a sub-file that will not extract.
            }
        }
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

std::vector<RoomModel> load_world_models(
    const std::string& world_code, const std::vector<std::size_t>& subfiles) {
    std::vector<RoomModel> out;
    try {
        const auto container = khdays::vfs::read(archive_path(world_code));
        for (const std::size_t index : subfiles) {
            std::vector<std::uint8_t> blob;
            try {
                blob = khdays::assets::extract_p2_subfile(
                    container.data(), container.size(), index);
            } catch (const std::exception&) {
                continue;
            }
            const auto pack =
                khdays::assets::parse_slot_container(blob.data(), blob.size());
            if (!pack.valid || pack.slots[7].empty()) {
                continue;
            }
            const auto& bmd0 = pack.slots[7].front();
            RoomModel piece;
            piece.subfile = index;
            piece.model =
                khdays::assets::decode_model_geometry(bmd0.data, bmd0.size);
            piece.name = piece.model.name;
            for (const auto& mesh : piece.model.meshes) {
                if (mesh.texture_name.empty()
                    || piece.textures.count(mesh.texture_name) != 0U) {
                    continue;
                }
                try {
                    piece.textures.emplace(
                        mesh.texture_name,
                        khdays::assets::load_tex0_texture(
                            bmd0.data, bmd0.size, mesh.texture_name));
                } catch (const std::exception&) {
                    // A mesh may name a texture this file does not carry; it
                    // draws untextured rather than failing the load.
                }
            }
            out.push_back(std::move(piece));
        }
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

}  // namespace khdays::resource
