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
#include "khdays/assets/message.h"
#include "khdays/game/settings.h"
#include "khdays/platform/gpu_renderer.h"
#include "khdays/port.h"
#include "khdays/resource/video.h"
#include "khdays/resource/ui_content.h"
#include "khdays/vfs/filesystem.h"
#include "music_backend.h"
#include "overlay_ui.h"

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

std::size_t opening_subtitle_language() {
    using khdays::game::Language;
    switch (khdays::game::language()) {
    case Language::English: return 0U;
    case Language::French: return 1U;
    case Language::German: return 2U;
    case Language::Italian: return 3U;
    case Language::Spanish: return 4U;
    }
    return 0U;
}

std::string normalized_game_path(const std::string_view path) {
    std::string result{path};
    std::replace(result.begin(), result.end(), '\\', '/');
    while (!result.empty() && result.front() == '/') {
        result.erase(result.begin());
    }
    return result;
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
        if (dynamic_tex_ != nullptr) {
            SDL_DestroyTexture(dynamic_tex_);
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
        int dst_width, int dst_height, int alpha) override {
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return;
        }
        SDL_Texture* texture = upload(rgba, width, height);
        if (texture == nullptr) {
            return;
        }
        // Textures are cached and shared, so set the modulation every draw
        // (it persists on the texture otherwise).
        SDL_SetTextureAlphaMod(
            texture, static_cast<Uint8>(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
        SDL_FRect dst{
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(dst_width > 0 ? dst_width : width),
            static_cast<float>(dst_height > 0 ? dst_height : height)};
        SDL_RenderTexture(renderer_, texture, nullptr, &dst);
    }

    void draw_image_affine(
        const std::uint8_t* rgba, int width, int height, const float m[6],
        int alpha) override {
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return;
        }
        SDL_Texture* texture = upload(rgba, width, height);
        if (texture == nullptr) {
            return;
        }
        const float fa =
            static_cast<float>(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha) / 255.0F;
        const SDL_FColor col{1.0F, 1.0F, 1.0F, fa};
        // Source corners (TL, TR, BR, BL) mapped through the 2x3 to the screen.
        const float sx[4] = {0.0F, static_cast<float>(width),
                             static_cast<float>(width), 0.0F};
        const float sy[4] = {0.0F, 0.0F, static_cast<float>(height),
                             static_cast<float>(height)};
        const float uv[4][2] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F},
                                {0.0F, 1.0F}};
        SDL_Vertex v[4];
        for (int k = 0; k < 4; ++k) {
            v[k].position.x = m[0] * sx[k] + m[2] * sy[k] + m[4];
            v[k].position.y = m[1] * sx[k] + m[3] * sy[k] + m[5];
            v[k].color = col;
            v[k].tex_coord.x = uv[k][0];
            v[k].tex_coord.y = uv[k][1];
        }
        const int indices[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer_, texture, v, 4, indices, 6);
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
    void draw_image_dynamic(
        const std::uint8_t* rgba, int width, int height, int x, int y,
        int dst_width, int dst_height, int alpha) override {
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return;
        }
        // A per-frame texture: re-upload into a reused streaming texture instead
        // of the pointer-keyed cache (which would serve stale pixels).
        if (dynamic_tex_ == nullptr || dynamic_w_ != width
            || dynamic_h_ != height) {
            if (dynamic_tex_ != nullptr) {
                SDL_DestroyTexture(dynamic_tex_);
            }
            dynamic_tex_ = SDL_CreateTexture(
                renderer_, SDL_PIXELFORMAT_ABGR8888,
                SDL_TEXTUREACCESS_STREAMING, width, height);
            dynamic_w_ = width;
            dynamic_h_ = height;
            if (dynamic_tex_ != nullptr) {
                SDL_SetTextureBlendMode(dynamic_tex_, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(dynamic_tex_, SDL_SCALEMODE_NEAREST);
            }
        }
        if (dynamic_tex_ == nullptr) {
            return;
        }
        SDL_UpdateTexture(dynamic_tex_, nullptr, rgba, width * 4);
        SDL_SetTextureAlphaMod(
            dynamic_tex_,
            static_cast<Uint8>(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
        SDL_FRect dst{
            static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(dst_width > 0 ? dst_width : width),
            static_cast<float>(dst_height > 0 ? dst_height : height)};
        SDL_RenderTexture(renderer_, dynamic_tex_, nullptr, &dst);
    }

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
    SDL_Texture* dynamic_tex_ = nullptr;  // reused for per-frame images
    int dynamic_w_ = 0;
    int dynamic_h_ = 0;
};

// Streaming bridge used by game scenes. Decoding stays in the neutral MODS
// decoder; this class contributes only the wall clock and SDL audio queue.
class SdlVideoPlayer final : public khdays::game::VideoPlayer {
public:
    ~SdlVideoPlayer() override { stop_video(); }

    void play_video(const std::string_view game_path) override {
        stop_video();
        try {
            decoder_.emplace(khdays::resource::load_mods_video(game_path));
            const double fps = decoder_->frames_per_second();
            if (fps <= 0.0) {
                throw std::runtime_error("MODS video has an invalid frame rate");
            }
            frame_duration_ns_ = static_cast<Uint64>(1000000000.0 / fps);
            next_frame_ns_ = SDL_GetTicksNS();
            playing_ = true;

            if (decoder_->info().has_audio()) {
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                    std::cerr << "video: SDL audio init failed: "
                              << SDL_GetError() << "; playing silent\n";
                } else {
                    audio_inited_ = true;
                    SDL_AudioSpec spec{};
                    spec.format = SDL_AUDIO_S16;
                    spec.channels = decoder_->info().audio_channels;
                    spec.freq = decoder_->info().audio_rate;
                    audio_stream_ = SDL_OpenAudioDeviceStream(
                        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                        nullptr);
                    if (audio_stream_ == nullptr) {
                        std::cerr << "video: audio device unavailable: "
                                  << SDL_GetError() << "; playing silent\n";
                    } else {
                        SDL_SetAudioStreamGain(audio_stream_, volume_);
                    }
                }
            }
        } catch (const std::exception& error) {
            std::cerr << "video: cannot play '" << game_path
                      << "': " << error.what() << '\n';
            stop_video();
        }
    }

    void stop_video() override {
        if (audio_stream_ != nullptr) {
            SDL_DestroyAudioStream(audio_stream_);
            audio_stream_ = nullptr;
        }
        if (audio_inited_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_inited_ = false;
        }
        decoder_.reset();
        current_ = {};
        playing_ = false;
        have_frame_ = false;
        audio_started_ = false;
        audio_flushed_ = false;
        next_frame_ns_ = 0;
    }

    bool video_playing() const override { return playing_; }

    khdays::game::VideoFrame video_frame() override {
        if (!decoder_ || !playing_) {
            return current_;
        }

        const Uint64 now = SDL_GetTicksNS();
        while (!decoder_->finished()
               && (!have_frame_ || now >= next_frame_ns_)) {
            if (!decoder_->decode_next(/*ds_exact=*/true)) {
                break;
            }
            const auto& decoded = decoder_->frame();
            current_ = {decoded.width, decoded.height, decoded.rgba.data()};
            have_frame_ = true;

            if (audio_stream_ != nullptr) {
                const auto& audio = decoder_->audio_chunk();
                const int bytes = static_cast<int>(
                    audio.samples.size() * sizeof(std::int16_t));
                if (bytes > 0
                    && !SDL_PutAudioStreamData(
                        audio_stream_, audio.samples.data(), bytes)) {
                    std::cerr << "video: audio queue failed: "
                              << SDL_GetError() << '\n';
                }
                if (!audio_started_ && bytes > 0) {
                    SDL_ResumeAudioStreamDevice(audio_stream_);
                    audio_started_ = true;
                }
            }
            next_frame_ns_ += frame_duration_ns_;
        }

        if (decoder_->finished()) {
            if (audio_stream_ != nullptr && !audio_flushed_) {
                SDL_FlushAudioStream(audio_stream_);
                audio_flushed_ = true;
            }
            if (audio_stream_ == nullptr
                || SDL_GetAudioStreamAvailable(audio_stream_) <= 0) {
                playing_ = false;
            }
        }
        return current_;
    }

    std::size_t video_frame_index() const override {
        if (!decoder_ || decoder_->frame_index() == 0U) {
            return 0U;
        }
        return decoder_->frame_index() - 1U;
    }

    void set_video_volume(const float volume) override {
        volume_ = std::clamp(volume, 0.0F, 1.0F);
        if (audio_stream_ != nullptr) {
            SDL_SetAudioStreamGain(audio_stream_, volume_);
        }
    }

private:
    std::optional<khdays::assets::ModsVideoDecoder> decoder_;
    SDL_AudioStream* audio_stream_ = nullptr;
    khdays::game::VideoFrame current_{};
    Uint64 frame_duration_ns_ = 0;
    Uint64 next_frame_ns_ = 0;
    float volume_ = 1.0F;
    bool playing_ = false;
    bool have_frame_ = false;
    bool audio_inited_ = false;
    bool audio_started_ = false;
    bool audio_flushed_ = false;
};

}  // namespace

int play_mods_video(const std::string_view game_path) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        log_sdl_error("SDL_Init");
        return EXIT_FAILURE;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_Texture* subtitle_texture = nullptr;
    SDL_AudioStream* audio_stream = nullptr;
    const auto cleanup = [&] {
        SDL_DestroyAudioStream(audio_stream);
        SDL_DestroyTexture(subtitle_texture);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    };
    if (!SDL_CreateWindowAndRenderer(
            "khdays-port - MobiClip", 1024, 640,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window,
            &renderer)) {
        log_sdl_error("SDL_CreateWindowAndRenderer");
        cleanup();
        return EXIT_FAILURE;
    }

    try {
        auto decoder = khdays::resource::load_mods_video(game_path);
        std::vector<khdays::assets::MovieSubtitleCue> subtitle_cues;
        try {
            const auto script = khdays::resource::load_opening_movie_script();
            if (normalized_game_path(script.movie_path)
                == normalized_game_path(game_path)) {
                subtitle_cues = script.subtitles[opening_subtitle_language()];
            }
        } catch (const std::exception&) {
            // Other MODS clips and partial extractions still preview normally.
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    decoder.info().width, decoder.info().height);
        if (texture == nullptr) {
            throw std::runtime_error(
                std::string{"SDL_CreateTexture failed: "} + SDL_GetError());
        }
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        const double fps = decoder.frames_per_second();
        if (fps <= 0.0) {
            throw std::runtime_error("MODS video has an invalid frame rate");
        }
        const auto frame_ns = static_cast<Uint64>(1000000000.0 / fps);
        if (decoder.info().has_audio()) {
            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_S16;
            spec.channels = decoder.info().audio_channels;
            spec.freq = decoder.info().audio_rate;
            audio_stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (audio_stream == nullptr) {
                throw std::runtime_error(
                    std::string{"SDL_OpenAudioDeviceStream failed: "}
                    + SDL_GetError());
            }
        }
        Uint64 next_frame_ns = SDL_GetTicksNS();
        bool running = true;
        bool have_frame = false;
        std::size_t active_subtitle = subtitle_cues.size();
        std::cout << "Playing " << game_path << ": " << decoder.info().width
                  << 'x' << decoder.info().height << ", "
                  << decoder.info().frame_count << " frames @ " << fps
                  << " fps (Esc to stop)\n";

        while (running && !decoder.finished()) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT
                    || (event.type == SDL_EVENT_KEY_DOWN
                        && event.key.key == SDLK_ESCAPE)) {
                    running = false;
                }
            }
            const Uint64 now = SDL_GetTicksNS();
            if (!have_frame || now >= next_frame_ns) {
                if (decoder.decode_next(/*ds_exact=*/true)) {
                    const auto& frame = decoder.frame();
                    if (!SDL_UpdateTexture(texture, nullptr, frame.rgba.data(),
                                           frame.width * 4)) {
                        throw std::runtime_error(
                            std::string{"SDL_UpdateTexture failed: "}
                            + SDL_GetError());
                    }
                    if (audio_stream != nullptr) {
                        const auto& audio = decoder.audio_chunk();
                        const auto bytes = static_cast<int>(
                            audio.samples.size() * sizeof(std::int16_t));
                        if (bytes > 0
                            && !SDL_PutAudioStreamData(
                                audio_stream, audio.samples.data(), bytes)) {
                            throw std::runtime_error(
                                std::string{"SDL_PutAudioStreamData failed: "}
                                + SDL_GetError());
                        }
                        if (decoder.frame_index() == 1U) {
                            SDL_ResumeAudioStreamDevice(audio_stream);
                        }
                    }
                    have_frame = true;
                }
                next_frame_ns = std::max(next_frame_ns + frame_ns, now);
            }

            const auto frame_index = decoder.frame_index() == 0U
                ? 0U : decoder.frame_index() - 1U;
            std::size_t next_subtitle = subtitle_cues.size();
            for (std::size_t i = 0U; i < subtitle_cues.size(); ++i) {
                if (frame_index >= subtitle_cues[i].start_frame
                    && frame_index < subtitle_cues[i].end_frame) {
                    next_subtitle = i;
                    break;
                }
            }
            if (next_subtitle != active_subtitle) {
                SDL_DestroyTexture(subtitle_texture);
                subtitle_texture = nullptr;
                active_subtitle = next_subtitle;
                if (active_subtitle < subtitle_cues.size()
                    && !subtitle_cues[active_subtitle].text.empty()) {
                    const auto rendered = khdays::resource::render_ui_text(
                        "text/font_eu_10all.nftr",
                        khdays::assets::message_from_utf8(
                            subtitle_cues[active_subtitle].text));
                    if (rendered) {
                        subtitle_texture = SDL_CreateTexture(
                            renderer, SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STATIC,
                            rendered->width, rendered->height);
                        if (subtitle_texture != nullptr) {
                            SDL_UpdateTexture(
                                subtitle_texture, nullptr,
                                rendered->rgba.data(), rendered->width * 4);
                            SDL_SetTextureBlendMode(
                                subtitle_texture, SDL_BLENDMODE_BLEND);
                            SDL_SetTextureScaleMode(
                                subtitle_texture, SDL_SCALEMODE_NEAREST);
                        }
                    }
                }
            }

            int output_width = 0;
            int output_height = 0;
            SDL_GetCurrentRenderOutputSize(renderer, &output_width, &output_height);
            const SDL_FRect bounds{0.0F, 0.0F, static_cast<float>(output_width),
                                   static_cast<float>(output_height)};
            const bool scripted_opening = !subtitle_cues.empty();
            const auto screen = fit_inside(
                decoder.info().width,
                scripted_opening ? 192 : decoder.info().height, bounds);
            SDL_FRect destination = screen;
            if (scripted_opening) {
                destination.h = screen.w
                    * static_cast<float>(decoder.info().height)
                    / static_cast<float>(decoder.info().width);
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, texture, nullptr, &destination);
            if (subtitle_texture != nullptr
                && active_subtitle < subtitle_cues.size()) {
                float text_width = 0.0F;
                float text_height = 0.0F;
                SDL_GetTextureSize(
                    subtitle_texture, &text_width, &text_height);
                const float scale = screen.w
                    / static_cast<float>(decoder.info().width);
                SDL_FRect text_destination{
                    screen.x + (screen.w - text_width * scale) * 0.5F,
                    screen.y + 160.0F * scale
                        + (32.0F - text_height) * scale * 0.5F,
                    text_width * scale, text_height * scale};
                SDL_FRect shadow = text_destination;
                shadow.x += scale;
                shadow.y += scale;
                SDL_SetTextureColorMod(subtitle_texture, 0, 0, 0);
                SDL_RenderTexture(renderer, subtitle_texture, nullptr, &shadow);
                SDL_SetTextureColorMod(subtitle_texture, 255, 255, 255);
                SDL_RenderTexture(
                    renderer, subtitle_texture, nullptr, &text_destination);
            }
            SDL_RenderPresent(renderer);

            const Uint64 after_render = SDL_GetTicksNS();
            if (after_render < next_frame_ns) {
                SDL_DelayNS(std::min<Uint64>(next_frame_ns - after_render,
                                             2000000U));
            }
        }
        if (running && audio_stream != nullptr) {
            SDL_FlushAudioStream(audio_stream);
            while (SDL_GetAudioStreamAvailable(audio_stream) > 0) {
                SDL_Delay(5);
            }
        }
        cleanup();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Video playback failed: " << error.what() << '\n';
        cleanup();
        return EXIT_FAILURE;
    }
}

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
    OverlayUi overlay{window, renderer};  // options menu bar (volume/layout/keys)

    // Music: render the game's SSEQ tracks and stream them for scenes that
    // request a BGM. Absent SDAT (no game data) → scenes simply run silent.
    std::optional<SdlMusicPlayer> music;
    if (const auto sdat_path = khdays::vfs::resolve("snd/sound_data.sdat")) {
        music.emplace(*sdat_path);
        if (music->ok()) {
            game.scenes().set_music_player(&*music);
        }
    }

    SdlVideoPlayer video;
    game.scenes().set_video_player(&video);

    std::uint16_t previous = 0;
    bool running = true;
    khdays::game::SceneId last_scene = game.scenes().current_id();
    Uint64 next_frame_ns = SDL_GetTicksNS();

    // Run until a scene ends the flow (no current scene) or the window closes.
    while (running && game.scenes().has_scene()) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            overlay.process_event(event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            // Esc quits, unless the overlay is capturing keys (e.g. rebinding).
            if (event.type == SDL_EVENT_KEY_DOWN
                && event.key.key == SDLK_ESCAPE && !overlay.wants_keyboard()) {
                running = false;
            }
        }

        // Suppress game input while the UI has the keyboard.
        const std::uint16_t down =
            overlay.wants_keyboard() ? 0 : poll_buttons(overlay.bindings());
        khdays::game::Input input;
        input.down = down;
        input.pressed = static_cast<std::uint16_t>(down & ~previous);
        previous = down;
        if (music) {
            music->set_volume(overlay.volume());
        }
        video.set_video_volume(overlay.volume());

        game.scenes().set_input(input);
        game.step();
        // A scene transition frees the old scene's images; drop cached textures
        // so the new scene's (possibly same-address) buffers upload fresh.
        if (game.scenes().current_id() != last_scene) {
            frame_renderer.clear_cache();
            last_scene = game.scenes().current_id();
        }
        game.render(frame_renderer);
        overlay.render();  // draw the menu bar / windows over the frame
        SDL_RenderPresent(renderer);

        // Pace to the selected frame rate (60 fps or the DS's 59.8261 Hz). Wait
        // until the next frame deadline; if we fell behind, resync to now so the
        // loop never spirals.
        next_frame_ns += static_cast<Uint64>(khdays::game::frame_duration_ns());
        const Uint64 now_ns = SDL_GetTicksNS();
        if (now_ns < next_frame_ns) {
            SDL_DelayNS(next_frame_ns - now_ns);
        } else {
            next_frame_ns = now_ns;
        }
    }

    overlay.save_config();  // persist volume / layout / key bindings
    // Detach the music player before it is destroyed at scope exit.
    game.scenes().set_music_player(nullptr);
    game.scenes().set_video_player(nullptr);
    video.stop_video();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

}  // namespace khdays::platform
