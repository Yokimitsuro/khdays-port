// The KH2 prizes mod's pieces in a mod (tools/mods/kh2_prizes.py; docs/KH2_SEQUENCES.md, "Field prizes"): each
// prize picture's effect or model and each chest's model, what a modder changes of them (a prize model's size and
// turn, a chest's scale), a frame of an effect drawn as the port draws it, and the mod made from the player's own
// 1.5+2.5 copy as the installer makes it.
#pragma once

#include "core/mods/settings.h"

#include "khdays/assets/texture.h"

#include "ui/prize_effects_model.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace studio {

struct PrizePiece {
    enum class Kind { effect, model, chest };
    Kind kind = Kind::effect;
    std::string name;                 // the prize picture (prize_01) or the chest model (tt_box_b)
    std::filesystem::path file;       // its effect (.txt) or its model (.gltf, .glb, .fbx)
    std::filesystem::path settings;   // a prize model's <picture>.txt, a chest's <model>.bones.txt
    std::string where;                // a chest's game file (mo/mo#102)
    float size = 1.0F, turn = 90.0F;  // a prize model's
    float scale = 1.0F;               // a chest's
    std::vector<std::string> other;   // its settings file's other lines, kept as they are
};

// The pieces of the mod in `mod_dir`: effects, prize models (graphics/mo/prize.pak.z/fx, mdl) and chests (a model
// named <world>_box_b under graphics/mo/mo#<entry>/mdl), in that order, each by name.
std::vector<PrizePiece> prize_pieces(const std::filesystem::path& mod_dir);

// A prize model's size and turn, or a chest's scale, written into its settings file (its other lines kept); false
// with the reason.
bool save_prize_piece(const PrizePiece& piece, std::string* why);

// An effect read as the port reads it, its pages stacked into one atlas.
struct PrizeEffect {
    khdays::ui::prizes::Effect effect;
    khdays::ui::prizes::Atlas layout;
    khdays::ui::prizes::Reach reach;
    khdays::assets::DecodedTexture atlas;
};
std::optional<PrizeEffect> load_prize_effect(const std::filesystem::path& file, std::string* why);

// Frame `frame` of it (the loop's) drawn on a square canvas of `side` pixels over `background` (RGBA), by the
// reference renderer: RGBA rows.
std::vector<std::uint8_t> prize_effect_frame(const PrizeEffect& e, int frame, int side, std::uint32_t background);

// The collection's entries the KH2 prizes tool reads (installer/mod_inputs.txt's: KH2's obj/ entries, and
// bbs_second's), by archive.
std::map<std::string, std::vector<std::string>> kh2_prizes_inputs(const std::filesystem::path& mod_inputs);

// The KH2 prizes mod made into the mod in `mod_dir` from the player's copy (the studio's setting): its entries taken
// out under saves/collection (kh2, bbs_second), then tools/mods/kh2_prizes.py run as the installer runs it, the
// chests made by `port_exe`. Blocks until done; `lines` what the tool said. False with the reason.
bool make_kh2_prizes(const Settings& settings, const std::filesystem::path& mod_dir,
                     const std::filesystem::path& port_exe, std::vector<std::string>* lines, std::string* why);

}  // namespace studio
