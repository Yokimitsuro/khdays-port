#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// KH2's event sequences (BAR entries of type 0x16, OpenKH's "Animation Loader"): an enemy's .a.us holds the ones its
// script starts by name (1:157, FUN_1403db2f0; Cerberus's reaction command's "00" to "30"), played by KH2's event
// player (FUN_1402cc4b0). Our reading of KH2FM.exe and the files; docs/KH2_ENEMIES.md, "Event sequences", has the
// layout and the functions each part follows.
namespace khdays::assets::kh2 {

// One key of a camera channel (16 bytes): bits 29-31 its interpolation to the next (0 the key's value held, 1 linear,
// 2-4 Hermite), bits 0-28 its time (signed, 1/512 s from the event's start), its value, its slopes in and out (a
// second's change).
struct EventKey final {
    int interpolation = 0;
    int time = 0;
    float value = 0.0F, slope_in = 0.0F, slope_out = 0.0F;
};

// A camera (command 0x13, FUN_1402d2620): its number and its eight channels -- the eye's x, y, z, the place it looks
// at, its roll (degrees) and its vertical field of view (degrees) -- in the model's axes (y up); a channel without keys
// leaves that value as it was.
struct EventCamera final {
    int id = 0;
    std::array<std::vector<EventKey>, 8> channels;
};

// A camera's span (command 6, FUN_1402d2660): that camera shows from frame `start` to `end` (30 a second).
struct EventCut final {
    int camera = 0, start = 0, end = 0;
};

struct Event final {
    std::string name;  // command 0's, from + 10 ("re_b_he020-00")
    int length = 0;    // command 8's frames
    std::vector<EventCamera> cameras;
    std::vector<EventCut> cuts;
    std::vector<int> commands;  // every command's number, in order (what the port does not play is told from it)
};

// The event in an entry of type 0x16: its commands each a u16 size and a u16 number, to one of size 0 or the end.
// Throws std::runtime_error when a command runs past the entry.
Event read_event(const std::uint8_t* data, std::size_t size);

// A channel at `seconds` from the event's start (FUN_1402d4580, FUN_1402d42a0): before its first key and after its
// last, that key's value carried on by its slope (the line to the next key's for a linear one); between, the keys'
// interpolation.
float event_channel(const std::vector<EventKey>& keys, float seconds);

// What a camera shows at `frame` (30 a second) from its cut's start (FUN_1402d3d40): the eye and the place it looks at
// in KH2's axes (the model's turned half round x: y and z the other way), the roll and the vertical field of view in
// radians. Channels without keys keep `out`'s.
struct EventView final {
    std::array<float, 3> eye{}, at{};
    float roll = 0.0F, fov = 0.0F;
};
void event_view(const EventCamera& camera, float frame, EventView& out);

// The camera an event shows at `frame` from its start, and the frame within its cut (FUN_1402d37b0: the cut whose
// span holds the frame, its time held at the cut's end); nullptr when no cut holds it.
const EventCamera* event_camera_at(const Event& event, float frame, float& within);

}  // namespace khdays::assets::kh2
