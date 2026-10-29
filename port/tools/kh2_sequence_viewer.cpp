// A local-asset SEQD viewer, independent of the Days runtime and LAD.
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
#include "../ui/kh2_hp_gauge.h"
#include "../ui/kh2_hp_config.h"
extern "C" {
#include "../host/render/png_io.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
namespace kh2 = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace gauge = khdays::ui::kh2;

void help()
{
    std::cout << "khdays-kh2-sequence-viewer --root KH2_DATA_DIRECTORY [options]\n"
                 "  --archive FILE --texture FILE   override the local field2d/us/zz0field.2dd and gaug PNG\n"
                 "  --entry NAME --sequence N --frame F --scale S --fps F\n"
                 "  --output FILE.png              render one frame and exit\n"
                 "  --list                         print the clip table and exit\n"
                 "  --hp DAYS_HP --max-hp DAYS_HP   display the separate HP gauge composition\n"
                 "  --config FILE.json             inspect a player or party gauge configuration\n"
                 "  --party N                      select party slot 1-3 from that configuration\n"
                 "  --enemy --bar-capacity N       inspect enemy gauge (Days: 250 story / 750 mission)\n"
                 "  --previous-hp DAYS_HP --damage-frame F --reaction small|medium|large\n"
                 "  --status normal|alert|dead --play --run-frames N\n"
                 "KHDAYS_KH2_ASSET_DIR can supply --root. No extraction is performed.\n"
                 "Viewer: Space play/pause, Left/Right step, Up/Down clip, Home restart, +/- zoom.\n"
                 "HP mode: Up/Down change HP by 31 Days units; Shift+Up/Down change maximum HP.\n"
                 "--fps is source timeline units per second (default 60), independent of display refresh.\n";
}

render::SequenceView centered(const kh2::SequenceData &data, std::size_t clip, float scale)
{
    const auto frame = kh2::evaluate_sequence(data, clip, 0);
    float x0 = std::numeric_limits<float>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    for (const auto &q : frame.quads) {
        for (const auto &v : q.vertices) {
            x0 = std::min(x0, v.x);
            y0 = std::min(y0, v.y);
            x1 = std::max(x1, v.x);
            y1 = std::max(y1, v.y);
        }
    }
    if (frame.quads.empty())
        x0 = y0 = x1 = y1 = 0;
    return {400 - (x0 + x1) * scale / 2, 250 - (y0 + y1) * scale / 2, scale};
}

} // namespace

int main(int argc, char **argv)
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    try {
        std::filesystem::path root, archive, picture, output, config_path;
        if (const char *env = std::getenv("KHDAYS_KH2_ASSET_DIR"))
            root = env;
        std::string entry = "gaug";
        std::size_t clip = 121;
        std::size_t config_slot = 0;
        float frame = 0, scale = 3, fps = 60;
        bool list = false, autoplay = false;
        std::size_t frame_limit = 0;
        bool hp_mode = false, enemy_mode = false;
        int bar_capacity = 250;
        gauge::HpGaugeSample hp;
        gauge::HpGaugeStyle style;
        gauge::EnemyGaugeStyle enemy_style;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                help();
                return 0;
            }
            if (option == "--list") {
                list = true;
                continue;
            }
            if (option == "--play") {
                autoplay = true;
                continue;
            }
            if (option == "--enemy") {
                enemy_mode = hp_mode = true;
                continue;
            }
            if (++i == argc)
                throw std::runtime_error("missing value for " + option);
            const std::string value = argv[i];
            if (option == "--root")
                root = value;
            else if (option == "--archive")
                archive = value;
            else if (option == "--texture")
                picture = value;
            else if (option == "--entry")
                entry = value;
            else if (option == "--config")
                config_path = value;
            else if (option == "--party") {
                config_slot = std::stoull(value);
                if (config_slot < 1 || config_slot > 3)
                    throw std::runtime_error("party slot must be 1-3");
            } else if (option == "--sequence")
                clip = std::stoull(value);
            else if (option == "--frame")
                frame = std::stof(value);
            else if (option == "--scale")
                scale = std::stof(value);
            else if (option == "--fps")
                fps = std::stof(value);
            else if (option == "--bar-capacity")
                bar_capacity = std::stoi(value);
            else if (option == "--output")
                output = value;
            else if (option == "--run-frames")
                frame_limit = std::stoull(value);
            else if (option == "--hp") {
                hp.hp = std::stof(value);
                hp_mode = true;
            } else if (option == "--max-hp") {
                hp.max_hp = std::stof(value);
                hp_mode = true;
            } else if (option == "--previous-hp") {
                hp.previous_hp = std::stof(value);
                hp_mode = true;
            } else if (option == "--damage-frame") {
                hp.damage_frame = std::stof(value);
                hp_mode = true;
            } else if (option == "--reaction") {
                if (value == "small")
                    hp.reaction = gauge::HpGaugeReaction::Small;
                else if (value == "medium")
                    hp.reaction = gauge::HpGaugeReaction::Medium;
                else if (value == "large")
                    hp.reaction = gauge::HpGaugeReaction::Large;
                else
                    throw std::runtime_error("reaction must be small, medium or large");
                hp_mode = true;
            } else if (option == "--status") {
                if (value == "normal")
                    hp.status = gauge::HpGaugeStatus::Normal;
                else if (value == "alert")
                    hp.status = gauge::HpGaugeStatus::Alert;
                else if (value == "dead")
                    hp.status = gauge::HpGaugeStatus::Dead;
                else
                    throw std::runtime_error("status must be normal, alert or dead");
                hp_mode = true;
            } else
                throw std::runtime_error("unknown option " + option);
        }
        if (!config_path.empty()) {
            if (enemy_mode) {
                const auto c = gauge::load_enemy_gauge_config(config_path);
                enemy_style = c.style;
                entry = c.entry;
                if (archive.empty())
                    archive = config_path.parent_path() / c.archive;
                if (picture.empty())
                    picture = config_path.parent_path() / c.texture;
            } else {
                const auto c = gauge::load_hp_gauge_configs(config_path)[config_slot];
                style = c.style;
                entry = c.entry;
                if (archive.empty())
                    archive = config_path.parent_path() / c.archive;
                if (picture.empty())
                    picture = config_path.parent_path() / c.texture;
            }
            hp_mode = true;
        }
        if ((archive.empty() || picture.empty()) && root.empty()) {
            help();
            return 1;
        }
        if (archive.empty())
            archive = root / "field2d/us/zz0field.2dd";
        if (picture.empty())
            picture = root / "remastered/field2d/us/zz0field.2dd/US_zz0field_2dd1.png";
        if (!std::isfinite(scale) || scale <= 0 || scale > 64 || !std::isfinite(fps) || fps <= 0 || fps > 1000)
            throw std::runtime_error("scale must be in (0,64], fps in (0,1000]");
        const auto asset = render::load_sequence_asset(archive, entry, picture);
        if (list) {
            for (std::size_t i = 0; i < asset.sequence.clips.size(); ++i) {
                const auto &c = asset.sequence.clips[i];
                std::cout << i << ": animations=" << c.animations.count
                          << " frames=" << kh2::sequence_frame_count(asset.sequence, i) << " loop=" << c.loop
                          << " restart=" << c.restart << '\n';
            }
            return 0;
        }
        if (clip >= asset.sequence.clips.size())
            throw std::runtime_error("sequence index out of range");
        kh2::SequencePlayer player;
        player.select(asset.sequence, clip);
        player.seek(frame);
        auto view = hp_mode ? render::SequenceView{400, 250, scale} : centered(asset.sequence, clip, scale);
        float damage_offset = hp.damage_frame - frame;
        std::vector<std::uint32_t> pixels(800 * 500);
        auto draw = [&] {
            for (int y = 0; y < 500; ++y)
                for (int x = 0; x < 800; ++x)
                    pixels[std::size_t(y) * 800 + std::size_t(x)] = ((x / 16 + y / 16) & 1) ? 0xff383838 : 0xff282828;
            hp.frame = player.frame();
            hp.damage_frame = std::max(0.0f, player.frame() + damage_offset);
            const auto evaluated = enemy_mode
                                       ? gauge::evaluate_enemy_gauge(asset.sequence,
                                                                     {int(hp.hp), int(hp.max_hp), int(hp.previous_hp),
                                                                      bar_capacity, hp.frame, hp.damage_frame},
                                                                     enemy_style)
                                   : hp_mode ? gauge::evaluate_hp_gauge(asset.sequence, hp, style)
                                             : kh2::evaluate_sequence(asset.sequence, player.clip(), player.frame());
            render::draw_sequence(evaluated.quads, asset.texture, asset.logical_size[0], asset.logical_size[1], pixels,
                                  800, 500, view);
        };
        draw();
        if (!output.empty()) {
            if (const char *error = khdays_png_write(output.string().c_str(),
                                                     reinterpret_cast<const std::uint8_t *>(pixels.data()), 800, 500))
                throw std::runtime_error(error);
            std::cout << "Rendered " << entry << " sequence " << clip << " frame " << frame << " to " << output.string()
                      << '\n';
            return 0;
        }
        if (!SDL_Init(SDL_INIT_VIDEO) ||
            !SDL_CreateWindowAndRenderer("KH2 SEQD", 800, 500, SDL_WINDOW_RESIZABLE, &window, &renderer))
            throw std::runtime_error(SDL_GetError());
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 800, 500);
        if (!texture)
            throw std::runtime_error(SDL_GetError());
        if (!SDL_SetRenderLogicalPresentation(renderer, 800, 500, SDL_LOGICAL_PRESENTATION_LETTERBOX))
            throw std::runtime_error(SDL_GetError());
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        bool running = true, playing = autoplay;
        std::size_t rendered_frames = 0;
        auto before = SDL_GetTicksNS();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT)
                    running = false;
                if (event.type != SDL_EVENT_KEY_DOWN)
                    continue;
                const auto key = event.key.key;
                if (key == SDLK_ESCAPE)
                    running = false;
                if (key == SDLK_SPACE)
                    playing = !playing;
                if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    playing = false;
                    player.seek(std::max(0.0f, std::floor(player.frame()) + (key == SDLK_RIGHT ? 1.0f : -1.0f)));
                }
                if (key == SDLK_HOME)
                    player.select(asset.sequence, player.clip(), true);
                if (key == SDLK_UP || key == SDLK_DOWN) {
                    if (hp_mode) {
                        const float before_hp = hp.hp, delta = key == SDLK_UP ? 31.0f : -31.0f;
                        if (event.key.mod & SDL_KMOD_SHIFT)
                            hp.max_hp = std::max(1.0f, hp.max_hp + delta);
                        else
                            hp.hp = std::clamp(hp.hp + delta, 0.0f, hp.max_hp);
                        hp.hp = std::min(hp.hp, hp.max_hp);
                        hp.previous_hp = std::max(before_hp, hp.hp);
                        hp.reaction = hp.hp < before_hp ? gauge::HpGaugeReaction::Small : gauge::HpGaugeReaction::None;
                        damage_offset = -player.frame();
                    } else {
                        const auto count = asset.sequence.clips.size();
                        player.select(asset.sequence, (player.clip() + (key == SDLK_UP ? 1 : count - 1)) % count, true);
                    }
                    view = hp_mode ? render::SequenceView{400, 250, scale}
                                   : centered(asset.sequence, player.clip(), scale);
                }
                if (key == SDLK_EQUALS || key == SDLK_MINUS) {
                    scale = std::clamp(scale * (key == SDLK_EQUALS ? 1.25f : 0.8f), 0.125f, 64.0f);
                    view = hp_mode ? render::SequenceView{400, 250, scale}
                                   : centered(asset.sequence, player.clip(), scale);
                }
            }
            const auto now = SDL_GetTicksNS();
            if (playing) {
                const float delta = float(double(now - before) / 1e9) * fps;
                if (hp_mode)
                    player.seek(player.frame() + delta);
                else
                    player.advance(asset.sequence, delta);
            }
            before = now;
            draw();
            char title[160];
            if (hp_mode)
                std::snprintf(title, sizeof title, "KH2 HP | Days %.0f / %.0f | frame %.1f | %s", hp.hp, hp.max_hp,
                              player.frame(), playing ? "playing" : "paused");
            else
                std::snprintf(title, sizeof title, "KH2 %s | sequence %zu | frame %.1f | %s | %.0f units/s",
                              entry.c_str(), player.clip(), player.frame(), playing ? "playing" : "paused", fps);
            SDL_SetWindowTitle(window, title);
            if (!SDL_UpdateTexture(texture, nullptr, pixels.data(), 800 * 4) ||
                !SDL_RenderTexture(renderer, texture, nullptr, nullptr) || !SDL_RenderPresent(renderer))
                throw std::runtime_error(SDL_GetError());
            if (frame_limit != 0 && ++rendered_frames >= frame_limit)
                running = false;
            SDL_Delay(8);
        }
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
}
