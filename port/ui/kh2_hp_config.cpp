// Shared gauge, label, reticle and information settings; no game or graphics-device dependencies.
#include "kh2_hp_config.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <variant>

namespace khdays::ui::kh2 {
namespace {
using Value = std::variant<std::string, double, bool>;
using Object = std::map<std::string, Value>;
class Reader {
    std::string text;
    std::size_t at = 0;
    Object values;
    std::set<std::string> keys;
    [[noreturn]] void fail() const
    {
        throw std::runtime_error("gauge JSON: invalid object at byte " + std::to_string(at));
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
                default: fail();
                }
            }
            out += c;
        }
        take('"');
        return out;
    }
    Value value()
    {
        if (next() == '"')
            return string();
        for (const auto &[word, boolean] : {std::pair{"true", true}, std::pair{"false", false}}) {
            const std::string token(word);
            if (text.compare(at, token.size(), token) == 0) {
                at += token.size();
                return boolean;
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
        if (depth > 4)
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
                    const bool label = key == "player_icon" || key == "limit_text";
                    const bool allowed =
                        (prefix.empty() &&
                         (key == "party" || key == "enemy" || key == "reticles" || key == "information" ||
                          key == "obtained" || key == "backpack_full" || key == "breakthrough" || key == "locked_on" ||
                          key == "mission_panel" || key == "heart_points" || key == "presentation" || label)) ||
                        (prefix == "heart_points." && key == "animations") ||
                        (prefix == "reticles." && (key == "soft_lock" || key == "hard_lock" || key == "interact" ||
                                                   key == "chain" || key == "error")) ||
                        (prefix == "party." && (key == "slots" || label)) ||
                        (prefix == "party.slots." && (key == "1" || key == "2" || key == "3")) ||
                        ((prefix == "party.slots.1." || prefix == "party.slots.2." || prefix == "party.slots.3.") &&
                         label);
                    if (!allowed)
                        fail();
                    object(name + '.', depth + 1);
                } else
                    values.emplace(name, value());
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
template <class T> T get(Object &o, const char *key, T fallback)
{
    auto i = o.find(key);
    if (i == o.end())
        return fallback;
    const auto *v = std::get_if<T>(&i->second);
    if (!v)
        throw std::runtime_error(std::string("gauge JSON: wrong type for ") + key);
    T result = *v;
    o.erase(i);
    return result;
}
double number(Object &o, const char *key, double fallback, double lo, double hi, bool integer = false)
{
    const double n = get(o, key, fallback);
    if (n < lo || n > hi || (integer && n != std::floor(n)))
        throw std::runtime_error(std::string("gauge JSON: out of range ") + key);
    return n;
}
void resource_name(const std::string &value, const std::string &key)
{
    if (value.empty() || value == "." || value == ".." || value.find_first_of("/\\:") != std::string::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; }))
        throw std::runtime_error("gauge JSON: use a local file name for " + key);
}
Object read_file(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 262144)
        throw std::runtime_error("gauge JSON: cannot read " + path.string());
    std::string text(static_cast<std::size_t>(file.tellg()), '\0');
    file.seekg(0);
    if (!file.read(text.data(), std::streamsize(text.size())))
        throw std::runtime_error("gauge JSON: incomplete file");
    return Reader(std::move(text)).read();
}
} // namespace

HpGaugeConfig parse_config(Object o, HpGaugeConfig c = {})
{
    number(o, "version", 1, 1, 1, true);
    c.enabled = get(o, "enabled", c.enabled);
    for (auto [key, field] : {std::pair{"archive", &c.archive}, {"texture", &c.texture}, {"entry", &c.entry}}) {
        *field = get(o, key, *field);
        resource_name(*field, key);
    }
    c.offset_x = float(number(o, "offset_x", c.offset_x, -256, 256));
    c.offset_y = float(number(o, "offset_y", c.offset_y, -256, 256));
    c.scale = float(number(o, "scale", c.scale, 0.05, 2));
    c.canvas_padding = float(number(o, "canvas_padding", 16, 0, 128));
    c.portrait_offset_x = float(number(o, "portrait_offset_x", 0, -128, 128));
    c.portrait_offset_y = float(number(o, "portrait_offset_y", 0, -128, 128));
    auto &s = c.style;
    s.geometry_scale = float(number(o, "geometry_scale", s.geometry_scale, 0.01, 4));
    s.source_origin_x = float(number(o, "source_origin_x", 0, -4096, 4096));
    s.source_origin_y = float(number(o, "source_origin_y", 0, -4096, 4096));
    s.play_intro = get(o, "play_intro", s.play_intro);
    s.center_override = int(number(o, "center_override", -1, -1, 65535, true));
    auto clip = [&](const char *key, std::size_t &v) { v = std::size_t(number(o, key, double(v), 0, 65535, true)); };
    clip("center_normal", s.center[0]);
    clip("center_alert", s.center[1]);
    clip("center_dead", s.center[2]);
    clip("center_damage_small", s.damage[0]);
    clip("center_damage_medium", s.damage[1]);
    clip("center_damage_large", s.damage[2]);
    clip("center_intro", s.intro);
    clip("center_fade", s.fade);
    clip("circle_normal", s.circle[0]);
    clip("circle_alert", s.circle[1]);
    clip("circle_dead", s.circle[2]);
    clip("bar_normal", s.bar[0]);
    clip("bar_alert", s.bar[1]);
    clip("bar_dead", s.bar[2]);
    clip("circle_damage", s.circle_damage);
    clip("bar_damage", s.bar_damage);
    clip("circle_fill", s.circle_fill);
    clip("bar_fill", s.bar_fill);
    clip("cap", s.cap);
    s.limit_circle = int(number(o, "limit_circle", -1, -1, 65535, true));
    s.limit_bar = int(number(o, "limit_bar", -1, -1, 65535, true));
    s.limit_marker = int(number(o, "limit_marker", -1, -1, 65535, true));
    for (auto [prefix, xy] : {std::pair{"center", &s.center_offset},
                              {"circle", &s.circle_offset},
                              {"bar", &s.bar_offset},
                              {"cap", &s.cap_offset},
                              {"limit_marker", &s.limit_marker_offset}}) {
        (*xy)[0] = float(number(o, (std::string(prefix) + "_offset_x").c_str(), 0, -128, 128));
        (*xy)[1] = float(number(o, (std::string(prefix) + "_offset_y").c_str(), 0, -128, 128));
    }
    for (auto [name, label] : {std::pair{"player_icon", &c.player_icon}, {"limit_text", &c.limit_text}}) {
        const auto prefix = std::string(name) + '.';
        auto num = [&](const char *key, double fallback, double lo, double hi, bool integer = false) {
            return number(o, (prefix + key).c_str(), fallback, lo, hi, integer);
        };
        label->enabled = get(o, (prefix + "enabled").c_str(), false);
        label->animation = int(num("animation", -1, -1, 65535, true));
        for (int i = 0; i < 4; ++i)
            label->animations[i] = int(num(("p" + std::to_string(i + 1)).c_str(), -1, -1, 65535, true));
        label->offset_x = float(num("offset_x", 0, -512, 512));
        label->offset_y = float(num("offset_y", 0, -512, 512));
        label->scale = float(num("scale", c.scale, 0.01, 4));
        label->source_origin_x = float(num("source_origin_x", 0, -4096, 4096));
        label->source_origin_y = float(num("source_origin_y", 0, -4096, 4096));
        label->frames_per_tick = float(num("frames_per_tick", 1, 0.01, 8));
        for (auto [key, field, fallback] :
             {std::tuple{"archive", &label->archive, &c.archive}, std::tuple{"texture", &label->texture, &c.texture},
              std::tuple{"entry", &label->entry, &c.entry}}) {
            *field = get(o, (prefix + key).c_str(), *fallback);
            resource_name(*field, key);
        }
    }
    if (!o.empty())
        throw std::runtime_error("gauge JSON: unknown key " + o.begin()->first);
    return c;
}
std::array<HpGaugeConfig, 4> load_hp_gauge_configs(const std::filesystem::path &path)
{
    auto all = read_file(path);
    auto extract = [&](const std::string &prefix) {
        Object result;
        for (auto it = all.begin(); it != all.end();) {
            if (it->first.starts_with(prefix)) {
                result.emplace(it->first.substr(prefix.size()), it->second);
                it = all.erase(it);
            } else
                ++it;
        }
        return result;
    };
    extract("enemy.");
    extract("reticles.");
    extract("information.");
    extract("obtained.");
    extract("backpack_full.");
    extract("breakthrough.");
    extract("locked_on.");
    extract("mission_panel.");
    extract("heart_points.");
    extract("presentation.");
    std::array<Object, 3> overrides;
    for (int i = 0; i < 3; ++i)
        overrides[i] = extract("party.slots." + std::to_string(i + 1) + '.');
    auto party = extract("party.");
    std::array<HpGaugeConfig, 4> result;
    result[0] = parse_config(std::move(all));
    const bool present = !party.empty();
    for (int i = 0; i < 3; ++i) {
        auto selected = party;
        for (const auto &[key, value] : overrides[i]) {
            const bool offset = key == "offset_x" || key == "offset_y" || key == "player_icon.offset_x" ||
                                key == "player_icon.offset_y" || key == "limit_text.offset_x" ||
                                key == "limit_text.offset_y";
            const auto found = selected.find(key);
            const auto *extra = std::get_if<double>(&value);
            const auto *base = found != selected.end() ? std::get_if<double>(&found->second) : nullptr;
            if (offset && base && extra)
                selected.insert_or_assign(key, *base + *extra);
            else
                selected.insert_or_assign(key, value);
        }
        auto defaults = result[0];
        defaults.enabled = present;
        defaults.offset_x = defaults.offset_y = 0;
        defaults.style.geometry_scale = 23.0f / 45;
        defaults.style.center = {174, 169, 174};
        defaults.style.damage = {172, 171, 170};
        defaults.style.intro = 173;
        defaults.style.fade = 175;
        defaults.style.circle = {160, 161, 160};
        defaults.style.bar = {163, 164, 163};
        defaults.style.circle_damage = 165;
        defaults.style.bar_damage = 166;
        defaults.style.circle_fill = 167;
        defaults.style.bar_fill = 168;
        defaults.style.cap = 162;
        result[i + 1] = parse_config(std::move(selected), defaults);
        auto &style = result[i + 1].style;
        style.limit_circle = style.limit_bar = style.limit_marker = -1;
    }
    return result;
}
HpGaugeConfig load_hp_gauge_config(const std::filesystem::path &path)
{
    return load_hp_gauge_configs(path)[0];
}

EnemyGaugeConfig load_enemy_gauge_config(const std::filesystem::path &path)
{
    const auto root = load_hp_gauge_config(path);
    auto all = read_file(path);
    Object o;
    for (const auto &[key, value] : all)
        if (key.starts_with("enemy."))
            o.emplace(key.substr(6), value);
    EnemyGaugeConfig c;
    c.enabled = !o.empty();
    c.archive = root.archive;
    c.texture = root.texture;
    c.entry = root.entry;
    c.scale = root.scale;
    c.enabled = get(o, "enabled", c.enabled);
    for (auto [key, field] : {std::pair{"archive", &c.archive}, {"texture", &c.texture}, {"entry", &c.entry}}) {
        *field = get(o, key, *field);
        resource_name(*field, key);
    }
    c.scale = float(number(o, "scale", c.scale, 0.05, 2));
    c.offset_x = float(number(o, "offset_x", 0, -512, 512));
    c.offset_y = float(number(o, "offset_y", 0, -512, 512));
    c.frames_per_tick = float(number(o, "frames_per_tick", 1, 0.01, 8));
    auto &s = c.style;
    for (auto [key, field] : {std::pair{"background", &s.background},
                              {"preview", &s.preview},
                              {"bar_background", &s.bar_background},
                              {"cap", &s.cap},
                              {"damage", &s.damage},
                              {"fill", &s.fill},
                              {"tank_full", &s.tank_full},
                              {"tank_empty", &s.tank_empty},
                              {"tank_damage", &s.tank_damage}})
        *field = std::size_t(number(o, key, double(*field), 0, 65535, true));
    for (auto [key, field] : {std::pair{"source_origin_x", &s.source_origin_x},
                              {"source_origin_y", &s.source_origin_y},
                              {"bar_offset_x", &s.bar_offset_x},
                              {"bar_offset_y", &s.bar_offset_y},
                              {"tank_offset_x", &s.tank_offset_x},
                              {"tank_offset_y", &s.tank_offset_y}})
        *field = float(number(o, key, *field, -4096, 4096));
    if (!o.empty())
        throw std::runtime_error("gauge JSON: unknown enemy key " + o.begin()->first);
    return c;
}

std::array<ReticleConfig, 5> load_reticle_configs(const std::filesystem::path &path)
{
    const auto root = load_hp_gauge_config(path);
    const auto all = read_file(path);
    Object shared;
    std::array<Object, 5> selected;
    constexpr const char *names[]{"soft_lock", "hard_lock", "interact", "chain", "error"};
    for (const auto &[key, value] : all) {
        if (!key.starts_with("reticles."))
            continue;
        auto tail = key.substr(9);
        const auto dot = tail.find('.');
        if (dot == std::string::npos) {
            shared.emplace(tail, value);
            continue;
        }
        for (int i = 0; i < 5; ++i)
            if (tail.substr(0, dot) == names[i])
                selected[i].emplace(tail.substr(dot + 1), value);
    }
    auto parse = [](Object o, ReticleConfig c) {
        c.enabled = get(o, "enabled", c.enabled);
        for (auto [key, field] : {std::pair{"archive", &c.archive}, {"texture", &c.texture}, {"entry", &c.entry}}) {
            *field = get(o, key, *field);
            resource_name(*field, key);
        }
        c.animation = int(number(o, "animation", c.animation, -1, 65535, true));
        c.end_animation = int(number(o, "end_animation", c.end_animation, -1, 65535, true));
        c.offset_x = float(number(o, "offset_x", c.offset_x, -512, 512));
        c.offset_y = float(number(o, "offset_y", c.offset_y, -512, 512));
        c.scale = float(number(o, "scale", c.scale, 0.01, 4));
        c.source_origin_x = float(number(o, "source_origin_x", c.source_origin_x, -4096, 4096));
        c.source_origin_y = float(number(o, "source_origin_y", c.source_origin_y, -4096, 4096));
        c.frames_per_tick = float(number(o, "frames_per_tick", c.frames_per_tick, 0.01, 8));
        if (!o.empty())
            throw std::runtime_error("gauge JSON: unknown reticle key " + o.begin()->first);
        return c;
    };
    ReticleConfig defaults;
    defaults.archive = root.archive;
    defaults.scale = root.scale;
    defaults.enabled =
        !shared.empty() || std::any_of(selected.begin(), selected.end(), [](const auto &o) { return !o.empty(); });
    defaults = parse(shared, defaults);
    std::array<ReticleConfig, 5> result;
    constexpr int clips[]{5, 27, 50, -1, -1}, ends[]{4, 26, 49, -1, -1};
    for (int i = 0; i < 5; ++i) {
        auto c = defaults;
        c.animation = clips[i];
        c.end_animation = ends[i];
        c.enabled = defaults.enabled && i < 3;
        result[i] = parse(selected[i], c);
        result[i].enabled &= defaults.enabled;
    }
    return result;
}
InformationConfig load_information_config(const std::filesystem::path &path)
{
    const auto root = load_hp_gauge_config(path);
    Object o;
    for (const auto &[key, value] : read_file(path))
        if (key.starts_with("information."))
            o.emplace(key.substr(12), value);
    InformationConfig c;
    c.enabled = !o.empty();
    c.archive = root.archive;
    c.texture = root.texture;
    c.entry = root.entry;
    c.scale = root.scale;
    c.enabled = get(o, "enabled", c.enabled);
    for (auto [key, field] : {std::pair{"archive", &c.archive}, {"texture", &c.texture}, {"entry", &c.entry}}) {
        *field = get(o, key, *field);
        resource_name(*field, key);
    }
    for (auto [key, field] :
         {std::pair{"intro", &c.intro}, {"loop", &c.loop}, {"reveal", &c.reveal}, {"outro", &c.outro}})
        *field = int(number(o, key, *field, 0, 65535, true));
    for (auto [key, field] : {std::pair{"offset_x", &c.offset_x},
                              {"offset_y", &c.offset_y},
                              {"source_origin_x", &c.source_origin_x},
                              {"source_origin_y", &c.source_origin_y},
                              {"text_offset_x", &c.text_offset_x},
                              {"text_offset_y", &c.text_offset_y}})
        *field = float(number(o, key, *field, -4096, 4096));
    c.scale = float(number(o, "scale", c.scale, 0.01, 4));
    c.text_scale = float(number(o, "text_scale", c.text_scale, 0.1, 4));
    c.frames_per_tick = float(number(o, "frames_per_tick", 1, 0.01, 8));
    c.clip_width = float(number(o, "clip_width", c.clip_width, 0, 2048));
    c.clip_height = float(number(o, "clip_height", c.clip_height, 1, 512));
    c.hold_frames = float(number(o, "hold_frames", c.hold_frames, 30, 36000));
    c.scroll_wait_frames = float(number(o, "scroll_wait_frames", c.scroll_wait_frames, 0, 36000));
    c.scroll_speed = float(number(o, "scroll_speed", c.scroll_speed, 0, 32));
    if (!o.empty())
        throw std::runtime_error("gauge JSON: unknown information key " + o.begin()->first);
    return c;
}
ObtainedConfig load_notification_config(const std::filesystem::path &path, const std::string &name)
{
    if (name != "obtained" && name != "backpack_full" && name != "breakthrough" && name != "locked_on")
        throw std::runtime_error("unknown notification section");
    const auto root = load_hp_gauge_config(path);
    Object o;
    for (const auto &[key, value] : read_file(path))
        if (key.starts_with(name + "."))
            o.emplace(key.substr(name.size() + 1), value);
    ObtainedConfig c;
    c.enabled = name == "obtained" && !o.empty();
    if (name != "obtained")
        c.animation = -1;
    c.loop = name == "locked_on";
    c.archive = root.archive;
    c.texture = root.texture;
    c.entry = root.entry;
    c.scale = root.scale;
    c.enabled = get(o, "enabled", c.enabled);
    for (auto [key, field] : {std::pair{"archive", &c.archive}, {"texture", &c.texture}, {"entry", &c.entry}}) {
        *field = get(o, key, *field);
        resource_name(*field, key);
    }
    c.animation = int(number(o, "animation", c.animation, -1, 65535, true));
    c.loop = get(o, "loop", c.loop);
    for (auto [key, field] : {std::pair{"offset_x", &c.offset_x},
                              {"offset_y", &c.offset_y},
                              {"source_origin_x", &c.source_origin_x},
                              {"source_origin_y", &c.source_origin_y},
                              {"text_offset_x", &c.text_offset_x},
                              {"text_offset_y", &c.text_offset_y}})
        *field = float(number(o, key, *field, -4096, 4096));
    c.scale = float(number(o, "scale", c.scale, 0.01, 4));
    c.text_scale = float(number(o, "text_scale", c.text_scale, 0.1, 4));
    c.frames_per_tick = float(number(o, "frames_per_tick", c.frames_per_tick, 0.01, 8));
    if (!o.empty())
        throw std::runtime_error("gauge JSON: unknown " + name + " key " + o.begin()->first);
    return c;
}
ObtainedConfig load_obtained_config(const std::filesystem::path &path)
{
    return load_notification_config(path, "obtained");
}
HeartPointsConfig load_heart_points_config(const std::filesystem::path &path)
{
    Object o;
    for (const auto &[key, value] : read_file(path))
        if (key.starts_with("heart_points."))
            o.emplace(key.substr(13), value);
    HeartPointsConfig c;
    c.enabled = get(o, "enabled", false);
    for (auto [key, field] : {std::pair{"archive", &c.archive}, {"texture", &c.texture}, {"entry", &c.entry}}) {
        *field = get(o, key, *field);
        resource_name(*field, key);
    }
    for (std::size_t i = 0; i < c.animations.size(); ++i) {
        const auto key = "animations." + std::string(heart_clip_names[i]);
        c.animations[i] = int(number(o, key.c_str(), i >= 41 ? -1.0 : double(i), -1, 65535, true));
    }
    for (auto [key, field] : {std::pair{"offset_x", &c.offset_x},
                              {"offset_y", &c.offset_y},
                              {"source_origin_x", &c.source_origin_x},
                              {"source_origin_y", &c.source_origin_y},
                              {"total_x", &c.total_x},
                              {"total_y", &c.total_y},
                              {"digit_spacing", &c.digit_spacing},
                              {"rows_x", &c.rows_x},
                              {"rows_y", &c.rows_y},
                              {"row_spacing", &c.row_spacing}})
        *field = float(number(o, key, *field, -4096, 4096));
    c.scale = float(number(o, "scale", c.scale, .01, 4));
    c.frames_per_tick = float(number(o, "frames_per_tick", 1, .01, 8));
    if (!o.empty())
        throw std::runtime_error("gauge JSON: unknown heart_points key " + o.begin()->first);
    return c;
}
MissionPanelConfig load_mission_panel_config(const std::filesystem::path &path)
{
    Object o;
    for (const auto &[key, value] : read_file(path))
        if (key.starts_with("mission_panel."))
            o.emplace(key.substr(14), value);
    MissionPanelConfig c;
    c.enabled = get(o, "enabled", false);
    c.pause_only = get(o, "pause_only", false);
    c.pause_offset_x = float(number(o, "pause_offset_x", 0, -4096, 4096));
    c.pause_offset_y = float(number(o, "pause_offset_y", 0, -4096, 4096));
    const std::array<std::string, 10> anchors{"native", "top-left",     "top",         "top-right", "middle-left",
                                              "middle", "middle-right", "bottom-left", "bottom",    "bottom-right"};
    auto anchor = [&](const char *key, unsigned fallback) {
        const auto value = get(o, key, anchors[fallback]);
        const auto found = std::find(anchors.begin(), anchors.end(), value);
        if (found == anchors.end())
            throw std::runtime_error(std::string("gauge JSON: invalid mission panel ") + key + ": " + value);
        return unsigned(found - anchors.begin());
    };
    c.anchor = anchor("anchor", 0);
    c.pause_anchor = anchor("pause_anchor", c.anchor);
    for (auto [key, field] : {std::pair{"archive", &c.archive}, {"texture", &c.texture}, {"entry", &c.entry}}) {
        *field = get(o, key, *field);
        resource_name(*field, key);
    }
    c.reference = int(number(o, "reference", c.reference, 0, 65535, true));
    c.intro = int(number(o, "intro", c.intro, 0, 65535, true));
    c.outro = int(number(o, "outro", c.outro, 0, 65535, true));
    c.track = int(number(o, "track", c.track, 0, 65535, true));
    c.fill = int(number(o, "fill", c.fill, 0, 65535, true));
    c.ready_fill = int(number(o, "ready_fill", c.ready_fill, 0, 65535, true));
    c.full_fill = int(number(o, "full_fill", c.full_fill, 0, 65535, true));
    c.heading = int(number(o, "heading", c.heading, 0, 65535, true));
    c.complete_heading = int(number(o, "complete_heading", c.complete_heading, 0, 65535, true));
    c.full_marker = int(number(o, "full_marker", c.full_marker, 0, 65535, true));
    c.minimum_marker = int(number(o, "minimum_marker", c.minimum_marker, 0, 65535, true));
    c.bead_empty = int(number(o, "bead_empty", c.bead_empty, 0, 65535, true));
    c.bead_full = int(number(o, "bead_full", c.bead_full, 0, 65535, true));
    c.unity = int(number(o, "unity", c.unity, 0, 65535, true));
    c.rank = int(number(o, "rank", c.rank, 0, 65535, true));
    c.chest = int(number(o, "chest", c.chest, 0, 65535, true));
    c.offset_x = float(number(o, "offset_x", c.offset_x, -4096, 4096));
    c.offset_y = float(number(o, "offset_y", c.offset_y, -4096, 4096));
    c.source_origin_x = float(number(o, "source_origin_x", c.source_origin_x, -4096, 4096));
    c.source_origin_y = float(number(o, "source_origin_y", c.source_origin_y, -4096, 4096));
    c.description_x = float(number(o, "description_x", c.description_x, -4096, 4096));
    c.description_y = float(number(o, "description_y", c.description_y, -4096, 4096));
    c.tally_x = float(number(o, "tally_x", c.tally_x, -4096, 4096));
    c.tally_y = float(number(o, "tally_y", c.tally_y, -4096, 4096));
    c.counters_right = float(number(o, "counters_right", c.counters_right, -4096, 4096));
    c.counters_y = float(number(o, "counters_y", c.counters_y, -4096, 4096));
    c.counter_text_offset_y = float(number(o, "counter_text_offset_y", c.counter_text_offset_y, -4096, 4096));
    c.beads_x = float(number(o, "beads_x", c.beads_x, -4096, 4096));
    c.beads_y = float(number(o, "beads_y", c.beads_y, -4096, 4096));
    c.bar_start_x = float(number(o, "bar_start_x", c.bar_start_x, -4096, 4096));
    c.bar_end_x = float(number(o, "bar_end_x", c.bar_end_x, -4096, 4096));
    c.bar_y = float(number(o, "bar_y", c.bar_y, -4096, 4096));
    c.heading_x = float(number(o, "heading_x", c.heading_x, -4096, 4096));
    c.heading_y = float(number(o, "heading_y", c.heading_y, -4096, 4096));
    c.full_marker_x = float(number(o, "full_marker_x", c.full_marker_x, -4096, 4096));
    c.full_marker_y = float(number(o, "full_marker_y", c.full_marker_y, -4096, 4096));
    c.minimum_marker_y = float(number(o, "minimum_marker_y", c.minimum_marker_y, -4096, 4096));
    c.scale = float(number(o, "scale", c.scale, 0.01, 4));
    c.text_scale = float(number(o, "text_scale", c.text_scale, 0.1, 4));
    c.counter_scale = float(number(o, "counter_scale", c.counter_scale, 0.1, 4));
    c.counters_scale = float(number(o, "counters_scale", c.counters_scale, 0.1, 4));
    c.frames_per_tick = float(number(o, "frames_per_tick", c.frames_per_tick, 0.01, 8));
    c.icon_gap = float(number(o, "icon_gap", c.icon_gap, 0, 128));
    c.counter_gap = float(number(o, "counter_gap", c.counter_gap, 0, 128));
    c.bead_spacing = float(number(o, "bead_spacing", c.bead_spacing, 1, 128));
    if (c.bar_end_x <= c.bar_start_x)
        throw std::runtime_error("gauge JSON: mission panel bar end must follow its start");
    if (!o.empty())
        throw std::runtime_error("gauge JSON: unknown mission panel key " + o.begin()->first);
    return c;
}
HudPresentationConfig load_hud_presentation_config(const std::filesystem::path &path)
{
    Object o;
    for (const auto &[key, value] : read_file(path))
        if (key.starts_with("presentation."))
            o.emplace(key.substr(13), value);
    HudPresentationConfig c;
    c.keep_hud_during_information = get(o, "keep_hud_during_information", false);
    c.align_hud_to_edges = get(o, "align_hud_to_edges", false);
    c.pause_mission_target = get(o, "pause_mission_target", false);
    if (!o.empty())
        throw std::runtime_error("gauge JSON: unknown presentation key " + o.begin()->first);
    return c;
}
} // namespace khdays::ui::kh2
