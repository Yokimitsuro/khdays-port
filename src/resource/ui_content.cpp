#include "khdays/resource/ui_content.h"

#include "khdays/assets/ui_layout.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>

#include "khdays/assets/font.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h"
#include "khdays/assets/screen.h"
#include "khdays/resource/loader.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::resource {

namespace {

std::uint16_t read_u16(
    const std::vector<std::uint8_t>& data,
    const std::size_t offset) {
    if (offset + 2U > data.size()) {
        throw std::runtime_error("ov002 layout u16 is out of range");
    }
    return static_cast<std::uint16_t>(
        data[offset] | (static_cast<std::uint16_t>(data[offset + 1U]) << 8U));
}

std::uint32_t read_u32(
    const std::vector<std::uint8_t>& data,
    const std::size_t offset) {
    if (offset + 4U > data.size()) {
        throw std::runtime_error("ov002 layout u32 is out of range");
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

struct Ov002SpriteRecord final {
    int source_x = 0;
    int source_y = 0;
    int width = 0;
    int height = 0;
};

std::optional<Ov002SpriteRecord> find_ov002_sprite(
    const std::vector<std::uint8_t>& layout,
    const std::uint16_t requested_id) {
    if (layout.size() < 8U) {
        return std::nullopt;
    }
    const std::size_t section = read_u32(layout, 4U);
    if (section + 4U > layout.size()) {
        return std::nullopt;
    }
    const std::size_t count = read_u32(layout, section);
    std::size_t cursor = section + 4U;
    for (std::size_t index = 0; index < count; ++index) {
        if (cursor + 22U > layout.size()) {
            return std::nullopt;
        }
        const std::size_t record_size = read_u32(layout, cursor);
        if (record_size < 22U || record_size > layout.size() - cursor) {
            return std::nullopt;
        }
        if (read_u16(layout, cursor + 4U) == requested_id) {
            return Ov002SpriteRecord{
                static_cast<int>(read_u16(layout, cursor + 12U)) * 8,
                static_cast<int>(read_u16(layout, cursor + 14U)) * 8,
                static_cast<int>(static_cast<std::int16_t>(
                    read_u16(layout, cursor + 16U))) * 8,
                static_cast<int>(static_cast<std::int16_t>(
                    read_u16(layout, cursor + 18U))) * 8};
        }
        cursor += record_size;
    }
    return std::nullopt;
}

khdays::assets::ResourceView find_ov002_layout_screen(
    const std::vector<std::uint8_t>& layout) {
    if (layout.size() < 4U) {
        return {};
    }
    const std::size_t section = read_u32(layout, 0U);
    if (section + 4U > layout.size()) {
        return {};
    }
    const std::size_t count = read_u32(layout, section);
    std::size_t cursor = section + 4U;
    for (std::size_t index = 0; index < count; ++index) {
        if (cursor + 8U > layout.size()) {
            return {};
        }
        const std::size_t record_size = read_u32(layout, cursor);
        if (record_size < 8U || record_size > layout.size() - cursor) {
            return {};
        }
        if (read_u16(layout, cursor + 4U) == 1U) {
            const auto screen = khdays::assets::find_nitro_resource(
                layout.data() + cursor + 8U, record_size - 8U, "RCSN");
            if (screen) {
                return screen;
            }
        }
        cursor += record_size;
    }
    return {};
}

khdays::assets::DecodedTexture crop_texture(
    const khdays::assets::DecodedTexture& source,
    const Ov002SpriteRecord& rectangle,
    const char* name) {
    if (rectangle.source_x < 0 || rectangle.source_y < 0
        || rectangle.width <= 0 || rectangle.height <= 0
        || rectangle.source_x + rectangle.width > source.width
        || rectangle.source_y + rectangle.height > source.height) {
        throw std::runtime_error("ov002 sprite rectangle is out of range");
    }
    khdays::assets::DecodedTexture out;
    out.name = name;
    out.format_name = "ov002-layout-sprite";
    out.color_zero_transparent = true;
    out.width = rectangle.width;
    out.height = rectangle.height;
    out.rgba.resize(
        static_cast<std::size_t>(out.width) * out.height * 4U);
    for (int row = 0; row < out.height; ++row) {
        const std::size_t source_begin =
            (static_cast<std::size_t>(rectangle.source_y + row) * source.width
             + static_cast<std::size_t>(rectangle.source_x)) * 4U;
        const std::size_t destination_begin =
            static_cast<std::size_t>(row) * out.width * 4U;
        std::copy_n(
            source.rgba.begin()
                + static_cast<std::ptrdiff_t>(source_begin),
            static_cast<std::size_t>(out.width) * 4U,
            out.rgba.begin()
                + static_cast<std::ptrdiff_t>(destination_begin));
    }
    return out;
}

khdays::assets::TileGraphics tile_rectangle(
    const khdays::assets::TileGraphics& source,
    const int first,
    const int width,
    const int height,
    const int source_stride) {
    khdays::assets::TileGraphics out;
    out.bpp = source.bpp;
    out.tile_count = width * height;
    out.indices.reserve(static_cast<std::size_t>(out.tile_count) * 64U);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const int tile = first + row * source_stride + column;
            if (tile < 0 || tile >= source.tile_count) {
                throw std::runtime_error(
                    "battle HUD tile rectangle is out of range");
            }
            const auto begin = source.indices.begin()
                + static_cast<std::ptrdiff_t>(tile) * 64;
            out.indices.insert(out.indices.end(), begin, begin + 64);
        }
    }
    return out;
}

std::array<std::uint8_t, 4> colour_bgr555(
    const std::uint16_t value) {
    const auto expand = [](const std::uint16_t channel) {
        return static_cast<std::uint8_t>(
            (channel * 255U + 15U) / 31U);
    };
    return {
        expand(value & 31U),
        expand((value >> 5U) & 31U),
        expand((value >> 10U) & 31U),
        255U};
}

std::vector<std::uint8_t> decompress_if_needed(
    std::vector<std::uint8_t> bytes) {
    if (!bytes.empty() && (bytes[0] == 0x10U || bytes[0] == 0x11U)) {
        return khdays::assets::lz_decompress(bytes);
    }
    return bytes;
}

std::optional<SpriteSet> decode_sprite_set(
    const std::vector<std::uint8_t>& pack) {
    const auto pal = khdays::assets::find_nitro_resource(
        pack.data(), pack.size(), "RLCN");
    const auto chr = khdays::assets::find_nitro_resource(
        pack.data(), pack.size(), "RGCN");
    const auto cer = khdays::assets::find_nitro_resource(
        pack.data(), pack.size(), "RECN");
    const auto nan = khdays::assets::find_nitro_resource(
        pack.data(), pack.size(), "RNAN");
    if (!pal || !chr || !cer) {
        return std::nullopt;
    }
    const auto palette = khdays::assets::decode_nclr(pal.data, pal.size);
    const auto tiles = khdays::assets::decode_ncgr(chr.data, chr.size);
    const auto bank = khdays::assets::decode_ncer(cer.data, cer.size);
    SpriteSet set;
    set.cells.reserve(bank.cells.size());
    set.cell_origins.reserve(bank.cells.size());
    for (const auto& cell : bank.cells) {
        set.cells.push_back(khdays::assets::render_cell(
            cell, tiles, palette, bank.tile_boundary));
        int min_x = 0;
        int min_y = 0;
        bool first = true;
        for (const auto& piece : cell.pieces) {
            if (first) {
                min_x = piece.x;
                min_y = piece.y;
                first = false;
            } else {
                min_x = std::min(min_x, piece.x);
                min_y = std::min(min_y, piece.y);
            }
        }
        set.cell_origins.push_back({min_x, min_y});
    }
    if (nan) {
        set.animations = khdays::assets::decode_nanr(nan.data, nan.size);
    }
    return set;
}

}  // namespace

std::optional<khdays::assets::UiLayout> load_ui_layout(const char* game_path) {
    try {
        const auto blob = khdays::vfs::read(game_path);
        return khdays::assets::decode_ui_layout(blob.data(), blob.size());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<SpriteSet> load_sprite_set(const char* game_path,
                                         const std::size_t subfile) {
    try {
        const auto container = khdays::vfs::read(game_path);
        const auto pack = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), subfile);
        return decode_sprite_set(pack);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<SpriteSet> load_sprite_container(const char* game_path) {
    try {
        return decode_sprite_set(decompress_if_needed(
            khdays::vfs::read(game_path)));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::array<khdays::assets::DecodedTexture, 10>>
load_calendar_digits() {
    try {
        std::array<khdays::assets::DecodedTexture, 10> digits;
        for (std::size_t digit = 0; digit < digits.size(); ++digit) {
            const std::string path =
                "UI/cal/" + std::to_string(digit) + "_a.pak.z";
            const auto pack = decompress_if_needed(khdays::vfs::read(path));
            const auto bmd0 = khdays::assets::find_nitro_resource(
                pack.data(), pack.size(), "BMD0");
            if (!bmd0) {
                return std::nullopt;
            }
            const auto model = khdays::assets::decode_model_geometry(
                bmd0.data, bmd0.size);
            const auto mesh = std::find_if(
                model.meshes.begin(), model.meshes.end(),
                [](const auto& value) { return !value.texture_name.empty(); });
            if (mesh == model.meshes.end()) {
                return std::nullopt;
            }
            digits[digit] = khdays::assets::load_tex0_texture(
                bmd0.data, bmd0.size, mesh->texture_name);
        }
        return digits;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<khdays::assets::DecodedTexture> load_boot_logo() {
    try {
        const auto container = khdays::vfs::read("ttl/ttl.p2");
        const auto pack = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 1);
        const auto pal = khdays::assets::find_nitro_resource(
            pack.data(), pack.size(), "RLCN");
        const auto chr = khdays::assets::find_nitro_resource(
            pack.data(), pack.size(), "RGCN");
        const auto scr = khdays::assets::find_nitro_resource(
            pack.data(), pack.size(), "RCSN");
        if (!pal || !chr || !scr) {
            return std::nullopt;
        }
        const auto palette = khdays::assets::decode_nclr(pal.data, pal.size);
        const auto tiles = khdays::assets::decode_ncgr(chr.data, chr.size);
        const auto map = khdays::assets::decode_nscr(scr.data, scr.size);
        return khdays::assets::compose_background(map, tiles, palette, false);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<khdays::assets::DecodedTexture> load_title_logo(
    const bool over_white, const float scale, const float y_offset,
    const char* only_texture) {
    try {
        const auto container = khdays::vfs::read("ttl/ttl.p2");
        const auto kaph = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 0);
        // The KAPH pack embeds a standard BMD0/NSBMD (BOM at +4), so the generic
        // Nitro-resource scan carves it out.
        const auto bmd0 = khdays::assets::find_nitro_resource(
            kaph.data(), kaph.size(), "BMD0");
        if (!bmd0) {
            return std::nullopt;
        }
        const auto model =
            khdays::assets::decode_model_geometry(bmd0.data, bmd0.size);
        std::map<std::string, khdays::assets::DecodedTexture> textures;
        for (const auto& mesh : model.meshes) {
            if (mesh.texture_name.empty()
                || textures.count(mesh.texture_name) != 0U) {
                continue;
            }
            textures.emplace(
                mesh.texture_name,
                khdays::assets::load_tex0_texture(bmd0.data, bmd0.size,
                                                  mesh.texture_name));
        }
        // The logo sits on the white top screen; composite it over white so the
        // scene can draw it as the whole top screen.
        const auto logo = khdays::assets::compose_flat_model(
            model, textures, 256, 192, scale, y_offset, only_texture);
        if (!over_white) {
            return logo;  // keep the logo's own alpha for overlaying
        }
        khdays::assets::DecodedTexture out;
        out.width = 256;
        out.height = 192;
        out.rgba.assign(static_cast<std::size_t>(256) * 192 * 4, 255);
        for (std::size_t i = 0; i + 4U <= logo.rgba.size(); i += 4U) {
            const std::uint8_t a = logo.rgba[i + 3U];
            if (a == 0U) {
                continue;
            }
            out.rgba[i] = static_cast<std::uint8_t>(
                (logo.rgba[i] * a + out.rgba[i] * (255 - a)) / 255);
            out.rgba[i + 1U] = static_cast<std::uint8_t>(
                (logo.rgba[i + 1U] * a + out.rgba[i + 1U] * (255 - a)) / 255);
            out.rgba[i + 2U] = static_cast<std::uint8_t>(
                (logo.rgba[i + 2U] * a + out.rgba[i + 2U] * (255 - a)) / 255);
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<TitleLogoModel> load_title_logo_model() {
    try {
        const auto container = khdays::vfs::read("ttl/ttl.p2");
        const auto kaph = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 0);
        const auto bmd0 = khdays::assets::find_nitro_resource(
            kaph.data(), kaph.size(), "BMD0");
        if (!bmd0) {
            return std::nullopt;
        }
        TitleLogoModel out;
        out.model = khdays::assets::decode_model_geometry(bmd0.data, bmd0.size);
        for (const auto& mesh : out.model.meshes) {
            if (mesh.texture_name.empty()
                || out.textures.count(mesh.texture_name) != 0U) {
                continue;
            }
            out.textures.emplace(
                mesh.texture_name,
                khdays::assets::load_tex0_texture(bmd0.data, bmd0.size,
                                                  mesh.texture_name));
        }
        // The joint animation rides in the same KAPH as a BCA0.
        const auto bca0 = khdays::assets::find_nitro_resource(
            kaph.data(), kaph.size(), "BCA0");
        if (bca0) {
            out.animation = khdays::assets::load_nsbca(bca0.data, bca0.size, 0);
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<khdays::assets::DecodedTexture> load_ui_background(
    const char* game_path, const std::size_t subfile, const std::size_t screen,
    const std::size_t tiles_index, const std::size_t palette_index) {
    try {
        const auto container = khdays::vfs::read(game_path);
        const auto blob = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), subfile);
        const auto pack = khdays::assets::parse_pk2d(blob.data(), blob.size());
        if (screen >= pack.screens.size() || tiles_index >= pack.tiles.size()
            || palette_index >= pack.palettes.size()) {
            return std::nullopt;
        }
        const auto map = khdays::assets::decode_nscr(
            pack.screens[screen].data, pack.screens[screen].size);
        const auto tiles = khdays::assets::decode_ncgr(
            pack.tiles[tiles_index].data, pack.tiles[tiles_index].size);
        const auto palette = khdays::assets::decode_nclr(
            pack.palettes[palette_index].data, pack.palettes[palette_index].size);
        return khdays::assets::compose_background(map, tiles, palette, false);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<OpeningArtwork> load_opening_artwork(
    const std::size_t language_subfile) {
    try {
        const auto container = khdays::vfs::read("op/op.p2");
        const auto base_blob = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 0);
        const auto localized_blob = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), language_subfile);
        const auto base = khdays::assets::parse_pk2d(
            base_blob.data(), base_blob.size());
        const auto localized = khdays::assets::parse_pk2d(
            localized_blob.data(), localized_blob.size());
        if (base.palettes.size() < 14U || base.tiles.size() < 28U
            || base.screens.size() < 28U || localized.tiles.size() < 13U
            || localized.screens.size() < 13U) {
            return std::nullopt;
        }

        const auto compose = [](
            const khdays::assets::ResourceView& screen,
            const khdays::assets::ResourceView& tiles,
            const khdays::assets::ResourceView& palette) {
            const auto map = khdays::assets::decode_nscr(
                screen.data, screen.size);
            const auto chars = khdays::assets::decode_ncgr(
                tiles.data, tiles.size);
            const auto colors = khdays::assets::decode_nclr(
                palette.data, palette.size);
            return khdays::assets::compose_background(
                map, chars, colors, /*color_zero_transparent=*/true);
        };

        OpeningArtwork out;
        for (std::size_t card = 0; card < out.base.size(); ++card) {
            out.base[card] = compose(
                base.screens[card * 2U], base.tiles[card * 2U],
                base.palettes[card]);
            out.accent[card] = compose(
                base.screens[card * 2U + 1U], base.tiles[card * 2U + 1U],
                base.palettes[card]);
            if (card < out.localized.size()) {
                out.localized[card] = compose(
                    localized.screens[card], localized.tiles[card],
                    base.palettes[card]);
            }
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<BattleHudArtwork> load_battle_hud_artwork() {
    try {
        const auto container = khdays::vfs::read("UI/btl/main.p2");

        // Ov002_OpenPanelScreen selects sub-file 4 for the normal 4bpp battle
        // HUD. Ov002_LoadPanelSlots takes the portrait sheets from sub-file 1.
        const auto hud_blob = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 4U);
        const auto portraits_blob = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), 1U);
        const auto hud_pack =
            khdays::assets::parse_pk2d(hud_blob.data(), hud_blob.size());
        const auto portraits_pack = khdays::assets::parse_pk2d(
            portraits_blob.data(), portraits_blob.size());
        if (hud_pack.tiles.empty() || hud_pack.palettes.empty()
            || portraits_pack.tiles.empty()
            || portraits_pack.palettes.empty()) {
            return std::nullopt;
        }

        const auto hud_tiles = khdays::assets::decode_ncgr(
            hud_pack.tiles[0].data, hud_pack.tiles[0].size);
        const auto hud_palette = khdays::assets::decode_nclr(
            hud_pack.palettes[0].data, hud_pack.palettes[0].size);
        if (hud_palette.colors.size() < 16U) {
            return std::nullopt;
        }

        BattleHudArtwork out;
        // data_ov002_0207ddfc starts the local-player buffer at byte 0x800:
        // tile 64, ten 4bpp tiles (0x140 bytes). The 1P and HP labels are the
        // two-tile runs referenced by the same base character sheet.
        const auto gauge_tiles = tile_rectangle(
            hud_tiles, 64, 10, 1, 10);
        out.player_gauge.empty = khdays::assets::render_tile_sheet(
            gauge_tiles, hud_palette, 0, 10, true);
        for (std::size_t i = 0; i < out.player_gauge.palette.size(); ++i) {
            out.player_gauge.palette[i] = hud_palette.colors[i];
        }
        out.player_label = khdays::assets::render_tile_sheet(
            tile_rectangle(hud_tiles, 1, 2, 1, 2),
            hud_palette, 0, 2, true);
        out.hp_label = khdays::assets::render_tile_sheet(
            tile_rectangle(hud_tiles, 53, 2, 1, 2),
            hud_palette, 0, 2, true);

        auto portrait_tiles = khdays::assets::decode_ncgr(
            portraits_pack.tiles[0].data, portraits_pack.tiles[0].size);
        auto portrait_palette = khdays::assets::decode_nclr(
            portraits_pack.palettes[0].data,
            portraits_pack.palettes[0].size);
        // The playable Roxas member kind maps through
        // data_ov002_0207ef68 to icon 12. Slot 0 copies six tiles from each
        // nine-tile row, beginning one source row into that icon. Palette
        // entry 15 is replaced with slot-0 colour 0x7d00.
        constexpr int roxas_icon = 12;
        constexpr int icon_tiles = 54;
        constexpr int portrait_first =
            9 + roxas_icon * icon_tiles;
        constexpr int portrait_palette_index = roxas_icon + 1;
        if (portrait_palette.colors_per_palette != 16
            || portrait_palette.colors.size()
                < static_cast<std::size_t>(
                    (portrait_palette_index + 1) * 16)) {
            return std::nullopt;
        }
        portrait_palette.colors[
            static_cast<std::size_t>(portrait_palette_index * 16 + 15)] =
            colour_bgr555(0x7d00U);
        out.roxas_portrait = khdays::assets::render_tile_sheet(
            tile_rectangle(portrait_tiles, portrait_first, 6, 6, 9),
            portrait_palette, portrait_palette_index, 6, true);
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<khdays::assets::Ov002CommandMenuArtwork>
load_ov002_command_menu(
    const char* localized_main_game_path,
    const char* command_table_game_path,
    const char* font_game_path) {
    try {
        const auto base_container = khdays::vfs::read("UI/btl/main.p2");
        const auto layout = khdays::assets::extract_p2_subfile(
            base_container.data(), base_container.size(), 0U);
        const auto palette_blob = khdays::assets::extract_p2_subfile(
            base_container.data(), base_container.size(), 4U);
        const auto palette_pack = khdays::assets::parse_pk2d(
            palette_blob.data(), palette_blob.size());

        const auto screen = find_ov002_layout_screen(layout);
        const auto idle = find_ov002_sprite(layout, 0x60U);
        const auto selected = find_ov002_sprite(layout, 0x61U);
        const auto header = find_ov002_sprite(layout, 0x63U);
        if (palette_pack.palettes.empty() || palette_pack.tiles.empty() || !screen
            || !idle || !selected || !header) {
            return std::nullopt;
        }

        const auto map = khdays::assets::decode_nscr(screen.data, screen.size);
        // English is the base character sheet in main.p2 sub-file 4. The EU
        // ROM only ships separate de/es/fr/it main.p2 files, each replacing
        // that sheet through its sub-file 0.
        auto tiles = khdays::assets::decode_ncgr(
            palette_pack.tiles[0].data, palette_pack.tiles[0].size);
        if (khdays::vfs::exists(localized_main_game_path)) {
            const auto localized_container =
                khdays::vfs::read(localized_main_game_path);
            const auto localized_tiles_blob =
                khdays::assets::extract_p2_subfile(
                    localized_container.data(), localized_container.size(), 0U);
            const auto localized_tiles = khdays::assets::find_nitro_resource(
                localized_tiles_blob.data(), localized_tiles_blob.size(),
                "RGCN");
            if (!localized_tiles) {
                return std::nullopt;
            }
            tiles = khdays::assets::decode_ncgr(
                localized_tiles.data, localized_tiles.size);
        }
        const auto palette = khdays::assets::decode_nclr(
            palette_pack.palettes[0].data,
            palette_pack.palettes[0].size);
        const auto sheet = khdays::assets::compose_background(
            map, tiles, palette, true);

        const auto command_path = khdays::vfs::resolve(
            command_table_game_path);
        const auto font_path = khdays::vfs::resolve(font_game_path);
        if (!command_path || !font_path) {
            return std::nullopt;
        }
        const auto commands = khdays::resource::load_string_table(
            *command_path);
        if (commands.size() < 3U) {
            return std::nullopt;
        }
        const auto font = khdays::resource::load_font(*font_path);

        khdays::assets::Ov002CommandMenuArtwork out;
        out.idle_row = crop_texture(sheet, *idle, "ov002_command_idle");
        out.selected_row = crop_texture(
            sheet, *selected, "ov002_command_selected");
        out.header = crop_texture(sheet, *header, "ov002_command_header");
        for (std::size_t index = 0; index < out.labels.size(); ++index) {
            out.labels[index] = khdays::assets::render_text(
                font, commands[index]);
            out.labels[index].name = "ov002_command_label_"
                + std::to_string(index);
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<khdays::assets::DecodedTexture> load_ui_tile_atlas(
    const char* game_path, const std::size_t subfile,
    const std::size_t tiles_index, const std::size_t palette_index) {
    try {
        const auto container = khdays::vfs::read(game_path);
        const auto blob = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), subfile);
        const auto pack = khdays::assets::parse_pk2d(blob.data(), blob.size());
        if (tiles_index >= pack.tiles.size()
            || palette_index >= pack.palettes.size()) {
            return std::nullopt;
        }
        const auto tiles = khdays::assets::decode_ncgr(
            pack.tiles[tiles_index].data, pack.tiles[tiles_index].size);
        const auto palette = khdays::assets::decode_nclr(
            pack.palettes[palette_index].data,
            pack.palettes[palette_index].size);
        return khdays::assets::render_tile_sheet(
            tiles, palette, 0, 16, true);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<khdays::assets::DecodedTexture> render_ui_text(
    const char* font_game_path, const std::u16string& text) {
    const auto font_path = khdays::vfs::resolve(font_game_path);
    if (!font_path || text.empty()) {
        return std::nullopt;
    }
    try {
        const auto font = khdays::resource::load_font(*font_path);
        return khdays::assets::render_text(font, text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace khdays::resource
