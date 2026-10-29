// The mod being made: choosing or creating it, what it holds
// and whether the port uses each file, removing one; and the settings.
#include "../app.h"
#include "../theme.h"

#include "core/files/paths.h"
#include "core/rooms/kh2_source.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>

namespace studio {

namespace {

// The settings written; when they cannot be, the person is told why.
void save_settings_told(App& app) {
    std::string why;
    if (!save_settings(app.settings, &why))
        app.tell(fmt(app.language, S::save_failed, {why}), false);
}

struct ModFile {
    std::string rel;  // relative to the mod
    int item = -1;    // the catalog item, -1: none
    bool companion = false;
    bool left_over = false;  // beside models, but no model of the mod names it
    bool used = false;
    bool whole = false;  // `files/<path>` of a game file the catalog does not list (one with an animation replaced)
};

// A game file replaced whole, as an item (files/<its path>).
Item whole_file_item(const std::string& rel) {
    Item item;
    item.kind = Kind::text_file;
    item.key = rel.substr(6);
    return item;
}

std::vector<ModFile> mod_files;
bool mod_files_stale = true;

void gather(App& app) {
    mod_files.clear();
    const Mod* m = app.current_mod();
    if (m == nullptr)
        return;
    const auto named = used_companions(m->dir);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(m->dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec))
            continue;
        ModFile f;
        f.rel = u8(it->path().lexically_relative(m->dir));
        if (f.rel.rfind("studio/", 0) == 0)
            continue;  // the tool's own
        std::string key = f.rel;
        if (key.rfind("files/", 0) != 0) {
            const auto dot = key.find_last_of('.');
            const auto slash = key.find_last_of('/');
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                key = key.substr(0, dot);
        }
        const auto found = app.by_path.find(key);
        if (found != app.by_path.end()) {
            f.item = found->second;
            const auto h = holders(app.mods_root, app.catalog.items[static_cast<std::size_t>(f.item)]);
            f.used = !h.empty() && h.front().mod == m->name && fs::equivalent(h.front().file, it->path(), type_ec);
        } else if (key.rfind("files/", 0) == 0 &&
                   fs::is_regular_file(khdays::vfs::data_root() / "nitrofs" / path_u8(key.substr(6)), type_ec)) {
            f.whole = true;
            const auto h = holders(app.mods_root, whole_file_item(f.rel));
            f.used = !h.empty() && h.front().mod == m->name && fs::equivalent(h.front().file, it->path(), type_ec);
        } else {
            f.companion = f.rel.find("/mdl/") != std::string::npos;  // a model's .bin, textures, bones.txt
            f.left_over = f.companion && !among(named, it->path());
        }
        mod_files.push_back(std::move(f));
    }
}

}  // namespace

void draw_mod_bar(App& app) {
    static bool show_files = false;
    static std::string new_name;
    static std::string new_error;
    const Mod* current = app.current_mod();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(app.language, S::mod_label));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320.0f * ImGui::GetStyle().FontScaleDpi);
    if (ImGui::BeginCombo("##mod", current != nullptr ? current->name.c_str() : tr(app.language, S::mod_none))) {
        for (int i = 0; i < static_cast<int>(app.mods.size()); ++i) {
            if (ImGui::Selectable(app.mods[static_cast<std::size_t>(i)].name.c_str(), i == app.mod)) {
                app.clear_candidate();
                app.mod = i;
                app.settings.mod = app.mods[static_cast<std::size_t>(i)].name;
                save_settings_told(app);
                mod_files_stale = true;
            }
        }
        ImGui::EndCombo();
    }
    same_line_if_room(button_width(tr(app.language, S::mod_new)));
    if (ImGui::Button(tr(app.language, S::mod_new))) {
        new_name.clear();
        new_error.clear();
        ImGui::OpenPopup("###newmod");
    }
    if (current != nullptr) {
        ImGui::SameLine();
        if (mod_files_stale && show_files) {
            gather(app);
            mod_files_stale = false;
        }
        if (ImGui::Button(fmt(app.language, S::in_this_mod, {current->name}).c_str())) {
            show_files = true;
            mod_files_stale = true;
        }
    }
    if (ImGui::BeginPopupModal((std::string(tr(app.language, S::mod_new)) + "###newmod").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(320.0f * ImGui::GetStyle().FontScaleDpi);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool enter =
            ImGui::InputText(tr(app.language, S::mod_name), &new_name, ImGuiInputTextFlags_EnterReturnsTrue);
        if (!new_error.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", new_error.c_str());
        if (ImGui::Button(tr(app.language, S::create)) || enter) {
            std::string why;
            if (create_mod(app.mods_root, new_name, &why)) {
                app.settings.mod = new_name;
                save_settings_told(app);
                app.tell(fmt(app.language, S::mod_created,
                             {new_name, u8(app.mods_root.lexically_relative(app.settings.root) / path_u8(new_name))}),
                         true);
                app.mod = -1;
                app.refresh_mods();
                mod_files_stale = true;
                ImGui::CloseCurrentPopup();
            } else {
                new_error = tr(app.language, why == "exists" ? S::mod_exists : S::mod_name_invalid);
            }
        }
        same_line_if_room(button_width(tr(app.language, S::cancel)));
        if (ImGui::Button(tr(app.language, S::cancel)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (show_files)
        draw_replacements(app, &show_files);
}

void draw_replacements(App& app, bool* open) {
    const Mod* m = app.current_mod();
    if (m == nullptr) {
        *open = false;
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(760, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::Begin(
            fmt(app.language, S::replacements_title, {std::to_string(mod_files.size())}).append("###files").c_str(),
            open)) {
        ImGui::End();
        return;
    }
    static std::string remove_rel;
    static int remove_item = -1;
    static bool remove_whole = false;
    bool ask_remove = false;
    // every file left over at once (a model removed before took none of its files)
    const auto left = static_cast<std::size_t>(
        std::count_if(mod_files.begin(), mod_files.end(), [](const ModFile& f) { return f.left_over; }));
    if (left > 0) {
        if (ImGui::Button(fmt(app.language, S::remove_left_over, {std::to_string(left)}).c_str())) {
            ImGui::OpenPopup("###remove_left");
        }
        if (ImGui::BeginPopupModal((std::string(tr(app.language, S::remove)) + "###remove_left").c_str(), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                fmt(app.language, S::remove_left_over_confirm, {std::to_string(left), m->name}).c_str());
            if (ImGui::Button(tr(app.language, S::remove))) {
                std::size_t removed = 0;
                std::string why;
                for (const auto& f : mod_files) {
                    if (f.left_over && remove_mod_file(m->dir, m->dir / path_u8(f.rel), &why))
                        ++removed;
                }
                if (removed == left) {
                    app.tell(fmt(app.language, S::left_over_removed, {std::to_string(removed), m->name}), true);
                } else {
                    app.tell(fmt(app.language, S::save_failed, {why}), false);
                }
                mod_files_stale = true;
                ImGui::CloseCurrentPopup();
            }
            same_line_if_room(button_width(tr(app.language, S::cancel)));
            if (ImGui::Button(tr(app.language, S::cancel)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();
    }
    for (std::size_t i = 0; i < mod_files.size(); ++i) {
        const ModFile& f = mod_files[i];
        ImGui::PushID(static_cast<int>(i));
        if (f.whole) {
            if (ImGui::SmallButton(tr(app.language, S::remove))) {
                remove_rel = f.rel;
                remove_item = -1;
                remove_whole = true;
                ask_remove = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(f.rel.c_str());
            ImGui::SameLine();
            if (f.used) {
                ImGui::TextDisabled("%s", tr(app.language, S::whole_game_file));
            } else {
                const auto h = holders(app.mods_root, whole_file_item(f.rel));
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s",
                                   fmt(app.language, S::in_game_now, {h.empty() ? "?" : h.front().mod}).c_str());
            }
        } else if (f.item >= 0) {
            if (ImGui::SmallButton(tr(app.language, S::remove))) {
                remove_rel = f.rel;
                remove_item = f.item;
                remove_whole = false;
                ask_remove = true;  // opened below, outside this row's ID
            }
            ImGui::SameLine();
            if (ImGui::Selectable(f.rel.c_str(), app.selected == f.item)) {
                stop_sounds();
                app.clear_candidate();
                app.selected = f.item;
            }
            if (!f.used) {
                const auto h = holders(app.mods_root, app.catalog.items[static_cast<std::size_t>(f.item)]);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s",
                                   fmt(app.language, S::in_game_now, {h.empty() ? "?" : h.front().mod}).c_str());
            }
        } else if (f.left_over) {
            if (ImGui::SmallButton(tr(app.language, S::remove))) {
                remove_rel = f.rel;
                remove_item = -1;
                remove_whole = false;
                ask_remove = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(f.rel.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(app.language, S::left_over));
        } else {
            ImGui::TextUnformatted(f.rel.c_str());
            if (!f.companion) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1), "%s", tr(app.language, S::replacement_unknown));
            }
        }
        ImGui::PopID();
    }
    if (ask_remove)
        ImGui::OpenPopup("###remove");
    if (ImGui::BeginPopupModal((std::string(tr(app.language, S::remove)) + "###remove").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(fmt(app.language, S::remove_confirm, {remove_rel, m->name}).c_str());
        if (ImGui::Button(tr(app.language, S::remove))) {
            std::string why;
            stop_sounds();
            bool ok = false;
            if (remove_item < 0 && !remove_whole) {
                ok = remove_mod_file(m->dir, m->dir / path_u8(remove_rel), &why);  // a file left over: it alone
            } else {
                const Item item = remove_whole ? whole_file_item(remove_rel)
                                               : app.catalog.items[static_cast<std::size_t>(remove_item)];
                ok = remove_replacement(m->dir, item, &why);
            }
            if (!ok) {
                app.tell(fmt(app.language, S::save_failed, {why}), false);
            } else {
                app.tell(fmt(app.language, S::removed, {remove_rel, m->name}), true);
            }
            mod_files_stale = true;
            ImGui::CloseCurrentPopup();
        }
        same_line_if_room(button_width(tr(app.language, S::cancel)));
        if (ImGui::Button(tr(app.language, S::cancel)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
    if (mod_files_stale) {
        gather(app);
        mod_files_stale = false;
    }
}

// The Themes window: the studio's colour palette, applied at once and kept in
// studio.ini.
void draw_themes(App& app, bool* open) {
    const float k = ImGui::GetStyle().FontScaleDpi;
    // first under its button, at the window's right
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 16.0f * k, vp->WorkPos.y + 44.0f * k),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    if (!ImGui::Begin(tr(app.language, S::themes), open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(tr(app.language, S::color_palette));
    const int current = theme_index(app.settings.theme);
    ImGui::SetNextItemWidth(240.0f * k);
    if (ImGui::BeginCombo("##palette", theme_label(current, app.language))) {
        for (int i = 0; i < theme_count(); ++i) {
            if (ImGui::Selectable(theme_label(i, app.language), i == current) && i != current) {
                app.settings.theme = theme_id(i);
                apply_theme(app.settings.theme);
                save_settings_told(app);
            }
            if (i == current)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::End();
}

void draw_settings(App& app, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr(app.language, S::settings), open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    bool changed = false;
    const char* current = app.settings.language.empty()
                              ? tr(app.language, S::language_system)
                              : language_name(language_from_code(app.settings.language, Language::en));
    if (ImGui::BeginCombo(tr(app.language, S::language), current)) {
        if (ImGui::Selectable(tr(app.language, S::language_system), app.settings.language.empty())) {
            app.settings.language.clear();
            app.language = system_language();
            changed = true;
        }
        for (int l = 0; l < static_cast<int>(Language::count); ++l) {
            const Language lang = static_cast<Language>(l);
            if (ImGui::Selectable(language_name(lang), app.settings.language == language_code(lang))) {
                app.settings.language = language_code(lang);
                app.language = lang;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    // the game's folder, found as the tool started (main.cpp)
    if (is_port_folder(app.settings.root))
        ImGui::TextDisabled("%s", fmt(app.language, S::port_folder, {u8(app.settings.root)}).c_str());
    else
        ImGui::TextWrapped("%s", tr(app.language, S::port_folder_none));
    bool paths = false;
    {
        std::lock_guard lock(app.dialog_mutex);
        if (!app.dialog_ports.empty()) {
            app.settings.port = app.dialog_ports.front();
            app.dialog_ports.clear();
            paths = true;
        }
    }
    paths |= ImGui::InputText(tr(app.language, S::port_exe), &app.settings.port);
    ImGui::SameLine();
    if (ImGui::SmallButton(tr(app.language, S::choose_file))) {
#ifdef _WIN32
        static const SDL_DialogFileFilter programs[] = {{"khdays-native", "exe"}};
        const int filters = 1;
#else
        static const SDL_DialogFileFilter* programs = nullptr;  // programs have no extension of their own there
        const int filters = 0;
#endif
        SDL_ShowOpenFileDialog(
            [](void* user, const char* const* files, int) {
                if (files == nullptr || files[0] == nullptr)
                    return;
                App* a = static_cast<App*>(user);
                std::lock_guard lock(a->dialog_mutex);
                a->dialog_ports.emplace_back(files[0]);
            },
            &app, app.window, programs, filters, u8(app.settings.root).c_str(), false);
    }
    {
        const fs::path exe = port_exe(app.settings);
        std::error_code ec;
        if (!app.settings.port.empty() && fs::is_regular_file(exe, ec))
            ImGui::TextDisabled("%s", fmt(app.language, S::port_exe_found, {u8(exe)}).c_str());
        else
            ImGui::TextWrapped("%s", tr(app.language, S::port_exe_missing));
    }
    paths |= ImGui::InputText(tr(app.language, S::graphics_dump), &app.settings.graphics_dump);
    paths |= ImGui::InputText(tr(app.language, S::sounds_dump), &app.settings.sounds_dump);
    changed |= ImGui::InputText(tr(app.language, S::ffmpeg), &app.settings.ffmpeg);
    // KH2's rooms: the 1.5+2.5 collection's folder, typed or chosen; empty, the install's
    {
        std::lock_guard lock(app.dialog_mutex);
        if (!app.dialog_collections.empty()) {
            app.settings.collection_1525 = app.dialog_collections.front();
            app.dialog_collections.clear();
            changed = true;
        }
    }
    changed |= ImGui::InputText(tr(app.language, S::collection_1525), &app.settings.collection_1525);
    ImGui::SameLine();
    if (ImGui::SmallButton(tr(app.language, S::choose_folder))) {
        SDL_ShowOpenFolderDialog(
            [](void* user, const char* const* folders, int) {
                if (folders == nullptr || folders[0] == nullptr)
                    return;
                App* a = static_cast<App*>(user);
                std::lock_guard lock(a->dialog_mutex);
                a->dialog_collections.emplace_back(folders[0]);
            },
            &app, app.window, nullptr, false);
    }
    {
        static std::string found_for = "\x01";
        static std::string found;
        if (found_for != app.settings.collection_1525) {
            found = u8(kh2_collection(app.settings));
            found_for = app.settings.collection_1525;
        }
        ImGui::TextDisabled("%s", found.empty() ? tr(app.language, S::collection_1525_none)
                                                : fmt(app.language, S::collection_1525_found, {found}).c_str());
    }
    if (ImGui::InputInt(tr(app.language, S::cache), &app.settings.cache_mb)) {
        if (app.settings.cache_mb < 1)
            app.settings.cache_mb = 1;
        app.images->set_budget(static_cast<std::size_t>(app.settings.cache_mb) << 20);
        changed = true;
    }
    if (changed || paths)
        save_settings_told(app);
    if (ImGui::Button(tr(app.language, S::reload)))
        app.start_catalog();
    same_line_if_room(button_width(tr(app.language, S::close)));
    if (ImGui::Button(tr(app.language, S::close)))
        *open = false;
    ImGui::End();
}

}  // namespace studio
