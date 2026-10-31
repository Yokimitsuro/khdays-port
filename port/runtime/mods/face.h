// A KH2 face's texture animation as KH2 plays it (kn::TEXAnmWork): a block of sprites (the eyes, the mouth)
// whose scripts are picked by slot id, the default script unless one is asked for this frame. Pure: the
// clock (game frames) and the picture are the caller's (models.cpp patches the texture; docs/RENDERING.md).
#ifndef KHDAYS_FACE_H
#define KHDAYS_FACE_H

#include "khdays/assets/model_import.h"

#include <cstdint>

namespace khdays::face {

class Player {
public:
    // `seed`: the step lengths' randomness (two faces blink apart)
    Player(const assets::ImportedTextureAnimation &animation, std::uint32_t seed);

    // TEXAnmWork::set: `slot`'s script, held for this frame's play (a new script starts from its first step)
    void set(int slot);
    // setDefaultSequence (ANMATR trigger 4) and resetDefaultSequence (trigger 5): what plays when nothing is held
    void set_default(int slot);
    void reset_default();
    bool knows(int slot) const; // the block has a script for `slot`

    // TEXAnmWork::play: `frames` game frames on
    void play(float frames);

    // the sprite shown, -1 the texture's own picture (a step that is not a sprite: KH2 sends none)
    int sprite() const;

private:
    int script_of(int slot) const;
    void start(int script);
    void draw_length();
    const assets::ImportedTextureAnimation::Step *step() const;

    const assets::ImportedTextureAnimation *animation_;
    std::uint32_t random_;
    int default_script_ = -1;
    int script_ = -1;
    int pc_ = 0;
    float length_ = 0.0F, elapsed_ = 0.0F;
    bool held_ = false;    // set this frame (flag 4)
    bool started_ = false; // started since the last play: shown a frame before it moves (flag 1)
};

} // namespace khdays::face

#endif
