// A room of the mod's own added from a collection's map or the modder's model (core/rooms/added_rooms.h), none of
// the game's replaced: the window asking the world it goes to and its key, the map chosen beforehand (the map view's
// lists) or in it (the missions': a game, its world and the map, or the modder's own model), the room made and added
// on a thread of its own. A mission is then played there (its Room, above its map).
#include "../app.h"

#include "core/files/paths.h"
#include "core/rooms/added_rooms.h"
#include "core/rooms/collection_source.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace studio {

namespace {

namespace fs = std::filesystem;

struct RoomAdder {
    std::string owner;                         // the view that asked (and is told the room added)
    bool choosing = false;                     // the map is chosen in the window
    const CollectionGameInfo* game = nullptr;  // the map's game; none: the modder's model
    CollectionMap map;
    fs::path model;
    std::string world, key;
    bool ask = false;  // the window opened on the next frame
    int game_shown = 0, world_shown = 0, map_shown = 0;
    std::map<std::string, std::vector<CollectionMap>> listed;  // by game code
    std::string listing;                                       // the game whose maps are being listed
    std::thread worker;
    std::atomic<bool> busy{false};
    // left by the worker, or by the file dialog
    std::mutex mutex;
    bool done = false, ok = false, close = false;
    std::vector<std::string> lines;
    std::string why, picked, picked_owner;
    ~RoomAdder() {
        if (worker.joinable())
            worker.join();
    }
};
RoomAdder adder;

fs::path nitrofs() {
    return khdays::vfs::data_root() / "nitrofs";
}

// A room's key from a map's or a file's name: its letters, digits, _ and -, anything else as _.
std::string room_key_from(const std::string& name) {
    std::string key;
    for (const char c : name)
        key += std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' ? c : '_';
    return key.empty() ? std::string("room") : key;
}

void ask(const std::string& owner, const CollectionGameInfo* game, const CollectionMap& map, const fs::path& model,
         const std::string& world, bool choosing) {
    if (adder.busy)
        return;
    adder.owner = owner;
    adder.choosing = choosing;
    adder.game = game;
    adder.map = map;
    adder.model = model;
    adder.world = world;
    adder.key = choosing ? std::string() : room_key_from(game != nullptr ? map.name : u8(model.stem()));
    adder.ask = true;
}

void start(std::function<void(std::vector<std::string>*, std::string*, bool*)> job) {
    if (adder.worker.joinable())
        adder.worker.join();
    adder.busy = true;
    adder.worker = std::thread([job = std::move(job)] {
        std::vector<std::string> lines;
        std::string why;
        bool ok = false;
        job(&lines, &why, &ok);
        std::lock_guard lock(adder.mutex);
        adder.lines = std::move(lines);
        adder.why = why;
        adder.ok = ok;
        adder.done = true;
        adder.busy = false;
    });
}

// The game's maps listed on the worker (a second or so: its packages' indexes read).
void list_maps(const Settings& settings, const CollectionGameInfo* game) {
    adder.listing = game->code;
    start([settings, game](std::vector<std::string>*, std::string* why, bool* ok) {
        std::vector<CollectionMap> maps;
        *ok = collection_maps(settings, *game, &maps, why);
        std::lock_guard lock(adder.mutex);
        adder.listed[game->code] = std::move(maps);
    });
}

void add_room(App& app, const Mod& mod) {
    const Settings settings = app.settings;
    const fs::path game_files = nitrofs(), mod_dir = mod.dir, model = adder.model;
    const CollectionGameInfo* game = adder.game;
    const CollectionMap map = adder.map;
    const std::string world = adder.world, key = adder.key;
    adder.listing.clear();
    start([settings, game_files, mod_dir, model, game, map, world, key](std::vector<std::string>* lines,
                                                                        std::string* why, bool* ok) {
        *ok = game != nullptr
                  ? add_collection_room(settings, game_files, mod_dir, world, key, *game, map, {}, lines, why)
                  : add_model_room(game_files, mod_dir, world, key, model, {}, lines, why);
    });
}

// The chooser's part: a game, one of its worlds and a map (listed once a game is chosen), or the modder's model.
void draw_choice(App& app) {
    const auto lang = app.language;
    const auto& games = collection_games();
    std::vector<const char*> names;
    for (const auto& g : games)
        names.push_back(g.name);
    const float em = ImGui::GetFontSize();
    ImGui::BeginDisabled(adder.busy);
    ImGui::SetNextItemWidth(22 * em);
    if (ImGui::Combo(tr(lang, S::map_other_game), &adder.game_shown, names.data(), static_cast<int>(names.size()))) {
        adder.world_shown = adder.map_shown = 0;
    }
    const CollectionGameInfo* game = &games[static_cast<std::size_t>(adder.game_shown)];
    std::vector<CollectionMap> maps;
    bool known = false;
    {
        std::lock_guard lock(adder.mutex);
        const auto found = adder.listed.find(game->code);
        known = found != adder.listed.end();
        if (known)
            maps = found->second;
    }
    if (!known && !adder.busy)
        list_maps(app.settings, game);
    if (adder.busy && adder.listing == game->code)
        ImGui::TextDisabled("%s", fmt(lang, S::adder_listing, {game->name}).c_str());
    std::vector<std::string> worlds;
    for (const auto& mp : maps)
        if (worlds.empty() || worlds.back() != mp.world)
            worlds.push_back(mp.world);
    if (!worlds.empty()) {
        adder.world_shown = std::clamp(adder.world_shown, 0, static_cast<int>(worlds.size()) - 1);
        const std::string& world = worlds[static_cast<std::size_t>(adder.world_shown)];
        std::vector<const CollectionMap*> in_world;
        for (const auto& mp : maps)
            if (mp.world == world)
                in_world.push_back(&mp);
        ImGui::SetNextItemWidth(8 * em);
        if (ImGui::BeginCombo(tr(lang, S::map_other_world), world.c_str())) {
            for (std::size_t w = 0; w < worlds.size(); ++w)
                if (ImGui::Selectable(worlds[w].c_str(), static_cast<int>(w) == adder.world_shown)) {
                    adder.world_shown = static_cast<int>(w);
                    adder.map_shown = 0;
                }
            ImGui::EndCombo();
        }
        adder.map_shown = std::clamp(adder.map_shown, 0, static_cast<int>(in_world.size()) - 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(12 * em);
        const CollectionMap* chosen = in_world[static_cast<std::size_t>(adder.map_shown)];
        if (ImGui::BeginCombo(tr(lang, S::adder_map), chosen->name.c_str())) {
            for (std::size_t k = 0; k < in_world.size(); ++k)
                if (ImGui::Selectable(in_world[k]->name.c_str(), static_cast<int>(k) == adder.map_shown))
                    adder.map_shown = static_cast<int>(k);
            ImGui::EndCombo();
        }
        // the key follows the map until the person types one
        if (adder.game != game || adder.map.name != chosen->name) {
            if (adder.key.empty() || adder.key == room_key_from(adder.map.name))
                adder.key = room_key_from(chosen->name);
            adder.game = game;
            adder.map = *chosen;
            adder.model.clear();
        }
    }
    if (ImGui::Button(tr(lang, S::map_add_model_room)))
        room_adder_choose_model(app, adder.owner, adder.world);
    if (!adder.model.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(u8(adder.model.filename()).c_str());
    }
    ImGui::EndDisabled();
    ImGui::Separator();
}

}  // namespace

void room_adder_open_map(const std::string& owner, const CollectionGameInfo* game, const CollectionMap& map,
                         const std::string& world) {
    ask(owner, game, map, {}, world, false);
}

void room_adder_open(const std::string& owner, const std::string& world) {
    ask(owner, nullptr, {}, {}, world, true);
}

void room_adder_choose_model(App& app, const std::string& owner, const std::string& world) {
    static const SDL_DialogFileFilter models[] = {{"FBX, glTF, glb, OBJ", "fbx;gltf;glb;obj"}};
    {
        std::lock_guard lock(adder.mutex);
        adder.picked_owner = owner;
    }
    adder.world = world;
    SDL_ShowOpenFileDialog(
        [](void*, const char* const* files, int) {
            if (files == nullptr || files[0] == nullptr)
                return;
            std::lock_guard lock(adder.mutex);
            adder.picked = files[0];
        },
        nullptr, app.window, models, 1, nullptr, false);
}

bool room_adder_busy() {
    return adder.busy;
}

bool draw_room_adder(App& app, const std::string& owner, std::string* world, std::string* key) {
    const auto lang = app.language;
    const ImVec4 warn(0.95f, 0.8f, 0.35f, 1);
    bool done = false, ok = false, added = false;
    std::vector<std::string> lines;
    std::string why, picked;
    {
        std::lock_guard lock(adder.mutex);
        if (adder.picked_owner == owner)
            std::swap(picked, adder.picked);
        if (adder.owner == owner) {
            std::swap(done, adder.done);
            ok = adder.ok;
            lines.swap(adder.lines);
            why = adder.why;
        }
    }
    if (!picked.empty()) {
        if (adder.ask || (adder.owner == owner && adder.choosing && ImGui::IsPopupOpen("###addroom"))) {
            adder.model = path_u8(picked);  // the chooser's model
            adder.game = nullptr;
            adder.key = room_key_from(u8(adder.model.stem()));
        } else {
            ask(owner, nullptr, {}, path_u8(picked), adder.world, false);
        }
    }
    if (adder.owner != owner)
        return false;
    const std::string source =
        adder.game != nullptr ? std::string(adder.game->code) + ":" + adder.map.name : u8(adder.model.filename());
    if (done && adder.listing.empty()) {  // a room added (a listing's end says nothing)
        if (adder.worker.joinable())
            adder.worker.join();
        for (const auto& line : lines)
            app.tell(source + ": " + line, true);
        if (ok) {
            app.tell(fmt(lang, S::map_add_room_done, {source, adder.world, adder.key}), true);
            *world = adder.world;
            *key = adder.key;
            added = true;
            adder.close = true;
        } else {
            app.tell(fmt(lang, S::save_failed, {why}), false);
        }
    } else if (done && !ok && !why.empty()) {
        app.tell(why, false);  // the maps could not be listed
    }
    if (adder.ask) {
        ImGui::OpenPopup("###addroom");
        adder.ask = false;
    }
    const std::string title =
        adder.choosing ? std::string(tr(lang, S::adder_title_choose)) : fmt(lang, S::map_add_room_title, {source});
    if (!ImGui::BeginPopupModal((title + "###addroom").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return added;
    if (adder.close) {
        adder.close = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return added;
    }
    const float em = ImGui::GetFontSize();
    ImGui::PushTextWrapPos(34 * em);
    ImGui::TextUnformatted(tr(lang, S::map_add_room_hint));
    ImGui::PopTextWrapPos();
    if (adder.choosing)
        draw_choice(app);
    const Mod* mod = app.current_mod();
    const bool chosen = adder.game != nullptr || !adder.model.empty();
    if (mod == nullptr) {
        ImGui::TextColored(warn, "%s", tr(lang, S::choose_mod_first));
    } else {
        std::vector<std::string> notes;
        const auto worlds = read_added_worlds(mod->dir, &notes);
        const auto label = [&](const AddedWorld& w) {
            const auto named = w.names.find("");
            return (named != w.names.end() ? named->second : w.key) + "  (" + w.key + ", " + w.like + ")";
        };
        std::string shown = world_name(lang, adder.world) + "  (" + adder.world + ")";
        for (const auto& w : worlds)
            if (w.key == adder.world)
                shown = label(w);
        ImGui::BeginDisabled(adder.busy);
        ImGui::SetNextItemWidth(20 * em);
        if (ImGui::BeginCombo(tr(lang, S::mw_new_world), shown.c_str())) {
            for (const auto& w : world_codes())
                if (w != "hb" && w != "pi" &&
                    ImGui::Selectable((world_name(lang, w) + "  (" + w + ")").c_str(), adder.world == w))
                    adder.world = w;
            for (const auto& w : worlds)
                if (ImGui::Selectable(label(w).c_str(), adder.world == w.key))
                    adder.world = w.key;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s", tr(lang, S::mw_new_key));
        ImGui::SetNextItemWidth(20 * em);
        ImGui::InputText("##addroomkey", &adder.key);
        ImGui::EndDisabled();
        const bool valid = added_room_key(adder.key);
        if (!valid)
            ImGui::TextColored(warn, "%s", tr(lang, S::map_add_room_key_bad));
        bool exists = false;
        for (const auto& r : read_added_rooms(mod->dir, &notes))
            exists = exists || (r.key == adder.key && r.world == adder.world);
        if (exists)
            ImGui::TextColored(warn, "%s", fmt(lang, S::map_add_room_exists, {adder.key}).c_str());
        if (adder.busy && adder.listing.empty())
            ImGui::TextDisabled("%s", fmt(lang, S::map_add_room_busy, {source}).c_str());
        ImGui::BeginDisabled(adder.busy || !valid || !chosen);
        if (ImGui::Button(tr(lang, S::map_add_room_go)))
            add_room(app, *mod);
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(adder.busy && adder.listing.empty());
    if (ImGui::Button(tr(lang, S::cancel)))
        ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    ImGui::EndPopup();
    return added;
}

}  // namespace studio
