#pragma once

#include <filesystem>
#include <string>

#include "khdays/assets/animation.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"

// The resource layer sits between the Nintendo DS format importers and the
// engine. The renderer, and later collisions/animation/gameplay, depend only on
// the neutral formats returned here (NeutralModel, DecodedTexture,
// SkeletalAnimation) and never on the DS formats directly. Each loader resolves
// a user override from the mods directory before falling back to the original
// DS resource, so content can be replaced without touching engine code.
namespace khdays::resource {

// Set the directory searched for overrides (default: "mods", relative to the
// working directory).
void set_mods_root(const std::filesystem::path& root);

// Load a model as a neutral, engine-independent mesh.
khdays::assets::NeutralModel load_model(
    const std::filesystem::path& ds_path);

// Load a skeletal animation.
khdays::assets::SkeletalAnimation load_animation(
    const std::filesystem::path& ds_path);

// Load a texture by name. `ds_source` is the DS file that embeds the TEX0 used
// as the fallback; an override at "<mods>/textures/<name>.bmp" wins.
khdays::assets::DecodedTexture load_texture(
    const std::string& name,
    const std::filesystem::path& ds_source);

}  // namespace khdays::resource
