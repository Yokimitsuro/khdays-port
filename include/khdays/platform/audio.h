#pragma once

#include "khdays/assets/audio.h"

namespace khdays::platform {

// Play decoded PCM audio through the default output device, blocking until it
// finishes. Returns EXIT_SUCCESS on success, EXIT_FAILURE if audio could not be
// initialized or there was nothing to play.
int play_audio_blocking(const khdays::assets::DecodedAudio& audio);

}  // namespace khdays::platform
