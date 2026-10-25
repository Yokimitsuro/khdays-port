// The settings menu's sample sounds (settings_sample.h): the game's own (Title_BGM_PCM8, Roxas's
// rox_warai_S01, a wave of SE_SYS_WAVE), decoded straight from the archive -- the sound driver belongs
// to the paused game -- and played at the volumes the mixer would give them (docs/PLATFORM.md).
#include "settings_sample.h"
#include "settings_ui.h"

#include "khdays/assets/audio.h"

extern "C" {
#include "../app/devices.h"
#include "../../runtime/core/rom.h"
#include "../../runtime/mods/installed.h"
#include "../../sound/archive.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

namespace {

constexpr const char *kArchivePath = "/snd/sound_data.sdat";
constexpr float kSampleSeconds = 2.5f;   // how much of a stream is played
constexpr float kShortestEffect = 0.15f; // effects shorter than this are too short to hear

enum Kind { MUSIC, EFFECTS, VOICES, KINDS };

struct Sample {
    bool tried = false;
    bool ok = false;
    SDL_AudioSpec spec{};
    std::vector<std::int16_t> samples;
    float volume = 1.0f; // the volume the archive gives it
};

KhdaysRomReader *reader;
u32 archive_offset, archive_size;
KhdaysSoundArchive *archive;
Sample samples[KINDS];
SDL_AudioStream *stream;

u32 read_archive(void *, u32 offset, void *destination, u32 size)
{
    if (offset >= archive_size) {
        return 0;
    }
    size = std::min(size, archive_size - offset);
    khdays_rom_reader_read(reader, archive_offset + offset, static_cast<u8 *>(destination), size);
    return size;
}

bool open_archive()
{
    static bool tried;
    if (!tried) {
        tried = true;
        reader = khdays_rom_reader_new();
        if (reader != nullptr && khdays_rom_find(kArchivePath, &archive_offset, &archive_size)) {
            archive = khdays_sound_archive_open(read_archive, nullptr);
        }
    }
    return archive != nullptr;
}

std::vector<std::uint8_t> read_bytes(u32 offset, u32 size)
{
    std::vector<std::uint8_t> bytes(size);
    if (size != 0) {
        khdays_sound_archive_read(archive, offset, bytes.data(), size);
    }
    return bytes;
}

u32 le32(const std::uint8_t *p)
{
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

// The number of the sound of `kind` named `name` (its identifier is
// "strm/<name>" or "wave/<name>/0"), or -1.
int find(int kind, const char *name)
{
    char want[160], id[160];
    std::snprintf(want, sizeof(want), kind == KHDAYS_SOUND_STREAM ? "strm/%s" : "wave/%s/0", name);
    const int count = khdays_sound_archive_count(archive, kind);
    for (int n = 0; n < count; ++n) {
        if (khdays_sound_archive_identifier(archive, kind, n, 0, id, (int)sizeof(id)) && std::strcmp(id, want) == 0) {
            return n;
        }
    }
    return -1;
}

void keep(Sample &s, const khdays::assets::DecodedAudio &audio, std::size_t frames)
{
    frames = std::min(frames, audio.samples.size() / std::max<std::size_t>(1, audio.channels));
    s.spec.format = SDL_AUDIO_S16;
    s.spec.channels = audio.channels;
    s.spec.freq = static_cast<int>(audio.sample_rate);
    s.samples.assign(audio.samples.begin(), audio.samples.begin() + static_cast<long long>(frames * audio.channels));
    s.ok = !s.samples.empty() && s.spec.freq > 0;
}

// Reads and decodes the first seconds of a stream, from the start of its file.
void load_stream(Sample &s, const char *name)
{
    const int n = find(KHDAYS_SOUND_STREAM, name);
    const u32 record = n >= 0 ? khdays_sound_archive_record(archive, KHDAYS_SOUND_STREAM, n) : 0;
    u32 offset = 0, size = 0;
    if (record == 0) {
        return;
    }
    // NNSSndArcStrmInfo: the file (its low 24 bits), the volume, ...
    const std::vector<std::uint8_t> info = read_bytes(record, 8);
    if (!khdays_sound_archive_file(archive, le32(info.data()) & 0xffffff, &offset, &size) || size < 0x68) {
        return;
    }
    const std::vector<std::uint8_t> head = read_bytes(offset, 0x68);
    const u32 channels = std::max<u32>(1, head[0x1a] | head[0x1b] << 8);
    const u32 rate = head[0x1c] | head[0x1d] << 8;
    const u32 data = le32(&head[0x28]), block = le32(&head[0x30]), block_samples = le32(&head[0x34]);
    if (rate == 0 || block_samples == 0) {
        return;
    }
    const u32 blocks = static_cast<u32>(kSampleSeconds * (float)rate / (float)block_samples) + 1;
    const u32 want = std::min(size, data + blocks * channels * block);
    const std::vector<std::uint8_t> bytes = read_bytes(offset, want);
    keep(s, khdays::assets::decode_strm(bytes.data(), bytes.size()),
         static_cast<std::size_t>(kSampleSeconds * (float)rate));
    s.volume = static_cast<float>(info[4]) / 127.0f;
}

// The first wave of a wave archive that plays once and is long enough to hear.
void load_effect(Sample &s, const char *name)
{
    const int n = find(KHDAYS_SOUND_WAVE, name);
    const u32 record = n >= 0 ? khdays_sound_archive_record(archive, KHDAYS_SOUND_WAVE, n) : 0;
    u32 offset = 0, size = 0;
    if (record == 0) {
        return;
    }
    const std::vector<std::uint8_t> info = read_bytes(record, 4); // NNSSndArcWaveArcInfo: the file
    if (!khdays_sound_archive_file(archive, le32(info.data()) & 0xffffff, &offset, &size) || size < 0x3c) {
        return;
    }
    const std::vector<std::uint8_t> swar = read_bytes(offset, size);
    const u32 count = le32(&swar[0x38]);
    for (u32 w = 0; w < count && 0x3c + 4 * (w + 1) <= swar.size(); ++w) {
        const u32 start = le32(&swar[0x3c + 4 * w]);
        const u32 end = w + 1 < count && 0x3c + 4 * (w + 2) <= swar.size() ? le32(&swar[0x3c + 4 * (w + 1)]) : size;
        if (start >= end || end > swar.size()) {
            continue;
        }
        const khdays::assets::DecodedAudio audio = khdays::assets::decode_swav(&swar[start], end - start);
        const float seconds = audio.sample_rate != 0
                                  ? (float)audio.samples.size() / (float)audio.channels / (float)audio.sample_rate
                                  : 0.0f;
        if (!audio.loops && seconds >= kShortestEffect) {
            keep(s, audio, audio.samples.size());
            return;
        }
    }
}

Sample *sample_of(int kind)
{
    Sample &s = samples[kind];
    if (!s.tried) {
        s.tried = true;
        try {
            if (open_archive()) {
                switch (kind) {
                case MUSIC: load_stream(s, "Title_BGM_PCM8"); break;
                case VOICES: load_stream(s, "rox_warai_S01"); break;
                default: load_effect(s, "SE_SYS_WAVE"); break;
                }
            }
        } catch (const std::exception &e) {
            std::fprintf(stderr, "settings: no sample for a volume: %s\n", e.what());
            s.ok = false;
        }
        static const char *const names[KINDS] = {"music", "effects", "voices"};
        if (s.ok) {
            std::fprintf(stderr, "settings: the %s volume's sample: %.2f s at %d Hz, %d channel(s)\n", names[kind],
                         (double)s.samples.size() / s.spec.channels / s.spec.freq, s.spec.freq, s.spec.channels);
        } else {
            std::fprintf(stderr, "settings: the sound archive gave no sample for the %s volume\n", names[kind]);
        }
    }
    return s.ok ? &s : nullptr;
}

void play(int kind, float gain)
{
    const Sample *s = sample_of(kind);
    if (s == nullptr) {
        return;
    }
    if (stream == nullptr) {
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s->spec, nullptr, nullptr);
        if (stream == nullptr) {
            std::fprintf(stderr, "settings: no stream for the volumes' samples: %s\n", SDL_GetError());
            return;
        }
    }
    SDL_ClearAudioStream(stream);
    SDL_SetAudioStreamFormat(stream, &s->spec, nullptr);
    SDL_SetAudioStreamGain(stream, gain * s->volume);
    SDL_PutAudioStreamData(stream, s->samples.data(), static_cast<int>(s->samples.size() * sizeof(std::int16_t)));
    SDL_ResumeAudioStreamDevice(stream);
}

// With the Menu Overhaul, the menus' own sounds: the game's se_sys_click, kettei, open and cancel (COMMON_SYS's
// sequences 0-3, one note of SE_SYS_BNK's instruments 0-3), as --dump-sounds renders them (whole, at full volume:
// the sequence's volume goes on top), from a mod's menu_sounds/se_sys_*.wav; else the wave alone of SE_SYS_WAVE.
struct Cue {
    const char *name; // its sequence
    int wave;         // its wave in SE_SYS_WAVE
    int volume;       // its sequence's volume, 0-127
};
constexpr Cue kCues[] = {{"se_sys_click", 39, 65},
                         {"se_sys_kettei", 40, 120},
                         {"se_sys_open", 42, 100},
                         {"se_sys_cancel", 38, 70}}; // KHDAYS_SETTINGS_CUE_*
Sample cue_samples[4];
bool cue_rendered[4]; // the game's own render (stereo, the pan's halves in it), not the wave alone
// The game plays each on a sequence player of its own (PLAYER_SE has 14): a sound goes on as the next
// starts. A few streams in turn do the same.
constexpr int kCueStreams = 4;
SDL_AudioStream *cue_streams[kCueStreams];
int cue_next;

// Cue `which` as the game sounds it, from the HD pack's menu_sounds; false if it has none.
bool load_rendered(Sample &s, int which)
{
    char relative[96], path[1024];
    SDL_AudioSpec spec;
    Uint8 *bytes = nullptr;
    Uint32 length = 0;
    std::snprintf(relative, sizeof(relative), "menu_sounds/%s.wav", kCues[which].name);
    if (!khdays_mods_find(relative, path, (int)sizeof(path)) || !SDL_LoadWAV(path, &spec, &bytes, &length)) {
        return false;
    }
    if (spec.format != SDL_AUDIO_S16 || length == 0) {
        SDL_free(bytes);
        return false;
    }
    s.spec = spec;
    s.samples.assign(reinterpret_cast<const std::int16_t *>(bytes),
                     reinterpret_cast<const std::int16_t *>(bytes) + length / sizeof(std::int16_t));
    s.ok = !s.samples.empty();
    SDL_free(bytes);
    return s.ok;
}

// Wave `wave` of the wave archive `name`.
void load_wave(Sample &s, const char *name, int wave)
{
    const int n = find(KHDAYS_SOUND_WAVE, name);
    const u32 record = n >= 0 ? khdays_sound_archive_record(archive, KHDAYS_SOUND_WAVE, n) : 0;
    u32 offset = 0, size = 0;
    if (record == 0) {
        return;
    }
    const std::vector<std::uint8_t> info = read_bytes(record, 4);
    if (!khdays_sound_archive_file(archive, le32(info.data()) & 0xffffff, &offset, &size) || size < 0x3c) {
        return;
    }
    const std::vector<std::uint8_t> swar = read_bytes(offset, size);
    const u32 count = le32(&swar[0x38]);
    const u32 w = static_cast<u32>(wave);
    if (w >= count || 0x3c + 4 * (w + 1) > swar.size()) {
        return;
    }
    const u32 start = le32(&swar[0x3c + 4 * w]);
    const u32 end = w + 1 < count && 0x3c + 4 * (w + 2) <= swar.size() ? le32(&swar[0x3c + 4 * (w + 1)]) : size;
    if (start >= end || end > swar.size()) {
        return;
    }
    const khdays::assets::DecodedAudio audio = khdays::assets::decode_swav(&swar[start], end - start);
    keep(s, audio, audio.samples.size());
}

void play_cue(int which, const KhdaysSound *levels)
{
    if (which < 0 || which >= (int)(sizeof(kCues) / sizeof(kCues[0])) || levels == nullptr ||
        !khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return;
    }
    Sample &s = cue_samples[which];
    if (!s.tried) {
        s.tried = true;
        try {
            cue_rendered[which] = load_rendered(s, which);
            if (!cue_rendered[which] && open_archive()) {
                load_wave(s, "SE_SYS_WAVE", kCues[which].wave);
            }
        } catch (const std::exception &e) {
            std::fprintf(stderr, "settings: no menu sound %d: %s\n", which, e.what());
            s.ok = false;
        }
    }
    if (!s.ok) {
        return;
    }
    SDL_AudioStream *&stream_now = cue_streams[cue_next];
    cue_next = (cue_next + 1) % kCueStreams;
    if (stream_now == nullptr) {
        stream_now = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s.spec, nullptr, nullptr);
        if (stream_now == nullptr) {
            std::fprintf(stderr, "settings: no stream for the menu's sounds: %s\n", SDL_GetError());
            return;
        }
    }
    // the sequence's volume as NNS gives it (SND_CalcDecibel: 200 log10(v / 127) tenths of a dB, its share
    // straight); the wave alone, mono, takes the middle pan's half too
    const float v = static_cast<float>(kCues[which].volume) / 127.0f * (cue_rendered[which] ? 1.0f : 0.5f);
    const float gain = static_cast<float>(levels->volume) / 100.0f * static_cast<float>(levels->effects) / 100.0f;
    SDL_ClearAudioStream(stream_now);
    SDL_SetAudioStreamFormat(stream_now, &s.spec, nullptr);
    SDL_SetAudioStreamGain(stream_now, gain * v);
    SDL_PutAudioStreamData(stream_now, s.samples.data(), static_cast<int>(s.samples.size() * sizeof(std::int16_t)));
    SDL_ResumeAudioStreamDevice(stream_now);
}

// The menu asks for its sounds through this (settings_ui.h); the tests' builds of the menu have none.
struct RegisterCue {
    RegisterCue()
    {
        khdays_settings_set_cue(play_cue);
    }
} register_cue;

} // namespace

extern "C" void khdays_settings_sample(const KhdaysSound *before, const KhdaysSound *now)
{
    const float master = static_cast<float>(now->volume) / 100.0f;
    if (now->music != before->music) {
        play(MUSIC, master * static_cast<float>(now->music) / 100.0f);
    } else if (now->voices != before->voices) {
        play(VOICES, master * static_cast<float>(now->voices) / 100.0f);
    } else if (now->effects != before->effects || now->volume != before->volume) {
        play(EFFECTS, master * static_cast<float>(now->effects) / 100.0f);
    }
    // the movies volume: there is no movie sound to play here (khdays_settings_sample_has_movies)
}

extern "C" void khdays_settings_sample_stop(void)
{
    if (stream != nullptr) {
        SDL_ClearAudioStream(stream);
        SDL_PauseAudioStreamDevice(stream);
    }
}

extern "C" int khdays_settings_sample_has_movies(void)
{
    return 0; // playing an HD video's sound as the sample is not built
}

extern "C" void khdays_menu_cue(int which)
{
    play_cue(which, &khdays_devices_profile()->sound);
}
