/* Mods that switch on features of the port's own (installed.h). */
#include "installed.h"

#include "../core/rom.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_atomic.h>

static const char *const IDS[KHDAYS_MODS] = {"menu-overhaul"};

/* each mod's state: 0 not looked for yet, 1 not installed, 2 installed */
static SDL_AtomicInt state[KHDAYS_MODS];

int khdays_mod_installed(int mod)
{
    int now;
    if (mod < 0 || mod >= KHDAYS_MODS) {
        return 0;
    }
    now = SDL_GetAtomicInt(&state[mod]);
    if (now == 0) {
        char relative[64], path[1024];
        snprintf(relative, sizeof(relative), "days-mods/%s.txt", IDS[mod]);
        now = khdays_mods_find(relative, path, (int)sizeof(path)) ? 2 : 1;
        SDL_SetAtomicInt(&state[mod], now);
    }
    return now == 2;
}

int khdays_mod_named(const char *name)
{
    for (int i = 0; i < KHDAYS_MODS; ++i) {
        if (strcmp(name, IDS[i]) == 0) {
            return i;
        }
    }
    return -1;
}
