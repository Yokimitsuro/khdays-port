// A KH2 motion played out after its game animation (motions.txt `finish`): when the game goes on to one of its
// idles before the motion has ended, the rest of it plays, then each `finish` slot, then the idle's motion fades in;
// the game leaving its idles drops it. Pure: the clock is the game's animation frames (models.cpp play_kh2).
#ifndef KHDAYS_KH2_TAIL_H
#define KHDAYS_KH2_TAIL_H

#include <cmath>
#include <vector>

namespace khdays::kh2_tail {

struct Piece {
    int slot = 0;
    float start = 0.0F;  // its first tick
    float length = 0.0F; // its last (played once to it, whatever loop it has)
};

class Tail final {
public:
    // Its pieces (the motion's rest from where it stood, then each `finish` slot) and its fades, in game frames
    // (0: cuts). Pieces already over are left out; none left: nothing plays.
    void start(std::vector<Piece> pieces, float blend)
    {
        pieces_.clear();
        for (const Piece &p : pieces)
            if (p.start < p.length)
                pieces_.push_back(p);
        blend_ = blend;
        at_ = 0;
        ticks_ = pieces_.empty() ? 0.0F : pieces_.front().start;
        weight_ = pieces_.empty() ? 0.0F : 1.0F;
        faded_ = -1;
        faded_weight_ = 0.0F;
        ending_ = false;
    }

    // One draw: `frames` the game's animation moved on (BCA frames, 2 ticks each; 0 paused); `idle` the game plays
    // only its idles. Out of them (or the last piece over), it fades out standing where it is
    void step(float frames, bool idle)
    {
        if (weight_ <= 0.0F)
            return;
        const float fade = blend_ > 0.0F ? frames / blend_ : 1.0F;
        if (faded_ >= 0) {
            faded_weight_ -= fade;
            if (faded_weight_ <= 0.0F)
                faded_ = -1;
        }
        if (!idle)
            ending_ = true;
        if (ending_) {
            weight_ = blend_ > 0.0F ? std::fmax(0.0F, weight_ - fade) : 0.0F;
            return;
        }
        ticks_ += frames * 2.0F;
        while (ticks_ >= pieces_[at_].length) {
            if (at_ + 1 >= pieces_.size()) { // the last one at its end: held while the idle's motion fades in
                ticks_ = pieces_[at_].length;
                ending_ = true;
                if (blend_ <= 0.0F)
                    weight_ = 0.0F;
                return;
            }
            const float over = ticks_ - pieces_[at_].length;
            if (blend_ > 0.0F) { // the piece before, held at its end, fading out under the next
                faded_ = static_cast<int>(at_);
                faded_ticks_ = pieces_[at_].length;
                faded_weight_ = 1.0F;
            }
            ++at_;
            ticks_ = pieces_[at_].start + over;
        }
    }

    // gone at once: the game cut into another animation (a hit, the death), where a fade out on its frames stood
    // the tail over the new pose (the death's slow motion held half of it for seconds)
    void stop()
    {
        weight_ = 0.0F;
        faded_ = -1;
        faded_weight_ = 0.0F;
    }

    bool active() const
    {
        return weight_ > 0.0F;
    }
    bool ending() const
    {
        return ending_;
    }
    // its share of the pose: 1 while it plays, down to 0 over its fade once it ended or the game left its idles
    float weight() const
    {
        return weight_;
    }
    int slot() const
    {
        return pieces_.empty() ? -1 : pieces_[at_].slot;
    }
    std::size_t piece() const
    {
        return at_;
    }
    float ticks() const
    {
        return ticks_;
    }
    // the piece before, at its end, fading out under this one (-1 none), and its share of the tail's pose
    int faded_slot() const
    {
        return faded_ >= 0 ? pieces_[static_cast<std::size_t>(faded_)].slot : -1;
    }
    float faded_ticks() const
    {
        return faded_ticks_;
    }
    float faded_weight() const
    {
        return faded_ >= 0 ? faded_weight_ : 0.0F;
    }

private:
    std::vector<Piece> pieces_;
    float blend_ = 0.0F;
    std::size_t at_ = 0;
    float ticks_ = 0.0F;
    float weight_ = 0.0F;
    int faded_ = -1;
    float faded_ticks_ = 0.0F, faded_weight_ = 0.0F;
    bool ending_ = false;
};

} // namespace khdays::kh2_tail

#endif
