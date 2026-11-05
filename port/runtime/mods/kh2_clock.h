// How a game animation's frame moved between two draws, for the clock of the KH2 motion standing in for it
// (models.cpp play_kh2). Pure: the frames are the game's (BCA, 30 a second).
#ifndef KHDAYS_KH2_CLOCK_H
#define KHDAYS_KH2_CLOCK_H

namespace khdays::kh2_clock {

enum class FrameMove { on, loop, restart };

// `before` and `now`: the frame at the last draw and this one; `loops`: the KH2 motion has a loop of its own. A
// set-back of a looping motion is counted on (it loops itself, smoothly: an idle's loop, the fall set back to 16, a
// run wrapping after a cross-fade held it on its last frame); one that does not loop starts over, as the game's
// animation did (Ov022_TurnAwayFromDrift plays a damage animation again from frame 0; counted on, it stood at its end)
inline FrameMove frame_move(float before, float now, bool loops)
{
    if (now >= before)
        return FrameMove::on;
    return loops ? FrameMove::loop : FrameMove::restart;
}

} // namespace khdays::kh2_clock

#endif
