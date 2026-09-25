#pragma once

#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/resource/ui_content.h"  // TitleLogoModel

namespace khdays::game::scenes {

// ov000's top screen from the title onwards (it stays up through every menu
// level and the save-file screen), composed as the DS layers it:
//
//   backdrop  white -- main BG palette colour 0 (0x7fff in ttl.p2's palette,
//             and every whitened level of it)
//   BG1       screen 7 / tiles 3 / palette 3 of ttl.p2 sub-file 1, shown once
//             DISPCNT enables it, with its palette pushed toward white by
//             `level` (func_ov000_0204e1dc: c + (level * (31 - c) >> 4) per
//             5-bit channel; 16 = white, 0 = the real colours)
//   BG0       the 3D logo (ttl.p2 sub-file 0), priority 0 so above BG1, posed
//             by its BCA0 and faded by its BMA0 at the same frame, through
//             the camera func_02023c60 sets up (eye (0,0,100) on the origin,
//             +Y up) and the orthographic volume func_02023d70 is given every
//             frame: x in [-0x4d9a, 0x4d9a], y in [-0x3b33, 0x3b33] (20.12).
//
// Verified against menuAnim.dst (inside func_ov000_0204e270, counter 9):
// DISPCNT A = 0x10108 (BG0 only), the main BG palette equals the source
// whitened at level 16.
class TitleTopScreen final {
public:
    // Loads the BG and the logo; false when the game data is missing.
    bool load();

    // The logo animation's length in frames (0 when absent).
    int logo_frames() const;

    // Compose one frame. `logo_frame` poses both logo animations;
    // `show_bg` is DISPCNT's BG1 bit; `level` whitens BG1's palette (0..16).
    const khdays::assets::DecodedTexture& compose(int logo_frame, bool show_bg,
                                                 int level);

private:
    std::optional<khdays::assets::DecodedTexture> background_;
    std::optional<khdays::resource::TitleLogoModel> logo_;
    khdays::assets::DecodedTexture frame_;
};

}  // namespace khdays::game::scenes
