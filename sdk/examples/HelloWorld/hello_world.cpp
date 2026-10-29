// The KH Mod SDK's first example: a plugin that checks the port it runs in, says hello in the logs and goodbye when
// the game ends. Build it with the SDK's CMake (sdk/CMakeLists.txt) and install it with its mod.ini.
#include <kh/kh_mod.h>

#include <exception>
#include <string>

namespace {

const KhHost *host = nullptr;

void say(KhLogLevel level, const std::string &text)
{
    if (host != nullptr)
        host->log(host->plugin, level, text.c_str());
}

KhResult start(const KhHost *given)
{
    // nothing of C++ may leave through the boundary: an exception ends here
    try {
        host = given;
        if (given->abi_major != KH_ABI_MAJOR) {
            return KH_ERROR;
        }
        say(KH_LOG_INFO, std::string("Hello from a native plugin, running in port ") + given->port_version + " (ABI " +
                             std::to_string(given->abi_major) + "." + std::to_string(given->abi_minor) + ")");
        // an interface this port does not have answers NULL
        if (given->find(given->plugin, "kh.example.missing", 1) == nullptr)
            say(KH_LOG_INFO, "asked for an interface the port has not got: none, as expected");
        return KH_OK;
    } catch (const std::exception &e) {
        say(KH_LOG_ERROR, e.what());
        return KH_ERROR;
    }
}

void stop()
{
    try {
        say(KH_LOG_INFO, "Goodbye: stopped before release");
    } catch (...) {
    }
    host = nullptr;
}

const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("Hello World", start, stop);

} // namespace

extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
