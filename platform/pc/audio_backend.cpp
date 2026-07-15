#include "khdays/platform/audio.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <SDL3/SDL.h>

namespace khdays::platform {

int play_audio_blocking(const khdays::assets::DecodedAudio& audio) {
    if (audio.samples.empty() || audio.sample_rate == 0U
        || audio.channels == 0U) {
        std::cerr << "audio: nothing to play\n";
        return EXIT_FAILURE;
    }

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init(audio) failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;  // host-endian signed 16-bit
    spec.channels = audio.channels;
    spec.freq = static_cast<int>(audio.sample_rate);

    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        std::cerr << "SDL_OpenAudioDeviceStream failed: " << SDL_GetError()
                  << '\n';
        SDL_Quit();
        return EXIT_FAILURE;
    }

    const int bytes =
        static_cast<int>(audio.samples.size() * sizeof(std::int16_t));
    if (!SDL_PutAudioStreamData(stream, audio.samples.data(), bytes)) {
        std::cerr << "SDL_PutAudioStreamData failed: " << SDL_GetError() << '\n';
    }
    SDL_FlushAudioStream(stream);
    SDL_ResumeAudioStreamDevice(stream);

    // Block until the device has consumed the whole buffer, then a short tail
    // so the hardware buffer finishes draining.
    while (SDL_GetAudioStreamAvailable(stream) > 0) {
        SDL_Delay(10);
    }
    SDL_Delay(200);

    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    return EXIT_SUCCESS;
}

}  // namespace khdays::platform
