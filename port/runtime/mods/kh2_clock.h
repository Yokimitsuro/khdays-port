// How a game animation's frame moved between two draws, for the clock of the KH2 motion standing in for it
// (models.cpp play_kh2). Pure: the frames are the game's (BCA, 30 a second).
#ifndef KHDAYS_KH2_CLOCK_H
#define KHDAYS_KH2_CLOCK_H

namespace khdays::kh2_clock {

enum class FrameMove { on, loop, restart };

// `before` and `now`: the frame at the last draw and this one; `frames`: the animation's length; `step`: its last
// forward step; `moving`: it was running up to the last draw. A set-back is a loop when it wraps a running frame
// from within two steps of its end (an idle's loop, the fall set back to 16); from anywhere else, or from a frame
// the game held, the game rewound it (Ov022_TurnAwayFromDrift plays a damage animation again from frame 0), so
// the motion starts over. An unknown length (0) counts as a loop.
inline FrameMove frame_move(float before, float now, float frames, float step, bool moving)
{
    if (now >= before)
        return FrameMove::on;
    if (frames <= 0.0F || (moving && before >= frames - 2.0F * step))
        return FrameMove::loop;
    return FrameMove::restart;
}

} // namespace khdays::kh2_clock

#endif
