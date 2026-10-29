/* The plugins' test of kh.actors and kh.characters (tests/plugins_test.c), library 6 (test.fighter): finds its mod's
 * character "block" and the game's roxas by name, refuses a character that is not there, and listens to "block":
 * each call says what it saw (its seat, its character, its HP and place, the standing enemies, an attack healing
 * it by 1, the handle refused once it has left) in the log, which the test reads. */
#include <kh/kh_actors.h>
#include <kh/kh_characters.h>
#include <kh/kh_events.h>
#include <kh/kh_net.h>
#include <kh/kh_parameters.h>

#include <stdio.h>

static const KhHost *host;
static const KhActors *actors;
static const KhCharacters *characters;
static const KhNet *net;
static int frames;

static void say(const char *text)
{
    host->log(host->plugin, KH_LOG_INFO, text);
}

static void entered(KhActor actor, int32_t seat, void *user)
{
    char text[200];
    float at[3] = {0};
    int32_t hp = 0, max = 0;
    (void)user;
    actors->hp(actor, &hp, &max);
    actors->position(actor, at);
    snprintf(text, sizeof(text), "entered seat %d as %d (seat %d, kind %d), HP %d/%d at %.1f %.1f %.1f", (int)seat,
             (int)actors->character(actor), (int)actors->seat(actor), (int)actors->kind(actor), (int)hp, (int)max,
             at[0], at[1], at[2]);
    say(text);
}

static void frame(KhActor actor, void *user)
{
    (void)user;
    (void)actor;
    if (frames++ == 0) {
        char text[200];
        KhActor first = 0;
        int32_t hp = 0, max = 0;
        const int32_t n = actors->enemies(&first, 1);
        actors->hp(first, &hp, &max);
        snprintf(text, sizeof(text), "enemies %d, the first of kind %d, HP %d/%d", (int)n,
                 (int)actors->character(first), (int)hp, (int)max);
        say(text);
    }
}

static void attack(KhActor actor, int32_t which, void *user)
{
    char text[100];
    int32_t hp = 0;
    (void)user;
    actors->hp(actor, &hp, NULL);
    snprintf(text, sizeof(text), "attack %d, healed: %s", (int)which,
             actors->set_hp(actor, hp + 1) == KH_OK ? "yes" : "no");
    say(text);
    if (net->in_room()) {
        snprintf(text, sizeof(text), "sent struck: %d", (int)net->send(host->plugin, "struck", 3, &which, 4));
        say(text);
    }
}

/* the message test.fighter.struck, from the host's copy */
static void struck(const KhEvent *event, void *user)
{
    char text[100];
    int32_t which = -1;
    (void)user;
    if (event->data_size == 4) {
        which = *(const int32_t *)event->data;
    }
    snprintf(text, sizeof(text), "heard struck v%d: %d", (int)event->a, (int)which);
    say(text);
}

static void left(KhActor actor, void *user)
{
    char text[100];
    (void)user;
    snprintf(text, sizeof(text), "left after %d frames, its handle %s", frames,
             actors->hp(actor, NULL, NULL) == KH_ERROR && actors->kind(actor) == KH_ACTOR_NONE ? "refused"
                                                                                               : "answered");
    say(text);
}

static const KhCharacterCalls calls = {sizeof(KhCharacterCalls), entered, frame, attack, left};

static KhResult start(const KhHost *given)
{
    char text[200];
    int32_t block;
    KhCharacterInfo info = {sizeof(KhCharacterInfo), 0, 0, NULL, NULL, NULL};
    host = given;
    actors = host->find(host->plugin, KH_ACTORS, KH_ACTORS_VERSION);
    characters = host->find(host->plugin, KH_CHARACTERS, KH_CHARACTERS_VERSION);
    net = host->find(host->plugin, KH_NET, KH_NET_VERSION);
    if (actors == NULL || characters == NULL || net == NULL) {
        return KH_ERROR;
    }
    ((const KhEvents *)host->find(host->plugin, KH_EVENTS, KH_EVENTS_VERSION))
        ->subscribe(host->plugin, "test.fighter.struck", struck, NULL);
    block = characters->find(host->plugin, "block");
    characters->info(characters->count() - 1, &info);
    snprintf(text, sizeof(text), "block %d, roxas %d, by identity %d, nobody %d; %d characters, the last %s (%s) on %d",
             (int)block, (int)characters->find(host->plugin, "roxas"),
             (int)characters->find(host->plugin, "test.fighter/block"), (int)characters->find(host->plugin, "nobody"),
             (int)characters->count(), info.identity, info.name, (int)info.base);
    say(text);
    say(characters->listen(host->plugin, 999, &calls, NULL) == 0 ? "999 refused" : "999 listened");
    {
        const KhParameters *p = host->find(host->plugin, KH_PARAMETERS, KH_PARAMETERS_VERSION);
        snprintf(text, sizeof(text), "parameters: spins %d, damage %.2f, loud %d, sound %s (%d), name %s, missing %d",
                 (int)p->integer(host->plugin, "spins", -1), p->number(host->plugin, "damage", -1.0),
                 (int)p->integer(host->plugin, "loud", -1), p->text(host->plugin, "sound", "?"),
                 (int)p->integer(host->plugin, "sound", -1), p->text(host->plugin, "name", "?"),
                 (int)p->integer(host->plugin, "nothing", -7));
        say(text);
    }
    return characters->listen(host->plugin, block, &calls, NULL) != 0 ? KH_OK : KH_ERROR;
}

static void stop(void) {}

static const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("fighter", start, stop);

KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
