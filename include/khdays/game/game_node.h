#pragma once

#include <cstdint>

// Building blocks for porting ov000's render-node subsystem (see
// docs/RENDER_NODES.md). The DS front-end renders a tree of nodes, each with an
// animated transform and object cells, through the geometry engine. This header
// holds the neutral, engine-independent pieces; the renderer integration and the
// node tree land in later slices.
namespace khdays::game {

// One animation channel of a node. The DS stores the current frame in 20.12
// fixed point and a length in whole frames, and wraps a set frame once against
// the length (khdays-decomp `Anim_SetFrameWrapped` / `func_01fff774`): it writes
// the frame, then, if it is at or past `length` frames, subtracts `length` once.
// Multiple channels per node drive different tracks (the title uses 0 and 2).
class AnimChannel {
  public:
    static constexpr int kFxOne = 1 << 12;  // 1.0 in 20.12 fixed point

    AnimChannel() = default;
    explicit AnimChannel(int length_frames) : length_(length_frames) {}

    void set_length(int length_frames) { length_ = length_frames; }
    [[nodiscard]] int length() const { return length_; }

    // Set the current frame (20.12 fixed). Wraps once, exactly as the DS does:
    // a frame at or beyond the length has the length subtracted a single time.
    void set_frame_fx(int frame_fx) {
        frame_fx_ = frame_fx;
        if (length_ > 0 && frame_fx >= length_ * kFxOne) {
            frame_fx_ = frame_fx - length_ * kFxOne;
        }
    }

    // Set from a whole-frame index (convenience).
    void set_frame(int frame) { set_frame_fx(frame * kFxOne); }

    // Advance by a 20.12 delta (e.g. kFxOne per game frame at 1x speed).
    void advance_fx(int delta_fx) { set_frame_fx(frame_fx_ + delta_fx); }

    [[nodiscard]] int frame_fx() const { return frame_fx_; }
    // The integer frame index the channel currently sits on.
    [[nodiscard]] int frame() const { return frame_fx_ >> 12; }
    // True once the channel has reached its last frame (for one-shot playback).
    [[nodiscard]] bool at_end() const {
        return length_ > 0 && frame_fx_ >= (length_ - 1) * kFxOne;
    }

  private:
    int frame_fx_ = 0;  // 20.12 fixed-point current frame
    int length_ = 0;    // length in whole frames
};

}  // namespace khdays::game
