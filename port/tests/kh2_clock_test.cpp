// A KH2 motion's clock against its game animation's frame (runtime/mods/kh2_clock.h): a running frame, a loop
// wrapped at the end, the fall set back to 16, a damage animation rewound mid-way or from a held last frame, a
// held frame, and an animation of unknown length.
#include "../runtime/mods/kh2_clock.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "kh2_clock: failed: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    using khdays::kh2_clock::frame_move;
    using khdays::kh2_clock::FrameMove;
    // the battle idle (39 frames) running at half a frame a draw
    check(frame_move(10.0F, 10.5F, 39.0F, 0.5F, true) == FrameMove::on, "a running frame moves on");
    check(frame_move(10.0F, 10.0F, 39.0F, 0.5F, true) == FrameMove::on, "a frame standing for a draw moves on");
    check(frame_move(38.75F, 0.25F, 39.0F, 0.5F, true) == FrameMove::loop, "a wrap at the end is a loop");
    // the jump's air (27 frames): falling, the game sets it back to 16 at its end
    check(frame_move(26.5F, 16.0F, 27.0F, 0.5F, true) == FrameMove::loop, "the fall's set-back is a loop");
    // a damage animation (30 frames) played again: Ov022_TurnAwayFromDrift rewinds it to 0
    check(frame_move(14.0F, 0.0F, 30.0F, 0.5F, true) == FrameMove::restart, "a rewind mid-way restarts");
    check(frame_move(29.75F, 0.0F, 30.0F, 0.5F, false) == FrameMove::restart,
          "a rewind from a held last frame restarts");
    check(frame_move(29.75F, 0.0F, 30.0F, 0.5F, true) == FrameMove::loop, "a running wrap at the end stays a loop");
    check(frame_move(20.0F, 3.0F, 0.0F, 0.5F, false) == FrameMove::loop, "an unknown length counts as a loop");
    if (failures == 0)
        std::printf("kh2_clock: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
