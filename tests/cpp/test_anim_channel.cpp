#include <iostream>

#include "khdays/game/game_node.h"

namespace {

int failures = 0;

void expect(const bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using khdays::game::AnimChannel;
    constexpr int kFx = AnimChannel::kFxOne;

    // --- Basic frame set (20.12 fixed) ------------------------------------
    {
        AnimChannel ch(10);
        ch.set_frame(5);
        expect(ch.frame() == 5, "whole-frame index");
        expect(ch.frame_fx() == 5 * kFx, "20.12 fixed value");
        expect(ch.length() == 10, "length");
    }

    // --- Wrap once, exactly like Anim_SetFrameWrapped --------------------
    {
        AnimChannel ch(10);
        ch.set_frame(9);
        expect(ch.frame() == 9, "9 < length: no wrap");
        ch.set_frame(10);
        expect(ch.frame() == 0, "frame == length wraps to 0");
        ch.set_frame(12);
        expect(ch.frame() == 2, "12 wraps once to 2");
        // The DS subtracts the length ONCE only: 25 -> 15 (still >= length).
        ch.set_frame(25);
        expect(ch.frame() == 15, "single wrap only (25 -> 15), matching the DS");
    }

    // --- advance wraps at the end (loop) --------------------------------
    {
        AnimChannel ch(10);
        ch.set_frame(9);
        ch.advance_fx(kFx);  // 9 -> 10 -> wraps to 0
        expect(ch.frame() == 0, "advancing past the last frame loops to 0");
        ch.advance_fx(kFx / 2);  // fractional advance stays sub-frame
        expect(ch.frame() == 0 && ch.frame_fx() == kFx / 2,
               "fractional advance keeps sub-frame position");
    }

    // --- at_end (one-shot playback) -------------------------------------
    {
        AnimChannel ch(10);
        ch.set_frame(8);
        expect(!ch.at_end(), "frame 8 of 10 is not the end");
        ch.set_frame(9);
        expect(ch.at_end(), "frame 9 of 10 is the last frame");
    }

    // --- zero length is inert (no wrap, no div by zero) ------------------
    {
        AnimChannel ch;
        ch.set_frame(123);
        expect(ch.frame() == 123, "no length: frame set verbatim, no wrap");
        expect(!ch.at_end(), "no length: never at end");
    }

    if (failures == 0) {
        std::cout << "all anim-channel tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
