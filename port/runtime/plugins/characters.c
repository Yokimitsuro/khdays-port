/* kh.characters for native plugins (sdk/include/kh/kh_characters.h): the game's characters and the mods'
 * (runtime/characters), and listeners called for the actors that play one. Each frame the four seats' actors are
 * looked at: one that appeared has entered, one gone has left; an attack is the join's (khdays_plugins_character_
 * attack), told once as it starts. */
#include "actors.h"
#include "interfaces.h"

#include "../characters/characters.h"
#include "../core/events.h"
#include "../scripts/game_link.h"

#include <kh/kh_characters.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEATS 4 /* the game's (data_020429c8) */
/* An attack's hits are tested each battle tick (30 a second in a room, 60 alone): a seat whose attack went untested
 * longer than this many frames has ended it, and the same attack again is another one. */
#define ATTACK_GAP 3u

typedef struct Listener {
    KhPlugin *owner;
    int number;
    KhCharacterCalls calls; /* the plugin's, the fields past its size left NULL */
    void *user;
    KhCharacterListener id;
} Listener;

static Listener *listeners;
static int listener_count, listener_room;
static KhCharacterListener next_id = 1;
static int delivering; /* listeners being called: the ones removed are only marked (owner NULL) */
static int listeners_dead;

/* What each seat had at the last look. */
static struct {
    KhActor actor;
    int number;
    int attack; /* -1 none */
    unsigned attack_frame;
} seats[SEATS];

static void drop_dead_listeners(void)
{
    int kept = 0;
    for (int i = 0; i < listener_count; ++i) {
        if (listeners[i].owner != NULL) {
            listeners[kept++] = listeners[i];
        }
    }
    listener_count = kept;
    listeners_dead = 0;
}

typedef enum Call { ENTERED, FRAME, ATTACK, LEFT } Call;

/* Each listener of character `number` called, one at a time by its place (a call may add or remove listeners);
 * `value` is the seat (ENTERED) or the attack (ATTACK). */
static void tell(int number, Call call, KhActor actor, int value)
{
    ++delivering;
    for (int i = 0; i < listener_count; ++i) {
        const Listener l = listeners[i];
        KhPlugin *was;
        if (l.owner == NULL || l.number != number) {
            continue;
        }
        was = khdays_plugins_calling(l.owner);
        switch (call) {
        case ENTERED:
            if (l.calls.entered != NULL) {
                l.calls.entered(actor, value, l.user);
            }
            break;
        case FRAME:
            if (l.calls.frame != NULL) {
                l.calls.frame(actor, l.user);
            }
            break;
        case ATTACK:
            if (l.calls.attack != NULL) {
                l.calls.attack(actor, value, l.user);
            }
            break;
        case LEFT:
            if (l.calls.left != NULL) {
                l.calls.left(actor, l.user);
            }
            break;
        }
        khdays_plugins_calling(was);
    }
    if (--delivering == 0 && listeners_dead) {
        drop_dead_listeners();
    }
}

/* The character a seat's actor plays, -1 without one. */
static int number_of(int seat)
{
    const unsigned actor = khdays_link_seat_actor(seat);
    return actor != 0 ? khdays_characters_of(seat, khdays_link_actor_kind(actor)) : -1;
}

/* Seat `seat` looked at: the actor that left told, the one that entered told. */
static void look_at(int seat)
{
    const KhActor actor = khdays_actors_player(seat);
    if (actor == seats[seat].actor) {
        return;
    }
    if (seats[seat].actor != 0) {
        tell(seats[seat].number, LEFT, seats[seat].actor, 0);
    }
    seats[seat].actor = actor;
    seats[seat].number = actor != 0 ? number_of(seat) : -1;
    seats[seat].attack = -1;
    if (actor != 0) {
        tell(seats[seat].number, ENTERED, actor, seat);
    }
}

void khdays_plugins_characters_frame(void)
{
    const unsigned frame = khdays_events_vblank_count();
    if (listener_count == 0) {
        return;
    }
    for (int seat = 0; seat < SEATS; ++seat) {
        look_at(seat);
        if (seats[seat].actor == 0) {
            continue;
        }
        if (seats[seat].attack >= 0 && frame - seats[seat].attack_frame > ATTACK_GAP) {
            seats[seat].attack = -1;
        }
        tell(seats[seat].number, FRAME, seats[seat].actor, 0);
    }
}

void khdays_plugins_character_attack(unsigned actor, int seat, int attack)
{
    if (listener_count == 0 || seat < 0 || seat >= SEATS || khdays_link_seat_actor(seat) != actor) {
        return;
    }
    look_at(seat); /* the seat's actor may attack before the frame's look */
    seats[seat].attack_frame = khdays_events_vblank_count();
    if (seats[seat].actor != 0 && attack != seats[seat].attack) {
        seats[seat].attack = attack;
        tell(seats[seat].number, ATTACK, seats[seat].actor, attack);
    }
}

/* The plugins that play a clip on a character (play), by number: theirs stop with them and as the scene changes. */
typedef struct Player {
    int number;
    KhPlugin *owner;
} Player;

static Player *players;
static int player_count, player_room;

static void stop_clips(KhPlugin *owner)
{
    for (int i = player_count - 1; i >= 0; --i) {
        if (owner == NULL || players[i].owner == owner) {
            khdays_characters_play_none(players[i].number);
            players[i] = players[--player_count];
        }
    }
}

void khdays_plugins_scene(int scene)
{
    (void)scene; /* any change ends the field's actors */
    khdays_actors_area_left();
    stop_clips(NULL);
    for (int seat = 0; seat < SEATS; ++seat) {
        if (seats[seat].actor != 0) {
            const KhActor gone = seats[seat].actor;
            seats[seat].actor = 0;
            tell(seats[seat].number, LEFT, gone, 0);
        }
    }
}

/* --- The interface ------------------------------------------------------------------------------------------------ */

static int32_t count(void)
{
    return KHDAYS_CHARACTER_BASES + khdays_characters_count();
}

static KhResult info(int32_t index, KhCharacterInfo *out)
{
    KhCharacterInfo i = {0};
    if (out == NULL || out->size < sizeof(uint32_t) || index < 0 || index >= count()) {
        return KH_ERROR;
    }
    i.size = out->size < sizeof(i) ? out->size : (uint32_t)sizeof(i);
    if (index < KHDAYS_CHARACTER_BASES) {
        i.number = i.base = index;
        i.key = i.name = khdays_character_base_name(index);
    } else {
        const KhdaysCharacter *c = khdays_characters_at(index - KHDAYS_CHARACTER_BASES);
        const int language = khdays_link_language() - 1;
        i.number = c->number;
        i.base = c->base;
        i.key = c->key;
        i.identity = c->identity;
        i.name = language >= 0 && language < KHDAYS_CHARACTER_LANGUAGES && c->name[language] != NULL ? c->name[language]
                 : c->any_name != NULL                                                               ? c->any_name
                                                                                                     : c->key;
    }
    memcpy(out, &i, i.size);
    return KH_OK;
}

static int32_t find(KhPlugin *plugin, const char *key)
{
    char own[256];
    if (key == NULL || key[0] == '\0') {
        return -1;
    }
    snprintf(own, sizeof(own), "%s/%s", plugin != NULL ? khdays_plugins_mod_id(plugin) : "", key);
    for (int i = 0; i < khdays_characters_count(); ++i) {
        const KhdaysCharacter *c = khdays_characters_at(i);
        if (strcmp(c->identity, key) == 0 || strcmp(c->identity, own) == 0) {
            return c->number;
        }
    }
    for (int n = 0; n < KHDAYS_CHARACTER_BASES; ++n) {
        if (strcmp(khdays_character_base_name(n), key) == 0) {
            return n;
        }
    }
    return -1;
}

static KhCharacterListener listen(KhPlugin *plugin, int32_t number, const KhCharacterCalls *calls, void *user)
{
    Listener l = {0};
    char text[160];
    if (plugin == NULL || calls == NULL || calls->size < sizeof(uint32_t)) {
        return 0;
    }
    if ((number < 0 || number >= KHDAYS_CHARACTER_BASES) && khdays_characters_numbered(number) == NULL) {
        snprintf(text, sizeof(text), "not listening: no character %d", (int)number);
        khdays_plugins_tell(plugin, text);
        return 0;
    }
    if (listener_count == listener_room) {
        const int room = listener_room > 0 ? listener_room * 2 : 8;
        Listener *grown = realloc(listeners, (size_t)room * sizeof(Listener));
        if (grown == NULL) {
            return 0;
        }
        listeners = grown;
        listener_room = room;
    }
    l.owner = plugin;
    l.number = number;
    memcpy(&l.calls, calls, calls->size < sizeof(l.calls) ? calls->size : sizeof(l.calls));
    l.user = user;
    l.id = next_id++;
    listeners[listener_count++] = l;
    if (listener_count == 1) {
        memset(seats, 0, sizeof(seats)); /* the first listener sees the seats' actors enter */
    }
    return l.id;
}

static void forget_listener(int i)
{
    listeners[i].owner = NULL;
    listeners_dead = 1;
}

static void unlisten(KhPlugin *plugin, KhCharacterListener listener)
{
    for (int i = 0; i < listener_count; ++i) {
        if (listeners[i].owner == plugin && listeners[i].id == listener) {
            forget_listener(i);
        }
    }
    if (delivering == 0 && listeners_dead) {
        drop_dead_listeners();
    }
}

static KhResult play(KhPlugin *plugin, KhActor actor, const char *action, uint32_t flags)
{
    const KhActors *actors = khdays_plugins_actors();
    const int number = actors->kind(actor) == KH_ACTOR_PLAYER ? actors->character(actor) : -1;
    int i = 0;
    if (plugin == NULL || action == NULL || !khdays_plugins_may_change(plugin) ||
        !khdays_characters_play(number, action, (flags & KH_PLAY_LOOP) != 0, khdays_events_vblank_count())) {
        return KH_UNSUPPORTED;
    }
    while (i < player_count && players[i].number != number) {
        ++i;
    }
    if (i == player_count) {
        if (player_count == player_room) {
            const int room = player_room > 0 ? player_room * 2 : 4;
            Player *grown = realloc(players, (size_t)room * sizeof(Player));
            if (grown == NULL) {
                khdays_characters_play_none(number);
                return KH_ERROR;
            }
            players = grown;
            player_room = room;
        }
        ++player_count;
    }
    players[i].number = number;
    players[i].owner = plugin;
    return KH_OK;
}

static void stop_playing(KhPlugin *plugin, KhActor actor)
{
    const KhActors *actors = khdays_plugins_actors();
    const int number = actors->kind(actor) == KH_ACTOR_PLAYER ? actors->character(actor) : -1;
    for (int i = player_count - 1; i >= 0; --i) {
        if (players[i].number == number && players[i].owner == plugin) {
            khdays_characters_play_none(number);
            players[i] = players[--player_count];
        }
    }
}

void khdays_plugins_characters_forget(KhPlugin *plugin)
{
    stop_clips(plugin);
    for (int i = 0; i < listener_count; ++i) {
        if (listeners[i].owner == plugin) {
            forget_listener(i);
        }
    }
    if (delivering == 0 && listeners_dead) {
        drop_dead_listeners();
    }
}

static const KhCharacters characters_table = {sizeof(KhCharacters), count, info, find, listen, unlisten, play,
                                              stop_playing};

const void *khdays_plugins_characters(void)
{
    return &characters_table;
}
