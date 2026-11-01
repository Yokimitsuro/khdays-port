// When a KH2 model's motions change with the game's state (models.cpp play_kh2): a landing straight out of the
// damage in the air (motions.txt `air ... land`), and a fade into the game's idle (`blend`). Pure.
#ifndef KHDAYS_KH2_MOVES_H
#define KHDAYS_KH2_MOVES_H

#include <cmath>
#include <cstdint>

namespace khdays::kh2_moves {

// The game's landing state entered at vblank `entered`, `window` vblanks or less after it last left its damage in
// the air (`left_air`, 0 never): Days lands it through its fall state for a frame or two (Ov022_StepInterruptible
// ends in state 2, which lands at once); a fall it recovered into lasts longer, and lands as a jump does
inline bool landed_out_of_air(bool landing, std::uint32_t entered, std::uint32_t left_air, std::uint32_t window)
{
    return landing && left_air != 0 && entered >= left_air && entered - left_air <= window;
}

// A fade from the pose last shown into the game's, when the game goes on to one of its idles from an animation with
// an idle fade: started on the first draw an idle plays (none the draw before) when the animation the game played
// most then was no idle and has a fade; it ends when the game plays anything other than its idles and that
// animation fading out (or a `finish` tail takes over). Its clock is the game's animation frames.
class IdleFade final {
public:
    // this draw: an idle plays now (`idle_now`), did at the last draw (`idle_before`); the last draw's main
    // animation was an idle (`main_idle`) and its idle fade (`frames`, 0 none); a tail plays (`tail`)
    bool starts(bool idle_now, bool idle_before, bool main_idle, float frames, bool tail) const
    {
        return weight_ <= 0.0F && idle_now && !idle_before && !main_idle && frames > 0.0F && !tail;
    }
    void start(float frames)
    {
        frames_ = frames;
        weight_ = 1.0F;
    }
    // one draw after its start: `game_frames` the game's animation moved on (0 paused), `other` it plays something
    // else than its idles and the faded animation fading out
    void step(float game_frames, bool other)
    {
        if (weight_ <= 0.0F)
            return;
        weight_ = other ? 0.0F : std::fmax(0.0F, weight_ - (frames_ > 0.0F ? game_frames / frames_ : 1.0F));
    }
    void stop()
    {
        weight_ = 0.0F;
    }
    float weight() const
    {
        return weight_;
    }

private:
    float frames_ = 0.0F, weight_ = 0.0F;
};

} // namespace khdays::kh2_moves

#endif
