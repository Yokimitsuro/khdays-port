/* The plugins' test of the universal core (tests/plugins_test.c), one library per KHDAYS_TEST_PLUGIN:
 *   4 test.provider: publishes a service, hears "frame" and test.user's own event, hooks TestPoint (before: skips
 *     the routine giving 7 when its first argument is 99; after: adds 1 to the result), declaring it replaces it;
 *   5 test.user (requires test.provider): uses the service, raises its own event, hooks TestPoint before at a
 *     higher priority (doubles the second argument), declaring it replaces it too (a conflict, told), reaches a
 *     routine and a datum by name.
 * Each says what it saw in the log, which the test reads. */
#include <kh/kh_engine.h>
#include <kh/kh_events.h>
#include <kh/kh_hooks.h>
#include <kh/kh_services.h>

#include <stdio.h>
#include <string.h>

typedef struct Math {
    uint32_t size;
    int (*add)(int a, int b);
} Math;

static const KhHost *host;

static void say(const char *text)
{
    host->log(host->plugin, KH_LOG_INFO, text);
}

#if KHDAYS_TEST_PLUGIN == 4

static int add(int a, int b)
{
    return a + b;
}

static const Math math = {sizeof(Math), add};

static void heard(const KhEvent *event, void *user)
{
    char text[200];
    (void)user;
    if (strcmp(event->name, "frame") == 0) {
        snprintf(text, sizeof(text), "frame %lld", (long long)event->a);
    } else {
        snprintf(text, sizeof(text), "%s %.*s", event->name, (int)event->data_size, (const char *)event->data);
    }
    say(text);
}

static void before(KhHookCall *call, void *user)
{
    (void)user;
    if (call->args[0] == 99) {
        call->result = 7;
        call->skip = 1;
    }
}

static void after(KhHookCall *call, void *user)
{
    (void)user;
    call->result += 1;
}

static KhResult start(const KhHost *given)
{
    const KhServices *services;
    const KhEvents *events;
    const KhHooks *hooks;
    host = given;
    services = host->find(host->plugin, KH_SERVICES, KH_SERVICES_VERSION);
    events = host->find(host->plugin, KH_EVENTS, KH_EVENTS_VERSION);
    hooks = host->find(host->plugin, KH_HOOKS, KH_HOOKS_VERSION);
    if (services == NULL || events == NULL || hooks == NULL) {
        return KH_ERROR;
    }
    if (services->publish(host->plugin, "test.provider.math", 1, &math) != KH_OK) {
        return KH_ERROR;
    }
    events->subscribe(host->plugin, "frame", heard, NULL);
    events->subscribe(host->plugin, "test.user.ping", heard, NULL);
    hooks->add(host->plugin, "TestPoint", KH_HOOK_BEFORE, 10, KH_HOOK_REPLACES, before, NULL);
    hooks->add(host->plugin, "TestPoint", KH_HOOK_AFTER, 0, 0, after, NULL);
    say(hooks->add(host->plugin, "NoSuchPoint", KH_HOOK_BEFORE, 0, 0, before, NULL) == 0 ? "no such point refused"
                                                                                         : "no such point added");
    return KH_OK;
}

static void stop(void) {}

static const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("provider", start, stop);

#else

static void doubles(KhHookCall *call, void *user)
{
    (void)user;
    call->args[1] *= 2;
}

static KhResult start(const KhHost *given)
{
    char text[200];
    const Math *m;
    const KhEvents *events;
    const KhHooks *hooks;
    const KhEngine *engine;
    int32_t module = 0;
    host = given;
    m = host->find(host->plugin, "test.provider.math", 1);
    events = host->find(host->plugin, KH_EVENTS, KH_EVENTS_VERSION);
    hooks = host->find(host->plugin, KH_HOOKS, KH_HOOKS_VERSION);
    engine = host->find(host->plugin, KH_ENGINE, KH_ENGINE_VERSION);
    if (m == NULL || events == NULL || hooks == NULL || engine == NULL) {
        return KH_ERROR;
    }
    snprintf(text, sizeof(text), "math %d", m->add(2, 3));
    say(text);
    say(host->find(host->plugin, "test.provider.math", 2) == NULL ? "no math 2" : "math 2 found");
    events->raise(host->plugin, "test.user.ping", "hello", 5);
    say(events->raise(host->plugin, "frame", NULL, 0) == KH_ERROR ? "frame refused" : "frame raised");
    hooks->add(host->plugin, "TestPoint", KH_HOOK_BEFORE, 20, KH_HOOK_REPLACES, doubles, NULL);
    {
        int (*routine)(void) = (int (*)(void))engine->routine("TestRoutine", &module);
        const KhRef ref = engine->datum("gTestDatum");
        const int *value = engine->pointer(ref);
        snprintf(text, sizeof(text), "routine %d in module %d, datum %d, none %p, back %s",
                 routine != NULL ? routine() : -1, (int)module, value != NULL ? *value : -1,
                 engine->routine("NoSuchRoutine", NULL), engine->reference(value) == ref ? "same" : "other");
        say(text);
    }
    return KH_OK;
}

static void stop(void) {}

static const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("user", start, stop);

#endif

KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
