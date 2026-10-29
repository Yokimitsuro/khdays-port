/* The mods' characters' faces and names as the port's screens draw them (runtime/characters): each screen keeps
 * its faces as textures of its own layout's renderer, read once. */
#ifndef KHDAYS_MOD_FACES_H
#define KHDAYS_MOD_FACES_H

#include "layout_screen.h"

typedef struct KhdaysModFaces {
    SDL_Texture **faces; /* by number - KHDAYS_CHARACTER_FIRST */
    char *tried;
    int count;
} KhdaysModFaces;

/* Character `number`'s face (its description's picture), NULL when it is not a mod's or has none. */
SDL_Texture *khdays_mod_face(KhdaysModFaces *f, KhdaysLayout *layout, int number);

/* Draws it at (x, y, w, h) of the layout, in colour or greyed (`light` 0-255). Returns 0 when there is none. */
int khdays_mod_face_draw(KhdaysModFaces *f, KhdaysLayout *layout, const KhdaysLayoutFrame *fr, int number, float x,
                         float y, float w, float h, Uint8 light);

/* Character `number`'s name, or its weapon's, in the game's language `language` (1-5), into `out` (`room` units);
 * NULL when it is not a mod's. */
const unsigned short *khdays_mod_name(int number, int weapon, int language, unsigned short *out, int room);

void khdays_mod_faces_forget(KhdaysModFaces *f);

#endif
