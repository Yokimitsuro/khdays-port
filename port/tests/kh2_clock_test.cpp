// A KH2 motion's clock against its game animation's frame (runtime/mods/kh2_clock.h): a running or standing
// frame moves on; set back, a looping motion counts on (an idle's wrap, the fall set back to 16, a run wrapping after
// a cross-fade held it on its last frame) and one that does not loop starts over (a damage animation rewound).
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
    check(frame_move(10.0F, 10.5F, true) == FrameMove::on, "a running frame moves on");
    check(frame_move(10.0F, 10.0F, false) == FrameMove::on, "a frame standing for a draw moves on");
    check(frame_move(38.75F, 0.25F, true) == FrameMove::loop, "the idle's wrap is counted on");
    check(frame_move(26.5F, 16.0F, true) == FrameMove::loop, "the fall's set-back is counted on");
    check(frame_move(18.0F, 0.5F, true) == FrameMove::loop, "a run held on its last frame, then wrapped, counts on");
    check(frame_move(14.0F, 0.0F, false) == FrameMove::restart, "a damage animation rewound mid-way starts over");
    check(frame_move(29.75F, 0.0F, false) == FrameMove::restart, "one rewound from its held last frame starts over");
    if (failures == 0)
        std::printf("kh2_clock: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
