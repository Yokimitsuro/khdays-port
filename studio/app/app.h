// The tool's state, shared by its views.
#pragma once

#include "core/files/catalog.h"
#include "core/mods/check.h"
#include "core/mods/mods.h"
#include "core/mods/process.h"
#include "core/mods/settings.h"

#include "gpu_images.h"
#include "strings.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace studio {

// A file the modder has offered for the shown item, copied into the mod's
// staging folder (mods/<Mod>/studio/staging, the tool's own) so that the
// port's readers can check it whatever its original path.
struct Candidate {
    int item = -1;
    std::filesystem::path source;  // as the modder gave it
    std::filesystem::path staged;  // the copy the checks read (a PNG for pictures)
    CheckResult check;
    bool any_palette = false;  // a texture: for all its palettes
};

struct App {
    Settings settings;
    std::filesystem::path mods_root;
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    std::unique_ptr<GpuImages> images;
    Language language = Language::en;

    // the catalog, walked on a thread
    Catalog catalog;
    std::unordered_map<std::string, int> by_path;  // a lookup path without extension -> item
    std::thread catalog_thread;
    std::atomic<bool> catalog_ready{false};
    std::atomic<bool> catalog_cancel{false};
    std::atomic<int> catalog_progress{0};
    Catalog catalog_next;
    void start_catalog();
    void take_catalog();  // on the interface's thread, once ready

    // the mods
    std::vector<Mod> mods;
    int mod = -1;  // index into mods, -1: none chosen
    void refresh_mods();
    const Mod* current_mod() const {
        return mod >= 0 && mod < static_cast<int>(mods.size()) ? &mods[static_cast<std::size_t>(mod)] : nullptr;
    }

    // the browser
    std::string search;
    unsigned kinds = (1u << kKinds) - 1u;
    std::vector<int> results;
    bool results_stale = true;
    int selected = -1;

    // the replacement being prepared
    Candidate candidate;
    std::string candidate_error;
    void offer(const std::filesystem::path& file);  // a dropped or chosen file for the shown item
    void clear_candidate();

    // the port's dumps, run from the tool
    std::unique_ptr<Job> job;
    std::string job_kind;

    // every 2D sheet written coloured into a folder (sheet_view.cpp), or every
    // 3D texture (image_view.cpp), on a thread of its own
    struct BulkExport {
        bool textures = false;  // the 3D textures, not the 2D sheets
        std::thread thread;
        std::filesystem::path folder;
        std::atomic<bool> stop{false}, done{false};
        std::atomic<std::size_t> files_done{0}, files{0};
        std::atomic<int> written{0};
        std::mutex mutex;
        std::string current;              // the sheet or texture last written
        std::vector<std::string> failed;  // once done: those not written, with why
        bool told = false;
        ~BulkExport() {
            stop = true;
            if (thread.joinable())
                thread.join();
        }
    };
    std::unique_ptr<BulkExport> bulk_export;
    int texture_scale = 1;  // the size a 3D texture's export writes (texture_size_combo)

    // files chosen in a dialog arrive on another thread
    std::mutex dialog_mutex;
    std::vector<std::string> dialog_files;
    std::vector<std::string> dialog_clips;            // animation clips
    std::vector<std::string> dialog_exports;          // where an assembled sheet is exported
    std::vector<std::string> dialog_grids;            // where its tile grid is exported
    std::vector<std::string> dialog_room_exports;     // where a room is exported
    std::vector<std::string> dialog_model_exports;    // where a game model is exported
    std::vector<std::string> dialog_sheet_folders;    // where every 2D sheet is exported
    std::vector<std::string> dialog_texture_exports;  // where a 3D texture is exported
    std::vector<std::string> dialog_texture_folders;  // where every 3D texture is exported
    std::vector<std::string> dialog_collections;      // the 1.5+2.5 collection's folder (KH2's rooms)
    std::vector<std::string> dialog_ports;            // the port's executable (the settings)
    std::vector<std::string> dialog_characters;       // a character's model, clips, picture or sound

    std::string status;
    bool missions_asked = false;  // the Missions window to open (main.cpp), asked by a game mission's view

    // What a person must see happened -- a save done or failed, work kept or
    // lost: shown over the window (main.cpp draw_notices) in green or red, a
    // success for a few seconds, a failure until closed; the last one stays in
    // the status line too.
    struct Notice {
        std::string text;
        bool ok = true;
        std::uint64_t since = 0;  // SDL_GetTicks
    };
    std::vector<Notice> notices;
    void tell(const std::string& text, bool ok);
    bool quit_asked = false;  // the window's close waits for an answer about unsaved work
};

// The views (each draws into the current ImGui window).
void draw_mod_bar(App& app);
void draw_browser(App& app);
void draw_item(App& app);
void draw_image_view(App& app, const Item& item);
void draw_sound_view(App& app, const Item& item);
void draw_model_view(App& app, const Item& item);
// A model file offered for a room of a mission world: it
// becomes the room's replacement being made, or its collision model.
void room_offer(App& app, const Item& item, const std::filesystem::path& file);
// A text file of the game, its strings edited beside the game's.
void draw_text_view(App& app, const Item& item);
// A movie: what plays now, and a video (MPEG-1 or MP4) saved into the mod in its place.
void draw_movie_view(App& app, const Item& item);
void movie_offer(App& app, const Item& item, const std::filesystem::path& file);
// The video playing in a movie's view stopped (another item chosen, the end).
void movie_view_leave();
// The room shown written as a glTF where the person chose (its models, its
// floor and walls, its textures), and told how to go on.
void room_export_to(App& app, const std::filesystem::path& file);
// A mission: its scripts, step by step.
void draw_mission_view(App& app, const Item& item);
// Whether the mod's new missions have changes not saved; asked when the window closes (true: quit anyway).
bool new_missions_unsaved();
// The mod's new missions in a window of their own (views/mission_view.cpp), and a mission's places on its room's map
// (views/mission_map.cpp): the mod's mission `key` played in `world`'s room `room`; its places changed, not saved.
void draw_mod_missions(App& app, bool* open);
void draw_mission_map(App& app, const Mod& m, const std::string& key, const std::string& world, int room);
bool mission_map_unsaved();
// The mod's characters (views/character_view.cpp): the window, a file dropped while the pointer is over it (true
// when it took it), and its changes not saved.
void draw_characters(App& app, bool* open);
bool characters_take_drop(App& app, const std::filesystem::path& file);
bool characters_unsaved();
std::string characters_unsaved_key();
bool draw_unsaved_quit(App& app);
// The mod's prizes and chests (views/prizes_view.cpp): each beside Days's, their size and turn, and the KH2 prizes
// mod made from the player's 1.5+2.5 copy.
void draw_prizes(App& app, bool* open);
// The mods' console: the running game's mods' log as it grows, and their reloads.
void draw_mods_console(App& app, bool* open);
// The chosen mod's native plugin's parameters, as its mod describes them, and the values saved for the game.
void draw_plugin_parameters(App& app, bool* open);

// Points a mission puts in a room (its spawners), drawn by the map view
// over that room when it is shown.
struct MissionPoint {
    enum Kind { spawner, start, chest, emblem };  // green, blue, gold, pink
    std::array<std::int32_t, 3> at{};             // 20.12
    std::string label;
    Kind kind = spawner;
};
struct MissionOverlay {
    std::string world;  // tt, aw ...
    int room = -1;
    std::vector<MissionPoint> points;
};
MissionOverlay& mission_overlay();
void draw_map_view(App& app, const Item& item);
// A model file the modder offers for a game model (dropped, chosen, or the
// one the mod holds), read as the port reads it.
void model_offer(App& app, const Item& item, const std::filesystem::path& file);
// The game model shown copied where the person chose, as the port's dump has
// it (a glTF with its skeleton, its buffer and its textures).
void model_export_to(App& app, const std::filesystem::path& file);
// A glTF clip chosen to replace the model view's shown animation, and
// the dialog that chooses one.
void clip_offer(App& app, const std::filesystem::path& file);
void choose_clip(App& app);
// A 2D sheet assembled: the mode switch (true: assembled), the view,
// an edited picture offered while it is shown (false: not taken -- the
// sheet is replaced as tiles), its export, its tile grid's and the dialog
// choosing where.
bool draw_sheet_mode(App& app);
void draw_sheet_assembled(App& app, const Item& item);
bool sheet_offer(App& app, const std::filesystem::path& file);
void sheet_export_to(App& app, const std::filesystem::path& file);
void sheet_grid_export_to(App& app, const std::filesystem::path& file);
void choose_export(App& app);
// Lets go of the model view's pictures before the graphics device goes.
void model_view_shutdown();
void draw_dump_panel(App& app);
void draw_bulk_export(App& app);  // its progress, in the browser (sheet_view.cpp)
// The progress a bulk export's thread reports, and its end (sheet_view.cpp).
std::function<bool(std::size_t, std::size_t, const std::string&)> bulk_progress(App::BulkExport* job);
void bulk_finish(App::BulkExport* job, int written, std::vector<std::string> failed);
void sheets_export_to(App& app, const std::filesystem::path& folder);
void textures_export_to(App& app, const std::filesystem::path& folder);  // image_view.cpp
void texture_export_to(App& app, const std::filesystem::path& file);
void draw_candidate_controls(App& app, const Item& item);
void draw_replacements(App& app, bool* open);
void draw_settings(App& app, bool* open);
void draw_themes(App& app, bool* open);  // the colour palette (theme.h)
void stop_sounds();

// The words of a check's finding in the interface's language.
std::string finding_text(const App& app, const Finding& finding);

// Opens the system's file dialog for the shown item.
void choose_file(App& app, const Item& item);

// Rows that wrap: the next item on the last one's line when its `width` fits
// in what the window shows, else on the next line, so nothing is hidden past
// the window's right edge.
void same_line_if_room(float width);
// The width of a button, and of a checkbox or radio button, with `label`.
float button_width(const char* label);
float check_width(const char* label);

// Texts from here to the end of the scope wrap at the window's right edge, never cut there (a child window keeps
// its own: each takes one of these).
class WrappedTexts {
public:
    WrappedTexts();
    ~WrappedTexts();
    WrappedTexts(const WrappedTexts&) = delete;
    WrappedTexts& operator=(const WrappedTexts&) = delete;
};

}  // namespace studio
