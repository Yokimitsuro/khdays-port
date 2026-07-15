#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "khdays/assets/audio.h"
#include "khdays/game/audio.h"

namespace khdays::platform {

// Plays the game's SSEQ music through SDL. It opens the SDAT once, maps track
// names (SDAT sequence symbols) to indices, renders a requested track to PCM on
// a worker thread (so the frame loop never blocks on the software synth), then
// streams it looped through an SDL audio device. Implements the neutral
// game::MusicPlayer, so scenes only ever name a track.
class SdlMusicPlayer final : public khdays::game::MusicPlayer {
public:
    explicit SdlMusicPlayer(const std::filesystem::path& sdat_path);
    ~SdlMusicPlayer() override;

    SdlMusicPlayer(const SdlMusicPlayer&) = delete;
    SdlMusicPlayer& operator=(const SdlMusicPlayer&) = delete;

    void play_music(std::string_view track) override;
    void stop_music() override;

    // True if the SDAT opened and the audio device is streaming.
    bool ok() const { return stream_ != nullptr && sdat_ != nullptr; }

private:
    static void SDLCALL feed(void* userdata, SDL_AudioStream* stream,
                             int additional_amount, int total_amount);
    void render_track(std::string track, std::uint64_t generation);

    std::shared_ptr<khdays::assets::Sdat> sdat_;
    std::unordered_map<std::string, std::size_t> track_index_;   // SSEQ
    std::unordered_map<std::string, std::size_t> stream_index_;  // STRM
    SDL_AudioStream* stream_ = nullptr;
    bool audio_inited_ = false;

    std::mutex mutex_;               // guards pcm_/pos_/ready_/loop fields
    std::vector<std::int16_t> pcm_;  // interleaved stereo
    std::size_t pos_ = 0;
    std::size_t loop_start_ = 0;     // sample index to loop back to
    bool loops_ = true;
    bool ready_ = false;

    std::string current_;  // track currently requested (main thread only)
    std::atomic<std::uint64_t> generation_{0};
    std::thread worker_;
};

}  // namespace khdays::platform
