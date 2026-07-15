#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <string>
#include <vector>

#include "khdays/assets/animation.h"
#include "khdays/assets/audio.h"
#include "khdays/assets/font.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h"
#include "khdays/assets/tex0.h"

// The resource layer sits between the Nintendo DS format importers and the
// engine. The renderer, and later collisions/animation/gameplay, depend only on
// the neutral formats returned here (NeutralModel, DecodedTexture,
// SkeletalAnimation) and never on the DS formats directly. Each loader resolves
// a user override from the mods directory before falling back to the original
// DS resource, so content can be replaced without touching engine code.
namespace khdays::resource {

// Set the directory searched for overrides (default: "mods", relative to the
// working directory). Each mod is a subfolder, e.g.
// "mods/<ModName>/textures/<anything>/<ds_name>.bmp"; the tree under a category
// (textures/, and later sounds/) is searched recursively by file name.
void set_mods_root(const std::filesystem::path& root);

// A texture ready for the engine: the image to upload (override or original DS)
// plus the *original DS texture size*. Texture coordinates are always normalized
// by the DS size, so a higher-resolution override maps correctly (HD textures).
struct LoadedTexture final {
    khdays::assets::DecodedTexture image;
    int reference_width = 0;
    int reference_height = 0;
};

// A model plus any textures that ship with it (glTF). DS models bundle no
// textures — those are loaded by name via load_texture().
struct LoadedModel final {
    khdays::assets::NeutralModel model;
    std::map<std::string, LoadedTexture> textures;
};

// Load a model through the resource layer. A path ending in .gltf/.glb is
// imported as glTF (with its own textures); otherwise the DS model is decoded.
LoadedModel load_model(const std::filesystem::path& path);

// Load a skeletal animation.
khdays::assets::SkeletalAnimation load_animation(
    const std::filesystem::path& ds_path);

// Load a texture by its DS name. `ds_source` is the DS file that embeds the
// TEX0. A "<mods>/<any-mod>/textures/**/<name>.bmp" override wins; the DS
// texture still provides the reference size for UV mapping.
LoadedTexture load_texture(
    const std::string& name,
    const std::filesystem::path& ds_source);

// Load a P2 message container and apply text overrides. A modder edits strings
// in "<mods>/<any-mod>/text/**/<db_name>.txt" (e.g. db_es.txt) with lines
// "subdb:index = text" (\n, \t, \xNN escapes); each replaces one string.
khdays::assets::MessageArchive load_message_archive(
    const std::filesystem::path& path);

// Load a UI string table (.s/.s.z) and apply text overrides from
// "<mods>/<any-mod>/text/**/<name>.txt" with lines "index = text".
std::vector<std::u16string> load_string_table(
    const std::filesystem::path& path);

// Decode a waveform from an SDAT, or a mod override if present: a WAV at
// "<mods>/<any-mod>/sounds/**/<wave_archive>_<swav>.wav" (e.g. 0_5.wav)
// replaces that waveform. Use --play-sound to preview the indices to override.
khdays::assets::DecodedAudio load_sound(
    const khdays::assets::Sdat& sdat,
    std::size_t wave_archive_index,
    std::size_t swav_index);

// Render an SDAT sequence to PCM, applying the same "sounds/" WAV overrides to
// its instrument waveforms, so a mod can swap the samples the music plays.
khdays::assets::DecodedAudio render_music(
    const khdays::assets::Sdat& sdat,
    std::size_t sequence_index,
    std::uint32_t sample_rate = 32768U,
    double max_seconds = 180.0);

// Compose a 2D background (NSCR tilemap + NCGR tiles + NCLR palette) to RGBA, or
// a mod override if present: a PNG/BMP at
// "<mods>/<any-mod>/graphics/**/<nscr_name>.{png,bmp}" replaces the whole image
// (e.g. an HD redraw). Pass the tile graphics in draw order.
khdays::assets::DecodedTexture load_background(
    const std::filesystem::path& nscr_path,
    const std::filesystem::path& nclr_path,
    const std::vector<std::filesystem::path>& ncgr_paths);

// Load an NFTR font, or a mod override if present: a replacement font at
// "<mods>/<any-mod>/fonts/**/<name>.nftr" wins.
khdays::assets::Font load_font(const std::filesystem::path& nftr_path);

}  // namespace khdays::resource
