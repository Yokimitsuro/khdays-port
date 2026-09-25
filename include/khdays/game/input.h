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
    L = 1u << 10,
    R = 1u << 11,
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

// Key auto-repeat, computed the way the game's own helper (func_0203617c)
// does: a button fires on the frame it is pressed; while it stays held it
// fires again once `delay` frames have passed since the press and then every
// `interval` frames. The game stores delay/interval in units it scales by its
// frame-rate mode -- x2 in mode 0, the 60 Hz mode the front-end runs in -- so
// callers pass frames (e.g. ov000's 15/4 become 30/8).
class KeyRepeat final {
public:
    KeyRepeat(int delay_frames, int interval_frames)
        : delay_(static_cast<std::uint32_t>(delay_frames)),
          interval_(static_cast<std::uint32_t>(interval_frames)) {}

    // Feed one frame's input; returns the buttons that fire this frame.
    std::uint16_t update(const Input& input) {
        std::uint16_t fired = 0;
        for (std::uint32_t bit = 0; bit < 16U; ++bit) {
            const auto mask = static_cast<std::uint16_t>(1U << bit);
            // The pad sampler stamps every change, press or release.
            if (((input.down ^ previous_) & mask) != 0U) {
                stamp_[bit] = now_;
            }
            if ((input.down & mask) == 0U) {
                continue;
            }
            if ((input.pressed & mask) != 0U) {
                fired = static_cast<std::uint16_t>(fired | mask);
                count_[bit] = 0U;
                continue;
            }
            std::uint32_t elapsed = now_ - stamp_[bit];
            if (elapsed >= delay_ && interval_ != 0U) {
                elapsed -= delay_;
                if (elapsed >= interval_ * count_[bit]) {
                    fired = static_cast<std::uint16_t>(fired | mask);
                    count_[bit] = elapsed / interval_ + 1U;
                }
            }
        }
        previous_ = input.down;
        ++now_;
        return fired;
    }

private:
    std::uint32_t delay_;
    std::uint32_t interval_;
    std::uint32_t now_ = 0;
    std::uint16_t previous_ = 0;
    std::uint32_t stamp_[16] = {};
    std::uint32_t count_[16] = {};
};

}  // namespace khdays::game
