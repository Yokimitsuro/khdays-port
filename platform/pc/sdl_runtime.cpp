#include "khdays/platform/runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <SDL3/SDL.h>

#include "khdays/assets/tex0.h"
#include "khdays/platform/gpu_renderer.h"
#include "khdays/port.h"
#include "khdays/vfs/filesystem.h"
#include "music_backend.h"

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
    if (options.model_path.has_value()) {
        return render_model(*options.model_path, options.animation_path);
    }

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

namespace {

// Bridges the neutral Renderer scenes draw through onto the SDL 2D renderer.
// Uploaded images are cached by their pixel pointer (scenes hold static images),
// so each is turned into an SDL texture once.
class SdlFrameRenderer final : public khdays::game::Renderer {
public:
    explicit SdlFrameRenderer(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~SdlFrameRenderer() override {
        for (auto& [key, texture] : cache_) {
            SDL_DestroyTexture(texture);
        }
    }

    void clear(khdays::game::Color color) override {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderClear(renderer_);
    }

    void fill_overlay(khdays::game::Color color) override {
        if (color.a == 0) {
            return;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer_, nullptr);
    }

    void draw_image(
        const std::uint8_t* rgba, int width, int height, int x, int y,
        int dst_width, int dst_height) override {
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return;
        }
        SDL_Texture* texture = upload(rgba, width, height);
        if (texture == nullptr) {
            return;
        }
        SDL_FRect dst{
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(dst_width > 0 ? dst_width : width),
            static_cast<float>(dst_height > 0 ? dst_height : height)};
        SDL_RenderTexture(renderer_, texture, nullptr, &dst);
    }

    int width() const override {
        int w = 0;
        int h = 0;
        SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
        return w;
    }
    int height() const override {
        int w = 0;
        int h = 0;
        SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
        return h;
    }

    // Drop all cached textures. The cache is keyed by the source pixel pointer,
    // which is only unique while a scene's images stay alive; across a scene
    // change a freed buffer can be reallocated at the same address, so the cache
    // must be invalidated on transition or it would serve the old scene's image.
    void clear_cache() {
        for (auto& [key, texture] : cache_) {
            SDL_DestroyTexture(texture);
        }
        cache_.clear();
    }

private:
    SDL_Texture* upload(const std::uint8_t* rgba, int width, int height) {
        const auto it = cache_.find(rgba);
        if (it != cache_.end()) {
            return it->second;
        }
        SDL_Texture* texture = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
            width, height);
        if (texture == nullptr) {
            return nullptr;
        }
        SDL_UpdateTexture(texture, nullptr, rgba, width * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        cache_.emplace(rgba, texture);
        return texture;
    }

    SDL_Renderer* renderer_;
    std::unordered_map<const void*, SDL_Texture*> cache_;
};

// Map the keyboard onto the neutral pad buttons.
std::uint16_t poll_buttons() {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    using B = khdays::game::Button;
    std::uint16_t down = 0;
    const auto set = [&](SDL_Scancode code, B button) {
        if (keys[code]) {
            down |= static_cast<std::uint16_t>(button);
        }
    };
    set(SDL_SCANCODE_Z, B::A);
    set(SDL_SCANCODE_SPACE, B::A);
    set(SDL_SCANCODE_X, B::B);
    set(SDL_SCANCODE_RETURN, B::Start);
    set(SDL_SCANCODE_RSHIFT, B::Select);
    set(SDL_SCANCODE_UP, B::Up);
    set(SDL_SCANCODE_DOWN, B::Down);
    set(SDL_SCANCODE_LEFT, B::Left);
    set(SDL_SCANCODE_RIGHT, B::Right);
    return down;
}

}  // namespace

int run_game(khdays::game::Game& game) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_sdl_error("SDL_Init");
        return EXIT_FAILURE;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    const std::string title =
        std::string{khdays::port::Version::name} + " " + KHDAYS_PORT_VERSION;
    if (!SDL_CreateWindowAndRenderer(
            title.c_str(),
            kInitialWindowWidth,
            kInitialWindowHeight,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer)) {
        log_sdl_error("SDL_CreateWindowAndRenderer");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SdlFrameRenderer frame_renderer{renderer};

    // Music: render the game's SSEQ tracks and stream them for scenes that
    // request a BGM. Absent SDAT (no game data) → scenes simply run silent.
    std::optional<SdlMusicPlayer> music;
    if (const auto sdat_path = khdays::vfs::resolve("snd/sound_data.sdat")) {
        music.emplace(*sdat_path);
        if (music->ok()) {
            game.scenes().set_music_player(&*music);
        }
    }

    std::uint16_t previous = 0;
    bool running = true;
    khdays::game::SceneId last_scene = game.scenes().current_id();

    // Run until a scene ends the flow (no current scene) or the window closes.
    while (running && game.scenes().has_scene()) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN
                && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        const std::uint16_t down = poll_buttons();
        khdays::game::Input input;
        input.down = down;
        input.pressed = static_cast<std::uint16_t>(down & ~previous);
        previous = down;

        game.scenes().set_input(input);
        game.step();
        // A scene transition frees the old scene's images; drop cached textures
        // so the new scene's (possibly same-address) buffers upload fresh.
        if (game.scenes().current_id() != last_scene) {
            frame_renderer.clear_cache();
            last_scene = game.scenes().current_id();
        }
        game.render(frame_renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    // Detach the music player before it is destroyed at scope exit.
    game.scenes().set_music_player(nullptr);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

}  // namespace khdays::platform
