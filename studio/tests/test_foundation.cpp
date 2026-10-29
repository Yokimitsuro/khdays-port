// Settings, the catalog, the mods and which one the port uses, and the safe
// write, on folders the tests make.
#include "test.h"

#include "core/files/catalog_tree.h"

#include "core/files/catalog.h"
#include "core/files/file_write.h"
#include "core/mods/mods.h"
#include "core/files/paths.h"
#include "core/mods/settings.h"

#include <algorithm>
#include <fstream>
#include <iterator>

using namespace studio;

namespace {

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

const Item* find(const Catalog& c, Kind kind, const std::string& key) {
    for (const auto& item : c.items) {
        if (item.kind == kind && item.key == key)
            return &item;
    }
    return nullptr;
}

Item texture(const std::string& key) {
    Item item;
    item.kind = Kind::texture;
    item.key = key;
    return item;
}

}  // namespace

TEST(settings_defaults_and_bad_file) {
    const fs::path root = studio_test::scratch("settings");
    std::string note;
    Settings s = load_settings(root, &note);
    CHECK(note.empty());
    CHECK(s.graphics_dump == "saves/dump/graphics");
    CHECK(s.sounds_dump == "saves/dump/sounds");
    CHECK(s.cache_mb == 512);
    CHECK(s.browser_tree);

    s.browser_tree = false;
    s.mod = "My Mod";
    s.language = "es";
    s.cache_mb = 64;
    CHECK(save_settings(s, nullptr));
    Settings again = load_settings(root, &note);
    CHECK(again.mod == "My Mod");
    CHECK(again.language == "es");
    CHECK(again.cache_mb == 64);
    CHECK(!again.browser_tree);

    studio_test::write(settings_file(root), "this line has no equals sign\n");
    note.clear();
    Settings bad = load_settings(root, &note);
    CHECK(!note.empty());
    CHECK(bad.mod.empty());
    fs::path kept = settings_file(root);
    kept += ".bad";
    CHECK(fs::exists(kept));
}

TEST(settings_find_the_port_and_take_pasted_paths) {
    // an install: the tool beside the game; a repository: the tool in build/studio/release
    const fs::path install = studio_test::scratch("settings_install"), repo = studio_test::scratch("settings_repo");
    const fs::path elsewhere = studio_test::scratch("settings_elsewhere");
    studio_test::write(install / "khdays-native.exe", "x");
    fs::create_directories(repo / "data");
    fs::create_directories(repo / "build" / "studio" / "release");
    CHECK(is_port_folder(install) && is_port_folder(repo) && !is_port_folder(elsewhere));
    CHECK(find_port_root(install, elsewhere) == install);                             // started there
    CHECK(find_port_root(elsewhere, install) == install);                             // started elsewhere
    CHECK(find_port_root(elsewhere, repo / "build" / "studio" / "release") == repo);  // its parents
    CHECK(find_port_root(elsewhere, fs::path()) == elsewhere);                        // none: as before

    CHECK(clean_path("  \"C:\\Program Files\\KH\\khdays-native.exe\"  ") == "C:\\Program Files\\KH\\khdays-native.exe");
    CHECK(clean_path("'saves/dump'") == "saves/dump");
    CHECK(clean_path("plain") == "plain");

    // the game's executable beside the tool, found with nothing set; a folder named stands for it; a full path,
    // quoted, taken
    std::string note;
    Settings s = load_settings(install, &note);
    CHECK(port_exe(s) == install / "khdays-native.exe");
    s.port = u8(install);
    CHECK(port_exe(s) == install / "khdays-native.exe");
    s.port = "\"" + u8(install / "khdays-native.exe") + "\"";
    CHECK(port_exe(s) == install / "khdays-native.exe");
    CHECK(resolve(s, "\"" + u8(elsewhere) + "\"") == elsewhere);
    // a named executable gone: the one there is
    s.port = "build/port/obj-release-x64/khdays-native.exe";
    CHECK(save_settings(s, nullptr));
    CHECK(load_settings(install, &note).port == "khdays-native.exe");
}

TEST(catalog_from_dumps_and_game_files) {
    const fs::path root = studio_test::scratch("catalog");
    const fs::path g = root / "graphics", s = root / "sounds", n = root / "nitrofs";
    for (const char* f : {"a/b.p.z/tex/t1@p1.png", "a/b.p.z/tex/t1@p2.png", "a/b.p.z/chr/0.png", "c/d.p2#0/mdl/m.gltf",
                          "a/b.p.z/mdl/notes.txt"}) {
        studio_test::write(g / path_u8(f), "x");
    }
    for (const char* f : {"seq/BGM_01.wav", "seqarc/ARC/3.wav", "strm/S.wav", "wave/W/0.wav", "other/x.wav"}) {
        studio_test::write(s / path_u8(f), "x");
    }
    for (const char* f : {"UI/btl/es/magic.s.z", "UI/cm/str/select_fr.s.z", "UI/x/y.s.z", "db/db_es.p2", "db/db.p2",
                          "mv/802.mods", "mv/dummy.dat"}) {
        studio_test::write(n / path_u8(f), "x");
    }
    const Catalog c = build_catalog({g, s, n}, nullptr, nullptr);
    CHECK(c.graphics_dump_found && c.sounds_dump_found && c.game_files_found);
    const Item* t1 = find(c, Kind::texture, "a/b.p.z/tex/t1@p1");
    CHECK(t1 != nullptr && t1->group == "a/b.p.z");
    CHECK(find(c, Kind::sheet, "a/b.p.z/chr/0") != nullptr);
    const Item* m = find(c, Kind::model, "c/d.p2#0/mdl/m");
    CHECK(m != nullptr && m->group == "c/d.p2#0");
    CHECK(find(c, Kind::sound, "seq/BGM_01") != nullptr);
    const Item* arc = find(c, Kind::sound, "seqarc/ARC/3");
    CHECK(arc != nullptr && arc->group == "seqarc/ARC");
    CHECK(find(c, Kind::sound, "strm/S") != nullptr);
    CHECK(find(c, Kind::sound, "wave/W/0") != nullptr);
    CHECK(find(c, Kind::sound, "other/x") == nullptr);
    const Item* magic = find(c, Kind::text_file, "UI/btl/es/magic.s.z");
    CHECK(magic != nullptr && magic->language == "es");
    const Item* select = find(c, Kind::text_file, "UI/cm/str/select_fr.s.z");
    CHECK(select != nullptr && select->language == "fr");
    const Item* y = find(c, Kind::text_file, "UI/x/y.s.z");
    CHECK(y != nullptr && y->language.empty());
    const Item* db = find(c, Kind::text_file, "db/db_es.p2");
    CHECK(db != nullptr && db->language == "es");
    CHECK(find(c, Kind::text_file, "db/db.p2") == nullptr);  // not known to hold text
    CHECK(find(c, Kind::movie, "802") != nullptr);
    CHECK(c.items.size() == 13u);

    const unsigned all = (1u << kKinds) - 1u;
    CHECK(c.search("MAGIC", all).size() == 1u);
    CHECK(c.search("t1 p2", all).size() == 1u);
    CHECK(c.search("t1", 1u << static_cast<unsigned>(Kind::sheet)).empty());
    CHECK(c.search("", 1u << static_cast<unsigned>(Kind::sound)).size() == 4u);
}

TEST(text_language_and_palettes) {
    CHECK(text_language("db/db_it.p2") == "it");
    CHECK(text_language("UI/btl/de/magic.s.z") == "de");
    CHECK(text_language("UI/load/str/ja/lrd.s.z") == "ja");
    CHECK(text_language("UI/load/lrd_en.s.z") == "en");
    CHECK(text_language("UI/mlt/mlt.s.z").empty());
    CHECK(any_palette_key(texture("x/y.p.z/tex/head@head_pl")) == "x/y.p.z/tex/head");
    CHECK(any_palette_key(texture("x/y.p.z/tex/head")).empty());
    CHECK(any_palette_key(texture("x/a@b/tex/head")).empty());  // an @ outside the name
}

TEST(export_names) {
    const auto item = [](Kind kind, const std::string& key, const std::string& group) {
        Item i;
        i.kind = kind;
        i.key = key;
        i.group = group;
        return i;
    };
    CHECK(export_name(item(Kind::sheet, "UI/btl/es/main.p2#0/chr/0", "UI/btl/es/main.p2#0")) == "main.p2#0_es.png");
    CHECK(export_name(item(Kind::sheet, "UI/btl/main.p2#1/chr/1", "UI/btl/main.p2#1")) == "main.p2#1_1.png");
    CHECK(export_name(item(Kind::sheet, "UI/cm/cmo_es.p2#3/chr/0", "UI/cm/cmo_es.p2#3")) == "cmo_es.p2#3.png");
    CHECK(export_name(item(Kind::model, "ba/ch/ro/def.p.z/mdl/ro_b", "ba/ch/ro/def.p.z")) == "ro_b.gltf");
    CHECK(export_name(item(Kind::room, "mi/wd/wd_tw/room/0", "mi/wd/wd_tw")) == "wd_tw_room0.gltf");
}

TEST(mod_names) {
    std::string why;
    CHECK(valid_mod_name("KH2 Sora FBX", &why));
    CHECK(valid_mod_name("Música ñ", &why));
    CHECK(!valid_mod_name("", &why));
    CHECK(!valid_mod_name("..", &why));
    CHECK(!valid_mod_name("a/b", &why));
    CHECK(!valid_mod_name("a:b", &why));
    CHECK(!valid_mod_name("trailing.", &why));
    const fs::path root = studio_test::scratch("mod_names");
    CHECK(create_mod(root, "New", &why));
    CHECK(!create_mod(root, "New", &why) && why == "exists");
}

TEST(which_mod_the_port_uses) {
    const fs::path mods = studio_test::scratch("winner");
    const Item t = texture("a/b.p.z/tex/t1@p1");
    // the same file in two mods: the first by name
    studio_test::write(mods / "b-mod/graphics/a/b.p.z/tex/t1@p1.png", "b");
    studio_test::write(mods / "a-mod/graphics/a/b.p.z/tex/t1@p1.png", "a");
    auto h = holders(mods, t);
    CHECK(h.size() == 2u && h.front().mod == "a-mod");

    // the exact name in any mod before the any-palette name in an earlier one
    fs::remove(mods / "a-mod/graphics/a/b.p.z/tex/t1@p1.png");
    studio_test::write(mods / "a-mod/graphics/a/b.p.z/tex/t1.png", "a");
    h = holders(mods, t);
    CHECK(h.size() == 2u && h.front().mod == "b-mod");

    // sounds: .wav in any mod before .ogg in an earlier one (sound_mods.c)
    Item snd;
    snd.kind = Kind::sound;
    snd.key = "seq/BGM";
    studio_test::write(mods / "a-mod/sounds/seq/BGM.ogg", "a");
    studio_test::write(mods / "b-mod/sounds/seq/BGM.wav", "b");
    h = holders(mods, snd);
    CHECK(h.size() == 2u && h.front().mod == "b-mod");

    const auto mods_list = list_mods(mods);
    CHECK(mods_list.size() == 2u && mods_list[0].name == "a-mod");
}

TEST(replacement_paths) {
    const Item t = texture("a/b.p.z/tex/t1@p1");
    CHECK(replacement_path(t, ".png") == "graphics/a/b.p.z/tex/t1@p1.png");
    CHECK(replacement_path(t, ".png", true) == "graphics/a/b.p.z/tex/t1.png");
    Item snd;
    snd.kind = Kind::sound;
    snd.key = "seqarc/ARC/3";
    CHECK(replacement_path(snd, ".ogg") == "sounds/seqarc/ARC/3.ogg");
    Item text;
    text.kind = Kind::text_file;
    text.key = "UI/btl/es/magic.s.z";
    CHECK(replacement_path(text, "") == "files/UI/btl/es/magic.s.z");
    Item movie;
    movie.kind = Kind::movie;
    movie.key = "802";
    CHECK(replacement_path(movie, "") == "movies/802.mpg");
    CHECK(replacement_path(movie, ".mp4") == "movies/hd802.mp4");
}

TEST(model_removed_with_its_companions) {
    // two models of a mod: m names m.png and shared.png, n names shared.png
    const fs::path mod = studio_test::scratch("companions") / "M";
    const fs::path mdl = mod / "graphics/x/mdl";
    auto gltf = [](const std::vector<std::string>& images) {
        std::string s = R"({"asset":{"version":"2.0"},"images":[)";
        for (std::size_t i = 0; i < images.size(); ++i)
            s += (i ? "," : "") + std::string(R"({"uri":")") + images[i] + "\"}";
        return s + "]}";
    };
    studio_test::write(mdl / "m.gltf", gltf({"m.png", "shared.png"}));
    studio_test::write(mdl / "n.gltf", gltf({"shared.png"}));
    studio_test::write(mdl / "m.png", "png");
    studio_test::write(mdl / "shared.png", "png");
    studio_test::write(mdl / "old.png", "png");  // left by a model removed before
    const auto named = used_companions(mod);
    CHECK(among(named, mdl / "m.png") && among(named, mdl / "shared.png"));
    CHECK(!among(named, mdl / "old.png"));
    Item m;
    m.kind = Kind::model;
    m.key = "x/mdl/m";
    CHECK(remove_replacement(mod, m, nullptr));
    CHECK(!fs::exists(mdl / "m.gltf") && !fs::exists(mdl / "m.png"));     // its own texture went with it
    CHECK(fs::exists(mdl / "shared.png") && fs::exists(mdl / "n.gltf"));  // n still names it
    CHECK(remove_mod_file(mod, mdl / "old.png", nullptr) && !fs::exists(mdl / "old.png"));
    CHECK(!remove_mod_file(mod, mod.parent_path() / "outside.png", nullptr));
}

TEST(safe_write) {
    const fs::path mod = studio_test::scratch("safe_write") / "M";
    const fs::path dest = mod / "graphics/x/tex/t.png";
    studio_test::write(dest, "old");
    auto produce = [](const fs::path& tmp) {
        std::ofstream(tmp, std::ios::binary) << "new";
        return std::string();
    };
    // a verifier that refuses: the old file stays, no temporary left
    WriteResult r = write_into_mod(mod, dest, produce, [](const fs::path&) { return std::string("no"); });
    CHECK(!r.ok && r.why == "no");
    CHECK(read(dest) == "old");
    CHECK(std::distance(fs::directory_iterator(dest.parent_path()), fs::directory_iterator()) == 1);
    // accepted: replaced
    r = write_into_mod(mod, dest, produce, [](const fs::path&) { return std::string(); });
    CHECK(r.ok && read(dest) == "new");
    // outside the mod: nothing done
    r = write_into_mod(mod, mod.parent_path() / "elsewhere.png", produce, nullptr);
    CHECK(!r.ok && r.outside && !fs::exists(mod.parent_path() / "elsewhere.png"));
    r = write_into_mod(mod, mod / ".." / "escape.png", produce, nullptr);
    CHECK(!r.ok && r.outside);
}

TEST(remove_a_replacement) {
    const fs::path mod = studio_test::scratch("remove") / "M";
    Item snd;
    snd.kind = Kind::sound;
    snd.key = "seqarc/ARC/3";
    studio_test::write(mod / "sounds/seqarc/ARC/3.wav", "w");
    studio_test::write(mod / "sounds/seqarc/ARC/3.ogg", "o");
    studio_test::write(mod / "sounds/seqarc/ARC/4.wav", "keep");
    CHECK(files_in_mod(mod, snd).size() == 2u);
    std::string why;
    CHECK(remove_replacement(mod, snd, &why));
    CHECK(files_in_mod(mod, snd).empty());
    CHECK(fs::exists(mod / "sounds/seqarc/ARC/4.wav"));
    fs::remove(mod / "sounds/seqarc/ARC/4.wav");
    studio_test::write(mod / "sounds/seqarc/ARC/3.wav", "w");
    CHECK(remove_replacement(mod, snd, &why));
    CHECK(!fs::exists(mod / "sounds"));  // the empty folders went, up to the mod
    CHECK(fs::exists(mod));
}

TEST(inside_a_folder) {
    CHECK(inside("C:/a/b", "C:/a/b/c.txt"));
    CHECK(inside("C:/a/b/", "C:/a/b/c/d"));
    CHECK(!inside("C:/a/b", "C:/a/b"));
    CHECK(!inside("C:/a/b", "C:/a/bc/d"));
    CHECK(!inside("C:/a/b", "C:/a/b/../c"));
}

TEST(the_catalog_as_folders) {
    Catalog c;
    const auto add = [&](Kind k, const char* key) {
        Item i;
        i.kind = k;
        i.key = key;
        c.items.push_back(i);
    };
    add(Kind::model, "ba/ch/ro/def.p.z");
    add(Kind::texture, "ba/ch/ro/def.p.z/tex/ro_head@ro_head_pl");
    add(Kind::texture, "ba/ch/ro/def.p.z/tex/ro_body@ro_body_pl");
    add(Kind::sound, "seqarc/COMMON_SYS/3");
    const std::vector<int> all = {0, 1, 2, 3};
    // one kind at a time: folders straight away
    CatalogTree t = build_catalog_tree(c, {1, 2}, false);
    CHECK(t.nodes[0].count == 2 && t.nodes[0].children.size() == 1u);
    const TreeNode& ba = t.nodes[static_cast<std::size_t>(t.nodes[0].children[0])];
    CHECK(ba.name == "ba" && ba.count == 2);
    const int tex = t.node_of_item[1];
    CHECK(tex == t.node_of_item[2] && t.nodes[static_cast<std::size_t>(tex)].name == "tex");
    CHECK(t.nodes[static_cast<std::size_t>(tex)].items == std::vector<int>({2, 1}));  // by name: body, head
    // every kind: a node per kind, the model a leaf of its own kind's ro
    t = build_catalog_tree(c, all, true);
    CHECK(t.nodes[0].children.size() == 3u && t.nodes[0].count == 4);
    CHECK(t.nodes[static_cast<std::size_t>(t.nodes[0].children[0])].kind == static_cast<int>(Kind::texture));
    CHECK(t.nodes[static_cast<std::size_t>(t.node_of_item[0])].name == "ro" &&
          t.nodes[static_cast<std::size_t>(t.node_of_item[0])].items == std::vector<int>({0}));
    // kinds mixed: the model is the def.p.z folder's own item, its textures inside
    t = build_catalog_tree(c, all, false);
    const int model = t.node_of_item[0];
    CHECK(t.nodes[static_cast<std::size_t>(model)].item == 0 &&
          t.nodes[static_cast<std::size_t>(model)].name == "def.p.z");
    CHECK(t.nodes[static_cast<std::size_t>(model)].count == 3);
    CHECK(leaf_name("seqarc/COMMON_SYS/3") == "3");
}
