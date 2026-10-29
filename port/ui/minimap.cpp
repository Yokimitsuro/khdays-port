// Sprite-only minimap layer, drawn by the existing GPU quad renderer at the native layer's position.
#include "../host/app/hud_settings.h"
#include "minimap.h"
#include "minimap_config.h"
#include "kh2_portrait.h"
#include "../gfx/kh2_sequence.h"
#include "khdays/assets/image.h"
#include "khdays/assets/dds.h"
extern "C" {
#include "elements.h"
#include "bg.h"
#include "parts.h"
#include "situation.h"
#include "../runtime/core/window.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/mods/textures.h"
#include "../runtime/mods/tiles2d.h"
#include "../runtime/menus/camp.h"
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
namespace {
namespace cfg = khdays::ui::minimap;
namespace seq = khdays::assets::kh2;
cfg::Config config;
std::vector<unsigned> pixels;
struct Draw {
    std::string sprite;
    float x, y, angle = 0, scale = 1;
};
std::vector<Draw> draws;
std::array<KhdaysSequenceImage, KHDAYS_MINIMAP_BATCHES> programs{};
KhdaysSequenceImage radar_program{};
std::vector<unsigned> mask_pixels;
int mask_atlas = -1, terrain_image = -1, terrain_scale = 0, radar_output = -1, radar_resolution = 0;
unsigned terrain_hash = 0;
const char *radar_status = "";
bool terrain_unavailable(const char *reason)
{
    if (std::strcmp(radar_status, reason)) {
        khdays_log("minimap radar: %s; retaining existing map presentation", reason);
        radar_status = reason;
    }
    return false;
}
void clear_programs()
{
    for (auto &program : programs)
        std::fill_n(program.words, KSI_WORDS, 0u);
    std::fill_n(radar_program.words, KSI_WORDS, 0u);
}
bool tried = false, loaded = false, recording = false, complete = false, overflow = false, owns = false;
unsigned owner = 0;
int atlas = -1, output = -1, output_scale = 0, layer = -1;
KhdaysSituationInputs context{};
std::filesystem::path path(const std::string &s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
unsigned read(unsigned p)
{
    unsigned v = 0;
    if (khdays_game_ram(p) && khdays_game_ram(p + 3))
        std::memcpy(&v, KHDAYS_W(p), 4);
    return v;
}
void fail(const char *why)
{
    loaded = owns = recording = complete = false;
    clear_programs();
    khdays_log("minimap: %s; retaining native icons", why);
}
khdays::assets::DecodedTexture image(const std::filesystem::path &file)
{
    auto ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".dds" ? khdays::assets::read_dds(file) : khdays::assets::read_image(file);
}
// Pack standalone overrides once, preserving their resolution and the shared GPU batch path.
void overrides(const std::filesystem::path &folder, khdays::assets::DecodedTexture &sheet)
{
    float factor = float(sheet.width) / config.width;
    for (auto &[name, s] : config.sprites) {
        s.x *= factor;
        s.y *= factor;
        s.width *= factor;
        s.height *= factor;
    }
    config.width = sheet.width;
    config.height = sheet.height;
    std::set<std::string> seen;
    auto append = [&](const cfg::Player &policy) {
        const auto &name = policy.sprite;
        if (!policy.enabled || name.empty() || config.sprites.contains(name) || !seen.insert(name).second)
            return;
        try {
            auto ext = path(name).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".png" && ext != ".dds")
                throw std::runtime_error("expected an atlas sprite name or a PNG/DDS filename");
            auto icon = image(folder / path(name));
            int w = std::max(sheet.width, icon.width + 2), h = sheet.height + icon.height + 2;
            if (w > 16384 || h > 16384 || size_t(w) * h > 64 * 1024 * 1024)
                throw std::runtime_error("combined minimap atlas is too large");
            std::vector<std::uint8_t> rgba(size_t(w) * h * 4);
            for (int y = 0; y < sheet.height; ++y)
                std::memcpy(rgba.data() + size_t(y) * w * 4, sheet.rgba.data() + size_t(y) * sheet.width * 4,
                            size_t(sheet.width) * 4);
            for (int y = 0; y < icon.height; ++y)
                std::memcpy(rgba.data() + (size_t(y + sheet.height + 1) * w + 1) * 4,
                            icon.rgba.data() + size_t(y) * icon.width * 4, size_t(icon.width) * 4);
            cfg::Sprite s;
            s.x = 1;
            s.y = float(sheet.height + 1);
            s.width = float(icon.width);
            s.height = float(icon.height);
            float ratio = 8.f / std::max(icon.width, icon.height);
            s.draw_width = icon.width * ratio;
            s.draw_height = icon.height * ratio;
            s.pivot_x = s.draw_width / 2;
            s.pivot_y = s.draw_height / 2;
            config.sprites.emplace(name, s);
            sheet.width = config.width = w;
            sheet.height = config.height = h;
            sheet.rgba = std::move(rgba);
        } catch (const std::exception &e) {
            khdays_log("minimap: cannot load optional sprite %s: %s; retaining player fallback", name.c_str(),
                       e.what());
        }
    };
    for (const auto &p : config.players)
        append(p);
    for (const auto &[name, p] : config.characters)
        append(p);
}
void load()
{
    if (tried)
        return;
    tried = true;
    char found[2048];
    if (!khdays_mods_find("layouts/minimap/minimap.json", found, sizeof(found)))
        return;
    try {
        config = cfg::load_config(path(found));
        if (!config.enabled)
            return;
        auto folder = path(found).parent_path();
        auto texture = image(folder / path(config.texture));
        if (texture.width % config.width || texture.height % config.height ||
            texture.width / config.width != texture.height / config.height)
            throw std::runtime_error("sheet must be an equal integer multiple of logical_width and logical_height");
        overrides(folder, texture);
        pixels.resize(texture.rgba.size() / 4);
        std::memcpy(pixels.data(), texture.rgba.data(), texture.rgba.size());
        atlas = khdays_textures_add_info("minimap:atlas", texture.width, texture.height, pixels.data(),
                                         KHDAYS_REPLACEMENT_NO_MIPS);
        if (atlas < 0)
            throw std::runtime_error("could not register sprite sheet");
        loaded = true;
        if (config.kh2map) {
            try {
                auto mask = image(folder / path(config.map_texture));
                if (mask.width % 64 || mask.height % 16 || mask.width / 64 != mask.height / 16)
                    throw std::runtime_error("radar mask must have dimensions proportional to 64x16");
                mask_pixels.resize(mask.rgba.size() / 4);
                std::memcpy(mask_pixels.data(), mask.rgba.data(), mask.rgba.size());
                mask_atlas = khdays_textures_add_info("minimap:radar_mask", mask.width, mask.height, mask_pixels.data(),
                                                      KHDAYS_REPLACEMENT_NO_MIPS);
                if (mask_atlas >= 0)
                    khdays_log("minimap radar: KH2 presentation enabled, mask %s", config.map_texture.c_str());
            } catch (const std::exception &e) {
                khdays_log("minimap: %s; retaining existing map presentation", e.what());
            }
        }
    } catch (const std::exception &e) {
        fail(e.what());
    }
}
bool ready()
{
    load();
    return loaded && khdays_textures_hd() && khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON;
}
void push(std::string sprite, float x, float y, float angle = 0, float scale = 1)
{
    if (!recording)
        return;
    if (auto found = config.sprites.find(sprite); found != config.sprites.end()) {
        const auto &s = found->second;
        float radius = std::hypot(std::max(std::abs(s.pivot_x), std::abs(s.draw_width - s.pivot_x)),
                                  std::max(std::abs(s.pivot_y), std::abs(s.draw_height - s.pivot_y))) *
                       s.scale * scale;
        radius *= (config.kh2map && khdays_hud_setting(KHDAYS_OPT_MINIMAP)) ? std::max(1.f, config.map_icon_scale) : 1;
        if (x + s.offset_x + radius <= 0 || x + s.offset_x - radius >= 120 ||
            y + s.offset_y + radius <= ((config.kh2map && khdays_hud_setting(KHDAYS_OPT_MINIMAP)) ? -8 : 0) ||
            y + s.offset_y - radius >= ((config.kh2map && khdays_hud_setting(KHDAYS_OPT_MINIMAP)) ? 88 : 80))
            return;
    }
    if (draws.size() >= KHDAYS_MINIMAP_BATCHES * KSI_MAX_QUADS) {
        overflow = true;
        return;
    }
    draws.push_back({std::move(sprite), x, y, angle, scale});
}
std::array<int, 2> cell(int x, int y)
{
    unsigned p = read(0x0207f638);
    return {((x + 0x800) >> 12) - int(read(p + 8)), ((y + 0x800) >> 12) - int(read(p + 12))};
}
std::string character(int player)
{
    unsigned panel = read(0x0207f614);
    if (!khdays_game_ram(panel))
        return {};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned record = panel + 0x130 + 16 * i;
        int id = int(read(record)), icon = int(read(record + 4));
        if (id == player && icon >= 0 && icon < int(khdays::ui::kh2::portrait_names.size()))
            return khdays::ui::kh2::portrait_names[icon];
    }
    return {};
}
// Infer the dynamic buffer's placement from its actual BG map, not from a single-screen crop.
bool locate(float &x, float &y)
{
    for (int bg : {2, 3}) {
        KhdaysBgLayer l;
        if (!khdays_bg_text_layer(1, bg, &l) || l.colors256)
            continue;
        for (int cy = 0; cy < 24; ++cy)
            for (int cx = 0; cx < 32; ++cx) {
                unsigned e = khdays_bg_map_entry(1, &l, (l.hofs >> 3) + cx, (l.vofs >> 3) + cy);
                unsigned tile = e & 1023u;
                if ((e >> 12) != 13 || tile < 0x139 || tile >= 0x1cf)
                    continue;
                unsigned n = tile - 0x139;
                x = float(cx * 8 - (l.hofs & 7) - int(n % 15) * 8);
                y = float(cy * 8 - (l.vofs & 7) - int(n / 15) * 8);
                if (x < 80 || x > 128 || y < 32 || y > 80)
                    continue;
                layer = bg;
                return true;
            }
    }
    return false;
}
// The NSCR places packed map tiles into a 32x32 grid, including repeated padding tiles.
bool terrain()
{
    if ((read(owner + 0x20) & 255) >= 2)
        return terrain_unavailable("native map is hidden");
    KhdaysBgLayer bg;
    if (!khdays_bg_text_layer(1, 3, &bg) || bg.colors256)
        return terrain_unavailable("terrain BG3 is unavailable");
    unsigned entry = khdays_bg_map_entry(1, &bg, 0, 0);
    if ((entry & 1023u) != 0x1cf)
        return terrain_unavailable("terrain tile map is not installed");
    const unsigned first = bg.chars + 0x39e0;
    const char *source;
    unsigned source_offset, run;
    if (!khdays_bg_source(1, first, &source, &source_offset, &run) || !std::strstr(source, "mapchr.p2"))
        return terrain_unavailable("resident map source is unavailable");
    std::vector<unsigned char> data;
    data.reserve(32768);
    for (unsigned tile = 0; tile < 1024; ++tile) {
        const char *file;
        unsigned offset;
        const unsigned at = first + tile * 32;
        if (!khdays_bg_source(1, at, &file, &offset, &run) || std::strcmp(file, source) ||
            offset != source_offset + tile * 32 || run < 32)
            break;
        const auto *tile_bytes = khdays_bg_memory(1, at);
        if (!tile_bytes)
            break;
        data.insert(data.end(), tile_bytes, tile_bytes + 32);
    }
    if (data.empty())
        return terrain_unavailable("resident map has no complete tiles");
    std::array<unsigned, 256> colours{};
    std::array<unsigned, 32 * 32> cells;
    std::vector<unsigned> hd(data.size() / 32);
    unsigned hash = 2166136261u;
    auto mix = [&](unsigned v) { hash = (hash ^ v) * 16777619u; };
    unsigned palettes = 0;
    for (unsigned i = 0; i < cells.size(); ++i) {
        cells[i] = khdays_bg_map_entry(1, &bg, i % 32, i / 32) & 0xffffu;
        mix(cells[i]);
        palettes |= 1u << (cells[i] >> 12);
    }
    for (unsigned bank = 0; bank < 16; ++bank)
        if (palettes & (1u << bank))
            for (unsigned i = 0; i < 16; ++i) {
                unsigned index = bank * 16 + i;
                colours[index] = khdays_ui_palette_colour(1, 3, KET_OPAQUE | index);
                mix(colours[index]);
            }
    for (auto byte : data)
        mix(byte);
    int resolution = 1;
    for (unsigned i = 0; i < hd.size(); ++i) {
        hd[i] = khdays_tiles2d_bg_tile(1, bg.chars + (0x1cf + i) * 32);
        mix(hd[i]);
        const auto *r = hd[i] ? khdays_textures_replacement(int(hd[i] >> 20) - 1) : nullptr;
        if (r && (r->info & KHDAYS_REPLACEMENT_TILES)) {
            resolution = std::max(resolution, std::min(8, int((r->info >> 8) & 255)));
            mix(khdays_textures_version(int(hd[i] >> 20) - 1));
        }
    }
    if (terrain_image >= 0 && terrain_hash == hash && terrain_scale == resolution) {
        if (std::strcmp(radar_status, "ready")) {
            khdays_log("minimap radar: ready, cached resident terrain");
            radar_status = "ready";
        }
        return true;
    }
    if (terrain_image < 0 || terrain_scale != resolution) {
        khdays_ui_image_put(terrain_image);
        terrain_image = khdays_ui_image_get(256 * resolution, 256 * resolution);
        terrain_scale = resolution;
    }
    if (terrain_image < 0)
        return false;
    auto *out = khdays_ui_image_begin(terrain_image);
    if (!out)
        return false;
    const int side = 256 * resolution;
    std::fill(out, out + size_t(side) * side, 0);
    for (unsigned cell = 0; cell < cells.size(); ++cell) {
        const unsigned map_entry = cells[cell], tile = (map_entry & 1023u) - 0x1cfu;
        if (tile >= hd.size())
            continue;
        const unsigned palette = (map_entry >> 12) * 16;
        const auto *r = hd[tile] ? khdays_textures_replacement(int(hd[tile] >> 20) - 1) : nullptr;
        int rs = r ? int((r->info >> 8) & 255) : 0, cols = r ? int(r->info >> 16) : 0;
        bool replaced = r && r->rgba && (r->info & KHDAYS_REPLACEMENT_TILES) && rs && cols;
        unsigned index = hd[tile] & 0xfffffu;
        if (replaced &&
            ((index % cols + 1) * 8 * rs > unsigned(r->width) || (index / cols + 1) * 8 * rs > unsigned(r->height)))
            replaced = false;
        for (int y = 0; y < 8 * resolution; ++y)
            for (int x = 0; x < 8 * resolution; ++x) {
                unsigned v = 0, colour = 0;
                unsigned tx = (map_entry & 0x400u) ? 8 * resolution - 1 - x : x;
                unsigned ty = (map_entry & 0x800u) ? 8 * resolution - 1 - y : y;
                if (replaced) {
                    unsigned u = (index % cols) * 8 * rs + (2 * tx + 1) * rs / (2 * resolution);
                    unsigned vv = (index / cols) * 8 * rs + (2 * ty + 1) * rs / (2 * resolution);
                    colour = r->rgba[size_t(vv) * r->width + u];
                    if (r->info & KHDAYS_REPLACEMENT_INDEXED) {
                        v = colour & 15;
                        colour = v ? colours[palette + v] : 0;
                    }
                } else {
                    unsigned px = tx / resolution, py = ty / resolution;
                    v = (data[tile * 32 + py * 4 + px / 2] >> ((px & 1) * 4)) & 15;
                    colour = v ? colours[palette + v] : 0;
                }
                if ((colour & 255) >= 248 && ((colour >> 8) & 255) >= 248 && ((colour >> 16) & 255) >= 248)
                    colour = 0;
                out[(size_t(cell / 32) * 8 * resolution + y) * side + (cell % 32) * 8 * resolution + x] = colour;
            }
    }
    khdays_ui_image_end(terrain_image);
    terrain_hash = hash;
    if (std::strcmp(radar_status, "ready")) {
        khdays_log("minimap radar: ready, %zu resident tiles, terrain resolution %dx", hd.size(), resolution);
        radar_status = "ready";
    }
    return true;
}
seq::SequenceQuad sprite_quad(const Draw &d, bool radar = false)
{
    const auto found = config.sprites.find(d.sprite);
    if (found == config.sprites.end())
        throw std::runtime_error("missing required sprite: " + d.sprite);
    const auto &s = found->second;
    float r = d.angle * 3.14159265358979323846f / 180, c = std::cos(r), sn = std::sin(r), scale = s.scale * d.scale;
    if (radar && !d.sprite.starts_with("emblem"))
        scale *= config.map_icon_scale;
    seq::SequenceQuad q;
    for (unsigned v = 0; v < 4; ++v) {
        float lx = ((v & 1 ? s.draw_width : 0) - s.pivot_x) * scale,
              ly = ((v & 2 ? s.draw_height : 0) - s.pivot_y) * scale;
        q.vertices[v] = {d.x + s.offset_x + lx * c + ly * sn - (radar ? 12 : 0),
                         d.y + s.offset_y - lx * sn + ly * c + (radar ? 8 : 0), s.x + (v & 1 ? s.width : 0),
                         s.y + (v & 2 ? s.height : 0), 0x80808080};
    }
    return q;
}
void radar(int resolution)
{
    if (!config.kh2map || !khdays_hud_setting(KHDAYS_OPT_MINIMAP))
        return;
    if (mask_atlas < 0) {
        terrain_unavailable("radar mask is unavailable");
        return;
    }
    if (draws.size() >= KSI_MAX_QUADS) {
        terrain_unavailable("radar icon capacity exceeded");
        return;
    }
    if (!terrain())
        return;
    if (radar_output < 0 || radar_resolution != resolution) {
        khdays_ui_image_put(radar_output);
        radar_output = khdays_ui_image_get(96 * resolution, 96 * resolution);
        radar_resolution = resolution;
    }
    if (radar_output < 0)
        return;
    auto &p = radar_program;
    auto *w = p.words;
    w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(radar_output));
    w[KSI_ATLAS] = unsigned(khdays_ui_image_replacement(terrain_image));
    w[KSI_LOGICAL_W] = w[KSI_LOGICAL_H] = 256;
    w[KSI_WIDTH] = w[KSI_HEIGHT] = 96 * resolution;
    w[KSI_CLEAR] = 1;
    w[KSI_FACE] = w[KSI_HD_FACE] = ~0u;
    w[KSI_RADAR_MASK] = unsigned(mask_atlas + 1);
    w[KSI_RADAR_ICONS] = unsigned(atlas);
    w[KSI_RADAR_ICON_W] = config.width;
    w[KSI_RADAR_ICON_H] = config.height;
    float factor = float(resolution);
    std::memcpy(w + KSI_SCALE, &factor, 4);
    std::memcpy(w + KSI_RADAR_SCALE, &config.map_scale, 4);
    std::memcpy(w + KSI_RADAR_ALPHA, &config.map_opacity, 4);
    std::memcpy(w + KSI_ANCHOR_X, &config.map_offset_x, 4);
    std::memcpy(w + KSI_ANCHOR_Y, &config.map_offset_y, 4);
    std::vector<seq::SequenceQuad> quads(1);
    float x = -float(int(read(owner + 8))) - 12, y = -float(int(read(owner + 12))) + 8;
    // Cover the radar at map boundaries; the sampler clamps UVs to the terrain's edge texels.
    for (unsigned v = 0; v < 4; ++v)
        quads[0].vertices[v] = {float(v & 1 ? 96 : 0), float(v & 2 ? 96 : 0), float(v & 1 ? 96 : 0) - x,
                                float(v & 2 ? 96 : 0) - y, 0x80808080};
    quads[0].linear_filter = false;
    for (const auto &d : draws)
        quads.push_back(sprite_quad(d, true));
    khdays::render::kh2::encode_sequence_quads(p, quads);
}
} // namespace
extern "C" void khdays_minimap_begin(void)
{
    recording = ready();
    draws.clear();
    complete = overflow = false;
    owner = read(0x0207f638);
}
extern "C" void khdays_minimap_end(void)
{
    complete = recording && !overflow;
    if (recording)
        khdays_ui_changed();
    recording = false;
}
extern "C" void khdays_minimap_icon(int kind, int x, int y)
{
    if (!recording || kind == 3 || kind == 7)
        return;
    auto c = cell(x, y);
    push("entity_" + std::to_string(kind), float(c[0]), float(c[1]));
}
extern "C" void khdays_minimap_player(int player, int x, int y, int heading)
{
    if (!recording || player < 0 || player > 3)
        return;
    auto c = cell(x, y);
    auto policy = config.players[player];
    auto custom = config.characters.find(character(player));
    if (custom != config.characters.end() && custom->second.enabled && !custom->second.sprite.empty() &&
        config.sprites.contains(custom->second.sprite))
        policy = custom->second;
    const int band = ((heading + 0x1000) / 0x2000) % 8;
    float angle = 0;
    std::string sprite = "player" + std::to_string(player + 1) + "_" + std::to_string(band);
    if (policy.enabled && (policy.smooth_rotation || !policy.sprite.empty())) {
        auto proposed = policy.sprite.empty() ? "player" + std::to_string(player + 1) + "_0" : policy.sprite;
        if (config.sprites.contains(proposed)) {
            sprite = proposed;
            angle = policy.smooth_rotation ? float(heading) * 360 / 65536.f : float(band) * 45;
        } else if (policy.smooth_rotation) {
            sprite = "player" + std::to_string(player + 1) + "_0";
            angle = float(heading) * 360 / 65536.f;
        }
    }
    if (policy.enabled)
        angle += policy.rotation_offset;
    push(sprite, float(c[0]), float(c[1]), angle, policy.enabled ? policy.scale : 1);
}
extern "C" void khdays_minimap_emblem(int player)
{
    if (player >= 0 && player < 4)
        push("emblem" + std::to_string(player + 1), 60, 40);
}
extern "C" void khdays_minimap_stamp(unsigned source, int x, int y)
{
    if (!recording)
        return;
    unsigned p = read(0x0207f638), chars = read(read(p + 0x18) + 0x14);
    unsigned offset = source - chars;
    char name[40];
    std::snprintf(name, sizeof(name), "marker_%03x", offset);
    push(name, float(x + 4), float(y + 4));
}
extern "C" void khdays_minimap_reset(void)
{
    draws.clear();
    recording = complete = owns = false;
    owner = 0;
    clear_programs();
    terrain_hash = 0;
}
extern "C" void khdays_minimap_frame(unsigned frame)
{
    khdays_situation_take(&context, frame);
    clear_programs();
    owns = false;
    if (ready() && complete)
        khdays_ui_changed();
}
extern "C" void khdays_minimap_emit(void)
{
    clear_programs();
    owns = false;
    if (!ready() || !complete || owner != read(0x0207f638) || context.scene != 2 || !owner ||
        context.camp_screen != KHDAYS_CAMP_NONE || context.movie || context.tutorial ||
        (context.overlays & ((1ull << 8) | (1ull << 25) | (1ull << 26) | (1ull << 27))))
        return;
    float x, y;
    if (!locate(x, y))
        return;
    try {
        int resolution = khdays_ui_scale();
        if (output < 0 || output_scale != resolution) {
            khdays_ui_image_put(output);
            output = khdays_ui_image_get(120 * resolution, 80 * resolution);
            output_scale = resolution;
        }
        if (output < 0)
            throw std::runtime_error("could not allocate icon layer");
        std::vector<seq::SequenceQuad> quads;
        quads.reserve(draws.size());
        for (const auto &d : draws)
            quads.push_back(sprite_quad(d));
        size_t batches = std::max(size_t(1), (quads.size() + KSI_MAX_QUADS - 1) / KSI_MAX_QUADS);
        for (size_t i = 0; i < batches; ++i) {
            auto &p = programs[i];
            auto *w = p.words;
            w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(output));
            w[KSI_ATLAS] = unsigned(atlas);
            w[KSI_WIDTH] = 120 * resolution;
            w[KSI_HEIGHT] = 80 * resolution;
            w[KSI_LOGICAL_W] = config.width;
            w[KSI_LOGICAL_H] = config.height;
            w[KSI_CLEAR] = i == 0;
            w[KSI_FACE] = w[KSI_HD_FACE] = ~0u;
            w[KSI_ANCHORED] = 1;
            float anchor_x = x + 60, anchor_y = y + 40;
            std::memcpy(w + KSI_ANCHOR_X, &anchor_x, 4);
            std::memcpy(w + KSI_ANCHOR_Y, &anchor_y, 4);
            float factor = float(resolution);
            std::memcpy(w + KSI_SCALE, &factor, 4);
            size_t first = i * KSI_MAX_QUADS, n = std::min(size_t(KSI_MAX_QUADS), quads.size() - first);
            khdays::render::kh2::encode_sequence_quads(p, std::span<const seq::SequenceQuad>(quads).subspan(first, n));
        }
        KhdaysUiElement e{"hud.minimap_icons",
                          1,
                          KES_DIRECT | KES_FINE | KES_PICTURE | KES_NATIVE_FADE | unsigned(layer),
                          x,
                          y,
                          120,
                          80,
                          output};
        khdays_ui_emit(KHDAYS_UI_HUD, &e);
        owns = true;
        try {
            radar(resolution);
        } catch (const std::exception &error) {
            radar_program = {};
            khdays_log("minimap: %s; retaining existing map presentation", error.what());
        }
    } catch (const std::exception &e) {
        fail(e.what());
    }
}
extern "C" int khdays_minimap_cell(int engine, int bg, unsigned entry)
{
    return owns && engine == 1 && bg == layer && (entry >> 12) == 13 && (entry & 1023u) >= 0x139 &&
           (entry & 1023u) < 0x1cf;
}
extern "C" void khdays_minimap_commands(KhdaysSequenceImage *out)
{
    std::copy(programs.begin(), programs.end(), out);
}
extern "C" void khdays_minimap_radar_commands(KhdaysSequenceImage *out)
{
    *out = radar_program;
}
