// Every map of the collections taken out and made at once (core/rooms/collection_source.h, take_out_every_map), so
// that using one later is quick: the window asks where they are kept (the setting collections), says for each game
// how many maps its copy holds, how many are ready and about the space the rest take against the drive's free space,
// and takes the games chosen out (or one alone, its row's button) on a thread of its own, its progress shown and stopped
// when asked. Asked from a game's map list, that game is the only one chosen.
#include "../app.h"

#include "core/files/paths.h"
#include "core/rooms/collection_source.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace studio {

namespace {

namespace fs = std::filesystem;

struct TakeOut {
    std::thread worker;
    std::atomic<bool> busy{false}, stop{false};
    std::string counted_for = "\x01";    // the folder the plans were counted in
    std::map<std::string, bool> chosen;  // a game's code: taken out
    // left by the worker
    std::mutex mutex;
    std::vector<CollectionPlan> plans;  // collection_games()'s order; empty while counting
    bool counting = false, running = false;
    std::string game, doing;  // the game being taken out, its step
    std::size_t done = 0, total = 0;
    std::vector<std::string> failed;  // once done: the maps not made, with why
    std::string outcome;              // once done: what the run did
    bool told = true;                 // the outcome said in the notices
    bool ok = true;
    ~TakeOut() {
        stop = true;
        if (worker.joinable())
            worker.join();
    }
};

TakeOut t;

// The free space where the maps would go: that folder's drive (its nearest folder that is there).
std::uintmax_t free_space(const fs::path& folder) {
    std::error_code ec;
    for (fs::path f = folder; !f.empty(); f = f.parent_path()) {
        if (fs::exists(f, ec)) {
            const fs::space_info s = fs::space(f, ec);
            return ec ? 0 : s.available;
        }
        if (f == f.parent_path())
            break;
    }
    return 0;
}

// Each game's maps counted on the worker (its copy's indexes read, its folder walked).
void count_plans(App& app) {
    if (t.busy)
        return;
    if (t.worker.joinable())
        t.worker.join();
    t.busy = true;
    {
        std::lock_guard lock(t.mutex);
        t.counting = true;
        t.plans.clear();
    }
    t.counted_for = u8(collections_folder(app.settings));
    const Settings settings = app.settings;
    t.worker = std::thread([settings] {
        std::vector<CollectionPlan> plans;
        for (const auto& g : collection_games())
            plans.push_back(collection_plan(settings, g));
        std::lock_guard lock(t.mutex);
        t.plans = std::move(plans);
        t.counting = false;
        t.busy = false;
    });
}

// The games chosen taken out on the worker, one after the other (`only`: that game alone); counted again once done.
void start_taking_out(App& app, const std::string& only) {
    if (t.busy)
        return;
    if (t.worker.joinable())
        t.worker.join();
    std::vector<CollectionPlan> chosen;
    {
        std::lock_guard lock(t.mutex);
        for (const auto& plan : t.plans)
            if (!plan.maps.empty() && (only.empty() ? t.chosen[plan.game->code] : only == plan.game->code))
                chosen.push_back(plan);
        t.running = true;
        t.failed.clear();
        t.outcome.clear();
        t.done = t.total = 0;
    }
    t.busy = true;
    t.stop = false;
    const Settings settings = app.settings;
    const Language lang = app.language;
    t.worker = std::thread([settings, chosen, lang] {
        std::vector<std::string> failed;
        bool stopped = false;
        std::string why;
        for (const auto& plan : chosen) {
            {
                std::lock_guard lock(t.mutex);
                t.game = plan.game->name;
            }
            std::vector<std::string> not_made;
            const bool ran = take_out_every_map(
                settings, plan,
                [](const std::string& doing, std::size_t done, std::size_t total) {
                    std::lock_guard lock(t.mutex);
                    t.doing = doing;
                    t.done = done;
                    t.total = total;
                    return !t.stop.load();
                },
                &not_made, &why);
            for (const auto& f : not_made)
                failed.push_back(std::string(plan.game->code) + ":" + f);
            if (!ran) {
                stopped = t.stop.load();
                if (!stopped)
                    failed.push_back(std::string(plan.game->name) + ": " + why);
                break;
            }
        }
        // counted again: what is ready now
        std::vector<CollectionPlan> plans;
        std::size_t ready = 0;
        for (const auto& g : collection_games()) {
            plans.push_back(collection_plan(settings, g));
            ready += plans.back().ready;
        }
        std::lock_guard lock(t.mutex);
        t.plans = std::move(plans);
        t.failed = std::move(failed);
        t.ok = t.failed.empty();  // stopped when asked is no failure
        t.outcome = stopped ? tr(lang, S::take_out_stopped)
                            : fmt(lang, S::take_out_done, {std::to_string(ready), u8(collections_folder(settings))});
        t.told = false;
        t.running = false;
        t.busy = false;
    });
}

}  // namespace

bool collections_folder_field(App& app) {
    bool changed = false;
    {
        std::lock_guard lock(app.dialog_mutex);
        if (!app.dialog_maps_kept.empty()) {
            app.settings.collections = app.dialog_maps_kept.front();
            app.dialog_maps_kept.clear();
            changed = true;
        }
    }
    ImGui::PushID("collections");
    ImGui::BeginDisabled(t.running);  // the maps under way go where the run began
    // as wide as the settings' other folders, its name and button beside it in a wider window too
    ImGui::SetNextItemWidth(std::min(ImGui::CalcItemWidth(), 18 * ImGui::GetFontSize()));
    changed |= ImGui::InputText(tr(app.language, S::collections_folder), &app.settings.collections);
    ImGui::SameLine();
    if (ImGui::SmallButton(tr(app.language, S::choose_folder))) {
        SDL_ShowOpenFolderDialog(
            [](void* user, const char* const* folders, int) {
                if (folders == nullptr || folders[0] == nullptr)
                    return;
                App* a = static_cast<App*>(user);
                std::lock_guard lock(a->dialog_mutex);
                a->dialog_maps_kept.emplace_back(folders[0]);
            },
            &app, app.window, u8(collections_folder(app.settings)).c_str(), false);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::TextDisabled("%s",
                        fmt(app.language, S::collection_1525_found, {u8(collections_folder(app.settings))}).c_str());
    return changed;
}

namespace {

// The window's insides (draw_take_out_all): where, each game in a row, the total against the free space, the buttons
// or the progress, and what the last run did.
void draw_take_out_body(App& app) {
    const auto lang = app.language;
    WrappedTexts wrapped;
    // asked from a game's map list: that game alone chosen (not while a run goes on: what it takes out stays shown)
    if (!app.take_out_game.empty() && !t.running) {
        for (const auto& g : collection_games())
            t.chosen[g.code] = app.take_out_game == g.code;
    }
    app.take_out_game.clear();
    ImGui::TextWrapped("%s", tr(lang, S::take_out_hint));
    // where: the setting, saved as it changes
    if (collections_folder_field(app)) {
        std::string why;
        if (!save_settings(app.settings, &why))
            app.tell(fmt(lang, S::save_failed, {why}), false);
    }
    const fs::path folder = collections_folder(app.settings);
    if (!t.busy && t.counted_for != u8(folder))
        count_plans(app);
    ImGui::Separator();
    bool start = false;
    std::string only;  // a row's own button: that game alone
    std::unique_lock lock(t.mutex);
    if (t.counting) {
        ImGui::TextDisabled("%s", tr(lang, S::take_out_reading));
    } else {
        std::uintmax_t more = 0;
        if (ImGui::BeginTable("##games", 6,
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn(tr(lang, S::take_out_col_game));
            ImGui::TableSetupColumn(tr(lang, S::take_out_col_maps));
            ImGui::TableSetupColumn(tr(lang, S::take_out_col_ready));
            ImGui::TableSetupColumn(tr(lang, S::take_out_col_used));
            ImGui::TableSetupColumn(tr(lang, S::take_out_col_more));
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            for (const auto& plan : t.plans) {
                ImGui::PushID(plan.game->code);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (plan.maps.empty()) {  // no copy found: its why where the button would be
                    bool none = false;
                    ImGui::BeginDisabled();
                    ImGui::Checkbox(plan.game->name, &none);
                    ImGui::EndDisabled();
                    for (int c = 0; c < 4; ++c)
                        ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", plan.why.c_str());
                    ImGui::PopID();
                    continue;
                }
                // chosen at first when its copy is found
                const auto seen = t.chosen.emplace(plan.game->code, true).first;
                ImGui::BeginDisabled(t.running);
                ImGui::Checkbox(plan.game->name, &seen->second);
                ImGui::EndDisabled();
                if (seen->second)
                    more += plan.more;
                ImGui::TableNextColumn();
                ImGui::Text("%zu", plan.maps.size());
                ImGui::TableNextColumn();
                ImGui::Text("%zu", plan.ready);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(space_text(plan.used).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(space_text(plan.more).c_str());
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(t.busy || plan.ready >= plan.maps.size());
                if (ImGui::SmallButton(tr(lang, S::take_out_only)))
                    only = plan.game->code;
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        const std::uintmax_t free = free_space(folder);
        ImGui::TextWrapped("%s", fmt(lang, S::take_out_total, {space_text(more), space_text(free)}).c_str());
        if (more > free) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
            ImGui::TextWrapped("%s", tr(lang, S::take_out_no_room));
            ImGui::PopStyleColor();
        }
        bool any = false;
        for (const auto& plan : t.plans)
            any = any || (!plan.maps.empty() && t.chosen[plan.game->code] && plan.ready < plan.maps.size());
        if (t.running) {
            const float share = t.total > 0 ? static_cast<float>(t.done) / static_cast<float>(t.total) : 0.0f;
            ImGui::ProgressBar(share, ImVec2(-1, 0), fmt(lang, S::take_out_running, {t.game, t.doing}).c_str());
            if (ImGui::Button(tr(lang, S::take_out_stop)))
                t.stop = true;
        } else {
            ImGui::BeginDisabled(!any || t.busy);
            start = ImGui::Button(tr(lang, S::take_out_start));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(t.busy);
            if (ImGui::Button(tr(lang, S::take_out_count_again)))
                t.counted_for = "\x01";  // counted on the next frame
            ImGui::EndDisabled();
        }
    }
    // what the last run did
    if (!t.outcome.empty()) {
        ImGui::TextWrapped("%s", t.outcome.c_str());
        if (!t.failed.empty()) {
            ImGui::TextWrapped("%s", fmt(lang, S::take_out_not_made, {std::to_string(t.failed.size())}).c_str());
            ImGui::BeginChild("##failed", ImVec2(0, 8 * ImGui::GetTextLineHeightWithSpacing()),
                              ImGuiChildFlags_Borders);
            for (const auto& f : t.failed)
                ImGui::TextWrapped("%s", f.c_str());
            ImGui::EndChild();
        }
    }
    if (!t.told) {
        app.tell(t.outcome, t.ok);
        t.told = true;
    }
    lock.unlock();
    if (start || !only.empty())
        start_taking_out(app, only);
}

}  // namespace

void draw_take_out_all(App& app, bool* open) {
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(62 * em, 30 * em), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr(app.language, S::take_out_title), open))
        draw_take_out_body(app);
    ImGui::End();
}

}  // namespace studio
