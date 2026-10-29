/* The worlds a new mission is played in (worlds.h). */
#include "worlds.h"

#include <string.h>

/* Ov002_GetWorldName's table, in its order; a world without its archive marked unshipped. */
static const struct {
    const char *code;
    int shipped;
} worlds[KHDAYS_GAME_WORLDS] = {{"tt", 1}, {"aw", 1}, {"he", 1}, {"al", 1}, {"eh", 1}, {"nm", 1},
                                {"hb", 0}, {"zz", 1}, {"pp", 1}, {"bb", 1}, {"tw", 1}, {"pi", 0}};

int khdays_world_index(const char *code)
{
    for (int i = 0; code != NULL && i < KHDAYS_GAME_WORLDS; ++i) {
        if (worlds[i].shipped && strcmp(worlds[i].code, code) == 0) {
            return i;
        }
    }
    return -1;
}

const char *khdays_world_code(int index)
{
    return index >= 0 && index < KHDAYS_GAME_WORLDS ? worlds[index].code : NULL;
}
