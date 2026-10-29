/* A plugin built into the plugins' test (tests/plugins_test.c) as KHDAYS_STATIC_PLUGINS builds one into the port, for
 * a platform that loads no library: the same source, its kh_plugin_query renamed, listed by its mod's id. */
#define kh_plugin_query khdays_test_static_query
#include <kh/kh_mod.h>

static const KhHost *host;

static KhResult start(const KhHost *given)
{
    host = given;
    host->log(host->plugin, KH_LOG_INFO, "built in, started");
    return KH_OK;
}

static void stop(void)
{
    host->log(host->plugin, KH_LOG_INFO, "built in, stopped");
}

static const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("built in", start, stop);

KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
