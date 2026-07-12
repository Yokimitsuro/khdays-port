#include <cstdlib>
#include <iostream>
#include <string_view>

#include <SDL3/SDL_main.h>

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

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string_view{argv[1]} == "--version") {
        print_version();
        return EXIT_SUCCESS;
    }

    print_version();
    std::cout << "Starting the native SDL3 platform runtime..." << '\n';

    return khdays::platform::run_application();
}
