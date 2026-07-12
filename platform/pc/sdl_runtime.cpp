#include "khdays/platform/runtime.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <SDL3/SDL.h>

#include "khdays/assets/tex0.h"
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

struct TextureDeleter final {
    void operator()(SDL_Texture* texture) const {
        SDL_DestroyTexture(texture);
    }
};

using TexturePointer = std::unique_ptr<SDL_Texture, TextureDeleter>;

struct RuntimeResource final {
    TexturePointer texture;
    std::string name;
    std::string format_name;
    int width = 0;
    int height = 0;
};

void log_sdl_error(const char* operation) {
    std::cerr
        << operation << " failed: " << SDL_GetError() << '\n';
}

SDL_FRect fit_inside(
    const int source_width,
    const int source_height,
    const SDL_FRect& bounds) {
    if (source_width <= 0 || source_height <= 0) {
        return bounds;
    }

    const auto horizontal_scale =
        bounds.w / static_cast<float>(source_width);
    const auto vertical_scale =
        bounds.h / static_cast<float>(source_height);
    const auto scale = std::min(horizontal_scale, vertical_scale);

    const auto width = static_cast<float>(source_width) * scale;
    const auto height = static_cast<float>(source_height) * scale;

    return SDL_FRect{
        bounds.x + (bounds.w - width) * 0.5F,
        bounds.y + (bounds.h - height) * 0.5F,
        width,
        height,
    };
}

std::optional<RuntimeResource> load_resource(
    SDL_Renderer* renderer,
    const khdays::platform::ApplicationOptions& options,
    std::string& error_message) {
    if (!options.resource_path.has_value()) {
        return std::nullopt;
    }

    try {
        const auto decoded = khdays::assets::load_tex0_texture(
            *options.resource_path,
            options.texture_name.has_value()
                ? std::optional<std::string_view>{*options.texture_name}
                : std::nullopt);

        SDL_Texture* raw_texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STATIC,
            decoded.width,
            decoded.height);

        if (raw_texture == nullptr) {
            throw std::runtime_error(
                std::string{"SDL_CreateTexture failed: "}
                + SDL_GetError());
        }

        TexturePointer texture{raw_texture};

        if (!SDL_UpdateTexture(
                texture.get(),
                nullptr,
                decoded.rgba.data(),
                decoded.width * 4)) {
            throw std::runtime_error(
                std::string{"SDL_UpdateTexture failed: "}
                + SDL_GetError());
        }

        if (!SDL_SetTextureScaleMode(
                texture.get(),
                SDL_SCALEMODE_NEAREST)) {
            throw std::runtime_error(
                std::string{"SDL_SetTextureScaleMode failed: "}
                + SDL_GetError());
        }

        SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);

        std::cout
            << "Loaded texture '" << decoded.name << "' "
            << decoded.width << 'x' << decoded.height << ' '
            << decoded.format_name << '\n';

        return RuntimeResource{
            std::move(texture),
            decoded.name,
            decoded.format_name,
            decoded.width,
            decoded.height,
        };
    } catch (const std::exception& error) {
        error_message = error.what();
        std::cerr << "Resource load failed: " << error_message << '\n';
        return std::nullopt;
    }
}

void render_frame(
    SDL_Renderer* renderer,
    const std::optional<RuntimeResource>& resource,
    const std::string& resource_error) {
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

    if (resource.has_value()) {
        const auto destination = fit_inside(
            resource->width,
            resource->height,
            top_screen);
        SDL_RenderTexture(
            renderer,
            resource->texture.get(),
            nullptr,
            &destination);
    }

    SDL_SetRenderDrawColor(renderer, 102, 132, 168, 255);
    SDL_RenderRect(renderer, &top_screen);
    SDL_RenderRect(renderer, &bottom_screen);

    SDL_SetRenderDrawColor(renderer, 224, 232, 240, 255);
    SDL_RenderDebugText(
        renderer,
        24.0F,
        24.0F,
        "khdays-port - native resource loading");

    if (resource.has_value()) {
        const std::string description =
            "Loaded TEX0: "
            + resource->name
            + " "
            + std::to_string(resource->width)
            + "x"
            + std::to_string(resource->height)
            + " "
            + resource->format_name;

        SDL_RenderDebugText(
            renderer,
            24.0F,
            44.0F,
            description.c_str());
    } else if (!resource_error.empty()) {
        const std::string message =
            "Resource error: " + resource_error;
        SDL_RenderDebugText(
            renderer,
            24.0F,
            44.0F,
            message.c_str());
    } else {
        SDL_RenderDebugText(
            renderer,
            24.0F,
            44.0F,
            "No resource selected; use --resource FILE");
    }

    SDL_RenderDebugText(
        renderer,
        24.0F,
        64.0F,
        "Press Escape to close");

    SDL_RenderPresent(renderer);
}

}  // namespace

namespace khdays::platform {

int run_application(const ApplicationOptions& options) {
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

    std::string resource_error;
    const auto resource = load_resource(
        renderer,
        options,
        resource_error);

    bool running = true;

    while (running) {
        SDL_Event event{};

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            if (
                event.type == SDL_EVENT_KEY_DOWN
                && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        render_frame(renderer, resource, resource_error);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}

}  // namespace khdays::platform
