// The music a mod gives a room (app.h draw_room_music): its field's and its battle's files, each chosen in the
// system's dialog (WAV, FLAC, Ogg) and copied into the mod, or removed; a notice for each.
#include "../app.h"
#include "core/files/paths.h"
#include "core/rooms/room_music.h"
#include "imgui.h"

#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace studio {

namespace {

// The room and the music (the field's or the battle's) the open dialog chooses for.
struct Asked {
    RoomMusicPlace place;
    bool battle = false;
    bool waiting = false;
};
Asked asked;

void ask(App& app, const RoomMusicPlace& place, bool battle) {
    static const SDL_DialogFileFilter sounds[] = {{"WAV, FLAC, Ogg Vorbis", "wav;flac;ogg"}};
    asked = {place, battle, true};
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            if (files == nullptr || files[0] == nullptr)
                return;
            App* a = static_cast<App*>(user);
            std::lock_guard lock(a->dialog_mutex);
            a->dialog_room_music.emplace_back(files[0]);
        },
        &app, app.window, sounds, 1, nullptr, false);
}

}  // namespace

void take_room_music(App& app) {
    std::vector<std::string> chosen;
    {
        std::lock_guard lock(app.dialog_mutex);
        chosen.swap(app.dialog_room_music);
    }
    if (chosen.empty() || !asked.waiting)
        return;
    asked.waiting = false;
    std::string why;
    if (room_music_set(asked.place, asked.battle, path_u8(chosen.back()), &why))
        app.tell(fmt(app.language, S::room_music_saved, {u8(room_music_file(asked.place, asked.battle))}), true);
    else
        app.tell(fmt(app.language, S::room_music_failed, {why}), false);
}

void draw_room_music(App& app, const RoomMusicPlace& place, const char* id) {
    const auto lang = app.language;
    ImGui::PushID(id);
    ImGui::TextWrapped("%s", tr(lang, S::room_music_hint));
    const fs::path field = room_music_file(place, false);
    for (const bool battle : {false, true}) {
        ImGui::PushID(battle ? 1 : 0);
        const fs::path file = battle ? room_music_file(place, true) : field;
        ImGui::TextUnformatted(tr(lang, battle ? S::room_music_battle : S::room_music_field));
        ImGui::SameLine();
        if (!file.empty())
            ImGui::TextUnformatted(u8(file.filename()).c_str());
        else
            ImGui::TextDisabled("%s", tr(lang, battle && !field.empty() ? S::room_music_field_goes_on
                                                                         : S::room_music_game));
        same_line_if_room(button_width(tr(lang, S::choose_file)));
        if (ImGui::Button(tr(lang, S::choose_file)))
            ask(app, place, battle);
        if (!file.empty()) {
            same_line_if_room(button_width(tr(lang, S::remove)));
            if (ImGui::Button(tr(lang, S::remove))) {
                std::string why;
                if (room_music_remove(place, battle, &why))
                    app.tell(fmt(lang, S::room_music_removed, {u8(file)}), true);
                else
                    app.tell(fmt(lang, S::room_music_failed, {why}), false);
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

}  // namespace studio
