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

// Sub-file 0 of a world archive: u8 room_count, three unknown bytes, then
// u32 offsets[room_count] into the table itself.
std::optional<std::size_t> room_entry_offset(
    const std::vector<std::uint8_t>& table, const std::size_t room_index) {
    if (table.empty() || room_index >= table[0]) {
        return std::nullopt;
    }
    const std::size_t at = 4U + room_index * 4U;
    const auto offset = static_cast<std::size_t>(read_u32(table, at));
    if (offset + 4U > table.size()) {
        return std::nullopt;
    }
    return offset;
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

std::optional<LoadedRoom> load_world_room(
    const std::string& world_code, const std::size_t room_index) {
    try {
        const auto container = khdays::vfs::read(archive_path(world_code));
        const auto table = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 0);
        const auto entry = room_entry_offset(table, room_index);
        if (!entry.has_value()) {
            return std::nullopt;
        }

        LoadedRoom room;
        // Byte +0x03 of the entry names the room's data sub-file. This is
        // established: it lands on an untyped sub-file for every room of all
        // ten shipped archives.
        room.data_subfile = table[*entry + 3U];

        // The sub-file count lives in the low 9 bits of the P2 header word.
        const std::size_t count = container.size() >= 4U
            ? (static_cast<std::size_t>(container[2])
               | (static_cast<std::size_t>(container[3]) << 8U))
                & 0x1FFU
            : 0U;

        // PORT HEURISTIC, not the game's rule -- see world.h. Walk forward from
        // the data blob and take KAPH sub-files until one is not a KAPH.
        for (std::size_t index = room.data_subfile + 1U; index < count; ++index) {
            std::vector<std::uint8_t> blob;
            try {
                blob = khdays::assets::extract_p2_subfile(
                    container.data(), container.size(), index);
            } catch (const std::exception&) {
                break;
            }
            const auto pack =
                khdays::assets::parse_slot_container(blob.data(), blob.size());
            if (!pack.valid || pack.slots[7].empty()) {
                break;
            }
            const auto& bmd0 = pack.slots[7].front();
            RoomModel piece;
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
                    // draws untextured rather than failing the whole room.
                }
            }
            room.models.push_back(std::move(piece));
        }

        if (room.models.empty()) {
            return std::nullopt;
        }
        return room;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace khdays::resource
