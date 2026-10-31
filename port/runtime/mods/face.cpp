// A KH2 face's texture animation (face.h), step for step as KH2's EBOOT plays one: kn::TEXAnmWork::set,
// ::play, ::setDefaultSequence and ::resetDefaultSequence. A step's length is vrand(min, max + 1) frames.

#include "face.h"

namespace khdays::face {

using Step = assets::ImportedTextureAnimation::Step;

Player::Player(const assets::ImportedTextureAnimation &animation, std::uint32_t seed)
    : animation_(&animation), random_(seed != 0 ? seed : 1U)
{
    reset_default();
}

int Player::script_of(int slot) const
{
    const int i = slot - animation_->first_slot;
    if (i < 0 || static_cast<std::size_t>(i) >= animation_->slot_script.size())
        return -1;
    const int s = animation_->slot_script[static_cast<std::size_t>(i)];
    return s >= 0 && static_cast<std::size_t>(s) < animation_->scripts.size() ? s : -1;
}

bool Player::knows(int slot) const
{
    return script_of(slot) >= 0;
}

const Step *Player::step() const
{
    if (script_ < 0)
        return nullptr;
    const auto &script = animation_->scripts[static_cast<std::size_t>(script_)];
    return pc_ >= 0 && static_cast<std::size_t>(pc_) < script.size() ? &script[static_cast<std::size_t>(pc_)] : nullptr;
}

void Player::draw_length()
{
    const Step *s = step();
    if (s == nullptr) {
        length_ = 1.0F;
        return;
    }
    random_ ^= random_ << 13U; // xorshift32
    random_ ^= random_ >> 17U;
    random_ ^= random_ << 5U;
    const int span = s->max_frames >= s->min_frames ? s->max_frames - s->min_frames + 1 : 1;
    length_ = static_cast<float>(s->min_frames + static_cast<int>(random_ % static_cast<std::uint32_t>(span)));
}

void Player::start(int script)
{
    if (script == script_)
        return;
    script_ = script;
    pc_ = 0;
    if (script_ >= 0) {
        started_ = true;
        elapsed_ = 0.0F;
        draw_length();
    }
}

void Player::set(int slot)
{
    const int s = script_of(slot);
    if (s < 0)
        return;
    held_ = true;
    start(s);
}

void Player::set_default(int slot)
{
    if (const int s = script_of(slot); s >= 0)
        default_script_ = s;
}

void Player::reset_default()
{
    const int s = animation_->default_script;
    default_script_ = s >= 0 && static_cast<std::size_t>(s) < animation_->scripts.size() ? s : -1;
}

void Player::play(float frames)
{
    if (held_)
        held_ = false;
    else
        start(default_script_);
    const Step *s = step();
    if (s == nullptr || s->kind == Step::Kind::stop)
        return;
    if (s->kind == Step::Kind::jump) // a script that starts with a jump
        pc_ += s->jump;
    if (started_) {
        started_ = false;
        return;
    }
    elapsed_ += frames;
    for (int guard = 0; elapsed_ >= length_ && guard < 1024; ++guard) { // (a loop of 0-frame steps: KH2's hangs)
        elapsed_ -= length_;
        ++pc_;
        s = step();
        if (s == nullptr)
            return;
        if (s->kind == Step::Kind::jump)
            pc_ += s->jump; // from the jump's own step
        else if (s->kind == Step::Kind::stop)
            return;
        draw_length();
    }
}

int Player::sprite() const
{
    const Step *s = step();
    if (s == nullptr || s->kind != Step::Kind::sprite)
        return -1;
    return s->sprite >= 0 && static_cast<std::size_t>(s->sprite) < animation_->sprites.size() ? s->sprite : -1;
}

} // namespace khdays::face
