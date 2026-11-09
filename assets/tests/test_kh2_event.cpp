// KH2's event sequences (kh2_event.h) on a made-up event: its commands read, its camera's keys (their interpolation
// and signed times), the channels between and beyond their keys, the eye and look-at turned to KH2's axes, the field
// of view in radians, the cuts that say which camera shows when, and a screen trail and a camera shake on their
// frames with the shake's envelopes.
#include "khdays/assets/kh2_event.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace kh2 = khdays::assets::kh2;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4F;
}

void put16(std::vector<std::uint8_t>& b, int v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

void putf(std::vector<std::uint8_t>& b, float f) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    put32(b, bits);
}

struct Key {
    int interpolation, time;
    float value, in, out;
};

// A command: its size and number, then `body`
void command(std::vector<std::uint8_t>& b, int number, const std::vector<std::uint8_t>& body) {
    put16(b, static_cast<int>(body.size()) + 4);
    put16(b, number);
    b.insert(b.end(), body.begin(), body.end());
}

std::vector<std::uint8_t> made_up_event() {
    std::vector<std::uint8_t> b, body;
    // 0: its name at + 10
    put32(body, 0);
    put16(body, 2);
    for (const char ch : std::string("re_test-00"))
        body.push_back(static_cast<std::uint8_t>(ch));
    body.push_back(0);
    command(b, 0, body);
    command(b, 0xf, {});
    body.clear();
    put16(body, 60);
    put16(body, 18);
    command(b, 8, body);
    // a camera: x linear 0 -> 100 over a second (a key before the start too), y held at 50, z a Hermite step 0 -> 1,
    // the field of view 45 degrees
    const std::vector<std::vector<Key>> channels = {
        {{1, -512, -100.0F, 0, 0}, {1, 0, 0.0F, 0, 0}, {1, 512, 100.0F, 0, 0}},
        {{0, 0, 50.0F, 0, 0}, {0, 512, 70.0F, 0, 0}},
        {{2, 0, 0.0F, 0, 0}, {2, 512, 1.0F, 0, 0}},
        {},
        {},
        {},
        {},
        {{2, 0, 45.0F, 0, 0}},
    };
    body.clear();
    put16(body, 3);  // its number
    int first = 0;
    for (const auto& ch : channels) {
        put16(body, first);
        put16(body, static_cast<int>(ch.size()));
        first += static_cast<int>(ch.size());
    }
    put16(body, 0);  // to + 0x28
    for (const auto& ch : channels)
        for (const Key& k : ch) {
            put32(body, (static_cast<std::uint32_t>(k.interpolation) << 29U) |
                            (static_cast<std::uint32_t>(k.time) & 0x1fffffffU));
            putf(body, k.value);
            putf(body, k.in);
            putf(body, k.out);
        }
    command(b, 0x13, body);
    body.clear();
    put16(body, 3);  // camera 3 from frame 0 to 60
    put16(body, 0);
    put16(body, 60);
    put16(body, 0);
    command(b, 6, body);
    // a trail from frame 10 (Cerberus's: alpha 0x3c, its quad 514 x 418, no turn), then a shake at frame 9 (150 cm
    // each way across, for 7 frames, rising then falling): listed after it, played after it
    body.clear();
    put16(body, 10);
    body.push_back(0);
    body.push_back(0x3c);
    putf(body, 0.0F);
    put16(body, 0x202);
    put16(body, 0x1a2);
    put16(body, 0);
    put16(body, 0);
    command(b, 0x17, body);
    body.clear();
    put16(body, 9);
    put16(body, 150);
    put16(body, 150);
    put16(body, 0);
    put16(body, 7);
    put16(body, 1);
    command(b, 0x27, body);
    put16(b, 0);  // the end
    put16(b, 0);
    return b;
}

}  // namespace

int main() {
    const auto bytes = made_up_event();
    const kh2::Event ev = kh2::read_event(bytes.data(), bytes.size());
    check(ev.name == "re_test-00", "the name from command 0");
    check(ev.length == 60, "the length from command 8");
    check(ev.commands == std::vector<int>({0, 0xf, 8, 0x13, 6, 0x17, 0x27}), "every command in order");
    check(ev.timed.size() == 2 && ev.timed[0].command == 0x17 && ev.timed[1].command == 0x27,
          "the timed commands in the file's order");
    if (ev.timed.size() == 2) {
        const kh2::EventTrail& trail = ev.timed[0].trail;
        check(ev.timed[0].frame == 10 && !trail.stop && trail.alpha == 0x3c && trail.width == 514 &&
                  trail.height == 418 && trail.turn == 0.0F,
              "the trail");
        const kh2::EventShake& shake = ev.timed[1].shake;
        check(ev.timed[1].frame == 9 && shake.amplitude[0] == 150.0F && shake.amplitude[1] == 150.0F &&
                  shake.amplitude[2] == 0.0F && shake.ticks == 14 && shake.shape == 1,
              "the shake (its frames twice in ticks)");
    }
    check(near(kh2::event_shake_envelope(0, 3.0F, 14.0F), 1.0F), "a steady shake");
    check(near(kh2::event_shake_envelope(1, 7.0F, 14.0F), 1.0F) &&
              near(kh2::event_shake_envelope(1, 10.5F, 14.0F), 0.5F),
          "a shake rising then falling");
    check(near(kh2::event_shake_envelope(2, 3.5F, 14.0F), 0.25F) &&
              near(kh2::event_shake_envelope(3, 3.5F, 14.0F), 0.75F) &&
              kh2::event_shake_envelope(5, 1.0F, 2.0F) == 0.0F,
          "the ramps, and none for another shape");
    check(ev.cuts.size() == 1 && ev.cuts[0].camera == 3 && ev.cuts[0].start == 0 && ev.cuts[0].end == 60,
          "the cut");
    check(ev.cameras.size() == 1 && ev.cameras[0].id == 3, "the camera");
    if (ev.cameras.size() != 1) {
        return 1;
    }
    const kh2::EventCamera& cam = ev.cameras[0];
    check(cam.channels[0].size() == 3 && cam.channels[0][0].time == -512, "a key's time is signed");
    check(cam.channels[0][0].interpolation == 1 && cam.channels[2][0].interpolation == 2, "the interpolations");
    check(cam.channels[3].empty() && cam.channels[7].size() == 1, "channels without keys");

    // linear, before the first key, at a key, beyond the last (the line to the key before carried on)
    check(near(kh2::event_channel(cam.channels[0], 0.5F), 50.0F), "linear between");
    check(near(kh2::event_channel(cam.channels[0], -0.5F), -50.0F), "linear between keys before the start");
    check(near(kh2::event_channel(cam.channels[0], 0.0F), 0.0F), "at a key");
    check(near(kh2::event_channel(cam.channels[0], 1.5F), 150.0F), "linear beyond the last key");
    // a held key
    check(near(kh2::event_channel(cam.channels[1], 0.9F), 50.0F), "a key held");
    // Hermite with flat slopes: the smooth step
    check(near(kh2::event_channel(cam.channels[2], 0.5F), 0.5F), "Hermite half way");
    check(near(kh2::event_channel(cam.channels[2], 0.25F), 0.15625F), "Hermite a quarter of the way");
    // one key: its value
    check(near(kh2::event_channel(cam.channels[7], 3.0F), 45.0F), "a single key");

    // the view at frame 15 (half a second): KH2's axes, the field of view in radians, the look-at left as it was
    kh2::EventView view;
    view.at = {1.0F, 2.0F, 3.0F};
    kh2::event_view(cam, 15.0F, view);
    check(near(view.eye[0], 50.0F) && near(view.eye[1], -50.0F) && near(view.eye[2], -0.5F),
          "the eye in KH2's axes");
    check(near(view.at[0], 1.0F) && near(view.at[1], 2.0F) && near(view.at[2], 3.0F), "empty channels kept");
    check(near(view.fov, 45.0F * 3.14159265F / 180.0F), "the field of view in radians");

    // the cuts: the camera within its span, its time held at its end, none after
    float within = 0.0F;
    check(kh2::event_camera_at(ev, 30.0F, within) == &cam && near(within, 30.0F), "a frame within the cut");
    check(kh2::event_camera_at(ev, 60.5F, within) == &cam && near(within, 60.0F), "the cut's last frame held");
    check(kh2::event_camera_at(ev, 61.0F, within) == nullptr, "no camera after the cut");

    // a command that runs past its entry
    bool threw = false;
    try {
        (void)kh2::read_event(bytes.data(), 40);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "a cut-short entry refused");

    if (failures == 0) {
        std::printf("kh2 events: commands, keys, channels, views, cuts, trails and shakes as KH2 reads them\n");
    }
    return failures == 0 ? 0 : 1;
}
