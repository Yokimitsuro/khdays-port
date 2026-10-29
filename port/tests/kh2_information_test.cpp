// Synthetic information timelines, native/HD glyph planes, cached uploads and fallback.
#include "../ui/kh2_information.h"
#include "../host/app/hud_settings.h"
#include "../ui/kh2_hp_config.h"
#include "../host/render/kh2_sequence_asset.h"
#include "khdays/assets/kh2_bar.h"
extern "C" {
#include "../ui/bg.h"
#include "../runtime/mods/textures.h"
#include "../runtime/display/frame_package.h"
#include "../ui/situation.h"
#include "../runtime/menus/camp.h"
}
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>
namespace {
namespace seq = khdays::assets::kh2;
std::filesystem::path settings;
bool missing = false, hd = true, paused = false, single = false;
unsigned frame = 10;
int scale = 1, uploads = 0, emitted = 0, wants = 9;
KhdaysSituationInputs situation = [] {
    KhdaysSituationInputs s{};
    s.scene = 2;
    s.overlays = 1ull << 2;
    s.camp_screen = KHDAYS_CAMP_NONE;
    s.event_status = -1;
    return s;
}();
bool native_upper = false;
std::array<unsigned char, 64> upper_pixels{};
int masked_cells = 0;
std::vector<std::vector<unsigned>> images;
KhdaysSequenceImage programs[2];
KhdaysUiElement submitted{}, box{};
std::optional<seq::SequenceData> original;
std::array<int, 2> original_size{};
seq::SequenceClip *loop_clip = nullptr;
void expect(bool good, const char *why)
{
    if (!good)
        throw std::runtime_error(why);
}
float number(unsigned index, int engine = 1)
{
    float v;
    std::memcpy(&v, programs[engine].words + index, 4);
    return v;
}
seq::SequenceData fixture()
{
    seq::SequenceData d;
    for (unsigned i = 0; i < 4; ++i) {
        seq::SequencePart part;
        part.uv = {int(i), 0, int(i + 1), 1};
        part.colors.fill(0x80808080);
        d.parts.push_back(part);
        d.groups.push_back({{0, 0, 40, 20}, i});
        d.sprites.push_back({static_cast<unsigned short>(i), 1});
        seq::SequenceAnimation a;
        a.flags = 0x77ff7;
        a.sprite = i;
        a.time = {0, i == 0 ? 3 : i == 3 ? 4 : 10};
        a.x = {10, 10};
        a.y = {20, 20};
        a.scale = a.scale_x = a.scale_y = {1, 1};
        a.colors.fill(0x80808080);
        d.animations.push_back(a);
        seq::SequenceClip clip;
        clip.animations = {static_cast<unsigned short>(i), 1};
        clip.loop = i == 0 || i == 3 ? 1 : 0;
        clip.parameters = {6, -12, 0, 45, 360};
        d.clips.push_back(clip);
    }
    seq::SequencePart edge;
    edge.uv = {0, 0, 1, 1};
    edge.colors = {0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u};
    const auto part = unsigned(d.parts.size()), group = unsigned(d.groups.size());
    d.parts.push_back(edge);
    d.groups.push_back(d.groups[2]);
    d.groups.push_back({{325, -20, 350, 0}, part}); // At x=10: transparent at 335.5, opaque at 360.5.
    d.sprites[2] = {static_cast<unsigned short>(group), 2};
    return d;
}
void step(bool shown = true, unsigned revision = 1, int engine = 1, int width = 64)
{
    frame += 2;
    emitted = 0;
    khdays_ui_information_frame(frame);
    khdays_ui_information_begin();
    if (shown) {
        const KhdaysUiElement e{"hud.info", engine, 1, 0, 8, float(width), 16, 0};
        std::vector<unsigned> glyphs(std::size_t(width) * 16, 0xffffffffu);
        expect(khdays_ui_information_text(&e, glyphs.data(), width, 16, revision, 8, 3, unsigned(width * 16)) ==
                   khdays_ui_information_wants(),
               "replacement did not preserve native fallback");
    }
    khdays_ui_information_end();
    khdays_ui_information_commands(programs);
}
int clip(unsigned q = 0, int engine = 1)
{
    if (q >= programs[engine].words[KSI_COUNT])
        return -1;
    float v;
    std::memcpy(&v, programs[engine].words + KSI_HEADER + q * KSI_QUAD_WORDS + 2, 4);
    return int(v);
}
void test()
{
    const unsigned short a[]{'a', 0}, b[]{'b', 0};
    khdays_ui_information_message(a);
    step(false);
    expect(clip() == 0 && number(KSI_TEXT_ALPHA) == 0, "intro exposed text");
    expect(programs[1].words[KSI_FACE] == ~0u && (khdays_ui_information_state() & 0x202u) == 0x202u,
           "window waited for a glyph canvas before replacing native art");
    expect(programs[1].words[KSI_TEXT_INSERT] == 2, "text not inserted between window and mask");
    step();
    const int allocated = int(images.size()), uploaded = uploads;
    step();
    expect(clip() == 1 && clip(1) == 2 && number(KSI_TEXT_ALPHA) == 1, "intro did not become loop/reveal");
    expect(int(images.size()) == allocated && uploads == uploaded, "warm frame allocated or uploaded glyph pixels");
    expect(submitted.x < 0 && box.y == 8 && submitted.h > 0, "expanded image lost its native composition anchor");
    expect(number(KSI_FACE_X) != 0 && number(KSI_FACE_Y) != 0, "text anchor ignored sequence and clip parameters");
    khdays_ui_information_message(b);
    step();
    expect(clip() == 1 && clip(1) == 2, "message change unnecessarily replayed intro");
    paused = true;
    step();
    expect(clip() == 1 && programs[1].words[KSI_COUNT] == 1 && number(KSI_TEXT_ALPHA) == 1,
           "paused caption retained its reveal cover");
    const auto saved = programs[1];
    step();
    expect(!std::memcmp(&saved, &programs[1], sizeof(saved)), "paused playback advanced");
    paused = false;
    step(false);
    expect(clip() == 1, "temporary canvas disappearance closed a retained caption");
    khdays_ui_information_message(nullptr);
    step(false);
    expect(clip() == 3 && programs[1].words[KSI_COUNT] == 1 && programs[1].words[KSI_FACE] == ~0u &&
               number(KSI_TEXT_ALPHA) == 0,
           "close exposed text when the reveal mask was hidden");
    step(false);
    step(false);
    step(false);
    expect(clip() < 0, "exit did not stop");
    for (int i = 0; i < 8; ++i)
        step(false);
    expect(khdays_ui_information_active(1) && clip() < 0, "empty message brought the original border back");
    hd = false;
    step(false);
    expect(!khdays_ui_information_wants() && !khdays_ui_information_state(), "HD off did not fall back");
    hd = true;
    single = true;
    khdays_ui_information_message(a);
    step();
    for (int i = 0; i < 24; ++i)
        step();
    expect(clip() < 0 && khdays_ui_information_active(1), "expired lower message restarted or native border returned");
    khdays_ui_information_message(b);
    step();
    expect(clip() == 0, "new message after timeout did not reopen");
    // Pause restores an expired retained caption; unpause starts a fresh display period.
    for (int i = 0; i < 24; ++i)
        step();
    khdays_ui_information_hold(1, 0);
    paused = true;
    step(false);
    expect(clip() == 1 && number(KSI_TEXT_ALPHA) == 1 && programs[1].words[KSI_COUNT] == 1,
           "pause did not restore an uncovered retained KH2 caption");
    const auto pause_image = programs[1];
    frame += 200;
    step(false);
    expect(!std::memcmp(&pause_image, &programs[1], sizeof(pause_image)), "retained pause animation advanced");
    khdays_ui_information_hold(0, 0);
    paused = false;
    frame -= 200; // Days restores the VBlank count saved when pausing.
    step(false);
    expect(clip() == 1 && (khdays_ui_information_state() & 0x200u), "unpause did not restore display time");
    // Long lines scroll only after the wait, and outlive the short-line timeout.
    khdays_ui_information_message(a);
    step(true, 10, 1, 512);
    const float initial_x = number(KSI_FACE_X);
    step(true, 10, 1, 512);
    expect(number(KSI_FACE_X) == initial_x, "marquee skipped its initial wait");
    for (int i = 0; i < 10; ++i)
        step(true, 10, 1, 512);
    expect(clip() == 1 && number(KSI_FACE_X) < initial_x, "overflowing text did not scroll");
    for (int i = 0; i < 15; ++i)
        step(true, 10, 1, 512);
    expect(clip() >= 0, "long text was cut off by the original five-second timer");
    for (int i = 0; i < 200; ++i)
        step(true, 10, 1, 512);
    expect(clip() < 0, "long text failed to finish its exit");
    // Mission-start art also opens before a text surface exists, and exits without that surface.
    khdays_ui_information_panel(a);
    situation.mission_start = 1;
    step(false);
    expect(clip(0, 0) < 0, "upper intro played while the lower panel was retracting");
    situation.mission_start = 2;
    step(false);
    expect(clip(0, 0) == 0 && (khdays_ui_information_state() & 0x100u), "upper introduction missing");
    step(false);
    step(false);
    expect(clip(0, 0) == 1 && clip(1, 0) == 2, "upper reveal missing");
    khdays_ui_information_panel(nullptr);
    step(false);
    expect(clip(0, 0) == 3 && (khdays_ui_information_state() & 0x100u), "upper exit cut off");
    step(false);
    step(false);
    step(false);
    expect(clip(0, 0) < 0, "upper exit did not finish");
    // The lower copy remains visible on two screens, including a renderer requesting only 2D.
    single = false;
    wants = KHDAYS_WANT_2D;
    khdays_ui_information_message(nullptr);
    step(false);
    khdays_ui_information_panel(a);
    step(false);
    khdays_ui_information_message(a);
    step();
    expect(clip() >= 0 && (khdays_ui_information_state() & 0x300u) == 0x300u,
           "two-screen handoff hid the lower caption");
    khdays_ui_information_panel(nullptr);
    khdays_ui_information_message(b);
    scale = 4;
    step(true, 2);
    expect(programs[1].words[KSI_HEIGHT] == unsigned(submitted.h * 4), "resolution change");
    situation.mission_start = 0;
}
void context_tests()
{
    const unsigned short text[]{'h', 'i', 0};
    scale = 1;
    single = false;
    wants = KHDAYS_WANT_2D;
    khdays_ui_information_reset();
    const unsigned first_epoch = khdays_ui_information_epoch();
    khdays_ui_information_message(text);
    step();
    const auto field = situation;
    for (int hidden = 0; hidden < 9; ++hidden) {
        situation = field;
        switch (hidden) {
        case 0:
            situation.camp_screen = 0;
            situation.camp_story = 1;
            break;
        case 1:
            situation.camp_screen = 2;
            situation.overlays |= 1ull << 25;
            break;
        case 2: situation.overlays |= 1ull << 8; break;
        case 3: situation.overlays |= 1ull << 26; break;
        case 4: situation.overlays |= 1ull << 27; break;
        case 5: situation.movie = 1; break;
        case 6: situation.tutorial = 1; break;
        case 7: situation.mission_start = 10; break;
        case 8:
            situation.event_status = 0;
            situation.updates[KHDAYS_UPD_Ov023_SceneRun / 64] |= 1ull << (KHDAYS_UPD_Ov023_SceneRun % 64);
            break;
        }
        step(false);
        expect(!khdays_ui_information_wants() && khdays_ui_information_state() == KHDAYS_INFORMATION_AVAILABLE &&
                   !emitted,
               "information escaped into a menu, movie, tutorial, or event");
        expect(khdays_ui_information_configured(), "temporary context hiding lost configured replacement availability");
        situation = field;
        step();
        expect(khdays_ui_information_wants() && emitted, "caption did not resume when field display returned");
    }
    // A newly created map can have the exact same caption as the previous map.
    khdays_ui_information_reset();
    expect(khdays_ui_information_epoch() != first_epoch &&
               khdays_ui_information_state() == KHDAYS_INFORMATION_AVAILABLE,
           "teardown retained ownership");
    khdays_ui_information_message(text);
    step(false);
    expect(clip() == 0 && programs[1].words[KSI_FACE] == ~0u, "same caption did not reopen cleanly on the next map");
    step();
    // Observe the real upper crawl even when it was initiated outside the request hook.
    situation.mission_start = 4;
    native_upper = true;
    masked_cells = 0;
    khdays_ui_information_crawl(text);
    step();
    expect(clip(0, 0) == 0 && masked_cells == 36, "upper main.p2 banner was not replaced");
    expect(number(KSI_SCALE, 0) == number(KSI_SCALE, 1), "upper and lower replacement scales differ");
    // The native panel can close before its final BG transfer. Suppress only identical retired tiles.
    situation.mission_start = 0;
    khdays_ui_information_panel(nullptr);
    masked_cells = 0;
    step(false);
    expect(masked_cells == 36, "native upper artwork flashed back when its panel state ended");
    upper_pixels[0] ^= 1;
    masked_cells = 0;
    step(false);
    expect(masked_cells == 0, "retired banner suppression hid newly published HUD pixels");
    // A less visible staging canvas must not replace the foreground caption's glyph plane.
    const KhdaysUiElement e{"hud.info", 1, 1, 0, 0, 64, 16, 0};
    std::vector<unsigned> blank(64 * 16), ink(64 * 16, 0xffffffffu);
    frame += 2;
    khdays_ui_information_frame(frame);
    khdays_ui_information_begin();
    expect(khdays_ui_information_text(&e, blank.data(), 64, 16, 90, 8, 3, 1), "staging caption rejected");
    expect(khdays_ui_information_text(&e, ink.data(), 64, 16, 91, 8, 3, 500), "visible caption rejected");
    expect(khdays_ui_information_text(&e, blank.data(), 64, 16, 90, 8, 3, 1), "staging suppression rejected");
    khdays_ui_information_end();
    khdays_ui_information_commands(programs);
    expect(images.at(programs[1].words[KSI_FACE])[0] == 0xffffffffu, "staging canvas hid the visible text");
    native_upper = false;
    situation = field;
    khdays_ui_information_hold(1, 1);
    step(false);
    expect(khdays_ui_information_state() == KHDAYS_INFORMATION_AVAILABLE,
           "held loading screen retained the information replacement");
    khdays_ui_information_hold(0, 0);
    step();
    single = true;
    for (int i = 0; i < 30; ++i)
        step();
    expect(clip() < 0, "single-screen caption did not expire");
    single = false;
    step();
    expect(clip() == 1, "switching to two screens left the persistent caption hidden");
    // A message first posted during loading/event setup has never been shown and still needs its intro.
    khdays_ui_information_reset();
    situation.movie = 1;
    khdays_ui_information_message(text);
    step(false);
    situation = field;
    step();
    expect(clip() == 0, "new caption posted while hidden incorrectly skipped its intro");
}
void pause_during_intro()
{
    const unsigned short text[]{'p', 0};
    khdays_ui_information_reset();
    single = true;
    khdays_ui_information_message(text);
    step();
    expect(clip() == 0 && number(KSI_TEXT_ALPHA) == 0, "fixture did not enter its intro");
    paused = true;
    step(false);
    expect(clip() == 1 && programs[1].words[KSI_COUNT] == 1 && number(KSI_TEXT_ALPHA) == 1 &&
               programs[1].words[KSI_FACE] != ~0u,
           "pausing during intro left the retained text invisible");
    const auto saved = programs[1];
    frame += 500;
    step(false);
    expect(!std::memcmp(&saved, &programs[1], sizeof(saved)), "pause advanced a frozen intro");
    paused = false;
    step(false);
    expect(clip() == 0, "pause discarded the original animation position");
    step(false);
    expect(clip() == 1 && clip(1) == 2, "unpause failed to resume the intro/reveal");
}
void marquee_end()
{
    const unsigned short text[]{'l', 'o', 'n', 'g', 0};
    single = true;
    for (const int width : {512, 1024}) {
        khdays_ui_information_reset();
        khdays_ui_information_message(text);
        bool reached = false, closed = false;
        for (int i = 0; i < 500; ++i) {
            step(true, 100u + unsigned(width), 1, width);
            const float right = number(KSI_FACE_X) + number(KSI_FACE_W);
            const float clear_edge = number(KSI_TEXT_CLIP_X) + 335.5f * .65f * scale;
            expect(std::abs(number(KSI_TEXT_CLIP_W) - 360 * .65f * scale) < .001f,
                   "PC information text was clipped at half the sequence width");
            if (clip() == 1 && std::abs(right - clear_edge) < .001f)
                reached = true;
            if (clip() == 3) {
                expect(reached, "exit started before the last character cleared the right edge fade");
                expect(programs[1].words[KSI_FACE] == ~0u && number(KSI_TEXT_ALPHA) == 0,
                       "exit exposed retained text after removing the reveal overlay");
                closed = true;
                break;
            }
        }
        expect(closed, "long caption failed to finish scrolling");
    }
}
void reference_test(const std::filesystem::path &archive)
{
    std::ifstream file(archive, std::ios::binary);
    expect(bool(file), "cannot open local information reference archive");
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    for (const auto &e : khdays::assets::kh2::bar_entries(bytes.data(), bytes.size()))
        if (e.name == "gaug") {
            if (e.type == 0x19)
                original = seq::read_sequence(bytes.data() + e.offset, e.size);
            if (e.type == 0x18)
                original_size = seq::read_sequence_image_size(bytes.data() + e.offset, e.size);
        }
    expect(original && original_size[0] > 0, "reference archive has no gaug image/sequence");
    const unsigned short message[]{'r', 0};
    single = true;
    khdays_ui_information_message(message);
    bool readable = false, reached = false, closed = false;
    for (int i = 0; i < 500; ++i) {
        step(true, 500, 1, 512);
        expect(std::abs(number(KSI_TEXT_CLIP_W) - 360 * .65f) < .001f,
               "original PC sequence did not provide its full text viewport");
        if (number(KSI_TEXT_ALPHA) > 0) {
            readable = true;
            const float last_pixel = number(KSI_FACE_X) + number(KSI_FACE_W);
            const float clear_edge = number(KSI_TEXT_CLIP_X) + 335.5f * .65f;
            reached |= std::abs(last_pixel - clear_edge) < .001f;
        }
        if (readable && programs[1].words[KSI_FACE] == ~0u) {
            expect(reached && number(KSI_TEXT_ALPHA) == 0,
                   "original PC fade hid the last characters or the exit exposed them");
            closed = true;
            break;
        }
    }
    expect(closed, "original information timeline did not finish its scroll/exit");
}
} // namespace
namespace khdays::render::kh2 {
SequenceAsset load_sequence_asset(const std::filesystem::path &, const std::string &entry,
                                  const std::filesystem::path &)
{
    if (missing)
        throw std::runtime_error("missing fixture");
    expect(entry == "gaug", "wrong information BAR entry");
    SequenceAsset a;
    a.sequence = original ? *original : fixture();
    loop_clip = &a.sequence.clips[1];
    a.logical_size = original ? original_size : std::array{512, 256};
    a.texture.width = a.texture.height = 1;
    a.texture.rgba = {255, 255, 255, 255};
    return a;
}
} // namespace khdays::render::kh2
extern "C" {
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
void khdays_ui_changed(void) {}
unsigned khdays_ui_information_policy(void)
{
    return khdays_hud_setting(KHDAYS_OPT_INFORMATION) ? 3u : 0u;
}
void khdays_situation_take(KhdaysSituationInputs *out, unsigned)
{
    *out = situation;
    out->pause_mode = paused ? 2 : 0;
}
int khdays_textures_hd(void)
{
    return hd;
}
int khdays_host_frame_wants(void)
{
    return wants;
}
int khdays_host_graphics_flags(void)
{
    return single ? 0x10 : 0;
}
unsigned khdays_link_scene(unsigned *)
{
    return 2;
}
int khdays_link_paused(void)
{
    return paused;
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
unsigned *khdays_ui_image_begin(int i)
{
    ++uploads;
    return images.at(std::size_t(i)).data();
}
void khdays_ui_image_end(int) {}
int khdays_ui_image_replacement(int i)
{
    return i;
}
int khdays_textures_add_info(const char *, int, int, const unsigned *, unsigned)
{
    return 99;
}
void khdays_ui_emit_extended(int, const KhdaysUiElement *e, const KhdaysUiElement *b)
{
    submitted = *e;
    box = *b;
    ++emitted;
}
int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *out)
{
    if (!native_upper || engine || bg != 3)
        return 0;
    *out = {};
    out->wt = out->ht = 32;
    return 1;
}
unsigned khdays_bg_map_entry(int, const KhdaysBgLayer *, int x, int)
{
    return x < 12 ? 1 : 2;
}
const char *khdays_bg_origin(int, unsigned address)
{
    return address == 32 ? "UI/btl/en/main.p2@000001" : "UI/btl/unrelated.NCGR";
}
const unsigned char *khdays_bg_memory(int, unsigned address)
{
    return native_upper && address == 32 ? upper_pixels.data() : nullptr;
}
void khdays_ui_left_out_cell(int, int, int, int x, int)
{
    expect(x < 12, "masked unrelated upper-screen artwork");
    ++masked_cells;
}
}
int main(int argc, char **argv)
{
    try {
        const bool legacy = argc > 1 && std::string(argv[1]) == "--legacy";
        missing = argc > 1 && std::string(argv[1]) == "--missing";
        settings =
            std::filesystem::temp_directory_path() / ("khdays-info" + std::string(argc > 1 ? argv[1] : "") + ".json");
        {
            std::ofstream f(settings);
            f << (legacy
                      ? "{}"
                      : R"({"information":{"enabled":true,"hold_frames":30,"scroll_wait_frames":4,"scroll_speed":2}})");
        }
        if (argc == 3 && std::string(argv[1]) == "--reference")
            reference_test(argv[2]);
        else if (legacy || missing) {
            step();
            expect(clip() < 0 && !emitted && !khdays_ui_information_state(), "unavailable replacement rendered");
            expect(!khdays_ui_information_configured(), "missing replacement reported available for HUD fade control");
        } else {
            test();
            context_tests();
            pause_during_intro();
            marquee_end();
            int preferences[KHDAYS_OPT_COUNT];
            khdays_hud_settings_default(preferences);
            preferences[KHDAYS_OPT_INFORMATION] = 0;
            khdays_hud_settings_publish(preferences);
            step();
            expect(!khdays_ui_information_wants() && !khdays_ui_information_configured(), "information off gate");
            preferences[KHDAYS_OPT_INFORMATION] = 1;
            khdays_hud_settings_publish(preferences);
            step();
            expect(khdays_ui_information_configured(), "information could not be re-enabled live");
            loop_clip->restart = ~0u;
            khdays_ui_information_reset();
            const unsigned short broken[]{'x', 0};
            khdays_ui_information_message(broken);
            for (int i = 0; i < 16; ++i)
                step();
            expect(!khdays_ui_information_state() && !khdays_ui_information_wants(),
                   "invalid runtime animation escaped fallback or retained stale ownership");
            expect(!programs[0].words[KSI_CLEAR] && !programs[1].words[KSI_CLEAR],
                   "failed information animation retained GPU commands");
        }
        std::filesystem::remove(settings);
        std::cout << "Information playback, text anchor, caching and fallback passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
