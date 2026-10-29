// Portrait settings and playback, independent of Days and the graphics device.
#pragma once
#include "khdays/assets/kh2_sequence.h"
#include <array>
#include <filesystem>
#include <string>

namespace khdays::ui::kh2 {
inline constexpr std::array<const char *, 21> portrait_names{
    "Xemnas", "Xigbar", "Xaldin", "Vexen",    "Lexaeus", "Zexion",    "Saix",
    "Axel",   "Demyx",  "Luxord", "Marluxia", "Larxene", "Roxas",     "Xion",
    "Mickey", "Sora",   "Riku",   "Donald",   "Goofy",   "Xion_Hood", "Roxas_Hood"};

struct PortraitConfig {
    bool enabled = false, play_intro = false;
    std::string archive, texture, image_entry = "face", sequence_entry = "face";
    float offset_x = 0, offset_y = 0, scale = 0.65f, source_origin_x = 0, source_origin_y = 0;
    float frames_per_tick = 1, canvas_padding = 4;
    int normal = 3, damage_small = 1, damage_medium = 1, damage_large = 1, alert = 0, dead = 2;
    int intro = -1, animation_override = -1;
};
using PortraitConfigs = std::array<std::array<PortraitConfig, 2>, 21>; // sheet icon, player/party
PortraitConfigs load_portrait_config(const std::filesystem::path &path);
// Ov002_LoadPanelSlots' destination character offsets and row layout (4bpp).
int portrait_tile_slot(unsigned byte_offset);

class PortraitPlayer {
public:
    void update(const assets::kh2::SequenceData &data, const PortraitConfig &config, int hp, int maximum, bool alert,
                float ticks, bool reset);
    assets::kh2::SequenceFrame evaluate(const assets::kh2::SequenceData &data,
                                        const assets::kh2::SequencePose &pose = {}) const;
    std::size_t clip() const
    {
        return player.clip();
    }
    float frame() const
    {
        return player.frame();
    }

private:
    assets::kh2::SequencePlayer player;
    int previous_hp = -1;
    bool temporary = false;
    float temporary_frames = 0;
};
} // namespace khdays::ui::kh2
