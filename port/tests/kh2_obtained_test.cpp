// Synthetic pickup playback: text follows the active track, including its fade and one-shot lifetime.
#include "../ui/kh2_obtained.h"
#include "../host/app/hud_settings.h"
#include "../ui/kh2_hp_config.h"
#include "../host/render/kh2_sequence_asset.h"
#include "khdays/assets/kh2_bar.h"
extern "C" {
#include "../ui/situation.h"
#include "../ui/parts.h"
#include "../ui/bg.h"
#include "../runtime/menus/camp.h"
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
std::filesystem::path settings;
bool missing = false, paused = false, hd = true;
int uploads = 0, scale = 2;
unsigned frame = 10;
unsigned caption_epoch = 1;
KhdaysSituationInputs context = [] {
    KhdaysSituationInputs c{};
    c.scene = 2;
    c.overlays = 1ull << 2;
    c.camp_screen = KHDAYS_CAMP_NONE;
    c.event_status = -1;
    return c;
}();
std::vector<std::vector<unsigned>> images;
KhdaysSequenceImage program{};
KhdaysUiElement submitted{}, anchor{};
std::optional<khdays::assets::kh2::SequenceData> reference_data;
std::array<unsigned char, 8192> bg_pixels = [] {
    std::array<unsigned char, 8192> p{};
    std::fill_n(p.begin() + 32, 32, static_cast<unsigned char>(0x11));
    std::fill_n(p.begin() + 0x1020, 32, static_cast<unsigned char>(0x22));
    return p;
}();
std::array<std::array<unsigned, 32 * 24>, 2> bg_maps = [] {
    std::array<std::array<unsigned, 32 * 24>, 2> m{};
    for (auto &layer : m)
        layer.fill(1);
    return m;
}();
void expect(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
float number(unsigned i)
{
    float v;
    std::memcpy(&v, program.words + i, 4);
    return v;
}
void step(bool text = true, unsigned revision = 1)
{
    khdays_ui_obtained_frame(frame++);
    khdays_ui_obtained_begin();
    if (text) {
        const KhdaysUiElement e{"hud.obtained", 0, 1, 0, 56, 80, 24, -1};
        std::vector<unsigned> rgba(64 * scale * 16 * scale, 0xffffffffu);
        khdays_ui_obtained_text(&e, rgba.data(), 64 * scale, 16 * scale, revision);
    }
    khdays_ui_obtained_end();
    khdays_ui_obtained_commands(&program);
}
void test()
{
    khdays_ui_obtained_request(1);
    step(false);
    expect(khdays_ui_obtained_owns() && khdays_ui_notification_cell(0, 3, 0, 7) &&
               khdays_ui_notification_cell(0, 1, 0, 7),
           "pickup exposed native art while waiting for restored text tiles");
    expect(!program.words[KSI_CLEAR], "pickup started before its captured text was available");
    step();
    expect(khdays_ui_obtained_owns() && program.words[KSI_COUNT] == 1, "pickup did not start");
    expect(number(KSI_TEXT_ALPHA) == 0 && program.words[KSI_TEXT_INSERT] == 2, "text did not follow intro alpha/order");
    expect(submitted.y == 54 && anchor.y == 56 && (submitted.slot & KES_PICTURE), "native pickup anchor lost");
    const float initial_y = number(KSI_FACE_Y);
    const int allocated = int(images.size()), uploaded = uploads;
    step();
    expect(number(KSI_FACE_Y) < initial_y && number(KSI_TEXT_ALPHA) > 0, "text did not move/fade with the sequence");
    paused = true;
    step();
    const auto frozen = program;
    step();
    expect(!std::memcmp(&frozen, &program, sizeof(program)), "paused pickup animation advanced");
    paused = false;
    for (int i = 0; i < 14; ++i)
        step(false);
    expect(!program.words[KSI_CLEAR] && khdays_ui_obtained_owns(), "one-shot looped or native art reappeared");
    expect(int(images.size()) == allocated && uploads == uploaded, "warm playback allocated or uploaded text");
    khdays_ui_obtained_request(1);
    step();
    expect(program.words[KSI_COUNT] && number(KSI_TEXT_ALPHA) == 0, "repeated item did not restart");
    expect(uploads == uploaded, "identical item needlessly uploaded text again");
    step(true, 2);
    expect(uploads == uploaded + 1, "changed item text was not refreshed");
    khdays_ui_obtained_request(0);
    expect(!khdays_ui_obtained_owns(), "normal selection remained hidden");
    for (int tick = 0; tick < 3; ++tick) {
        step(false);
        expect(khdays_ui_notification_cell(0, 3, 0, 7) && khdays_ui_notification_cell(0, 1, 0, 7),
               "pickup art/text leaked while the native clear was pending");
    }
    std::fill_n(bg_pixels.begin() + 0x1020, 32, static_cast<unsigned char>(0));
    expect(!khdays_ui_notification_cell(0, 3, 0, 7) && khdays_ui_notification_cell(0, 1, 0, 7),
           "pickup did not retire artwork and text independently");
    std::fill_n(bg_pixels.begin() + 32, 32, static_cast<unsigned char>(0));
    step(false);
    expect(!khdays_ui_notification_cell(0, 1, 0, 7), "pickup retained cells after their pixel data cleared");
    std::fill_n(bg_pixels.begin() + 32, 32, static_cast<unsigned char>(0x11));
    std::fill_n(bg_pixels.begin() + 0x1020, 32, static_cast<unsigned char>(0x22));
    expect(!khdays_ui_notification_cell(0, 3, 0, 7), "retired pickup mask hid later artwork reusing a tile");
    step(false);
    expect(program.words[KSI_CLEAR], "native disappearance cut off the sequence's own exit");
    context.camp_screen = KHDAYS_CAMP_IN_MENU;
    step(false);
    expect(!program.words[KSI_CLEAR] && !khdays_ui_obtained_owns(), "pickup leaked into camp");
    context.camp_screen = KHDAYS_CAMP_NONE;
    step(false);
    expect(!program.words[KSI_CLEAR], "old pickup resurrected after leaving a menu");
    khdays_ui_obtained_request(1);
    step();
    ++caption_epoch;
    step(false);
    expect(!program.words[KSI_CLEAR] && !khdays_ui_obtained_owns(), "pickup survived native panel recreation");
    khdays_ui_obtained_request(1);
    step();
    hd = false;
    expect(!khdays_ui_obtained_enabled() && !khdays_ui_obtained_owns(), "HD-disabled fallback was suppressed");
    hd = true;
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    preferences[KHDAYS_OPT_NOTIFICATIONS] = 0;
    khdays_hud_settings_publish(preferences);
    khdays_ui_obtained_request(1);
    step(false);
    expect(!khdays_ui_obtained_owns() && !program.words[KSI_CLEAR], "live notification off hid native popup");
    preferences[KHDAYS_OPT_NOTIFICATIONS] = 1;
    khdays_hud_settings_publish(preferences);
    step();
    expect(khdays_ui_obtained_owns() && program.words[KSI_COUNT], "existing popup could not be enabled live");
}
void notifications()
{
    hd = true;
    ++caption_epoch;
    step(false);
    std::array<short, 14> request{};
    request[8] = 11;
    request[1] = 0;
    request[2] = 7;
    request[5] = 13;
    request[6] = 3;
    std::vector<unsigned> ink(64 * scale * 16 * scale, 0xffffffffu);
    KhdaysSequenceImage result[3]{};
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    preferences[KHDAYS_OPT_LOCKED_ON] = 1; /* on (off by default): the notifications' switch must leave it so */
    preferences[KHDAYS_OPT_NOTIFICATIONS] = 0;
    khdays_hud_settings_publish(preferences);
    request[0] = 78;
    khdays_ui_notification_layout(request.data(), 1);
    step(false);
    expect(khdays_ui_notification_configured(1) && !khdays_ui_notification_enabled(1) &&
               khdays_ui_notification_enabled(3) && !khdays_ui_notification_cell(0, 3, 12, 9),
           "notification family toggle affected LOCKED ON or retained suppression");
    preferences[KHDAYS_OPT_NOTIFICATIONS] = 1;
    khdays_hud_settings_publish(preferences);
    for (int k = 1; k <= 3; ++k) {
        request[0] = short(k == 1 ? 78 : k == 2 ? 72 : 60);
        if (k != 1)
            khdays_ui_notification_layout(request.data(), 1);
        expect(khdays_ui_notification_cell(0, 3, 12, 9), "notice exposed its box before the text layer was restored");
        khdays_ui_obtained_begin();
        KhdaysUiElement box{};
        expect(khdays_ui_notification_box(k, &box), "notice did not follow native layout");
        expect(!khdays_ui_notification_cell(1, 3, 0, 7), "notice hid the other screen");
        if (k != 3)
            khdays_ui_notification_text(k, &box, ink.data(), 64 * scale, 16 * scale, 1);
        khdays_ui_obtained_end();
        khdays_ui_notification_commands(result);
        expect(result[k - 1].words[KSI_CLEAR] && result[k - 1].words[KSI_COUNT] == 1, "notice did not render on GPU");
        expect(khdays_ui_notification_cell(0, 3, 12, 9), "notice left native background visible");
        expect(!khdays_ui_notification_cell(0, 3, 13, 9), "notice hid adjacent artwork");
        if (k != 3)
            expect(khdays_ui_notification_cell(0, 1, 12, 9), "notice left native text visible");
        if (k == 3) {
            for (int tick = 0; tick < 15; ++tick)
                step(false);
            khdays_ui_notification_commands(result);
            expect(result[2].words[KSI_CLEAR] && khdays_ui_notification_owns(3),
                   "locked-on loop stopped before native dismissal");
        }
        khdays_ui_notification_layout(request.data(), 0);
        expect(!khdays_ui_notification_owns(k), "dismissed notice kept suppressing native UI");
        for (int tick = 0; tick < 3; ++tick) {
            step(false);
            expect(khdays_ui_notification_cell(0, 3, 12, 9), "closed notice flashed its pending native artwork");
            if (k != 3)
                expect(khdays_ui_notification_cell(0, 1, 12, 9), "closed notice flashed pending native text");
        }
        for (auto &m : bg_maps)
            for (int y = 7; y < 10; ++y)
                for (int x = 0; x < 13; ++x)
                    m[y * 32 + x] = 0;
        step(false);
        expect(!khdays_ui_notification_cell(0, 3, 12, 9) && !khdays_ui_notification_cell(0, 1, 12, 9),
               "notice mask survived native tilemap clearing");
        for (auto &m : bg_maps)
            m.fill(1);
        expect(!khdays_ui_notification_cell(0, 3, 12, 9), "retired notice hid a later copy of the same art");
    }
    step(false);
    khdays_ui_notification_commands(result);
    expect(!result[2].words[KSI_CLEAR], "dismissed looping lock caption kept rendering");
    request[0] = 72;
    khdays_ui_notification_layout(request.data(), 1);
    ++caption_epoch;
    step(false);
    expect(!khdays_ui_notification_box(2, &submitted), "notice survived scene recreation");
}
void hidden_hud()
{
    for (int reason = 0; reason < 3; ++reason) {
        ++caption_epoch;
        step(false);
        const auto hide = [&] {
            context.tutorial = reason == 0;
            context.movie = reason == 1;
            context.event_status = reason == 2 ? 0 : -1;
            const unsigned update = KHDAYS_UPD_Ov023_SceneRun;
            context.updates[update / 64] |= 1ull << (update % 64);
        };
        const auto show = [&] {
            context.tutorial = context.movie = 0;
            context.event_status = -1;
            context.updates[KHDAYS_UPD_Ov023_SceneRun / 64] = 0;
        };
        hide();
        step(false);
        khdays_ui_obtained_request(1);
        step();
        step(false);
        expect(!program.words[KSI_CLEAR] && !khdays_ui_obtained_owns(), "hidden pickup rendered over the HUD");
        show();
        step();
        expect(program.words[KSI_CLEAR] && khdays_ui_obtained_owns(), "hidden pickup request was lost on return");
        expect(khdays_ui_notification_cell(0, 3, 0, 7), "restored pickup exposed the native background");
        step();
        const float alpha = number(KSI_TEXT_ALPHA);
        hide();
        step(false);
        expect(!program.words[KSI_CLEAR], "active pickup remained visible while hidden");
        show();
        step();
        expect(program.words[KSI_CLEAR] && number(KSI_TEXT_ALPHA) > alpha,
               "active pickup was lost or restarted after temporary HUD hiding");

        std::array<short, 14> request{};
        request[8] = 11;
        request[2] = 7;
        request[5] = 13;
        request[6] = 3;
        std::vector<unsigned> ink(64 * scale * 16 * scale, 0xffffffffu);
        for (int k = 1; k <= 3; ++k) {
            hide();
            step(false);
            request[0] = short(k == 1 ? 78 : k == 2 ? 72 : 60);
            khdays_ui_notification_layout(request.data(), 1);
            step(false);
            expect(!khdays_ui_notification_owns(k), "hidden notification retained visible ownership");
            show();
            step(false);
            KhdaysUiElement box{};
            expect(khdays_ui_notification_box(k, &box), "hidden notification lost its native layout");
            if (k != 3)
                khdays_ui_notification_text(k, &box, ink.data(), 64 * scale, 16 * scale, 1);
            khdays_ui_obtained_end();
            KhdaysSequenceImage result[3]{};
            khdays_ui_notification_commands(result);
            expect(result[k - 1].words[KSI_CLEAR] && khdays_ui_notification_owns(k),
                   "notification fell back to native art after HUD return");
            const unsigned anchor_x = result[k - 1].words[KSI_ANCHOR_X];
            const unsigned anchor_y = result[k - 1].words[KSI_ANCHOR_Y];
            context.mission_start = 4;
            step(false);
            khdays_ui_notification_commands(result);
            expect(result[k - 1].words[KSI_CLEAR] && result[k - 1].words[KSI_ANCHOR_X] == anchor_x &&
                       result[k - 1].words[KSI_ANCHOR_Y] == anchor_y,
                   "upper INFORMATION moved the notification box anchor");
            context.mission_start = 0;
            hide();
            step(false);
            show();
            step(false);
            khdays_ui_notification_commands(result);
            expect(result[k - 1].words[KSI_CLEAR] && khdays_ui_notification_cell(0, 3, 12, 9),
                   "active notification was lost while the HUD was hidden");
            khdays_ui_notification_layout(request.data(), 0);
        }
    }
}
void reference(const char *filename)
{
    namespace seq = khdays::assets::kh2;
    std::ifstream file(filename, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    for (const auto &e : seq::bar_entries(bytes.data(), bytes.size()))
        if (e.type == 25 && e.name == "gaug" && e.size)
            reference_data = seq::read_sequence(bytes.data() + e.offset, e.size);
    expect(reference_data.has_value(), "missing reference gaug");
    const auto &d = *reference_data;
    expect(seq::evaluate_sequence(d, 76, 27).active->x == 0 && seq::evaluate_sequence(d, 76, 27).active->y == 0,
           "obtained active origin was not centered");
    expect(seq::evaluate_sequence(d, 76, 8).active->y == 0 && seq::evaluate_sequence(d, 76, 12).active->y == -4,
           "original pickup entrance used a full-turn sway instead of a half-turn");
    khdays_ui_obtained_request(1);
    const int count = seq::sequence_frame_count(d, 76);
    expect(count == 107, "unexpected original obtained lifetime");
    for (int t = 0; t < count; ++t) {
        step(t == 0);
        const auto f = seq::evaluate_sequence(d, 76, float(t));
        expect(program.words[KSI_COUNT] == f.quads.size(), "runtime lost obtained art");
        expect(f.active.has_value(), "reference frame lost its text parent");
        const float x = number(KSI_FACE_X) - number(KSI_VIEW_X) + (d.clips[76].parameters[4] == 1 ? 32 * scale : 0);
        const float y = number(KSI_FACE_Y) - number(KSI_VIEW_Y) + 3 * scale;
        expect(std::abs(x - (f.active->x + d.clips[76].parameters[0]) * .65f * scale) < .001f &&
                   std::abs(y - (f.active->y + d.clips[76].parameters[1]) * .65f * scale) < .001f,
               "text did not follow original active translation");
        expect(std::abs(number(KSI_TEXT_ALPHA) - std::clamp(float(f.active->color >> 24) / 128, 0.0f, 1.0f)) < .001f,
               "text did not follow original active alpha");
    }
    step(false);
    expect(!program.words[KSI_CLEAR], "obtained text outlived animation 76");
}
} // namespace
namespace khdays::render::kh2 {
SequenceAsset load_sequence_asset(const std::filesystem::path &, const std::string &, const std::filesystem::path &)
{
    if (missing)
        throw std::runtime_error("missing fixture");
    SequenceAsset a;
    a.logical_size = {512, 256};
    a.texture.width = a.texture.height = 1;
    a.texture.rgba = {255, 255, 255, 255};
    auto &d = a.sequence;
    if (reference_data) {
        d = *reference_data;
        return a;
    }
    assets::kh2::SequencePart p;
    p.uv = {0, 0, 1, 1};
    p.colors.fill(0x80808080);
    d.parts.push_back(p);
    d.groups.push_back({{-84, -6, 182, 21}, 0});
    d.sprites.push_back({0, 1});
    assets::kh2::SequenceAnimation anim;
    anim.flags = 1 | 2 | (1u << 4) | (1u << 5) | (1u << 6);
    anim.time = {0, 9};
    anim.offset_y = {40, 0};
    anim.colors = {0x00808080, 0x80808080};
    d.animations.push_back(anim);
    d.clips.resize(77);
    d.clips[76].animations = {0, 1};
    d.clips[76].loop = 1;
    d.clips[76].restart = 150;
    return a;
}
} // namespace khdays::render::kh2
extern "C" {
int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *layer)
{
    if (engine || (bg != 1 && bg != 3))
        return 0;
    *layer = {bg == 3 ? 0x2800u : 0x2000u, bg == 3 ? 0x1000u : 0u, 32, 32, 0, 0, 0, 0};
    return 1;
}
unsigned khdays_bg_map_entry(int, const KhdaysBgLayer *layer, int x, int y)
{
    return bg_maps[layer->chars == 0x1000u][y * 32 + x];
}
const unsigned char *khdays_bg_memory(int engine, unsigned address)
{
    return !engine && address < bg_pixels.size() ? bg_pixels.data() + address : nullptr;
}
unsigned khdays_ui_information_epoch(void)
{
    return caption_epoch;
}
int khdays_settings_game_language(void)
{
    return 0;
}
const char *khdays_language_code(int)
{
    return "en";
}
int khdays_mods_find(const char *, char *out, int size)
{
    std::snprintf(out, std::size_t(size), "%s", settings.string().c_str());
    return 1;
}
int khdays_textures_hd(void)
{
    return hd;
}
int khdays_ui_mode(int)
{
    return KHDAYS_UI_ON;
}
void khdays_ui_changed(void) {}
void khdays_log(const char *, ...) {}
int khdays_link_paused(void)
{
    return paused;
}
void khdays_situation_take(KhdaysSituationInputs *out, unsigned)
{
    *out = context;
}
int khdays_ui_scale(void)
{
    return scale;
}
int khdays_ui_image_get(int w, int h)
{
    images.emplace_back(std::size_t(w) * h);
    return int(images.size()) - 1;
}
void khdays_ui_image_put(int) {}
unsigned *khdays_ui_image_begin(int image)
{
    return images.at(std::size_t(image)).data();
}
void khdays_ui_image_end(int)
{
    ++uploads;
}
int khdays_ui_image_replacement(int image)
{
    return image;
}
int khdays_textures_add_info(const char *, int, int, const unsigned *, unsigned)
{
    return 99;
}
void khdays_ui_emit_extended(int, const KhdaysUiElement *e, const KhdaysUiElement *box)
{
    submitted = *e;
    anchor = *box;
}
}
int main(int argc, char **argv)
{
    try {
        const bool legacy = argc > 1 && std::string(argv[1]) == "--legacy";
        missing = argc > 1 && std::string(argv[1]) == "--missing";
        settings = std::filesystem::temp_directory_path() /
                   (std::string("khdays-obtained") + (argc > 1 ? argv[1] : "") + ".json");
        std::ofstream(settings)
            << (legacy
                    ? "{}"
                    : R"({"obtained":{"enabled":true},"backpack_full":{"enabled":true,"animation":76},"breakthrough":{"enabled":true,"animation":76},"locked_on":{"enabled":true,"animation":76}})");
        if (argc == 3 && std::string(argv[1]) == "--reference")
            reference(argv[2]);
        else if (missing || legacy) {
            khdays_ui_obtained_request(1);
            step();
            expect(!khdays_ui_obtained_enabled() && !program.words[KSI_CLEAR],
                   "missing/legacy replacement broke fallback");
            for (int k = 1; k <= 3; ++k)
                expect(!khdays_ui_notification_enabled(k), "new notice enabled without an asset");
        } else {
            test();
            notifications();
            hidden_hud();
        }
        std::filesystem::remove(settings);
        std::cout << "Obtained animation, cached text, parent alpha and native fallback passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
