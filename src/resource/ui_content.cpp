#include "khdays/resource/ui_content.h"

#include "khdays/assets/ui_layout.h"

#include <algorithm>
#include <exception>
#include <map>
#include <string>

#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h"
#include "khdays/assets/screen.h"
#include "khdays/resource/loader.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::resource {

namespace {

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
