// The catalog on the left: search, kinds, the items.
#include "../app.h"

#include "core/characters/character_files.h"
#include "core/files/catalog_tree.h"
#include "core/files/paths.h"
#include "core/missions/mission_names.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"
#include "imgui_stdlib.h"

namespace studio {

namespace {

S kind_word(Kind k) {
    switch (k) {
    case Kind::texture: return S::kind_texture;
    case Kind::sheet: return S::kind_sheet;
    case Kind::model: return S::kind_model;
    case Kind::sound: return S::kind_sound;
    case Kind::text_file: return S::kind_text;
    case Kind::movie: return S::kind_movie;
    case Kind::room: return S::kind_room;
    case Kind::mission: return S::kind_mission;
    }
    return S::kind_texture;
}

const char* kind_tag(Kind k) {
    switch (k) {
    case Kind::texture: return "TEX";
    case Kind::sheet: return "2D";
    case Kind::model: return "MDL";
    case Kind::sound: return "SND";
    case Kind::text_file: return "TXT";
    case Kind::movie: return "MOV";
    case Kind::room: return "MAP";
    case Kind::mission: return "MIS";
    }
    return "?";
}

// The catalog as folders (core/files/catalog_tree.h), rebuilt when the results change.
struct TreeView {
    CatalogTree tree;
    std::vector<int> built_from;  // the results it was built from
    bool by_kind = true;
    bool open_found = false;   // a search's results: their folders opened once
    int revealed = -1;         // the item last opened to and scrolled to
    std::vector<char> reveal;  // nodes to open this frame (on the way to the selected item)
};
TreeView tv;

void select_item(App& app, int index) {
    if (app.selected != index) {
        stop_sounds();
        app.clear_candidate();
        app.selected = index;
    }
}

// What a folder holds, where its name says it: a world's archive (mi/wd/wd_tt, its parts wd_tt#3) or a character's
// folder (ba/ch/ro); empty for any other.
std::string folder_meaning(App& app, const TreeNode& node) {
    const std::string& name = node.name;
    if (name.rfind("wd_", 0) == 0 && name.size() >= 5 && (name.size() == 5 || name[5] == '#' || name[5] == '.')) {
        const std::string code = name.substr(3, 2);
        const std::string world = world_name(app.language, code);
        if (world != code)
            return world;
    }
    if (node.parent > 0 && tv.tree.nodes[static_cast<std::size_t>(node.parent)].name == "ch") {
        for (const auto& base : character_bases())
            if (character_base_code(base) == name)
                return character_display_name(base);
        if (name == "r2")  // the folders' 20th (data_02042a70), ov049's: not a base a new character plays as
            return tr(app.language, S::folder_dual_roxas);
    }
    return {};
}

// An item's name as listed: a mission file's (mi/mi/0105) with the name the game's list gives it, in the window's
// language (read from the player's copy).
std::string item_name(App& app, const Item& item, const std::string& leaf) {
    if (item.kind != Kind::mission)
        return leaf;
    static std::map<Language, std::map<int, GameMissionName>> read;
    auto found = read.find(app.language);
    if (found == read.end())
        found = read.emplace(app.language,
                             game_mission_names(khdays::vfs::data_root() / "nitrofs", language_code(app.language)))
                    .first;
    const auto named = found->second.find(std::atoi(leaf.c_str()));
    return named == found->second.end() ? leaf : leaf + "   " + named->second.name;
}

void draw_tree_node(App& app, int n) {
    const TreeNode& node = tv.tree.nodes[static_cast<std::size_t>(n)];
    if (tv.open_found)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (!tv.reveal.empty() && tv.reveal[static_cast<std::size_t>(n)])
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    const std::string meaning = node.kind >= 0 ? std::string() : folder_meaning(app, node);
    const std::string name = node.kind >= 0    ? std::string(tr(app.language, kind_word(static_cast<Kind>(node.kind))))
                             : meaning.empty() ? node.name
                                               : node.name + "  -  " + meaning;
    const std::string label = name + "  (" + std::to_string(node.count) + ")###n" + std::to_string(n);
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.item >= 0 && app.selected == node.item)
        flags |= ImGuiTreeNodeFlags_Selected;
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (node.item >= 0) {
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            select_item(app, node.item);
        if (tv.revealed != app.selected && app.selected == node.item) {
            ImGui::SetScrollHereY(0.3f);
            tv.revealed = app.selected;
        }
        if (ImGui::IsItemHovered()) {
            const Item& item = app.catalog.items[static_cast<std::size_t>(node.item)];
            ImGui::SetTooltip("%s  %s", kind_tag(item.kind), item.group.c_str());
        }
    }
    if (!open)
        return;
    for (const int c : node.children)
        draw_tree_node(app, c);
    // the leaves: only those in view drawn
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(node.items.size()));
    // the selected leaf, when it is to be shown, kept in the clipped range
    int wanted = -1;
    if (tv.revealed != app.selected) {
        for (std::size_t i = 0; i < node.items.size(); ++i) {
            if (node.items[i] == app.selected)
                wanted = static_cast<int>(i);
        }
    }
    if (wanted >= 0)
        clipper.IncludeItemByIndex(wanted);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int index = node.items[static_cast<std::size_t>(row)];
            const Item& item = app.catalog.items[static_cast<std::size_t>(index)];
            ImGui::PushID(index);
            ImGui::TreeNodeEx("##leaf",
                              ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                  ImGuiTreeNodeFlags_SpanAvailWidth |
                                  (app.selected == index ? ImGuiTreeNodeFlags_Selected : 0),
                              "%s  %s", kind_tag(item.kind), item_name(app, item, leaf_name(item.key)).c_str());
            if (ImGui::IsItemClicked())
                select_item(app, index);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", item.group.c_str());
            if (row == wanted) {
                ImGui::SetScrollHereY(0.3f);
                tv.revealed = app.selected;
            }
            ImGui::PopID();
        }
    }
    ImGui::TreePop();
}

void draw_tree(App& app) {
    const bool by_kind = app.kinds == (1u << kKinds) - 1u;
    if (tv.built_from != app.results || tv.by_kind != by_kind || tv.tree.nodes.empty()) {
        tv.tree = build_catalog_tree(app.catalog, app.results, by_kind);
        tv.built_from = app.results;
        tv.by_kind = by_kind;
        tv.open_found = !app.search.empty();
        tv.revealed = -1;
    }
    // the way to the selected item opened, once each time it changes
    tv.reveal.clear();
    if (app.selected >= 0 && tv.revealed != app.selected &&
        static_cast<std::size_t>(app.selected) < tv.tree.node_of_item.size()) {
        int n = tv.tree.node_of_item[static_cast<std::size_t>(app.selected)];
        if (n >= 0) {
            tv.reveal.assign(tv.tree.nodes.size(), 0);
            // the node holding it (and, when it is the node's own item, its parents)
            if (tv.tree.nodes[static_cast<std::size_t>(n)].item == app.selected)
                n = tv.tree.nodes[static_cast<std::size_t>(n)].parent;
            for (; n > 0; n = tv.tree.nodes[static_cast<std::size_t>(n)].parent)
                tv.reveal[static_cast<std::size_t>(n)] = 1;
        } else {
            tv.revealed = app.selected;  // not in these results
        }
    }
    ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None);
    for (const int c : tv.tree.nodes[0].children)
        draw_tree_node(app, c);
    for (const int index : tv.tree.nodes[0].items) {  // keys with no folder
        const Item& item = app.catalog.items[static_cast<std::size_t>(index)];
        ImGui::PushID(index);
        ImGui::TreeNodeEx("##leaf",
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth |
                              (app.selected == index ? ImGuiTreeNodeFlags_Selected : 0),
                          "%s  %s", kind_tag(item.kind), item.key.c_str());
        if (ImGui::IsItemClicked())
            select_item(app, index);
        ImGui::PopID();
    }
    ImGui::EndChild();
    tv.open_found = false;
}

}  // namespace

void draw_browser(App& app) {
    if (!app.catalog_ready && app.catalog_thread.joinable()) {
        ImGui::TextUnformatted(
            fmt(app.language, S::loading_catalog, {std::to_string(app.catalog_progress.load())}).c_str());
        return;
    }
    draw_dump_panel(app);

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", tr(app.language, S::search), &app.search))
        app.results_stale = true;
    // one kind or all of them
    const unsigned all = (1u << kKinds) - 1u;
    int shown = -1;
    for (int k = 0; k < kKinds; ++k) {
        if (app.kinds == 1u << static_cast<unsigned>(k))
            shown = k;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##kinds", tr(app.language, shown < 0 ? S::kind_all : kind_word(static_cast<Kind>(shown))))) {
        if (ImGui::Selectable(tr(app.language, S::kind_all), shown < 0)) {
            app.kinds = all;
            app.results_stale = true;
        }
        for (int k = 0; k < kKinds; ++k) {
            if (ImGui::Selectable(tr(app.language, kind_word(static_cast<Kind>(k))), shown == k)) {
                app.kinds = 1u << static_cast<unsigned>(k);
                app.results_stale = true;
            }
        }
        ImGui::EndCombo();
    }
    if (app.results_stale) {
        app.results = app.catalog.search(app.search, app.kinds);
        app.results_stale = false;
    }
    ImGui::TextDisabled("%s", fmt(app.language, S::results, {std::to_string(app.results.size())}).c_str());
    // folders or one list, kept in the settings
    ImGui::SameLine();
    bool tree = app.settings.browser_tree;
    if (ImGui::RadioButton(tr(app.language, S::browser_folders), tree))
        tree = true;
    same_line_if_room(check_width(tr(app.language, S::browser_list)));
    if (ImGui::RadioButton(tr(app.language, S::browser_list), !tree))
        tree = false;
    if (tree != app.settings.browser_tree) {
        app.settings.browser_tree = tree;
        std::string why;
        if (!save_settings(app.settings, &why))
            app.tell(fmt(app.language, S::save_failed, {why}), false);
    }
    if (tree) {
        draw_tree(app);
        return;
    }

    const ImGuiTableFlags flags =
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("##items", 3, flags))
        return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(app.results.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int index = app.results[static_cast<std::size_t>(row)];
            const Item& item = app.catalog.items[static_cast<std::size_t>(index)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(index);
            const bool selected = app.selected == index;
            if (ImGui::Selectable(kind_tag(item.kind), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                select_item(app, index);
            }
            ImGui::PopID();
            const auto slash = item.key.find_last_of('/');
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(
                item_name(app, item, slash == std::string::npos ? item.key : item.key.substr(slash + 1)).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", item.group.c_str());
        }
    }
    ImGui::EndTable();
}

void draw_dump_panel(App& app) {
    auto offer_dump = [&](const char* kind, S missing, const std::string& setting) {
        ImGui::TextWrapped("%s", fmt(app.language, missing, {setting}).c_str());
        if (app.job)
            return;
        const fs::path exe = port_exe(app.settings);
        std::error_code ec;
        if (app.settings.port.empty() || !fs::is_regular_file(exe, ec)) {
            ImGui::TextWrapped("%s", tr(app.language, S::no_port));
            return;
        }
        ImGui::PushID(kind);
        if (ImGui::Button(tr(app.language, S::make_dump))) {
            app.job = start_port_dump(exe, app.settings.root, kind, resolve(app.settings, setting));
            app.job_kind = kind;
        }
        ImGui::PopID();
    };
    if (!app.catalog.game_files_found)
        ImGui::TextWrapped("%s", tr(app.language, S::no_game_files));
    if (!app.catalog.graphics_dump_found)
        offer_dump("graphics", S::no_graphics_dump, app.settings.graphics_dump);
    if (!app.catalog.sounds_dump_found)
        offer_dump("sounds", S::no_sounds_dump, app.settings.sounds_dump);
    draw_bulk_export(app);

    if (app.job) {
        const std::string last = app.job->last_line();
        if (!app.job->done()) {
            ImGui::TextUnformatted(tr(app.language, S::dump_running));
        } else if (app.job->exit_code() == 0) {
            ImGui::TextUnformatted(tr(app.language, S::dump_done));
        } else {
            ImGui::TextUnformatted(fmt(app.language, S::dump_failed, {std::to_string(app.job->exit_code())}).c_str());
            if (!app.job->error().empty())
                ImGui::TextWrapped("%s", app.job->error().c_str());
        }
        if (!last.empty())
            ImGui::TextDisabled("%s", last.c_str());
        if (app.job->done()) {
            if (ImGui::Button(tr(app.language, S::reload))) {
                app.job.reset();
                app.start_catalog();
            }
        }
        ImGui::Separator();
    }
}

}  // namespace studio
