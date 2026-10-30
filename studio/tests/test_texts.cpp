// The story scenes' lines (ev/EV_<world>.p2): a synthetic archive's scripts read as the game's
// script machine shows them, a line changed in one language and read back, the others as they were.
#include "test.h"

#include "core/files/containers.h"
#include "core/texts/texts.h"

using namespace studio;

namespace {

using Bytes = std::vector<std::uint8_t>;

void add16(Bytes& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v & 0xff));
    d.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}
void add32(Bytes& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}
void put32(Bytes& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xff);
}

void step(Bytes& d, std::uint8_t g, std::uint8_t c, const std::vector<std::uint32_t>& words) {
    d.push_back(g);
    d.push_back(c);
    add16(d, static_cast<std::uint32_t>(1 + words.size()));
    for (const auto w : words)
        add32(d, w);
}

Bytes p2(const std::vector<Bytes>& entries) {
    P2File p;
    p.base = 0x200;
    p.head.assign(0x200, 0);
    p.head[0] = 'P';
    p.head[1] = '2';
    p.head[2] = static_cast<std::uint8_t>(entries.size());
    put32(p.head, 0xc, 0x200);
    for (const auto& e : entries) {
        P2Entry entry;
        entry.data = e;
        p.entries.push_back(entry);
    }
    return write_p2(p);
}

// A scene: two lines (the first shown by two steps), a raw step and a name; its data the two tables
// (a word per language: Japanese none, French and German one text) and the texts.
Bytes scene() {
    Bytes code;
    add32(code, 0);
    step(code, 1, 29, {0x40, 0x00});     // line 0
    step(code, 0, 5, {0x40, 0x1c});      // raw: its words are not slots
    step(code, 1, 29, {0x40, 0x1c});     // line 1
    step(code, 2, 12, {2, 0x38, 1, 0});  // a name in the data, not a line
    step(code, 1, 29, {0x40, 0x00});     // line 0 again
    put32(code, 0, static_cast<std::uint32_t>(code.size()));
    // the data: the tables at 0x00 and 0x1c, the texts from 0x38
    Bytes texts;
    const auto text = [&](const std::string& s) {
        const auto at = static_cast<std::uint32_t>(0x38 + texts.size());
        texts.insert(texts.end(), s.begin(), s.end());
        texts.push_back(0);
        return at;
    };
    text("ro_a");  // the name, at 0x38
    const std::uint32_t hello = text("Hello"), cava = text("Ça va ?"), dots = text("..."), hola = text("Hola"),
                        bye = text("Bye"), salut = text("Salut"), adios = text("Adiós");
    Bytes data;
    for (const auto w : {0xffffffffu, hello, cava, dots, dots, hola, 0xffffffffu})
        add32(data, w);
    for (const auto w : {0xffffffffu, bye, salut, dots, dots, adios, 0xffffffffu})
        add32(data, w);
    data.insert(data.end(), texts.begin(), texts.end());
    code.insert(code.end(), data.begin(), data.end());
    return code;
}

Bytes no_lines() {
    Bytes code;
    add32(code, 0);
    step(code, 0, 1, {0x08});
    put32(code, 0, static_cast<std::uint32_t>(code.size()));
    return code;
}

}  // namespace

TEST(a_scene_archives_lines_read_by_language) {
    CHECK(is_scene_archive("ev/EV_S.p2") && is_scene_archive("ev/EV_TT.p2"));
    CHECK(!is_scene_archive("db/db_en.p2") && !is_scene_archive("ev/ecam.p2") && !is_scene_archive("ev/hint_en.z"));
    const Bytes file = p2({scene(), no_lines()});
    TextFile en, fr, de;
    std::string why;
    CHECK(parse_scene_file(file, 1, &en, &why));
    CHECK(en.scenes && en.language == 1 && en.lists.size() == 2);
    CHECK(en.lists[0] == std::vector<std::u16string>({u"Hello", u"Bye"}));  // one row for the shared table
    CHECK(en.lines[0].size() == 2 && en.lines[0][0].step == 0 && en.lines[0][1].step == 2 &&
          en.lines[0][1].table == 0x1c);
    CHECK(en.lists[1].empty());
    CHECK(parse_scene_file(file, 2, &fr, &why) && fr.lists[0][0] == u"Ça va ?" && fr.lists[0][1] == u"Salut");
    CHECK(parse_scene_file(file, 3, &de, &why) && de.lists[0] == std::vector<std::u16string>({u"...", u"..."}));
    CHECK(!parse_scene_file(file, 0, &en, &why) && !parse_scene_file(file, 6, &en, &why));
    // written back with nothing changed: the same bytes
    CHECK(write_text_file(fr) == file);
}

TEST(a_scene_line_changed_in_one_language_only) {
    const Bytes file = p2({scene(), no_lines()});
    TextFile fr;
    std::string why;
    CHECK(parse_scene_file(file, 2, &fr, &why));
    fr.lists[0][0] = u"Comment ça va, Roxas ?";
    const Bytes written = write_text_file(fr);
    TextFile again;
    CHECK(parse_scene_file(written, 2, &again, &why) && again.lists == fr.lists);
    // English untouched; German and Italian, which shared their text, untouched too
    for (int l : {1, 3, 4, 5}) {
        TextFile before, after;
        CHECK(parse_scene_file(file, l, &before, &why) && parse_scene_file(written, l, &after, &why));
        CHECK(after.lists == before.lists);
    }
    // the scene with no lines keeps its bytes; the changed one grew by its new text alone
    P2File a, b;
    CHECK(parse_p2(file, &a, &why) && parse_p2(written, &b, &why));
    CHECK(b.entries[1].data == a.entries[1].data);
    CHECK(b.entries[0].data.size() == a.entries[0].data.size() + std::string("Comment ça va, Roxas ?").size() + 1);
    // emptied: an empty text, still read
    again.lists[0][1].clear();
    TextFile emptied;
    CHECK(parse_scene_file(write_text_file(again), 2, &emptied, &why) && emptied.lists[0][1].empty());
}

TEST(scene_lines_saved_language_after_language) {
    // as the Texts view saves: each edited language read from what the one before wrote, then written
    Bytes file = p2({scene(), no_lines()});
    std::string why;
    TextFile en, fr;
    CHECK(parse_scene_file(file, 1, &en, &why));
    en.lists[0][1] = u"See you, Roxas.";
    file = write_text_file(en);
    CHECK(parse_scene_file(file, 2, &fr, &why));
    fr.lists[0][1] = u"À plus, Roxas.";
    file = write_text_file(fr);
    TextFile en_after, fr_after, es_after;
    CHECK(parse_scene_file(file, 1, &en_after, &why) && en_after.lists[0][1] == u"See you, Roxas.");
    CHECK(parse_scene_file(file, 2, &fr_after, &why) && fr_after.lists[0][1] == u"À plus, Roxas.");
    CHECK(parse_scene_file(file, 5, &es_after, &why) && es_after.lists[0][1] == u"Adiós");
    // the mod's copy edited again: the line read back as typed last
    en_after.lists[0][1] = u"Later, Roxas.";
    TextFile again;
    CHECK(parse_scene_file(write_text_file(en_after), 1, &again, &why) && again.lists[0][1] == u"Later, Roxas.");
}

TEST(a_scene_whose_table_is_past_its_data_is_refused) {
    Bytes bad;
    add32(bad, 0);
    step(bad, 1, 29, {0x40, 0x40});
    put32(bad, 0, static_cast<std::uint32_t>(bad.size()));
    bad.resize(bad.size() + 16, 0);
    TextFile t;
    std::string why;
    CHECK(!parse_scene_file(p2({bad}), 1, &t, &why) && !why.empty());
}
