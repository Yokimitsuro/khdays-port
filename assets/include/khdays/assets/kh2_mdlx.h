#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "khdays/assets/model_import.h"

namespace khdays::assets::kh2 {

// KH2's models (obj/<name>.mdlx: a BAR of the model, its textures and more),
// imported like a mod's glTF: in metres (KH2's units are centimetres), Y up,
// its bones the joints, named kh2_<bone index>, at rest as the file places
// them (a character's T-pose); its textures decoded from the PS2's. Throws
// std::runtime_error naming what it cannot read.
ImportedModel import_mdlx(const std::filesystem::path& path);
ImportedModel import_mdlx(const std::uint8_t* data, std::size_t size, const std::string& name);

// The joint name given to bone `bone`.
std::string mdlx_joint_name(int bone);

}  // namespace khdays::assets::kh2
