// KH2's texts: command and enemy names in the game's language.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// KH2's text as its menus' international encoding has it (msg/<language>/sys.bar; our reading of the files): 01 a
// space, 2e-47 A-Z, 9a-b3 a-z, 90-99 the digits, b7-cb and d0-e4 the accented small and capital letters (a grave,
// acute, circumflex, diaeresis ... u diaeresis, each set in that order), b5 oe, b6 sharp s, e5 and e6 the opening
// exclamation and question marks, punctuation; the codes below 0x20 (OpenKH's MSG page: a colour, a scale, a pause,
// each with its bytes) and characters of other tables left out
std::u16string kh2_text(const std::uint8_t *b, std::size_t n)
{
    static const char16_t small[] = {0x00e0, 0x00e1, 0x00e2, 0x00e4, 0x00e8, 0x00e9, 0x00ea,
                                     0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef, 0x00f1, 0x00f2,
                                     0x00f3, 0x00f4, 0x00f6, 0x00f9, 0x00fa, 0x00fb, 0x00fc};
    static const char16_t capital[] = {0x00c0, 0x00c1, 0x00c2, 0x00c4, 0x00c8, 0x00c9, 0x00ca,
                                       0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf, 0x00d1, 0x00d2,
                                       0x00d3, 0x00d4, 0x00d6, 0x00d9, 0x00da, 0x00db, 0x00dc};
    // each control code's length with its own byte (0 none of its own: 1)
    static const std::uint8_t length[0x20] = {1, 1, 1, 1, 1, 7, 1, 5, 4, 2, 2, 2, 2, 1, 2, 6,
                                              1, 5, 3, 5, 3, 3, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2};
    std::u16string out;
    for (std::size_t i = 0; i < n && b[i] != 0;) {
        const std::uint8_t c = b[i];
        if (c < 0x20) {
            if (c == 0x01)
                out += u' ';
            else if (c == 0x02)
                out += u' ';
            i += length[c];
            continue;
        }
        ++i;
        if (c >= 0x2e && c <= 0x47)
            out += char16_t(u'A' + (c - 0x2e));
        else if (c >= 0x9a && c <= 0xb3)
            out += char16_t(u'a' + (c - 0x9a));
        else if (c >= 0x90 && c <= 0x99)
            out += char16_t(u'0' + (c - 0x90));
        else if (c >= 0xb7 && c <= 0xcb)
            out += small[c - 0xb7];
        else if (c >= 0xd0 && c <= 0xe4)
            out += capital[c - 0xd0];
        else
            switch (c) {
            case 0x48: out += u'!'; break;
            case 0x49: out += u'?'; break;
            case 0x4f: out += u'.'; break;
            case 0x50: out += u','; break;
            case 0x52: out += u':'; break;
            case 0x5a: out += u'('; break;
            case 0x5b: out += u')'; break;
            case 0x66: out += u'-'; break;
            case 0x2b: out += u'+'; break;
            case 0xb5: out += char16_t(0x0153); break;
            case 0xb6: out += char16_t(0x00df); break;
            case 0xcc: out += char16_t(0x00ba); break;
            case 0xe5: out += char16_t(0x00a1); break;
            case 0xe6: out += char16_t(0x00bf); break;
            case 0xec:
            case 0xee: out += u'\''; break;
            case 0xed: out += u'"'; break;
            default: break;
            }
    }
    return out;
}

// A message of KH2's (its number) in the game's language, from the mod's kh2/msg/<language>/<file>'s BAR entry
// `entry` (each: u32, u32 its count, then {u32 number, u32 offset}); empty when there is none
std::u16string kh2_message(const char *file, const char *entry, std::uint32_t id)
{
    static const char *const folders[] = {"us", "us", "fr", "gr", "it", "sp"};    // the game's 1 English .. 5 Spanish
    static std::map<std::pair<std::string, int>, std::vector<std::uint8_t>> read; // each file's, each language's
    const int language = khdays_link_language();
    const int l = language >= 1 && language <= 5 ? language : 1;
    auto it = read.find({file, l});
    if (it == read.end())
        it = read.emplace(std::make_pair(std::string(file), l),
                          read_file(kh2_file(std::string("msg/") + folders[l] + "/" + file)))
                 .first;
    const std::vector<std::uint8_t> &bar = it->second;
    try {
        for (const kh2::BarEntry &e : kh2::bar_entries(bar.data(), bar.size())) {
            if (e.name != entry || e.size < 8)
                continue;
            std::uint32_t count = 0;
            std::memcpy(&count, bar.data() + e.offset + 4, 4);
            for (std::uint32_t i = 0; i < count && 8 + 8 * (i + 1) <= e.size; ++i) {
                std::uint32_t mid = 0, at = 0;
                std::memcpy(&mid, bar.data() + e.offset + 8 + 8 * i, 4);
                std::memcpy(&at, bar.data() + e.offset + 12 + 8 * i, 4);
                if (mid == id && at < e.size)
                    return kh2_text(bar.data() + e.offset + at, e.size - at);
            }
        }
    } catch (const std::exception &) {
    }
    return {};
}

// A command's name (its text's number, 03system.bin's; its top bit cleared it is sys.bar's message) in the game's
// language; empty when there is none
std::u16string command_name(int text)
{
    return kh2_message("sys.bar", "sys", std::uint32_t(text) & 0x7fffu);
}

// A KH2 enemy's name in the game's language: its kind's title in KH2's journal (jm.bar's message, the number the mod's
// list gives it from menu/us/jiminy.bar); empty when its list gives none
std::u16string enemy_name(const std::string &kind)
{
    const Kind *k = find_kind(mod().kinds, kind);
    return k != nullptr && k->journal >= 0 ? kh2_message("jm.bar", "jm", std::uint32_t(k->journal)) : std::u16string{};
}

int put_name(int text, unsigned short *out, int room)
{
    if (text == 0 || out == nullptr || room <= 0)
        return 0;
    const std::u16string name = command_name(text);
    const int n = std::min(int(name.size()), room);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<unsigned short>(name[std::size_t(i)]);
    return n;
}
} // namespace

} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

extern "C" int khdays_kh2_enemies_offered_name(unsigned short *out, int room)
{
    int text = 0;
    return khdays_kh2_enemies_offered(&text) != 0 ? put_name(text, out, room) : 0;
}

// Ov002_DrawPanelRowText (prepare.py's join): the text of the deck's main block's line `line` while it shows the
// command menu a script gives -- lines 0 to 2 its first row (Attack, or Talk and the rest in its place), 3 to 5 the
// rows after it (Magic, Items) -- its UTF-16 letters into `out` (at most `room`, then a 0), how many; 0 Days's own
extern "C" int khdays_kh2_enemies_deck_label(int line, unsigned short *out, int room)
{
    const int row = line < 3 ? 0 : line - 2;
    if (!top_offer.shown || out == nullptr || room <= 0 || row >= int(top_offer.labels.size()))
        return 0;
    const int id = top_offer.labels[std::size_t(row)];
    const Command *c = id > 0 ? command(id) : nullptr;
    const std::u16string name = c != nullptr ? command_name(c->text) : std::u16string{};
    const int n = std::min(int(name.size()), room);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<unsigned short>(name[std::size_t(i)]);
    out[n] = 0;
    return n;
}

// ui/reaction_command.cpp, drawing the menu as KH2's own: how many slots the menu a script gives has (0 none shown)
// and its own cursor; asked each VBlank it draws it, which A then follows (take_command)
extern "C" int khdays_kh2_enemies_menu_slots(int *cursor)
{
    if (!top_offer.shown || !top_menu_standing())
        return 0;
    top_offer.drawn = khdays_link_frame();
    if (cursor != nullptr)
        *cursor = top_offer.cursor;
    return int(top_offer.commands.size());
}

// ... and slot `slot`'s command's name, UTF-16 into `out` (at most `room`), how many letters; 0 none
extern "C" int khdays_kh2_enemies_menu_slot(int slot, unsigned short *out, int room)
{
    if (!top_offer.shown || out == nullptr || room <= 0 || slot < 0 || slot >= int(top_offer.commands.size()))
        return 0;
    const int id = top_offer.commands[std::size_t(slot)];
    const Command *c = id > 0 ? command(id) : nullptr;
    const std::u16string name = c != nullptr ? command_name(c->text) : std::u16string{};
    const int n = std::min(int(name.size()), room);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<unsigned short>(name[std::size_t(i)]);
    return n;
}

// The name of the last reaction command this game's own player took and its id in `id` (0 none)
extern "C" int khdays_kh2_enemies_taken_name(unsigned short *out, int room, int *id)
{
    const Command *c = local_taken >= 0 ? command(local_taken) : nullptr;
    *id = c != nullptr ? local_taken : 0;
    return c != nullptr ? put_name(c->text, out, room) : 0;
}

// Ov002_BuildPanelLabels (prepare.py's join): the field's enemy labels -- one for each enemy class the mission has,
// drawn as the field begins, the targeted enemy's shown -- with KH2's name for a class whose every row in the mission
// stands in for one KH2 enemy: its UTF-16 letters into `out` (at most `room`, then a 0), how many; 0 for any other
// class (Days's name drawn)
extern "C" int khdays_kh2_enemies_label(int enemy_class, unsigned short *out, int room)
{
    char kind[128];
    if (out == nullptr || room <= 0 || mod().kinds.empty() ||
        !khdays_missions_kh2_enemy_of_class(enemy_class, kind, int(sizeof(kind))))
        return 0;
    const std::u16string name = enemy_name(kind);
    const int n = std::min(int(name.size()), room);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<unsigned short>(name[std::size_t(i)]);
    out[n] = 0;
    if (tracing())
        khdays_log("kh2 enemies: the enemy label of class %d is %s's KH2 name (%d letters)", enemy_class, kind, n);
    return n;
}
