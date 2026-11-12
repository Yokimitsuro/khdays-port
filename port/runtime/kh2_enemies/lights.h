// KINGDOM HEARTS II's lights a script makes (YS::LIGHT: 1:181 trap_light_create, 1:182 trap_light_set_flag, 1:183
// trap_light_set_color, 1:184 trap_light_fadeout), as KH2FM.exe keeps them (our reading, docs/KH2_ENEMIES.md "Lights"):
// a list of records (FUN_1403f6f80 adds one at its end, FUN_1403f7580 takes one out), each a kind, flags, a colour and
// a strength that runs to a value over ticks (FUN_1403b7810, stepped by FUN_1403b77c0). Every frame (FUN_1403f7040)
// each light's strength steps, a light at no strength leaves the list, and the colours are composed: kind 1 lights
// tint the objects (the area's object light, FUN_14016f920), kind 0 lights with flag 1 the room (the map's colour and
// its fog, FUN_1401626f0) -- a boss darkening the field for an attack (Xaldin, Demyx, Saix, the Lingering Will).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace khdays::kh2_enemies {

class Lights final {
public:
    // The colours composed, KH2's bytes: 128 as the field is, 0 black, up to 255 (twice as bright)
    struct Colours final {
        std::array<std::uint8_t, 3> room{128, 128, 128}, objects{128, 128, 128};
        bool any = false; // a light changes one of them
    };

    // A light of `kind` (0 the room's, 1 the objects'): neutral (128 each) at full strength; its handle, never 0
    std::uint32_t create(int kind);
    // A flag of the light set (bit 1 makes a kind 0 light tint the room)
    void set_flag(std::uint32_t handle, int bit);
    // Its colour, its strength run from nothing to full over `ticks` (KH2's: 2 or fewer one)
    void set_colour(std::uint32_t handle, int r, int g, int b, float ticks);
    // Its strength run from what it has to nothing over `ticks`: it leaves the list then
    void fade_out(std::uint32_t handle, float ticks);
    // A KH2 tick (a 60th of a second): each light's strength stepped (one at none leaves), the colours composed
    void tick();
    const Colours &colours() const
    {
        return now;
    }
    std::size_t size() const
    {
        return lights.size();
    }
    void clear();

private:
    struct Light final {
        std::uint32_t handle = 0;
        int kind = 0;
        std::vector<std::uint32_t> flags;
        std::array<std::uint8_t, 3> colour{128, 128, 128};
        float strength = 1.0F, step = 0.0F;
    };
    Light *find(std::uint32_t handle);
    std::vector<Light> lights; // in KH2's order: the oldest first
    std::uint32_t next = 1;
    Colours now;
};

} // namespace khdays::kh2_enemies
