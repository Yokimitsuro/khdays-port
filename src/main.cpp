#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_main.h>

#include "khdays/assets/animation.h"
#include "khdays/assets/mdl0.h"
#include "khdays/assets/mesh.h"
#include "khdays/platform/runtime.h"
#include "khdays/port.h"

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
        << "  khdays-port --render-model FILE\n"
        << "  khdays-port --model-info FILE\n"
        << "  khdays-port --anim-info FILE\n"
        << "  khdays-port --export-obj FILE [OUTPUT.obj]\n"
        << "  khdays-port --version\n"
        << "  khdays-port --help\n"
        << '\n'
        << "Options:\n"
        << "  --resource FILE     Load TEX0 data from a user-extracted NSBMD/NSBTX.\n"
        << "  --texture NAME      Select a texture by name; defaults to the first.\n"
        << "  --render-model FILE Render an MDL0 model in 3D in the native window.\n"
        << "  --model-info FILE   Inspect MDL0 models, materials, meshes, and GPU commands.\n"
        << "  --anim-info FILE    Inspect an NSBCA skeletal animation.\n"
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

        throw std::invalid_argument(
            "unknown argument: " + std::string{argument});
    }

    if (options.texture_name.has_value() && !options.resource_path.has_value()) {
        throw std::invalid_argument(
            "--texture can only be used together with --resource");
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
