#pragma once

#include <string_view>

#include "khdays/assets/mods_video.h"

namespace khdays::resource {

khdays::assets::MobiClipCoefficientTables load_mobiclip_coefficient_tables();
khdays::assets::ModsVideoDecoder load_mods_video(std::string_view game_path);

}  // namespace khdays::resource
