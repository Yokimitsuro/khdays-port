#pragma once

// Runtime display/audio preferences the platform lets the user change (screen
// layout here; audio volume lives on the platform music player). Kept in the
// game layer because the neutral layout helpers in draw.h read it.
namespace khdays::game {

// How the two DS screens are arranged on the window.
enum class ScreenLayout {
    Vertical,    // top screen above the bottom screen (the DS's own layout)
    Horizontal,  // side by side: top screen left, bottom screen right
};

ScreenLayout screen_layout();
void set_screen_layout(ScreenLayout layout);
void toggle_screen_layout();

}  // namespace khdays::game
