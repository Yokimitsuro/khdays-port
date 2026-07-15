#pragma once

#include <string_view>

// The abstract music service scenes drive. The platform backend implements it
// (rendering the game's SSEQ sequences to PCM and streaming them); neutral scene
// code only names a track. Keeping it here means the game layer never sees the
// SDAT or the audio API — the same separation as Renderer for graphics.
namespace khdays::game {

class MusicPlayer {
public:
    virtual ~MusicPlayer() = default;

    // Start (and loop) the background track named `track`. Names are the game's
    // own SDAT sequence symbols (e.g. "ThemeXIII", "Entrymulti"). Requesting the
    // track that is already playing is a no-op, so scenes may call it every
    // frame or once on entry.
    virtual void play_music(std::string_view track) = 0;

    // Stop any current track.
    virtual void stop_music() = 0;
};

}  // namespace khdays::game
