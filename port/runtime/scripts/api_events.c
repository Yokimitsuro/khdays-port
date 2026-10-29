/* The game's events as the mods hear them (events.h). What raises each: the frame point
 * (khdays_script_events_frame: the frame, the scene, the menus, the items and munny compared with the last
 * frame) and the game's routines the build joins (scripts.c). */
#include "events.h"
#include "../plugins/plugins.h"

#include "api_v2.h"
#include "game_link.h"
#include "mod.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdlib.h>
#include <string.h>

#define SCENE_TITLE 1

static const char *const NAMES[KHDAYS_EVENT_COUNT] = {
    "frame",     "scene",      "title", "mission_start",  "mission_end",    "hp",     "item",  "munny",
    "menu_open", "menu_close", "save",  "enemy_appeared", "enemy_defeated", "defeat", "shared"};

static int depth; /* scripts running now */
static KhdaysEvent *waiting;
static int waiting_count, waiting_capacity;

int khdays_script_events_wanted(int id)
{
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        const KhdaysMod *m = khdays_scripts_mod(i);
        if (m->state != KHDAYS_MOD_LOADED) {
            continue;
        }
        for (int h = 0; h < m->handler_count; ++h) {
            if (m->handlers[h].event == id && m->handlers[h].ref != LUA_NOREF) {
                return 1;
            }
        }
    }
    return 0;
}

int khdays_script_events_on(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    const char *name = luaL_checkstring(L, 1);
    int id = -1;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    for (int i = 0; i < KHDAYS_EVENT_COUNT; ++i) {
        if (strcmp(name, NAMES[i]) == 0 && (i < KHDAYS_EVENT_FIRST_V2 || m->manifest.api >= 2)) {
            id = i;
        }
    }
    if (id < 0) {
        return luaL_error(L, "mod.on: there is no event '%s'", name);
    }
    if (m->handler_count == m->handler_capacity) {
        const int grown = m->handler_capacity == 0 ? 8 : m->handler_capacity * 2;
        KhdaysHandler *h = realloc(m->handlers, (size_t)grown * sizeof(*h));
        if (h == NULL) {
            return luaL_error(L, "not enough memory");
        }
        m->handlers = h;
        m->handler_capacity = grown;
    }
    lua_pushvalue(L, 2);
    m->handlers[m->handler_count].event = id;
    m->handlers[m->handler_count].ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m->handlers[m->handler_count].handle = ++m->next_handle;
    lua_pushinteger(L, m->handlers[m->handler_count].handle);
    ++m->handler_count;
    return 1;
}

int khdays_script_events_off(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    const lua_Integer handle = luaL_checkinteger(L, 1);
    for (int h = 0; h < m->handler_count; ++h) {
        if (m->handlers[h].handle == handle && m->handlers[h].ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, m->handlers[h].ref);
            m->handlers[h].ref = LUA_NOREF; /* taken out after the delivery that may be walking them */
        }
    }
    return 0;
}

static void set_int(lua_State *L, const char *key, int value)
{
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}

static void set_text(lua_State *L, const char *key, const char *value)
{
    lua_pushstring(L, value);
    lua_setfield(L, -2, key);
}

/* Whether `m` hears `e`: an enemy appeared is told only to the mod that made it, a defeat to the mod
 * holding it. */
static int hears(const KhdaysMod *m, const KhdaysEvent *e)
{
    switch (e->id) {
    case KHDAYS_EVENT_ENEMY_APPEARED: return khdays_api_enemies_handle(m, (unsigned)e->a) != 0;
    case KHDAYS_EVENT_DEFEAT: return m == khdays_api_run_holder();
    case KHDAYS_EVENT_SHARED: return khdays_api_time_shared_for(m, e->a);
    default: return 1;
    }
}

static void push_event(lua_State *L, const KhdaysMod *m, const KhdaysEvent *e)
{
    lua_createtable(L, 0, 4);
    set_text(L, "event", NAMES[e->id]);
    switch (e->id) {
    case KHDAYS_EVENT_FRAME:
        set_int(L, "frame", e->a);
        lua_pushboolean(L, e->b);
        lua_setfield(L, -2, "paused");
        break;
    case KHDAYS_EVENT_SCENE:
        set_int(L, "from", e->a);
        set_int(L, "to", e->b);
        set_int(L, "argument", e->c);
        break;
    case KHDAYS_EVENT_MISSION_START: set_int(L, "mission", e->a); break;
    case KHDAYS_EVENT_MISSION_END:
        set_int(L, "mission", e->a);
        set_int(L, "rank", e->b);
        lua_pushboolean(L, e->d);
        lua_setfield(L, -2, "given_up");
        if (e->c >= 0) {
            lua_pushboolean(L, e->c);
            lua_setfield(L, -2, "complete");
        }
        if (m->manifest.api >= 2 && e->text != NULL) {
            set_text(L, "ending", e->text);
        }
        break;
    case KHDAYS_EVENT_HP:
        set_text(L, "who", e->text);
        set_int(L, "before", e->a);
        set_int(L, "after", e->b);
        set_int(L, "max", e->c);
        break;
    case KHDAYS_EVENT_ITEM:
        set_int(L, "item", e->a);
        set_int(L, "count", e->b);
        set_int(L, "change", e->c);
        break;
    case KHDAYS_EVENT_MUNNY:
        set_int(L, "total", e->a);
        set_int(L, "change", e->b);
        break;
    case KHDAYS_EVENT_MENU_OPEN:
    case KHDAYS_EVENT_MENU_CLOSE: set_text(L, "menu", e->text); break;
    case KHDAYS_EVENT_ENEMY_APPEARED:
    case KHDAYS_EVENT_ENEMY_DEFEATED: {
        const int handle = khdays_api_enemies_handle(m, (unsigned)e->a);
        if (handle != 0) {
            set_int(L, "enemy", handle); /* nil for the mission's own, or another mod's */
        }
        set_int(L, "kind", e->b);
        lua_pushboolean(L, handle != 0);
        lua_setfield(L, -2, "mine");
        break;
    }
    case KHDAYS_EVENT_DEFEAT: {
        const int handle = e->b != 0 ? khdays_api_enemies_handle(m, (unsigned)e->b) : 0;
        set_int(L, "damage", e->a);
        if (handle != 0) {
            set_int(L, "enemy", handle);
        }
        if (e->c >= 0) {
            set_int(L, "kind", e->c);
        }
        break;
    }
    case KHDAYS_EVENT_SHARED:
        khdays_api_time_push_shared(L, e->a);
        lua_setfield(L, -2, "data");
        break;
    default: break;
    }
}

/* Drops the handlers mod.off took out. */
static void tidy(KhdaysMod *m)
{
    int kept = 0;
    for (int h = 0; h < m->handler_count; ++h) {
        if (m->handlers[h].ref != LUA_NOREF) {
            m->handlers[kept++] = m->handlers[h];
        }
    }
    m->handler_count = kept;
}

static void deliver(const KhdaysEvent *e)
{
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        KhdaysMod *m = khdays_scripts_mod(i);
        const int n = m->handler_count; /* not the ones its handlers add now */
        for (int h = 0; h < n && m->state == KHDAYS_MOD_LOADED; ++h) {
            if (m->handlers[h].event != e->id || m->handlers[h].ref == LUA_NOREF || !hears(m, e)) {
                continue;
            }
            lua_rawgeti(m->L, LUA_REGISTRYINDEX, m->handlers[h].ref);
            push_event(m->L, m, e);
            khdays_sandbox_call(m, 1, 0);
        }
        tidy(m);
    }
}

void khdays_script_events_hold(void)
{
    ++depth;
}

void khdays_script_events_release(void)
{
    if (depth > 1) {
        --depth;
        return;
    }
    /* the outermost: what was raised meanwhile, in order, including what that raises */
    for (int next = 0; next < waiting_count; ++next) {
        const KhdaysEvent e = waiting[next];
        deliver(&e);
    }
    waiting_count = 0;
    depth = 0;
}

void khdays_script_events_raise(const KhdaysEvent *e)
{
    if (e->id >= 0 && e->id <= KHDAYS_EVENT_SAVE) {
        /* the game's events the native plugins hear too (kh.events), as they happen */
        khdays_plugins_game_event(NAMES[e->id], e->a, e->b, e->c, e->d, e->text);
    }
    if (!khdays_script_events_wanted(e->id)) {
        return;
    }
    if (depth > 0) {
        if (waiting_count == waiting_capacity) {
            const int grown = waiting_capacity == 0 ? 16 : waiting_capacity * 2;
            KhdaysEvent *w = realloc(waiting, (size_t)grown * sizeof(*w));
            if (w == NULL) {
                return;
            }
            waiting = w;
            waiting_capacity = grown;
        }
        waiting[waiting_count++] = *e;
        return;
    }
    khdays_script_events_hold();
    deliver(e);
    khdays_script_events_release();
}

/* The last frame's state, to raise what changed. */
static struct {
    int seen;
    unsigned scene;
    int menus[2];
    unsigned state; /* gGameState's reference when the items were copied; 0: not copied */
    unsigned char items[KHDAYS_LINK_ITEMS];
    int munny;
} last;

void khdays_script_events_reset(void)
{
    memset(&last, 0, sizeof(last));
}

static void raise_event(int id, int a, int b, int c, const char *text)
{
    KhdaysEvent e;
    e.id = id;
    e.a = a;
    e.b = b;
    e.c = c;
    e.d = 0;
    e.text = text;
    khdays_script_events_raise(&e);
}

static void compare_state(int fresh)
{
    unsigned char items[KHDAYS_LINK_ITEMS];
    const unsigned state = khdays_link_state();
    int munny;
    if (state == 0 || !khdays_link_items(items)) {
        last.state = 0;
        return;
    }
    munny = khdays_link_munny();
    if (last.state == state && !fresh) {
        for (int i = 0; i < KHDAYS_LINK_ITEMS; ++i) {
            if (items[i] != last.items[i]) {
                raise_event(KHDAYS_EVENT_ITEM, i, items[i], (int)items[i] - (int)last.items[i], NULL);
            }
        }
        if (munny != last.munny) {
            raise_event(KHDAYS_EVENT_MUNNY, munny, munny - last.munny, 0, NULL);
        }
    }
    memcpy(last.items, items, sizeof(items));
    last.munny = munny;
    last.state = state;
}

void khdays_script_events_frame(void)
{
    static const char *const MENUS[2] = {"pause", "camp"};
    unsigned argument = 0;
    const unsigned scene = khdays_link_scene(&argument);
    const int scene_changed = last.seen && scene != last.scene;
    if (scene_changed) {
        raise_event(KHDAYS_EVENT_SCENE, (int)last.scene, (int)scene, (int)argument, NULL);
    }
    if (scene == SCENE_TITLE && (!last.seen || scene_changed)) {
        raise_event(KHDAYS_EVENT_TITLE, 0, 0, 0, NULL);
    }
    for (int m = 0; m < 2; ++m) {
        const int open = khdays_link_menu_open(m);
        if (last.seen && open != last.menus[m]) {
            raise_event(open ? KHDAYS_EVENT_MENU_OPEN : KHDAYS_EVENT_MENU_CLOSE, 0, 0, 0, MENUS[m]);
        }
        last.menus[m] = open;
    }
    /* in the title the game's state is made anew or loaded from a save: nothing is obtained there, so
     * it is copied without events */
    compare_state(scene == SCENE_TITLE || (scene_changed && last.scene == SCENE_TITLE));
    raise_event(KHDAYS_EVENT_FRAME, (int)khdays_link_frame(), khdays_link_paused(), 0, NULL);
    last.scene = scene;
    last.seen = 1;
}
