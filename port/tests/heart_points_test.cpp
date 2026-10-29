// Counter event playback must outlive native visual clips, with exact fallback boundaries.
#include "../ui/heart_points.h"
#include "../host/render/kh2_sequence_asset.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
namespace seq = khdays::assets::kh2;
std::filesystem::path settings;
std::array<unsigned char, 0x1474> memory{};
std::vector<KhdaysG3dPolygon> polygons;
bool hd = true, gpu = true, paused = false, missing = false;
bool looping_pickup = false;
bool final_tick = false;
bool native_chain = false;
unsigned frame = 10;
int loads = 0;
void expect(bool value, const char *why)
{
    if (!value)
        throw std::runtime_error(why);
}
void put(int at, int value)
{
    std::memcpy(memory.data() + at, &value, 4);
}
void begin()
{
    polygons.clear();
    khdays_heart_begin(memory.data());
}
void body()
{
    khdays_heart_node(memory.data() + 0xe8);
    khdays_heart_node(memory.data() + 0x8cc);
}
unsigned commands()
{
    KhdaysG3dList list{};
    list.npolys = int(polygons.size());
    list.polys = polygons.data();
    std::array<KhdaysSequenceImage, KHDAYS_HEART_PROGRAMS> out;
    khdays_heart_commands(&list, out.data());
    return out[0].words[KSI_COUNT];
}
float quad_y(unsigned quad)
{
    KhdaysG3dList list{};
    list.npolys = int(polygons.size());
    list.polys = polygons.data();
    std::array<KhdaysSequenceImage, KHDAYS_HEART_PROGRAMS> out;
    khdays_heart_commands(&list, out.data());
    float y;
    std::memcpy(&y, out[0].words + KSI_HEADER + quad * KSI_QUAD_WORDS + 1, 4);
    return y;
}
} // namespace
namespace khdays::render::kh2 {
SequenceAsset load_sequence_asset(const std::filesystem::path &, const std::string &, const std::filesystem::path &)
{
    ++loads;
    if (missing)
        throw std::runtime_error("missing synthetic counter");
    SequenceAsset a;
    a.texture.width = a.texture.height = 1;
    a.texture.rgba = {255, 255, 255, 255};
    a.logical_size = {1, 1};
    auto &d = a.sequence;
    d.parts.push_back({0, {0, 0, 1, 1}, {0, 0}, {0x80808080, 0x80808080, 0x80808080, 0x80808080}});
    d.groups.push_back({{-8, -8, 8, 8}, 0});
    d.sprites.push_back({0, 1});
    d.sprites.push_back({0, 0});
    for (unsigned i = 0; i < (native_chain ? 43u : final_tick ? 42u : 41u); ++i) {
        seq::SequenceAnimation anim;
        anim.flags = 1 | 2 | 16 | 32 | 64;
        anim.sprite = native_chain && i == 10 ? 1 : 0;
        anim.time = {0, 19};
        anim.x = {0, 19};
        anim.y = {int(i), int(i)};
        anim.scale = anim.scale_x = anim.scale_y = {1, 1};
        anim.colors.fill(0x80808080);
        d.animations.push_back(anim);
        seq::SequenceClip c;
        c.animations = {static_cast<unsigned short>(i), 1};
        c.loop = i == 0 || i == 1 || i == 3 || i == 9 || (i == 2 && looping_pickup) || (i >= 12 && i <= 33) ? 0 : 1;
        d.clips.push_back(c);
    }
    return a;
}
} // namespace khdays::render::kh2
extern "C" {
int khdays_mods_find(const char *, char *out, int n)
{
    std::snprintf(out, std::size_t(n), "%s", settings.string().c_str());
    return 1;
}
int khdays_settings_game_language(void)
{
    return 1;
}
const char *khdays_language_code(int)
{
    return "en";
}
void khdays_log(const char *, ...) {}
int khdays_textures_hd(void)
{
    return hd;
}
int khdays_host_frame_wants(void)
{
    return gpu ? 1 : 0;
}
unsigned khdays_link_scene(unsigned *)
{
    return 2;
}
unsigned khdays_link_frame(void)
{
    return frame;
}
int khdays_link_paused(void)
{
    return paused;
}
int khdays_g3d_build_slot(void)
{
    return 0;
}
int khdays_textures_add_info(const char *, int, int, const unsigned *, unsigned)
{
    return 0;
}
void khdays_g3d_sequence_polygon(const KhdaysG3dClipVertex *vertices, int count, unsigned attr, int atlas,
                                 unsigned token)
{
    expect(count == 4 && token >= 20 && token < 36, "invalid counter GPU submission");
    expect(vertices[0].w == 1, "counter must remain near the camera in W-buffer mode");
    expect(vertices[0].y > vertices[2].y && vertices[0].x < vertices[2].x,
           "counter submission must retain both dimensions in optimized builds");
    KhdaysG3dPolygon p{};
    p.flags = KHDAYS_G3D_MOD | KHDAYS_G3D_SEQUENCE | KHDAYS_G3D_HUD;
    p.pltt = token;
    p.replacement = atlas;
    p.attr = attr;
    polygons.push_back(p);
}
}
int main(int argc, char **argv)
{
    try {
        missing = argc > 1 && std::string(argv[1]) == "--missing";
        looping_pickup = argc > 1 && std::string(argv[1]) == "--pickup-loop";
        final_tick = argc > 1 && std::string(argv[1]) == "--final-tick";
        native_chain = argc > 1 && std::string(argv[1]) == "--native-chain";
        const bool disabled = argc > 1 && std::string(argv[1]) == "--disabled";
        settings = std::filesystem::temp_directory_path() /
                   ("khdays-hearts-test" + std::string(argc > 1 ? argv[1] : "") + ".json");
        {
            std::ofstream f(settings);
            f << (disabled       ? "{}"
                  : native_chain ? R"({"heart_points":{"enabled":true,"animations":{"chain_warning":42}}})"
                  : final_tick   ? R"({"heart_points":{"enabled":true,"animations":{"total_gain_final":41}}})"
                                 : R"({"heart_points":{"enabled":true}})");
        }
        put(0xd60, 123);
        begin();
        if (missing || disabled) {
            expect(!khdays_heart_node(memory.data() + 0xe8), "fallback hid native heart");
            khdays_heart_end();
            expect(polygons.empty(), "fallback emitted replacement");
        } else if (native_chain) {
            body();
            khdays_heart_end();
            const auto base = commands();
            ++frame;
            begin();
            body();
            khdays_heart_node(memory.data() + 0xdc0);
            khdays_heart_end();
            ++frame;
            put(0xd0, 1);
            put(0xfe0, 1);
            begin();
            body();
            khdays_heart_node(memory.data() + 0xdc0);
            khdays_heart_end();
            expect(commands() == base + 1 && quad_y(base) == 34, "CHAIN warning did not use its separate clip");
            ++frame;
            begin();
            body();
            khdays_heart_end();
            expect(commands() == base, "native CHAIN removal left the warning drawing");
        } else if (final_tick) {
            body();
            khdays_heart_end();
            const float resting = quad_y(2);
            ++frame;
            begin();
            body();
            put(0xd58, 1);
            put(0xd64, 124);
            put(0xd60, 124);
            khdays_heart_end();
            expect(quad_y(2) == resting + 42, "last increment did not use its separate opaque pulse");
            ++frame;
            begin();
            body();
            put(0xd64, 130);
            put(0xd60, 125);
            khdays_heart_end();
            expect(quad_y(2) == resting + 38, "ordinary increment did not use its normal pulse");
        } else if (looping_pickup) {
            body();
            khdays_heart_event(0, 0);
            put(0x48, 1);
            khdays_heart_end();
            const auto base = commands();
            expect(base == 5, "pickup appeared before the native widget's next draw");
            for (int i = 0; i < 45; ++i) {
                ++frame;
                begin();
                body();
                khdays_heart_end();
                expect(commands() == base + 1, "pickup stopped while native count-up still held it open");
            }
            put(0x48, 0);
            ++frame;
            begin();
            body();
            khdays_heart_end();
            expect(commands() == base + 1, "native completion truncated the replacement's current pickup cycle");
            for (int i = 0; i < 20; ++i) {
                ++frame;
                begin();
                body();
                khdays_heart_end();
            }
            expect(commands() == base, "pickup loop never finished after the native hold ended");
        } else {
            expect(khdays_heart_node(memory.data() + 0xe8), "counter was not replaced");
            expect(!khdays_heart_node(memory.data() + 0x1214) && !khdays_heart_node(memory.data() + 0x110c) &&
                       !khdays_heart_node(memory.data() + 0x610),
                   "replacement touched another HUD widget");
            body();
            khdays_heart_end();
            const auto base = commands();
            expect(base == 5, "counter body/digits wrong");
            expect(quad_y(1) == -8, "resting heart selected the active clip");
            ++frame;
            put(0xe4, 1);
            begin();
            body();
            khdays_heart_end();
            expect(quad_y(1) == -7, "active heart selected the resting clip");
            put(0xe4, 0);
            ++frame;
            begin();
            body();
            khdays_heart_event(0, 0);
            khdays_heart_end();
            expect(commands() == base, "pickup appeared before its native draw phase");
            ++frame;
            begin();
            body();
            khdays_heart_end();
            expect(commands() == base + 1, "pickup event did not start the 2DD");
            for (int i = 0; i < 15; ++i) {
                ++frame;
                begin();
                body();
                khdays_heart_end();
            }
            expect(commands() == base + 1, "native disappearance shortened replacement pickup");
            paused = true;
            frame += 2;
            begin();
            body();
            khdays_heart_end();
            expect(commands() == base + 1, "pause advanced clip");
            paused = false;
            for (int i = 0; i < 6; ++i) {
                ++frame;
                begin();
                body();
                khdays_heart_end();
            }
            expect(commands() == base, "one-shot ignored 2DD duration");
            ++frame;
            begin();
            body();
            put(0xd90, 42);
            put(0x7c, 1);
            khdays_heart_event(1, 0);
            khdays_heart_end();
            expect(commands() == base + 3, "signed delta row missing");
            frame += 10;
            begin();
            body();
            khdays_heart_node(memory.data() + 0xdc0);
            khdays_heart_end();
            expect(commands() == base + 1 && quad_y(base) == 0, "CHAIN intro did not start");
            ++frame;
            put(0xd0, 1);
            begin();
            body();
            khdays_heart_node(memory.data() + 0xdc0);
            khdays_heart_end();
            expect(commands() == base + 1 && quad_y(base) == 2,
                   "CHAIN waited for native disappearance instead of starting its fade");
            for (int i = 0; i < 20; ++i) {
                ++frame;
                begin();
                body();
                khdays_heart_node(memory.data() + 0xdc0);
                khdays_heart_end();
            }
            expect(commands() == base, "CHAIN restarted while its native fade was still ending");
            put(0xfe0, 1);
            ++frame;
            begin();
            body();
            khdays_heart_node(memory.data() + 0xdc0);
            khdays_heart_end();
            expect(commands() == base, "warning restart lost the native transparent boundary frame");
            ++frame;
            begin();
            body();
            khdays_heart_node(memory.data() + 0xdc0);
            khdays_heart_end();
            expect(commands() == base + 1, "CHAIN warning never flashed back on after its fade restarted");
            put(0xfe0, 2);
            put(0xcc, 1);
            ++frame;
            begin();
            body();
            khdays_heart_node(memory.data() + 0xdc0);
            khdays_heart_end();
            expect(commands() == base + 1, "a refreshed chain failed to restart visibly");
            put(0xcc, 0);
            put(0xd0, 0);
            hd = false;
            ++frame;
            begin();
            expect(!khdays_heart_node(memory.data() + 0xe8), "HD off hid original");
            khdays_heart_end();
            hd = true;
            gpu = false;
            begin();
            expect(!khdays_heart_node(memory.data() + 0xe8), "reference path hid original");
            khdays_heart_end();
            expect(loads == 1, "asset reloaded during gameplay");
        }
        std::filesystem::remove(settings);
        std::cout << "heart counter passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
