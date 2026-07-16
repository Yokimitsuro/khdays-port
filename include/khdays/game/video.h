#pragma once

#include <cstdint>
#include <string_view>

// The abstract video service scenes drive, mirroring MusicPlayer. The platform
// backend implements it; neutral scene code only names a clip and blits the
// frames it gets back.
//
// Nothing here is allowed to encode a Nintendo DS assumption. The game's own
// clips happen to be 256x160 at 14.99 fps in a MobiClip container, but the
// interface deliberately carries a size and a plain RGBA buffer instead, so a
// mod can supply a clip of any resolution, aspect or frame rate through the same
// path. That is the whole reason the DS's ceiling does not become the port's.
namespace khdays::game {

// One decoded frame: RGBA8888, `width` x `height`, row-major, no padding.
struct VideoFrame final {
    int width = 0;
    int height = 0;
    const std::uint8_t* rgba = nullptr;  // width * height * 4 bytes; not owned
};

class VideoPlayer {
public:
    virtual ~VideoPlayer() = default;

    // Start playing the clip at `game_path` (a VFS path, e.g. "mv/802.mods").
    // The VFS resolves mod overrides first, so a mod may replace the clip — with
    // another container or a modern one — without scenes knowing.
    virtual void play_video(std::string_view game_path) = 0;

    // Stop playback and release the clip.
    virtual void stop_video() = 0;

    // True while a clip is decoding; false once it ends (or was stopped), which
    // is how a scene knows the cutscene is over.
    virtual bool video_playing() const = 0;

    // Advance to the frame due at the current wall-clock time and return it, or
    // a frame with `rgba == nullptr` if none is ready. Timing follows the clip's
    // own rate, not the game's frame rate.
    virtual VideoFrame video_frame() = 0;
};

}  // namespace khdays::game
