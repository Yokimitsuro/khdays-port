#pragma once

#include <array>
#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"

namespace khdays::game::scenes {

// Scene 1 (ov000): the boot logo sequence. Three publisher/legal screen pairs
// from ttl.p2 (Disney Interactive + Square Enix, h.a.n.d. + MobiClip, the legal
// notice + "Licensed by Nintendo"), each fading in and out on both DS screens,
// then it advances to the title -- the native form of ov000's fresh-boot logo
// playback.
//
// Everything below is ov000's own code. The fresh-boot setup
// (func_ov000_0204d7c8) sets both master brightnesses to +0x10 and returns a
// chain of identical per-pair states -- func_ov000_0204dc38 -> 0204dd34 ->
// 0204de30 -- each counting heap[0] and driving the master brightness of both
// screens (func_0201e374 main / func_0201e3cc sub):
//
//   counter 0x00..0x1f : 0x10 - n/2       (fade in)
//   counter 0x20..0x3a : 0                (hold)
//   counter 0x3b..0x5a : (n - 0x3a)/2     (fade out)
//   counter 0x5b       : 0x10, next state (advance)
//
// so a pair lasts 0x5c = 92 frames: Obj_UpdateAll (func_02023adc) runs a
// returned state from the NEXT frame. A POSITIVE master brightness is mode Up
// (GXx_SetMasterBrightness_: v | 0x4000), i.e. toward WHITE, so the logos fade
// from and to white. None of the states reads the pad, so the sequence cannot
// be skipped.
//
// On the third pair's first frame, ov000 streams ttl_&.p2 sub-file 0 into the
// main BG1 character VRAM at 0x7000 (tile 448 of the 8bpp sheet) when the game
// runs in a language other than English -- the localized legal screen.
//
// After the pairs, func_ov000_0204df98 keeps both screens at +0x10 while it
// runs the save check: one frame to start it, one poll per attempt until three
// have completed, and one frame to hand over to the title. With the port's
// immediate I/O that is its minimum of five frames; on the DS a slower card
// only lengthens it.
class BootLogoScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    static constexpr int kPairs = 3;
    static constexpr int kFadeInEnd = 0x20;
    static constexpr int kHoldEnd = 0x3a;
    static constexpr int kFadeOutEnd = 0x5a;
    static constexpr int kBrightMax = 0x10;  // master brightness +0x10 = white
    static constexpr int kPairFrames = kFadeOutEnd + 2;  // counters 0..0x5b
    static constexpr int kSaveCheckFrames = 5;  // func_ov000_0204df98, see above
    static constexpr int kTotalFrames = kPairFrames * kPairs + kSaveCheckFrames;

    std::array<std::optional<khdays::assets::DecodedTexture>, kPairs> top_;
    std::array<std::optional<khdays::assets::DecodedTexture>, kPairs> bottom_;
    int tick_ = 0;       // the DS state counter of the frame being shown
    int next_tick_ = 0;  // advanced once per update
};

}  // namespace khdays::game::scenes
