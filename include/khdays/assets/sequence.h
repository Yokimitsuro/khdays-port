#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "khdays/assets/audio.h"

namespace khdays::assets {

// Optional waveform override: given a (wave archive, swav) pair, return a
// replacement waveform or std::nullopt to use the SDAT's own. Lets the resource
// layer inject modded samples into the synth without the assets layer depending
// on it.
using SampleOverride =
    std::function<std::optional<DecodedAudio>(int wave_archive, int swav)>;

// Render an SDAT sequence to PCM by running its SSEQ bytecode through a software
// synthesizer: notes look up instruments in the sequence's bank (SBNK), which
// point at waveforms in the wave archives (SWAR); each voice is pitched,
// enveloped (ADSR), panned, and mixed. Output is interleaved stereo 16-bit PCM
// at `sample_rate`. Rendering stops when every track ends or after
// `max_seconds` (looping music runs to the limit). If `sample_override` is set,
// it is consulted for each waveform before decoding the SDAT's. Throws
// std::runtime_error on malformed data.
DecodedAudio render_sequence(
    const Sdat& sdat,
    std::size_t sequence_index,
    std::uint32_t sample_rate = 32768U,
    double max_seconds = 180.0,
    const SampleOverride& sample_override = nullptr);

}  // namespace khdays::assets
