#include "khdays/platform/runtime.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include <SDL3/SDL.h>

#include "khdays/port.h"

#ifndef KHDAYS_PORT_VERSION
#define KHDAYS_PORT_VERSION "unknown"
#endif

namespace {

constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 720;
constexpr float kScreenWidth = 512.0F;
constexpr float kScreenHeight = 384.0F;
constexpr float kScreenGap = 32.0F;

void log_sdl_error(const char* operation) {
    std::cerr
        << operation << " failed: " << SDL_GetError() << '\n';
}

void render_placeholder(SDL_Renderer* renderer) {
    int output_width = kInitialWindowWidth;
    int output_height = kInitialWindowHeight;

    if (!SDL_GetCurrentRenderOutputSize(
            renderer,
            &output_width,
            &output_height)) {
        log_sdl_error("SDL_GetCurrentRenderOutputSize");
    }

    const float total_width =
        (kScreenWidth * 2.0F) + kScreenGap;
    const float start_x =
        (static_cast<float>(output_width) - total_width) * 0.5F;
    const float start_y =
        (static_cast<float>(output_height) - kScreenHeight) * 0.5F;

    const SDL_FRect top_screen{
        start_x,
        start_y,
        kScreenWidth,
        kScreenHeight,
    };
    const SDL_FRect bottom_screen{
        start_x + kScreenWidth + kScreenGap,
        start_y,
        kScreenWidth,
        kScreenHeight,
    };

    SDL_SetRenderDrawColor(renderer, 10, 16, 28, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 25, 40, 64, 255);
    SDL_RenderFillRect(renderer, &top_screen);

    SDL_SetRenderDrawColor(renderer, 32, 50, 78, 255);
    SDL_RenderFillRect(renderer, &bottom_screen);

    SDL_SetRenderDrawColor(renderer, 102, 132, 168, 255);
    SDL_RenderRect(renderer, &top_screen);
    SDL_RenderRect(renderer, &bottom_screen);

    SDL_SetRenderDrawColor(renderer, 224, 232, 240, 255);
    SDL_RenderDebugText(
        renderer,
        24.0F,
        24.0F,
        "khdays-port - SDL3 platform runtime");
    SDL_RenderDebugText(
        renderer,
        24.0F,
        44.0F,
        "Two Nintendo DS screen placeholders");
    SDL_RenderDebugText(
        renderer,
        24.0F,
        64.0F,
        "Press Escape to close");

    SDL_RenderPresent(renderer);
}

}  // namespace

namespace khdays::platform {

int run_application() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_sdl_error("SDL_Init");
        return EXIT_FAILURE;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    const std::string title =
        std::string{khdays::port::Version::name}
        + " "
        + KHDAYS_PORT_VERSION;

    const SDL_WindowFlags flags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (!SDL_CreateWindowAndRenderer(
            title.c_str(),
            kInitialWindowWidth,
            kInitialWindowHeight,
            flags,
            &window,
            &renderer)) {
        log_sdl_error("SDL_CreateWindowAndRenderer");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    bool running = true;

    while (running) {
        SDL_Event event{};

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            if (
                event.type == SDL_EVENT_KEY_DOWN
                && event.key.key == SDLK_ESCAPE
            ) {
                running = false;
            }
        }

        render_placeholder(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}

}  // namespace khdays::platform
