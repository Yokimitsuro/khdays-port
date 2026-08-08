#include "khdays/game/scenes/title_scene.h"

#include <algorithm>
#include <cmath>

#include "khdays/game/draw.h"
#include "khdays/game/settings.h"

namespace khdays::game::scenes {

namespace {
constexpr char kTitleTheme[] = "Title_BGM_PCM8";  // the title BGM (SDAT stream)

// Distance the option block travels when the page/level changes: one screen
// width (the DS lays the menu pages out one screen apart). Port rendition of the
// measured page-scroll ease; see the header.
constexpr float kPagePitch = 256.0F;

// The DS only offers CARGAR once a save file exists. The port has no save
// system yet, so no save is present.
bool has_save_data() {
    return false;
}
}  // namespace

void TitleScene::on_enter(SceneManager& manager) {
    // The title's two screens live in ttl.p2 sub-file 1 (a D2KP background pack),
    // paired by data_ov000_0205a9d4: screen 7 / tiles 3 / palette 3 = the top
    // screen (Disney + SQUARE ENIX + the KINGDOM HEARTS 358/2 Days logo), and
    // screen 3 / tiles 1 / palette 1 = the bottom character illustration. The
    // menu options are the real localized OBJ textures from ttl_<lang>.p2.
    //
    // (The port used to draw a 3D BMD0 logo on white here; that dropped the
    // Disney/SQUARE ENIX logos and did not match the DS's 2D screen.)
    top_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 7, 3, 3);
    illustration_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 3, 1, 1);
    // English is the odd one out: there is no ttl_en.p2 — the English option
    // textures are the base file's sub-file 2, while the other four ship as
    // ttl_<lang>.p2 sub-file 1.
    buttons_ = language() == Language::English
                   ? khdays::resource::load_sprite_set("ttl/ttl.p2", 2)
                   : khdays::resource::load_sprite_set(
                         localized_path("ttl/ttl_&.p2").c_str(), 1);
    if (auto* music = manager.music()) {
        music->play_music(kTitleTheme);
    }
    begin_page_slide(kPagePitch);  // the menu slides in as the title appears
}

void TitleScene::begin_page_slide(const float from) {
    page_x_ = from;
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
        begin_page_slide(kPagePitch);  // deeper: new page slides in from the right
        break;
    case Level::Story:
        if (selected_ == 0) {
            // NUEVA PARTIDA. The DS shows a difficulty selector before gameplay.
            // Partly measured (tools/savestate_obj): the option names are
            // UI/cm/cmo_&.p2 sub-file 3 cells 40 PRINCIPIANTE / 41 NORMAL /
            // 42 EXPERTO (43 CRITICAL), and the OBJ layer is three plates at
            // Y=56/72/88 with the selected one red plus a cursor. Not ported
            // yet: the plate art is a tile surface (not a clean cell) and the
            // name label positions live on the BG layer, which the savestate's
            // register mirror does not expose -- building it now would mean
            // inventing those positions, so it waits for a BG-layer reading.
            manager.change_scene(kSceneGameplay);
        }
        // CARGAR is unreachable while has_save_data() is false.
        break;
    case Level::Mission:
        if (selected_ == 0) {
            // UN JUGADOR. The DS shows the save-file screen ("Seleccionar
            // archivo.") before the character select, and so does this.
            manager.change_scene(kSceneSaveFile);
        }
        // MULTIJUGADOR is DS local wireless — not ported.
        break;
    }
}

void TitleScene::update(SceneManager& manager) {
    ++frame_;
    const auto& in = manager.input();

    // Page-scroll ease (func_ov000_02050ec4): close a quarter of the gap to the
    // resting position each frame, snapping once the step is under 1/8 px.
    page_x_ += (0.0F - page_x_) * 0.25F;
    if (std::abs(page_x_) < 0.125F) {
        page_x_ = 0.0F;
    }

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
        begin_page_slide(-kPagePitch);  // back: page slides in from the left
    }
}

void TitleScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);

    if (top_) {
        draw_screen(r, layout, *top_, /*bottom=*/false);  // Disney/SE + KH logo
    }
    if (illustration_) {
        draw_screen(r, layout, *illustration_, /*bottom=*/true);
    }

    // The option block eases horizontally into place (see update()).
    const int page_dx = static_cast<int>(std::lround(page_x_));

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
                // 28px row pitch. page_dx applies the page-scroll ease.
                //
                // The DS pulses the selection's alpha between blend 2/16 and
                // 8/16 over 500 ms (func_ov000_0205157c), but that pulse is on a
                // separate cursor object, NOT the option bar. This localized
                // cell bakes the bar and its "square" together, so pulsing it
                // washes the whole bar out. The bar is kept solid until the
                // cursor sprite is drawn as its own layer (then pulse only that,
                // via draw_overlay's alpha argument, which now exists).
                draw_overlay(r, layout, buttons_->cells[cell], page_dx,
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
