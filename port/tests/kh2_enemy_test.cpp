// Synthetic enemy clips and native RAM: capacity, rollover, selective replacement, and cached GPU commands.
#include "../host/app/hud_settings.h"
#include "../ui/kh2_enemy_gauge.h"
#include "../ui/kh2_enemy_hud.h"
#include "../ui/kh2_hp_config.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "../ui/elements.h"
#include "../ui/bg.h"
#include "../ui/parts.h"
#include "../runtime/scripts/game_link.h"
}
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
bool hd_graphics = true;
namespace gauge = khdays::ui::kh2;
namespace seq = khdays::assets::kh2;
std::vector<unsigned char> ram(0x02400000);
constexpr unsigned ctx = 0x02100000;
std::filesystem::path config_file;
bool missing = false, no_config = false, paused = false, shown = true, source = true;
bool enemy_cells = true;
int native_x = 24, native_y = 1;
int uploads = 0, allocations = 0, scale = 1, emitted = 0;
std::vector<std::uint32_t> pixels;
std::vector<std::array<int, 3>> suppressed;
KhdaysUiElement element{}, anchor{};
void expect(bool good, const char *why)
{
    if (!good)
        throw std::runtime_error(why);
}
bool near(float a, float b)
{
    return std::fabs(a - b) < 0.01f;
}
void word(unsigned at, unsigned v)
{
    std::memcpy(ram.data() + at, &v, 4);
}
void half(unsigned at, unsigned short v)
{
    std::memcpy(ram.data() + at, &v, 2);
}
seq::SequenceData fixture()
{
    seq::SequenceData d;
    for (unsigned i = 0; i < 47; ++i) {
        seq::SequencePart p;
        p.uv = {int(i), 0, int(i + 1), 1};
        p.colors.fill(0x80808080);
        d.parts.push_back(p);
        std::array<int, 4> rect{0, -5, 100, 5};
        if (i == 38)
            rect = {-23, 3, 2, 13};
        if (i == 40)
            rect = {-1, -5, 1, 5};
        if (i == 42)
            rect = {-200, -5, 0, 5};
        if (i >= 44)
            rect = {-33, 3, -20, 13};
        d.groups.push_back({rect, i});
        d.sprites.push_back({static_cast<unsigned short>(i), 1});
        seq::SequenceAnimation a;
        a.flags = 0x77ff7;
        a.sprite = i;
        a.time = {0, 90};
        a.scale = a.scale_x = a.scale_y = {1, 1};
        a.colors.fill(0x80808080);
        if (i == 38) {
            a.x = {498, 498};
            a.y = {31, 31};
        }
        if (i == 40)
            a.x = {-1, -1};
        d.animations.push_back(a);
        seq::SequenceClip c;
        c.animations = {static_cast<unsigned short>(i), 1};
        c.restart = i == 41 || i == 46 ? 100u : 0u;
        c.parameters[4] = 11;
        d.clips.push_back(c);
    }
    return d;
}
std::vector<seq::SequenceQuad> component(const seq::SequenceFrame &frame, int clip)
{
    std::vector<seq::SequenceQuad> out;
    for (const auto &q : frame.quads)
        if (near(q.vertices[0].u, float(clip) + 0.5f))
            out.push_back(q);
    return out;
}
unsigned component(const KhdaysSequenceImage &commands, int clip)
{
    unsigned count = 0;
    for (unsigned i = 0; i < commands.words[KSI_COUNT]; ++i) {
        float u;
        std::memcpy(&u, commands.words + KSI_HEADER + i * KSI_QUAD_WORDS + 2, 4);
        count += near(u, float(clip) + 0.5f);
    }
    return count;
}
void composition()
{
    for (int capacity : {250, 750}) {
        const auto full = gauge::enemy_gauge_extent(capacity, capacity);
        const auto over = gauge::enemy_gauge_extent(capacity + 1, capacity);
        expect(full.tanks == 0 && near(full.bar, 200), "full bar must not add a tank");
        expect(over.tanks == 1 && near(over.bar, 200.0f / capacity), "rollover capacity");
        expect(gauge::enemy_gauge_extent(2 * capacity, capacity).tanks == 1, "exact multiple tank count");
        expect(gauge::enemy_gauge_extent(0, capacity).tanks == 0, "zero HP tank count");
    }
    auto d = fixture();
    gauge::EnemyGaugeSample s{375, 375, 375, 250, 0, 100};
    auto f = gauge::evaluate_enemy_gauge(d, s);
    auto bar = component(f, 39).at(0);
    expect(near(bar.vertices[1].x - bar.vertices[0].x, 100), "partial initial foreground length");
    expect(component(f, 42).size() == 1, "missing preview behind partial initial bar");
    expect(component(f, 44).size() == 1 && component(f, 45).size() == 1, "initial tank composition");
    s.hp = 300;
    f = gauge::evaluate_enemy_gauge(d, s);
    expect(near(component(f, 43)[0].vertices[0].x, -39.5f), "green bar must stay anchored to the right edge");
    s.damage_frame = 0;
    f = gauge::evaluate_enemy_gauge(d, s);
    expect(near(component(f, 41)[0].vertices[0].x, -99.5f), "damage must keep its own right-anchored extent");
    s.hp = 250;
    s.damage_frame = 0;
    f = gauge::evaluate_enemy_gauge(d, s);
    bar = component(f, 39).at(0);
    expect(near(bar.vertices[1].x - bar.vertices[0].x, 200), "full bar after tank rollover");
    expect(component(f, 42).empty() && component(f, 44).empty(), "old preview or green tank survived rollover");
    expect(component(f, 46).size() == 1 && component(f, 41).size() == 1, "missing red tank and damage bar");
    s.damage_frame = 100;
    f = gauge::evaluate_enemy_gauge(d, s);
    expect(component(f, 46).empty() && component(f, 41).empty() && component(f, 45).size() == 1,
           "damage clips did not disappear into their out-of-range restart");
    s.hp = 0;
    f = gauge::evaluate_enemy_gauge(d, s);
    expect(component(f, 43).empty() && component(f, 44).empty(), "zero HP still draws green");
    s = {1125, 1125, 1125, 750, 0, 100};
    f = gauge::evaluate_enemy_gauge(d, s);
    expect(near(component(f, 43)[0].vertices[1].x - component(f, 43)[0].vertices[0].x, 100),
           "Mission Mode mapping differs from Story Mode");
    const auto before = component(f, 43)[0].vertices[0].x;
    d.animations[38].x = {508, 508};
    f = gauge::evaluate_enemy_gauge(d, s);
    expect(near(component(f, 43)[0].vertices[0].x - before, 10), "children do not follow animated parent");
    bool rejected = false;
    try {
        gauge::enemy_gauge_extent(1, 0);
    } catch (const std::exception &) {
        rejected = true;
    }
    expect(rejected, "zero capacity accepted");
}
void configs(const std::filesystem::path &folder)
{
    const auto file = folder / "config.json";
    std::ofstream(file) << R"({"scale":0.75,"archive":"local.bin"})";
    auto c = gauge::load_enemy_gauge_config(file);
    expect(!c.enabled && near(c.scale, 0.75f) && c.archive == "local.bin", "legacy config fallback");
    std::ofstream(file) << R"({"enabled":false,"enemy":{"enabled":true,"offset_x":-8,"offset_y":12,
                          "scale":0.65,"fill":40,"source_origin_x":500,"tank_offset_x":3}})";
    c = gauge::load_enemy_gauge_config(file);
    expect(c.enabled && near(c.scale, 0.65f) && c.style.fill == 40 && c.offset_x == -8 && c.offset_y == 12 &&
               c.style.source_origin_x == 500 && c.style.tank_offset_x == 3,
           "enemy overrides");
    expect(!gauge::load_hp_gauge_config(file).enabled, "enemy settings changed player config");
    std::ofstream(file) << R"({"enemy":{"misspelled":1}})";
    bool rejected = false;
    try {
        gauge::load_enemy_gauge_config(file);
    } catch (const std::exception &) {
        rejected = true;
    }
    expect(rejected, "unknown enemy setting accepted");
}
} // namespace

extern "C" {
int khdays_textures_hd(void)
{
    return hd_graphics;
}
unsigned char *khdays_window = ram.data();
int khdays_mods_find(const char *, char *out, int size)
{
    if (no_config)
        return 0;
    const auto s = config_file.string();
    if (int(s.size()) >= size)
        return 0;
    std::memcpy(out, s.c_str(), s.size() + 1);
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
int khdays_ui_mode(int)
{
    return KHDAYS_UI_ON;
}
int khdays_overlay_loaded(int)
{
    return 1;
}
unsigned khdays_link_scene(unsigned *)
{
    return 2;
}
int khdays_link_paused(void)
{
    return paused;
}
void khdays_ui_changed(void) {}
void khdays_log(const char *, ...) {}
int khdays_ui_scale(void)
{
    return scale;
}
int khdays_textures_add_info(const char *, int, int, const unsigned *, unsigned)
{
    return 2;
}
int khdays_ui_image_get(int w, int h)
{
    ++allocations;
    pixels.resize(std::size_t(w) * h);
    return 1;
}
void khdays_ui_image_put(int) {}
int khdays_ui_image_replacement(int i)
{
    return i;
}
uint32_t *khdays_ui_image_begin(int)
{
    return pixels.data();
}
void khdays_ui_image_end(int)
{
    ++uploads;
}
void khdays_ui_emit_extended(int, const KhdaysUiElement *e, const KhdaysUiElement *box)
{
    ++emitted;
    element = *e;
    anchor = *box;
}
void khdays_ui_left_out_cell(int, int screen, int bg, int x, int y)
{
    expect(screen == 0, "other screen suppressed");
    suppressed.push_back({bg, x, y});
}
int khdays_bg_text_layer(int screen, int bg, KhdaysBgLayer *l)
{
    if (!shown || screen != 0 || bg != 3)
        return 0;
    *l = {0, 0x4000, 32, 32, 0, 0, 0, 3};
    return 1;
}
uint32_t khdays_bg_map_entry(int, const KhdaysBgLayer *, int x, int y)
{
    if (enemy_cells && y == native_y && x >= native_x && x < native_x + 8)
        return 47u + unsigned(x - native_x);
    if (enemy_cells && y == native_y + 1 && x >= native_x + 5 && x <= native_x + 6)
        return 55u + unsigned(x - native_x - 5);
    if (y == 0 && x == 28)
        return 500;
    if (y == 1 && x == 0)
        return 47;
    return 0;
}
int khdays_bg_cell_shows(int, const KhdaysBgLayer *, unsigned e)
{
    return e != 0;
}
const char *khdays_bg_origin(int, unsigned at)
{
    return source && at < 0x4000 + 500 * 32 ? "UI/btl/main.p2#4" : "UI/text/name";
}
}
namespace khdays::render::kh2 {
SequenceAsset load_sequence_asset(const std::filesystem::path &, const std::string &, const std::filesystem::path &)
{
    if (missing)
        throw std::runtime_error("missing enemy asset");
    SequenceAsset a;
    a.sequence = fixture();
    a.logical_size = {64, 2};
    a.texture.width = 64;
    a.texture.height = 2;
    a.texture.rgba.assign(64 * 2 * 4, 255);
    return a;
}
} // namespace khdays::render::kh2

int main(int argc, char **argv)
{
    const auto folder = std::filesystem::temp_directory_path() /
                        ("khdays-enemy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        std::filesystem::create_directories(folder);
        composition();
        configs(folder);
        const std::string mode = argc > 1 ? argv[1] : "";
        missing = mode == "--missing";
        no_config = mode == "--no-config";
        const bool cpu = mode == "--reference";
        config_file = folder / "gauge.json";
        std::ofstream(config_file) << R"({"enemy":{"enabled":true,"offset_x":-12,"offset_y":20,"scale":0.65}})";
        word(0x0207f618, ctx);
        word(ctx + 0x48, 1);
        word(ctx + 0x44, 1);
        half(ctx + 0xec, 375);
        half(ctx + 0xee, 375);
        word(ctx + 0x20, ctx + 0x1000);
        std::fill_n(ram.begin() + ctx + 0x1000, 0xc0, static_cast<unsigned char>(0x22));
        word(ctx + 0x48, 0);
        khdays_ui_kh2_enemy_frame(0);
        khdays_ui_kh2_enemy_reference(cpu);
        khdays_ui_kh2_enemy_emit();
        expect(emitted == 0 && suppressed.empty(), "an unseen target claimed stale native gauge tiles");
        word(ctx + 0x48, 1);
        const auto untouched = ram;
        khdays_ui_kh2_enemy_frame(1);
        khdays_ui_kh2_enemy_reference(cpu);
        khdays_ui_kh2_enemy_emit();
        expect(ram == untouched, "enemy HUD changed game memory");
        if (missing || no_config) {
            expect(emitted == 0 && suppressed.empty() && allocations == 0, "missing asset did not keep native HUD");
        } else {
            if (emitted != 1 || suppressed.size() != 10)
                throw std::runtime_error("native bar and tanks: " + std::to_string(emitted) + " images, " +
                                         std::to_string(suppressed.size()) + " suppressed cells");
            expect(std::none_of(suppressed.begin(), suppressed.end(), [](auto c) { return c[2] == 0 || c[1] == 0; }),
                   "enemy name or unrelated HUD tile was suppressed");
            expect(element.x < 163 && element.w > 120, "canvas clipped to native gauge region");
            expect((element.slot & (KES_PICTURE | KES_FINE)) == (KES_PICTURE | KES_FINE),
                   "missing full-resolution picture flags");
            expect((element.slot & KES_HUD) != 0, "enemy gauge does not follow single-screen HUD visibility");
            KhdaysSequenceImage commands{};
            khdays_ui_kh2_enemy_commands(&commands);
            float ax, ay;
            std::memcpy(&ax, commands.words + KSI_ANCHOR_X, 4);
            std::memcpy(&ay, commands.words + KSI_ANCHOR_Y, 4);
            expect(near(ax, 254) && near(ay, 12) && commands.words[KSI_ANCHORED],
                   "mod offsets moved native composition anchor");
            expect(cpu ? uploads == 1 : uploads == 0 && commands.words[KSI_COUNT] > 0,
                   "GPU path uploaded a CPU raster");
            for (int width : {8, 16, 32}) {
                std::vector<unsigned char> strip(std::size_t(width) * 8, 2);
                expect(khdays_ui_kh2_enemy_sprite(strip.data(), width, 8), "enemy OBJ strip escaped suppression");
                strip[0] = 3;
                expect(!khdays_ui_kh2_enemy_sprite(strip.data(), width, 8), "unrelated enemy OBJ strip removed");
            }
            const int allocated = allocations;
            khdays_ui_kh2_enemy_frame(2);
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(allocations == allocated, "steady gauge reallocates its image");
            if (!cpu) {
                expect(uploads == 0, "steady GPU gauge uploads pixels");
                auto before = commands;
                paused = true;
                khdays_ui_kh2_enemy_frame(100);
                khdays_ui_kh2_enemy_reference(0);
                khdays_ui_kh2_enemy_emit();
                khdays_ui_kh2_enemy_commands(&commands);
                expect(std::memcmp(&before, &commands, sizeof(commands)) == 0, "paused gauge advanced");
                paused = false;
                half(ctx + 0xee, 250);
                khdays_ui_kh2_enemy_frame(101);
                khdays_ui_kh2_enemy_reference(0);
                khdays_ui_kh2_enemy_emit();
                khdays_ui_kh2_enemy_commands(&commands);
                expect(component(commands, 41) == 1 && component(commands, 46) == 1,
                       "live rollover did not start bar and tank damage");
                half(ctx + 0xee, 200);
                khdays_ui_kh2_enemy_frame(102);
                khdays_ui_kh2_enemy_reference(0);
                khdays_ui_kh2_enemy_emit();
                khdays_ui_kh2_enemy_commands(&commands);
                expect(component(commands, 41) == 1 && component(commands, 46) == 0,
                       "successive damage retained an already depleted tank");
                khdays_ui_kh2_enemy_frame(202);
                khdays_ui_kh2_enemy_reference(0);
                khdays_ui_kh2_enemy_emit();
                khdays_ui_kh2_enemy_commands(&commands);
                expect(component(commands, 41) == 0, "live damage animation did not end");
                half(ctx + 0xee, 300);
                khdays_ui_kh2_enemy_frame(203);
                khdays_ui_kh2_enemy_reference(0);
                khdays_ui_kh2_enemy_emit();
                khdays_ui_kh2_enemy_commands(&commands);
                expect(component(commands, 41) == 0 && component(commands, 44) == 1,
                       "healing failed to restore green tanks without red damage");
                half(ctx + 0xee, 350);
                half(ctx + 0x12e, 64);
                half(ctx + 0x14a, 64);
                khdays_ui_kh2_enemy_frame(204);
                khdays_ui_kh2_enemy_reference(0);
                khdays_ui_kh2_enemy_emit();
                khdays_ui_kh2_enemy_commands(&commands);
                expect(component(commands, 41) == 0, "fresh native target inherited a damage trail");
            }
            source = false;
            emitted = 0;
            suppressed.clear();
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(emitted == 0 && suppressed.empty(), "unrelated tile origin was replaced");
            source = true;
            shown = false;
            emitted = 0;
            suppressed.clear();
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(emitted == 0 && suppressed.empty(), "hidden native HUD still draws replacement");
            word(ctx + 0x48, 0);
            half(ctx + 0xee, 0);
            shown = true;
            khdays_ui_kh2_enemy_frame(205);
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(emitted == 1 && suppressed.size() == 10,
                   "target teardown released native gauge ownership before its visual transition ended");
            khdays_ui_kh2_enemy_commands(&commands);
            expect(cpu || component(commands, 43) == 0, "boss death retained green HP during the closing transition");
            native_x = 4;
            native_y = 8;
            emitted = 0;
            suppressed.clear();
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(emitted == 1 && suppressed.size() == 10, "moved enemy gauge escaped the old top-right bounds");
            int preferences[KHDAYS_OPT_COUNT];
            khdays_hud_settings_default(preferences);
            preferences[KHDAYS_OPT_ENEMY_HP] = 0;
            khdays_hud_settings_publish(preferences);
            khdays_ui_kh2_enemy_frame(206);
            emitted = 0;
            suppressed.clear();
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(emitted == 0 && suppressed.empty(), "live enemy off retained suppression");
            preferences[KHDAYS_OPT_ENEMY_HP] = 1;
            khdays_hud_settings_publish(preferences);
            khdays_ui_kh2_enemy_frame(207);
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(emitted == 1 && suppressed.size() == 10, "enemy gauge required a reload after enabling");
            hd_graphics = false;
            khdays_ui_kh2_enemy_frame(208);
            emitted = 0;
            suppressed.clear();
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(!khdays_ui_kh2_enemy_ready() && emitted == 0 && suppressed.empty(),
                   "HD graphics off retained enemy drawing or suppression");
            hd_graphics = true;
            khdays_ui_kh2_enemy_frame(209);
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(khdays_ui_kh2_enemy_ready() && emitted == 1 && suppressed.size() == 10,
                   "HD graphics on required reloading the enemy gauge");
            enemy_cells = false;
            emitted = 0;
            suppressed.clear();
            khdays_ui_kh2_enemy_reference(cpu);
            khdays_ui_kh2_enemy_emit();
            expect(emitted == 0 && suppressed.empty(), "removed native gauge still draws replacement");
        }
        std::cout << "KH2 enemy gauge tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(folder);
    return result;
}
