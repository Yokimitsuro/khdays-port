#include "khdays/game/scenes/boot_logo_scene.h"

#include <algorithm>

#include "khdays/game/draw.h"
#include "khdays/game/settings.h"
#include "khdays/resource/ui_content.h"

namespace khdays::game::scenes {

void BootLogoScene::on_enter(SceneManager&) {
    // The boot screens live in ttl.p2 sub-file 1 (a D2KP background pack),
    // paired by data_ov000_0205a9d4: the main engine shows screens 4/5/6 with
    // tiles 2 / palette 2, the sub engine screens 0/1/2 with tiles 0 / palette 0.
    top_[0] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 4, 2, 2);  // Disney
    top_[1] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 5, 2, 2);  // h.a.n.d.
    // func_ov000_0204d72c loads ttl/ttl_&.p2 for every language variant but
    // English (which has no ttl_en.p2), and func_ov000_0204de30 copies its
    // sub-file 0 over the legal screen's tiles from char offset 0x7000.
    top_[2] = language() == Language::English
        ? khdays::resource::load_ui_background("ttl/ttl.p2", 1, 6, 2, 2)
        : khdays::resource::load_ui_background(
              "ttl/ttl.p2", 1, 6, 2, 2,
              khdays::resource::CharacterPatch{
                  localized_path("ttl/ttl_&.p2"), 0U, 0x7000U});  // legal
    bottom_[0] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 0, 0, 0);  // Square Enix
    bottom_[1] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 1, 0, 0);  // MobiClip
    bottom_[2] = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 2, 0, 0);  // Licensed by Nintendo
}

void BootLogoScene::update(SceneManager& manager) {
    tick_ = next_tick_++;
    // The last save-check frame is the one on which func_ov000_0204df98
    // returns the title's state; the title runs from the next frame.
    if (tick_ == kTotalFrames - 1) {
        manager.change_scene(kSceneTitle);
    }
}

void BootLogoScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);
    // The logo screens' own backdrop is white.
    draw_screen_fill(r, layout, /*bottom=*/false, /*white=*/true, 255);
    draw_screen_fill(r, layout, /*bottom=*/true, /*white=*/true, 255);

    // Master brightness for this frame, as the pair states set it; the save
    // check after the last pair holds +0x10.
    int bright = kBrightMax;
    const int pair = tick_ / kPairFrames;
    if (pair < kPairs) {
        const int n = tick_ - pair * kPairFrames;
        if (n < kFadeInEnd) {
            bright = kBrightMax - n / 2;
        } else if (n <= kHoldEnd) {
            bright = 0;
        } else if (n <= kFadeOutEnd) {
            bright = (n - kHoldEnd) / 2;
        }
        if (top_[pair]) {
            draw_screen(r, layout, *top_[pair], /*bottom=*/false);
        }
        if (bottom_[pair]) {
            draw_screen(r, layout, *bottom_[pair], /*bottom=*/true);
        }
    }
    if (bright > 0) {
        // Mode Up: each channel moves toward white by bright/16.
        const int a = std::min(255, bright * 255 / kBrightMax);
        draw_screen_fill(r, layout, /*bottom=*/false, /*white=*/true, a);
        draw_screen_fill(r, layout, /*bottom=*/true, /*white=*/true, a);
    }
}

}  // namespace khdays::game::scenes
