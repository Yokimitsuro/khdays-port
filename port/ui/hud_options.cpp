// Presentation options load independently of any particular replacement resource.
#include "hud_options.h"
#include "kh2_hp_config.h"
#include "kh2_information.h"
#include "../host/app/hud_settings.h"
extern "C" {
#include "parts.h"
#include "text.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/window.h"
#include "../host/screens/ui_text.h"
#include "../runtime/core/session_log.h"
#include "../runtime/mods/textures.h"
int khdays_settings_game_language(void);
}
#include <cstdio>
#include <filesystem>
#include <array>
#include <cstring>

extern "C" void Ov002_PushMapSnapshot(int key);
extern "C" void Ov002_PopMapSnapshot(void);
extern "C" int Ov002_Hud_IsPanelOpen(void);
static bool preserving_maps;
static unsigned caption_context, caption_map;
static std::array<unsigned char, 0x100> caption_tail;
static int caption_scroll_x, caption_scroll_y;
static bool caption_scroll_valid;

static unsigned ram_word(unsigned address)
{
    unsigned value;
    std::memcpy(&value, KHDAYS_W(address), sizeof(value));
    return value;
}

extern "C" unsigned khdays_ui_information_policy(void)
{
    static bool latched;
    static unsigned policy;
    const unsigned wanted = (khdays_hud_setting(KHDAYS_OPT_INFORMATION) ? 1u : 0u) |
                            (khdays_hud_setting(KHDAYS_OPT_INFORMATION_HUD) ? 2u : 0u);
    const unsigned panel = khdays_window ? ram_word(0x0207f614) : 0;
    const unsigned state =
        panel && khdays_game_ram(panel) && khdays_game_ram(panel + 0x18f) ? ram_word(panel + 0x18c) : 0;
    if (state < 1 || state > 8)
        latched = false;
    if (!latched)
        policy = wanted;
    latched = state >= 1 && state <= 8;
    return policy;
}

extern "C" void khdays_ui_hud_settings_frame(void)
{
    static unsigned previous;
    const unsigned generation = khdays_hud_settings_generation();
    if (generation != previous) {
        previous = generation;
        khdays_ui_text_settings_changed();
        khdays_ui_changed();
    }
}

extern "C" void khdays_ui_information_push_maps(void)
{
    preserving_maps = khdays_ui_information_keep_hud() != 0;
    caption_context = caption_map = 0;
    caption_scroll_valid = false;
    if (preserving_maps) {
        const unsigned ui = ram_word(0x0207f60c);
        if (khdays_game_ram(ui) && khdays_game_ram(ui + 0x94)) {
            const unsigned map = ram_word(ui + 0x58);
            if (khdays_game_ram(map) && khdays_game_ram(map + 0x7ff)) {
                caption_context = ui;
                caption_map = map;
                std::memcpy(caption_tail.data(), KHDAYS_W(map + 0x700), caption_tail.size());
            }
        }
        khdays_log("INFORMATION: retaining live HUD artwork and text (BG1/BG2/BG3)");
    }
    Ov002_PushMapSnapshot(0);
    preserving_maps = false;
}

extern "C" void khdays_ui_information_pop_maps(void)
{
    Ov002_PopMapSnapshot();
    if (caption_context && ram_word(0x0207f60c) == caption_context && ram_word(caption_context + 0x58) == caption_map) {
        std::memcpy(KHDAYS_W(caption_map + 0x700), caption_tail.data(), caption_tail.size());
        *static_cast<unsigned char *>(KHDAYS_W(caption_context + 0x52)) |= 1;
    }
    caption_context = caption_map = 0;
}

extern "C" int khdays_ui_information_capture_scroll(unsigned x, unsigned y)
{
    if (!khdays_ui_information_keep_hud())
        return 0;
    caption_scroll_x = int(x & 511);
    caption_scroll_y = int(y & 511);
    caption_scroll_valid = true;
    return 1;
}

extern "C" int khdays_ui_information_text_scroll(int *x, int *y)
{
    if (!caption_scroll_valid || !khdays_ui_information_keep_hud())
        return 0;
    *x = caption_scroll_x;
    *y = caption_scroll_y;
    return 1;
}

extern "C" int khdays_ui_information_preserving_maps(void)
{
    return preserving_maps;
}

extern "C" unsigned khdays_ui_hud_options(void)
{
    static bool tried = false;
    static unsigned options = 0;
    static bool presentation_configured = false;
    if (!tried) {
        tried = true;
        try {
            char relative[128], found[2048];
            const char *language = khdays_language_code(khdays_settings_game_language());
            std::snprintf(relative, sizeof(relative), "layouts/gauge/%s/gauge.json",
                          language && *language ? language : "en");
            if (khdays_mods_find(relative, found, sizeof(found)) ||
                khdays_mods_find("layouts/gauge/en/gauge.json", found, sizeof(found))) {
                const auto config = khdays::ui::kh2::load_hud_presentation_config(
                    std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(found))));
                presentation_configured = true;
                options = (config.keep_hud_during_information ? KHDAYS_HUD_KEEP_INFORMATION : 0u) |
                          (config.align_hud_to_edges ? KHDAYS_HUD_ALIGN_EDGES : 0u) |
                          (config.pause_mission_target ? KHDAYS_HUD_PAUSE_TARGET : 0u);
            }
        } catch (const std::exception &e) {
            khdays_log("HUD presentation: %s; retaining default settings", e.what());
        }
    }
    unsigned live = khdays_textures_hd() ? options : 0;
    const int padding = khdays_hud_setting(KHDAYS_OPT_UI_PADDING);
    if (padding >= 0 && presentation_configured && khdays_textures_hd()) {
        live &= ~KHDAYS_HUD_ALIGN_EDGES;
        if (!padding)
            live |= KHDAYS_HUD_ALIGN_EDGES;
    }
    if (!khdays_hud_setting(KHDAYS_OPT_PAUSE_TARGET))
        live &= ~KHDAYS_HUD_PAUSE_TARGET;
    if (khdays_ui_information_policy() != 3)
        live &= ~KHDAYS_HUD_KEEP_INFORMATION;
    return live;
}

extern "C" int khdays_ui_information_keep_hud(void)
{
    return (khdays_ui_hud_options() & KHDAYS_HUD_KEEP_INFORMATION) && khdays_ui_information_configured();
}

// Only command input treats retained INFORMATION as nonmodal; panel scheduling stays native.
extern "C" int khdays_ui_command_panel_open(void)
{
    return Ov002_Hud_IsPanelOpen() && !khdays_ui_information_keep_hud();
}
