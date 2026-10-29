/* The situation's names (situation.h). Plain C: the host test builds it too. */
#include "situation.h"

#include <string.h>

static const char *const scene_names[KHDAYS_SITUATIONS] = {
    "intro",    "title",          "load-menu", "save-menu", "settings",      "day-counter",    "movie",
    "field",    "camp-menu",      "pause",     "tutorial",  "field-3d-both", "mission-review", "shop",
    "thoughts", "world-selector", "game-over", "the-end",   "unknown",
};

static const char *const state_names[KHDAYS_STATES] = {
    "hud",      "dialog",  "challenge",   "info-upper",        "info-lower",
    "lower-3d", "party-4", "info-new",    "mission-panel",     "title-lit",
    "event",    "holo",    "lower-shown", "mission-panel-mod", "information-mod",
};

const char *khdays_situation_name(int scene)
{
    return scene >= 0 && scene < KHDAYS_SITUATIONS ? scene_names[scene] : "?";
}

const char *khdays_situation_state_name(int state)
{
    return state >= 0 && state < KHDAYS_STATES ? state_names[state] : "?";
}

static int named(const char *const *names, int count, const char *name, int length)
{
    for (int i = 0; i < count; ++i) {
        if ((int)strlen(names[i]) == length && strncmp(names[i], name, (size_t)length) == 0) {
            return i;
        }
    }
    return -1;
}

int khdays_situation_named(const char *name, int length)
{
    return named(scene_names, KHDAYS_SITUATIONS, name, length);
}

int khdays_situation_state_named(const char *name, int length)
{
    return named(state_names, KHDAYS_STATES, name, length);
}

uint32_t khdays_situation_swaps(uint32_t *history, int changed)
{
    uint32_t count = 0;
    *history = ((*history << 1) | (changed ? 1u : 0u)) & 0xffu;
    for (uint32_t bits = *history; bits != 0; bits &= bits - 1u) {
        ++count;
    }
    return count;
}
