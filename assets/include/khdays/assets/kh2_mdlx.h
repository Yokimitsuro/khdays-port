#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "khdays/assets/model_import.h"

namespace khdays::assets::kh2 {

// KH2's models (obj/<name>.mdlx) imported like a mod's glTF: in metres (KH2's are
// centimetres), Y up, its bones the joints (kh2_<bone index>) at rest as the file
// places them; textures decoded from the PS2's or, where `hd_folder` has them, its
// -N.png or -N.dds (docs/KH2_MODELS.md); a file's is the folder beside it with its
// name. Throws std::runtime_error naming what it cannot read.
ImportedModel import_mdlx(const std::filesystem::path& path);
ImportedModel import_mdlx(const std::uint8_t* data, std::size_t size, const std::string& name,
                          const std::filesystem::path& hd_folder = {});

// The joint name given to bone `bone`.
std::string mdlx_joint_name(int bone);

// How many of a model file's HD pictures (-N.dds) its texture containers take (BAR entries of type 7, in their
// order, each container of an entry that holds several: each GS picture, then a sprite sheet per texture animation):
// the file's other pictures -- its PAX's (entry 0x12, Hades's hair's flame: -10 and -11 after his two sets' ten) --
// are numbered from there. Throws std::runtime_error naming what it cannot read.
int mdlx_texture_pictures(const std::uint8_t* data, std::size_t size);

}  // namespace khdays::assets::kh2
