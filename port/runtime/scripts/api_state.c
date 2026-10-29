/* The game's state by name: game (munny, items, flags), player, mission and scene. What each reads is
 * game_link.c's; the limits are the game's own (game_link.h). */
#include "api.h"

#include "game_link.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

#include <string.h>

#define ITEM_MAX 99 /* Ov008_AddItemCount's cap */

int khdays_api_may_change(lua_State *L, int needs_state)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    const char *why = m->manifest.kind == KHDAYS_MOD_VISUAL     ? "visual mod"
                      : khdays_link_guest()                     ? "guest"
                      : needs_state && khdays_link_state() == 0 ? "no game"
                                                                : NULL;
    if (why == NULL) {
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 0;
}

static int refuse(lua_State *L, const char *why)
{
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

static lua_Integer clamp(lua_Integer v, lua_Integer low, lua_Integer high)
{
    return v < low ? low : v > high ? high : v;
}

static int set_munny(lua_State *L)
{
    const lua_Integer v = luaL_checkinteger(L, 1);
    if (!khdays_api_may_change(L, 1)) {
        return 2;
    }
    khdays_link_set_munny((int)clamp(v, 0, KHDAYS_LINK_MUNNY_MAX));
    lua_pushboolean(L, 1);
    return 1;
}

static int item(lua_State *L)
{
    unsigned char counts[KHDAYS_LINK_ITEMS];
    const lua_Integer id = luaL_checkinteger(L, 1);
    if (id < 0 || id >= KHDAYS_LINK_ITEMS) {
        return refuse(L, "unknown item");
    }
    if (!khdays_link_items(counts)) {
        return refuse(L, "no game");
    }
    lua_pushinteger(L, counts[id]);
    return 1;
}

static int set_item(lua_State *L)
{
    const lua_Integer id = luaL_checkinteger(L, 1), n = luaL_checkinteger(L, 2);
    if (id < 0 || id >= KHDAYS_LINK_ITEMS) {
        return refuse(L, "unknown item");
    }
    if (!khdays_api_may_change(L, 1)) {
        return 2;
    }
    khdays_link_set_item((int)id, (int)clamp(n, 0, ITEM_MAX));
    lua_pushboolean(L, 1);
    return 1;
}

static int flag(lua_State *L)
{
    const lua_Integer n = luaL_checkinteger(L, 1);
    if (n < 0 || n >= KHDAYS_LINK_FLAGS) {
        return refuse(L, "unknown flag");
    }
    if (khdays_link_state() == 0) {
        return refuse(L, "no game");
    }
    lua_pushboolean(L, khdays_link_flag((int)n));
    return 1;
}

static int set_flag(lua_State *L)
{
    const lua_Integer n = luaL_checkinteger(L, 1);
    const int on = lua_toboolean(L, 2);
    if (n < 0 || n >= KHDAYS_LINK_FLAGS) {
        return refuse(L, "unknown flag");
    }
    if (!khdays_api_may_change(L, 1)) {
        return 2;
    }
    khdays_link_set_flag((int)n, on);
    lua_pushboolean(L, 1);
    return 1;
}

static int set_hp(lua_State *L)
{
    const lua_Integer v = luaL_checkinteger(L, 1);
    const char *why;
    int hp, max;
    if (!khdays_api_may_change(L, 1)) {
        return 2;
    }
    if (!khdays_link_player_hp(&hp, &max)) {
        return refuse(L, "no player");
    }
    /* the game clamps to the maximum; 0 is a defeat, as a hit's */
    why = khdays_link_set_player_hp((int)clamp(v, 0, max));
    if (why != NULL) {
        return refuse(L, why);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int position(lua_State *L)
{
    float at[3];
    if (!khdays_link_position(at)) {
        return refuse(L, "no player");
    }
    lua_pushnumber(L, at[0]);
    lua_pushnumber(L, at[1]);
    lua_pushnumber(L, at[2]);
    return 3;
}

/* The tables' values read when asked: game.munny, mission.id, scene.id. */
static int game_value(lua_State *L)
{
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "munny") == 0) {
        if (khdays_link_state() == 0) {
            return 0;
        }
        lua_pushinteger(L, khdays_link_munny());
        return 1;
    }
    return 0;
}

static int mission_value(lua_State *L)
{
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "id") == 0) {
        const int id = khdays_link_mission();
        if (id < 0) {
            return 0;
        }
        lua_pushinteger(L, id);
        return 1;
    }
    return 0;
}

static int scene_value(lua_State *L)
{
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "id") == 0) {
        lua_pushinteger(L, (lua_Integer)khdays_link_scene(NULL));
        return 1;
    }
    return 0;
}

/* A global table of `functions` whose other keys `value` answers. */
static void table(lua_State *L, const char *name, const luaL_Reg *functions, lua_CFunction value)
{
    lua_newtable(L);
    if (functions != NULL) {
        luaL_setfuncs(L, functions, 0);
    }
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, value);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setglobal(L, name);
}

static int player_value(lua_State *L)
{
    const char *key = luaL_checkstring(L, 2);
    int hp, max;
    const int hp_key = strcmp(key, "hp") == 0, max_key = strcmp(key, "max_hp") == 0;
    if ((!hp_key && !max_key) || !khdays_link_player_hp(&hp, &max)) {
        return 0;
    }
    lua_pushinteger(L, hp_key ? hp : max);
    return 1;
}

void khdays_api_state_open(KhdaysMod *m)
{
    static const luaL_Reg game[] = {{"set_munny", set_munny}, {"item", item},         {"set_item", set_item},
                                    {"flag", flag},           {"set_flag", set_flag}, {NULL, NULL}};
    static const luaL_Reg player[] = {{"position", position}, {"set_hp", set_hp}, {NULL, NULL}};
    lua_State *L = m->L;
    table(L, "game", game, game_value);
    table(L, "player", player, player_value);
    table(L, "mission", NULL, mission_value);
    table(L, "scene", NULL, scene_value);
}
