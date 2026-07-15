#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/audio.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
}

void put32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}

// Build a SWAV header for the given format and length (in 32-bit words).
Bytes swav_header(std::uint8_t format, std::uint16_t rate, std::uint32_t words) {
    Bytes b;
    b.push_back(format);
    b.push_back(0U);           // loop flag
    put16(b, rate);            // sample rate
    put16(b, 0U);              // time
    put16(b, 0U);              // loop start
    put32(b, words);           // non-loop length (words)
    return b;
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    try {
        // PCM16: two exact samples in one 32-bit word.
        {
            Bytes swav = swav_header(1U, 8000U, 1U);
            put16(swav, static_cast<std::uint16_t>(100));
            put16(swav, static_cast<std::uint16_t>(-200 & 0xFFFF));
            const auto audio =
                khdays::assets::decode_swav(swav.data(), swav.size());
            expect(audio.sample_rate == 8000U, "pcm16 rate");
            expect(audio.channels == 1U, "pcm16 channels");
            expect(audio.samples.size() == 2U, "pcm16 sample count");
            expect(audio.samples[0] == 100, "pcm16 sample 0");
            expect(audio.samples[1] == -200, "pcm16 sample 1");
        }

        // PCM8: one word = four signed 8-bit samples, scaled to 16-bit.
        {
            Bytes swav = swav_header(0U, 11025U, 1U);
            swav.push_back(static_cast<std::uint8_t>(0));
            swav.push_back(static_cast<std::uint8_t>(127));
            swav.push_back(static_cast<std::uint8_t>(-128 & 0xFF));
            swav.push_back(static_cast<std::uint8_t>(-1 & 0xFF));
            const auto audio =
                khdays::assets::decode_swav(swav.data(), swav.size());
            expect(audio.samples.size() == 4U, "pcm8 sample count");
            expect(audio.samples[0] == 0, "pcm8 sample 0");
            expect(audio.samples[1] == (127 << 8), "pcm8 sample 1");
            expect(audio.samples[2] == static_cast<std::int16_t>(-128 << 8),
                   "pcm8 sample 2");
        }

        // IMA-ADPCM: a 4-byte header word plus one data word. All-zero nibbles
        // from a zero predictor/index decode to zero samples.
        {
            Bytes swav = swav_header(2U, 16000U, 2U);
            put16(swav, 0U);  // initial predictor
            swav.push_back(0U);  // initial step index
            swav.push_back(0U);  // reserved
            for (int i = 0; i < 4; ++i) {
                swav.push_back(0U);  // 8 nibbles -> 8 samples
            }
            const auto audio =
                khdays::assets::decode_swav(swav.data(), swav.size());
            expect(audio.sample_rate == 16000U, "adpcm rate");
            expect(audio.samples.size() == 8U, "adpcm sample count");
            for (const auto s : audio.samples) {
                expect(s == 0, "adpcm zero decode");
            }
        }

        // WAV serialization: canonical 44-byte header, correct data size.
        {
            khdays::assets::DecodedAudio audio;
            audio.sample_rate = 22050U;
            audio.channels = 1U;
            audio.samples = {0, 1, -1};
            const auto wav = khdays::assets::to_wav(audio);
            expect(wav.size() == 44U + 6U, "wav size");
            expect(std::string(wav.begin(), wav.begin() + 4) == "RIFF",
                   "wav RIFF tag");
            expect(std::string(wav.begin() + 8, wav.begin() + 12) == "WAVE",
                   "wav WAVE tag");
            expect(std::string(wav.begin() + 36, wav.begin() + 40) == "data",
                   "wav data tag");
        }

        // WAV round-trip: to_wav -> file -> load_wav reproduces the samples.
        {
            khdays::assets::DecodedAudio audio;
            audio.sample_rate = 32000U;
            audio.channels = 1U;
            audio.samples = {0, 1000, -1000, 32767, -32768, 42};
            const auto wav = khdays::assets::to_wav(audio);
            const auto wav_path =
                std::filesystem::temp_directory_path() / "khdays_wav_test.wav";
            {
                std::ofstream out{wav_path, std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(wav.data()),
                    static_cast<std::streamsize>(wav.size()));
            }
            const auto back = khdays::assets::load_wav(wav_path);
            std::filesystem::remove(wav_path);
            expect(back.sample_rate == 32000U, "wav round-trip rate");
            expect(back.channels == 1U, "wav round-trip channels");
            expect(back.samples == audio.samples, "wav round-trip samples");
        }

        std::cout << "Audio decoder test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
