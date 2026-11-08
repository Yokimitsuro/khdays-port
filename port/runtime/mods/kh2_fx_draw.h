// KINGDOM HEARTS II's effects (PAX, assets/kh2_effect.h) drawn in Days's 3D lists: their pictures' pages as the
// port's textures (an HD picture of a page where the mod has one), and what an effect draws a tick sent as the
// port's own polygons. Shared by KH2's Dark Corridors (kh2_corridors.cpp) and KH2's enemies
// (runtime/kh2_enemies/shell.cpp). Main thread.
#pragma once

#include "khdays/assets/kh2_effect.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace khdays::kh2_fx {

namespace kh2 = khdays::assets::kh2;

// The HD picture of a page (a distinct tbp, its first picture's palette): <stem>/-<n>.dds, mods' files
struct HdPage final {
    std::uint16_t tbp = 0, cbp = 0;
    std::string relative;
};

// An effect file's pages: its blocks' pictures (and the resident ones', effect/texcommon.dpd) and their HD pictures.
class Pages final {
public:
    explicit Pages(std::string name) : name_(std::move(name)) {}
    // One file's blocks' pictures; their HD pictures (one a page of the file's, in ascending order) under the mods'
    // `hd_stem`, "" none
    void add(const std::vector<const kh2::EffectBlock *> &blocks, const std::string &hd_stem);
    // A draw's picture: its TEX0's view of the pages as a texture of the port's, the HD picture of its page when
    // `hd` and a mod has one that covers the same view with the same palette; -1 none.
    int page(std::uint64_t tex0, bool hd);
    std::size_t pictures() const
    {
        return pictures_.size();
    }
    bool tracing = false; // each page made told in the session log

private:
    std::string name_;
    std::vector<const kh2::EffectPicture *> pictures_; // kept by the blocks, which outlive this
    std::vector<HdPage> hd_;
    std::map<std::pair<std::uint64_t, bool>, int> pages_;            // (TEX0, HD) -> the texture added, -1 none
    std::vector<std::unique_ptr<std::vector<std::uint8_t>>> texels_; // theirs: textures.h keeps them, never copied
};

// What an effect drew (its clip space divided by `units`, as KH2's camera for it makes it: multiplied back) as the
// port's polygons, each draw's page from `pages`.
void submit(const std::vector<kh2::EffectDraw> &draws, float units, Pages &pages);

} // namespace khdays::kh2_fx
