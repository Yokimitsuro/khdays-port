/* Exercise the unchanged Days duration and divider routines, independently of rendering. */
#include "../../external/khdays-decomp/src/overlays/field/ov022_battle/func_ov022_02094678.c"
#include "../../external/khdays-decomp/src/overlays/scenes/ov002_field/Ov002_UpdateScrollThumb.c"

#include <stdio.h>
#include <stdlib.h>

int gObjSystem;
Ov002SceneContext *data_ov002_0207f618;

int FX_Div(int numerator, int denominator)
{
    return (int)(((long long)numerator * 4096 + denominator / 2) / denominator);
}

static void expect(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

int main(void)
{
    Ov002SceneContext context = {0};
    data_ov002_0207f618 = &context;
    const int budget = func_ov022_02094678(60, 200);
    expect(budget == 430050, "normal duration differs from the original 20.12 calculation");
    expect(func_ov022_02094678(120, 400) == budget && func_ov022_02094678(240, 800) == budget,
           "absolute maximum HP changed duration at an equal starting fraction");
    expect(func_ov022_02094678(31, 186) == func_ov022_02094678(62, 372),
           "186/372 HP gauges have different durations at an equal starting fraction");
    context.wScrollTop = context.wRowHeight = 23;
    for (int phase = 0; phase < 2; ++phase) {
        context.nMode17c = phase;
        const int total = phase ? budget * 2 : budget;
        Ov002_UpdateScrollThumb(0, total, budget);
        expect(context.wScrollThumb == 23, "divider did not start at shown HP");
        Ov002_UpdateScrollThumb(budget / 2, total, budget);
        expect(context.wScrollThumb == 50, "divider lost the original halfway position");
        Ov002_UpdateScrollThumb(budget, total, budget);
        expect(context.wScrollThumb >= 76, "divider did not reach the far end on time");
        if (phase) {
            Ov002_UpdateScrollThumb(budget + budget / 2, total, budget);
            expect(context.wScrollThumb == 50, "double limit did not return halfway");
            Ov002_UpdateScrollThumb(total, total, budget);
            expect(context.wScrollThumb == 23, "double limit did not return to current shown HP");
        }
    }
    const unsigned short latched = context.wScrollThumb;
    expect(latched == 23, "native return endpoint changed");
    gObjSystem = 1;
    expect(func_ov022_02094678(60, 200) == 286700, "original alternate frame-mode budget changed");
    printf("Days original divider timing: %.6f s outward at 30 Hz / 30%% starting HP; HP-independent fraction\n",
           (double)budget / (4096 * 30));
    return 0;
}
