// Synthetic mission state and art: counter packing, native ownership, fades and font fallback.
#include "../ui/kh2_mission_panel.cpp"
#include <fstream>
#include <iostream>
#include <random>
#include <string>
namespace {
std::vector<std::vector<unsigned>> test_images;
KhdaysSituationInputs test_context{};
KhdaysUiElement test_element{};
bool test_paused = false, test_visible = true, test_tall_font = false;
bool test_font_observing = false, test_font_available = false;
int test_uploads = 0, test_cells = 0;
unsigned test_epoch = 1, test_tick = 0;
std::filesystem::path test_config;
std::filesystem::path temp_config; // this run's own: ctest runs the test's four ways at once
void check(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
template <class T> void store(unsigned p, T v)
{
    std::memcpy(KHDAYS_W(p), &v, sizeof(v));
}
float word(unsigned i)
{
    float f;
    std::memcpy(&f, program.words + i, 4);
    return f;
}
void step()
{
    khdays_ui_mission_panel_frame(++test_tick);
    khdays_ui_mission_panel_emit();
}
seq::SequenceData fixture()
{
    seq::SequenceData d;
    seq::SequencePart part;
    part.uv = {0, 0, 1, 1};
    part.colors.fill(0x80808080);
    d.parts.push_back(part);
    for (int i = 0; i < 16; ++i) {
        d.groups.push_back({i < 3    ? std::array<int, 4>{0, -35, 260, 13}
                            : i <= 6 ? std::array<int, 4>{0, -3, 229, 3}
                                     : std::array<int, 4>{-5, -5, 5 + i, 5},
                            0});
        d.sprites.push_back({(unsigned short)i, 1});
        seq::SequenceAnimation a;
        a.sprite = unsigned(i);
        a.time = {0, 9};
        a.colors = {0x80808080, 0x80808080};
        a.flags = 1 | 2 | (1u << 4) | (1u << 5) | (1u << 6);
        if (i == 1)
            a.colors = {0x00808080, 0x80808080};
        if (i == 2) {
            a.colors = {0x80808080, 0x00808080};
            a.y = {0, 10};
        }
        d.animations.push_back(a);
        seq::SequenceClip c;
        c.animations = {(unsigned short)i, 1};
        c.restart = 9;
        d.clips.push_back(c);
    }
    return d;
}
void setup()
{
    test_context.scene = 2;
    test_context.overlays = 4;
    test_context.event_status = -1;
    test_context.camp_screen = KHDAYS_CAMP_NONE;
    khdays_window = new unsigned char[0x02400000]{};
    store(0x0207f630u, 0x02100000u);
    store(0x0207f614u, 0x02101000u);
    store(0x02100188u, 100);
    store(0x0210018cu, 40);
    store(0x02100190u, 60);
    store(0x02100194u, 89);
    store(0x021000a8u, 1);
    store(0x021000b8u, 0x02102000u);
    std::memcpy(KHDAYS_W(0x02102000), u"Destroy the shadow globs", 48);
    temp_config = std::filesystem::temp_directory_path() /
                  ("khdays-mission-panel-test-" + std::to_string(std::random_device{}()) + ".json");
    test_config = temp_config;
    std::ofstream(test_config) << R"({"mission_panel":{"enabled":true}})";
    config = ui::load_mission_panel_config(test_config);
    tried = true;
    asset.emplace();
    asset->sequence = fixture();
    asset->logical_size = {512, 256};
    atlas = 99;
    width = 220;
    height = 60;
    left = -2;
    top = -10;
    for (int i = 0; i < 16; ++i)
        bounds[i] = frame_bounds(seq::evaluate_sequence(asset->sequence, i, 9));
    message_language = 1;
    messages[0] = u"DAY %d";
    messages[1] = u"Missions Left %d/%d";
}
void tests()
{
    std::ofstream(test_config) << "{}";
    check(!ui::load_mission_panel_config(test_config).enabled, "legacy JSON enabled panel");
    std::ofstream(test_config)
        << R"({"mission_panel":{"enabled":true,"description_x":12,"tally_x":249,"icon_gap":7,"counters_scale":1.5}})";
    auto custom = ui::load_mission_panel_config(test_config);
    check(custom.description_x == 12 && custom.tally_x == 249 && custom.icon_gap == 7 && custom.counters_scale == 1.5f,
          "JSON anchors/row scale ignored");
    check(ui::load_hp_gauge_config(test_config).scale == .65f, "mission settings broke gauge loader");
    const char *anchors[] = {"top-left",     "top",         "top-right", "middle-left", "middle",
                             "middle-right", "bottom-left", "bottom",    "bottom-right"};
    for (unsigned i = 0; i < 9; ++i) {
        std::ofstream(test_config) << "{\"mission_panel\":{\"anchor\":\"" << anchors[i] << "\"}}";
        const auto anchored = ui::load_mission_panel_config(test_config);
        check(anchored.anchor == i + 1 && anchored.pause_anchor == i + 1, "anchor parsing/inheritance");
    }
    std::ofstream(test_config) << R"({"mission_panel":{"anchor":"top-right","pause_anchor":"bottom"}})";
    const auto anchored = ui::load_mission_panel_config(test_config);
    check(anchored.anchor == 3 && anchored.pause_anchor == 8, "pause anchor override");
    check(config.anchor == 0 && config.pause_anchor == 0, "legacy panel placement changed");
    for (const char *bad : {R"({"mission_panel":{"scale":0}})", R"({"mission_panel":{"archive":"../bad"}})",
                            R"({"mission_panel":{"bar_start_x":25,"bar_end_x":10}})",
                            R"({"mission_panel":{"unknown":1}})", R"({"mission_panel":{"counters_scale":0}})",
                            R"({"mission_panel":{"anchor":"invalid"}})", R"({"mission_panel":{"pause_anchor":4}})"}) {
        std::ofstream(test_config) << bad;
        bool rejected = false;
        try {
            ui::load_mission_panel_config(test_config);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "invalid mission JSON accepted");
    }

    // The display preference starts off; configured routes must still bind.
    step();
    check(description_font.route == "text/font_eu_10.nftr.hd" && counter_font.route == "text/font_eu_08.nftr.hd" &&
              !test_font_observing,
          "loading with HD fonts off lost the scoped mapping or leaked observation mode");
    test_font_available = true;
    check(owns && native && program.words[KSI_CLEAR], "visible panel was not replaced");
    check(test_element.screen == 1 && test_element.y == 152 + top + config.offset_y && word(KSI_ANCHOR_Y) == 152,
          "dual-screen panel lost its native lower-screen location");
    check(word(KSI_TEXT_ALPHA) == 0, "intro did not fade its text");
    check(khdays_ui_mission_panel_cell(1, 0, 5, 20) && khdays_ui_mission_panel_cell(1, 2, 5, 20),
          "panel text/art were not suppressed");
    check(!khdays_ui_mission_panel_cell(0, 2, 5, 20) && !khdays_ui_mission_panel_cell(1, 1, 5, 20) &&
              !khdays_ui_mission_panel_cell(1, 2, 5, 10),
          "suppression escaped panel");
    for (int i = 0; i < 9; ++i)
        step();
    check(word(KSI_TEXT_ALPHA) == 1, "intro did not finish");
    int uploads = test_uploads;
    step();
    check(test_uploads == uploads, "unchanged panel text uploaded every frame");
    Status st;
    const auto resting = frame_bounds(seq::evaluate_sequence(asset->sequence, config.intro, 9));
    const auto original_config = config;
    config.offset_x = 7;
    config.offset_y = -9;
    config.pause_offset_x = 13;
    config.pause_offset_y = -11;
    for (unsigned anchor = 1; anchor <= 9; ++anchor) {
        config.anchor = anchor;
        config.pause_anchor = 10 - anchor;
        for (int paused = 0; paused < 2; ++paused) {
            test_context.pause_mode = context.pause_mode = static_cast<unsigned char>(paused);
            draw(st);
            const unsigned chosen = paused ? config.pause_anchor : anchor;
            const float ax = float((chosen - 1) % 3) * .5f, ay = float((chosen - 1) / 3) * .5f;
            const float px = (std::lerp(resting.x0, resting.x1, ax) - config.source_origin_x) * config.scale - left;
            const float py = (std::lerp(resting.y0, resting.y1, ay) - config.source_origin_y) * config.scale - top;
            check(program.words[KSI_PANEL_ANCHOR] == chosen, "panel anchor metadata");
            check(std::abs(test_element.x + px - (256 * ax + 7 + paused * 13)) < .001f,
                  "dual-screen horizontal anchor/offset");
            check(std::abs(test_element.y + py - (192 * ay - 9 - paused * 11)) < .001f,
                  "dual-screen vertical anchor/offset");
        }
    }
    config = original_config;
    test_context.pause_mode = context.pause_mode = 0;
    st.description = u"Objective";
    st.chest_found = 11;
    st.chest_total = 127;
    st.rank_found = 1;
    st.rank_total = 2;
    st.unity_total = 1;
    draw(st);
    check(program.words[KSI_COUNT] == 4, "counter blocks missing");
    // All icon bounds, including their animation's translation, and text must fit before the next block.
    float previous_start = config.counters_right;
    for (unsigned q = 1; q < 4; ++q) {
        unsigned base = KSI_HEADER + q * KSI_QUAD_WORDS;
        float x0 = word(base), x1 = word(base + 5);
        const auto text = q == 1 ? counter(11, 127) : q == 2 ? counter(0, 1) : counter(1, 2);
        float text_width = float(counter_font.width(text)) * config.counter_scale / .65f;
        check(x1 + config.icon_gap + text_width <= previous_start + .001f, "counter overlaps right-hand block");
        previous_start = x0 - config.counter_gap;
    }
    // The complete row scales around its right/Y anchor, including icon geometry and spacing.
    KhdaysSequenceImage full_row = program;
    for (float scale : {.5f, 1.5f}) {
        config.counters_scale = scale;
        draw(st);
        for (unsigned q = 1; q < 4; ++q)
            for (unsigned v = 0; v < 4; ++v) {
                unsigned base = KSI_HEADER + q * KSI_QUAD_WORDS + v * 5;
                float old_x, old_y;
                std::memcpy(&old_x, full_row.words + base, 4);
                std::memcpy(&old_y, full_row.words + base + 1, 4);
                check(std::abs(word(base) - (config.counters_right + (old_x - config.counters_right) * scale)) < .001f,
                      "counter icons/gaps did not scale together around right anchor");
                check(std::abs(word(base + 1) - (config.counters_y + (old_y - config.counters_y) * scale)) < .001f,
                      "counter icons did not scale vertically around row anchor");
            }
    }
    config.counters_scale = 1;
    auto saved_description = st.description;
    st.description.clear();
    draw(st);
    auto ink_count = [&] {
        size_t count = 0;
        for (auto c : test_images.at(text_image))
            count += (c >> 24) != 0;
        return count;
    };
    auto full_ink = ink_count();
    config.counters_scale = .5f;
    draw(st);
    check(ink_count() > 0 && ink_count() < full_ink, "row scale did not shrink the counter text");
    config.counters_scale = 1;
    st.description = std::move(saved_description);
    draw(st);

    // Counters outside the description's vertical band must not compress its text.
    auto packed_status = st;
    st.chest_total = st.rank_total = st.unity_total = 0;
    draw(st);
    auto without_counters = test_images.at(text_image);
    float counter_y = config.counters_y;
    config.counters_y = 200;
    st = packed_status;
    draw(st);
    check(test_images.at(text_image) == without_counters, "counters on another row squeezed description text");
    float counter_text_offset = config.counter_text_offset_y;
    config.counters_y = -100;
    config.counter_text_offset_y = 300;
    last_text.clear();
    draw(st);
    check(test_images.at(text_image) == without_counters,
          "empty space between separated icon/text rows squeezed description");
    config.counter_text_offset_y = counter_text_offset;
    config.counters_y = counter_y;
    last_text.clear();
    draw(st);
    check(test_images.at(text_image) != without_counters, "same-row counters stopped reserving space");

    // A sized replacement can extend below the NFTR cell, including its one-unit shadow.
    test_tall_font = true;
    std::vector<unsigned> glyph_pixels(32 * 32);
    counter_font.paint(u"1", glyph_pixels, 32, 32, 1, 2, 2, 1, true);
    check(glyph_pixels[13 * 32 + 4] == 0xff00ffffu, "replacement counter bottom was clipped to NFTR height");
    check(glyph_pixels[14 * 32 + 5] == 0xff000000u, "replacement counter shadow was clipped");
    test_tall_font = false;
    st.chest_total = st.rank_total = st.unity_total = 0;
    st.gauge = true;
    st.total = 100;
    st.goal = 60;
    st.done = 40;
    st.displayed = 224;
    draw(st);
    unsigned fill_vertex = KSI_HEADER + 2 * KSI_QUAD_WORDS;
    check(std::abs(word(fill_vertex + 5) - word(fill_vertex) - 229) < .001f, "100% fill left a gap");
    st.displayed = 112;
    draw(st);
    check(std::abs(word(fill_vertex + 5) - word(fill_vertex) - 114.5f) < .001f, "50% fill not proportional");
    config.bar_end_x = config.bar_start_x + 300;
    st.displayed = 224;
    draw(st);
    check(std::abs(word(fill_vertex + 5) - word(fill_vertex) - 300) < .001f,
          "configured endpoints did not stretch track/fill together");
    unsigned marker = KSI_HEADER + 5 * KSI_QUAD_WORDS;
    float expected = config.bar_start_x + 134.f * 300 / 224 + bounds.at(config.minimum_marker).x0;
    check(std::abs(word(marker) - expected) < .001f, "minimum marker did not use native rounding and configured span");
    st.goal = st.total;
    draw(st);
    check(program.words[KSI_COUNT] == 5, "minimum marker remained at a 100% goal");
    config.bar_end_x = 252;
    st = {};
    st.day = true;
    st.tally = u"Missions Left: 4/4";
    draw(st);
    const auto &text_pixels = test_images.at(text_image);
    int last = -1;
    for (int y = 0; y < height * 2; ++y)
        for (int x = 0; x < width * 2; ++x)
            if (text_pixels[size_t(y) * width * 2 + x] >> 24)
                last = std::max(last, x);
    float right_edge = ((config.tally_x - config.source_origin_x) * config.scale - left) * 2;
    check(last >= 0 && last <= right_edge + 2, "missions-left text was not right justified");
    auto original = counter_font.width(u"11/127");
    check(original > counter_font.width(u"1/2"), "multi-digit width not measured");
    test_visible = false;
    step();
    check(owns && closing, "outro disappeared immediately");
    test_paused = true;
    auto old = player.frame();
    step();
    check(player.frame() == old, "paused outro advanced");
    test_paused = false;
    // Once closing starts, destroying the game's context must not invalidate the retained outgoing panel.
    store(0x0207f630u, 0u);
    step();
    check(owns, "outro read a destroyed native context");
    for (int i = 0; i < 12; ++i)
        step();
    check(!owns && !program.words[KSI_CLEAR], "outro failed to end");
    test_visible = true;
    store(0x0207f630u, 0x02100000u);
    step();
    check(owns, "panel failed to reopen");
    test_context.camp_screen = 1;
    step();
    check(!owns, "panel appeared in camp");
    test_context.camp_screen = KHDAYS_CAMP_NONE;
    step();
    check(owns, "return to field failed");
    const auto normal_x = test_element.x, normal_y = test_element.y;
    config.pause_offset_x = 5;
    config.pause_offset_y = 11;
    test_context.pause_mode = 1;
    test_paused = true;
    step();
    check(std::abs(test_element.x - normal_x - 5) < .001f && std::abs(test_element.y - normal_y - 11) < .001f,
          "paused panel position ignored");
    config.pause_only = true;
    test_context.pause_mode = 0;
    test_paused = false;
    step();
    check(owns && (khdays_ui_mission_panel_state() & 4) && !program.words[KSI_CLEAR],
          "pause-only panel did not hide while retaining native suppression");
    test_context.pause_mode = 1;
    test_paused = true;
    step();
    check(owns && !(khdays_ui_mission_panel_state() & 4) && program.words[KSI_CLEAR],
          "pause-only panel did not return on pause");
    test_context.pause_mode = 0;
    test_paused = false;
    config.pause_only = false;
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    preferences[KHDAYS_OPT_MISSION] = 0;
    khdays_hud_settings_publish(preferences);
    step();
    check(!owns && !program.words[KSI_CLEAR], "live panel off suppressed native artwork");
    preferences[KHDAYS_OPT_MISSION] = 1;
    preferences[KHDAYS_OPT_MISSION_VISIBILITY] = 1;
    preferences[KHDAYS_OPT_MISSION_ANCHOR] = 3;
    preferences[KHDAYS_OPT_MISSION_PAUSE_ANCHOR] = 8;
    khdays_hud_settings_publish(preferences);
    step();
    check(owns && !program.words[KSI_CLEAR] && (khdays_ui_mission_panel_state() & 4), "live pause-only preference");
    test_context.pause_mode = 1;
    test_paused = true;
    step();
    check(program.words[KSI_PANEL_ANCHOR] == 8, "saved pause anchor did not override JSON");
    preferences[KHDAYS_OPT_MISSION_VISIBILITY] = 0;
    khdays_hud_settings_publish(preferences);
    test_context.pause_mode = 0;
    test_paused = false;
    step();
    check(program.words[KSI_PANEL_ANCHOR] == 3 && !(khdays_ui_mission_panel_state() & 4),
          "live always/gameplay anchor");
    config.enabled = false;
    asset.reset();
    step();
    check(!owns && !program.words[KSI_CLEAR], "disabled mod suppressed native panel");
}
} // namespace
namespace khdays::vfs {
std::optional<std::filesystem::path> resolve(std::string_view s)
{
    return std::filesystem::path(s);
}
} // namespace khdays::vfs
namespace khdays::assets {
Font decode_nftr(const std::filesystem::path &)
{
    Font f;
    f.cell_width = 6;
    f.cell_height = 8;
    f.line_feed = 8;
    for (char16_t c = 32; c < 127; ++c) {
        f.char_to_glyph[c] = int(f.glyphs.size());
        Font::Glyph g;
        g.left = c == u'1' ? -2 : 0;
        g.width = 5;
        g.advance = 6;
        g.alpha.assign(48, 255);
        f.glyphs.push_back(g);
    }
    return f;
}
} // namespace khdays::assets
extern "C" {
unsigned char *khdays_window;
unsigned khdays_ui_information_epoch(void)
{
    return test_epoch;
}
int khdays_settings_game_language(void)
{
    return 1;
}
int GetLanguage(void)
{
    return 1;
}
int GameState_GetField(int field, int)
{
    return field == 0 ? 10 : 0;
}
const char *khdays_language_code(int)
{
    return "en";
}
int khdays_mods_find(const char *, char *out, int size)
{
    std::snprintf(out, size, "%s", test_config.string().c_str());
    return 1;
}
int khdays_textures_hd(void)
{
    return 1;
}
int khdays_ui_mode(int)
{
    return KHDAYS_UI_ON;
}
void khdays_ui_changed(void) {}
void khdays_log(const char *, ...) {}
int khdays_link_paused(void)
{
    return test_paused;
}
void khdays_situation_take(KhdaysSituationInputs *out, unsigned)
{
    *out = test_context;
}
int khdays_ui_scale(void)
{
    return 2;
}
int khdays_ui_image_get(int w, int h)
{
    test_images.emplace_back(size_t(w) * h);
    return int(test_images.size() - 1);
}
void khdays_ui_image_put(int) {}
unsigned *khdays_ui_image_begin(int i)
{
    return test_images.at(i).data();
}
void khdays_ui_image_end(int)
{
    ++test_uploads;
}
int khdays_ui_image_replacement(int i)
{
    return i;
}
int khdays_textures_add_info(const char *, int, int, const unsigned *, unsigned)
{
    return 99;
}
void khdays_ui_emit(int, const KhdaysUiElement *e)
{
    test_element = *e;
}
void khdays_ui_left_out_cell(int, int, int, int, int)
{
    ++test_cells;
}
int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *l)
{
    *l = {};
    l->wt = l->ht = 32;
    return engine == 1 && (bg == 0 || bg == 2);
}
unsigned khdays_bg_map_entry(int, const KhdaysBgLayer *, int x, int y)
{
    return test_visible && y == 19 && x < 3 ? 0x90u + unsigned(x) : 0;
}
const int *khdays_mission_chests(unsigned short, int *count)
{
    *count = 0;
    return nullptr;
}
unsigned khdays_mission_badges(int, unsigned short, unsigned short)
{
    return 0;
}
KhdaysRomReader *khdays_rom_reader_new(void)
{
    return nullptr;
}
int khdays_rom_file_data(KhdaysRomReader *, const char *, int, unsigned char **, unsigned *)
{
    return 0;
}
int khdays_hd_font_observe(int enabled)
{
    const bool previous_observing = test_font_observing;
    test_font_observing = enabled != 0;
    return previous_observing;
}
int khdays_hd_font_select(const char *name, const char *, char *out, int size)
{
    if (!test_font_observing && !test_font_available)
        return 0;
    const auto route = std::string(name) + ".hd";
    check(route.size() < std::size_t(size), "synthetic scoped font name exceeds output");
    std::memcpy(out, route.c_str(), route.size() + 1);
    return 1;
}
void khdays_hd_font_bind_charmap(const char *, const unsigned *, int) {}
double khdays_hd_font_metric_f(const char *, int, int field, double fallback)
{
    return test_tall_font && field == KHDF_CELL_HEIGHT ? 12 : fallback;
}
double khdays_hd_font_kerning_f(const char *, unsigned, unsigned)
{
    return -.25;
}
int khdays_hd_font_paint(const char *, int, double x, double y, int, int, unsigned tint, unsigned *out, int w, int h,
                         int resolution)
{
    if (!test_tall_font)
        return 0;
    for (int py = int(y * resolution); py < int((y + 12) * resolution); ++py)
        for (int px = int(x * resolution); px < int((x + 5) * resolution); ++px)
            if (px >= 0 && px < w && py >= 0 && py < h)
                out[size_t(py) * w + px] = tint;
    return 1;
}
}
int main(int argc, char **argv)
{
    try {
        setup();
        if (argc == 2) {
            asset.reset();
            tried = false;
            std::ofstream(test_config) << (std::string(argv[1]) == "--legacy" ? "{}"
                                           : std::string(argv[1]) == "--disabled"
                                               ? R"({"mission_panel":{"enabled":false}})"
                                               : R"({"mission_panel":{"enabled":true}})");
            step();
            check(!owns && !program.words[KSI_CLEAR], "unavailable replacement suppressed native panel");
        } else if (argc == 3 && std::string(argv[1]) == "--asset") {
            asset.reset();
            bounds.clear();
            tried = false;
            test_config = argv[2];
            load();
            check(asset.has_value(), "mod asset rejected");
            step();
            for (int i = 0; i < 9; ++i)
                step();
            check(owns, "mod asset did not draw");
        } else
            tests();
        std::filesystem::remove(temp_config);
        delete[] khdays_window;
        std::cout << "Mission panel ownership, fonts, counter packing and transitions passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        std::filesystem::remove(temp_config, ec);
        return 1;
    }
}
