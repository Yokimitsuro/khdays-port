#pragma once

#include <cstddef>
#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"  // SpriteSet

namespace khdays::game::scenes {

// Scene 1 (ov000): the title screen — and the whole front-end menu. Two DS
// screens from ttl.p2: the KINGDOM HEARTS 358/2 Days logo on top, the character
// illustration on the bottom. The title hosts EVERY menu level: the option pair
// swaps in place at the same two slots, (0,116) and (0,144) (positions read
// from the live ov000 sub-engine OAM, 28px row pitch):
//
//   MODO HISTORIA / MODO MISION
//     MODO HISTORIA -> NUEVA PARTIDA / CARGAR   (CARGAR only when a save exists)
//     MODO MISION   -> UN JUGADOR / MULTIJUGADOR
//
// Up/Down move the cursor, A/Start confirm, B goes back a level. The options are
// the real localized OBJ textures from ttl_<lang>.p2. Plays the title theme.
class TitleScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    // Which option pair the title is currently showing.
    enum class Level { Root, Story, Mission };

    // One option: its localized cell in ttl_<lang>.p2 when selected (red bar)
    // and when not (gray bar).
    struct Option {
        int selected;
        int normal;
    };

    // Fill `out` with the current level's options; returns how many there are.
    std::size_t options(Option* out) const;
    void confirm(SceneManager& manager);

    std::optional<khdays::assets::DecodedTexture> logo3d_;        // top: white+logo
    std::optional<khdays::assets::DecodedTexture> illustration_;  // bottom screen
    std::optional<khdays::resource::SpriteSet> buttons_;  // localized option textures
    Level level_ = Level::Root;
    int selected_ = 0;
    int frame_ = 0;  // for the fade-in
};

}  // namespace khdays::game::scenes
