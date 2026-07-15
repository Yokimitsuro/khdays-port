#include "khdays/resource/ui_content.h"

#include <exception>

#include "khdays/resource/loader.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::resource {

std::optional<SpriteSet> load_sprite_set(const char* game_path,
                                         const std::size_t subfile) {
    try {
        const auto container = khdays::vfs::read(game_path);
        const auto pack = khdays::assets::extract_p2_subfile(
            container.data(), container.size(), subfile);
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
        for (const auto& cell : bank.cells) {
            set.cells.push_back(khdays::assets::render_cell(
                cell, tiles, palette, bank.tile_boundary));
        }
        if (nan) {
            set.animations = khdays::assets::decode_nanr(nan.data, nan.size);
        }
        return set;
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
