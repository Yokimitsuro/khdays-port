// A replaced room's own animation (runtime/mods/room_animation.cpp): keys between and past the end, a loop
// from part way and none, a child under a moving parent, nodes listed children first and a parents' loop, a scroll
// kept within its texture, frames in turn.
#include "../runtime/mods/room_animation.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "room_animation: failed: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4F;
}

} // namespace

int main()
{
    namespace assets = khdays::assets;
    std::vector<assets::ImportedNode> nodes(3);
    nodes[0].name = "room";
    nodes[1].name = "piece";
    nodes[1].parent = 0;
    nodes[1].translation = {5, 0, 0};
    nodes[1].local[12] = 5;
    nodes[2].name = "piece_j0";
    nodes[2].parent = 1;

    // the bone: x 0 -> 10 over 1 s, then held or looped from 0.5 s; turned 90 degrees about y at 1 s
    assets::ImportedAnimation a;
    a.name = "piece";
    a.duration = 1.0F;
    assets::ImportedTrack move;
    move.node = 2;
    move.path = assets::ImportedTrack::Path::translation;
    move.times = {0.0F, 1.0F};
    move.values = {0, 0, 0, 10, 0, 0};
    assets::ImportedTrack turn;
    turn.node = 2;
    turn.path = assets::ImportedTrack::Path::rotation;
    turn.times = {0.0F, 1.0F};
    turn.values = {0, 0, 0, 1, 0, 0.7071068F, 0, 0.7071068F};
    a.tracks = {move, turn};

    auto world = khdays::room::node_worlds(nodes, {a}, 0.5);
    check(near(world[2][12], 10.0F), "between keys, under its parent (5 + 5)");
    check(near(world[1][12], 5.0F), "an unmoved node at rest");
    // half way to 90 degrees about y: x axis (cos 45, 0, -sin 45)
    check(near(world[2][0], 0.7071068F) && near(world[2][2], -0.7071068F), "a rotation half way");
    world = khdays::room::node_worlds(nodes, {a}, 3.0);
    check(near(world[2][12], 15.0F), "held at its end without a loop");
    a.loop = true;
    a.loop_from = 0.5F;
    world = khdays::room::node_worlds(nodes, {a}, 1.25);
    check(near(world[2][12], 12.5F), "looped from part way: 1.25 s is 0.75 s");
    world = khdays::room::node_worlds(nodes, {a}, 1.5);
    check(near(world[2][12], 10.0F), "looped from part way: 1.5 s is 0.5 s");
    world = khdays::room::node_worlds(nodes, {}, 0.0);
    check(near(world[2][12], 5.0F), "no animation: the rest");

    // a chain listed children first, as Blender writes a skeleton: foot (0) under leg (1) under root (2), each 1 up
    // and the root turned 90 degrees about y; and a node whose parents loop back, placed as a root
    std::vector<assets::ImportedNode> chain(5);
    chain[0].parent = 1;
    chain[1].parent = 2;
    chain[3].parent = 4;
    chain[4].parent = 3;
    khdays::room::Mat up{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1};
    khdays::room::Mat turned{0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1};
    const auto placed = khdays::room::chain_worlds(chain, {up, up, turned, up, up});
    check(near(placed[0][13], 2.0F) && near(placed[1][13], 1.0F), "children before their parents: under them");
    check(near(placed[0][0], 0.0F) && near(placed[0][2], -1.0F), "a child turned with its root");
    check(placed.size() == 5 && near(placed[4][13], 1.0F) && near(placed[3][13], 2.0F),
          "a loop: the last of the walk up a root, the other under it");

    const auto off = khdays::room::scroll_offset({0.5F, -1.0F}, 1000, 64.0F, 32.0F);
    check(near(off[0], 52.0F) && near(off[1], -8.0F), "a scroll within its texture (500 % 64, -1000 % 32)");

    const std::vector<std::pair<std::string, int>> frames{{"a", 10}, {"b", 20}, {"c", 5}};
    check(khdays::room::frame_at(frames, true, 0, 0) == 0 && khdays::room::frame_at(frames, true, 0, 9) == 0,
          "the first picture for its length");
    check(khdays::room::frame_at(frames, true, 0, 10) == 1 && khdays::room::frame_at(frames, true, 0, 34) == 2,
          "then the others in turn");
    check(khdays::room::frame_at(frames, true, 0, 35) == 0, "a loop starts again");
    check(khdays::room::frame_at(frames, true, 1, 35) == 1 && khdays::room::frame_at(frames, true, 1, 55) == 2,
          "a loop from the second");
    check(khdays::room::frame_at(frames, false, 0, 500) == 2, "no loop: the last held");
    check(khdays::room::frame_at({}, true, 0, 5) == 0, "no frames");

    if (failures == 0)
        std::printf("room_animation: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
