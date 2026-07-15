#pragma once

#include <cstdint>

namespace khdays::game {

// Neutral pad buttons (loosely mirroring the DS). The platform maps real
// devices onto these so scene logic stays device-independent.
enum class Button : std::uint16_t {
    A = 1u << 0,
    B = 1u << 1,
    X = 1u << 2,
    Y = 1u << 3,
    Start = 1u << 4,
    Select = 1u << 5,
    Up = 1u << 6,
    Down = 1u << 7,
    Left = 1u << 8,
    Right = 1u << 9,
};

// One frame's input snapshot: which buttons are held, and which became pressed
// this frame.
struct Input final {
    std::uint16_t down = 0;
    std::uint16_t pressed = 0;

    bool held(Button b) const {
        return (down & static_cast<std::uint16_t>(b)) != 0;
    }
    bool just_pressed(Button b) const {
        return (pressed & static_cast<std::uint16_t>(b)) != 0;
    }
};

}  // namespace khdays::game
