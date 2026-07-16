#pragma once

#include <array>
#include <optional>

#include "khdays/assets/tex0.h"
#include "khdays/game/scene.h"

namespace khdays::game::scenes {

// Scene 1 (ov000): the boot logo sequence. Three publisher/legal screen pairs
// from ttl.p2 (Disney Interactive + Square Enix, h.a.n.d. + MobiClip, the legal
// notice + "Licensed by Nintendo"), each fading in and out on both DS screens,
// then it advances to the title — the native form of ov000's fresh-boot logo
// playback.
//
// The timings are the DS's own: ov000's fresh-boot setup (func_ov000_0204d7c8)
// returns a chain of identical per-pair fade states — func_ov000_0204dc38 ->
// 0204dd34 -> 0204de30 — each running 91 frames off heap[0] and driving the
// **master brightness** of both screens (func_0201e374 main / func_0201e3cc
// sub), where a POSITIVE value darkens (0x10 = black, 0 = normal):
//
//   frames 0x00..0x1f : fade in   -> 0x10 - f/2   (black -> normal)
//   frames 0x20..0x3a : hold      -> 0            (normal)
//   frames 0x3b..0x5a : fade out  -> (f-0x3a)/2   (normal -> black)
//   frame  > 0x5a     : advance to the next pair's state
//
// So the boot logos fade through BLACK (the title, entered with -0x10, is the
// one that fades from white).
class BootLogoScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    static constexpr int kPairs = 3;
    static constexpr int kFadeInEnd = 0x20;   // 32
    static constexpr int kHoldEnd = 0x3a;     // 58
    static constexpr int kFadeOutEnd = 0x5a;  // 90
    static constexpr int kBrightMax = 0x10;   // DS master brightness: 0x10 = black
    static constexpr int kPairFrames = kFadeOutEnd + 1;  // 91 frames per pair
    static constexpr int kTotalFrames = kPairFrames * kPairs;

    std::array<std::optional<khdays::assets::DecodedTexture>, kPairs> top_;
    std::array<std::optional<khdays::assets::DecodedTexture>, kPairs> bottom_;
    int frame_ = 0;
};

}  // namespace khdays::game::scenes
