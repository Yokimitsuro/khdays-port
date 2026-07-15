#pragma once

#include <cstdint>
#include <string>

#include <SDL3/SDL.h>

#include "khdays/game/input.h"  // Button

namespace khdays::platform {

// Keyboard → DS button bindings, remappable in the Controls window.
struct KeyBindings final {
    SDL_Scancode up = SDL_SCANCODE_UP;
    SDL_Scancode down = SDL_SCANCODE_DOWN;
    SDL_Scancode left = SDL_SCANCODE_LEFT;
    SDL_Scancode right = SDL_SCANCODE_RIGHT;
    SDL_Scancode a = SDL_SCANCODE_Z;
    SDL_Scancode b = SDL_SCANCODE_X;
    SDL_Scancode x = SDL_SCANCODE_S;
    SDL_Scancode y = SDL_SCANCODE_A;
    SDL_Scancode start = SDL_SCANCODE_RETURN;
    SDL_Scancode select = SDL_SCANCODE_RSHIFT;
};

// The current DS button state from the keyboard, using `bindings`.
std::uint16_t poll_buttons(const KeyBindings& bindings);

// The in-window options overlay: a menu bar (Config: volume / controls / screen
// layout, View: hide bar / fullscreen) drawn through the SDL renderer with Dear
// ImGui. Built without ImGui (KHDAYS_HAS_UI off) it is an inert no-op so the
// runtime still links.
class OverlayUi final {
public:
    OverlayUi(SDL_Window* window, SDL_Renderer* renderer);
    ~OverlayUi();
    OverlayUi(const OverlayUi&) = delete;
    OverlayUi& operator=(const OverlayUi&) = delete;

    // Feed an SDL event (for ImGui input and key-rebind capture).
    void process_event(const SDL_Event& event);
    // Build the menu bar / windows and draw them over the frame.
    void render();

    // True while the UI is capturing input, so the game should ignore it.
    bool wants_keyboard() const;

    float volume() const { return volume_; }
    const KeyBindings& bindings() const { return bindings_; }

private:
    void apply_fullscreen();

    SDL_Window* window_;
    SDL_Renderer* renderer_;
    KeyBindings bindings_;
    float volume_ = 0.7F;
    bool fullscreen_ = false;
    bool applied_fullscreen_ = false;
    bool show_menu_bar_ = true;
    bool show_controls_ = false;
    int remapping_ = -1;      // index of the binding being rebound, or -1
    float hint_timer_ = 0.0F;  // seconds left to show `hint_`
    std::string hint_;
    bool ui_ = false;  // ImGui initialised
};

}  // namespace khdays::platform
