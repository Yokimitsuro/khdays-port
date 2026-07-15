#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <SDL3/SDL_main.h>

#include "khdays/assets/animation.h"
#include "khdays/assets/audio.h"
#include "khdays/assets/cell.h"
#include "khdays/assets/font.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/mdl0.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h"
#include "khdays/assets/sdat.h"
#include "khdays/assets/sequence.h"
#include "khdays/game/game.h"
#include "khdays/game/scenes/boot_logo_scene.h"
#include "khdays/game/scenes/main_menu_scene.h"
#include "khdays/game/scenes/title_scene.h"
#include "khdays/game/software_renderer.h"
#include "khdays/platform/audio.h"
#include "khdays/platform/runtime.h"
#include "khdays/port.h"
#include "khdays/resource/loader.h"
#include "khdays/vfs/filesystem.h"

#ifndef KHDAYS_PORT_VERSION
#define KHDAYS_PORT_VERSION "unknown"
#endif

namespace {

// Placeholder scenes for the --game-demo: they only log, standing in for the
// real boot/logo and title scenes until khdays-decomp names their logic. They
// exercise the same scene/task framework the game will use.
class DemoLogoScene final : public khdays::game::Scene {
public:
    void update(khdays::game::SceneManager& manager) override {
        ++frames_;
        std::cout << "  [logo] frame " << frames_ << '\n';
        if (frames_ == 3) {
            std::cout << "  [logo] done -> requesting title\n";
            manager.change_scene(khdays::game::kSceneContinue);
        }
    }

private:
    int frames_ = 0;
};

class DemoTitleScene final : public khdays::game::Scene {
public:
    void update(khdays::game::SceneManager&) override {
        ++frames_;
        std::cout << "  [title] frame " << frames_ << '\n';
    }

private:
    int frames_ = 0;
};

int run_game_demo() {
    khdays::game::Game game;
    game.scenes().on_scene_entered([](khdays::game::SceneId id, int arg) {
        std::cout << "== enter scene " << id << " (arg " << arg << ") ==\n";
    });
    game.scenes().register_scene(
        khdays::game::kSceneBootLogo,
        [] { return std::make_unique<DemoLogoScene>(); });
    game.scenes().register_scene(
        khdays::game::kSceneContinue,
        [] { return std::make_unique<DemoTitleScene>(); });

    // A background object whose state-machine ticks a few frames (the game's
    // object model) alongside the scenes.
    int ticks = 0;
    game.objects().spawn([&ticks](khdays::game::Object& self) {
        std::cout << "  (object tick " << ++ticks << ")\n";
        if (ticks >= 4) {
            self.finish();
        }
    });

    std::cout << "boot (fresh) ...\n";
    game.boot(0);
    for (int i = 0; i < 8; ++i) {
        game.step();
    }
    std::cout << "ran " << game.frame() << " frames; final scene "
              << game.scenes().current_id() << '\n';
    return EXIT_SUCCESS;
}

void print_version() {
    std::cout
        << khdays::port::Version::name << ' ' << KHDAYS_PORT_VERSION << '\n'
        << "Stage: " << khdays::port::Version::stage << '\n';
}

void print_help() {
    print_version();
    std::cout
        << '\n'
        << "Usage:\n"
        << "  khdays-port [--resource FILE] [--texture NAME]\n"
        << "  khdays-port --render-model FILE [--anim FILE]\n"
        << "  khdays-port --model-info FILE\n"
        << "  khdays-port --anim-info FILE\n"
        << "  khdays-port --audio-info FILE\n"
        << "  khdays-port --vfs-resolve GAMEPATH\n"
        << "  khdays-port --game\n"
        << "  khdays-port --game-demo\n"
        << "  khdays-port --render-tiles NCGR NCLR OUT.bmp [PALETTE]\n"
        << "  khdays-port --render-bg NSCR NCLR OUT.bmp NCGR [NCGR...]\n"
        << "  khdays-port --render-text NFTR TEXT OUT.bmp\n"
        << "  khdays-port --render-cell P2 SUBFILE CELL OUT.bmp\n"
        << "  khdays-port --extract-wav SDAT WAVEARCHIVE SWAV OUTPUT.wav\n"
        << "  khdays-port --play-sound SDAT WAVEARCHIVE SWAV\n"
        << "  khdays-port --render-sequence SDAT SEQ OUTPUT.wav [SECONDS]\n"
        << "  khdays-port --play-sequence SDAT SEQ [SECONDS]\n"
        << "  khdays-port --message-info FILE\n"
        << "  khdays-port --dump-messages FILE [SUBDB]\n"
        << "  khdays-port --dump-strings FILE\n"
        << "  khdays-port --export-obj FILE [OUTPUT.obj]\n"
        << "  khdays-port --version\n"
        << "  khdays-port --help\n"
        << '\n'
        << "Options:\n"
        << "  --resource FILE     Load TEX0 data from a user-extracted NSBMD/NSBTX.\n"
        << "  --texture NAME      Select a texture by name; defaults to the first.\n"
        << "  --render-model FILE Render an MDL0 model in 3D in the native window.\n"
        << "  --anim FILE         Play this NSBCA animation instead of the auto-detected one.\n"
        << "  --model-info FILE   Inspect MDL0 models, materials, meshes, and GPU commands.\n"
        << "  --anim-info FILE    Inspect an NSBCA skeletal animation.\n"
        << "  --vfs-resolve GAMEPATH  Resolve a NitroFS game path in the extracted data.\n"
        << "  --game              Run the scene/task frame loop in a window (placeholder scenes).\n"
        << "  --game-demo         Run the scene/task frame loop headless (logs the flow).\n"
        << "  --render-tiles NCGR NCLR OUT.bmp [PALETTE]  Render an NCGR tile sheet to BMP.\n"
        << "  --render-bg NSCR NCLR OUT.bmp NCGR...  Compose an NSCR background to BMP.\n"
        << "  --render-text NFTR TEXT OUT.bmp  Render TEXT with an NFTR font to BMP.\n"
        << "  --audio-info FILE   List the contents of an SDAT sound archive.\n"
        << "  --extract-wav SDAT WAVEARCHIVE SWAV OUTPUT.wav  Decode a SWAV waveform to WAV.\n"
        << "  --play-sound SDAT WAVEARCHIVE SWAV  Decode and play a SWAV waveform.\n"
        << "  --render-sequence SDAT SEQ OUT.wav [SECONDS]  Synthesize an SSEQ to WAV.\n"
        << "  --play-sequence SDAT SEQ [SECONDS]  Synthesize and play an SSEQ.\n"
        << "  --message-info FILE Summarize a P2 message container (db_<lang>.p2).\n"
        << "  --dump-messages FILE [SUBDB]  Print decoded UTF-8 text (optionally one sub-db).\n"
        << "  --dump-strings FILE Print a UI string table (.s/.s.z) as UTF-8.\n"
        << "  --export-obj FILE   Decode the first MDL0 model to a Wavefront OBJ mesh.\n"
        << "  --version           Print version information without opening a window.\n"
        << "  --help              Show this help text.\n";
}

khdays::platform::ApplicationOptions parse_runtime_options(
    int argc,
    char* argv[]) {
    khdays::platform::ApplicationOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if (argument == "--resource") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--resource requires a file path");
            }
            options.resource_path = std::filesystem::path{argv[++index]};
            continue;
        }

        if (argument == "--texture") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--texture requires a texture name");
            }
            options.texture_name = std::string{argv[++index]};
            continue;
        }

        if (argument == "--render-model") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--render-model requires a file path");
            }
            options.model_path = std::filesystem::path{argv[++index]};
            continue;
        }

        if (argument == "--anim") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--anim requires a file path");
            }
            options.animation_path = std::filesystem::path{argv[++index]};
            continue;
        }

        throw std::invalid_argument(
            "unknown argument: " + std::string{argument});
    }

    if (options.texture_name.has_value() && !options.resource_path.has_value()) {
        throw std::invalid_argument(
            "--texture can only be used together with --resource");
    }

    if (options.animation_path.has_value() && !options.model_path.has_value()) {
        throw std::invalid_argument(
            "--anim can only be used together with --render-model");
    }

    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) {
        const std::string_view first{argv[1]};

        if (first == "--version") {
            print_version();
            return EXIT_SUCCESS;
        }

        if (first == "--help" || first == "-h") {
            print_help();
            return EXIT_SUCCESS;
        }

        if (first == "--model-info") {
            if (argc != 3) {
                std::cerr
                    << "ERROR: --model-info requires exactly one file path\n";
                return EXIT_FAILURE;
            }

            try {
                const auto information =
                    khdays::assets::inspect_mdl0(
                        std::filesystem::path{argv[2]});
                std::cout
                    << khdays::assets::format_model_report(information);
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--audio-info") {
            if (argc != 3) {
                std::cerr << "ERROR: --audio-info requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto sdat = khdays::assets::read_sdat_inventory(
                    std::filesystem::path{argv[2]});
                const auto show = [](const char* label,
                                     const std::vector<std::string>& items) {
                    std::cout << label << ": " << items.size() << '\n';
                    for (std::size_t i = 0; i < items.size() && i < 8; ++i) {
                        if (!items[i].empty()) {
                            std::cout << "    " << items[i] << '\n';
                        }
                    }
                    if (items.size() > 8) {
                        std::cout << "    ... (" << (items.size() - 8)
                                  << " more)\n";
                    }
                };
                std::cout << "SDAT inventory:\n";
                show("  Sequences", sdat.sequences);
                show("  Sequence archives", sdat.sequence_archives);
                show("  Banks", sdat.banks);
                show("  Wave archives", sdat.wave_archives);
                show("  Stream players", sdat.stream_players);
                show("  Streams", sdat.streams);
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--game-demo") {
            return run_game_demo();
        }

        if (first == "--menu-shot") {
            // Headless snapshot of the main-menu scene (for previewing the
            // layout without opening a window). Optional 2nd arg = how many
            // times to move the cursor right before capturing.
            if (argc < 3) {
                std::cerr << "ERROR: --menu-shot requires an output BMP "
                             "[cursor-steps]\n";
                return EXIT_FAILURE;
            }
            khdays::vfs::autodetect_data_root();
            const int steps = argc > 3 ? std::stoi(argv[3]) : 0;
            khdays::game::SceneManager manager;
            manager.register_scene(khdays::game::kSceneMainMenu, [] {
                return std::make_unique<khdays::game::scenes::MainMenuScene>();
            });
            manager.start(khdays::game::kSceneMainMenu);
            for (int i = 0; i < steps; ++i) {
                khdays::game::Input in;
                in.pressed = static_cast<std::uint16_t>(khdays::game::Button::Right);
                manager.set_input(in);
                manager.step();
            }
            manager.set_input(khdays::game::Input{});
            manager.step();
            khdays::game::SoftwareRenderer sw{512, 384};
            manager.render(sw);
            const auto bmp = khdays::assets::to_bmp(sw.snapshot());
            std::ofstream out{argv[2], std::ios::binary};
            out.write(reinterpret_cast<const char*>(bmp.data()),
                      static_cast<std::streamsize>(bmp.size()));
            std::cout << "Main-menu snapshot (cursor step " << steps
                      << ") -> BMP: " << argv[2] << '\n';
            return EXIT_SUCCESS;
        }

        if (first == "--game") {
            if (!khdays::vfs::autodetect_data_root()) {
                std::cerr << "note: no extracted data under data/extracted; "
                             "scenes will show without game assets\n";
            }
            khdays::game::Game game;
            game.scenes().register_scene(khdays::game::kSceneBootLogo, [] {
                return std::make_unique<khdays::game::scenes::BootLogoScene>();
            });
            game.scenes().register_scene(khdays::game::kSceneTitle, [] {
                return std::make_unique<khdays::game::scenes::TitleScene>();
            });
            game.scenes().register_scene(khdays::game::kSceneMainMenu, [] {
                return std::make_unique<khdays::game::scenes::MainMenuScene>();
            });
            game.boot(0);
            std::cout << "Running the game frame loop (Esc to quit, "
                         "Enter/Z/Space to advance)...\n";
            return khdays::platform::run_game(game);
        }

        if (first == "--vfs-resolve") {
            if (argc != 3) {
                std::cerr << "ERROR: --vfs-resolve requires a game path\n";
                return EXIT_FAILURE;
            }
            if (!khdays::vfs::autodetect_data_root()) {
                std::cerr << "ERROR: could not find extracted data under "
                             "data/extracted (run the extractor first)\n";
                return EXIT_FAILURE;
            }
            std::cout << "data root: " << khdays::vfs::data_root().string()
                      << '\n';
            const auto resolved = khdays::vfs::resolve(argv[2]);
            if (!resolved) {
                std::cerr << "not found: " << argv[2] << '\n';
                return EXIT_FAILURE;
            }
            std::error_code ec;
            const auto size = std::filesystem::file_size(*resolved, ec);
            std::cout << argv[2] << " -> " << resolved->string() << " ("
                      << size << " bytes)\n";
            return EXIT_SUCCESS;
        }

        if (first == "--render-tiles") {
            if (argc != 5 && argc != 6) {
                std::cerr << "ERROR: --render-tiles requires NCGR, NCLR, an "
                             "output BMP, and an optional palette index\n";
                return EXIT_FAILURE;
            }
            try {
                const auto tiles =
                    khdays::assets::decode_ncgr(std::filesystem::path{argv[2]});
                const auto palette =
                    khdays::assets::decode_nclr(std::filesystem::path{argv[3]});
                const int palette_index = argc == 6 ? std::stoi(argv[5]) : 0;
                const auto image = khdays::assets::render_tile_sheet(
                    tiles, palette, palette_index);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[4], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << "Decoded " << tiles.tile_count << " tiles ("
                          << tiles.bpp << "bpp), " << palette.colors.size()
                          << " palette colors -> " << image.width << 'x'
                          << image.height << " BMP: " << argv[4] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-bg") {
            if (argc < 6) {
                std::cerr << "ERROR: --render-bg requires NSCR, NCLR, an output "
                             "BMP, and one or more NCGR\n";
                return EXIT_FAILURE;
            }
            try {
                std::vector<std::filesystem::path> ncgr_paths;
                for (int i = 5; i < argc; ++i) {
                    ncgr_paths.emplace_back(argv[i]);
                }
                const auto image = khdays::resource::load_background(
                    std::filesystem::path{argv[2]},
                    std::filesystem::path{argv[3]},
                    ncgr_paths);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[4], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << "Composed " << image.width << 'x' << image.height
                          << " background -> BMP: " << argv[4] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-cell") {
            if (argc != 6) {
                std::cerr << "ERROR: --render-cell requires a P2 file, sub-file "
                             "index, cell index, and an output BMP\n";
                return EXIT_FAILURE;
            }
            try {
                const auto pack = khdays::assets::extract_p2_subfile(
                    std::filesystem::path{argv[2]},
                    static_cast<std::size_t>(std::stoul(argv[3])));
                const auto pal = khdays::assets::find_nitro_resource(
                    pack.data(), pack.size(), "RLCN");
                const auto chr = khdays::assets::find_nitro_resource(
                    pack.data(), pack.size(), "RGCN");
                const auto cer = khdays::assets::find_nitro_resource(
                    pack.data(), pack.size(), "RECN");
                if (!pal || !chr || !cer) {
                    std::cerr << "ERROR: pack has no NCLR/NCGR/NCER sprites\n";
                    return EXIT_FAILURE;
                }
                const auto palette = khdays::assets::decode_nclr(pal.data, pal.size);
                const auto tiles = khdays::assets::decode_ncgr(chr.data, chr.size);
                const auto bank = khdays::assets::decode_ncer(cer.data, cer.size);
                const auto index = static_cast<std::size_t>(std::stoul(argv[4]));
                if (index >= bank.cells.size()) {
                    std::cerr << "ERROR: cell " << index << " out of range ("
                              << bank.cells.size() << ")\n";
                    return EXIT_FAILURE;
                }
                const auto image = khdays::assets::render_cell(
                    bank.cells[index], tiles, palette, bank.tile_boundary);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[5], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << bank.cells.size() << " cells (boundary "
                          << bank.tile_boundary << "), cell " << index << " -> "
                          << image.width << 'x' << image.height << " BMP: "
                          << argv[5] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--dump-ui") {
            // Exploration aid: parse a D2KP UI pack (a P2 sub-file) and dump
            // every non-blank screen x tile-sheet composition, to identify which
            // tilemap/tiles/palette form each background layer.
            if (argc != 5) {
                std::cerr << "ERROR: --dump-ui requires a P2 file, sub-file "
                             "index, and an output directory\n";
                return EXIT_FAILURE;
            }
            try {
                const auto blob = khdays::assets::extract_p2_subfile(
                    std::filesystem::path{argv[2]},
                    static_cast<std::size_t>(std::stoul(argv[3])));
                const auto pack =
                    khdays::assets::parse_pk2d(blob.data(), blob.size());
                std::cout << "D2KP: " << pack.palettes.size() << " NCLR, "
                          << pack.tiles.size() << " NCGR, " << pack.screens.size()
                          << " NSCR, " << pack.cells.size() << " NCER, "
                          << pack.anims.size() << " NANR\n";
                const std::filesystem::path out_dir{argv[4]};
                std::filesystem::create_directories(out_dir);
                for (std::size_t s = 0; s < pack.screens.size(); ++s) {
                    const auto map = khdays::assets::decode_nscr(
                        pack.screens[s].data, pack.screens[s].size);
                    for (std::size_t t = 0; t < pack.tiles.size(); ++t) {
                        const auto tiles = khdays::assets::decode_ncgr(
                            pack.tiles[t].data, pack.tiles[t].size);
                        for (std::size_t p = 0; p < pack.palettes.size(); ++p) {
                            const auto palette = khdays::assets::decode_nclr(
                                pack.palettes[p].data, pack.palettes[p].size);
                            const auto image = khdays::assets::compose_background(
                                map, tiles, palette, true);
                            std::size_t opaque = 0;
                            for (std::size_t k = 3; k < image.rgba.size(); k += 4) {
                                if (image.rgba[k] != 0) {
                                    ++opaque;
                                }
                            }
                            const double frac = image.rgba.empty() ? 0.0
                                : static_cast<double>(opaque)
                                    / (image.rgba.size() / 4.0);
                            if (frac < 0.02) {
                                continue;  // essentially blank pairing
                            }
                            const auto bmp = khdays::assets::to_bmp(image);
                            const auto path = out_dir
                                / ("s" + std::to_string(s) + "_t"
                                   + std::to_string(t) + "_p" + std::to_string(p)
                                   + ".bmp");
                            std::ofstream f{path, std::ios::binary};
                            f.write(reinterpret_cast<const char*>(bmp.data()),
                                    static_cast<std::streamsize>(bmp.size()));
                            std::cout << "  s" << s << " t" << t << " p" << p
                                      << "  " << image.width << 'x' << image.height
                                      << "  opaque=" << static_cast<int>(frac * 100)
                                      << "%\n";
                        }
                    }
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-text") {
            if (argc != 5) {
                std::cerr << "ERROR: --render-text requires NFTR, text, and an "
                             "output BMP\n";
                return EXIT_FAILURE;
            }
            try {
                const auto font =
                    khdays::resource::load_font(std::filesystem::path{argv[2]});
                const auto text = khdays::assets::message_from_utf8(argv[3]);
                const auto image = khdays::assets::render_text(font, text);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[4], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << "Font: " << font.glyphs.size() << " glyphs, "
                          << font.char_to_glyph.size() << " mapped, "
                          << font.cell_width << 'x' << font.cell_height << ' '
                          << font.bpp << "bpp -> " << image.width << 'x'
                          << image.height << " BMP: " << argv[4] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--extract-wav") {
            if (argc != 6) {
                std::cerr << "ERROR: --extract-wav requires SDAT, wave-archive "
                             "index, SWAV index, and an output path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto sdat =
                    khdays::assets::open_sdat(std::filesystem::path{argv[2]});
                const auto wave_archive =
                    static_cast<std::size_t>(std::stoul(argv[3]));
                const auto swav =
                    static_cast<std::size_t>(std::stoul(argv[4]));
                const auto audio =
                    khdays::assets::sdat_waveform(*sdat, wave_archive, swav);
                const auto wav = khdays::assets::to_wav(audio);
                std::ofstream out{argv[5], std::ios::binary};
                if (!out) {
                    std::cerr << "ERROR: cannot write " << argv[5] << '\n';
                    return EXIT_FAILURE;
                }
                out.write(
                    reinterpret_cast<const char*>(wav.data()),
                    static_cast<std::streamsize>(wav.size()));
                std::cout << "Decoded SWAV " << wave_archive << ':' << swav
                          << " -> " << audio.samples.size() << " samples @ "
                          << audio.sample_rate << " Hz ("
                          << (audio.sample_rate > 0
                                  ? static_cast<double>(audio.samples.size())
                                      / audio.sample_rate
                                  : 0.0)
                          << " s), wrote " << argv[5] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--play-sound") {
            if (argc != 5) {
                std::cerr << "ERROR: --play-sound requires SDAT, wave-archive "
                             "index, and SWAV index\n";
                return EXIT_FAILURE;
            }
            try {
                const auto sdat =
                    khdays::assets::open_sdat(std::filesystem::path{argv[2]});
                const auto audio = khdays::resource::load_sound(
                    *sdat,
                    static_cast<std::size_t>(std::stoul(argv[3])),
                    static_cast<std::size_t>(std::stoul(argv[4])));
                std::cout << "Playing SWAV " << argv[3] << ':' << argv[4]
                          << " (" << audio.samples.size() << " samples @ "
                          << audio.sample_rate << " Hz)" << std::endl;
                return khdays::platform::play_audio_blocking(audio);
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-sequence" || first == "--play-sequence") {
            const bool render = first == "--render-sequence";
            const int min_args = render ? 5 : 4;
            if (argc < min_args) {
                std::cerr << "ERROR: " << first << " requires an SDAT and a "
                             "sequence index"
                          << (render ? ", and an output path\n" : "\n");
                return EXIT_FAILURE;
            }
            try {
                const auto sdat =
                    khdays::assets::open_sdat(std::filesystem::path{argv[2]});
                const auto seq =
                    static_cast<std::size_t>(std::stoul(argv[3]));
                const int seconds_arg = render ? 5 : 4;
                const double seconds = argc > seconds_arg
                    ? std::stod(argv[seconds_arg])
                    : (render ? 180.0 : 30.0);
                const auto audio = khdays::resource::render_music(
                    *sdat, seq, 32768U, seconds);
                std::cout << "Synthesized sequence " << seq << ": "
                          << audio.samples.size() / 2U << " frames @ "
                          << audio.sample_rate << " Hz ("
                          << (audio.sample_rate > 0
                                  ? static_cast<double>(audio.samples.size() / 2U)
                                      / audio.sample_rate
                                  : 0.0)
                          << " s)" << std::endl;
                if (render) {
                    const auto wav = khdays::assets::to_wav(audio);
                    std::ofstream out{argv[4], std::ios::binary};
                    if (!out) {
                        std::cerr << "ERROR: cannot write " << argv[4] << '\n';
                        return EXIT_FAILURE;
                    }
                    out.write(
                        reinterpret_cast<const char*>(wav.data()),
                        static_cast<std::streamsize>(wav.size()));
                    std::cout << "Wrote " << argv[4] << '\n';
                    return EXIT_SUCCESS;
                }
                return khdays::platform::play_audio_blocking(audio);
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--message-info") {
            if (argc != 3) {
                std::cerr << "ERROR: --message-info requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto archive = khdays::resource::load_message_archive(
                    std::filesystem::path{argv[2]});
                std::cout << "P2 message container: "
                          << archive.subdbs.size() << " sub-databases, "
                          << archive.string_count() << " strings\n";
                for (std::size_t d = 0; d < archive.subdbs.size(); ++d) {
                    std::cout << "  sub-db " << d << ": "
                              << archive.subdbs[d].size() << " strings";
                    if (!archive.subdbs[d].empty()) {
                        std::cout << "  e.g. \""
                                  << khdays::assets::message_to_utf8(
                                         archive.subdbs[d].front())
                                  << "\"";
                    }
                    std::cout << '\n';
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--dump-messages") {
            if (argc != 3 && argc != 4) {
                std::cerr << "ERROR: --dump-messages requires a file path and "
                             "an optional sub-db index\n";
                return EXIT_FAILURE;
            }
            try {
                const auto archive = khdays::resource::load_message_archive(
                    std::filesystem::path{argv[2]});
                const bool one = argc == 4;
                const auto only = one
                    ? static_cast<std::size_t>(std::stoul(argv[3]))
                    : 0U;
                if (one && only >= archive.subdbs.size()) {
                    std::cerr << "ERROR: sub-db " << only << " out of range ("
                              << archive.subdbs.size() << ")\n";
                    return EXIT_FAILURE;
                }
                for (std::size_t d = 0; d < archive.subdbs.size(); ++d) {
                    if (one && d != only) {
                        continue;
                    }
                    for (std::size_t s = 0; s < archive.subdbs[d].size(); ++s) {
                        std::cout << '[' << d << ':' << s << "] "
                                  << khdays::assets::message_to_utf8(
                                         archive.subdbs[d][s])
                                  << '\n';
                    }
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--dump-strings") {
            if (argc != 3) {
                std::cerr << "ERROR: --dump-strings requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto strings = khdays::resource::load_string_table(
                    std::filesystem::path{argv[2]});
                std::cout << strings.size() << " strings\n";
                for (std::size_t i = 0; i < strings.size(); ++i) {
                    std::cout << '[' << i << "] "
                              << khdays::assets::message_to_utf8(strings[i])
                              << '\n';
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--anim-info") {
            if (argc != 3) {
                std::cerr << "ERROR: --anim-info requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto animation =
                    khdays::assets::load_nsbca(std::filesystem::path{argv[2]});
                std::size_t animated = 0;
                std::size_t rot_samples = 0;
                std::size_t rot_const = 0;
                std::size_t trans_samples = 0;
                std::size_t total_rot_keys = 0;
                for (const auto& bone : animation.bones) {
                    if (bone.animated) {
                        ++animated;
                    }
                    using RK = khdays::assets::AnimationRotationCurve::Kind;
                    if (bone.rotation.kind == RK::Samples) {
                        ++rot_samples;
                        total_rot_keys += bone.rotation.samples.size();
                    } else if (bone.rotation.kind == RK::Constant) {
                        ++rot_const;
                    }
                    using CK = khdays::assets::AnimationCurve::Kind;
                    for (const auto& t : bone.translation) {
                        if (t.kind == CK::Samples) {
                            ++trans_samples;
                        }
                    }
                }
                std::cout
                    << "Animation: " << animation.frame_count << " frames, "
                    << animation.bones.size() << " bones ("
                    << animated << " animated)\n"
                    << "  rotation curves: " << rot_samples << " sampled ("
                    << total_rot_keys << " keys total), " << rot_const
                    << " constant\n"
                    << "  translation sampled curves: " << trans_samples << "\n";
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--export-obj") {
            if (argc != 3 && argc != 4) {
                std::cerr
                    << "ERROR: --export-obj requires an input file and an "
                       "optional output path\n";
                return EXIT_FAILURE;
            }

            const std::filesystem::path input{argv[2]};
            const std::filesystem::path output =
                argc == 4
                    ? std::filesystem::path{argv[3]}
                    : std::filesystem::path{input}.replace_extension(".obj");

            try {
                const auto model =
                    khdays::assets::decode_model_geometry(input);
                const auto obj = khdays::assets::to_wavefront_obj(model);

                std::ofstream stream{output, std::ios::binary};
                if (!stream) {
                    std::cerr
                        << "ERROR: cannot write OBJ to " << output.string()
                        << '\n';
                    return EXIT_FAILURE;
                }
                stream.write(
                    obj.data(),
                    static_cast<std::streamsize>(obj.size()));

                std::size_t total_vertices = 0U;
                std::size_t total_triangles = 0U;
                for (const auto& mesh : model.meshes) {
                    total_vertices += mesh.vertices.size();
                    total_triangles += mesh.indices.size() / 3U;
                }

                std::cout
                    << "Decoded model '" << model.name << "' with "
                    << model.meshes.size() << " meshes, "
                    << total_vertices << " vertices ("
                    << model.header_vertex_count << " expected), "
                    << total_triangles << " triangles\n"
                    << "Wrote " << output.string() << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }
    }

    try {
        const auto options = parse_runtime_options(argc, argv);
        print_version();
        std::cout << "Starting the native SDL3 platform runtime..." << '\n';
        return khdays::platform::run_application(options);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        std::cerr << "Run khdays-port --help for usage." << '\n';
        return EXIT_FAILURE;
    }
}
