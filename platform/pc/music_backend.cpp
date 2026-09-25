#include "music_backend.h"

#include <algorithm>
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
        for (std::size_t i = 0; i < inventory.streams.size(); ++i) {
            if (!inventory.streams[i].empty()) {
                stream_index_.emplace(inventory.streams[i], i);
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
    for (auto& worker : workers_) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
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
    if (track_index_.find(current_) == track_index_.end()
        && stream_index_.find(current_) == stream_index_.end()) {
        std::cerr << "music: no track named '" << current_ << "'\n";
        return;  // unknown track: stay silent
    }
    // Reap renders that have finished; superseded ones still running are left
    // alone rather than joined here, which would stall the frame loop for as
    // long as the synth needs.
    workers_.erase(
        std::remove_if(workers_.begin(), workers_.end(),
                       [](Worker& worker) {
                           if (!worker.done->load()) {
                               return false;
                           }
                           worker.thread.join();
                           return true;
                       }),
        workers_.end());
    auto done = std::make_shared<std::atomic<bool>>(false);
    workers_.push_back(Worker{
        std::thread([this, track = current_, generation, done] {
            render_track(track, generation);
            done->store(true);
        }),
        done});
}

void SdlMusicPlayer::set_volume(const float volume) {
    // NaN fails every comparison, so test for the valid range positively.
    volume_.store(volume >= 0.0F ? (volume <= 1.0F ? volume : 1.0F) : 0.0F);
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
        const auto stream = stream_index_.find(track);
        if (stream != stream_index_.end()) {
            // A pre-recorded STRM track (e.g. the title BGM): decode it whole.
            audio = khdays::assets::decode_stream(*sdat_, stream->second);
        } else {
            // An SSEQ sequence: synthesize it (rendered whole and looped).
            audio = khdays::resource::render_music(
                *sdat_, track_index_.at(track),
                static_cast<std::uint32_t>(kSampleRate), kLoopSeconds);
        }
    } catch (const std::exception& error) {
        std::cerr << "music: render failed for '" << track << "': "
                  << error.what() << '\n';
        return;
    }
    const std::uint16_t channels = audio.channels == 0U ? 2U : audio.channels;
    // The output device is stereo; expand a mono track so channels stay aligned.
    if (channels == 1U) {
        std::vector<std::int16_t> stereo;
        stereo.reserve(audio.samples.size() * 2U);
        for (const auto s : audio.samples) {
            stereo.push_back(s);
            stereo.push_back(s);
        }
        audio.samples = std::move(stereo);
    }
    const std::uint16_t out_channels = channels == 1U ? 2U : channels;
    // Streams keep their own rate (most STRMs are not the synth's 32768 Hz),
    // so the stream's input format follows each track rather than playing
    // every one at the device-open rate.
    SDL_AudioSpec track_spec;
    SDL_zero(track_spec);
    track_spec.format = SDL_AUDIO_S16;
    track_spec.channels = kChannels;
    track_spec.freq = audio.sample_rate != 0U
        ? static_cast<int>(audio.sample_rate)
        : kSampleRate;

    // Commit only if no newer request arrived while we were rendering. The
    // stream lock comes first, matching feed(): SDL holds it while calling back.
    SDL_LockAudioStream(stream_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_.load()) {
        SDL_UnlockAudioStream(stream_);
        return;
    }
    if (!SDL_SetAudioStreamFormat(stream_, &track_spec, nullptr)) {
        std::cerr << "music: cannot set the stream format for '" << track
                  << "': " << SDL_GetError() << '\n';
    }
    pcm_ = std::move(audio.samples);
    pos_ = 0;
    loops_ = audio.loops;
    // loop_start is in sample frames; pcm_ is interleaved by channel.
    loop_start_ = static_cast<std::size_t>(audio.loop_start) * out_channels;
    if (loop_start_ >= pcm_.size()) {
        loop_start_ = 0;
    }
    ready_ = !pcm_.empty();
    SDL_UnlockAudioStream(stream_);
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
    const float volume = self->volume_.load();

    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (self->ready_ && !self->pcm_.empty()) {
            for (std::size_t i = 0; i < want_samples; ++i) {
                if (self->pos_ >= self->pcm_.size()) {
                    if (!self->loops_) {
                        break;  // one-shot finished: rest is silence
                    }
                    self->pos_ = self->loop_start_;  // loop from the loop point
                }
                chunk[i] = static_cast<std::int16_t>(
                    self->pcm_[self->pos_++] * volume);
            }
        }
    }
    SDL_PutAudioStreamData(stream, chunk.data(),
                           static_cast<int>(chunk.size() * sizeof(std::int16_t)));
}

}  // namespace khdays::platform
