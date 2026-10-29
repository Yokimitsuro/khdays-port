/* Time, music, chance and sharing for the mods (api_v2.h; time_link.h is the game's side). Timers run
 * on the game's time, which stands still while the game is paused. `random` draws from the room's seed
 * (each mod its own run of draws), started again at each mission's start, so every player of a room
 * draws the same from there. `mod.share` sends a table from the host's mod to the guests' copies of it,
 * who hear it as `shared`. */
#include "api_v2.h"

#include "api.h"
#include "events.h"
#include "game_link.h"
#include "sandbox.h"
#include "time_link.h"
#include "../plugins/plugins.h"

#include <lauxlib.h>
#include <lua.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VBLANKS_A_SECOND 60.0
#define FADE 30   /* the field's own fade between tracks, in frames (Ov002_TickGameplayState) */
#define DEPTH 200 /* the tables a shared table may hold one inside another (Lua's own C-call bound) */

/* A timer: when it fires next (in the game's time), every how long again (0 once), its function. */
struct KhdaysModTimer {
    int handle;
    unsigned long long due, every;
    int ref;
};

static unsigned long long game_time; /* VBlanks while the game ran, not paused */
static unsigned last_vblanks;
static int time_started, next_handle;
static const KhdaysMod *music_owner;

/* The shares the host sent, heard this frame: the mod they are for and the table's bytes. */
static struct Share {
    unsigned char *data;
    unsigned size, value; /* the bytes and where the value starts (after the mod's id) */
} *shares;
static int share_count, share_room;

static int refuse(lua_State *L, const char *why)
{
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

/* ---- The draws ----------------------------------------------------------------------------- */

static unsigned long long splitmix(unsigned long long *x)
{
    unsigned long long z = (*x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

/* The mod's draws started from `seed` and its id (xoshiro256**'s state from splitmix64). */
static void seed_draws(KhdaysMod *m, unsigned long long seed)
{
    unsigned long long x = seed;
    for (const unsigned char *c = (const unsigned char *)m->manifest.id; c != NULL && *c != 0; ++c) {
        x = (x ^ *c) * 0x100000001b3ull;
    }
    for (int i = 0; i < 4; ++i) {
        m->v2->draws[i] = splitmix(&x);
    }
}

static unsigned long long rotl(unsigned long long x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static unsigned long long next_draw(unsigned long long *s)
{
    const unsigned long long result = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

/* random(n): 1..n; random(a, b): a..b. Every whole number of the range as likely. */
static int draw(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    const lua_Integer a = lua_gettop(L) >= 2 ? luaL_checkinteger(L, 1) : 1;
    const lua_Integer b = lua_gettop(L) >= 2 ? luaL_checkinteger(L, 2) : luaL_checkinteger(L, 1);
    unsigned long long range, limit, x;
    if (b < a) {
        return luaL_argerror(L, lua_gettop(L) >= 2 ? 2 : 1, "an empty range");
    }
    range = (unsigned long long)b - (unsigned long long)a + 1u; /* 0: all 2^64 */
    limit = range == 0 ? 0 : ~0ull - (~0ull % range + 1u) % range;
    do {
        x = next_draw(m->v2->draws);
    } while (range != 0 && x > limit);
    lua_pushinteger(L, (lua_Integer)((unsigned long long)a + (range == 0 ? x : x % range)));
    return 1;
}

/* ---- Timers -------------------------------------------------------------------------------- */

static unsigned long long after(lua_State *L, int arg, int at_least_one)
{
    const lua_Number seconds = luaL_checknumber(L, arg);
    const lua_Number vblanks = floor(seconds * VBLANKS_A_SECOND + 0.5);
    if (!(seconds >= 0) || (at_least_one && vblanks < 1) || vblanks > 1e15) {
        luaL_argerror(L, arg, at_least_one ? "a time above 0" : "a time of 0 or more");
    }
    return (unsigned long long)vblanks;
}

static int add_timer(lua_State *L, int every)
{
    KhdaysModV2 *v = khdays_sandbox_mod(L)->v2;
    const unsigned long long wait = after(L, 1, every);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (v->timer_count == v->timer_room) {
        const int room = v->timer_room ? v->timer_room * 2 : 8;
        KhdaysModTimer *grown = realloc(v->timers, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return luaL_error(L, "not enough memory");
        }
        v->timers = grown;
        v->timer_room = room;
    }
    lua_pushvalue(L, 2);
    v->timers[v->timer_count].ref = luaL_ref(L, LUA_REGISTRYINDEX);
    v->timers[v->timer_count].handle = ++next_handle;
    v->timers[v->timer_count].due = game_time + wait;
    v->timers[v->timer_count].every = every ? wait : 0;
    lua_pushinteger(L, v->timers[v->timer_count++].handle);
    return 1;
}

static int timer_after(lua_State *L)
{
    return add_timer(L, 0);
}

static int timer_every(lua_State *L)
{
    return add_timer(L, 1);
}

static int timer_cancel(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    const lua_Integer handle = luaL_checkinteger(L, 1);
    for (int i = 0; i < m->v2->timer_count; ++i) {
        KhdaysModTimer *t = &m->v2->timers[i];
        if (t->handle == handle && t->ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, t->ref);
            t->ref = LUA_NOREF; /* dropped once no timer of it runs */
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    return refuse(L, "no such timer");
}

/* The mod's timers due by now, each called once; its timers made or cancelled meanwhile kept apart. */
static void fire(KhdaysMod *m)
{
    const int n = m->v2->timer_count;
    int kept = 0;
    for (int i = 0; i < n && m->state == KHDAYS_MOD_LOADED; ++i) {
        KhdaysModTimer *t = &m->v2->timers[i];
        if (t->ref == LUA_NOREF || t->due > game_time) {
            continue;
        }
        lua_rawgeti(m->L, LUA_REGISTRYINDEX, t->ref);
        if (t->every == 0) {
            luaL_unref(m->L, LUA_REGISTRYINDEX, t->ref);
            t->ref = LUA_NOREF;
        } else {
            t->due = t->due + t->every > game_time ? t->due + t->every : game_time + t->every;
        }
        khdays_script_events_hold();
        khdays_sandbox_call(m, 0, 0);
        khdays_script_events_release();
    }
    for (int i = 0; m->v2 != NULL && i < m->v2->timer_count; ++i) {
        if (m->v2->timers[i].ref != LUA_NOREF) {
            m->v2->timers[kept++] = m->v2->timers[i];
        }
    }
    if (m->v2 != NULL) {
        m->v2->timer_count = kept;
    }
}

/* ---- Music --------------------------------------------------------------------------------- */

static int fade_arg(lua_State *L, int arg)
{
    const lua_Number seconds = luaL_optnumber(L, arg, FADE / VBLANKS_A_SECOND);
    if (!(seconds >= 0) || seconds > 60) {
        luaL_argerror(L, arg, "a fade of 0 to 60 seconds");
    }
    return (int)floor(seconds * VBLANKS_A_SECOND + 0.5);
}

/* music.play(track[, fade]): a track of the game by its name in the sound archive, in place of the
 * game's own until music.stop or the scene's end. */
static int music_play(lua_State *L)
{
    const int track = khdays_link_music_track(luaL_checkstring(L, 1));
    const int fade = fade_arg(L, 2);
    const char *why;
    if (track < 0) {
        return refuse(L, "unknown track");
    }
    why = khdays_link_music_play(track, fade);
    if (why != NULL) {
        return refuse(L, why);
    }
    music_owner = khdays_sandbox_mod(L);
    lua_pushboolean(L, 1);
    return 1;
}

/* music.stop([fade]): the game's own music again. */
static int music_stop(lua_State *L)
{
    const int fade = fade_arg(L, 1);
    if (music_owner != khdays_sandbox_mod(L) || !khdays_link_music_playing()) {
        return refuse(L, "not playing");
    }
    khdays_link_music_stop(fade);
    music_owner = NULL;
    lua_pushboolean(L, 1);
    return 1;
}

/* ---- Sharing ------------------------------------------------------------------------------- */

typedef struct Bytes {
    unsigned char *data;
    size_t size, room;
} Bytes;

static void put(lua_State *L, Bytes *b, const void *data, size_t size)
{
    if (size == 0) {
        return;
    }
    if (b->size + size > b->room) {
        size_t room = b->room ? b->room * 2 : 256;
        unsigned char *grown;
        while (room < b->size + size) {
            room *= 2;
        }
        grown = realloc(b->data, room);
        if (grown == NULL) {
            free(b->data);
            b->data = NULL;
            luaL_error(L, "not enough memory");
            return;
        }
        b->data = grown;
        b->room = room;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
}

/* Whether the value at `index` can be shared (raises why not): nil, booleans, numbers, texts and tables
 * of them, keys and values. Checked whole before anything is written. */
static void check_value(lua_State *L, int index, int depth)
{
    switch (lua_type(L, index)) {
    case LUA_TNIL:
    case LUA_TBOOLEAN:
    case LUA_TNUMBER: break;
    case LUA_TSTRING:
        if (lua_rawlen(L, index) > 0xffffffffu) {
            luaL_error(L, "a text too long to share");
        }
        break;
    case LUA_TTABLE:
        if (depth >= DEPTH) {
            luaL_error(L, "tables too deep to share");
        }
        luaL_checkstack(L, 3, "a table to share");
        index = lua_absindex(L, index);
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            check_value(L, -2, depth + 1);
            check_value(L, -1, depth + 1);
            lua_pop(L, 1);
        }
        break;
    default: luaL_error(L, "only nil, booleans, numbers, texts and tables can be shared"); break;
    }
}

/* The value at `index` written (check_value passed it). */
static void write_value(lua_State *L, Bytes *b, int index)
{
    unsigned char tag;
    switch (lua_type(L, index)) {
    case LUA_TNIL: put(L, b, "n", 1); break;
    case LUA_TBOOLEAN: put(L, b, lua_toboolean(L, index) ? "t" : "f", 1); break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            const long long v = lua_tointeger(L, index);
            put(L, b, "i", 1);
            put(L, b, &v, sizeof(v));
        } else {
            const double v = lua_tonumber(L, index);
            put(L, b, "d", 1);
            put(L, b, &v, sizeof(v));
        }
        break;
    case LUA_TSTRING: {
        size_t n;
        const char *s = lua_tolstring(L, index, &n);
        const unsigned u = (unsigned)n;
        put(L, b, "s", 1);
        put(L, b, &u, sizeof(u));
        put(L, b, s, n);
        break;
    }
    default: /* a table */
        tag = 'T';
        put(L, b, &tag, 1);
        index = lua_absindex(L, index);
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            write_value(L, b, -2);
            write_value(L, b, -1);
            lua_pop(L, 1);
        }
        put(L, b, "e", 1);
        break;
    }
}

/* A value read at `*at` and pushed; 0 (nothing pushed) when the bytes are not a value. */
static int read_value(lua_State *L, const unsigned char *d, unsigned size, unsigned *at, int depth)
{
    unsigned char tag;
    if (*at >= size || depth >= DEPTH || !lua_checkstack(L, 3)) {
        return 0;
    }
    tag = d[(*at)++];
    switch (tag) {
    case 'n': lua_pushnil(L); return 1;
    case 't': lua_pushboolean(L, 1); return 1;
    case 'f': lua_pushboolean(L, 0); return 1;
    case 'i':
    case 'd':
        if (size - *at < 8) {
            return 0;
        }
        if (tag == 'i') {
            long long v;
            memcpy(&v, d + *at, 8);
            lua_pushinteger(L, (lua_Integer)v);
        } else {
            double v;
            memcpy(&v, d + *at, 8);
            lua_pushnumber(L, v);
        }
        *at += 8;
        return 1;
    case 's': {
        unsigned n;
        if (size - *at < 4) {
            return 0;
        }
        memcpy(&n, d + *at, 4);
        *at += 4;
        if (size - *at < n) {
            return 0;
        }
        lua_pushlstring(L, (const char *)d + *at, n);
        *at += n;
        return 1;
    }
    case 'T':
        lua_newtable(L);
        while (*at < size && d[*at] != 'e') {
            if (!read_value(L, d, size, at, depth + 1)) {
                lua_pop(L, 1);
                return 0;
            }
            if (!read_value(L, d, size, at, depth + 1)) {
                lua_pop(L, 2);
                return 0;
            }
            if (lua_isnil(L, -2) || (lua_type(L, -2) == LUA_TNUMBER && lua_tonumber(L, -2) != lua_tonumber(L, -2))) {
                lua_pop(L, 2); /* no key a table can hold */
            } else {
                lua_rawset(L, -3);
            }
        }
        if (*at >= size) {
            lua_pop(L, 1);
            return 0;
        }
        ++*at;
        return 1;
    default: return 0;
    }
}

/* mod.share(t): the host's mod sends `t` to the guests' copies of itself. */
static int share(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    Bytes b = {NULL, 0, 0};
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!khdays_api_may_change(L, 0)) {
        return 2;
    }
    check_value(L, 1, 0);
    put(L, &b, m->manifest.id, strlen(m->manifest.id) + 1);
    write_value(L, &b, 1);
    khdays_link_share(b.data, (unsigned)b.size);
    free(b.data);
    lua_pushboolean(L, 1);
    return 1;
}

int khdays_api_time_shared_for(const KhdaysMod *m, int share)
{
    return share >= 0 && share < share_count && strcmp((const char *)shares[share].data, m->manifest.id) == 0;
}

void khdays_api_time_push_shared(lua_State *L, int share)
{
    unsigned at = shares[share].value;
    if (!read_value(L, shares[share].data, shares[share].size, &at, 0) || !lua_istable(L, -1)) {
        lua_newtable(L); /* not a table the port's sharing made */
    }
}

/* The shares heard since the last frame, raised as `shared` for the mods they are for. */
static void take_shares(void)
{
    unsigned char *data;
    unsigned size;
    for (int i = 0; i < share_count; ++i) {
#pragma warning(suppress : 6001) /* every share below the count was written before the count moved */
        free(shares[i].data);
    }
    share_count = 0;
    while (khdays_link_shared(&data, &size)) {
        const unsigned char *end = memchr(data, 0, size);
        KhdaysEvent e;
        khdays_plugins_shared(data, size); /* a native plugin's message (kh.net), heard by the plugins */
        if (end == NULL) {
            free(data);
            continue;
        }
        if (share_count == share_room) {
            const int room = share_room ? share_room * 2 : 4;
            struct Share *grown = realloc(shares, (size_t)room * sizeof(*grown));
            if (grown == NULL) {
                free(data);
                break;
            }
            shares = grown;
            share_room = room;
        }
        shares[share_count].data = data;
        shares[share_count].size = size;
        shares[share_count].value = (unsigned)(end - data) + 1;
        memset(&e, 0, sizeof(e));
        e.id = KHDAYS_EVENT_SHARED;
        e.a = share_count++;
        khdays_script_events_raise(&e);
    }
}

/* ---- The frame, missions, the mods' parts ------------------------------------------------- */

void khdays_api_time_frame(void)
{
    const unsigned now = khdays_link_vblanks();
    if (!time_started) {
        time_started = 1;
        last_vblanks = now;
    }
    if (!khdays_link_paused()) {
        game_time += now - last_vblanks;
    }
    last_vblanks = now;
    khdays_link_music_frame();
    if (!khdays_link_music_playing()) {
        music_owner = NULL;
    }
    take_shares();
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        KhdaysMod *m = khdays_scripts_mod(i);
        if (m->state == KHDAYS_MOD_LOADED && m->v2 != NULL && m->v2->timer_count > 0) {
            fire(m);
        }
    }
}

void khdays_api_time_mission_start(void)
{
    int in_room;
    const unsigned long long seed = khdays_link_seed(&in_room);
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        KhdaysMod *m = khdays_scripts_mod(i);
        if (m->v2 != NULL) {
            seed_draws(m, seed);
        }
    }
}

void khdays_api_time_open(KhdaysMod *m)
{
    static const luaL_Reg timer[] = {
        {"after", timer_after}, {"every", timer_every}, {"cancel", timer_cancel}, {NULL, NULL}};
    static const luaL_Reg music[] = {{"play", music_play}, {"stop", music_stop}, {NULL, NULL}};
    int in_room;
    seed_draws(m, khdays_link_seed(&in_room));
    luaL_newlib(m->L, timer);
    lua_setglobal(m->L, "timer");
    luaL_newlib(m->L, music);
    lua_setglobal(m->L, "music");
    lua_pushcfunction(m->L, draw);
    lua_setglobal(m->L, "random");
    lua_getglobal(m->L, "mod");
    if (lua_istable(m->L, -1)) {
        lua_pushcfunction(m->L, share);
        lua_setfield(m->L, -2, "share");
    }
    lua_pop(m->L, 1);
}

void khdays_api_time_release(KhdaysMod *m)
{
    if (m->v2 == NULL) {
        return;
    }
    for (int i = 0; i < m->v2->timer_count; ++i) {
        if (m->v2->timers[i].ref != LUA_NOREF && m->L != NULL) {
            luaL_unref(m->L, LUA_REGISTRYINDEX, m->v2->timers[i].ref);
        }
    }
    free(m->v2->timers);
    m->v2->timers = NULL;
    m->v2->timer_count = m->v2->timer_room = 0;
    if (music_owner == m) {
        khdays_link_music_stop(FADE);
        music_owner = NULL;
    }
}
