// khdays-secondary-replay's stand-ins for the running game that port/runtime/mods/models.cpp calls: with `--days`
// the replay poses a model by the game's animations through models.cpp itself (khdays_models_offline_pose), the game's
// side of it answered here: no drawing, textures, characters or rooms; the game model's name and bytes, the mods'
// files and a VBlank count from the replay (replay_game_stubs.h).

#include "replay_game_stubs.h"

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../gfx/character_shadow.h"
#include "../gfx/g3d.h"
#include "../host/app/hud_settings.h"
#include "../runtime/core/events.h"
#include "../runtime/core/origin.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/window.h"
#include "../runtime/mods/textures.h"
#include "../runtime/scripts/game_link.h"

KhdaysRef GetTrackEntryBase(int index);
KhdaysRef Collision_RunRayCast(KhdaysRef world, KhdaysRef params);
void Vec3ScaleAddQ27(s32 distance, KhdaysRef direction, KhdaysRef origin, KhdaysRef out);
int khdays_controls_walk_share(unsigned node);
int khdays_overlay_loaded(int id);
const void *khdays_character_drawn_object(void);
void khdays_walk_model_draw(unsigned model_resource);
void khdays_walk_model_draw_end(void);
}
#include "../runtime/characters/characters.h"
#include "../runtime/mods/names.h"

namespace {
std::string model_file, model_name;
unsigned model_offset = 0;
std::vector<unsigned char> model_bmd0;
std::filesystem::path own_mod;
u32 vblanks = 0;
double per_draw = 1.0, carried = 0.0; // VBlanks a draw (60 / the draw rate)
unsigned char no_window[16];
} // namespace

void replay_game_set_model(const std::string &file, unsigned offset, const std::string &name,
                           std::vector<unsigned char> bmd0, const std::filesystem::path &mod)
{
    model_file = file;
    model_offset = offset;
    model_name = name;
    model_bmd0 = std::move(bmd0);
    own_mod = mod;
}

void replay_game_tick()
{
    carried += per_draw;
    const auto whole = static_cast<u32>(carried);
    vblanks += whole;
    carried -= whole;
}

void replay_game_set_draw_hz(float hz)
{
    per_draw = hz > 0.0F ? 60.0 / hz : 1.0;
    carried = 0.0;
}

void replay_game_gap()
{
    vblanks += 600U;
}

// --- the game model and the mods' files ---------------------------------------------------------------------------

int khdays_names_model(const char *file, u32 offset, char *out, int size)
{
    if (file == nullptr || model_file != file || offset != model_offset || size <= 0)
        return 0;
    std::snprintf(out, static_cast<std::size_t>(size), "%s", model_name.c_str());
    return 1;
}

bool khdays_names_model_bmd0(const char *file, u32 offset, std::vector<unsigned char> &bmd0, std::size_t &index)
{
    if (file == nullptr || model_file != file || offset != model_offset || model_bmd0.empty())
        return false;
    bmd0 = model_bmd0;
    index = 0;
    return true;
}

int khdays_names_animation(const char *, u32, char *, int)
{
    return 0;
}

// The model's own mod first, so its files are the ones read; then the others beside it
int khdays_mods_find(const char *relative, char *path, int size)
{
    std::error_code e;
    std::vector<std::filesystem::path> mods{own_mod};
    for (const auto &mod : std::filesystem::directory_iterator(own_mod.parent_path(), e))
        if (mod.path() != own_mod)
            mods.push_back(mod.path());
    for (const auto &mod : mods) {
        const std::filesystem::path p = mod / relative;
        if (std::filesystem::is_regular_file(p, e)) {
            const int n = std::snprintf(path, static_cast<std::size_t>(size), "%s", p.string().c_str());
            return n > 0 && n < size ? 1 : 0; // a path too long for the game's buffer: not found, as the port's
        }
    }
    return 0;
}

int khdays_origin_of_ram(u32, const char **, u32 *, u32 *)
{
    return 0;
}

// no rooms a mod adds, no characters, and no mods started for the cloth's prewarm: only the model given
extern "C" int khdays_rooms_model(const char *, char *, int)
{
    return 0;
}

extern "C" const char *khdays_character_base_hand(int, int)
{
    return nullptr;
}

extern "C" int khdays_mods_started_count(void)
{
    return 0;
}

extern "C" const char *khdays_mods_started_path(int)
{
    return nullptr;
}

// no KH2 Dark Corridors offline: the replay draws no rooms' effects
extern "C" int khdays_kh2_corridors_begin(const void *, const void *)
{
    return 0;
}

extern "C" void khdays_kh2_corridors_node(unsigned) {}
extern "C" void khdays_kh2_corridors_shape(void) {}

// --- the game's memory, clock and log: none of the game's own, one VBlank a draw, the log to stderr off ------------

unsigned char *khdays_window = no_window;

KhdaysRef khdays_reference(const void *)
{
    return 0; // never the game's RAM (khdays_game_ram is false for it)
}

u32 khdays_events_vblank_count(void)
{
    return vblanks;
}

unsigned khdays_link_frame(void)
{
    return vblanks;
}

double khdays_link_frame_seconds(void)
{
    return 1.0 / 60.0;
}

int khdays_link_paused(void)
{
    return 0;
}

// KHDAYS_REPLAY_LOG=1 also lets models.cpp's own traces through (KHDAYS_TRACE_MODELS and the like)
int khdays_log_on(void)
{
    static const bool on = std::getenv("KHDAYS_REPLAY_LOG") != nullptr;
    return on ? 1 : 0;
}

// KHDAYS_REPLAY_LOG=1: the session log's lines on stderr (models.cpp's traces among them)
void khdays_log(const char *format, ...)
{
    static const bool on = std::getenv("KHDAYS_REPLAY_LOG") != nullptr;
    if (!on)
        return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc(10, stderr);
}

u32 khdays_log_marks(void)
{
    return 0;
}

int khdays_overlay_loaded(int)
{
    return 0;
}

int khdays_controls_walk_share(unsigned)
{
    return 0;
}

void khdays_walk_model_draw(unsigned) {}

const KhdaysShadowOutcome *khdays_shadow_outcome(uintptr_t)
{
    return nullptr;
}

unsigned khdays_shadow_lists_finished(void)
{
    return 0;
}

const void *khdays_character_drawn_object(void)
{
    return nullptr; // no game object: `upright` leaves the model as posed
}
void khdays_walk_model_draw_end(void) {}

KhdaysRef GetTrackEntryBase(int)
{
    return 0;
}

KhdaysRef Collision_RunRayCast(KhdaysRef, KhdaysRef)
{
    return 0;
}

void Vec3ScaleAddQ27(s32, KhdaysRef, KhdaysRef, KhdaysRef) {}

// --- the 3D engine: model space, nothing drawn ---------------------------------------------------------------------

void khdays_g3d_draw_begin(const void *) {}
void khdays_g3d_draw_end(void) {}

// A camera at the world's origin (the world is model space): what models.cpp's `hand` posing needs to run
int khdays_g3d_camera(float camera[16])
{
    std::memset(camera, 0, sizeof(float) * 16);
    camera[0] = camera[5] = camera[10] = camera[15] = 1.0F;
    return 1;
}

int khdays_g3d_base(float base[16])
{
    std::memset(base, 0, sizeof(float) * 16);
    base[0] = base[5] = base[10] = base[15] = 1.0F;
    return 0;
}

void khdays_g3d_matrices(float position[16], float vector[16], float projection[16])
{
    for (float *m : {position, vector, projection}) {
        std::memset(m, 0, sizeof(float) * 16);
        m[0] = m[5] = m[10] = m[15] = 1.0F;
    }
}

int khdays_g3d_param_count(unsigned)
{
    return 0;
}

void khdays_g3d_command(unsigned, const uint32_t *) {}

void khdays_g3d_material(KhdaysG3dMaterial *out)
{
    std::memset(out, 0, sizeof(*out));
}

void khdays_g3d_light(const KhdaysG3dMaterial *, const float[3], float rgb[3])
{
    rgb[0] = rgb[1] = rgb[2] = 1.0F;
}

void khdays_g3d_mod_polygon(const KhdaysG3dClipVertex *, int, uint32_t, uint32_t, int32_t, int, int) {}
void khdays_g3d_clip_polygon(const KhdaysG3dClipVertex *) {}
void khdays_g3d_set_clip_owner(int) {}

int khdays_shadow_sprite_active(void)
{
    return 0;
}

int khdays_shadow_has_sprite(void)
{
    return 0;
}

// --- textures, settings and characters: HD on, every switch on, no mod characters --------------------------------

int khdays_textures_hd(void)
{
    return 1;
}

int khdays_textures_add(const char *, int, int, const uint8_t *)
{
    return -1;
}

int khdays_textures_add_changing(const char *, int, int, uint32_t)
{
    return -1;
}

uint32_t *khdays_textures_change_begin(int)
{
    return nullptr;
}

void khdays_textures_change_end(int) {}

extern "C" const char *const khdays_hud_setting_keys[KHDAYS_OPT_COUNT] = {};

int khdays_hud_setting(int)
{
    return 1;
}

const KhdaysCharacterAction *khdays_characters_action(const KhdaysCharacter *, const char *)
{
    return nullptr;
}

const char *khdays_characters_clip_mark(const char *)
{
    return nullptr;
}

int khdays_characters_file(const KhdaysCharacter *, const char *, char *, int)
{
    return 0;
}

int khdays_characters_model(const char *, char *, int)
{
    return 0;
}

const KhdaysCharacter *khdays_characters_of_model(const char *)
{
    return nullptr;
}

void khdays_characters_play_none(int) {}

const KhdaysCharacterAction *khdays_characters_played(const KhdaysCharacter *, unsigned *, int *)
{
    return nullptr;
}
