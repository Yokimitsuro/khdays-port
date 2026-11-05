// A KH2 face's texture animation (runtime/mods/face.cpp), as kn::TEXAnmWork plays one: the default script with
// its first step shown a frame before it moves, a slot held only while set, a jump back from its own step, a
// stop showing the texture's own picture, defaults set and put back, random lengths within their bounds.
#include "../runtime/mods/face.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "face: failed: %s\n", what);
        ++failures;
    }
}

using Animation = khdays::assets::ImportedTextureAnimation;
using Kind = Animation::Step::Kind;

Animation::Step sprite(int n, int min, int max)
{
    return {Kind::sprite, 0, min, max, n};
}

Animation::Step jump(int by)
{
    return {Kind::jump, by, 0, 0, 0};
}

// slots 10-14: 10 closed (sprite 10, held), 12 a blink (4 for 2, 5 for 6, 0 for 30, back), 13 a stop after
// sprite 7, 14 random (sprite 1 for 2-4, sprite 2 for 2-4, back); 11 none
Animation eyes()
{
    Animation a;
    a.texture = "tex1";
    a.width = a.height = 1;
    a.sprites.assign(11, std::vector<std::uint8_t>(4, 0));
    a.scripts = {
        {sprite(4, 2, 2), sprite(5, 6, 6), sprite(0, 30, 30), jump(-3)},
        {sprite(10, 60, 60), jump(-1)},
        {sprite(7, 3, 3), {Kind::stop, 0, 0, 0, 0}},
        {sprite(1, 2, 4), sprite(2, 2, 4), jump(-2)},
    };
    a.first_slot = 10;
    a.slot_script = {1, -1, 0, 2, 3};
    a.default_script = 0;
    return a;
}

} // namespace

int main()
{
    const Animation a = eyes();
    {
        khdays::face::Player p(a, 1);
        p.play(1.0F);
        check(p.sprite() == 4, "the default's first step");
        p.play(1.0F);
        check(p.sprite() == 4, "shown its 2 frames");
        p.play(1.0F);
        check(p.sprite() == 5, "then the next");
        p.play(6.0F);
        check(p.sprite() == 0, "sprite 5 for 6 frames");
        p.play(30.0F);
        check(p.sprite() == 4, "the jump back from its own step");
    }
    {
        khdays::face::Player p(a, 1);
        check(p.knows(10) && p.knows(12) && !p.knows(11) && !p.knows(9) && !p.knows(15), "the slots it has");
        p.set(10);
        p.play(1.0F);
        check(p.sprite() == 10, "a held slot");
        p.set(10);
        p.play(100.0F);
        check(p.sprite() == 10, "held on, looping");
        p.set(11);
        p.play(1.0F);
        check(p.sprite() == 4, "a slot it lacks is not held: the default starts again");
    }
    {
        khdays::face::Player p(a, 1);
        p.set(13);
        p.play(1.0F);
        p.set(13);
        p.play(3.0F);
        check(p.sprite() == -1, "a stop: the texture's own picture");
        p.set(13);
        p.play(10.0F);
        check(p.sprite() == -1, "and stays");
    }
    {
        khdays::face::Player p(a, 1);
        p.set_default(10);
        p.play(1.0F);
        check(p.sprite() == 10, "a default set (trigger 4)");
        p.reset_default();
        p.play(1.0F);
        check(p.sprite() == 4, "the file's back (trigger 5)");
    }
    {
        khdays::face::Player p(a, 7);
        p.set(14);
        p.play(1.0F);
        int last = p.sprite(), run = 0, shortest = 99, longest = 0;
        for (int f = 0; f < 400; ++f) {
            p.set(14);
            p.play(1.0F);
            ++run;
            if (p.sprite() != last) {
                shortest = run < shortest ? run : shortest;
                longest = run > longest ? run : longest;
                last = p.sprite();
                run = 0;
            }
        }
        check(shortest >= 2 && longest <= 4 && shortest < longest, "lengths at random within 2-4 frames");
    }
    if (failures == 0)
        std::printf("face: ok\n");
    return failures == 0 ? 0 : 1;
}
