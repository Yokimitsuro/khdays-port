#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_main.h>

#include "khdays/assets/animation.h"
#include "khdays/assets/audio.h"
#include "khdays/assets/mdl0.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h"
#include "khdays/assets/sdat.h"
#include "khdays/platform/audio.h"
#include "khdays/platform/runtime.h"
#include "khdays/port.h"
#include "khdays/resource/loader.h"

#ifndef KHDAYS_PORT_VERSION
#define KHDAYS_PORT_VERSION "unknown"
#endif

namespace {

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
        << "  khdays-port --extract-wav SDAT WAVEARCHIVE SWAV OUTPUT.wav\n"
        << "  khdays-port --play-sound SDAT WAVEARCHIVE SWAV\n"
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
        << "  --audio-info FILE   List the contents of an SDAT sound archive.\n"
        << "  --extract-wav SDAT WAVEARCHIVE SWAV OUTPUT.wav  Decode a SWAV waveform to WAV.\n"
        << "  --play-sound SDAT WAVEARCHIVE SWAV  Decode and play a SWAV waveform.\n"
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
