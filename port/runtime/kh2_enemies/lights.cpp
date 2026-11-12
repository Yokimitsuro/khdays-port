// KINGDOM HEARTS II's lights a script makes (lights.h).
#include "lights.h"

#include <algorithm>

namespace khdays::kh2_enemies {

namespace {

// FUN_1403b7810: a value set going from `from` to `to` over `ticks` (2 or fewer: one tick)
void run(float &value, float &step, float from, float to, float ticks)
{
    if (ticks <= 2.0F)
        ticks = 1.0F;
    value = from;
    step = (to - from) / ticks;
}

// One light's part of a colour channel (FUN_1403f7040): the channel times 128 + (its colour - 128) x its strength
// (truncated), over 128, kept a byte
std::uint8_t tinted(std::uint8_t channel, std::uint8_t colour, float strength)
{
    const auto part = std::uint8_t(int((float(int(colour) - 128)) * strength) + 128);
    return std::uint8_t((unsigned(channel) * unsigned(part)) >> 7);
}

} // namespace

Lights::Light *Lights::find(std::uint32_t handle)
{
    const auto it = std::find_if(lights.begin(), lights.end(), [&](const Light &l) { return l.handle == handle; });
    return it != lights.end() ? &*it : nullptr;
}

std::uint32_t Lights::create(int kind)
{
    Light l;
    l.handle = next++;
    if (next == 0)
        next = 1;
    l.kind = kind;
    lights.push_back(l);
    return l.handle;
}

void Lights::set_flag(std::uint32_t handle, int bit)
{
    Light *l = find(handle);
    if (l == nullptr || bit < 0)
        return;
    const auto word = std::size_t(unsigned(bit) >> 5);
    if (l->flags.size() <= word)
        l->flags.resize(word + 1, 0);
    l->flags[word] |= 1u << (unsigned(bit) & 31u);
}

void Lights::set_colour(std::uint32_t handle, int r, int g, int b, float ticks)
{
    Light *l = find(handle);
    if (l == nullptr)
        return;
    l->colour = {std::uint8_t(r), std::uint8_t(g), std::uint8_t(b)};
    run(l->strength, l->step, 0.0F, 1.0F, ticks);
}

void Lights::fade_out(std::uint32_t handle, float ticks)
{
    if (Light *l = find(handle))
        run(l->strength, l->step, l->strength, 0.0F, ticks);
}

void Lights::tick()
{
    Colours c;
    for (auto it = lights.begin(); it != lights.end();) {
        Light &l = *it;
        // FUN_1403b77c0: stepped, held at 0 and 1 (its step ended there)
        if (l.step != 0.0F) {
            l.strength += l.step;
            if (l.strength <= 0.0F)
                l.strength = 0.0F, l.step = 0.0F;
            else if (l.strength >= 1.0F)
                l.strength = 1.0F, l.step = 0.0F;
        }
        if (l.strength == 0.0F) {
            it = lights.erase(it); // FUN_1403f7580
            continue;
        }
        const bool room = l.kind == 0 && !l.flags.empty() && (l.flags[0] & 2u) != 0;
        if (l.kind == 1 || room) {
            auto &channels = l.kind == 1 ? c.objects : c.room;
            for (std::size_t i = 0; i < 3; ++i)
                channels[i] = tinted(channels[i], l.colour[i], l.strength);
        }
        ++it;
    }
    c.any = c.room != std::array<std::uint8_t, 3>{128, 128, 128} ||
            c.objects != std::array<std::uint8_t, 3>{128, 128, 128};
    now = c;
}

void Lights::clear()
{
    lights.clear();
    now = Colours{};
}

} // namespace khdays::kh2_enemies
