#include "kh2_portrait.h"
#include "flat_json.h"
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
using json::Object;
constexpr const char *kWhat = "portrait";
// characters.<portrait>.player|party.<leaf>, and the version
const json::Rules kRules{
    3,
    [](const std::string &, const std::string &key, int depth) {
        return (depth == 0 && key == "characters") ||
               (depth == 1 && std::any_of(portrait_names.begin(), portrait_names.end(),
                                          [&](const char *name) { return key == name; })) ||
               (depth == 2 && (key == "player" || key == "party"));
    },
    [](const std::string &key, int depth) { return (depth == 0 && key == "version") || depth == 3; }};
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
        throw std::runtime_error("portrait JSON: archive and texture must be file names in the chr folder");
}
} // namespace

PortraitConfigs load_portrait_config(const std::filesystem::path &path)
{
    auto o = json::read_file(path, kWhat, kRules);
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
