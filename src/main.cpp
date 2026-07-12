#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_main.h>

#include "khdays/assets/mdl0.h"
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
        << "  khdays-port --model-info FILE\n"
        << "  khdays-port --version\n"
        << "  khdays-port --help\n"
        << '\n'
        << "Options:\n"
        << "  --resource FILE   Load TEX0 data from a user-extracted NSBMD/NSBTX.\n"
        << "  --texture NAME    Select a texture by name; defaults to the first.\n"
        << "  --model-info FILE Inspect MDL0 models, materials, meshes, and GPU commands.\n"
        << "  --version         Print version information without opening a window.\n"
        << "  --help            Show this help text.\n";
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
