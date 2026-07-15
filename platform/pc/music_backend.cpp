#include "music_backend.h"

#include <exception>
#include <iostream>

#include "khdays/assets/sdat.h"
#include "khdays/resource/loader.h"

namespace khdays::platform {

namespace {
constexpr int kSampleRate = 32768;
constexpr int kChannels = 2;
constexpr double kLoopSeconds = 30.0;  // rendered length; looped as a whole
}  // namespace

SdlMusicPlayer::SdlMusicPlayer(const std::filesystem::path& sdat_path) {
    try {
        sdat_ = khdays::assets::open_sdat(sdat_path);
        const auto inventory = khdays::assets::read_sdat_inventory(sdat_path);
        for (std::size_t i = 0; i < inventory.sequences.size(); ++i) {
            if (!inventory.sequences[i].empty()) {
                track_index_.emplace(inventory.sequences[i], i);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "music: SDAT unavailable (" << error.what()
                  << "); running silent\n";
        sdat_.reset();
        return;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::cerr << "music: SDL audio init failed: " << SDL_GetError() << '\n';
        return;
    }
    audio_inited_ = true;

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = kChannels;
    spec.freq = kSampleRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        &SdlMusicPlayer::feed, this);
    if (stream_ == nullptr) {
        std::cerr << "music: SDL_OpenAudioDeviceStream failed: " << SDL_GetError()
                  << '\n';
        return;
    }
    SDL_ResumeAudioStreamDevice(stream_);
}

SdlMusicPlayer::~SdlMusicPlayer() {
    generation_.fetch_add(1);  // make any in-flight render commit nothing
    if (worker_.joinable()) {
        worker_.join();
    }
    if (stream_ != nullptr) {
        SDL_DestroyAudioStream(stream_);
    }
    if (audio_inited_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void SdlMusicPlayer::play_music(const std::string_view track) {
    if (!ok()) {
        return;
    }
    if (track == current_) {
        return;  // already playing/rendering this track
    }
    current_.assign(track);

    // Supersede any in-flight render and silence the stream until the new track
    // is ready.
    const std::uint64_t generation = generation_.fetch_add(1) + 1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
        pcm_.clear();
        pos_ = 0;
    }
    if (track_index_.find(current_) == track_index_.end()) {
        std::cerr << "music: no track named '" << current_ << "'\n";
        return;  // unknown track: stay silent
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&SdlMusicPlayer::render_track, this, current_,
                          generation);
}

void SdlMusicPlayer::stop_music() {
    generation_.fetch_add(1);
    current_.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = false;
    pcm_.clear();
    pos_ = 0;
}

void SdlMusicPlayer::render_track(std::string track,
                                  const std::uint64_t generation) {
    khdays::assets::DecodedAudio audio;
    try {
        const auto it = track_index_.find(track);
        if (it == track_index_.end()) {
            return;
        }
        audio = khdays::resource::render_music(*sdat_, it->second,
                                               static_cast<std::uint32_t>(kSampleRate),
                                               kLoopSeconds);
    } catch (const std::exception& error) {
        std::cerr << "music: render failed for '" << track << "': "
                  << error.what() << '\n';
        return;
    }
    // Commit only if no newer request arrived while we were rendering.
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_.load()) {
        return;
    }
    pcm_ = std::move(audio.samples);
    pos_ = 0;
    ready_ = !pcm_.empty();
}

void SDLCALL SdlMusicPlayer::feed(void* userdata, SDL_AudioStream* stream,
                                  const int additional_amount, int) {
    auto* self = static_cast<SdlMusicPlayer*>(userdata);
    if (additional_amount <= 0) {
        return;
    }
    const std::size_t want_samples =
        static_cast<std::size_t>(additional_amount) / sizeof(std::int16_t);
    std::vector<std::int16_t> chunk(want_samples, 0);

    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (self->ready_ && !self->pcm_.empty()) {
            for (std::size_t i = 0; i < want_samples; ++i) {
                chunk[i] = self->pcm_[self->pos_];
                if (++self->pos_ >= self->pcm_.size()) {
                    self->pos_ = 0;  // loop the whole track
                }
            }
        }
    }
    SDL_PutAudioStreamData(stream, chunk.data(),
                           static_cast<int>(chunk.size() * sizeof(std::int16_t)));
}

}  // namespace khdays::platform
