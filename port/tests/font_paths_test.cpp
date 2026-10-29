// The real metadata loader must treat NitroFS root-prefixed font paths as
// the same resource, while retaining subtree and metadata source validation.
#include "../runtime/mods/fonts.cpp"
#include "../runtime/mods/font_runtime.cpp"
#include <clocale>
#include <cstdlib>
#include <cstring>

static int lookups;
static bool mapping_file;
static bool source_font_file;
static bool layout_file;
static int png_w = 8, png_h = 8;
static std::string mod_directory = "build/tests/", last_png_path;
extern "C" int khdays_mods_find(const char *relative, char *path, int size)
{
    ++lookups;
    const char *name = nullptr;
    if (layout_file && std::strcmp(relative, "fonts/layouts.json") == 0)
        name = "font-layouts.json";
    else if (source_font_file && std::strcmp(relative, "fonts/text/small.nftr.png") == 0)
        name = "font-source.nftr.png";
    else if (mapping_file && std::strcmp(relative, "fonts/mappings.json") == 0)
        name = "font-mappings.json";
    else if (std::strcmp(relative, "fonts/text/paths.nftr.png") == 0)
        name = "font-paths.nftr.png";
    if (!name)
        return 0;
    const std::string file = mod_directory + name;
    return std::snprintf(path, static_cast<std::size_t>(size), "%s", file.c_str()) < size;
}
extern "C" std::uint8_t *khdays_png_read(const char *path, int *w, int *h, const char **)
{
    last_png_path = path;
    *w = png_w;
    *h = png_h;
    auto *pixels = static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(*w) * *h * 4));
    std::memset(pixels, 255, static_cast<std::size_t>(*w) * *h * 4);
    return pixels;
}
extern "C" void khdays_png_free(std::uint8_t *p)
{
    std::free(p);
}

static bool original_fallback(const char *json)
{
    using namespace khdays::hd_fonts;
    const std::string entries = R"("glyphs":[
        {"index":0,"x":0,"y":0,"w":8,"h":8,"left":-2,"width":8,"advance":12},
        {"index":1,"fallback":"original","left":999,"width":999,"advance":999},
        {"index":2,"x":0,"y":0,"w":8,"h":8,"left":3,"width":8,"advance":14},
        {"index":3,"fallback":"original"},{"index":4,"fallback":"original"}],
        "kerning":[{"left":65,"right":67,"adjust":-2},
        {"left":65,"right":66,"adjust":-9},{"left":66,"right":67,"adjust":-9},
        {"left":68,"right":69,"adjust":-9}])";
    const std::string prefix = R"({"source":"text/paths.nftr","format":"NFTR",
        "atlas":{"width":8,"height":8},"cell_width":8,"cell_height":8,
        "baseline":8,"line_feed":10,"max_width":8,)";
    const std::string map = R"("charmap":[{"codepoint":65,"glyph":0},{"codepoint":66,"glyph":1},
        {"codepoint":67,"glyph":2},{"codepoint":68,"glyph":3},{"codepoint":69,"glyph":4}],)";
    bool ok = true;
    png_w = png_h = 8;
    for (const bool sized : {false, true}) {
        for (const bool mapped : {false, true}) {
            const std::string text =
                prefix + (sized ? "\"scale\":0.5," : "") + (mapped ? map : "\"charmap\":[],") + entries + "}";
            std::ofstream(json) << text;
            cache.clear();
            auto atlas = load("text/paths.nftr");
            ok = ok && atlas && atlas->glyphs.size() == 5 && atlas->glyphs[1].original && atlas->glyphs[3].original &&
                 atlas->glyphs[4].original;
            const std::uint32_t codes[] = {65, 66, 67, 68, 69};
            ok = ok && !khdays_hd_font_map_ready("text/paths.nftr");
            khdays_hd_font_bind_charmap("text/paths.nftr", codes, 5);
            ok = ok && khdays_hd_font_map_ready("text/paths.nftr");
            for (int i = 0; i < 5; ++i) {
                const bool native = i == 1 || i >= 3;
                ok = ok && khdays_hd_font_glyph_available("text/paths.nftr", i) == !native;
                std::uint32_t pixels[64] = {0};
                ok = ok &&
                     khdays_hd_font_paint("text/paths.nftr", i, 0, 0, 8, 8, 0xffffffffu, pixels, 8, 8, 1) == !native;
                ok = ok && pixels[0] == (native ? 0 : 0xffffffffu);
                if (native) {
                    for (int field = 0; field <= KHDF_CELL_HEIGHT; ++field)
                        ok = ok && khdays_hd_font_metric("text/paths.nftr", i, field, 7) == 7;
                    int geometry[3] = {-1, -1, -1};
                    ok = ok && !khdays_hd_font_geometry("text/paths.nftr", i, 8, 8, 4, geometry) && geometry[0] == -1 &&
                         khdays_hd_font_baseline("text/paths.nftr", i, 6) == 0 &&
                         khdays_hd_font_extent("text/paths.nftr", i, 2, 4, 9) == (sized ? 6 : 9);
                }
            }
            ok = ok && khdays_hd_font_metric("text/paths.nftr", 2, KHDF_ADVANCE, 6) == (sized ? 7 : 14) &&
                 khdays_hd_font_kerning("text/paths.nftr", 'A', 'C') == (sized ? -1 : -2) &&
                 khdays_hd_font_kerning("text/paths.nftr", 'A', 'B') == 0 &&
                 khdays_hd_font_kerning("text/paths.nftr", 'B', 'C') == 0 &&
                 khdays_hd_font_kerning("text/paths.nftr", 'D', 'E') == 0;
            if (mapped) {
                // Original NFTR indices can also be reordered relative to JSON indices.
                cache.clear();
                const std::uint32_t reordered[] = {67, 66, 65, 69, 68};
                khdays_hd_font_bind_charmap("text/paths.nftr", reordered, 5);
                ok = ok && khdays_hd_font_metric("text/paths.nftr", 0, KHDF_ADVANCE, 6) == (sized ? 7 : 14) &&
                     khdays_hd_font_metric("text/paths.nftr", 2, KHDF_ADVANCE, 6) == (sized ? 6 : 12) &&
                     !khdays_hd_font_glyph_available("text/paths.nftr", 1);
            }
        }
    }
    for (const char *bad : {"\"native\"", "true", "null"}) {
        std::string text = prefix + "\"charmap\":[]," + entries + "}";
        text.replace(text.find("\"original\""), std::strlen("\"original\""), bad);
        std::ofstream(json) << text;
        bool rejected = false;
        try {
            (void)read_atlas("text/paths.nftr", "build/tests/font-paths.nftr.png");
        } catch (const std::exception &) {
            rejected = true;
        }
        ok = ok && rejected;
    }
    return ok;
}

static bool source_symbols_test(const char *json)
{
    using namespace khdays::hd_fonts;
    const std::string header = R"({"source":"text/paths.nftr","format":"NFTR","scale":0.5,
        "atlas":{"width":8,"height":8},"cell_width":8,"cell_height":8,
        "baseline":6,"line_feed":8,"max_width":8,
        "glyphs":[{"index":0,"x":0,"y":0,"w":8,"h":8,"left":0,"width":8,"advance":12}],)";
    const std::string target = header + R"("charmap":[{"codepoint":65,"glyph":0},
        {"codepoint":65324,"glyph":0},{"codepoint":65346,"glyph":0}]})";
    std::ofstream(json) << target;
    std::string source = header + R"("charmap":[{"codepoint":65,"glyph":0}],
        "original_codepoints":[65324,65346]})";
    source.replace(source.find("text/paths.nftr"), std::strlen("text/paths.nftr"), "text/small.nftr");
    const char *source_json = "build/tests/font-source.nftr.json";
    std::ofstream(source_json) << source;
    cache.clear();
    routed.clear();
    rules.clear();
    rules_read = true;
    rules["camp"]["text/small.nftr"] = "text/paths.nftr";
    rules["camp"]["text/other.nftr"] = "text/paths.nftr";
    source_font_file = true;
    png_w = png_h = 8;
    auto target_font = std::make_shared<Atlas>(*load("text/paths.nftr"));
    target_font->secondary = std::make_shared<Atlas>(*target_font);
    cache.at("text/paths.nftr").font = target_font;
    const auto routed_source = select("text/small.nftr", "camp");
    const auto routed_other = select("text/other.nftr", "camp");
    const std::uint32_t codes[] = {65, 65324, 65346, 65};
    bool ok = !routed_source.empty() && !routed_other.empty();
    for (const auto &path :
         {std::string("text/small.nftr"), routed_source, routed_other, std::string("text/paths.nftr")}) {
        khdays_hd_font_bind_charmap(path.c_str(), codes, 4);
        const bool native = path == "text/small.nftr" || path == routed_source;
        ok = ok && khdays_hd_font_glyph_available(path.c_str(), 0) && khdays_hd_font_glyph_available(path.c_str(), 3);
        for (int i : {1, 2})
            ok = ok && khdays_hd_font_glyph_available(path.c_str(), i) == !native &&
                 khdays_hd_font_metric_f(path.c_str(), i, KHDF_ADVANCE, 7) == (native ? 7 : 6);
    }
    for (const char *bad : {"true", "[true]", "[-1]", "[1114112]", "[65,65]"}) {
        std::ofstream(source_json) << source.substr(0, source.find("\"original_codepoints\"")) +
                                          "\"original_codepoints\":" + bad + "}";
        bool rejected = false;
        try {
            (void)read_atlas("text/small.nftr", "build/tests/font-source.nftr.png");
        } catch (const std::exception &) {
            rejected = true;
        }
        ok = ok && rejected;
    }
    source_font_file = false;
    std::remove(source_json);
    return ok;
}

static bool mapping_rules_test(const char *json)
{
    using namespace khdays::hd_fonts;
    std::ofstream(json) << R"({"source":"text/paths.nftr","format":"NFTR","scale":0.35,
        "atlas":{"width":8,"height":8},"cell_width":8,"cell_height":8,
        "baseline":6,"line_feed":8,"max_width":8,
        "glyphs":[{"index":0,"x":0,"y":0,"w":4,"h":8,"left":-1,"width":4,"advance":9},
        {"index":1,"fallback":"original"}],
        "charmap":[{"codepoint":65,"glyph":0},{"codepoint":66,"glyph":1}],
        "kerning":[{"left":65,"right":66,"adjust":-1}]})";
    std::ofstream("build/tests/font-mappings.json") << R"({"version":1,"scopes":{
        "camp":{"text/small.nftr":"/text/paths.NFTR","text/other.nftr":"text/paths.nftr"},
        "camp.story.screen.2":{"text/small.nftr":"text/small.nftr"},
        "camp.story.screen.1.mission_select.summary":{"text/small.nftr":"text/small.nftr"},
        "camp.story.screen.1.mission_select.objectives":{"text/small.nftr":"text/small.nftr"},
        "camp.story.screen.1.mission_select.rewards":{"text/small.nftr":"text/small.nftr"},
        "camp.save":{"text/small.nftr":"text/absent.nftr"},
        "field.tutorial":{"text/small.nftr":"text/paths.nftr"},
        "shop":{"text/small.nftr":"text/paths.nftr"},
        "field.obtained":{"text/small.nftr":"text/paths.nftr"},
        "field.hud":{"text/small.nftr":"text/paths.nftr"},
        "field.hud.area":{"text/small.nftr":"text/small.nftr"},
        "mission.results":{"text/small.nftr":"text/paths.nftr"}}})";
    cache.clear();
    routed.clear();
    rules.clear();
    rules_read = false;
    mapping_file = true;
    png_w = png_h = 8;
    char selected[160] = {0}, second[160] = {0};
    bool ok = khdays_hd_font_select("text/small.nftr", "camp.story.screen.0", selected, sizeof(selected)) &&
              khdays_hd_font_select("text/other.nftr", "camp.mission.screen.2", second, sizeof(second)) &&
              std::strcmp(selected, second) != 0;
    const std::uint32_t first_codes[] = {65, 66, 67};
    const std::uint32_t second_codes[] = {67, 65, 66};
    khdays_hd_font_bind_charmap(selected, first_codes, 3);
    khdays_hd_font_bind_charmap(second, second_codes, 3);
    auto first = load(selected);
    auto other = load(second);
    ok = ok && first && other && &first->pixels() == &other->pixels() && first->rgba.empty() && other->rgba.empty() &&
         khdays_hd_font_glyph_available(selected, 0) && !khdays_hd_font_glyph_available(selected, 1) &&
         !khdays_hd_font_glyph_available(selected, 2) && !khdays_hd_font_glyph_available(second, 0) &&
         khdays_hd_font_glyph_available(second, 1) && !khdays_hd_font_glyph_available(second, 2) &&
         std::abs(khdays_hd_font_metric_f(selected, 0, KHDF_ADVANCE, 8) - 3.15) < 1e-12 &&
         khdays_hd_font_metric_f(selected, 1, KHDF_ADVANCE, 8) == 8 &&
         khdays_hd_font_kerning_f(selected, 65, 66) == 0 &&
         !khdays_hd_font_select("text/small.nftr", "dialogue", second, sizeof(second)) &&
         !khdays_hd_font_select("text/small.nftr", "camp.story.screen.2", second, sizeof(second)) &&
         !khdays_hd_font_select("text/small.nftr", "camp.save", second, sizeof(second)) &&
         !khdays_hd_font_select("text/small.nftr", "camp", second, 2);
    std::uint32_t pixels[64] = {0};
    ok = ok && khdays_hd_font_paint(selected, 0, .25, .25, 3, 3, 0xffffffffu, pixels, 8, 8, 1);
    const int before = lookups;
    for (const char *role : {"instruction", "world", "header", "summary", "objectives", "rewards", "buttons"}) {
        const std::string scope = std::string("camp.story.screen.1.mission_select.") + role;
        const bool body = std::strcmp(role, "summary") == 0 || std::strcmp(role, "objectives") == 0 ||
                          std::strcmp(role, "rewards") == 0;
        const int mapped = khdays_hd_font_select("text/small.nftr", scope.c_str(), second, sizeof(second));
        ok = ok && (body ? !mapped : mapped && std::strcmp(selected, second) == 0);
    }
    ok = ok && khdays_hd_font_select("text/small.nftr", "field.tutorial", second, sizeof(second)) &&
         std::strcmp(selected, second) == 0 &&
         !khdays_hd_font_select("text/small.nftr", "field.dialogue", second, sizeof(second));
    for (const char *scope : {"field.hud.information", "field.hud.status", "mission.results.summary",
                              "mission.results.rewards", "shop", "field.obtained"})
        ok = ok && khdays_hd_font_select("text/small.nftr", scope, second, sizeof(second)) &&
             std::strcmp(selected, second) == 0;
    ok = ok && !khdays_hd_font_select("text/small.nftr", "field.hud.area", second, sizeof(second)) &&
         !khdays_hd_font_select("text/small.nftr", "field.command", second, sizeof(second)) &&
         !khdays_hd_font_select("text/small.nftr", "field.dialogue", second, sizeof(second));
    for (int i = 0; i < 100; ++i)
        ok = ok && khdays_hd_font_select("text/small.nftr", "camp.mission.screen.5", second, sizeof(second)) &&
             std::strcmp(selected, second) == 0;
    ok = ok && lookups == before && first->original_glyphs[0] == 0; // No repeated file probes or remapping.
    for (const char *bad :
         {R"({"version":2,"scopes":{}})", R"({"version":1,"scopes":{"camp":{"text/a.nftr":"../escape.nftr"}}})",
          R"({"version":1,"scopes":{"camp":{"text/a.nftr":12}}})", R"({"version":1,"scopes":{"camp..bad":{}}})",
          R"({"version":1,"scopes":{"camp":{"text/a.nftr":"text/a.nftr\u0000other.nftr"}}})",
          R"({"version":1,"scopes":{"camp":{"text/a.nftr":"text/\bother.nftr"}}})"}) {
        bool rejected = false;
        try {
            (void)read_rules(Reader(bad).read());
        } catch (const std::exception &) {
            rejected = true;
        }
        ok = ok && rejected;
    }
    std::filesystem::remove("build/tests/font-mappings.json");
    std::printf("HD font scoped mappings: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool json_reader_test()
{
    using namespace khdays::hd_fonts;
    bool ok = Reader("0.35").read().real == 0.35 && Reader("-1024").read().number == -1024 &&
              Reader(R"("\uD83D\uDE00")").read().str == "\xf0\x9f\x98\x80";
    for (const char *bad : {R"("\uD800")", R"("\uDC00")", R"("\uD800\u0041")", "1e1000", "9223372036854775808", "0.35x",
                            "01", "1e+", "[1,]"}) {
        bool rejected = false;
        try {
            (void)Reader(bad).read();
        } catch (const std::exception &) {
            rejected = true;
        }
        ok = ok && rejected;
    }
    const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
    for (const char *locale : {"German_Germany.1252", "de_DE.UTF-8", "de_DE"}) {
        if (std::setlocale(LC_NUMERIC, locale)) {
            const bool numeric = Reader("0.35").read().real == 0.35 && Reader("3.5e-1").read().real == 0.35;
            ok = numeric && ok;
            std::printf("HD font JSON decimal-comma locale: %s\n", numeric ? "PASS" : "FAIL");
            break;
        }
    }
    std::setlocale(LC_NUMERIC, previous.c_str());
    std::printf("HD font JSON reader: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool layout_rules_test()
{
    using namespace khdays::hd_fonts;
    const auto parsed = read_layouts(Reader(R"({"version":1,"scopes":{
        "camp":"font_metrics","camp.save":"original",
        "camp.save.play_time":"font_metrics","title.load.play_time":"font_metrics"}})")
                                         .read());
    bool ok = layout_for(parsed, "camp.panels.inventory.counter") && !layout_for(parsed, "camp.save.other") &&
              layout_for(parsed, "camp.save.play_time") && !layout_for(parsed, "field") && !layout_for({}, "camp");
    for (const char *invalid :
         {R"({"version":2,"scopes":{}})", R"({"scopes":{"camp":true}})", R"({"scopes":{"camp":"unknown"}})",
          R"({"scopes":{"camp..save":"original"}})",
          R"({"scopes":{"field.command.items.name":{"mode":"fit","max_width":0}}})",
          R"({"scopes":{"field.command.items.name":{"mode":"fit","max_width":65}}})",
          R"({"scopes":{"field.command.items.name":{"mode":"fit","max_width":53,"x_offset":12}}})",
          R"({"scopes":{"field.command.items.name":{"mode":"fit","max_width":true}}})"}) {
        bool rejected = false;
        try {
            (void)read_layouts(Reader(invalid).read());
        } catch (const std::exception &) {
            rejected = true;
        }
        ok = ok && rejected;
    }
    mod_directory = "build/tests/\xe6\xb5\x8b\xe8\xaf\x95/";
    const auto folder = u8(mod_directory);
    std::filesystem::create_directories(folder);
    const auto file = folder / "font-layouts.json";
    std::ofstream(file) << R"({"version":1,"scopes":{"camp":"font_metrics","camp.save":"original",
        "field.command.items.name":{"mode":"fit","max_width":53.5,"x_offset":1.25},
        "field.obtained":{"mode":"fit","max_width":32}}})";
    layout_file = true;
    ok = ok && khdays_hd_font_numeric_layout(KHDF_LAYOUT_PANEL_COUNTER) &&
         !khdays_hd_font_numeric_layout(KHDF_LAYOUT_LOAD_TIME) &&
         !khdays_hd_font_numeric_layout(KHDF_LAYOUT_SAVE_TIME) && !khdays_hd_font_numeric_layout(-1) &&
         !khdays_hd_font_numeric_layout(KHDF_LAYOUT_COUNT);
    const int after_load = lookups;
    double offset = 0;
    ok = ok && khdays_hd_font_fit_layout(KHDF_FIT_ITEMS, &offset) == 53.5 && offset == 1.25 &&
         khdays_hd_font_fit_layout(KHDF_FIT_OBTAINED, &offset) == 0 && offset == 0 &&
         khdays_hd_font_fit_layout(-1, &offset) == 0;
    for (int i = 0; i < 10000; ++i)
        ok = ok && khdays_hd_font_numeric_layout(KHDF_LAYOUT_PANEL_COUNTER);
    ok = ok && lookups == after_load;
    layout_file = false;
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
    mod_directory = "build/tests/";
    return ok;
}

int main()
{
    const char *json = "build/tests/font-paths.nftr.json";
    const std::string metadata = R"({"source":"text/paths.nftr","format":"NFTR",
        "atlas":{"width":8,"height":8},"cell_width":8,"cell_height":8,
        "baseline":0,"line_feed":17,"max_width":8,"glyphs":[
        {"index":0,"x":0,"y":0,"w":8,"h":8,"left":-130,"width":512,"advance":300}],"charmap":[],
        "kerning":[{"left":65,"right":86,"adjust":-2}]})";
    std::ofstream(json) << metadata;
    using namespace khdays::hd_fonts;
    auto a = load("/text/paths.nftr");
    bool ok = json_reader_test();
    ok = ok && a && a == load("text/paths.nftr") && a == load("\\text\\paths.nftr") && probe("/text/paths.nftr") == 1 &&
         lookups == 2 && a == load("/text/paths.NFTR") && a == load("text/paths.NfTr");
    ok = ok && !load("/../escape.nftr") && !load("C:/escape.nftr") && lookups == 2;
    ok = ok && khdays_hd_font_metric("text/paths.nftr", 0, KHDF_LEFT, 8) == -130 &&
         khdays_hd_font_metric("text/paths.nftr", 0, KHDF_WIDTH, 8) == 512 &&
         khdays_hd_font_metric("text/paths.nftr", 0, KHDF_ADVANCE, 8) == 300 &&
         khdays_hd_font_metric("text/paths.nftr", -1, KHDF_LINE_FEED, 8) == 17 &&
         khdays_hd_font_metric("text/paths.nftr", 1, KHDF_ADVANCE, 8) == 8 &&
         khdays_hd_font_kerning("text/paths.nftr", 'A', 'V') == -2 &&
         khdays_hd_font_kerning("text/paths.nftr", 'V', 'A') == 0;
    ok = ok && khdays_hd_font_baseline("text/paths.nftr", 0, 6) == 0 &&
         khdays_hd_font_extent("text/paths.nftr", 0, 0, 8, 8) == 300 &&
         khdays_hd_font_extent("text/paths.nftr", 0, -130, 512, 8) == 300 &&
         khdays_hd_font_extent("text/paths.nftr", 1, 0, 8, 8) == 8;
    for (const int width : {0, 4, 12}) {
        std::string edited = metadata;
        const std::string original = "\"left\":-130,\"width\":512,\"advance\":300";
        edited.replace(edited.find(original), original.size(),
                       "\"left\":10,\"width\":" + std::to_string(width) + ",\"advance\":6");
        std::ofstream(json) << edited;
        cache.clear(); // Simulate restarting after editing the JSON.
        ok = ok && khdays_hd_font_metric("text/paths.nftr", 0, KHDF_LEFT, 1) == 10 &&
             khdays_hd_font_extent("text/paths.nftr", 0, 1, 4, 6) == 6;
    }
    // Replacement CMAP order can differ from NFTR; no bitmap matching is involved.
    std::string remapped = metadata;
    remapped.replace(remapped.find("\"charmap\":[]"), std::strlen("\"charmap\":[]"),
                     "\"charmap\":[{\"codepoint\":65,\"glyph\":0}]");
    std::ofstream(json) << remapped;
    cache.clear();
    const std::uint32_t original_codes[] = {90, 65, 0xff21};
    ok = ok && !khdays_hd_font_map_ready("text/paths.nftr");
    ok = ok && !khdays_hd_font_glyph_available("text/paths.nftr", 0) &&
         khdays_hd_font_metric("text/paths.nftr", 0, KHDF_ADVANCE, 8) == 8;
    khdays_hd_font_bind_charmap("text/paths.nftr", original_codes, 3);
    ok = ok && khdays_hd_font_map_ready("text/paths.nftr") && khdays_hd_font_glyph_available("text/paths.nftr", 1) &&
         !khdays_hd_font_glyph_available("text/paths.nftr", 0) &&
         !khdays_hd_font_glyph_available("text/paths.nftr", 2) &&
         khdays_hd_font_metric("text/paths.nftr", 1, KHDF_ADVANCE, 8) == 300 &&
         khdays_hd_font_metric("text/paths.nftr", 0, KHDF_ADVANCE, 8) == 8 &&
         khdays_hd_font_extent("text/paths.nftr", 1, 0, 4, 8) == 300;
    int remap_geometry[3] = {0};
    ok = ok && khdays_hd_font_geometry("text/paths.nftr", 1, 8, 8, 4, remap_geometry) && remap_geometry[0] == 8 &&
         !khdays_hd_font_geometry("text/paths.nftr", 0, 8, 8, 4, remap_geometry);
    std::uint32_t remap_pixels[64] = {0};
    ok = ok && khdays_hd_font_paint("text/paths.nftr", 1, 0, 0, 8, 8, 0xffffffffu, remap_pixels, 8, 8, 1) &&
         remap_pixels[0] == 0xffffffffu &&
         !khdays_hd_font_paint("text/paths.nftr", 0, 0, 0, 8, 8, 0xffffffffu, remap_pixels, 8, 8, 1);
    std::string wrong_source = metadata;
    const auto source_at = wrong_source.find("text/paths.nftr");
    wrong_source.replace(source_at, std::strlen("text/paths.nftr"), "text/pathss.nftr");
    std::ofstream(json) << wrong_source;
    bool rejected = false;
    try {
        (void)read_atlas("text/paths.nftr", "build/tests/font-paths.nftr.png");
    } catch (const std::exception &) {
        rejected = true;
    }
    ok = ok && rejected;
    const std::string kh2 = R"({"source":"text/paths.nftr","format":"NFTR","scale":0.5,
        "atlas":{"width":18,"height":24},"cell_width":18,"cell_height":24,
        "baseline":18,"line_feed":24,"max_width":18,"glyphs":[
        {"index":0,"x":0,"y":0,"w":18,"h":24,"left":-2,"width":16,"advance":18}],"charmap":[],
        "kerning":[{"left":65,"right":86,"adjust":-2}]})";
    std::ofstream(json) << kh2;
    cache.clear();
    png_w = 36;
    png_h = 48;
    auto native = load("text/paths.nftr");
    int geometry[3] = {0};
    ok = ok && native && native->sized && native->scale == 0.5 && native->baseline_offset == 0 &&
         khdays_hd_font_baseline("text/paths.nftr", 0, 6) == -3 &&
         khdays_hd_font_baseline("text/paths.nftr", 1, 6) == 0 &&
         khdays_hd_font_geometry("text/paths.nftr", 0, 7, 8, 4, geometry) && geometry[0] == 9 && geometry[1] == 12 &&
         geometry[2] == 8 && khdays_hd_font_metric("text/paths.nftr", 0, KHDF_LEFT, 0) == -1 &&
         khdays_hd_font_metric("text/paths.nftr", 0, KHDF_ADVANCE, 0) == 9 &&
         khdays_hd_font_metric("text/paths.nftr", -1, KHDF_LINE_FEED, 0) == 12 &&
         khdays_hd_font_kerning("text/paths.nftr", 'A', 'V') == -1 &&
         khdays_hd_font_extent("text/paths.nftr", 0, 0, 4, 6) == 7 &&
         khdays_hd_font_extent("text/paths.nftr", 1, 1, 4, 6) == 5;
    std::vector<std::uint32_t> first(32 * 32), doubled(32 * 32);
    ok = ok && khdays_hd_font_paint("text/paths.nftr", 0, 0, 0, 7, 8, 0xffffffffu, first.data(), 32, 32, 1);
    ok = ok && first[11 * 32 + 7] == 0xffffffffu && first[8] == 0 && first[12 * 32] == 0;
    cache.clear();
    png_w = 72;
    png_h = 96;
    ok = ok && khdays_hd_font_paint("text/paths.nftr", 0, 0, 0, 7, 8, 0xffffffffu, doubled.data(), 32, 32, 1);
    ok = ok && first == doubled;
    for (int offset : {-3, 4}) {
        std::string edited = kh2;
        edited.insert(edited.find("\"baseline\""), "\"baseline_offset\":" + std::to_string(offset) + ",");
        std::ofstream(json) << edited;
        cache.clear();
        ok = ok && khdays_hd_font_baseline("text/paths.nftr", 0, 6) == -3 + offset;
        edited.erase(edited.find("\"scale\":"), std::strlen("\"scale\":0.5,"));
        std::ofstream(json) << edited;
        cache.clear();
        ok = ok && khdays_hd_font_baseline("text/paths.nftr", 0, 6) == 0;
    }
    for (const char *invalid : {"0", "-1", "17", "1e1000", "true"}) {
        std::string bad = kh2;
        bad.replace(bad.find("0.5"), 3, invalid);
        std::ofstream(json) << bad;
        bool bad_scale = false;
        try {
            (void)read_atlas("text/paths.nftr", "build/tests/font-paths.nftr.png");
        } catch (const std::exception &) {
            bad_scale = true;
        }
        ok = ok && bad_scale;
    }
    std::string fractional = kh2;
    fractional.replace(fractional.find("0.5"), 3, "0.35");
    std::ofstream(json) << fractional;
    cache.clear();
    png_w = 36;
    png_h = 48;
    ok = ok && std::abs(khdays_hd_font_metric_f("text/paths.nftr", 0, KHDF_ADVANCE, 0) - 6.3) < 1e-12 &&
         std::abs(khdays_hd_font_metric_f("text/paths.nftr", 0, KHDF_LEFT, 0) + .7) < 1e-12 &&
         std::abs(khdays_hd_font_metric_f("text/paths.nftr", 0, KHDF_WIDTH, 0) - 5.6) < 1e-12 &&
         std::abs(khdays_hd_font_kerning_f("text/paths.nftr", 'A', 'V') + .7) < 1e-12 &&
         std::abs(khdays_hd_font_baseline_f("text/paths.nftr", 0, 8) - 1.7) < 1e-12;
    png_w = 72;
    png_h = 96;
    cache.clear();
    ok = ok && std::abs(khdays_hd_font_metric_f("text/paths.nftr", 0, KHDF_ADVANCE, 0) - 6.3) < 1e-12;
    ok = original_fallback(json) && ok;
    ok = source_symbols_test(json) && ok;
    ok = mapping_rules_test(json) && ok;
    ok = layout_rules_test() && ok;
    // SDL mod paths and the PNG reader are UTF-8, including outside the Windows ANSI codepage.
    mod_directory = "build/tests/\xe6\xb5\x8b\xe8\xaf\x95/";
    const auto unicode_dir = u8(mod_directory);
    std::filesystem::create_directories(unicode_dir);
    std::ofstream(unicode_dir / "font-paths.nftr.json") << metadata;
    std::ofstream(unicode_dir / "font-mappings.json") << R"({"version":1,"scopes":{"camp":{
        "text/small.nftr":"text/paths.nftr"}}})";
    cache.clear();
    rules.clear();
    rules_read = false;
    mapping_file = true;
    png_w = png_h = 8;
    const auto unicode = load("text/paths.nftr");
    ok = ok && unicode && unicode->png_path == mod_directory + "font-paths.nftr.png" &&
         last_png_path == unicode->png_path && mappings_enabled();
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    preferences[KHDAYS_OPT_FONT] = 0;
    khdays_hud_settings_publish(preferences);
    const uint32_t codes[] = {'A', 'B'};
    khdays_hd_font_bind_charmap("text/paths.nftr", codes, 2);
    ok = ok && unicode->codes_bound;
    ok = ok && !load("text/paths.nftr") && probe("text/paths.nftr") == 0 && !mappings_enabled() &&
         select("text/small.nftr", "camp").empty() && !numeric_layout(KHDF_LAYOUT_PANEL_COUNTER);
    const int previous_observation = khdays_hd_font_observe(1);
    ok = ok && load("text/paths.nftr") == unicode && mappings_enabled() && !select("text/small.nftr", "camp").empty();
    khdays_hd_font_observe(previous_observation);
    ok = ok && !load("text/paths.nftr");
    preferences[KHDAYS_OPT_FONT] = 1;
    khdays_hud_settings_publish(preferences);
    ok = ok && load("text/paths.nftr") == unicode && mappings_enabled();
    std::filesystem::remove(unicode_dir / "font-paths.nftr.json");
    std::filesystem::remove(unicode_dir / "font-mappings.json");
    std::filesystem::remove(unicode_dir);
    mod_directory = "build/tests/";
    std::filesystem::remove(json);
    std::printf("HD font paths: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
