// Synthetic runtime adapter checks: HP, pause, selective suppression, and the face over the gauge.
#include "../host/app/hud_settings.h"
#include "../ui/kh2_hp_hud.h"
#include "../ui/kh2_hp_config.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "../ui/parts.h"
#include "../runtime/scripts/game_link.h"
}
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>

namespace {
bool hd_graphics = true;
std::vector<unsigned char> memory(0x02100000);
int hp = 186, maximum = 186, paused, changed, scene = 2, slot, hud_mode = KHDAYS_UI_ON;
khdays::assets::kh2::SequenceAnimation *parent_animation = nullptr;
int limit_hp = 62;
KhdaysLinkLimitClock limit_clock{};
int native_w, native_h;
int rgba_reads, uploads, test_scale = 1;
bool gauge_origin = true;
std::vector<std::array<int, 3>> suppressed;
std::vector<std::uint32_t> image;
KhdaysUiElement emitted{}, box{};
std::filesystem::path config_path;
void expect(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void word(unsigned at, unsigned v)
{
    std::memcpy(memory.data() + at, &v, 4);
}
} // namespace
extern "C" {
int khdays_textures_hd(void)
{
    return hd_graphics;
}
int khdays_settings_game_language(void)
{
    return 2;
}
const char *khdays_language_code(int language)
{
    return language == 2 ? "fr" : "en";
}
int khdays_mods_find(const char *name, char *out, int size)
{
    if (std::string(name) != "layouts/gauge/en/gauge.json")
        return 0;
    const auto path = config_path.string();
    if (int(path.size()) >= size)
        return 0;
    std::memcpy(out, path.c_str(), path.size() + 1);
    return 1;
}
unsigned char *khdays_window = memory.data();
int khdays_bg_text_layer(int screen, int bg, KhdaysBgLayer *layer)
{
    if (screen || bg != 3)
        return 0;
    *layer = {0, 0, 32, 32, 0, 0, 0, 3};
    return 1;
}
unsigned khdays_bg_map_entry(int, const KhdaysBgLayer *, int x, int y)
{
    return x == 2 && y == 3 ? 64 : 0;
}
const char *khdays_bg_origin(int, unsigned)
{
    return gauge_origin ? "UI/btl/main.p2#4" : "UI/unrelated";
}
void khdays_ui_left_out_cell(int, int screen, int bg, int x, int y)
{
    expect(screen == 0, "gauge suppression changed engine");
    suppressed.push_back({bg, x, y});
}
int khdays_ui_kh2_party_parent(int, KhdaysHudParent *)
{
    return 0;
}
int khdays_ui_kh2_party_ready(int)
{
    return 0;
}
int khdays_overlay_loaded(int)
{
    return scene == 2;
}
unsigned khdays_link_scene(unsigned *)
{
    return unsigned(scene);
}
int khdays_link_player_hp(int *out, int *max)
{
    *out = hp;
    *max = maximum;
    return 1;
}
int khdays_link_player_low_hp(void)
{
    return hp <= maximum / 4;
}
int khdays_link_player_limit_hp(void)
{
    return limit_hp;
}
int khdays_link_player_limit_clock(KhdaysLinkLimitClock *out)
{
    *out = limit_clock;
    return 1;
}
int khdays_link_paused(void)
{
    return paused;
}
unsigned Session_GetLocalPlayerIndex(void)
{
    return 0;
}
int Ov002_GetPanelWord0220Idx(int)
{
    return slot;
}
int khdays_ui_mode(int)
{
    return hud_mode;
}
void khdays_ui_changed(void)
{
    ++changed;
}
void khdays_log(const char *, ...) {}
int khdays_ui_scale(void)
{
    return test_scale;
}
void khdays_ui_image_put(int) {}
int khdays_ui_image_get(int w, int h)
{
    native_w = w;
    native_h = h;
    image.assign(std::size_t(w * h), 0);
    return 1;
}
uint32_t *khdays_ui_image_begin(int)
{
    return image.data();
}
void khdays_ui_image_end(int)
{
    ++uploads;
}
int khdays_ui_image_replacement(int i)
{
    return i;
}
int khdays_ui_element_direct(const KhdaysUiElement *)
{
    return 2;
}
int khdays_textures_add_info(const char *, int, int, const unsigned *, unsigned)
{
    return 3;
}
uint32_t khdays_ui_element_rgba(const KhdaysUiElement *, int x, int y)
{
    ++rgba_reads;
    return x == 70 && y == 24 ? 0xff123456 : 0;
}
void khdays_ui_emit_extended(int, const KhdaysUiElement *e, const KhdaysUiElement *b)
{
    emitted = *e;
    box = *b;
}
}

namespace khdays::render::kh2 {
SequenceAsset load_sequence_asset(const std::filesystem::path &, const std::string &, const std::filesystem::path &)
{
    using namespace assets::kh2;
    SequenceAsset out;
    out.logical_size = {1, 1};
    out.texture.width = out.texture.height = 1;
    out.texture.rgba = {0, 255, 0, 255};
    auto &d = out.sequence;
    SequencePart p;
    p.colors.fill(0x80808080);
    p.uv = {0, 0, 1, 1};
    d.parts.push_back(p);
    d.groups.push_back({{-40, -40, 40, 40}, 0});
    d.sprites = {{0, 1}, {0, 0}};
    SequenceAnimation a;
    a.flags = 1 | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 12) | (1u << 14);
    a.time = {0, 200};
    a.colors.fill(0x80808080);
    d.animations.push_back(a);
    a.flags |= 2;
    a.sprite = 1;
    a.flags &= ~(1u << 12);
    a.x = {455, 463};
    a.y = {355, 347};
    d.animations.push_back(a);
    parent_animation = &d.animations.back();
    d.clips.resize(160);
    for (auto &c : d.clips)
        c.animations = {0, 1};
    for (int i = 132; i < 138; ++i)
        d.clips[i].animations = {1, 1};
    return out;
}
} // namespace khdays::render::kh2

int main()
{
    try {
        config_path =
            std::filesystem::temp_directory_path() /
            ("khdays-gauge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        const auto write_config = [](const char *text) { std::ofstream(config_path) << text; };
        write_config(R"({"offset_x":-12.5,"center_normal":138,"bar_fill":124})");
        const auto custom = khdays::ui::kh2::load_hp_gauge_config(config_path);
        expect(custom.offset_x == -12.5f && custom.style.center[0] == 138 && custom.style.bar_fill == 124,
               "custom JSON mappings ignored");
        for (const auto *bad : {R"({"scale":0})", R"({"center_normal":1.5})", R"({"offset_x":0,"offset_x":1})",
                                R"({"archive":"../other.2dd"})", R"({"unknown":1})", R"({"enabled":1})",
                                R"({"scale":01})", R"({"scale":1}garbage)"}) {
            write_config(bad);
            bool rejected = false;
            try {
                khdays::ui::kh2::load_hp_gauge_config(config_path);
            } catch (const std::exception &) {
                rejected = true;
            }
            expect(rejected, "bad gauge JSON accepted");
        }
        write_config(
            R"({"canvas_padding":0,"source_origin_x":455,"source_origin_y":355,"limit_circle":157,"limit_bar":158,"limit_marker":159})");
        _putenv_s("KHDAYS_KH2_HUD", "");
        _putenv_s("KHDAYS_KH2_ASSET_DIR", "");
        word(0x0207f618, 0x02080000);
        word(0x02080000, 0x02081000);
        std::fill_n(memory.begin() + 0x02081000, 320, static_cast<unsigned char>(0x11));
        khdays_ui_kh2_hp_frame(100);
        std::filesystem::remove(config_path);
        expect(khdays_ui_kh2_hp_ready(), "mod gauge did not auto-enable without launch variables");
        KhdaysHudParent parent{};
        expect(khdays_ui_kh2_hp_parent(0, &parent) && parent.x == 230 && parent.y == 160,
               "portrait anchor does not match the normalized black center");
        expect(!khdays_ui_kh2_hp_parent(1, &parent), "player gauge claimed a party portrait parent");
        KhdaysBgLayer layer{};
        expect(khdays_ui_kh2_hp_cell(0, 3, &layer, 64), "local HP tile not removed");
        expect(!khdays_ui_kh2_hp_cell(0, 3, &layer, 63) && !khdays_ui_kh2_hp_cell(0, 3, &layer, 74),
               "unrelated tile removed");
        expect(!khdays_ui_kh2_hp_cell(1, 3, &layer, 64) && !khdays_ui_kh2_hp_cell(0, 2, &layer, 64),
               "unrelated layer removed");
        khdays_ui_kh2_hp_prepare();
        expect(suppressed == std::vector<std::array<int, 3>>{{3, 2, 3}},
               "moved gauge tile escaped suppression outside the portrait region");
        suppressed.clear();
        gauge_origin = false;
        khdays_ui_kh2_hp_prepare();
        expect(suppressed.empty(), "gauge suppression removed an unrelated file's tile");
        gauge_origin = true;
        for (int width : {8, 16, 32, 64}) {
            std::vector<unsigned char> strip(std::size_t(width) * 8u, 1);
            expect(khdays_ui_kh2_hp_sprite(strip.data(), width, 8), "temporary gauge strip escaped suppression");
            strip[0] = 2;
            expect(!khdays_ui_kh2_hp_sprite(strip.data(), width, 8), "unrelated sprite strip removed");
        }
        std::array<unsigned char, 128> pixels;
        pixels.fill(1);
        expect(khdays_ui_kh2_hp_sprite(pixels.data(), 16, 8), "live gauge OBJ copy not recognized");
        pixels[0] = 2;
        expect(!khdays_ui_kh2_hp_sprite(pixels.data(), 16, 8), "unrelated OBJ removed");
        KhdaysUiElement face{"hud.portrait", 0, 3, 160, 136, 96, 56, 0};
        expect(khdays_ui_kh2_hp_compose(&face, nullptr), "native gauge image missing");
        const int px = 230 - int(emitted.x), py = 160 - int(emitted.y);
        expect(image[std::size_t(py * native_w + px)] == 0xff123456, "gauge covered the face artwork");
        expect(box.x == face.x && box.w == face.w, "gauge changed the portrait's layout box");
        expect(emitted.x + emitted.w == 272, "circle still cropped at the portrait's right edge");
        expect((emitted.slot & KES_PICTURE) != 0, "gauge still confined to a screen crop");
        expect((emitted.slot & KES_HUD) != 0, "gauge does not follow single-screen HUD visibility");
        paused = 1;
        changed = 0;
        khdays_ui_kh2_hp_frame(110);
        expect(changed == 0, "pause advanced the gauge");
        limit_hp = 93;
        khdays_ui_kh2_hp_frame(110);
        expect(changed != 0, "changed vanilla limit threshold did not refresh the paused HUD");
        hp = 31;
        khdays_ui_kh2_hp_frame(111);
        expect(changed != 0, "HP changes were lost while paused");
        maximum = 65535;
        hp = maximum;
        khdays_ui_kh2_hp_frame(112);
        expect(khdays_ui_kh2_hp_compose(&face, nullptr) && native_w == 1024 && native_h == 64,
               "long gauge allocation unbounded");
        slot = -1;
        khdays_ui_kh2_hp_frame(113);
        expect(!khdays_ui_kh2_hp_ready(), "missing party slot retained a stale gauge");
        slot = 0;
        khdays_ui_kh2_hp_frame(114);
        expect(khdays_ui_kh2_hp_ready(), "gauge did not recover");
        hud_mode = KHDAYS_UI_OFF;
        khdays_ui_kh2_hp_frame(114);
        expect(!khdays_ui_kh2_hp_ready(), "disabled HUD retained a live replacement");
        hud_mode = KHDAYS_UI_ON;
        scene = 1;
        khdays_ui_kh2_hp_frame(115);
        expect(!khdays_ui_kh2_hp_ready(), "gauge persisted outside the field");
        scene = 2;
        maximum = hp = 372;
        paused = 0;
        test_scale = 8;
        khdays_ui_kh2_hp_frame(116);
        expect(khdays_ui_kh2_hp_compose(&face, nullptr), "padded reference image missing");
        const int beyond_face = (256 - int(emitted.x)) * test_scale;
        expect(beyond_face < native_w, "no image storage beyond the portrait");
        expect((image[std::size_t(32 * test_scale * native_w + beyond_face)] & 0xff00u) != 0,
               "gauge pixels beyond the portrait were clipped");
        khdays_ui_kh2_hp_reference(0);
        rgba_reads = uploads = 0;
        for (int i = 0; i < 100; ++i)
            expect(khdays_ui_kh2_hp_compose(&face, nullptr), "GPU command image missing");
        KhdaysSequenceImage commands{};
        khdays_ui_kh2_hp_commands(&commands);
        expect(commands.words[KSI_COUNT] > 0 && commands.words[KSI_HEIGHT] == 512,
               "missing high-resolution GPU commands");
        float view_x, face_x;
        std::memcpy(&view_x, commands.words + KSI_VIEW_X, 4);
        std::memcpy(&face_x, commands.words + KSI_FACE_X, 4);
        expect(emitted.x + view_x / test_scale == 230 && emitted.x + face_x / test_scale == face.x,
               "right padding shifted the gauge or portrait");
        const auto limit_count = commands.words[KSI_COUNT];
        limit_hp = 0;
        khdays_ui_kh2_hp_frame(117);
        expect(khdays_ui_kh2_hp_compose(&face, nullptr), "disabled limit composition failed");
        khdays_ui_kh2_hp_commands(&commands);
        expect(commands.words[KSI_COUNT] < limit_count, "disabled vanilla limit threshold left an overlay");
        khdays_ui_kh2_hp_frame(216);
        expect(khdays_ui_kh2_hp_compose(&face, nullptr), "moving parent composition failed");
        khdays_ui_kh2_hp_commands(&commands);
        float moving_x, moving_y;
        std::memcpy(&moving_x, commands.words + KSI_FACE_X, 4);
        std::memcpy(&moving_y, commands.words + KSI_FACE_Y, 4);
        expect(std::abs(emitted.x + moving_x / test_scale - (face.x + 4 * 0.65f)) < 0.001f &&
                   std::abs(emitted.y + moving_y / test_scale - (face.y - 4 * 0.65f)) < 0.001f,
               "portrait did not follow the animated center's relative translation");
        expect(rgba_reads == 0 && uploads == 0, "live GPU path rasterized or uploaded CPU pixels");
        const auto hidden_count = commands.words[KSI_COUNT];
        paused = 1;
        word(0x02080000 + 0x2c, 1);
        word(0x02080000 + 0x112, 19);
        changed = 0;
        khdays_ui_kh2_hp_frame(216);
        expect(changed != 0 && khdays_ui_kh2_hp_compose(&face, nullptr), "ready divider did not refresh paused HUD");
        khdays_ui_kh2_hp_commands(&commands);
        expect(commands.words[KSI_COUNT] == hidden_count + 1, "ready divider missing");
        const auto saved_parent_animation = parent_animation;
        const auto data = khdays::render::kh2::load_sequence_asset({}, "", {}).sequence;
        parent_animation = saved_parent_animation;
        khdays::ui::kh2::HpGaugeStyle style;
        style.limit_marker = 159;
        const auto number = [](const KhdaysSequenceImage &p, unsigned at) {
            float out;
            std::memcpy(&out, p.words + at, 4);
            return out;
        };
        const auto expect_marker = [&](float expected_hp) {
            khdays::ui::kh2::HpGaugeSample desired;
            desired.hp = float(hp);
            desired.max_hp = desired.previous_hp = float(maximum);
            desired.limit_marker_hp = expected_hp;
            const auto expected = khdays::ui::kh2::evaluate_hp_gauge(data, desired, style);
            const auto &marker = expected.quads.back(), &cap = expected.quads[expected.quads.size() - 2];
            const auto at = KSI_HEADER + (commands.words[KSI_COUNT] - 1) * KSI_QUAD_WORDS;
            const float dx = number(commands, at - KSI_QUAD_WORDS) - cap.vertices[0].x;
            const float dy = number(commands, at - KSI_QUAD_WORDS + 1) - cap.vertices[0].y;
            for (unsigned i = 0; i < 4; ++i)
                expect(std::abs(number(commands, at + i * 5) - dx - marker.vertices[i].x) < 0.001f &&
                           std::abs(number(commands, at + i * 5 + 1) - dy - marker.vertices[i].y) < 0.001f,
                       "native divider clock/HP did not map to the exact HP edge");
            expect((commands.words[at + KSI_FLAGS] & 1u) == 0, "active divider clipped by current HP mask");
        };
        const auto update_marker = [&](float expected_hp) {
            khdays_ui_kh2_hp_frame(217);
            expect(khdays_ui_kh2_hp_compose(&face, nullptr), "divider composition failed");
            khdays_ui_kh2_hp_commands(&commands);
            expect_marker(expected_hp);
        };
        for (int max_hp : {186, 372, 744}) {
            maximum = max_hp;
            for (int current : {1, 31, 47, 62, 93, 124, 125, 185, 186, 187, 248, 371}) {
                if (current > maximum)
                    continue;
                hp = current;
                update_marker(float(hp));
            }
        }
        maximum = 372;
        hp = 62;
        word(0x02080000 + 0x178, 1);
        word(0x02080000 + 0x182, 76); // The DS pixel clamp must not constrain the native geometry.
        word(0x02080000 + 0x184, 409600);
        limit_clock = {0, 409600, 62, 4096};
        for (const auto &[elapsed, position] :
             {std::pair{0, 62.0f}, {102400, 139.5f}, {204800, 217.0f}, {409600, 372.0f}, {413696, 372.0f}}) {
            limit_clock.elapsed = elapsed;
            update_marker(position);
        }
        const auto at = KSI_HEADER + (commands.words[KSI_COUNT] - 1) * KSI_QUAD_WORDS;
        expect(std::memcmp(commands.words + at, commands.words + at - KSI_QUAD_WORDS, 20 * 4) == 0,
               "outward divider did not reach the cap's exact endpoint");
        word(0x02080000 + 0x17c, 1);
        word(0x02080000 + 0x184, 800002);
        limit_clock.outward = 400001;
        limit_clock.elapsed = 401408; // First 4096-unit tick beyond the turn.
        update_marker(372);
        limit_clock.elapsed = 600001;
        update_marker(372 + (62 - 372) * 200000.0f / 400001);
        limit_clock.elapsed = 800002;
        update_marker(62);
        hp = 31;
        update_marker(31);
        const auto latched = commands;
        paused = 0;
        update_marker(31);
        expect(std::memcmp(latched.words + at, commands.words + at, KSI_QUAD_WORDS * 4) == 0,
               "divider introduced an independent timer");
        word(0x02080000 + 0x178, 0);
        word(0x02080000 + 0x2c, 0);
        paused = 1;
        changed = 0;
        khdays_ui_kh2_hp_frame(218);
        expect(changed != 0 && khdays_ui_kh2_hp_compose(&face, nullptr), "divider did not hide with native state");
        khdays_ui_kh2_hp_commands(&commands);
        expect(commands.words[KSI_COUNT] + 1 == latched.words[KSI_COUNT], "native flags left a stale divider");
        expect(rgba_reads == 0 && uploads == 0, "divider introduced CPU pixel work");
        int preferences[KHDAYS_OPT_COUNT];
        khdays_hud_settings_default(preferences);
        preferences[KHDAYS_OPT_PARTY_HP] = 0;
        khdays_hud_settings_publish(preferences);
        khdays_ui_kh2_hp_frame(219);
        expect(!khdays_ui_kh2_hp_ready() && !khdays_ui_kh2_hp_compose(&face, nullptr),
               "live HP off retained ownership");
        preferences[KHDAYS_OPT_PARTY_HP] = 1;
        khdays_hud_settings_publish(preferences);
        khdays_ui_kh2_hp_frame(220);
        expect(khdays_ui_kh2_hp_ready() && khdays_ui_kh2_hp_compose(&face, nullptr), "live HP on required a reload");
        hd_graphics = false;
        khdays_ui_kh2_hp_frame(221);
        expect(!khdays_ui_kh2_hp_ready() && !khdays_ui_kh2_hp_compose(&face, nullptr),
               "HD graphics off retained the player gauge");
        hd_graphics = true;
        khdays_ui_kh2_hp_frame(222);
        expect(khdays_ui_kh2_hp_ready() && khdays_ui_kh2_hp_compose(&face, nullptr),
               "HD graphics on required reloading the player gauge");
        parent_animation->flags &= ~2u;
        expect(!khdays_ui_kh2_hp_parent(0, &parent) && !khdays_ui_kh2_hp_ready(),
               "invalid animated parent escaped the native fallback");
        khdays_ui_kh2_hp_commands(&commands);
        expect(!commands.words[KSI_COUNT], "failed parent left stale GPU commands");
        std::cout << "KH2 HP HUD adapter tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
