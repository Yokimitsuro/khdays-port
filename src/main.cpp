#include <cstdlib>
#include <iostream>

#include "khdays/port.h"

#ifndef KHDAYS_PORT_VERSION
#define KHDAYS_PORT_VERSION "unknown"
#endif

int main() {
    std::cout
        << khdays::port::Version::name << " " << KHDAYS_PORT_VERSION << '\n'
        << "Stage: " << khdays::port::Version::stage << '\n'
        << "The native runtime is not playable yet." << '\n';

    return EXIT_SUCCESS;
}
