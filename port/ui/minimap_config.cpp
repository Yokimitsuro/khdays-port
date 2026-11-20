#include "minimap_config.h"
#include "flat_json.h"
#include "kh2_portrait.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <variant>
namespace khdays::ui::minimap {
namespace {
using json::Object;
constexpr const char *kWhat = "minimap";
// sprites, characters and players, each of objects of leaves; leaves in the file's own
const json::Rules kRules{
    3,
    [](const std::string &, const std::string &key, int depth) {
        return (depth == 0 && (key == "sprites" || key == "characters" || key == "players")) || depth == 1;
    },
    [](const std::string &, int depth) { return depth != 1; }};
template <class T> T get(Object &o, const std::string &key, T fallback)
{
    return json::get(o, key, fallback, kWhat);
}
double number(Object &o, const std::string &key, double fallback, double lo, double hi, bool integer = false)
{
    return json::number(o, key, fallback, lo, hi, integer, kWhat);
}
void filename(const std::string &name)
{
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
        throw std::runtime_error("minimap JSON: texture must be a file name in the minimap folder");
}
} // namespace
Config load_config(const std::filesystem::path &path)
{
    auto o = json::read_file(path, kWhat, kRules);
    Config c;
    number(o, "version", 1, 1, 1, true);
    c.enabled = get(o, "enabled", false);
    c.kh2map = get(o, "kh2map", false);
    c.map_texture = get(o, "map_texture", c.map_texture);
    filename(c.map_texture);
    c.map_scale = float(number(o, "map_scale", .65, .1, 4));
    c.map_offset_x = float(number(o, "map_offset_x", 0, -512, 512));
    c.map_offset_y = float(number(o, "map_offset_y", 0, -512, 512));
    c.map_opacity = float(number(o, "map_opacity", .75, 0, 1));
    c.map_icon_scale = float(number(o, "map_icon_scale", 2, .1, 8));
    c.texture = get(o, "texture", c.texture);
    filename(c.texture);
    c.width = int(number(o, "logical_width", 256, 1, 4096, true));
    c.height = int(number(o, "logical_height", 128, 1, 4096, true));
    std::set<std::string> names;
    for (const auto &[key, v] : o)
        if (key.starts_with("sprites.")) {
            auto end = key.find('.', 8);
            if (end == key.npos)
                throw std::runtime_error("minimap JSON: invalid sprite");
            names.insert(key.substr(8, end - 8));
        }
    for (const auto &name : names) {
        Sprite s;
        auto p = "sprites." + name + ".";
        s.x = float(number(o, p + "x", 0, 0, c.width));
        s.y = float(number(o, p + "y", 0, 0, c.height));
        s.width = float(number(o, p + "width", 8, 0.01, c.width));
        s.height = float(number(o, p + "height", 8, 0.01, c.height));
        s.draw_width = float(number(o, p + "draw_width", s.width, 0.01, 128));
        s.draw_height = float(number(o, p + "draw_height", s.height, 0.01, 128));
        s.pivot_x = float(number(o, p + "pivot_x", s.draw_width / 2, -128, 128));
        s.pivot_y = float(number(o, p + "pivot_y", s.draw_height / 2, -128, 128));
        s.scale = float(number(o, p + "scale", 1, .1, 8));
        s.offset_x = float(number(o, p + "offset_x", 0, -128, 128));
        s.offset_y = float(number(o, p + "offset_y", 0, -128, 128));
        if (s.x + s.width > c.width || s.y + s.height > c.height)
            throw std::runtime_error("minimap JSON: sprite outside sheet");
        c.sprites.emplace(name, s);
    }
    auto player = [&](std::string p, bool active) {
        Player r;
        r.enabled = get(o, p + "enabled", active);
        r.smooth_rotation = get(o, p + "smooth_rotation", false);
        r.sprite = get(o, p + "sprite", r.sprite);
        auto ext = std::filesystem::path(r.sprite).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        if (ext == ".png" || ext == ".dds")
            filename(r.sprite);
        r.scale = float(number(o, p + "scale", 1, .1, 8));
        r.rotation_offset = float(number(o, p + "rotation_offset", 0, -360, 360));
        return r;
    };
    for (int i = 0; i < 4; ++i)
        c.players[i] = player("players." + std::to_string(i + 1) + ".", true);
    for (const auto *name : khdays::ui::kh2::portrait_names)
        c.characters.emplace(name, player("characters." + std::string(name) + ".", false));
    if (!o.empty())
        throw std::runtime_error("minimap JSON: unknown key " + o.begin()->first);
    return c;
}
} // namespace khdays::ui::minimap
