#include "khdays/game/settings.h"

namespace khdays::game {

namespace {
ScreenLayout g_screen_layout = ScreenLayout::Vertical;
}  // namespace

ScreenLayout screen_layout() { return g_screen_layout; }

void set_screen_layout(const ScreenLayout layout) { g_screen_layout = layout; }

void toggle_screen_layout() {
    g_screen_layout = g_screen_layout == ScreenLayout::Vertical
                          ? ScreenLayout::Horizontal
                          : ScreenLayout::Vertical;
}

}  // namespace khdays::game
