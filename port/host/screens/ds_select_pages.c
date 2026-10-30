/* The game's own character select on the mods' characters' pages (ds_select_pages.h). Each slot of the game's grid
 * (its table's row and column, charselect.h) holds the page's mod's character in the place of the game's portrait:
 * a portrait 24 x 64 DS pixels, the first row's from 32 down and 28 left of its column's 24 pixels, the second's
 * 64 lower and 16 left (as the game's screen shows its portraits), its face's middle cut to the portrait's shape,
 * grey as the game's unless a place is on it, then in colour with the place's corners round it. A slot without a
 * character is dark. Under the grid, a dot a page, the page shown lit. */
#include "ds_select_pages.h"

#include "mod_faces.h"
#include "../app/places.h"
#include "../../runtime/menus/charselect.h"

#define PORTRAIT_W 24.0f
#define PORTRAIT_H 64.0f
#define FIRST_TOP 32.0f
#define GREY_LIGHT 140 /* a face nobody is on, as the game greys its portraits */
#define CORNER 6.0f    /* a place's corner's arms */
#define DOTS_TOP 164.0f
#define DOT 4.0f

static KhdaysLayout layout;
static KhdaysModFaces faces;

static void fill(SDL_Renderer *r, float x, float y, float w, float h, Uint8 red, Uint8 green, Uint8 blue)
{
    const SDL_FRect at = {x, y, w, h};
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
    SDL_RenderFillRect(r, &at);
}

/* A place's corners round (x, y, w, h), `t` thick. */
static void corners(SDL_Renderer *r, float x, float y, float w, float h, float t, float arm, const Uint8 rgb[3])
{
    const float right = x + w, bottom = y + h;
    fill(r, x, y, arm, t, rgb[0], rgb[1], rgb[2]);
    fill(r, x, y, t, arm, rgb[0], rgb[1], rgb[2]);
    fill(r, right - arm, y, arm, t, rgb[0], rgb[1], rgb[2]);
    fill(r, right - t, y, t, arm, rgb[0], rgb[1], rgb[2]);
    fill(r, x, bottom - t, arm, t, rgb[0], rgb[1], rgb[2]);
    fill(r, x, bottom - arm, t, arm, rgb[0], rgb[1], rgb[2]);
    fill(r, right - arm, bottom - t, arm, t, rgb[0], rgb[1], rgb[2]);
    fill(r, right - t, bottom - arm, t, arm, rgb[0], rgb[1], rgb[2]);
}

void khdays_ds_select_pages_draw(SDL_Renderer *renderer, float x0, float y0, float sx, float sy)
{
    KhdaysSelectView v;
    khdays_charselect_view(&v);
    if (v.ds_page < 1 || v.ds_slots == 0 || renderer == NULL) {
        return;
    }
    if (layout.renderer != renderer) {
        khdays_mod_faces_forget(&faces);
        layout.renderer = renderer;
        layout.who = "select pages";
    }
    for (int s = 0; s < v.ds_slots; ++s) {
        const int row = v.ds_slot_row[s], column = v.ds_slot_column[s];
        const float x = x0 + ((float)column * PORTRAIT_W - (row != 0 ? 16.0f : 28.0f)) * sx;
        const float y = y0 + (FIRST_TOP + (float)row * PORTRAIT_H) * sy, w = PORTRAIT_W * sx, h = PORTRAIT_H * sy;
        const int c = khdays_charselect_ds_at(&v, s);
        int on = -1;
        fill(renderer, x, y, w, h, 16, 16, 16);
        if (c < 0) {
            continue;
        }
        for (int i = 0; i < KHDAYS_SELECT_PLACES && on < 0; ++i) {
            const int there = i == v.self ? v.character : v.places[i].character;
            if ((v.places[i].present || i == v.self) && there == c) {
                on = i;
            }
        }
        SDL_Texture *face = khdays_mod_face(&faces, &layout, c);
        if (face != NULL) {
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(face, &tw, &th);
            /* the face's middle, cut to the portrait's shape */
            const float cut_w = th * PORTRAIT_W / PORTRAIT_H < tw ? th * PORTRAIT_W / PORTRAIT_H : tw;
            const float cut_h = cut_w * PORTRAIT_H / PORTRAIT_W;
            const SDL_FRect from = {(tw - cut_w) / 2.0f, (th - cut_h) / 2.0f, cut_w, cut_h}, at = {x, y, w, h};
            const Uint8 light = on >= 0 ? 255 : GREY_LIGHT;
            SDL_SetTextureColorMod(face, light, light, light);
            SDL_RenderTexture(renderer, face, &from, &at);
        }
        if (on >= 0) {
            corners(renderer, x, y, w, h, sx, CORNER * sx, KHDAYS_PLACE_COLOURS[on & 3]);
        }
    }
    /* the pages: the game's own first, then the mods' */
    const float dots_w = (float)v.ds_pages * DOT * 2.0f - DOT, left = x0 + (256.0f - dots_w) / 2.0f * sx;
    for (int p = 0; p < v.ds_pages; ++p) {
        const Uint8 grey = p == v.ds_page ? 255 : 90;
        fill(renderer, left + (float)p * DOT * 2.0f * sx, y0 + DOTS_TOP * sy, DOT * sx, DOT * sy, grey, grey, grey);
    }
}

void khdays_ds_select_pages_renderer_gone(void)
{
    khdays_mod_faces_forget(&faces);
    layout.renderer = NULL;
}
