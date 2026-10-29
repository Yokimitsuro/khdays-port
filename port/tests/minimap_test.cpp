// Minimap capture retains native fallback, exact directional art and optional continuous rotation.
#include "../ui/minimap.cpp"
#include <fstream>
#include <iostream>
namespace {
unsigned tick = 0;
int test_hd = 1;
KhdaysSituationInputs test_context{};
KhdaysUiElement test_element{};
std::filesystem::path settings;
std::vector<std::vector<unsigned>> images;
bool test_terrain = false, test_hd_map = false;
unsigned terrain_uploads = 0;
unsigned hd_map_version = 1;
std::vector<unsigned char> resident_map(32);
unsigned map_cell_flip = 0;
bool resident_source = true;
std::vector<unsigned> hd_map_pixels(16 * 16, 0xff123456u);
KhdaysTextureReplacement hd_map{16, 16, hd_map_pixels.data(), KHDAYS_REPLACEMENT_TILES | 2u << 8 | 1u << 16};
void check(bool c, const char *why)
{
    if (!c)
        throw std::runtime_error(why);
}
void store(unsigned p, unsigned v)
{
    std::memcpy(KHDAYS_W(p), &v, 4);
}
float value(unsigned word)
{
    float f;
    std::memcpy(&f, programs[0].words + word, 4);
    return f;
}
void step()
{
    khdays_minimap_frame(++tick);
    khdays_minimap_emit();
}
void setup()
{
    khdays_window = new unsigned char[0x02400000]{};
    store(0x0207f638, 0x02100000);
    store(0x02100018, 0x02102000);
    store(0x02102014, 0x02103000);
    store(0x0207f614, 0x02101000);
    store(0x02101130, 0);
    store(0x02101134, 7);
    test_context.scene = 2;
    test_context.overlays = 4;
    test_context.camp_screen = KHDAYS_CAMP_NONE;
    settings = std::filesystem::temp_directory_path() / "khdays-minimap-test.json";
    std::ofstream(settings)
        << R"({"enabled":true,"players":{"1":{"smooth_rotation":true}},"sprites":{"player1_0":{"x":0,"y":0,"width":8,"height":8}}})";
    config = cfg::load_config(settings);
    tried = loaded = true;
    atlas = 99;
    for (auto name : {"entity_0", "entity_1", "marker_120", "emblem1", "player1_1", "Axel"})
        config.sprites[name] = {};
    config.sprites["Axel"].x = 32;
}
void tests()
{
    check(config.players[0].smooth_rotation, "smooth setting ignored");
    for (const char *bad : {R"({"logical_width":0})", R"({"texture":"../bad.png"})",
                            R"({"sprites":{"bad":{"width":1000}}})", R"({"players":{"5":{"smooth_rotation":true}}})",
                            R"({"enabled":true,"enabled":false})", R"({"players":{"1":{"sprite":"../bad.png"}}})"}) {
        std::ofstream(settings) << bad;
        bool rejected = false;
        try {
            cfg::load_config(settings);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "invalid settings accepted");
    }
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 4096);
    khdays_minimap_end();
    step();
    check(owns && test_element.screen == 1 && test_element.x == 104 && test_element.y == 56, "icon layer misplaced");
    check(programs[0].words[KSI_COUNT] == 1, "player not captured");
    check((test_element.slot & (KES_PICTURE | KES_NATIVE_FADE)) == (KES_PICTURE | KES_NATIVE_FADE) &&
              programs[0].words[KSI_ANCHORED] && value(KSI_ANCHOR_X) == 164 && value(KSI_ANCHOR_Y) == 96,
          "icon layer did not bypass terrain recolouring at the map anchor");
    float angle = 3.14159265358979323846f / 8;
    check(std::abs(value(KSI_HEADER) - (60 - 4 * std::cos(angle) - 4 * std::sin(angle))) < .001,
          "heading was snapped to 45 degrees");
    check(khdays_minimap_cell(1, 2, 0xd139) && !khdays_minimap_cell(1, 3, 0xd139) &&
              !khdays_minimap_cell(1, 2, 0xd1cf) && !khdays_minimap_cell(0, 2, 0xd139),
          "suppression affected map or other screens");
    config.players[0].smooth_rotation = false;
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 4096);
    khdays_minimap_end();
    step();
    check(value(KSI_HEADER) == 56 && value(KSI_HEADER + 1) == 36, "native directional mode changed geometry");
    config.characters["Axel"] = {true, true, "Axel", 0, 2};
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 0);
    khdays_minimap_end();
    step();
    check(value(KSI_HEADER + 2) == 32 && value(KSI_HEADER) == 52, "character-specific player sprite was not selected");
    config.characters["Axel"].sprite = "missing";
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 4096);
    khdays_minimap_end();
    step();
    check(value(KSI_HEADER) == 56, "missing optional character icon broke player fallback");
    khdays_minimap_begin();
    khdays_minimap_icon(1, 40 << 12, 30 << 12);
    khdays_minimap_stamp(0x02103120, 20, 10);
    khdays_minimap_emblem(0);
    khdays_minimap_end();
    step();
    check(programs[0].words[KSI_COUNT] == 3, "entity/goal/emblem icons lost");
    khdays_minimap_begin();
    for (int i = 0; i < 130; ++i)
        khdays_minimap_icon(1, 40 << 12, 30 << 12);
    khdays_minimap_end();
    step();
    check(owns && programs[0].words[KSI_COUNT] == 128 && programs[1].words[KSI_COUNT] == 2 &&
              programs[0].words[KSI_CLEAR] && !programs[1].words[KSI_CLEAR],
          "multi-batch order or clear wrong");
    khdays_minimap_begin();
    for (int i = 0; i < 513; ++i)
        khdays_minimap_icon(1, 40 << 12, 30 << 12);
    khdays_minimap_end();
    step();
    check(!owns, "overflow did not retain native icons");
    khdays_minimap_begin();
    khdays_minimap_icon(1, 40 << 12, 30 << 12);
    khdays_minimap_end();
    test_context.camp_screen = 1;
    step();
    check(!owns, "icons appeared in menu");
    test_context.camp_screen = KHDAYS_CAMP_NONE;
    test_hd = 0;
    step();
    check(!owns, "HD disabled suppressed native");
    test_hd = 1;
    khdays_minimap_reset();
    step();
    check(!owns, "old icons survived map teardown");
    loaded = false;
    tried = false;
    std::ofstream(settings) << "{}";
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 0);
    khdays_minimap_end();
    step();
    check(!owns && !programs[0].words[KSI_CLEAR], "absent mod settings changed native icon rendering");
    tried = false;
    std::filesystem::remove(settings);
    khdays_minimap_begin();
    khdays_minimap_end();
    step();
    check(!owns, "missing JSON did not preserve native fallback");
}
void standalone()
{
    auto folder = std::filesystem::temp_directory_path() / "khdays-minimap-files-test";
    std::filesystem::create_directories(folder);
    std::array<unsigned, 40> dds{};
    dds[0] = 0x20534444;
    dds[1] = 124;
    dds[3] = 4;
    dds[4] = 2;
    dds[19] = 32;
    dds[22] = 32;
    dds[23] = 0xff;
    dds[24] = 0xff00;
    dds[25] = 0xff0000;
    dds[26] = 0xff000000;
    std::fill(dds.begin() + 32, dds.end(), 0xff0000ff);
    std::ofstream(folder / "key.DDS", std::ios::binary).write(reinterpret_cast<const char *>(dds.data()), sizeof(dds));
    config = {};
    config.sprites["player1_0"] = {};
    config.sprites["player1_0"].x = 8;
    config.players[0] = {true, true, "key.DDS", 0, 1};
    config.players[1] = config.players[0];
    config.characters["Axel"] = config.players[0];
    khdays::assets::DecodedTexture sheet;
    sheet.width = 512;
    sheet.height = 256;
    sheet.rgba.resize(512 * 256 * 4, 123);
    overrides(folder, sheet);
    check(config.sprites.contains("key.DDS"), "standalone DDS was not loaded");
    const auto &icon = config.sprites.at("key.DDS");
    check(config.height == 262 && sheet.height == 262 && config.sprites.size() == 2,
          "shared override packed more than once");
    check(icon.width == 2 && icon.height == 4 && icon.draw_width == 4 && icon.draw_height == 8,
          "standalone image lost its aspect ratio or native display size");
    check(config.sprites.at("player1_0").x == 16 && config.sprites.at("player1_0").width == 16 &&
              config.sprites.at("player1_0").draw_width == 8 && sheet.rgba.front() == 123 &&
              sheet.rgba[(257 * 512 + 1) * 4] == 255,
          "packing changed shared HD atlas coordinates, geometry or pixels");
    tried = loaded = true;
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 4096);
    khdays_minimap_end();
    step();
    check(owns && value(KSI_HEADER + 2) == 1 && value(KSI_HEADER + 3) == 257,
          "standalone character override was not rendered");
    config.characters.clear();
    config.players[0].sprite = "absent.png";
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 4096);
    khdays_minimap_end();
    step();
    float r = 3.14159265358979323846f / 8;
    check(owns && std::abs(value(KSI_HEADER) - (60 - 4 * std::cos(r) - 4 * std::sin(r))) < .001,
          "missing optional file disabled continuous rotation");
    std::filesystem::remove(folder / "key.DDS");
    std::filesystem::remove(folder);
}
void native_map()
{
    config.kh2map = true;
    mask_atlas = 98;
    test_terrain = true;
    // DropPendingEdit cleared the upload state and freed its NCGR buffer before drawing.
    store(0x02100030, 0);
    store(0x02100028, 0);
    resident_map.assign(32, 0x11);
    khdays_minimap_begin();
    khdays_minimap_player(0, 60 << 12, 40 << 12, 0);
    khdays_minimap_end();
    step();
    check(radar_program.words[KSI_CLEAR] && radar_program.words[KSI_COUNT] == 2, "native map not generated");
    check(radar_program.words[KSI_WIDTH] == 192 && radar_program.words[KSI_RADAR_MASK] == 99,
          "native map resolution or mask wrong");
    auto coordinate = [](unsigned word) {
        float f;
        std::memcpy(&f, radar_program.words + word, sizeof(f));
        return f;
    };
    check(coordinate(KSI_HEADER) == 0 && coordinate(KSI_HEADER + 1) == 0 && coordinate(KSI_HEADER + 5) == 96 &&
              coordinate(KSI_HEADER + 11) == 96 && coordinate(KSI_HEADER + 2) == 12 && coordinate(KSI_HEADER + 3) == -8,
          "map boundary left a black gap instead of clamping the terrain edge");
    check(images[terrain_image][0] == 0xff808080u, "native terrain palette was lost");
    unsigned uploads = terrain_uploads;
    step();
    check(terrain_uploads == uploads, "unchanged terrain uploaded every frame");
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    preferences[KHDAYS_OPT_MINIMAP] = 0;
    khdays_hud_settings_publish(preferences);
    step();
    check(!radar_program.words[KSI_CLEAR] && owns && programs[0].words[KSI_COUNT],
          "KH2 map off removed icon replacement or retained the square map");
    preferences[KHDAYS_OPT_MINIMAP] = 1;
    khdays_hud_settings_publish(preferences);
    step();
    check(radar_program.words[KSI_CLEAR], "KH2 map could not be re-enabled live");
    test_hd_map = true;
    step();
    check(terrain_scale == 2 && images[terrain_image][0] == 0xff123456u && images[terrain_image][15] == 0xff123456u &&
              images[terrain_image][16] == 0,
          "existing HD tile replacement was not retained at its own scale");
    hd_map.info |= KHDAYS_REPLACEMENT_INDEXED;
    std::fill(hd_map_pixels.begin(), hd_map_pixels.end(), 2);
    ++hd_map_version;
    step();
    check(images[terrain_image][0] == 0xff00ff00u, "indexed HD map lost the native palette");
    test_hd_map = false;
    resident_map.assign(480 * 32, 0x11);
    std::fill(resident_map.begin() + 24 * 32, resident_map.begin() + 25 * 32, static_cast<unsigned char>(0x22));
    resident_map[0] = 0x12;
    step();
    check(images[terrain_image][8 * 256] == 0xff00ff00u && images[terrain_image][0] == 0xff00ff00u &&
              images[terrain_image][1] == 0xff808080u,
          "24-column packed map tiles were incorrectly treated as a 32-column image");
    map_cell_flip = 0x400;
    step();
    check(images[terrain_image][0] == 0xff808080u && images[terrain_image][7] == 0xff00ff00u,
          "map-cell horizontal flip was not applied");
    map_cell_flip = 0;
    resident_source = false;
    step();
    check(!radar_program.words[KSI_CLEAR] && owns, "missing resident source did not retain existing icons");
    resident_source = true;
    store(0x02100020, 2);
    step();
    check(!radar_program.words[KSI_CLEAR], "hidden native map was resurrected");
    store(0x02100020, 0);
    config.kh2map = false;
    step();
    check(!radar_program.words[KSI_CLEAR] && owns, "disabled KH2 map did not retain existing icon path");
    config.kh2map = true;
    mask_atlas = -1;
    step();
    check(!radar_program.words[KSI_CLEAR] && owns, "missing radar asset disabled the existing map");
    test_terrain = test_hd_map = false;
    config.kh2map = false;
}
} // namespace
extern "C" {
unsigned char *khdays_window;
int khdays_mods_find(const char *, char *out, int size)
{
    std::snprintf(out, size, "%s", settings.string().c_str());
    return 1;
}
int khdays_textures_hd(void)
{
    return test_hd;
}
int khdays_ui_mode(int)
{
    return KHDAYS_UI_ON;
}
void khdays_log(const char *, ...) {}
void khdays_ui_changed(void) {}
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
    images.emplace_back(size_t(w) * h);
    return int(images.size() - 1);
}
void khdays_ui_image_put(int) {}
unsigned *khdays_ui_image_begin(int i)
{
    return images[i].data();
}
void khdays_ui_image_end(int)
{
    ++terrain_uploads;
}
unsigned khdays_ui_palette_colour(int, unsigned, unsigned index)
{
    return (index & 15) == 2 ? 0xff00ff00u : (index & 15) ? 0xff808080u : 0xffffffffu;
}
unsigned khdays_tiles2d_bg_tile(int, unsigned)
{
    return test_hd_map ? 1u << 20 : 0;
}
const KhdaysTextureReplacement *khdays_textures_replacement(int i)
{
    return test_hd_map && i == 0 ? &hd_map : nullptr;
}
unsigned khdays_textures_version(int)
{
    return hd_map_version;
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
int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *l)
{
    *l = {};
    l->wt = l->ht = 32;
    l->chars = bg == 3 ? 0x10000 : 0;
    return engine == 1 && (bg == 2 || (bg == 3 && test_terrain));
}
const unsigned char *khdays_bg_memory(int, unsigned address)
{
    unsigned offset = address - 0x139e0;
    return offset < resident_map.size() ? resident_map.data() + offset : nullptr;
}
int khdays_bg_source(int, unsigned address, const char **file, unsigned *offset, unsigned *run)
{
    unsigned at = address - 0x139e0;
    if (!resident_source || at >= resident_map.size())
        return 0;
    *file = "UI/btl/mapchr.p2@36a00";
    *offset = 48 + at;
    *run = unsigned(resident_map.size()) - at;
    return 1;
}
unsigned khdays_bg_map_entry(int, const KhdaysBgLayer *l, int x, int y)
{
    if (l->chars == 0x10000)
        return (x < 24 && y < 20 ? 0xb1cfu + y * 24 + x : 0xb1cfu + 479) | (x == 0 && y == 0 ? map_cell_flip : 0);
    return x >= 13 && x < 28 && y >= 7 && y < 17 ? 0xd139u + (y - 7) * 15 + (x - 13) : 0;
}
}
int main(int argc, char **argv)
{
    try {
        setup();
        if (argc == 3 && std::string(argv[1]) == "--asset") {
            settings = argv[2];
            tried = loaded = false;
            load();
            check(loaded, "local atlas rejected");
            for (const auto &p : config.players)
                if (p.enabled && !p.sprite.empty() &&
                    std::filesystem::is_regular_file(settings.parent_path() / p.sprite))
                    check(config.sprites.contains(p.sprite), "local standalone player image was not packed");
            khdays_minimap_begin();
            khdays_minimap_player(0, 60 << 12, 40 << 12, 4096);
            khdays_minimap_end();
            step();
            check(owns, "local atlas not drawn");
        } else {
            tests();
            standalone();
            native_map();
        }
        std::filesystem::remove(std::filesystem::temp_directory_path() / "khdays-minimap-test.json");
        delete[] khdays_window;
        std::cout << "Minimap icon capture, rotation, character overrides, batching and fallback passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
