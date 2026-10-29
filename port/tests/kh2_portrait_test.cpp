// Synthetic assets exercise the live adapter, exact tile suppression, fallback, playback, and parent.
#include "../host/app/hud_settings.h"
#include "../ui/kh2_portrait.h"
#include "../ui/kh2_portrait_hud.h"
#include "../ui/kh2_hp_hud.h"
#include "../ui/kh2_party_hud.h"
#include "../ui/kh2_hud_labels.h"
#include "../ui/kh2_hp_config.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "../ui/parts.h"
}
#include <bit>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
bool hd_graphics = true;
namespace portrait = khdays::ui::kh2;
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
std::filesystem::path directory;
int present = 1, paused = 0, hp = 186, parent_on = 0, reads = 0, uploads = 0, atlases = 0, allocations = 0;
int masked[4]{}, scale = 2;
std::vector<std::pair<int, int>> label_border_masks;
float parent_x = 230, parent_y = 160;
std::vector<std::vector<std::uint32_t>> images;
std::vector<KhdaysUiElement> emitted;
struct RegisteredAtlas {
    const unsigned *pixels;
    std::vector<unsigned> expected;
};
std::vector<RegisteredAtlas> registered_atlases;
bool test_party = false, test_labels = false, native_limit = false;
bool limit_tiles = true;
int party_health[]{372, 124, 62};
void expect(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message)
{
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
void word(std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = std::uint8_t(value >> (8 * i));
}
void write(const std::filesystem::path &p, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}
void text(const std::filesystem::path &p, const std::string &value)
{
    std::ofstream(p) << value;
}
std::vector<std::uint8_t> sequence(unsigned version)
{
    std::vector<std::uint8_t> b(836);
    word(b, 0, 0x44514553);
    word(b, 4, version);
    const unsigned counts[]{1, 1, 1, 4, 4}, offsets[]{48, 92, 112, 116, 692};
    for (unsigned t = 0; t < 5; ++t) {
        word(b, 8 + 8 * t, counts[t]);
        word(b, 12 + 8 * t, offsets[t]);
    }
    word(b, 60, 4);
    word(b, 64, 4);
    for (unsigned i = 0; i < 4; ++i)
        word(b, 76 + i * 4, 0x80808080);
    word(b, 92, unsigned(-4));
    word(b, 96, unsigned(-4));
    word(b, 100, 4);
    word(b, 104, 4);
    word(b, 112, 0x10000);
    for (unsigned i = 0; i < 4; ++i) {
        const auto a = 116 + i * 144;
        word(b, a, 0x71);
        word(b, a + 12, 59);
        for (unsigned o = 72; o < 96; o += 4)
            word(b, a + o, std::bit_cast<unsigned>(1.0f));
        word(b, a + 136, 0x80808080);
        word(b, a + 140, 0x80808080);
        word(b, 692 + i * 36, 0x10000 | i);
        word(b, 696 + i * 36, i == 1 ? 1u : 0u);
    }
    return b;
}
void archive(const std::filesystem::path &p, unsigned version, bool gauge = false)
{
    auto s = sequence(version);
    if (gauge) {
        s.resize(692 + 180 * 36);
        word(s, 40, 180);
        word(s, 116, 0x73);
        for (unsigned clip = 0; clip < 180; ++clip) {
            word(s, 692 + clip * 36, 0x10000);
            word(s, 696 + clip * 36, 0);
        }
        // A custom parent has no active attachment on its last ten frames.
        word(s, 692 + 176 * 36, 0x20002);
        word(s, 116 + 3 * 144, 0x73);
        word(s, 116 + 3 * 144 + 12, 49);
    }
    std::vector<std::uint8_t> b(112 + s.size());
    word(b, 0, 0x01524142);
    word(b, 4, 2);
    word(b, 16, 24);
    word(b, 20, 0x00676d69);
    word(b, 24, 48);
    word(b, 28, 64);
    word(b, 32, 25);
    word(b, 36, 0x00716573);
    word(b, 40, 112);
    word(b, 44, unsigned(s.size()));
    word(b, 48, 0x44474d49);
    word(b, 52, 0x100);
    word(b, 76, 4 | (4u << 16));
    std::copy(s.begin(), s.end(), b.begin() + 112);
    write(p, b);
}
void assets()
{
    archive(directory / "portrait.any-extension", 0x160);
    archive(directory / "old.archive", 0x150);
    archive(directory / "gauge.archive", 0x160, true);
    archive(directory / "label.archive", 0x160);
    {
        std::ifstream input(directory / "label.archive", std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        input.close();
        word(bytes, 36, 0x00676d69);
        for (unsigned i = 0; i < 4; ++i) {
            word(bytes, 112 + 116 + i * 144 + 16, i * 10);
            word(bytes, 112 + 116 + i * 144 + 20, i * 10 + 59);
        }
        word(bytes, 112 + 692 + 8, 10); // Non-zero loop point after a one-time entrance.
        word(bytes, 112 + 692 + 12, 40);
        write(directory / "label.archive", bytes);
        word(bytes, 112 + 48, 1); // Unsupported palette: fail during playback after registering the atlas.
        write(directory / "invalid-label.archive", bytes);
    }
    std::vector<std::uint8_t> dds(192, 255);
    std::fill(dds.begin(), dds.begin() + 128, std::uint8_t(0));
    word(dds, 0, 0x20534444);
    word(dds, 4, 124);
    word(dds, 12, 4);
    word(dds, 16, 4);
    word(dds, 76, 32);
    word(dds, 80, 0x41);
    word(dds, 88, 32);
    word(dds, 92, 255);
    word(dds, 96, 0xff00);
    word(dds, 100, 0xff0000);
    word(dds, 104, 0xff000000);
    write(directory / "portrait.DDS", dds);
    // A generated 1x1 RGBA PNG: both texture codecs must work through the same asset interface.
    const std::vector<std::uint8_t> png{137, 80,  78, 71,  13, 10, 26, 10,  0,   0,  0,   13, 73, 72,  68,  82,  0,
                                        0,   0,   1,  0,   0,  0,  1,  8,   6,   0,  0,   0,  31, 21,  196, 137, 0,
                                        0,   0,   11, 73,  68, 65, 84, 120, 156, 99, 248, 15, 4,  0,   9,   251, 3,
                                        253, 251, 94, 107, 43, 0,  0,  0,   0,   73, 69,  78, 68, 174, 66,  96,  130};
    write(directory / "portrait.png", png);
}
void configuration()
{
    text(directory / "presentation.json", R"({"presentation":{"align_hud_to_edges":true,
        "keep_hud_during_information":true,"pause_mission_target":true}})");
    const auto presentation = portrait::load_hud_presentation_config(directory / "presentation.json");
    expect(presentation.align_hud_to_edges && presentation.keep_hud_during_information &&
               presentation.pause_mission_target,
           "presentation options ignored");
    expect(portrait::load_hp_gauge_config(directory / "presentation.json").scale == .65f,
           "presentation settings broke gauge parsing");
    text(directory / "presentation.json", "{}");
    const auto defaults = portrait::load_hud_presentation_config(directory / "presentation.json");
    expect(!defaults.align_hud_to_edges && !defaults.keep_hud_during_information && !defaults.pause_mission_target,
           "presentation defaults changed native behavior");
    text(directory / "presentation.json", R"({"presentation":{"unknown":true}})");
    rejects([&] { portrait::load_hud_presentation_config(directory / "presentation.json"); },
            "unknown presentation option accepted");
    {
        std::ifstream input(directory / "portrait.any-extension", std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        for (unsigned entry : {16u, 32u}) {
            auto invalid = bytes;
            word(invalid, entry + 8, ~0u);
            word(invalid, entry + 12, 0);
            write(directory / "empty-entry.archive", invalid);
            rejects(
                [&] {
                    render::load_sequence_asset(directory / "empty-entry.archive", "img", "seq",
                                                directory / "portrait.DDS");
                },
                "empty BAR entry with a sentinel offset accepted");
        }
    }
    for (const auto *section : {"", "player_icon", "party", "enemy", "information", "obtained", "backpack_full",
                                "breakthrough", "locked_on", "heart_points", "mission_panel", "reticles"}) {
        for (const auto *name : {".", "..", "bad\\nname", "../outside.2dd"}) {
            std::string value = std::string("{\"archive\":\"") + name + "\"}";
            if (*section)
                value = std::string("{\"") + section + "\":" + value + "}";
            text(directory / "bad-resource.json", value);
            rejects(
                [&] {
                    if (std::string(section) == "enemy")
                        portrait::load_enemy_gauge_config(directory / "bad-resource.json");
                    else if (std::string(section) == "reticles")
                        portrait::load_reticle_configs(directory / "bad-resource.json");
                    else if (std::string(section) == "information")
                        portrait::load_information_config(directory / "bad-resource.json");
                    else if (std::string(section) == "mission_panel")
                        portrait::load_mission_panel_config(directory / "bad-resource.json");
                    else if (std::string(section) == "heart_points")
                        portrait::load_heart_points_config(directory / "bad-resource.json");
                    else if (std::string(section) == "obtained" || std::string(section) == "backpack_full" ||
                             std::string(section) == "breakthrough" || std::string(section) == "locked_on")
                        portrait::load_notification_config(directory / "bad-resource.json", section);
                    else
                        portrait::load_hp_gauge_configs(directory / "bad-resource.json");
                },
                "invalid gauge resource name accepted");
        }
    }
    text(directory / "gauge.json",
         R"({"archive":"gauge.archive","texture":"portrait.DDS","entry":"img",
        "player_icon":{"enabled":true,"animation":0,"archive":"label.archive","scale":1,"offset_x":2,"offset_y":3},
        "limit_text":{"enabled":true,"animation":0,"archive":"label.archive","scale":1},
        "party":{"enabled":true,"geometry_scale":0.5111111111111111,"slots":{
        "1":{"offset_x":0,"offset_y":0},
        "2":{"offset_x":5,"offset_y":-3,"player_icon":{"enabled":true,"animation":1,"p3":2,"archive":"label.archive","scale":1}},
        "3":{"center_override":176,"player_icon":{"enabled":true,"animation":0,"archive":"invalid-label.archive","scale":1}}
        }}})");
    // Gauge fixtures use the same image/sequence name, unlike the portrait test archive.
    auto gauge = std::ifstream(directory / "gauge.archive", std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(gauge)), {});
    gauge.close();
    word(bytes, 36, 0x00676d69);
    write(directory / "gauge.archive", bytes);
    if (native_limit) {
        std::ifstream input(directory / "gauge.json");
        std::string config((std::istreambuf_iterator<char>(input)), {});
        const auto key = config.find("\"limit_text\":{\"enabled\":true");
        expect(key != std::string::npos, "native LIMIT fixture not found");
        config.replace(key, std::strlen("\"limit_text\":{\"enabled\":true"), "\"limit_text\":{\"enabled\":false");
        input.close();
        text(directory / "gauge.json", config);
    }
    const auto gauges = portrait::load_hp_gauge_configs(directory / "gauge.json");
    expect(gauges[1].enabled && gauges[2].offset_x == 5 && gauges[2].offset_y == -3 && gauges[3].offset_x == 0,
           "combined gauge settings lost their shared defaults or slot overrides");
    expect(gauges[0].player_icon.enabled && !gauges[1].player_icon.enabled && gauges[2].player_icon.animations[2] == 2,
           "label settings did not inherit independently");
    text(
        directory / "shared-gauge.json",
        R"({"party":{"offset_x":2,"player_icon":{"offset_y":3},"slots":{"2":{"offset_x":5,"player_icon":{"offset_y":7}}}}})");
    const auto shared = portrait::load_hp_gauge_configs(directory / "shared-gauge.json");
    expect(shared[1].offset_x == 2 && shared[2].offset_x == 7 && shared[3].offset_x == 2 &&
               shared[1].player_icon.offset_y == 3 && shared[2].player_icon.offset_y == 10,
           "shared and individual gauge/icon offsets did not add together");
    for (const auto &bad : {R"({"party":{"slots":{"4":{}}}})", R"({"party":{"slots":{"2":{"offset_x":"bad"}}}})",
                            R"({"player_icon":{"animation":1,"animation":2}})", R"({"limit_text":{"animaton":1}})"}) {
        text(directory / "bad-gauge.json", bad);
        rejects([&] { portrait::load_hp_gauge_configs(directory / "bad-gauge.json"); },
                "invalid nested gauge settings were accepted");
    }
    text(directory / "portraits.json", R"({"version":1,"characters":{
        "Roxas":{"player":{"enabled":true,"archive":"portrait.any-extension","texture":"portrait.DDS","image_entry":"img","sequence_entry":"seq","offset_x":3,"offset_y":-2,"scale":1}},
        "Luxord":{"party":{"enabled":true,"archive":"old.archive","texture":"portrait.png","image_entry":"img","sequence_entry":"seq"}},
        "Xigbar":{"party":{"enabled":true,"archive":"missing","texture":"portrait.DDS"}}
    }})");
    const auto c = portrait::load_portrait_config(directory / "portraits.json");
    expect(c[12][0].enabled && !c[12][1].enabled && c[9][1].enabled, "per-character, per-role mappings");
    expect(c[12][0].normal == 3 && c[12][0].damage_small == 1 && c[12][0].alert == 0 && c[12][0].dead == 2,
           "original face state defaults");
    expect(c[9][1].normal == 3 && c[9][1].alert == 0 && c[9][1].dead == 2 && c[9][1].scale == 0.65f,
           "party portrait must share KH2 states and gauge scale");
    expect(c[0][0].scale == 0.65f, "default player portrait scale must match the gauge");
    const std::string numeric_locale = std::setlocale(LC_NUMERIC, nullptr);
    text(directory / "decimal.json", R"({"characters":{"Roxas":{"player":{"scale":0.65}}}})");
    if (std::setlocale(LC_NUMERIC, "de-DE")) {
        const auto localized = portrait::load_portrait_config(directory / "decimal.json");
        const auto localized_gauge = portrait::load_hp_gauge_configs(directory / "gauge.json");
        expect(localized[12][0].scale == 0.65f && localized_gauge[1].style.geometry_scale > 0.5f,
               "decimal JSON settings depend on the process locale");
    }
    std::setlocale(LC_NUMERIC, numeric_locale.c_str());
    text(directory / "bad.json", R"({"characters":{"Roxas":{"player":{"scale":0}}}})");
    rejects([&] { portrait::load_portrait_config(directory / "bad.json"); }, "zero scale accepted");
    text(directory / "bad.json",
         R"({"characters":{"Roxas":{"player":{"enabled":true,"archive":"../bad","texture":"x.png"}}}})");
    rejects([&] { portrait::load_portrait_config(directory / "bad.json"); }, "parent directory accepted");
    text(directory / "bad.json", R"({"characters":{"Roxas":{"player":{"normal":0,"normal":1}}}})");
    rejects([&] { portrait::load_portrait_config(directory / "bad.json"); }, "duplicate key accepted");
    text(directory / "bad.json", R"({"characters":{"Roxas":{"player":{"scalee":1}}}})");
    rejects([&] { portrait::load_portrait_config(directory / "bad.json"); }, "unknown setting accepted");
    text(directory / "bad.json", R"({"characters":{"Typo":{}}})");
    rejects([&] { portrait::load_portrait_config(directory / "bad.json"); }, "unknown empty character accepted");
}
void playback()
{
    const auto b = sequence(0x150);
    const auto data = seq::read_sequence(b.data(), b.size());
    portrait::PortraitConfig c;
    portrait::PortraitPlayer p;
    p.update(data, c, 100, 100, false, 0, true);
    expect(p.clip() == 3, "normal animation (KH2 status 0 -> clip 3)");
    p.update(data, c, 90, 100, false, 1, false);
    expect(p.clip() == 1 && p.frame() == 0, "damage starts on hit");
    p.update(data, c, 90, 100, false, 30, false);
    expect(p.clip() == 1 && p.frame() == 30, "damage stays active");
    p.update(data, c, 90, 100, false, 30, false);
    expect(p.clip() == 3, "damage returns to current state");
    p.update(data, c, 20, 100, true, 1, false);
    p.update(data, c, 20, 100, true, 60, false);
    expect(p.clip() == 0, "damage ends in alert state (KH2 status 1 -> clip 0)");
    p.update(data, c, 0, 100, true, 1, false);
    expect(p.clip() == 2, "defeated overrides damage (KH2 status 2 -> clip 2)");
    p.update(data, c, 100, 100, false, 1, false);
    expect(p.clip() == 3, "revival returns to normal");
    c.animation_override = 2;
    p.update(data, c, 100, 100, false, 1, false);
    expect(p.clip() == 2, "configurable override");
    c.animation_override = -1;
    c.intro = 3;
    c.play_intro = true;
    p.update(data, c, 20, 100, true, 0, true);
    expect(p.clip() == 3, "custom intro");
    p.update(data, c, 20, 100, true, 60, false);
    expect(p.clip() == 0, "looping intro returns to live state after one pass");
    for (unsigned address = 0; address < 0x8000; address += 32) {
        int expected = -1;
        const unsigned offsets[]{0x1800, 0x18c0, 0x1920, 0x24c0};
        for (int slot = 0; slot < 4; ++slot)
            for (unsigned row = 0; row < (slot ? 3u : 6u); ++row)
                for (unsigned col = 0; col < (slot ? 3u : 6u); ++col)
                    if (address == offsets[slot] + row * 0x400 + col * 32)
                        expected = slot;
        expect(portrait::portrait_tile_slot(address) == expected, "portrait selector affects unrelated tiles");
    }
}
void collect(unsigned frame, bool cpu = false)
{
    emitted.clear();
    std::fill(std::begin(masked), std::end(masked), 0);
    khdays_ui_kh2_portrait_frame(frame);
    khdays_ui_kh2_portrait_reference(cpu);
    khdays_ui_kh2_portrait_prepare();
    khdays_ui_kh2_portrait_emit();
}
void runtime()
{
    collect(1);
    expect(emitted.size() == 2 && masked[0] == 36 && masked[1] == 9 && !masked[2] && !masked[3],
           "valid slots replace exactly their own tiles; missing/disabled slots fall back");
    expect(reads == 0 && uploads == 0 && atlases == 2, "live GPU path uploads atlases only once");
    KhdaysSequenceImage programs[4];
    khdays_ui_kh2_portrait_commands(programs);
    expect(programs[0].words[KSI_COUNT] == 1 && programs[1].words[KSI_COUNT] == 1 && programs[2].words[KSI_COUNT] == 0,
           "per-slot GPU programs");
    const auto original = emitted[0];
    parent_on = 1;
    parent_x = 240;
    parent_y = 150;
    collect(2);
    expect(std::abs(emitted[0].x - original.x - 8) < 0.01f && std::abs(emitted[0].y - original.y + 10) < 0.01f,
           "portrait follows absolute gauge center, with configured local offsets");
    const int before = allocations;
    for (unsigned i = 3; i <= 30; ++i)
        collect(i);
    expect(allocations == before && atlases == 2 && uploads == 0,
           "stable live path has no reallocations or CPU uploads");
    collect(31, true);
    expect(uploads == 2, "reference path renders independently");
    paused = 1;
    collect(32);
    khdays_ui_kh2_portrait_commands(programs);
    paused = 0;
    present = 0;
    collect(33);
    khdays_ui_kh2_portrait_commands(programs);
    expect(emitted.empty() && !masked[0] && !programs[0].words[KSI_CLEAR],
           "leaving the HUD clears commands and suppression");
}
void private_assets(const std::filesystem::path &folder)
{
    for (const auto *name : {"P_EX110.a.us", "P_EX020.a.us"}) {
        const auto a =
            render::load_sequence_asset(folder / name, "face", "face", folder / (std::string(name) + "_face.dds"));
        expect(a.sequence.clips.size() == 4, "private face clip count");
        for (unsigned clip = 0; clip < 4; ++clip)
            for (int frame = 0; frame < seq::sequence_frame_count(a.sequence, clip); ++frame) {
                const auto evaluated = seq::evaluate_sequence(a.sequence, clip, float(frame));
                expect(!evaluated.quads.empty(), "private portrait lost a visible frame");
            }
        std::cout << name << ": 4 clips validated, DDS " << a.texture.width << 'x' << a.texture.height << '\n';
    }
}

void party_runtime()
{
    present = 1;
    test_party = true;
    khdays_ui_kh2_party_frame(1);
    expect(khdays_ui_kh2_party_ready(1) && khdays_ui_kh2_party_ready(2) && khdays_ui_kh2_party_ready(3) &&
               !khdays_ui_kh2_party_ready(0),
           "party gauges claim only their own slots");
    auto collect_party = [&](unsigned frame, bool cpu = false) {
        khdays_ui_kh2_party_frame(frame);
        khdays_ui_kh2_party_reference(cpu);
        khdays_ui_kh2_party_prepare();
        for (int slot = 1; slot <= 3; ++slot) {
            KhdaysUiElement face;
            expect(khdays_ui_kh2_party_box(slot, &face), "native party anchor missing");
            face.image = 0;
            expect(khdays_ui_kh2_party_compose(slot, &face, nullptr),
                   "party gauge did not compose with the native face");
        }
    };
    const int uploads_before = uploads;
    collect_party(2);
    KhdaysHudParent parent;
    expect(khdays_ui_kh2_party_parent(1, &parent) && parent.x == 244 && parent.y == 100,
           "party center is not normalized to its live portrait");
    KhdaysHudParent second{}, third{};
    expect(khdays_ui_kh2_party_parent(2, &second) && khdays_ui_kh2_party_parent(3, &third) && second.x == 249,
           "party slots did not apply independent offsets");
    const float radius = 23 * 0.65f, gap = 160 - 45 * 0.65f - (parent.y + radius);
    expect(std::abs(parent.y - radius - (second.y + 3 + radius) - gap) < 0.001f &&
               std::abs(second.y + 3 - radius - (third.y + radius) - gap) < 0.001f,
           "party spacing must match the visible player-to-party gap");
    const int before = allocations;
    for (unsigned frame = 3; frame < 20; ++frame)
        collect_party(frame);
    expect(allocations == before && uploads == uploads_before,
           "live party gauges allocated or uploaded pixels repeatedly");
    KhdaysSequenceImage programs[3];
    khdays_ui_kh2_party_commands(programs);
    expect(programs[0].words[KSI_COUNT] > 0 && programs[1].words[KSI_COUNT] > 0 && programs[2].words[KSI_COUNT] > 0,
           "party GPU programs were not independent");
    party_health[0] = 31;
    collect_party(20);
    const auto damage = programs[0];
    khdays_ui_kh2_party_commands(programs);
    expect(programs[0].words[KSI_COUNT] > damage.words[KSI_COUNT],
           "damage trail did not retain the previous bar extent");
    paused = 1;
    collect_party(21);
    KhdaysSequenceImage frozen[3];
    khdays_ui_kh2_party_commands(frozen);
    expect(std::memcmp(programs, frozen, sizeof(programs)) == 0, "paused party gauge changed its animation");
    paused = 0;
    collect_party(22, true);
    expect(uploads > uploads_before, "party CPU reference did not render");
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    preferences[KHDAYS_OPT_PARTY_HP] = 0;
    khdays_hud_settings_publish(preferences);
    khdays_ui_kh2_party_frame(23);
    khdays_ui_kh2_party_reference(0);
    khdays_ui_kh2_party_prepare();
    KhdaysUiElement disabled_box{};
    expect(!khdays_ui_kh2_party_ready(1) && !khdays_ui_kh2_party_box(1, &disabled_box),
           "party HP toggle retained replacement");
    collect(23);
    expect(emitted.empty() && !masked[0] && !masked[1], "HUD portrait toggle retained drawing or suppression");
    preferences[KHDAYS_OPT_PARTY_HP] = 1;
    khdays_hud_settings_publish(preferences);
    collect_party(24);
    expect(khdays_ui_kh2_party_ready(1) && !emitted.empty(), "party HP required a reload to enable");
    hd_graphics = false;
    khdays_ui_kh2_party_frame(25);
    khdays_ui_kh2_party_reference(0);
    khdays_ui_kh2_party_prepare();
    emitted.clear();
    expect(!khdays_ui_kh2_party_ready(1) && emitted.empty(), "HD graphics off retained the party gauge");
    collect(25);
    expect(emitted.empty() && !masked[0] && !masked[1], "HD graphics off retained portrait drawing or suppression");
    hd_graphics = true;
    collect_party(26);
    expect(khdays_ui_kh2_party_ready(1) && !emitted.empty(), "HD graphics on required reloading the party gauge");
    collect(26);
    expect(!emitted.empty() && masked[0] && masked[1], "HD graphics on required reloading portraits");
    present = 0;
    khdays_ui_kh2_party_frame(23);
    khdays_ui_kh2_party_reference(0);
    khdays_ui_kh2_party_commands(programs);
    expect(!khdays_ui_kh2_party_ready(1) && !programs[0].words[KSI_COUNT], "party gauge retained stale scene commands");
}
void labels_runtime()
{
    present = parent_on = 1;
    parent_x = 230;
    parent_y = 160;
    test_labels = true;
    auto collect_labels = [&](unsigned frame, bool cpu = false) {
        khdays_ui_kh2_party_frame(frame);
        khdays_ui_kh2_labels_frame(frame);
        khdays_ui_kh2_party_reference(cpu);
        khdays_ui_kh2_labels_reference(cpu);
        khdays_ui_kh2_party_prepare();
        khdays_ui_kh2_labels_prepare();
    };
    collect_labels(40);
    expect(std::find(label_border_masks.begin(), label_border_masks.end(), std::pair{25, 21}) !=
                   label_border_masks.end() &&
               std::find(label_border_masks.begin(), label_border_masks.end(), std::pair{26, 21}) !=
                   label_border_masks.end(),
           "player replacement left the compact badge's upper border visible");
    expect(std::find(label_border_masks.begin(), label_border_masks.end(), std::pair{27, 12}) ==
               label_border_masks.end(),
           "native party-label fallback lost its original upper border");
    for (int i = 0; i < 5; ++i) {
        KhdaysUiElement box;
        expect(khdays_ui_kh2_label_box(i, &box), "native player/limit label was not found");
    }
    KhdaysUiElement box;
    khdays_ui_kh2_label_box(1, &box);
    emitted.clear();
    expect(khdays_ui_kh2_label_native(1, &box), "native party label fallback missing");
    KhdaysUiElement limit_box{};
    khdays_ui_kh2_label_box(4, &limit_box);
    limit_box.image = 0;
    khdays_ui_kh2_label_native(4, &limit_box);
    expect(emitted.empty(), "native labels must be deferred until gauges and portraits are emitted");
    KhdaysUiElement face{};
    expect(khdays_ui_kh2_party_box(1, &face), "party gauge anchor missing");
    face.image = 0;
    face.id = "hud.party1";
    expect(khdays_ui_kh2_party_compose(1, &face, nullptr), "party gauge did not emit before its number");
    khdays_ui_kh2_labels_emit();
    const auto tag = std::find_if(emitted.begin(), emitted.end(),
                                  [](const auto &e) { return std::string(e.id) == "hud.party1.icon"; });
    KhdaysHudParent tag_parent{};
    khdays_ui_kh2_party_parent(1, &tag_parent);
    expect(tag != emitted.end() && tag > emitted.begin() && emitted[0].id == std::string("hud.party1") &&
               tag->x == box.x && std::abs(tag->y - (tag_parent.y + 37 * (23.0f / 45) * 0.65f - box.h)) < 0.001f,
           "native number must draw above its gauge at the bar attachment");
    const auto limit = std::find_if(emitted.begin(), emitted.end(),
                                    [](const auto &e) { return std::string(e.id) == "hud.limit_text"; });
    expect(limit != emitted.end() && limit > emitted.begin() &&
               (!native_limit || (limit->image == 0 && limit->x == limit_box.x && limit->y == limit_box.y)),
           "LIMIT text must be emitted above the gauge without moving its native placement");
    for (int i = 1; i < 4; ++i) {
        KhdaysUiElement own{};
        khdays_ui_kh2_label_box(i, &own);
        expect(own.y == float(104 - (i - 1) * 24) && own.w == 16,
               "party number rows were missed, combined, or assigned to another slot");
    }
    emitted.clear();
    khdays_ui_kh2_label_box(0, &box);
    khdays_ui_kh2_label_native(0, &box);
    expect(emitted.empty(), "sequence label also emitted its native duplicate");
    expect(khdays_ui_kh2_label_cell(0, 173) && !khdays_ui_kh2_label_cell(0, 64) &&
               khdays_ui_kh2_label_cell(4, 0x7180) && !khdays_ui_kh2_label_cell(4, 173),
           "label suppression affects unrelated HUD tiles");
    KhdaysSequenceImage programs[5];
    khdays_ui_kh2_labels_commands(programs);
    expect(programs[0].words[KSI_COUNT] && !programs[1].words[KSI_COUNT] && programs[2].words[KSI_COUNT] &&
               !programs[3].words[KSI_COUNT] && bool(programs[4].words[KSI_COUNT]) == !native_limit,
           "optional label replacements or missing-resource fallback failed");
    expect(std::bit_cast<float>(programs[2].words[KSI_HEADER]) > 15,
           "3P did not select its number-specific sequence assignment");
    const int before = allocations, before_uploads = uploads;
    collect_labels(41);
    expect(before == allocations && before_uploads == uploads, "live labels repeatedly allocate or upload pixels");
    khdays_ui_kh2_labels_commands(programs);
    paused = 1;
    collect_labels(42);
    KhdaysSequenceImage frozen[5];
    khdays_ui_kh2_labels_commands(frozen);
    expect(std::memcmp(programs, frozen, sizeof(programs)) == 0, "paused label animation advanced");
    paused = 0;
    collect_labels(43, true);
    expect(uploads == before_uploads + (native_limit ? 2 : 3), "label CPU reference did not render each replacement");
    test_labels = false;
    collect_labels(44);
    khdays_ui_kh2_labels_commands(programs);
    expect(!programs[4].words[KSI_COUNT] && !programs[4].words[KSI_CLEAR],
           "limit sequence remained visible after the native limit text disappeared");
    collect_labels(95);
    KhdaysHudParent failed{};
    expect(!khdays_ui_kh2_party_parent(3, &failed) && !khdays_ui_kh2_party_ready(3),
           "bad party attachment escaped fallback");
    collect_labels(96);
    expect(!khdays_ui_kh2_party_ready(3) && khdays_ui_kh2_party_ready(1),
           "bad party attachment retried every frame or disabled another slot");
    test_labels = true;
    for (unsigned frame = 100; frame <= 141; ++frame) {
        limit_tiles = frame < 105 || frame > 110;
        collect_labels(frame);
        khdays_ui_kh2_labels_commands(programs);
        if (!native_limit)
            expect(programs[4].words[KSI_COUNT] != 0, "native LIMIT blinking interrupted the custom animation");
    }
    if (!native_limit)
        expect(std::abs(std::bit_cast<float>(programs[4].words[KSI_HEADER]) - 6.5f) < .001f,
               "LIMIT replayed its entrance instead of looping to authored frame 10");
    hd_graphics = false;
    collect_labels(142);
    khdays_ui_kh2_labels_commands(programs);
    for (int i = 0; i < 5; ++i) {
        KhdaysUiElement box{};
        expect(!programs[i].words[KSI_CLEAR] && !khdays_ui_kh2_label_box(i, &box),
               "HD graphics off retained label drawing or suppression");
    }
    hd_graphics = true;
    collect_labels(143);
    khdays_ui_kh2_labels_commands(programs);
    expect(programs[0].words[KSI_CLEAR], "HD graphics on required reloading labels");
    test_labels = false;
    collect_labels(144);
    khdays_ui_kh2_labels_commands(programs);
    expect(!programs[4].words[KSI_CLEAR], "LIMIT remained after availability ended");
}
} // namespace
extern "C" {
int khdays_textures_hd(void)
{
    return hd_graphics;
}
int khdays_link_limit_available(void)
{
    return test_labels;
}
int khdays_mods_find(const char *name, char *out, int size)
{
    const bool party = std::string(name) == "layouts/gauge/en/gauge.json";
    if (!party && std::string(name) != "graphics/UI/btl/main.p2#1/chr/portraits.json")
        return 0;
    const auto p = (directory / (party ? "gauge.json" : "portraits.json")).string();
    if (int(p.size()) >= size)
        return 0;
    std::memcpy(out, p.c_str(), p.size() + 1);
    return 1;
}
int khdays_settings_game_language(void)
{
    return 0;
}
const char *khdays_language_code(int)
{
    return "en";
}
int khdays_ui_element_direct(const KhdaysUiElement *e)
{
    return e->image;
}
unsigned khdays_ui_element_rgba(const KhdaysUiElement *, int x, int y)
{
    ++reads;
    return x == 2 && y == 2 ? 0xff123456 : 0;
}
int khdays_link_paused(void)
{
    return paused;
}
int khdays_link_hud_portrait(int slot, int *icon, int *health, int *maximum, int *low)
{
    if (!present)
        return 0;
    const int icons[]{12, 9, 1, 12};
    *icon = icons[slot];
    *health = test_party && slot > 0 ? party_health[slot - 1] : hp;
    *maximum = 372;
    *low = hp < 50;
    return 1;
}
int khdays_ui_mode(int)
{
    return KHDAYS_UI_ON;
}
void khdays_ui_changed(void) {}
void khdays_log(const char *, ...) {}
int khdays_ui_scale(void)
{
    return scale;
}
int khdays_textures_add_info(const char *, int w, int h, const unsigned *pixels, unsigned)
{
    registered_atlases.push_back({pixels, std::vector<unsigned>(pixels, pixels + std::size_t(w) * h)});
    return 100 + atlases++;
}
int khdays_ui_image_get(int w, int h)
{
    ++allocations;
    images.emplace_back(std::size_t(w) * std::size_t(h));
    return int(images.size()) - 1;
}
void khdays_ui_image_put(int) {}
int khdays_ui_image_replacement(int id)
{
    return id;
}
unsigned *khdays_ui_image_begin(int id)
{
    return images[std::size_t(id)].data();
}
void khdays_ui_image_end(int)
{
    ++uploads;
}
int khdays_ui_kh2_hp_ready(void)
{
    return present && parent_on;
}
int khdays_ui_kh2_hp_parent(int slot, KhdaysHudParent *p)
{
    if (slot > 0)
        return khdays_ui_kh2_party_parent(slot, p);
    if (!parent_on)
        return 0;
    *p = {parent_x, parent_y, 1, 1, 1};
    return 1;
}
int khdays_bg_text_layer(int, int, KhdaysBgLayer *l)
{
    *l = {0, 0, 32, 32, 0, 0, 0, 0};
    return present;
}
int khdays_bg_cell_shows(int, const KhdaysBgLayer *, unsigned entry)
{
    return entry != 0;
}
unsigned khdays_bg_map_entry(int, const KhdaysBgLayer *, int x, int y)
{
    if (test_labels) {
        if (y == 21 && (x == 25 || x == 26))
            return x == 25 ? 140 : 297;
        if (x == 25 && y == 22)
            return 172;
        if (x == 26 && y == 22)
            return 173;
        if (limit_tiles && x >= 22 && x < 26 && y == 17) {
            const unsigned tiles[]{0x180, 0x181, 0x1a0, 0x1a1};
            return tiles[x - 22];
        }
        for (int i = 0; i < 3; ++i)
            if (y == 12 - i * 3 && (x == 27 || x == 28))
                return x == 27 ? 140 : 297;
        for (int i = 0; i < 3; ++i)
            if (y == 13 - i * 3 && (x == 27 || x == 28)) {
                const unsigned numbers[]{174, 141, 142};
                return x == 27 ? 172 : numbers[i];
            }
    }
    if (x >= 26 && x < 32 && y >= 17 && y < 23)
        return (0x1800u + unsigned(y - 17) * 0x400 + unsigned(x - 26) * 32) / 32;
    const unsigned offsets[]{0x18c0, 0x1920, 0x24c0};
    for (int slot = 0; slot < 3; ++slot)
        if (x >= 29 && x < 32 && y >= 12 - slot * 3 && y < 15 - slot * 3)
            return (offsets[slot] + unsigned(y - (12 - slot * 3)) * 0x400 + unsigned(x - 29) * 32) / 32;
    return 0;
}
const char *khdays_bg_origin(int, unsigned)
{
    return "UI/btl/main.p2@c00";
}
int khdays_ui_cell_taken(int, int, int, int)
{
    return 0;
}
void khdays_ui_left_out_cell(int, int, int, int x, int y)
{
    if (test_labels)
        label_border_masks.emplace_back(x, y);
    KhdaysBgLayer l{};
    const int slot = portrait::portrait_tile_slot(khdays_bg_map_entry(0, &l, x, y) * 32);
    if (slot >= 0)
        ++masked[slot];
}
void khdays_ui_emit_extended(int, const KhdaysUiElement *element, const KhdaysUiElement *)
{
    expect(!(element->slot & KES_PICTURE) || (element->slot & KES_HUD),
           "portrait, party gauge, or sequence label bypasses single-screen HUD visibility");
    emitted.push_back(*element);
}
}
int main(int argc, char **argv)
{
    native_limit = argc == 2 && std::string(argv[1]) == "--native-limit";
    directory = std::filesystem::temp_directory_path() /
                ("khdays-portrait-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(directory);
        assets();
        configuration();
        playback();
        runtime();
        party_runtime();
        labels_runtime();
        for (const auto &atlas : registered_atlases)
            expect(std::equal(atlas.expected.begin(), atlas.expected.end(), atlas.pixels),
                   "registered atlas was freed or modified after a replacement failed");
        if (argc == 2 && !native_limit)
            private_assets(argv[1]);
        std::filesystem::remove_all(directory);
        std::cout << "KH2 portrait tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
