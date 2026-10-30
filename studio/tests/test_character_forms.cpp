// A character's forms in its description: found, added, renamed, copied, taken away and put in order, each line
// kept as written; their rules as the window's rows and back; their files checked by the port's reader; the
// [stats] lines as a table of levels over a base's; the game's items read from made-up databases.
#include "test.h"

#include "core/characters/character_files.h"
#include "core/characters/character_forms.h"
#include "core/characters/form_rules.h"
#include "core/characters/game_items.h"
#include "core/files/containers.h"
#include "core/files/paths.h"

extern "C" {
#include "characters/registry.h"
#include "characters/stats.h"
}

#include <cstring>

using namespace studio;

namespace {

const char* const kVentus = "[character]\nformat = 1\nbase = roxas\nname = Ventus\n\n"
                            "[model]\nfile = model/v.glb\nskeleton = own\n\n"
                            "[form dual]   # two keyblades\nbase = roxas_dual\nwhen = dual\n\n"
                            "[form dual actions]\nfallback = clips/d.glb\n\n"
                            "[form calm]\nwhen = level >= 50\n";

// A P2 of `entries` stored as they are (no compression), its tables in its first 0x200 bytes.
std::string p2_of(const std::vector<std::string>& entries) {
    P2File p2;
    p2.head.assign(0x200, 0);
    p2.head[0] = 'P';
    p2.head[1] = '2';
    p2.head[2] = static_cast<std::uint8_t>(entries.size());
    p2.head[3] = static_cast<std::uint8_t>(entries.size() >> 8);
    p2.head[0xd] = 0x02;  // its data at 0x200
    for (const auto& e : entries) {
        P2Entry entry;
        entry.data.assign(e.begin(), e.end());
        p2.entries.push_back(entry);
    }
    const auto bytes = write_p2(p2);
    return std::string(bytes.begin(), bytes.end());
}

void u32(std::string* out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out->push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

}  // namespace

TEST(character_forms_found_changed_and_ordered) {
    IniText ini = IniText::parse(kVentus);
    CHECK(form_ids(ini) == (std::vector<std::string>{"dual", "calm"}));
    CHECK(form_part_names(ini, "dual") == (std::vector<std::string>{"form dual", "form dual actions"}));
    CHECK(form_part("", "model") == "model" && form_part("", "") == "character");
    CHECK(form_part("dual", "model") == "form dual" && form_part("dual", "stats") == "form dual stats");
    std::string why;
    CHECK(!add_form(&ini, "dual", &why));     // taken
    CHECK(!add_form(&ini, "default", &why));  // the default's name
    CHECK(!add_form(&ini, "a b", &why));
    CHECK(add_form(&ini, "fast", &why));
    CHECK(ini.get("character", "format") == "2");  // raised for its forms
    CHECK(form_ids(ini).back() == "fast");
    // renamed: every part, the heading's comment kept
    CHECK(rename_form(&ini, "dual", "twin", &why));
    CHECK(form_ids(ini) == (std::vector<std::string>{"twin", "calm", "fast"}));
    CHECK(ini.text().find("[form twin]   # two keyblades") != std::string::npos);
    CHECK(ini.get("form twin actions", "fallback") == "clips/d.glb");
    CHECK(!rename_form(&ini, "twin", "calm", &why));
    // copied with all its parts
    CHECK(duplicate_form(&ini, "twin", "twin2", &why));
    CHECK(ini.get("form twin2 actions", "fallback") == "clips/d.glb" && ini.get("form twin2", "base") == "roxas_dual");
    // put in order: earlier, later, past the ends nothing
    move_form(&ini, "fast", -1);
    CHECK(form_ids(ini) == (std::vector<std::string>{"twin", "fast", "calm", "twin2"}));
    move_form(&ini, "twin", -1);
    move_form(&ini, "twin", +1);
    CHECK(form_ids(ini) == (std::vector<std::string>{"fast", "twin", "calm", "twin2"}));
    CHECK(ini.text().find("[form twin actions]") > ini.text().find("[form twin]"));
    remove_form(&ini, "twin2");
    CHECK(form_ids(ini) == (std::vector<std::string>{"fast", "twin", "calm"}));
    CHECK(!ini.has_part("form twin2 actions"));
    // what is not a form's is as written
    CHECK(ini.text().rfind("[character]\nformat = 2\nbase = roxas\nname = Ventus\n\n[model]\n", 0) == 0);
    CHECK(form_base_code("roxas_dual") == "r2" && form_bases().size() == 20);
}

TEST(character_form_rules_as_rows) {
    RuleRows rows;
    CHECK(rule_rows("gear 230 and links 241 >= 3  # Zero Gear", &rows));
    CHECK(!rows.any && rows.conditions.size() == 2);
    CHECK(rows.conditions[0].kind == RuleCondition::gear && rows.conditions[0].item == 230);
    CHECK(rows.conditions[1].kind == RuleCondition::links && rows.conditions[1].item == 241 &&
          rows.conditions[1].op == ">=" && rows.conditions[1].n == 3);
    CHECK(rule_text(rows) == "gear 230 and links 241 >= 3");
    CHECK(rule_names(rows, [](int item) { return item == 230 ? std::string("Zero Gear") : std::string(); }) ==
          "Zero Gear");
    CHECK(rule_rows("not dual or level > 9 or links >= 2 or plugin a.b or hp < 30 or panel 93", &rows));
    CHECK(rows.any && rows.conditions.size() == 6 && rows.conditions[0].negate);
    CHECK(rule_text(rows) == "not dual or level > 9 or links >= 2 or plugin a.b or hp < 30 or panel 93 >= 1");
    // what the rows cannot hold: kept as text
    CHECK(!rule_rows("gear 1 or (dual and level == 2)", &rows));
    CHECK(!rule_rows("gear 1 or dual and level == 2", &rows));
    CHECK(!rule_rows("gear", &rows));
    // what the rows write, the port reads
    char why[200] = "";
    KhdaysRule* rule = khdays_rule_parse("not dual or level > 9 or links >= 2 or panel 93 >= 1", why, sizeof(why));
    CHECK(rule != nullptr);
    khdays_rule_free(rule);
}

TEST(character_forms_files_checked) {
    const fs::path mod = studio_test::scratch("character_forms");
    studio_test::write(mod / "mod.ini", "id = test.forms\nname = Forms\nkind = gameplay\n");
    studio_test::write(mod / "characters" / "ven" / "model" / "v.glb", "x");
    studio_test::write(mod / "characters" / "ven" / "clips" / "d.glb", "x");
    IniText ini = IniText::parse(kVentus);
    ini.set("character", "format", "2");
    CHECK(check_character(mod, "ven", ini.text()).empty());
    // a form's own files looked for as the character's are
    ini.set("form dual", "model", "model/gone.glb");
    const auto problems = check_character(mod, "ven", ini.text());
    CHECK(problems.size() == 1 && problems[0].find("[form dual] model") != std::string::npos);
    ini.remove("form dual", "model");
    // a form the port leaves out is a problem the window shows
    ini.set("form calm", "when", "level >=");
    CHECK(!check_character(mod, "ven", ini.text()).empty());
}

TEST(character_stats_as_levels) {
    const auto rows = stats_of({{"1", "hp 30 strength 9"}, {"50", "defense 19"}, {"x", "hp 1"}});
    CHECK(rows.size() == 2);
    CHECK(rows.at(1)[KHDAYS_STAT_HP] == 30 && rows.at(1)[KHDAYS_STAT_STRENGTH] == 9 &&
          rows.at(1)[KHDAYS_STAT_MAGIC] == -1);
    CHECK(rows.at(50)[KHDAYS_STAT_DEFENCE] == 19);
    const auto entries = stats_entries(rows);
    CHECK(entries.size() == 2 && entries[0].first == "1" && entries[0].second == "hp 30 strength 9");
    CHECK(entries[1].second == "defence 19");
    // over a base's table: given values, the rest the base's
    std::vector<std::uint8_t> base(KHDAYS_STATS_TABLE, 0);
    for (int level = 1; level <= KHDAYS_CHARACTER_LEVELS; ++level) {
        std::uint8_t* row = base.data() + (level - 1) * KHDAYS_STATS_ROW;
        row[khdays_stats_offset(KHDAYS_STAT_MAGIC)] = static_cast<std::uint8_t>(level);
    }
    const auto table = level_table(base, rows);
    CHECK(level_value(table, 1, KHDAYS_STAT_HP) == 30 && level_value(table, 100, KHDAYS_STAT_HP) == 30);
    CHECK(level_value(table, 7, KHDAYS_STAT_MAGIC) == 7);
    CHECK(level_value(table, 25, KHDAYS_STAT_DEFENCE) == 19);  // before the first given: the first's
    CHECK(level_table(std::vector<std::uint8_t>(10), rows).empty());
    // the base's own table read from a copy's folder
    const fs::path nitrofs = studio_test::scratch("character_stats_nitrofs");
    std::string why;
    CHECK(base_level_table(nitrofs, "ro", &why).empty() && !why.empty());
}

TEST(character_game_items_read) {
    // two items: a gear (category 4, shape 21) and a link (category 4, shape -1)
    std::string records;
    u32(&records, 2);
    for (const int shape : {21, -1}) {
        std::string r(0x64, '\0');
        r[0] = 4;
        r[8] = static_cast<char>(shape);
        records += r;
    }
    // their names and descriptions, UTF-16
    std::string names;
    const char* const texts[] = {"Zero", "a gear", "Unit", "a link"};
    u32(&names, 4);
    std::string strings;
    for (const char* t : texts) {
        for (const char* c = t; *c != '\0'; ++c) {
            strings.push_back(*c);
            strings.push_back('\0');
        }
        strings += std::string(2, '\0');
        u32(&names, static_cast<std::uint32_t>(strings.size()));
    }
    names += strings;
    const fs::path nitrofs = studio_test::scratch("character_items_nitrofs");
    std::vector<std::string> db(0x16), db_en(0x16);
    db[0x15] = records;
    db_en[0x15] = names;
    studio_test::write(nitrofs / "db" / "db.p2", p2_of(db));
    studio_test::write(nitrofs / "db" / "db_en.p2", p2_of(db_en));
    std::string why;
    const auto items = game_items(nitrofs, "en", &why);
    CHECK(items.size() == 2);
    CHECK(items.size() == 2 && items[0].id == 1 && items[0].name == "Zero" && !items[0].one_cell);
    CHECK(items.size() == 2 && items[1].id == 2 && items[1].name == "Unit" && items[1].one_cell);
    CHECK(game_gears(items).size() == 1 && game_links(items).size() == 1 && game_panels(items).size() == 2);
    CHECK(game_items(nitrofs, "fr", &why).empty() && !why.empty());
}
