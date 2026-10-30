// A text file of the game (a UI string table, a message database or the
// story's scenes), each string shown beside the one the mod gives, edited in
// place and saved whole into the mod (core/texts/texts.h): what a modder
// translating or rewording the game works in. The characters the game's font
// cannot draw are said.
#include "../app.h"

#include "core/mods/mods.h"
#include "core/files/paths.h"
#include "core/texts/texts.h"

#include "khdays/assets/font.h"
#include "khdays/assets/message.h"
#include "khdays/vfs/filesystem.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>

namespace studio {

namespace {

namespace assets = khdays::assets;

// What the person has edited in a file, kept while they look at other items
// (nothing they typed is lost by choosing something else).
struct Edits {
    std::vector<std::vector<std::string>> lists;  // per list, per string: the text as shown (escapes)
};

struct TextView {
    int item = -1;
    std::string mod;
    std::string error;
    TextFile game, start;  // the game's, and what the mod holds (or the game's) when opened
    int list = 0;
    std::string filter;
    int language = 1;  // scenes: the language shown (GetLanguage's 1 en .. 5 es)
};

TextView v;
std::map<std::string, Edits> pending;        // by the item's key (and a scene archive's language)
std::map<std::string, std::size_t> changes;  // the texts changed there when last shown
int scene_language = 0;                      // the language the scenes were last shown in; 0: the studio's

std::vector<std::uint8_t> read_bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The key of what is edited: a scene archive's languages are edited apart.
std::string edits_key(const Item& item, int language) {
    return is_scene_archive(item.key) ? item.key + "#" + std::to_string(language) : item.key;
}

bool parse_for(const Item& item, int language, const std::vector<std::uint8_t>& bytes, TextFile* out,
               std::string* why) {
    if (is_scene_archive(item.key))
        return parse_scene_file(bytes, language, out, why);
    return parse_text_file(bytes, is_text_database(item.key), out, why);
}

// The font the game draws these texts with (the European one; the Japanese one for ja).
const assets::Font* font_for(const Item& item) {
    static std::map<std::string, assets::Font> fonts;
    static std::map<std::string, bool> tried;
    const std::string name = item.language == "ja" ? "text/font_jp_10.nftr" : "text/font_eu_10.nftr";
    if (!tried[name]) {
        tried[name] = true;
        try {
            fonts[name] = assets::decode_nftr(khdays::vfs::data_root() / "nitrofs" / path_u8(name));
        } catch (const std::exception&) {
        }
    }
    const auto found = fonts.find(name);
    return found != fonts.end() ? &found->second : nullptr;
}

// The mod's copy of the item's file when it reads as the game's does (as many lists), else the game's.
std::vector<std::uint8_t> held_or_game(const Mod* m, const Item& item, int language) {
    if (m != nullptr) {
        const fs::path mine = m->dir / path_u8(replacement_path(item, "", false));
        std::error_code ec;
        TextFile held, game;
        std::string why;
        if (fs::is_regular_file(mine, ec)) {
            const std::vector<std::uint8_t> bytes = read_bytes(mine);
            if (parse_for(item, language, bytes, &held, &why) &&
                parse_for(item, language, read_bytes(item.original), &game, &why) &&
                held.lists.size() == game.lists.size()) {
                return bytes;
            }
        }
    }
    return read_bytes(item.original);
}

void open(App& app, const Item& item, int language) {
    v = TextView{};
    v.item = app.selected;
    v.mod = app.current_mod() != nullptr ? app.current_mod()->name : std::string();
    v.language = language;
    std::string why;
    if (!parse_for(item, language, read_bytes(item.original), &v.game, &why)) {
        v.error = why;
        return;
    }
    // the mod's own copy, when it has one: what is edited from
    if (!parse_for(item, language, held_or_game(app.current_mod(), item, language), &v.start, &why))
        v.start = v.game;
    Edits& e = pending[edits_key(item, language)];
    if (e.lists.empty()) {
        for (const auto& list : v.start.lists) {
            std::vector<std::string> shown;
            for (const auto& s : list)
                shown.push_back(assets::message_to_utf8(s));
            e.lists.push_back(std::move(shown));
        }
    }
}

// `t`'s texts made the edited ones, where they differ.
void apply(const Edits& e, TextFile* t) {
    for (std::size_t l = 0; l < t->lists.size() && l < e.lists.size(); ++l) {
        for (std::size_t k = 0; k < t->lists[l].size() && k < e.lists[l].size(); ++k) {
            if (e.lists[l][k] != assets::message_to_utf8(t->lists[l][k]))
                t->lists[l][k] = assets::message_from_utf8(e.lists[l][k]);
        }
    }
}

// The file with every edit (a scene archive: each language's in turn), written into the mod and read back.
void save(App& app, const Item& item, const Mod& m, std::size_t changed) {
    const auto lang = app.language;
    const fs::path dest = m.dir / path_u8(replacement_path(item, "", false));
    std::vector<std::uint8_t> bytes;
    std::vector<int> languages;
    std::string why;
    if (is_scene_archive(item.key)) {
        bytes = held_or_game(&m, item, v.language);
        for (int l = 1; l <= 5 && why.empty(); ++l) {
            const auto e = pending.find(edits_key(item, l));
            TextFile t;
            if (e == pending.end() || !parse_scene_file(bytes, l, &t, &why))
                continue;
            apply(e->second, &t);
            bytes = write_text_file(t);
            languages.push_back(l);
        }
    } else {
        TextFile out = v.start;
        apply(pending[item.key], &out);
        bytes = write_text_file(out);
        languages.push_back(v.language);
    }
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (why.empty()) {
        std::ofstream f(dest, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f)
            why = "cannot write " + u8(dest);
    }
    // read back as the game will: every edited language as typed
    const std::vector<std::uint8_t> back = why.empty() ? read_bytes(dest) : std::vector<std::uint8_t>();
    for (const int l : languages) {
        TextFile check;
        if (!why.empty() || !parse_for(item, l, back, &check, &why))
            break;
        TextFile want = check;
        apply(pending[edits_key(item, l)], &want);
        if (check.lists != want.lists)
            why = "it does not read back as written";
    }
    if (!why.empty()) {
        app.tell(fmt(lang, S::save_failed, {why}), false);
        return;
    }
    app.tell(fmt(lang, S::text_saved, {u8(dest.lexically_relative(app.settings.root)), std::to_string(changed)}) +
                 "  " + tr(lang, S::restart_note),
             true);
    parse_for(item, v.language, back, &v.start, &why);  // what the mod holds now
    for (const int l : languages)
        changes[edits_key(item, l)] = 0;
}

std::string lower_text(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

void draw_text_view(App& app, const Item& item) {
    const std::string mod_name = app.current_mod() != nullptr ? app.current_mod()->name : std::string();
    const bool scenes = is_scene_archive(item.key);
    if (scenes && scene_language == 0)
        scene_language = static_cast<int>(app.language) + 1;
    if (v.item != app.selected || v.mod != mod_name || (scenes && v.language != scene_language)) {
        // another language of the same scenes: the scene and the search stay
        const bool same = v.item == app.selected && v.mod == mod_name;
        const int list = v.list;
        const std::string filter = v.filter;
        open(app, item, scenes ? scene_language : 1);
        if (same) {
            v.list = list;
            v.filter = filter;
        }
    }
    const auto lang = app.language;
    if (!v.error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", v.error.c_str());
        return;
    }
    Edits& e = pending[edits_key(item, v.language)];
    std::size_t texts = 0, changed = 0;
    for (std::size_t l = 0; l < e.lists.size(); ++l) {
        texts += e.lists[l].size();
        for (std::size_t k = 0; k < e.lists[l].size(); ++k)
            changed += e.lists[l][k] != assets::message_to_utf8(v.start.lists[l][k]) ? 1 : 0;
    }
    changes[edits_key(item, v.language)] = changed;
    // a scene archive's other languages' edits are saved with this one's
    std::size_t to_save = changed;
    if (scenes) {
        to_save = 0;
        for (int l = 1; l <= 5; ++l)
            to_save += changes[edits_key(item, l)];
    }
    ImGui::TextUnformatted(
        fmt(lang, S::text_header, {item.key, std::to_string(texts), std::to_string(e.lists.size())}).c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr(lang, S::text_hint));
    if (scenes)
        ImGui::TextDisabled("%s", tr(lang, S::text_scene_hint));
    ImGui::PopTextWrapPos();

    // saving: the whole file into the mod, or the mod's copy taken away
    const Mod* m = app.current_mod();
    if (m == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::choose_mod_first));
    } else {
        ImGui::BeginDisabled(to_save == 0);
        if (ImGui::Button(fmt(lang, S::text_save, {std::to_string(to_save)}).c_str()))
            save(app, item, *m, to_save);
        ImGui::EndDisabled();
        std::error_code ec;
        const fs::path mine = m->dir / path_u8(replacement_path(item, "", false));
        if (fs::is_regular_file(mine, ec)) {
            same_line_if_room(button_width(tr(lang, S::text_give_back)));
            if (ImGui::Button(tr(lang, S::text_give_back))) {
                std::string why;
                if (remove_replacement(m->dir, item, &why)) {
                    for (int l = 1; l <= 5; ++l) {
                        pending.erase(edits_key(item, l));
                        changes.erase(edits_key(item, l));
                    }
                    app.tell(fmt(lang, S::removed, {item.key, m->name}), true);
                    v.item = -1;  // opened again from the game's
                    return;
                }
                app.tell(fmt(lang, S::save_failed, {why}), false);
            }
        }
    }

    // a scene archive's language: the one its lines are shown and edited in
    if (scenes) {
        ImGui::SetNextItemWidth(9 * ImGui::GetFontSize());
        if (ImGui::BeginCombo("##language", language_name(static_cast<Language>(v.language - 1)))) {
            for (int l = 1; l <= 5; ++l) {
                if (ImGui::Selectable(language_name(static_cast<Language>(l - 1)), l == v.language))
                    scene_language = l;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    // a database's lists, a scene archive's scenes
    if (e.lists.size() > 1) {
        const S label = scenes ? S::text_scene : S::text_list;
        ImGui::SetNextItemWidth(16 * ImGui::GetFontSize());
        const std::string current = fmt(
            lang, label, {std::to_string(v.list), std::to_string(e.lists[static_cast<std::size_t>(v.list)].size())});
        if (ImGui::BeginCombo("##list", current.c_str())) {
            for (std::size_t l = 0; l < e.lists.size(); ++l) {
                if (ImGui::Selectable(fmt(lang, label, {std::to_string(l), std::to_string(e.lists[l].size())}).c_str(),
                                      static_cast<int>(l) == v.list)) {
                    v.list = static_cast<int>(l);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", tr(lang, S::text_filter), &v.filter);

    if (v.list < 0 || static_cast<std::size_t>(v.list) >= e.lists.size())
        v.list = 0;
    if (e.lists.empty())
        return;
    auto& shown = e.lists[static_cast<std::size_t>(v.list)];
    const auto& game = v.game.lists[static_cast<std::size_t>(v.list)];
    std::vector<int> rows;
    const std::string want = lower_text(v.filter);
    for (std::size_t k = 0; k < shown.size(); ++k) {
        if (want.empty() || lower_text(shown[k]).find(want) != std::string::npos ||
            lower_text(assets::message_to_utf8(game[k])).find(want) != std::string::npos) {
            rows.push_back(static_cast<int>(k));
        }
    }
    const assets::Font* font = font_for(item);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("##texts", 3, flags))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(tr(lang, S::text_games), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(tr(lang, S::text_yours), ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const std::size_t k = static_cast<std::size_t>(rows[static_cast<std::size_t>(r)]);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%zu", k);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(assets::message_to_utf8(game[k]).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(static_cast<int>(k));
            const bool edited = shown[k] != assets::message_to_utf8(v.start.lists[static_cast<std::size_t>(v.list)][k]);
            if (edited)
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.32f, 0.18f, 1));
            const int lines = 1 + static_cast<int>(std::count(shown[k].begin(), shown[k].end(), '\n'));
            ImGui::InputTextMultiline("##t", &shown[k],
                                      ImVec2(-1, static_cast<float>(lines) * ImGui::GetTextLineHeight() +
                                                     2 * ImGui::GetStyle().FramePadding.y));
            if (edited)
                ImGui::PopStyleColor();
            // the characters the game cannot draw
            if (font != nullptr) {
                const std::u16string missing = missing_glyphs(assets::message_from_utf8(shown[k]),
                                                              [&](char16_t c) { return font->glyph_of(c) >= 0; });
                if (!missing.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1), "%s",
                                       fmt(lang, S::text_no_glyph, {assets::message_to_utf8(missing)}).c_str());
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

}  // namespace studio
