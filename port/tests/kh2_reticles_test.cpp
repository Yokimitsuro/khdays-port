// Selection states, GPU-only images, retained lists, and exact native fallback boundaries.
#include "../host/app/hud_settings.h"
#include "../ui/kh2_reticles.h"
#include "../ui/kh2_hp_config.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "../runtime/mods/textures.h"
}
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
namespace seq = khdays::assets::kh2;
std::filesystem::path settings;
std::vector<unsigned char> ram(0x02400000);
bool hd = true, gpu = true, paused = false, missing = false;
bool marker_scope = false;
int slot = 0, scale = 4, allocations = 0, atlas_loads = 0;
unsigned current_frame = 10;
constexpr unsigned sprite = 0x02100000;
std::vector<KhdaysG3dPolygon> polygons;
std::array<KhdaysG3dClipVertex, 4> last_vertices;
std::array<std::array<KhdaysG3dClipVertex, 4>, 5> marker_vertices;
std::array<int, 3> target_world{128 * 4096, 96 * 4096, 4 * 4096};
int camera_x = 0, camera_y = 0;
bool projected_visible = true;
KhdaysSequenceImage commands[5];
void expect(bool test, const char *why)
{
    if (!test)
        throw std::runtime_error(why);
}
bool near(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}
seq::SequenceData fixture()
{
    seq::SequenceData d;
    for (unsigned i = 0; i < 51; ++i) {
        seq::SequencePart p;
        p.uv = {int(i), 0, int(i + 1), 1};
        p.colors.fill(0x80808080);
        d.parts.push_back(p);
        d.groups.push_back({{-16, -16, 16, 16}, i});
        d.sprites.push_back({static_cast<unsigned short>(i), 1});
        seq::SequenceAnimation a;
        a.flags = 0x77ff7 & ~(1u << 12);
        a.sprite = i;
        a.time = {0, 9};
        a.x = {0, 9};
        a.scale = a.scale_x = a.scale_y = {1, 1};
        a.colors.fill(0x80808080);
        d.animations.push_back(a);
        seq::SequenceClip c;
        c.animations = {static_cast<unsigned short>(i), 1};
        c.restart = 3;
        c.loop = i == 4 || i == 26 || i == 49 ? 1 : 0;
        d.clips.push_back(c);
    }
    return d;
}
void begin(unsigned delta = 2)
{
    current_frame += delta;
    khdays_reticles_frame(current_frame);
    polygons.clear();
    khdays_reticles_begin();
    // The native hook calls Ov022_ProjectToScreen before any early target-selection return.
    for (int kind = 0; kind < 5; ++kind) {
        int world[3];
        if (khdays_reticles_saved_world(kind, world))
            khdays_reticles_projected(kind, projected_visible, world[0] + camera_x, world[1] + camera_y);
    }
}
int mark(int kind, unsigned identity = 1)
{
    khdays_reticles_target(target_world.data());
    return khdays_reticles_mark(kind, identity, sprite, target_world[0] + camera_x, target_world[1] + camera_y);
}
void finish(int cancel = 0)
{
    khdays_reticles_end(cancel);
    expect(!marker_scope, "marker scope leaked into unrelated geometry");
    KhdaysG3dList list{};
    list.npolys = int(polygons.size());
    list.polys = polygons.data();
    khdays_reticles_commands(&list, commands);
}
int clip(int kind)
{
    const auto &p = commands[kind];
    if (!p.words[KSI_COUNT])
        return -1;
    float uv;
    std::memcpy(&uv, p.words + KSI_HEADER + 2, 4);
    return int(uv);
}
void configs()
{
    const auto c = khdays::ui::kh2::load_reticle_configs(settings);
    expect(c[0].animation == 5 && c[0].end_animation == 4 && c[1].animation == 27 && c[1].end_animation == 26 &&
               c[2].animation == 50 && c[2].end_animation == 49,
           "wrong default animation mapping");
    expect(c[0].enabled && !c[3].enabled && !c[4].enabled, "optional marker defaults");
    expect(near(c[0].scale, 0.65f) && near(c[2].scale, 0.5f) && near(c[2].offset_y, -3), "per-marker settings");
}
void playback()
{
    begin();
    expect(mark(0) == 1, "soft-lock not replaced");
    expect(!mark(3) && !mark(4) && !mark(-1) && !mark(5), "replaced an unrequested native marker");
    finish();
    expect(clip(0) == 5 && clip(1) < 0, "wrong soft clip");
    expect(atlas_loads == 1 && allocations == 1,
           "fiel must share a single atlas without allocating intermediate images");
    expect(near(last_vertices[0].w, 1024) && near(last_vertices[0].z, -1024), "native orthographic depth");
    const int cached_allocations = allocations;
    begin();
    mark(0);
    finish();
    expect(allocations == cached_allocations, "allocated pixels on a warm frame");
    const auto saved = commands[0];
    const auto saved_polygons = polygons;
    paused = true;
    begin();
    mark(0);
    finish();
    expect(!std::memcmp(&saved, &commands[0], sizeof(saved)), "paused animation advanced");
    paused = false;
    slot = 1;
    begin();
    mark(1);
    finish();
    expect(clip(0) == 4 && clip(1) == 27, "soft-to-hard must play the soft exit and hard intro");
    KhdaysG3dList held{};
    held.npolys = int(saved_polygons.size());
    held.polys = const_cast<KhdaysG3dPolygon *>(saved_polygons.data());
    KhdaysSequenceImage retained[5];
    khdays_reticles_commands(&held, retained);
    expect(!std::memcmp(&saved, &retained[0], sizeof(saved)), "building a new list overwrote the shown list's image");
    for (int i = 0; i < 6; ++i) {
        begin();
        mark(1);
        finish();
    }
    expect(clip(0) < 0 && clip(1) == 27, "end clip should stop; active clip should loop");
    begin();
    mark(1, 2);
    finish();
    float x;
    std::memcpy(&x, commands[1].words + KSI_HEADER, 4);
    expect(near(x, -15.5f), "new target did not restart its animation");
    begin();
    mark(2);
    finish();
    expect(clip(1) == 26 && clip(2) == 50, "interact transition");
    begin();
    finish(1);
    expect(polygons.empty(), "suppressed native UI must cancel rather than play an exit above it");
    begin();
    finish();
    expect(polygons.empty(), "cancelled marker survived");
    hd = false;
    begin();
    expect(!mark(0), "HD off must preserve native PNG/DS path");
    finish();
    hd = true;
    gpu = false;
    begin();
    expect(!mark(0), "reference-only renderer must preserve DS marker");
    finish();
    gpu = true;
    begin();
    mark(0);
    finish();
    begin(60);
    finish();
    expect(polygons.empty(), "reticle survived a scene/pause gap");
}
void anchors()
{
    begin();
    mark(0);
    finish();
    const auto original = marker_vertices[0][0];
    const auto original_world = target_world;
    int saved[3];
    expect(!khdays_reticles_saved_world(0, saved), "saved anchor accessible outside the selection update");

    // Emulate a target being removed/reused and a different target appearing at another position.
    target_world = {180 * 4096, 64 * 4096, 8 * 4096};
    camera_x = -24 * 4096;
    camera_y = 12 * 4096;
    begin();
    expect(khdays_reticles_saved_world(0, saved) && std::equal(saved, saved + 3, original_world.begin()),
           "world anchor retained target memory instead of copying all three coordinates");
    expect(!khdays_reticles_saved_world(-1, saved) && !khdays_reticles_saved_world(5, saved) &&
               !khdays_reticles_saved_world(0, nullptr),
           "invalid saved-anchor request");
    mark(1, 2);
    finish();
    expect(clip(0) == 4 && clip(1) == 27, "target switch did not retain the old exit");
    expect(near(marker_vertices[0][0].x - original.x, -24.0f / 128 * 1024) &&
               near(marker_vertices[0][0].y - original.y, -12.0f / 96 * 1024),
           "exit did not move with the reprojected world anchor");

    projected_visible = false; // Native projector returns -1 for off-screen or behind-camera points.
    begin();
    expect(khdays_reticles_saved_world(0, saved) && std::equal(saved, saved + 3, original_world.begin()),
           "new target overwrote the exiting reticle's world anchor");
    finish();
    expect(polygons.empty(), "invisible world anchors rendered at stale screen positions");
    camera_x = 16 * 4096;
    camera_y = -6 * 4096;
    projected_visible = true;
    paused = true;
    begin();
    finish();
    expect(clip(0) == 4 && near(marker_vertices[0][0].x - original.x, 16.0f / 128 * 1024),
           "paused exit stopped following camera reprojection");
    float x;
    std::memcpy(&x, commands[0].words + KSI_HEADER, 4);
    expect(near(x, -13.5f), "hidden exit did not advance, or pause advanced its timeline");
    paused = false;
    begin();
    mark(0, 3);
    finish();
    expect(clip(0) == 5, "reacquiring a target did not restart the intro");
    begin();
    finish(1);
    begin();
    expect(!khdays_reticles_saved_world(0, saved), "cancelled marker retained an active world anchor");
    finish();

    begin();
    mark(2);
    finish();
    projected_visible = false;
    for (int i = 0; i < 7; ++i) {
        begin();
        finish();
    }
    projected_visible = true;
    begin();
    expect(!khdays_reticles_saved_world(2, saved), "off-screen exit failed to finish its timeline");
    finish();
    expect(polygons.empty(), "expired exit reappeared after returning on-screen");
    target_world = original_world;
    camera_x = camera_y = 0;
}
} // namespace
namespace khdays::render::kh2 {
SequenceAsset load_sequence_asset(const std::filesystem::path &, const std::string &entry,
                                  const std::filesystem::path &texture)
{
    ++atlas_loads;
    expect(entry == "fiel", "wrong BAR entry");
    if (missing || texture.filename() == "missing.dds")
        throw std::runtime_error("missing private texture");
    expect(texture.filename() == "US_zz0field_2dd0.png", "wrong remastered atlas");
    SequenceAsset a;
    a.sequence = fixture();
    a.logical_size = {256, 256};
    a.texture.width = a.texture.height = 1;
    a.texture.rgba = {255, 255, 255, 255};
    return a;
}
} // namespace khdays::render::kh2
extern "C" {
unsigned char *khdays_window = ram.data();
int khdays_mods_find(const char *, char *out, int size)
{
    std::snprintf(out, std::size_t(size), "%s", settings.string().c_str());
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
int khdays_host_frame_scale(void)
{
    return scale;
}
unsigned khdays_link_scene(unsigned *)
{
    return 2;
}
int khdays_link_paused(void)
{
    return paused;
}
int khdays_g3d_build_slot(void)
{
    return slot;
}
void khdays_g3d_set_marker(int on)
{
    marker_scope = on != 0;
}
int khdays_textures_add_info(const char *, int, int, const std::uint32_t *, std::uint32_t)
{
    return allocations++;
}
int khdays_textures_add_changing(const char *, int, int, std::uint32_t)
{
    return allocations++;
}
void khdays_g3d_sequence_polygon(const KhdaysG3dClipVertex *v, int n, unsigned attr, int atlas, unsigned program)
{
    expect(n == 4 && program < 20, "invalid 3D marker submission");
    expect(marker_scope, "sequence marker was not submitted in the foreground stage");
    std::copy(v, v + 4, last_vertices.begin());
    marker_vertices[program / 4] = last_vertices;
    KhdaysG3dPolygon p{};
    p.flags = KHDAYS_G3D_MOD | KHDAYS_G3D_SEQUENCE | KHDAYS_G3D_MARKER;
    p.replacement = atlas;
    p.pltt = program;
    p.attr = attr;
    polygons.push_back(p);
}
}
int main(int argc, char **argv)
{
    try {
        missing = argc > 1 && std::string(argv[1]) == "--missing";
        const bool optional = argc > 1 && std::string(argv[1]) == "--optional";
        const bool partial = argc > 1 && std::string(argv[1]) == "--partial";
        const bool legacy = argc > 1 && std::string(argv[1]) == "--legacy";
        settings = std::filesystem::temp_directory_path() /
                   ("khdays-reticle-test" + std::string(argc > 1 ? argv[1] : "") + ".json");
        std::ofstream file(settings);
        if (optional)
            file
                << R"({"reticles":{"enabled":true,"chain":{"enabled":true,"animation":5,"end_animation":4},"error":{"enabled":true,"animation":27,"end_animation":26}}})";
        else if (partial)
            file << R"({"reticles":{"enabled":true,"soft_lock":{"texture":"missing.dds"}}})";
        else if (legacy)
            file << "{}";
        else
            file << R"({"reticles":{"enabled":true,"interact":{"scale":0.5,"offset_y":-3}}})";
        file.close();
        ram[sprite + 0x24] = 61;
        ram[sprite + 0x2a] = 31;
        if (optional) {
            begin();
            expect(mark(3) && mark(4), "configured chain/error was not replaced");
            finish();
            expect(clip(3) == 5 && clip(4) == 27, "optional animation mapping");
            begin();
            finish();
            expect(clip(3) == 4 && clip(4) == 26, "optional exit animation mapping");
        } else if (partial) {
            begin();
            expect(!mark(0) && mark(1) && mark(2), "one missing resource disabled unrelated reticles");
            finish();
            expect(clip(0) < 0 && clip(1) == 27 && clip(2) == 50, "partial resource fallback");
        } else if (missing || legacy) {
            begin();
            expect(!mark(0) && !mark(1) && !mark(2), "missing KH2 file must leave all native draws intact");
            expect(marker_scope, "PNG/DS fallback was not marked for the foreground stage");
            finish();
            expect(polygons.empty(), "missing-file replacement rendered");
        } else {
            configs();
            playback();
            anchors();
        }
        if (!missing && !legacy && !partial) {
            int preferences[KHDAYS_OPT_COUNT];
            khdays_hud_settings_default(preferences);
            preferences[KHDAYS_OPT_RETICLES] = 0;
            khdays_hud_settings_publish(preferences);
            begin();
            for (int kind = 0; kind < 5; ++kind)
                expect(!mark(kind), "disabled reticle suppressed its native fallback");
            finish();
            expect(polygons.empty(), "live reticle off retained replacement polygons");
            preferences[KHDAYS_OPT_RETICLES] = 1;
            khdays_hud_settings_publish(preferences);
            begin();
            expect(mark(optional ? 3 : 0), "reticle could not be enabled without a reload");
            finish();
        }
        std::filesystem::remove(settings);
        std::cout << "KH2 reticle states, world anchors, retained GPU images and native fallback passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
