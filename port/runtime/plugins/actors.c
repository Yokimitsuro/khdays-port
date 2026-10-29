/* kh.actors for native plugins (sdk/include/kh/kh_actors.h): the field's players and standing enemies as handles.
 * A handle is an entry's place and that entry's generation. An entry ends when the game is done with its actor (an
 * enemy no longer standing: defeated, retired, sent away; every actor at its area's end or a change of scene) and a
 * new one for the same actor takes a new generation, so that an ended handle never reaches the actor made again in
 * its place. Each use also checks that the game still has the actor where the entry found it. */
#include "actors.h"
#include "interfaces.h"

#include "../characters/characters.h"
#include "../core/window.h"
#include "../scripts/enemies_link.h"
#include "../scripts/game_link.h"

#include <kh/kh_actors.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEATS 4 /* the game's (data_020429c8) */

typedef struct Entry {
    unsigned actor;      /* its game reference */
    int kind;            /* KhActorKind: KH_ACTOR_NONE once ended */
    int seat;            /* a player's */
    unsigned generation; /* from 1, raised as the entry is used again */
} Entry;

static Entry *entries;
static int entry_count, entry_room;

static KhActor handle_of(int index)
{
    return ((KhActor)entries[index].generation << 32) | (KhActor)(unsigned)(index + 1);
}

/* Whether the game still has the entry's actor where it was found. */
static int still_there(const Entry *e)
{
    return e->kind == KH_ACTOR_PLAYER ? khdays_link_seat_actor(e->seat) == e->actor
                                      : khdays_enemies_state(e->actor, NULL);
}

static Entry *entry_of(KhActor handle)
{
    const unsigned index = (unsigned)(handle & 0xffffffffu) - 1u;
    Entry *e;
    if (handle == 0 || index >= (unsigned)entry_count) {
        return NULL;
    }
    e = &entries[index];
    if (e->kind == KH_ACTOR_NONE || e->generation != (unsigned)(handle >> 32)) {
        return NULL;
    }
    if (!still_there(e)) {
        e->kind = KH_ACTOR_NONE;
        return NULL;
    }
    return e;
}

/* The handle of `actor`: its entry's while it lives, else a new one; 0 when no room could be made. */
static KhActor meet(unsigned actor, int kind, int seat)
{
    int free_one = -1;
    for (int i = 0; i < entry_count; ++i) {
        const Entry *e = &entries[i];
        if (e->kind == kind && e->actor == actor && e->seat == seat) {
            return handle_of(i);
        }
        if (e->kind == KH_ACTOR_NONE && free_one < 0) {
            free_one = i;
        }
    }
    if (free_one < 0) {
        if (entry_count == entry_room) {
            const int room = entry_room > 0 ? entry_room * 2 : 16;
            Entry *grown = realloc(entries, (size_t)room * sizeof(Entry));
            if (grown == NULL) {
                return 0;
            }
            entries = grown;
            entry_room = room;
        }
        free_one = entry_count++;
        entries[free_one].generation = 0;
    }
    entries[free_one].actor = actor;
    entries[free_one].kind = kind;
    entries[free_one].seat = seat;
    ++entries[free_one].generation;
    return handle_of(free_one);
}

void khdays_actors_ended(unsigned actor)
{
    for (int i = 0; i < entry_count; ++i) {
        if (entries[i].actor == actor) {
            entries[i].kind = KH_ACTOR_NONE;
        }
    }
}

void khdays_actors_area_left(void)
{
    for (int i = 0; i < entry_count; ++i) {
        entries[i].kind = KH_ACTOR_NONE;
    }
}

void khdays_actors_frame(void)
{
    /* an enemy that stopped standing ends now, before a later wave sets the same actor going again */
    for (int i = 0; i < entry_count; ++i) {
        if (entries[i].kind == KH_ACTOR_ENEMY && !still_there(&entries[i])) {
            entries[i].kind = KH_ACTOR_NONE;
        }
    }
}

static KhActor player(int32_t seat)
{
    const unsigned actor = khdays_link_seat_actor(seat);
    return actor != 0 ? meet(actor, KH_ACTOR_PLAYER, seat) : 0;
}

KhActor khdays_actors_player(int seat)
{
    return player(seat);
}

static int32_t local_seat(void)
{
    return khdays_link_local_seat();
}

static int32_t enemies(KhActor *out, int32_t room)
{
    static unsigned *found;
    static int found_room;
    const int n = khdays_enemies_list(NULL, 0);
    if (out == NULL || room <= 0 || n == 0) {
        return n;
    }
    if (n > found_room) {
        unsigned *grown = realloc(found, (size_t)n * sizeof(unsigned));
        if (grown == NULL) {
            return 0;
        }
        found = grown;
        found_room = n;
    }
    khdays_enemies_list(found, n);
    for (int i = 0; i < n && i < room; ++i) {
        out[i] = meet(found[i], KH_ACTOR_ENEMY, -1);
    }
    return n;
}

static KhActor of(KhRef ref)
{
    if (ref == 0) {
        return 0;
    }
    for (int seat = 0; seat < SEATS; ++seat) {
        if (khdays_link_seat_actor(seat) == ref) {
            return meet(ref, KH_ACTOR_PLAYER, seat);
        }
    }
    return khdays_enemies_state(ref, NULL) ? meet(ref, KH_ACTOR_ENEMY, -1) : 0;
}

static int32_t kind(KhActor actor)
{
    const Entry *e = entry_of(actor);
    return e != NULL ? e->kind : KH_ACTOR_NONE;
}

static KhRef ref(KhActor actor)
{
    const Entry *e = entry_of(actor);
    return e != NULL ? e->actor : 0;
}

static int32_t seat(KhActor actor)
{
    const Entry *e = entry_of(actor);
    return e != NULL && e->kind == KH_ACTOR_PLAYER ? e->seat : -1;
}

static int32_t character(KhActor actor)
{
    const Entry *e = entry_of(actor);
    KhdaysEnemyInfo info;
    if (e == NULL) {
        return -1;
    }
    if (e->kind == KH_ACTOR_PLAYER) {
        return khdays_characters_of(e->seat, khdays_link_actor_kind(e->actor));
    }
    return khdays_enemies_state(e->actor, &info) ? info.kind : -1;
}

static KhResult position(KhActor actor, float at[3])
{
    const Entry *e = entry_of(actor);
    KhdaysEnemyInfo info;
    if (e == NULL || at == NULL) {
        return KH_ERROR;
    }
    if (e->kind == KH_ACTOR_PLAYER) {
        khdays_link_actor_position(e->actor, at);
        return KH_OK;
    }
    if (!khdays_enemies_state(e->actor, &info)) {
        return KH_ERROR;
    }
    memcpy(at, info.at, sizeof(info.at));
    return KH_OK;
}

static KhResult hp(KhActor actor, int32_t *now, int32_t *max)
{
    const Entry *e = entry_of(actor);
    KhdaysEnemyInfo info;
    int h = 0, m = 0;
    if (e == NULL) {
        return KH_ERROR;
    }
    if (e->kind == KH_ACTOR_PLAYER) {
        khdays_link_entity_hp(KHDAYS_W(e->actor), &h, &m);
    } else if (khdays_enemies_state(e->actor, &info)) {
        h = info.hp;
        m = info.max_hp;
    } else {
        return KH_ERROR;
    }
    if (now != NULL) {
        *now = h;
    }
    if (max != NULL) {
        *max = m;
    }
    return KH_OK;
}

static KhResult set_position(KhActor actor, const float at[3])
{
    const Entry *e = entry_of(actor);
    if (e == NULL || at == NULL) {
        return KH_ERROR;
    }
    if (!khdays_plugins_may_change(NULL)) {
        return KH_UNSUPPORTED;
    }
    if (e->kind != KH_ACTOR_PLAYER) {
        return khdays_enemies_move(e->actor, at) ? KH_OK : KH_ERROR;
    }
    return khdays_link_place_seat(e->seat, at) == NULL ? KH_OK : KH_UNSUPPORTED;
}

static KhResult set_hp(KhActor actor, int32_t value)
{
    const Entry *e = entry_of(actor);
    if (e == NULL) {
        return KH_ERROR;
    }
    if (!khdays_plugins_may_change(NULL)) {
        return KH_UNSUPPORTED;
    }
    if (e->kind != KH_ACTOR_PLAYER) {
        return KH_UNSUPPORTED;
    }
    return khdays_link_set_actor_hp(e->actor, value) == NULL ? KH_OK : KH_UNSUPPORTED;
}

static KhActor make_enemy(KhPlugin *plugin, int32_t kind_of, const float at[3], int32_t level, float hp_times,
                          float strength)
{
    const char *why = NULL;
    unsigned actor;
    char text[160];
    if (at == NULL || !khdays_plugins_may_change(plugin)) {
        return 0;
    }
    actor = khdays_enemies_make(kind_of, at, level, hp_times, strength, &why);
    if (actor == 0) {
        snprintf(text, sizeof(text), "no enemy of kind %d made: %s", (int)kind_of, why != NULL ? why : "not made");
        khdays_plugins_tell(plugin, text);
        return 0;
    }
    return meet(actor, KH_ACTOR_ENEMY, -1);
}

static const KhActors actors_table = {
    sizeof(KhActors), player,   local_seat, enemies,      of,     kind,      ref, seat,
    character,        position, hp,         set_position, set_hp, make_enemy};

const void *khdays_plugins_actors(void)
{
    return &actors_table;
}
