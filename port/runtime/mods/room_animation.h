// A replaced room's own animation (a KH2 room's moving pieces and textures, the glTF's): each node's
// matrix at a time, a texture's scroll and which of its pictures shows at a game frame. Pure: the
// clock is the caller's (models.cpp counts the game's frames, standing still while it is paused).
#ifndef KHDAYS_ROOM_ANIMATION_H
#define KHDAYS_ROOM_ANIMATION_H

#include "khdays/assets/model_import.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace khdays::room {

using Mat = std::array<float, 16>; // column-major, for column vectors

// Every node's world matrix at `seconds` into each animation: a looping one
// starts again at its loop_from after its end, one that does not holds its end.
std::vector<Mat> node_worlds(const std::vector<assets::ImportedNode> &nodes,
                             const std::vector<assets::ImportedAnimation> &animations, double seconds);

// Every node's world matrix from each one's own (`local`, by node), under its parent's whatever order the file lists
// the nodes in (Blender writes children before their parents); a node whose parents loop back is placed as a root.
std::vector<Mat> chain_worlds(const std::vector<assets::ImportedNode> &nodes, const std::vector<Mat> &local);

// A texture's offset (texels) after `frames` game frames of `scroll` texels each, kept within the
// texture's size (it repeats).
std::array<float, 2> scroll_offset(const std::array<float, 2> &scroll, std::uint64_t frames, float width, float height);

// Which of `frames` (texture, game frames each) shows after `frame` game frames: shown in order, then
// from `loop_from` again when looping, else the last held. 0 when there are none.
std::size_t frame_at(const std::vector<std::pair<std::string, int>> &frames, bool loop, std::size_t loop_from,
                     std::uint64_t frame);

} // namespace khdays::room

#endif
