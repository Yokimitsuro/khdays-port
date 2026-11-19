#include "khdays/assets/kh2_event.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace khdays::assets::kh2 {
namespace {

std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::int16_t s16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(u16(p));
}

std::uint32_t u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

float f32(const std::uint8_t* p) {
    const std::uint32_t bits = u32(p);
    float f = 0.0F;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

constexpr float kTicksASecond = 512.0F;  // a key's time unit (DAT_1405b1390)
constexpr float kFramesASecond = 30.0F;  // the event's frames (FUN_1402d61f0: DAT_140623bcc)
constexpr float kPi = 3.14159265358979F;

// A key's time, its low 29 bits signed (`(word << 3) >> 3`)
int key_time(std::uint32_t word) {
    return static_cast<int>(word << 3U) >> 3;
}

// The keys `k` and `k + 1` at `seconds` (FUN_1402d42a0)
float segment(const std::vector<EventKey>& keys, std::size_t k, float seconds) {
    const EventKey& a = keys[k];
    const EventKey& b = keys[k + 1];
    const float t0 = static_cast<float>(a.time) / kTicksASecond, t1 = static_cast<float>(b.time) / kTicksASecond;
    switch (a.interpolation) {
    case 0: return a.value;
    case 1: return a.value + (b.value - a.value) * (seconds - t0) / (t1 - t0);
    case 2:
    case 3:
    case 4: {
        const float s = seconds - t0, d = 1.0F / (t1 - t0), s2 = s * s;
        const float cube = s2 * s * d * d;           // s^3 / span^2
        const float twice_cube = 2.0F * cube * d;    // 2 (s / span)^3
        const float thrice_square = 3.0F * s2 * d * d;  // 3 (s / span)^2
        return a.value * ((twice_cube - thrice_square) + 1.0F) + b.value * (-twice_cube + thrice_square) +
               a.slope_out * ((cube - 2.0F * s2 * d) + s) + b.slope_in * (cube - s2 * d);
    }
    default: return 0.0F;
    }
}

}  // namespace

Event read_event(const std::uint8_t* data, std::size_t size) {
    Event ev;
    std::size_t at = 0;
    while (at + 2 <= size) {
        const std::size_t length = u16(data + at);
        if (length == 0) {
            break;  // the end (FUN_1402cc180, FUN_1402cb5d0: a command of size 0)
        }
        if (length < 4 || at + length > size) {
            throw std::runtime_error("kh2 event: command at " + std::to_string(at) + " runs past the entry");
        }
        const std::uint8_t* c = data + at;
        const int number = u16(c + 2);
        ev.commands.push_back(number);
        switch (number) {
        case 0:
            if (length > 10) {
                const char* name = reinterpret_cast<const char*>(c + 10);
                ev.name.assign(name, strnlen(name, length - 10));
            }
            break;
        case 6:
            if (length >= 10) {
                ev.cuts.push_back({s16(c + 4), s16(c + 6), s16(c + 8)});
            }
            break;
        case 8:
            if (length >= 6) {
                ev.length = s16(c + 4);
            }
            break;
        case 0x17:
            if (length >= 0x12) {
                EventTimed t;
                t.command = number;
                t.frame = s16(c + 4);
                t.trail.stop = c[6] != 0;
                t.trail.alpha = c[7];
                t.trail.turn = f32(c + 8);
                t.trail.width = ((u16(c + 0xc) >> 1U) & 0xffffU) * 2;  // FUN_14015f1e0's halves, doubled
                t.trail.height = ((u16(c + 0xe) >> 1U) & 0xffffU) * 2;
                t.trail.turn_ticks = s16(c + 0x10);
                ev.timed.push_back(t);
            }
            break;
        case 0x31:  // the camera given back (sa::EVENT::set_bin_seq_objcamera)
            if (length >= 8) {
                EventTimed t;
                t.command = number;
                t.frame = s16(c + 4);
                t.objcamera = s16(c + 6);
                ev.timed.push_back(t);
            }
            break;
        case 0x4c:  // the camera's start from the game's (sa::EVENT::set_bin_seq_start_camera)
            ev.start_camera = true;
            break;
        case 0x27:
            if (length >= 0x10) {
                EventTimed t;
                t.command = number;
                t.frame = s16(c + 4);
                t.shake.amplitude = {static_cast<float>(s16(c + 6)), static_cast<float>(s16(c + 8)),
                                     static_cast<float>(s16(c + 0xa))};
                t.shake.ticks = 2 * s16(c + 0xc);
                t.shake.shape = s16(c + 0xe);
                ev.timed.push_back(t);
            }
            break;
        case 0x13: {
            if (length < 0x28) {
                throw std::runtime_error("kh2 event: a camera too short for its channels");
            }
            EventCamera cam;
            cam.id = s16(c + 4);
            for (std::size_t ch = 0; ch < cam.channels.size(); ++ch) {
                const std::size_t first = u16(c + 6 + ch * 4);
                const int count = s16(c + 8 + ch * 4);
                if (count <= 0) {
                    continue;
                }
                if (0x28 + (first + static_cast<std::size_t>(count)) * 16 > length) {
                    throw std::runtime_error("kh2 event: a camera channel's keys run past its command");
                }
                auto& keys = cam.channels[ch];
                for (int k = 0; k < count; ++k) {
                    const std::uint8_t* key = c + 0x28 + (first + static_cast<std::size_t>(k)) * 16;
                    const std::uint32_t word = u32(key);
                    keys.push_back({static_cast<int>(word >> 29U), key_time(word), f32(key + 4), f32(key + 8),
                                    f32(key + 12)});
                }
            }
            ev.cameras.push_back(std::move(cam));
            break;
        }
        default: break;
        }
        at += length;
    }
    return ev;
}

float event_channel(const std::vector<EventKey>& keys, float seconds) {
    if (keys.empty()) {
        return 0.0F;
    }
    if (keys.size() == 1) {
        return keys[0].value;
    }
    const int t = static_cast<int>(seconds * kTicksASecond);
    const EventKey& first = keys.front();
    const EventKey& last = keys.back();
    if (t <= first.time) {
        // before the first key: its value carried back by its slope in (the line to the next key's for a linear one)
        const float span = static_cast<float>(keys[1].time - first.time);
        const float slope = first.interpolation == 1 && span != 0.0F ? (keys[1].value - first.value) / span
                                                                     : first.slope_in;
        return first.value - static_cast<float>(first.time - t) * slope;
    }
    if (t >= last.time) {
        const EventKey& before = keys[keys.size() - 2];
        const float span = static_cast<float>(last.time - before.time);
        const float slope = last.interpolation == 1 && span != 0.0F ? (last.value - before.value) / span
                                                                    : last.slope_out;
        return last.value + static_cast<float>(t - last.time) * slope;
    }
    std::size_t lo = 0, hi = keys.size() - 1;
    for (;;) {
        if (hi - lo < 2) {
            return segment(keys, hi - 1, seconds);
        }
        const std::size_t mid = (lo + hi) / 2;
        if (t < keys[mid].time) {
            hi = mid;
        } else if (keys[mid].time < t) {
            lo = mid;
        } else {
            return keys[mid].value;
        }
    }
}

void event_view(const EventCamera& camera, float frame, EventView& out) {
    const float seconds = frame / kFramesASecond;
    const auto& ch = camera.channels;
    // the model's axes to KH2's: y and z the other way
    if (!ch[0].empty()) out.eye[0] = event_channel(ch[0], seconds);
    if (!ch[1].empty()) out.eye[1] = -event_channel(ch[1], seconds);
    if (!ch[2].empty()) out.eye[2] = -event_channel(ch[2], seconds);
    if (!ch[3].empty()) out.at[0] = event_channel(ch[3], seconds);
    if (!ch[4].empty()) out.at[1] = -event_channel(ch[4], seconds);
    if (!ch[5].empty()) out.at[2] = -event_channel(ch[5], seconds);
    if (!ch[6].empty()) out.roll = kPi * event_channel(ch[6], seconds) / 180.0F;
    if (!ch[7].empty()) out.fov = event_channel(ch[7], seconds) * (kPi / 180.0F);  // FUN_1402d6180
}

float event_shake_envelope(int shape, float ticks, float length) {
    const float f = length > 0.0F ? ticks / length : 1.0F;
    switch (shape) {
    case 0: return 1.0F;
    case 1: return 2.0F * (f > 0.5F ? 1.0F - f : f);
    case 2: return f;
    case 3: return 1.0F - f;
    default: return 0.0F;
    }
}

const EventCamera* event_camera_at(const Event& event, float frame, float& within) {
    const EventCamera* shown = nullptr;
    const int whole = static_cast<int>(std::floor(frame));
    for (const EventCut& cut : event.cuts) {
        if (whole < cut.start || whole >= cut.end + 1) {
            continue;
        }
        for (const EventCamera& cam : event.cameras) {
            if (cam.id == cut.camera) {
                shown = &cam;  // each cut that holds the frame sets the camera: the last stands
                within = std::fmin(frame - static_cast<float>(cut.start), static_cast<float>(cut.end - cut.start));
            }
        }
    }
    return shown;
}

}  // namespace khdays::assets::kh2
