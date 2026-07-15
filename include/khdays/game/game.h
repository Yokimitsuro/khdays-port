#pragma once

#include <cstdint>

#include "khdays/game/object.h"
#include "khdays/game/scene.h"

// The game frame loop — the native shape of the DS `main` loop (@0x02000cac):
// each frame walks the object list (the func_02023adc state-machine update),
// then updates and renders the current scene, applying scene transitions. VBlank
// sync, rendering, and input come from the platform layer; this owns only the
// flow.
namespace khdays::game {

class Game final {
public:
    SceneManager& scenes() { return scenes_; }
    ObjectList& objects() { return objects_; }

    // Pick and enter the first scene from the persisted boot state, mirroring
    // BootTask_Construct: a fresh boot (0, or the "-2" continue-less path) goes
    // to the boot/logo scene; anything else goes to the continue scene.
    void boot(int boot_state = 0);

    // Advance one frame: objects first, then the scene (the DS frame order).
    void step();

    // Draw the current scene (after step()).
    void render(Renderer& renderer) { scenes_.render(renderer); }

    std::uint64_t frame() const { return scenes_.frame(); }

private:
    SceneManager scenes_;
    ObjectList objects_;
};

}  // namespace khdays::game
