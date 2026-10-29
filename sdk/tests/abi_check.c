/* The SDK's boundary checked from both sides on any platform (tests/load_plugin.py is the host): a plugin with no C
 * library -- it calls only what the host hands it -- that checks the host table, logs through it, asks for an
 * interface the host has not got, and logs again as it stops. Built for Linux on any machine with the SDK's
 * cmake/toolchain-linux-x64-clang.cmake (no Linux system needed: it uses nothing of one). */
#include <kh/kh_mod.h>

static const KhHost *host;

static KhResult start(const KhHost *given)
{
    if (given == 0 || given->size < sizeof(KhHost) || given->abi_major != KH_ABI_MAJOR) {
        return KH_ERROR;
    }
    host = given;
    host->log(host->plugin, KH_LOG_INFO, "abi check started");
    if (host->find(host->plugin, "kh.not-there", 1) != 0) {
        return KH_ERROR;
    }
    host->log(host->plugin, KH_LOG_WARNING, "abi check: an interface the host has not got answered none");
    return KH_OK;
}

static void stop(void)
{
    host->log(host->plugin, KH_LOG_INFO, "abi check stopped");
}

static const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("abi check", start, stop);

KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
