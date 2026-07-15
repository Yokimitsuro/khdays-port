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
// playback before it requests scene 7.
class BootLogoScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;

private:
    static constexpr int kPairs = 3;
    static constexpr int kFadeFrames = 15;
    static constexpr int kHoldFrames = 90;
    static constexpr int kPairFrames = kFadeFrames * 2 + kHoldFrames;
    static constexpr int kTotalFrames = kPairFrames * kPairs;

    std::array<std::optional<khdays::assets::DecodedTexture>, kPairs> top_;
    std::array<std::optional<khdays::assets::DecodedTexture>, kPairs> bottom_;
    int frame_ = 0;
};

}  // namespace khdays::game::scenes
