#pragma once

#include <optional>
#include <vector>

#include "khdays/assets/cakp.h"
#include "khdays/game/scene.h"
#include "khdays/resource/ui_content.h"

namespace khdays::game::scenes {

// Scene 11 (ov012): the five-minute opening. The MobiClip stream is shown on
// the sub screen while the main screen runs the original op.p2 illustration
// timeline. START performs the native sixteen-step early-out fade.
class OpeningScene final : public Scene {
public:
    void on_enter(SceneManager& manager) override;
    void update(SceneManager& manager) override;
    void render(SceneManager& manager, Renderer& renderer) override;
    void on_exit(SceneManager& manager) override;

private:
    std::optional<khdays::resource::OpeningArtwork> artwork_;
    std::vector<khdays::assets::MovieSubtitleCue> subtitle_cues_;
    std::vector<std::optional<khdays::assets::DecodedTexture>> subtitles_;
    VideoPlayer* player_ = nullptr;
    VideoFrame video_frame_{};
    std::size_t video_frame_index_ = 0U;
    int timeline_frame_ = 0;
    int exit_fade_ = 0;
    bool playback_started_ = false;
    bool exiting_ = false;
};

}  // namespace khdays::game::scenes
