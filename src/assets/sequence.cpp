#include "khdays/assets/sequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "khdays/assets/audio.h"

// Software synthesizer for Nintendo DS SDAT sequences. Interprets the SSEQ
// command stream across its tracks, resolves each note to an SBNK instrument
// region (waveform + ADSR + pan), and mixes pitched, enveloped voices into
// stereo PCM. The SSEQ opcode set and SBNK layout follow the public Nintendo DS
// documentation, validated against the game's own sequences.

namespace khdays::assets {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ----- SBNK instrument bank -------------------------------------------------

struct Region final {
    int swav = 0;
    int swar_slot = 0;
    int base_note = 60;
    std::uint8_t attack = 127;
    std::uint8_t decay = 127;
    std::uint8_t sustain = 127;
    std::uint8_t release = 127;
    std::uint8_t pan = 64;
};

struct Instrument final {
    int type = 0;  // 0 empty; 1-5 single; 16 drum set; 17 key split
    std::array<std::uint8_t, 8> bounds{};  // key-split upper-note limits
    int low_note = 0;                      // drum-set base note
    std::vector<Region> regions;

    const Region* resolve(const int note) const {
        if (regions.empty()) {
            return nullptr;
        }
        if (type == 16) {
            const int index = note - low_note;
            if (index < 0 || index >= static_cast<int>(regions.size())) {
                return nullptr;
            }
            return &regions[static_cast<std::size_t>(index)];
        }
        if (type == 17) {
            for (std::size_t i = 0; i < regions.size() && i < 8U; ++i) {
                if (note <= bounds[i]) {
                    return &regions[i];
                }
            }
            return nullptr;
        }
        return &regions[0];
    }
};

// A drum/key-split sub-region: u16 type; u16 swav; u16 swar; u8 note; ADSR; pan.
Region parse_sub_region(const std::uint8_t* p) {
    Region r;
    r.swav = p[2] | (p[3] << 8);
    r.swar_slot = p[4] | (p[5] << 8);
    r.base_note = p[6];
    r.attack = p[7];
    r.decay = p[8];
    r.sustain = p[9];
    r.release = p[10];
    r.pan = p[11];
    return r;
}

std::vector<Instrument> parse_sbnk(const std::uint8_t* d, const std::size_t size) {
    const std::uint32_t count = static_cast<std::uint32_t>(d[0x38])
        | (static_cast<std::uint32_t>(d[0x39]) << 8U)
        | (static_cast<std::uint32_t>(d[0x3A]) << 16U)
        | (static_cast<std::uint32_t>(d[0x3B]) << 24U);

    std::vector<Instrument> instruments;
    instruments.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t rec = 0x3CU + static_cast<std::size_t>(i) * 4U;
        if (rec + 4U > size) {
            break;
        }
        Instrument inst;
        inst.type = d[rec];
        const std::size_t off = d[rec + 1U] | (d[rec + 2U] << 8U);
        if (inst.type == 0 || off == 0U || off + 2U > size) {
            instruments.push_back(std::move(inst));
            continue;
        }
        const std::uint8_t* p = d + off;
        if (inst.type >= 1 && inst.type <= 5) {  // single waveform
            Region r;
            r.swav = p[0] | (p[1] << 8);
            r.swar_slot = p[2] | (p[3] << 8);
            r.base_note = p[4];
            r.attack = p[5];
            r.decay = p[6];
            r.sustain = p[7];
            r.release = p[8];
            r.pan = p[9];
            inst.regions.push_back(r);
        } else if (inst.type == 16) {  // drum set
            inst.low_note = p[0];
            const int high = p[1];
            const std::uint8_t* q = p + 2;
            for (int n = inst.low_note; n <= high; ++n) {
                if (static_cast<std::size_t>(q + 12 - d) > size) {
                    break;
                }
                inst.regions.push_back(parse_sub_region(q));
                q += 12;
            }
        } else if (inst.type == 17) {  // key split
            for (int b = 0; b < 8; ++b) {
                inst.bounds[static_cast<std::size_t>(b)] = p[b];
            }
            const std::uint8_t* q = p + 8;
            for (int b = 0; b < 8 && inst.bounds[static_cast<std::size_t>(b)] != 0; ++b) {
                if (static_cast<std::size_t>(q + 12 - d) > size) {
                    break;
                }
                inst.regions.push_back(parse_sub_region(q));
                q += 12;
            }
        }
        instruments.push_back(std::move(inst));
    }
    return instruments;
}

// ----- Voices ---------------------------------------------------------------

struct Voice final {
    const DecodedAudio* sample = nullptr;
    double pos = 0.0;
    double step = 0.0;
    bool loops = false;
    double loop_start = 0.0;

    double gate_samples = 0.0;  // remaining gated time; <0 = released
    bool releasing = false;
    bool dead = false;

    // Envelope.
    double env = 0.0;
    int phase = 0;  // 0 attack, 1 decay, 2 sustain, 3 release
    double attack_inc = 1.0;
    double decay_inc = 1.0;
    double release_inc = 1.0;
    double sustain_level = 1.0;

    double gain_l = 0.0;
    double gain_r = 0.0;
};

double envelope_time(const std::uint8_t rate, const double max_seconds) {
    // 127 = instant, lower = slower.
    return (127.0 - static_cast<double>(rate)) / 127.0 * max_seconds;
}

// ----- Track state ----------------------------------------------------------

struct Track final {
    bool active = false;
    std::size_t pc = 0;
    int delay = 0;
    int program = 0;
    int volume = 127;
    int expression = 127;
    int pan = 64;
    bool has_pan = false;
    int transpose = 0;
    int pitch_bend = 0;
    int bend_range = 2;
    std::vector<std::size_t> call_stack;
    std::size_t loop_pc = 0;
    int loop_count = 0;
    bool in_loop = false;
    bool cond = true;
};

std::uint32_t read_u24(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8U)
        | (static_cast<std::uint32_t>(p[2]) << 16U);
}

}  // namespace

DecodedAudio render_sequence(
    const Sdat& sdat,
    const std::size_t sequence_index,
    const std::uint32_t sample_rate,
    const double max_seconds) {
    const auto seq = sdat_sequence(sdat, sequence_index);
    if (seq.bank < 0) {
        throw std::runtime_error("sequence has no bank");
    }
    const auto bank = sdat_bank(sdat, static_cast<std::size_t>(seq.bank));
    const auto instruments = parse_sbnk(bank.data, bank.size);

    // SSEQ command stream: DATA block, u32 pointer at 0x18 to the commands.
    const std::uint32_t data_ptr = static_cast<std::uint32_t>(seq.data[0x18])
        | (static_cast<std::uint32_t>(seq.data[0x19]) << 8U)
        | (static_cast<std::uint32_t>(seq.data[0x1A]) << 16U)
        | (static_cast<std::uint32_t>(seq.data[0x1B]) << 24U);
    if (data_ptr >= seq.size) {
        throw std::runtime_error("SSEQ command pointer is out of range");
    }
    const std::uint8_t* cmd = seq.data + data_ptr;
    const std::size_t cmd_len = seq.size - data_ptr;

    // Decoded-waveform cache, keyed by (wave archive, swav).
    std::map<std::pair<int, int>, DecodedAudio> sample_cache;
    const auto fetch_sample =
        [&](const int swar_slot, const int swav) -> const DecodedAudio* {
        if (swar_slot < 0 || swar_slot >= 4) {
            return nullptr;
        }
        const int wave_archive = bank.wave_archives[static_cast<std::size_t>(swar_slot)];
        if (wave_archive < 0) {
            return nullptr;
        }
        const auto key = std::make_pair(wave_archive, swav);
        auto it = sample_cache.find(key);
        if (it == sample_cache.end()) {
            try {
                it = sample_cache
                         .emplace(key, sdat_waveform(
                                           sdat,
                                           static_cast<std::size_t>(wave_archive),
                                           static_cast<std::size_t>(swav)))
                         .first;
            } catch (const std::exception&) {
                return nullptr;
            }
        }
        return &it->second;
    };

    std::array<Track, 16> tracks{};
    std::vector<Voice> voices;
    double tempo = 120.0;
    const double seq_gain = static_cast<double>(seq.volume) / 127.0;

    // Track 0 begins at the stream start; a leading 0xFE allocates tracks and
    // 0x93 records point the other tracks at their command offsets.
    tracks[0].active = true;
    tracks[0].pc = 0;

    const auto spawn_voice =
        [&](const Track& track, const int note, const int velocity,
            const int gate_ticks, const double samples_per_tick) {
            static const Instrument empty_instrument{};
            const Instrument& inst =
                track.program >= 0
                        && static_cast<std::size_t>(track.program) < instruments.size()
                    ? instruments[static_cast<std::size_t>(track.program)]
                    : empty_instrument;
            const Region* region = inst.resolve(note);
            if (region == nullptr) {
                return;
            }
            const DecodedAudio* sample = fetch_sample(region->swar_slot, region->swav);
            if (sample == nullptr || sample->samples.empty()) {
                return;
            }

            Voice v;
            v.sample = sample;
            v.loops = sample->loops;
            v.loop_start = static_cast<double>(sample->loop_start);

            const double semitones = static_cast<double>(note - region->base_note)
                + track.transpose
                + static_cast<double>(track.pitch_bend) * track.bend_range / 128.0;
            v.step = std::pow(2.0, semitones / 12.0)
                * static_cast<double>(sample->sample_rate)
                / static_cast<double>(sample_rate);

            v.gate_samples = static_cast<double>(gate_ticks) * samples_per_tick;

            const double amp = (static_cast<double>(velocity) / 127.0)
                * (static_cast<double>(track.volume) / 127.0)
                * (static_cast<double>(track.expression) / 127.0) * seq_gain * 0.4;
            const int pan = track.has_pan ? track.pan : region->pan;
            const double theta = (static_cast<double>(pan) / 127.0) * (kPi / 2.0);
            v.gain_l = std::cos(theta) * amp;
            v.gain_r = std::sin(theta) * amp;

            v.sustain_level = static_cast<double>(region->sustain) / 127.0;
            // Floor the attack to ~1.5 ms so an "instant" attack still ramps up
            // over a few samples instead of clicking at note onset.
            const double attack_time =
                std::max(envelope_time(region->attack, 0.5), 0.0015);
            v.attack_inc = 1.0 / (attack_time * sample_rate);
            const double decay_time = envelope_time(region->decay, 0.8);
            v.decay_inc = decay_time > 0.0
                ? (1.0 - v.sustain_level) / (decay_time * sample_rate)
                : 1.0;
            const double release_time = envelope_time(region->release, 0.8) + 0.005;
            v.release_inc = 1.0 / (release_time * sample_rate);
            voices.push_back(v);
        };

    // Advance one sequencer tick: run each track's commands until it waits.
    const auto read_varlen = [&](std::size_t& pc) -> int {
        int value = 0;
        for (int i = 0; i < 4 && pc < cmd_len; ++i) {
            const std::uint8_t b = cmd[pc++];
            value = (value << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) {
                break;
            }
        }
        return value;
    };

    double samples_per_tick =
        static_cast<double>(sample_rate) * 60.0 / (tempo * 48.0);

    const auto step_track = [&](Track& track) {
        int guard = 0;
        while (track.active && track.delay == 0 && ++guard < 100000) {
            if (track.pc >= cmd_len) {
                track.active = false;
                break;
            }
            std::size_t pc = track.pc;
            std::uint8_t op = cmd[pc++];

            bool conditional = false;
            if (op == 0xA2) {  // if: run next command only when cond is true
                conditional = true;
                if (pc >= cmd_len) {
                    track.active = false;
                    break;
                }
                op = cmd[pc++];
            }
            const bool run = !conditional || track.cond;

            if (op < 0x80) {  // note on
                const int note = op;
                const int velocity = pc < cmd_len ? cmd[pc++] : 0;
                const int gate = read_varlen(pc);
                if (run) {
                    spawn_voice(track, note, velocity, gate, samples_per_tick);
                }
                track.pc = pc;
                continue;
            }

            switch (op) {
                case 0x80: {  // rest
                    const int rest = read_varlen(pc);
                    if (run) {
                        track.delay = rest;
                    }
                    break;
                }
                case 0x81: {  // program change
                    const int prog = read_varlen(pc);
                    if (run) {
                        track.program = prog;
                    }
                    break;
                }
                case 0x93: {  // open track (header only)
                    const int number = cmd[pc];
                    const std::uint32_t off = read_u24(cmd + pc + 1U);
                    pc += 4U;
                    if (number > 0 && number < static_cast<int>(tracks.size())) {
                        tracks[static_cast<std::size_t>(number)].active = true;
                        tracks[static_cast<std::size_t>(number)].pc = off;
                    }
                    break;
                }
                case 0x94: {  // jump
                    const std::uint32_t off = read_u24(cmd + pc);
                    pc += 3U;
                    if (run) {
                        pc = off;
                    }
                    break;
                }
                case 0x95: {  // call
                    const std::uint32_t off = read_u24(cmd + pc);
                    pc += 3U;
                    if (run) {
                        track.call_stack.push_back(pc);
                        pc = off;
                    }
                    break;
                }
                case 0xC0: track.pan = cmd[pc++]; track.has_pan = true; break;
                case 0xC1: track.volume = cmd[pc++]; break;
                case 0xC2: track.expression = cmd[pc++]; break;
                case 0xC3: track.transpose = static_cast<std::int8_t>(cmd[pc++]); break;
                case 0xC4: track.pitch_bend = static_cast<std::int8_t>(cmd[pc++]); break;
                case 0xC5: track.bend_range = cmd[pc++]; break;
                case 0xD4: {  // loop start
                    track.loop_count = cmd[pc++];
                    track.loop_pc = pc;
                    track.in_loop = true;
                    break;
                }
                case 0xFC: {  // loop end
                    if (track.in_loop) {
                        if (track.loop_count > 1) {
                            --track.loop_count;
                            pc = track.loop_pc;
                        } else {
                            track.in_loop = false;
                        }
                    }
                    break;
                }
                case 0xFD: {  // return
                    if (!track.call_stack.empty()) {
                        pc = track.call_stack.back();
                        track.call_stack.pop_back();
                    } else {
                        track.active = false;
                    }
                    break;
                }
                case 0xFE: pc += 2U; break;  // allocate tracks (bitmask)
                case 0xFF: track.active = false; break;  // end of track
                case 0xE1: {  // tempo
                    const int t = cmd[pc] | (cmd[pc + 1U] << 8U);
                    pc += 2U;
                    if (run && t > 0) {
                        tempo = static_cast<double>(t);
                        samples_per_tick =
                            static_cast<double>(sample_rate) * 60.0 / (tempo * 48.0);
                    }
                    break;
                }
                case 0xE0: case 0xE3: pc += 2U; break;  // 2-byte params
                default:
                    if (op >= 0xB0 && op <= 0xBF) {
                        pc += 3U;  // variable ops
                    } else if (op >= 0xC0 && op <= 0xD6) {
                        pc += 1U;  // remaining 1-byte controllers
                    } else if (op == 0xA0) {
                        pc += 1U;  // random prefix handled crudely: skip its cmd
                    } else if (op == 0xA1) {
                        pc += 1U;
                    }
                    break;
            }
            track.pc = pc;
        }
    };

    const auto any_active = [&]() {
        for (const auto& t : tracks) {
            if (t.active) {
                return true;
            }
        }
        return false;
    };

    // Sample-driven render loop; the sequencer ticks whenever the accumulator
    // runs out.
    DecodedAudio out;
    out.sample_rate = sample_rate;
    out.channels = 2;
    const std::size_t max_samples =
        static_cast<std::size_t>(max_seconds * sample_rate);
    double tick_accumulator = 0.0;

    // Two-pole low-pass (~12 kHz, cascaded one-poles = 12 dB/oct) to tame the
    // grain the resampler leaves in the top octave without dulling the mids.
    const double lp_alpha =
        1.0 - std::exp(-2.0 * kPi * 12000.0 / static_cast<double>(sample_rate));
    double lp_left = 0.0;
    double lp_left2 = 0.0;
    double lp_right = 0.0;
    double lp_right2 = 0.0;

    while (out.samples.size() / 2U < max_samples) {
        if (tick_accumulator <= 0.0) {
            if (!any_active() && voices.empty()) {
                break;
            }
            for (auto& track : tracks) {
                if (!track.active) {
                    continue;
                }
                if (track.delay > 0) {
                    --track.delay;
                } else {
                    step_track(track);
                    if (track.delay > 0) {
                        --track.delay;
                    }
                }
            }
            tick_accumulator += samples_per_tick;
        }

        // Mix one output frame.
        double left = 0.0;
        double right = 0.0;
        for (auto& v : voices) {
            if (v.dead) {
                continue;
            }
            // Catmull-Rom cubic interpolation: smoother than linear, so the
            // resampler adds less grain of its own.
            const auto& buf = v.sample->samples;
            const auto n = static_cast<std::ptrdiff_t>(buf.size());
            const auto i = static_cast<std::ptrdiff_t>(v.pos);
            double s = 0.0;
            if (i < n) {
                const auto at = [&](std::ptrdiff_t k) -> double {
                    if (k < 0) k = 0;
                    if (k >= n) k = n - 1;
                    return buf[static_cast<std::size_t>(k)];
                };
                const double t = v.pos - static_cast<double>(i);
                const double y0 = at(i - 1);
                const double y1 = at(i);
                const double y2 = at(i + 1);
                const double y3 = at(i + 2);
                const double a0 = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
                const double a1 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
                const double a2 = -0.5 * y0 + 0.5 * y2;
                s = ((a0 * t + a1) * t + a2) * t + y1;
            }

            // Envelope.
            switch (v.phase) {
                case 0:
                    v.env += v.attack_inc;
                    if (v.env >= 1.0) { v.env = 1.0; v.phase = 1; }
                    break;
                case 1:
                    v.env -= v.decay_inc;
                    if (v.env <= v.sustain_level) { v.env = v.sustain_level; v.phase = 2; }
                    break;
                case 2:
                    break;
                case 3:
                    v.env -= v.release_inc;
                    if (v.env <= 0.0) { v.env = 0.0; v.dead = true; }
                    break;
                default:
                    break;
            }
            const double value = s * v.env;
            left += value * v.gain_l;
            right += value * v.gain_r;

            v.pos += v.step;
            if (v.pos >= static_cast<double>(v.sample->samples.size())) {
                if (v.loops) {
                    v.pos = v.loop_start
                        + (v.pos - static_cast<double>(v.sample->samples.size()));
                    if (v.pos < 0.0
                        || v.pos >= static_cast<double>(v.sample->samples.size())) {
                        v.pos = v.loop_start;
                    }
                } else {
                    // Hold the last sample and fade it out (release) instead of
                    // snapping to silence, which would click.
                    v.pos = static_cast<double>(v.sample->samples.size() - 1);
                    if (!v.releasing) {
                        v.releasing = true;
                        v.phase = 3;
                    }
                }
            }

            if (!v.releasing) {
                v.gate_samples -= 1.0;
                if (v.gate_samples <= 0.0) {
                    v.releasing = true;
                    v.phase = 3;
                }
            }
        }

        lp_left += lp_alpha * (left - lp_left);
        lp_left2 += lp_alpha * (lp_left - lp_left2);
        lp_right += lp_alpha * (right - lp_right);
        lp_right2 += lp_alpha * (lp_right - lp_right2);
        left = lp_left2;
        right = lp_right2;

        // Soft-knee limiter: transparent below the knee, tanh-compressed above,
        // so stacked voices never hard-clip into buzzing.
        const auto limit = [](double x) -> std::int16_t {
            constexpr double knee = 28000.0;
            if (x > knee) {
                x = knee + (32767.0 - knee) * std::tanh((x - knee) / (32767.0 - knee));
            } else if (x < -knee) {
                x = -knee - (32767.0 - knee) * std::tanh((-x - knee) / (32767.0 - knee));
            }
            return static_cast<std::int16_t>(x);
        };
        out.samples.push_back(limit(left));
        out.samples.push_back(limit(right));
        tick_accumulator -= 1.0;

        // Drop dead voices occasionally to keep the mix small.
        if ((out.samples.size() & 0x3FFU) == 0U) {
            voices.erase(
                std::remove_if(voices.begin(), voices.end(),
                               [](const Voice& v) { return v.dead; }),
                voices.end());
        }
    }

    return out;
}

}  // namespace khdays::assets
