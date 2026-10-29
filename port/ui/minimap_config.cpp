#include "minimap_config.h"
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
using Value = std::variant<std::string, double, bool>;
using Object = std::map<std::string, Value>;
// The minimap schema needs objects and scalar leaves, with no external JSON dependency.
class Reader {
    std::string text;
    std::size_t at = 0;
    Object values;
    std::set<std::string> keys;
    [[noreturn]] void fail() const
    {
        throw std::runtime_error("minimap JSON: invalid object at byte " + std::to_string(at));
    }
    char next()
    {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t'))
            ++at;
        return at < text.size() ? text[at] : '\0';
    }
    void take(char c)
    {
        if (next() != c)
            fail();
        ++at;
    }
    unsigned hex()
    {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (at == text.size())
                fail();
            const char c = text[at++];
            const int n = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                 : -1;
            if (n < 0)
                fail();
            value = value * 16 + unsigned(n);
        }
        return value;
    }
    std::string string()
    {
        take('"');
        std::string out;
        while (at < text.size() && text[at] != '"') {
            char c = text[at++];
            if (static_cast<unsigned char>(c) < 32)
                fail();
            if (c == '\\') {
                if (at == text.size())
                    fail();
                c = text[at++];
                switch (c) {
                case '"':
                case '\\':
                case '/': break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    unsigned v = hex();
                    if (v >= 0xd800 && v <= 0xdbff) {
                        if (at + 2 > text.size() || text[at++] != '\\' || text[at++] != 'u')
                            fail();
                        const unsigned low = hex();
                        if (low < 0xdc00 || low > 0xdfff)
                            fail();
                        v = 0x10000 + (v - 0xd800) * 1024 + low - 0xdc00;
                    } else if (v >= 0xdc00 && v <= 0xdfff)
                        fail();
                    if (v < 0x80)
                        out += char(v);
                    else {
                        if (v < 0x800)
                            out += char(0xc0 | (v >> 6));
                        else {
                            if (v < 0x10000)
                                out += char(0xe0 | (v >> 12));
                            else {
                                out += char(0xf0 | (v >> 18));
                                out += char(0x80 | ((v >> 12) & 63));
                            }
                            out += char(0x80 | ((v >> 6) & 63));
                        }
                        out += char(0x80 | (v & 63));
                    }
                    continue;
                }
                default: fail();
                }
            }
            out += c;
        }
        take('"');
        return out;
    }
    Value scalar()
    {
        if (next() == '"')
            return string();
        for (const auto &[word, value] : {std::pair{"true", true}, std::pair{"false", false}}) {
            const std::string token(word);
            if (text.compare(at, token.size(), token) == 0) {
                at += token.size();
                return value;
            }
        }
        const auto start = at;
        if (next() == '-')
            ++at;
        auto digit = [&] { return at < text.size() && text[at] >= '0' && text[at] <= '9'; };
        if (!digit())
            fail();
        if (text[at] == '0')
            ++at;
        else
            while (digit())
                ++at;
        if (at < text.size() && text[at] == '.') {
            ++at;
            if (!digit())
                fail();
            while (digit())
                ++at;
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            ++at;
            if (at < text.size() && (text[at] == '+' || text[at] == '-'))
                ++at;
            if (!digit())
                fail();
            while (digit())
                ++at;
        }
        double n = 0;
        const auto parsed = std::from_chars(text.data() + start, text.data() + at, n);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + at || !std::isfinite(n))
            fail();
        return n;
    }
    void object(const std::string &prefix, int depth)
    {
        if (depth > 3)
            fail();
        take('{');
        if (next() != '}')
            for (;;) {
                const auto key = string();
                if (key.empty() || key.find('.') != std::string::npos)
                    fail();
                const auto name = prefix + key;
                if (!keys.insert(name).second)
                    fail();
                take(':');
                if (next() == '{') {
                    if ((depth == 0 && key != "sprites" && key != "characters" && key != "players") || depth > 1)
                        fail();
                    object(name + '.', depth + 1);
                } else {
                    if (depth == 1)
                        fail();
                    values.emplace(name, scalar());
                }
                if (next() != ',')
                    break;
                ++at;
            }
        take('}');
    }

public:
    explicit Reader(std::string input) : text(std::move(input)) {}
    Object read()
    {
        object("", 0);
        if (next() != '\0' || at != text.size())
            fail();
        return values;
    }
};
template <class T> T get(Object &o, const std::string &key, T fallback)
{
    const auto it = o.find(key);
    if (it == o.end())
        return fallback;
    const auto *v = std::get_if<T>(&it->second);
    if (!v)
        throw std::runtime_error("minimap JSON: wrong type for " + key);
    T result = *v;
    o.erase(it);
    return result;
}
double number(Object &o, const std::string &key, double fallback, double lo, double hi, bool integer = false)
{
    const double n = get(o, key, fallback);
    if (n < lo || n > hi || (integer && n != std::floor(n)))
        throw std::runtime_error("minimap JSON: out of range " + key);
    return n;
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
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() < 0 || f.tellg() > 262144)
        throw std::runtime_error("minimap JSON: cannot read settings");
    std::string text(size_t(f.tellg()), '\0');
    f.seekg(0);
    if (!f.read(text.data(), text.size()))
        throw std::runtime_error("minimap JSON: read failed");
    auto o = Reader(text).read();
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
