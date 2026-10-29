/* The mods' characters' faces and names on the port's screens (mod_faces.h). */
#include "mod_faces.h"

#include "../../runtime/characters/characters.h"

SDL_Texture *khdays_mod_face(KhdaysModFaces *f, KhdaysLayout *layout, int number)
{
    const KhdaysCharacter *c = khdays_characters_numbered(number);
    const int index = number - KHDAYS_CHARACTER_FIRST;
    char path[1024];
    int w, h;
    if (c == NULL || c->face == NULL) {
        return NULL;
    }
    if (index >= f->count) {
        SDL_Texture **faces = SDL_realloc(f->faces, (size_t)(index + 1) * sizeof(*faces));
        if (faces != NULL) {
            f->faces = faces;
        }
        char *tried = SDL_realloc(f->tried, (size_t)(index + 1));
        if (tried != NULL) {
            f->tried = tried;
        }
        if (faces == NULL || tried == NULL) {
            return NULL;
        }
        for (int i = f->count; i <= index; ++i) {
            f->faces[i] = NULL;
            f->tried[i] = 0;
        }
        f->count = index + 1;
    }
    if (!f->tried[index]) {
        f->tried[index] = 1;
        if (khdays_characters_file(c, c->face, path, (int)sizeof(path))) {
            f->faces[index] = khdays_layout_texture(layout, path, 0, &w, &h);
        }
    }
    return f->faces[index];
}

int khdays_mod_face_draw(KhdaysModFaces *f, KhdaysLayout *layout, const KhdaysLayoutFrame *fr, int number, float x,
                         float y, float w, float h, Uint8 light)
{
    SDL_Texture *t = khdays_mod_face(f, layout, number);
    if (t == NULL) {
        return 0;
    }
    const SDL_FRect r = khdays_layout_to_window(fr, x, y, w, h);
    SDL_SetTextureColorMod(t, light, light, light);
    SDL_RenderTexture(layout->renderer, t, NULL, &r);
    return 1;
}

const unsigned short *khdays_mod_name(int number, int weapon, int language, unsigned short *out, int room)
{
    const KhdaysCharacter *c = khdays_characters_numbered(number);
    if (c == NULL) {
        return NULL;
    }
    const char *text = weapon ? khdays_characters_weapon(c, language - 1) : khdays_characters_name(c, language - 1);
    khdays_layout_utf8(text != NULL ? text : "", out, room);
    return out;
}

void khdays_mod_faces_forget(KhdaysModFaces *f)
{
    for (int i = 0; i < f->count; ++i) {
        if (f->faces[i] != NULL) {
            SDL_DestroyTexture(f->faces[i]);
        }
    }
    SDL_free(f->faces);
    SDL_free(f->tried);
    f->faces = NULL;
    f->tried = NULL;
    f->count = 0;
}
