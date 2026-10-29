#include "kh2_portrait.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <variant>

namespace khdays::ui::kh2 {
namespace {
using Value = std::variant<std::string, double, bool>;
using Object = std::map<std::string, Value>;
// The small portrait schema needs objects and scalar leaves, with no external JSON dependency.
class Reader {
    std::string text;
    std::size_t at = 0;
    Object values;
    std::set<std::string> keys;
    [[noreturn]] void fail() const
    {
        throw std::runtime_error("portrait JSON: invalid object at byte " + std::to_string(at));
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
                    if ((depth == 0 && key != "characters") ||
                        (depth == 1 && std::none_of(portrait_names.begin(), portrait_names.end(),
                                                    [&](const char *name) { return key == name; })) ||
                        (depth == 2 && key != "player" && key != "party") || depth == 3)
                        fail();
                    object(name + '.', depth + 1);
                } else {
                    if ((depth == 0 && key != "version") || (depth != 0 && depth != 3))
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
        throw std::runtime_error("portrait JSON: wrong type for " + key);
    T result = *v;
    o.erase(it);
    return result;
}
double number(Object &o, const std::string &key, double fallback, double lo, double hi, bool integer = false)
{
    const double n = get(o, key, fallback);
    if (n < lo || n > hi || (integer && n != std::floor(n)))
        throw std::runtime_error("portrait JSON: out of range " + key);
    return n;
}
void filename(const std::string &name)
{
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
        throw std::runtime_error("portrait JSON: archive and texture must be file names in the chr folder");
}
} // namespace

PortraitConfigs load_portrait_config(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 262144)
        throw std::runtime_error("portrait JSON: cannot read " + path.string());
    std::string text(static_cast<std::size_t>(file.tellg()), '\0');
    file.seekg(0);
    if (!file.read(text.data(), std::streamsize(text.size())))
        throw std::runtime_error("portrait JSON: incomplete file");
    auto o = Reader(std::move(text)).read();
    number(o, "version", 1, 1, 1, true);
    PortraitConfigs result;
    for (std::size_t icon = 0; icon < result.size(); ++icon)
        for (int role = 0; role < 2; ++role) {
            auto &c = result[icon][role];
            const auto prefix = std::string("characters.") + portrait_names[icon] + (role ? ".party." : ".player.");
            c.enabled = get(o, prefix + "enabled", c.enabled);
            for (auto [key, field] : {std::pair{"archive", &c.archive},
                                      {"texture", &c.texture},
                                      {"image_entry", &c.image_entry},
                                      {"sequence_entry", &c.sequence_entry}})
                *field = get(o, prefix + key, *field);
            if (c.enabled) {
                filename(c.archive);
                filename(c.texture);
            }
            for (const auto *entry : {&c.image_entry, &c.sequence_entry})
                if (entry->empty() || entry->size() > 4 || entry->find('\0') != std::string::npos)
                    throw std::runtime_error("portrait JSON: BAR entry names contain 1 to 4 bytes");
            c.offset_x = float(number(o, prefix + "offset_x", 0, -512, 512));
            c.offset_y = float(number(o, prefix + "offset_y", 0, -512, 512));
            c.scale = float(number(o, prefix + "scale", c.scale, 0.01, 4));
            c.source_origin_x = float(number(o, prefix + "source_origin_x", 0, -4096, 4096));
            c.source_origin_y = float(number(o, prefix + "source_origin_y", 0, -4096, 4096));
            c.frames_per_tick = float(number(o, prefix + "frames_per_tick", 1, 0.01, 8));
            c.canvas_padding = float(number(o, prefix + "canvas_padding", 4, 0, 64));
            c.play_intro = get(o, prefix + "play_intro", false);
            for (auto [key, field] : {std::pair{"normal", &c.normal},
                                      {"alert", &c.alert},
                                      {"dead", &c.dead},
                                      {"damage_small", &c.damage_small},
                                      {"damage_medium", &c.damage_medium},
                                      {"damage_large", &c.damage_large},
                                      {"intro", &c.intro},
                                      {"animation_override", &c.animation_override}})
                *field = int(number(o, prefix + key, *field, -1, 65535, true));
            if (c.normal < 0 || c.alert < 0 || c.dead < 0)
                throw std::runtime_error("portrait JSON: normal, alert and dead animations are required");
        }
    if (!o.empty())
        throw std::runtime_error("portrait JSON: unknown setting " + o.begin()->first);
    return result;
}

int portrait_tile_slot(unsigned address)
{
    constexpr unsigned offsets[]{0x1800, 0x18c0, 0x1920, 0x24c0};
    for (int slot = 0; slot < 4; ++slot) {
        if (address < offsets[slot])
            continue;
        const unsigned d = address - offsets[slot];
        if (d / 0x400 < (slot == 0 ? 6u : 3u) && d % 0x400 < (slot == 0 ? 0xc0u : 0x60u))
            return slot;
    }
    return -1;
}

void PortraitPlayer::update(const assets::kh2::SequenceData &data, const PortraitConfig &c, int hp, int maximum,
                            bool low, float ticks, bool reset)
{
    const int status = hp == 0 ? c.dead : low ? c.alert : c.normal;
    if (reset || previous_hp < 0) {
        temporary = c.play_intro && c.intro >= 0 && hp > 0;
        temporary_frames = 0;
        player.select(data, std::size_t(temporary ? c.intro : status), true);
    } else {
        player.advance(data, ticks * c.frames_per_tick);
        temporary_frames += ticks * c.frames_per_tick;
        if (hp > 0 && hp < previous_hp) {
            const int lost = previous_hp - hp;
            const int damage = lost * 4 >= maximum   ? c.damage_large
                               : lost * 8 >= maximum ? c.damage_medium
                                                     : c.damage_small;
            temporary = damage >= 0;
            temporary_frames = 0;
            if (temporary)
                player.select(data, std::size_t(damage), true);
        }
        if (temporary &&
            (!player.playing() || temporary_frames >= float(assets::kh2::sequence_frame_count(data, player.clip()))))
            temporary = false;
        if (!temporary || hp == 0) {
            temporary = false;
            player.select(data, std::size_t(status));
        }
    }
    if (c.animation_override >= 0) {
        temporary = false;
        player.select(data, std::size_t(c.animation_override));
    }
    previous_hp = hp;
}

assets::kh2::SequenceFrame PortraitPlayer::evaluate(const assets::kh2::SequenceData &data,
                                                    const assets::kh2::SequencePose &pose) const
{
    return assets::kh2::evaluate_sequence(data, player.clip(), player.frame(), pose);
}
} // namespace khdays::ui::kh2
