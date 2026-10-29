/* The plugin manager's test plugins (tests/plugins_test.c), one library per KHDAYS_TEST_PLUGIN:
 *   1 a plugin built for another SDK major,
 *   2 a plugin whose start fails,
 *   3 a plugin that counts the calls it gets (start, stop) in a file its test reads after it is released. */
#include <kh/kh_mod.h>

#include <stdio.h>

#if KHDAYS_TEST_PLUGIN == 1

static KhResult start(const KhHost *host)
{
    (void)host;
    return KH_OK;
}

static void stop(void) {}

static const KhPluginDescriptor descriptor = {
    sizeof(KhPluginDescriptor), KH_ABI_MAJOR + 1, 0, "other SDK", start, stop};

#elif KHDAYS_TEST_PLUGIN == 2

static KhResult start(const KhHost *host)
{
    host->log(host->plugin, KH_LOG_ERROR, "start fails on purpose");
    return KH_ERROR;
}

static void stop(void) {}

static const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("failing start", start, stop);

#else

static const KhHost *given;

static KhResult start(const KhHost *host)
{
    given = host;
    host->log(host->plugin, KH_LOG_INFO, "counting");
    return KH_OK;
}

/* its last word: written where the test looks once the plugin is gone */
static void stop(void)
{
    FILE *f = fopen("plugin_stopped.txt", "w");
    if (f != NULL) {
        fputs("stopped once", f);
        fclose(f);
    }
    given->log(given->plugin, KH_LOG_INFO, "stopping");
}

static const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("counting", start, stop);

#endif

KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
