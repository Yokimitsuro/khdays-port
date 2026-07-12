#pragma once

namespace khdays::platform {

// Initializes the native platform runtime and runs the window/event loop.
// Returns EXIT_SUCCESS on a clean exit and EXIT_FAILURE after an SDL error.
int run_application();

}  // namespace khdays::platform
