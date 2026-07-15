#include "khdays/game/game.h"

namespace khdays::game {

void Game::boot(int boot_state) {
    // BootTask_Construct: state 0 or -2 → the boot/logo scene; else → continue.
    const SceneId first =
        (boot_state == 0 || boot_state == -2) ? kSceneBootLogo : kSceneContinue;
    scenes_.start(first, boot_state);
}

void Game::step() {
    objects_.update();
    scenes_.step();
}

}  // namespace khdays::game
