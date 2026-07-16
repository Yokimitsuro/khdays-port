#include "khdays/game/scenes/title_scene.h"

#include <algorithm>

#include "khdays/game/draw.h"

namespace khdays::game::scenes {

namespace {
constexpr char kTitleTheme[] = "Title_BGM_PCM8";  // the title BGM (SDAT stream)

// The DS only offers CARGAR once a save file exists. The port has no save
// system yet, so no save is present.
bool has_save_data() {
    return false;
}
}  // namespace

void TitleScene::on_enter(SceneManager& manager) {
    // The title's two screens live in ttl.p2 sub-file 1 (a D2KP background pack):
    // screen 7 / tiles 3 / palette 0 = the KINGDOM HEARTS logo, and screen 3 /
    // tiles 1 / palette 1 = the character illustration. The menu options are the
    // real localized OBJ textures from ttl_<lang>.p2.
    logo3d_ = khdays::resource::load_title_logo();  // real KH logo on white
    illustration_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 3, 1, 1);
    buttons_ = khdays::resource::load_sprite_set("ttl/ttl_es.p2", 1);
    if (auto* music = manager.music()) {
        music->play_music(kTitleTheme);
    }
}

std::size_t TitleScene::options(Option* out) const {
    // ttl_<lang>.p2 sub-file 1 cells, as {selected (red bar), normal (gray bar)}.
    constexpr Option kStoryMode{4, 5};       // MODO HISTORIA
    constexpr Option kMissionMode{6, 7};     // MODO MISION
    constexpr Option kNewGame{8, 9};         // NUEVA PARTIDA
    constexpr Option kLoad{10, 11};          // CARGAR
    constexpr Option kSinglePlayer{18, 19};  // UN JUGADOR
    constexpr Option kMultiPlayer{20, 21};   // MULTIJUGADOR

    switch (level_) {
    case Level::Root:
        out[0] = kStoryMode;
        out[1] = kMissionMode;
        return 2;
    case Level::Story:
        out[0] = kNewGame;
        // With no save file the DS shows NUEVA PARTIDA alone, in the first slot.
        if (has_save_data()) {
            out[1] = kLoad;
            return 2;
        }
        return 1;
    case Level::Mission:
        out[0] = kSinglePlayer;
        out[1] = kMultiPlayer;
        return 2;
    }
    return 0;
}

void TitleScene::confirm(SceneManager& manager) {
    switch (level_) {
    case Level::Root:
        level_ = selected_ == 0 ? Level::Story : Level::Mission;
        selected_ = 0;
        break;
    case Level::Story:
        if (selected_ == 0) {
            // NUEVA PARTIDA. The DS picks a difficulty (Principiante / Normal /
            // Experto) before gameplay; that screen is not ported yet.
            manager.change_scene(kSceneGameplay);
        }
        // CARGAR is unreachable while has_save_data() is false.
        break;
    case Level::Mission:
        if (selected_ == 0) {
            // UN JUGADOR. The DS shows the save-file screen ("Cargar" /
            // "Seleccionar archivo.") before the character select; that screen
            // is not ported yet, so this goes straight to the roster.
            manager.change_scene(kSceneMainMenu);
        }
        // MULTIJUGADOR is DS local wireless — not ported.
        break;
    }
}

void TitleScene::update(SceneManager& manager) {
    ++frame_;
    const auto& in = manager.input();

    Option opts[2];
    const int count = static_cast<int>(options(opts));
    if (in.just_pressed(Button::Down)) {
        selected_ = std::min(count - 1, selected_ + 1);
    }
    if (in.just_pressed(Button::Up)) {
        selected_ = std::max(0, selected_ - 1);
    }
    if (in.just_pressed(Button::A) || in.just_pressed(Button::Start)) {
        confirm(manager);
    }
    if (in.just_pressed(Button::B) && level_ != Level::Root) {
        level_ = Level::Root;
        selected_ = 0;
    }
}

void TitleScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);

    if (logo3d_) {
        draw_screen(r, layout, *logo3d_, /*bottom=*/false);  // top: white + logo
    }
    if (illustration_) {
        draw_screen(r, layout, *illustration_, /*bottom=*/true);
    }

    // The current level's options on the bottom screen, red for the selected one
    // and gray for the rest.
    if (buttons_) {
        Option opts[2];
        const std::size_t count = options(opts);
        for (std::size_t i = 0; i < count; ++i) {
            const int cell = static_cast<int>(i) == selected_ ? opts[i].selected
                                                              : opts[i].normal;
            if (cell >= 0
                && static_cast<std::size_t>(cell) < buttons_->cells.size()) {
                // Real positions from the ov000 sub-engine OAM: the option slots
                // are at (0, 116) and (0, 144) — left-aligned, 24px tall, with a
                // 28px row pitch.
                draw_overlay(r, layout, buttons_->cells[cell], 0,
                             116 + static_cast<int>(i) * 28, /*bottom=*/true);
            }
        }
    }

    // Fade in from white on entry (the title enters by a fade in the game).
    constexpr int kFadeIn = 24;
    if (frame_ < kFadeIn) {
        const int a = 255 * (kFadeIn - frame_) / kFadeIn;
        r.fill_overlay(Color{255, 255, 255, static_cast<std::uint8_t>(a)});
    }
}

}  // namespace khdays::game::scenes
