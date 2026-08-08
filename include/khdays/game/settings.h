#pragma once

#include <string>
#include <string_view>

// Runtime display/audio/language preferences the platform lets the user change
// (screen layout here; audio volume lives on the platform music player). Kept in
// the game layer because the neutral layout helpers in draw.h read it, and
// because scene code resolves its localized assets through it.
namespace khdays::game {

// How the two DS screens are arranged on the window.
enum class ScreenLayout {
    Vertical,    // top screen above the bottom screen (the DS's own layout)
    Horizontal,  // side by side: top screen left, bottom screen right
};

ScreenLayout screen_layout();
void set_screen_layout(ScreenLayout layout);
void toggle_screen_layout();

// The languages this (European) release ships. One setting drives both the
// game's own localized assets and the port's interface — the user picks a
// language, not a language per subsystem.
enum class Language {
    German,
    English,
    Spanish,
    French,
    Italian,
};

Language language();
void set_language(Language value);

// Frame pacing: the DS refreshes at 59.8261 Hz; "Sixty" rounds to a flat 60 for
// monitors/round numbers. Drives the platform loop's target frame time only --
// game logic is frame-based either way.
enum class FrameRate {
    Sixty,       // 60.0 fps
    DsOriginal,  // 59.8261 fps (the DS's real refresh)
};

FrameRate frame_rate();
void set_frame_rate(FrameRate value);

// Target frame duration in nanoseconds for the current frame-rate setting.
long long frame_duration_ns();

// The game's two-letter code for a language ("de", "en", "es", "fr", "it") —
// the suffix its localized files carry.
std::string_view language_code(Language value);
// The language whose code this is; English if unrecognised.
Language language_from_code(std::string_view code);
// The name to show in a menu, in that language itself.
std::string_view language_name(Language value);

// Resolve a localized game path. The game writes these paths with an `&` where
// the language code goes — ov000 stores "UI/cm/cmo_&.p2" and "UI/mlt/res_&.p2"
// literally — so this mirrors that convention rather than inventing one.
//
//     localized_path("UI/cm/cmo_&.p2")  ->  "UI/cm/cmo_es.p2"
std::string localized_path(std::string_view pattern);

}  // namespace khdays::game
