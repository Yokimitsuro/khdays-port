/* A screen of the port's drawn from a mod's pieces (layout_screen.h). */
#include "layout_screen.h"

#include "../app/devices.h"
#include "../render/png_io.h"
#include "../app/profile.h"
#include "../settings/settings_ui.h"
#include "../../runtime/mods/installed.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the DS's buttons in the game's font: a glyph or three for each, the input's action (the
 * Preferences page's words, UI/cm/str/cfg_&.s, as the DS draws them: R, L, the D-pad's four ways,
 * SELECT, and the D-pad whole -- "use it to go over the list" -- as its UP and DOWN) */
#define GLYPH_A 0x3349u
#define GLYPH_B 0x3314u
#define GLYPH_X 0x3322u
/* the port's own for Y, which the game's text never writes (a private use code) */
#define GLYPH_Y KHDAYS_LAYOUT_GLYPH_Y
static const struct {
    unsigned short glyph[3];
    int length;
    int action[2];
    int actions;
} BUTTON_GLYPHS[] = {
    {{0x3336u, 0x3351u, 0x3357u}, 3, {KHDAYS_ACT_START, 0}, 1},
    {{0x330du, 0x3326u, 0x3323u}, 3, {KHDAYS_ACT_SELECT, 0}, 1},
    {{GLYPH_A, 0, 0}, 1, {KHDAYS_ACT_A, 0}, 1},
    {{GLYPH_B, 0, 0}, 1, {KHDAYS_ACT_B, 0}, 1},
    {{GLYPH_X, 0, 0}, 1, {KHDAYS_ACT_X, 0}, 1},
    {{GLYPH_Y, 0, 0}, 1, {KHDAYS_ACT_Y, 0}, 1},
    {{0x3327u, 0, 0}, 1, {KHDAYS_ACT_R, 0}, 1},
    {{0x3318u, 0, 0}, 1, {KHDAYS_ACT_L, 0}, 1},
    {{0x333bu, 0, 0}, 1, {KHDAYS_ACT_RIGHT, 0}, 1},
    {{0x337bu, 0, 0}, 1, {KHDAYS_ACT_LEFT, 0}, 1},
    {{0x332bu, 0, 0}, 1, {KHDAYS_ACT_UP, 0}, 1},
    {{0x334au, 0, 0}, 1, {KHDAYS_ACT_DOWN, 0}, 1},
    {{0x3303u, 0, 0}, 1, {KHDAYS_ACT_UP, KHDAYS_ACT_DOWN}, 2},
};

/* Kenney's gamepad icons (settings_menu.cpp's): their textures, made from the executable's PNGs, on the
 * renderer they were made on */
static struct {
    char name[KHDAYS_LAYOUT_NAME];
    SDL_Texture *texture;
} *kenney_icons;
static int kenney_icon_count;
static SDL_Renderer *kenney_renderer;

static void *grow(void *array, int count, size_t size)
{
    /* room for one more: doubles at each power of two */
    if (count == 0 || (count & (count - 1)) == 0) {
        void *bigger = realloc(array, size * (size_t)(count == 0 ? 8 : count * 2));
        if (bigger == NULL) {
            return NULL;
        }
        return bigger;
    }
    return array;
}

static int compare_glyphs(const void *a, const void *b)
{
    const unsigned x = ((const KhdaysLayoutGlyph *)a)->code, y = ((const KhdaysLayoutGlyph *)b)->code;
    return x < y ? -1 : x > y;
}

/* ---- The pieces' file ------------------------------------------------------------------------- */

void khdays_layout_forget(KhdaysLayout *l)
{
    if (l->atlas != NULL) {
        SDL_DestroyTexture(l->atlas);
    }
    if (l->font != NULL) {
        SDL_DestroyTexture(l->font);
    }
    l->atlas = l->font = NULL;
    free(l->pieces);
    free(l->places);
    free(l->texts);
    free(l->glyphs);
    l->pieces = NULL;
    l->places = NULL;
    l->texts = NULL;
    l->glyphs = NULL;
    l->piece_count = l->place_count = l->text_count = l->glyph_count = 0;
    l->font_colours = 0;
}

void khdays_layout_utf8(const char *in, unsigned short *out, int room)
{
    int n = 0;
    while (*in != '\0' && n < room - 1) {
        const unsigned char c = (unsigned char)*in;
        unsigned code;
        int extra;
        if (c < 0x80) {
            code = c;
            extra = 0;
        } else if ((c & 0xe0) == 0xc0) {
            code = c & 0x1fu;
            extra = 1;
        } else if ((c & 0xf0) == 0xe0) {
            code = c & 0x0fu;
            extra = 2;
        } else {
            code = '?';
            extra = 3;
        }
        ++in;
        for (int i = 0; i < extra && (*in & 0xc0) == 0x80; ++i, ++in) {
            code = code << 6 | ((unsigned char)*in & 0x3fu);
        }
        out[n++] = (unsigned short)(code > 0xffff ? '?' : code);
    }
    out[n] = 0;
}

SDL_Texture *khdays_layout_texture(KhdaysLayout *l, const char *path, int grey_as_alpha, int *w, int *h)
{
    const char *error = NULL;
    uint8_t *px = khdays_png_read(path, w, h, &error);
    SDL_Texture *t;
    if (px == NULL) {
        SDL_Log("%s: %s: %s", l->who, path, error != NULL ? error : "not read");
        return NULL;
    }
    if (grey_as_alpha) {
        for (int i = 0; i < *w * *h; ++i) {
            px[4 * i + 3] = px[4 * i];
            px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = 255;
        }
    }
    t = SDL_CreateTexture(l->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, *w, *h);
    if (t != NULL) {
        SDL_UpdateTexture(t, NULL, px, *w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    khdays_png_free(px);
    return t;
}

static int add_piece(KhdaysLayout *l, const char *name, float u, float v, float w, float h)
{
    KhdaysLayoutPiece *p = (KhdaysLayoutPiece *)grow(l->pieces, l->piece_count, sizeof(KhdaysLayoutPiece));
    if (p == NULL) {
        return 0;
    }
    l->pieces = p;
    snprintf(p[l->piece_count].name, KHDAYS_LAYOUT_NAME, "%s", name);
    p[l->piece_count].u = u;
    p[l->piece_count].v = v;
    p[l->piece_count].w = w;
    p[l->piece_count].h = h;
    ++l->piece_count;
    return 1;
}

int khdays_layout_line(KhdaysLayout *l, char *line, const char *folder)
{
    char name[KHDAYS_LAYOUT_NAME] = "", icon[KHDAYS_LAYOUT_NAME + 8] = "", file[256] = "", align[16] = "",
         kind[16] = "";
    float a, b, c, d, e, g;
    unsigned code;
    int grey, fields, rgb[3];
    if (SDL_sscanf(line, "piece %39s %f %f %f %f", name, &a, &b, &c, &d) == 5) {
        add_piece(l, name, a, b, c, d);
    } else if (SDL_sscanf(line, "icon %39s %f %f %f %f", name, &a, &b, &c, &d) == 5) {
        snprintf(icon, sizeof(icon), "icon %s", name);
        add_piece(l, icon, a, b, c, d);
    } else if (SDL_sscanf(line, "place %39s %f %f %f %f", name, &a, &b, &c, &d) == 5) {
        KhdaysLayoutPlace *p = (KhdaysLayoutPlace *)grow(l->places, l->place_count, sizeof(KhdaysLayoutPlace));
        if (p != NULL) {
            l->places = p;
            snprintf(p[l->place_count].name, KHDAYS_LAYOUT_NAME, "%s", name);
            p[l->place_count].x = a;
            p[l->place_count].y = b;
            p[l->place_count].w = c;
            p[l->place_count].h = d;
            ++l->place_count;
        }
    } else if ((fields = SDL_sscanf(line, "text %39s %f %f %f %15s %d %d %d %d", name, &a, &b, &c, align, &grey,
                                    &rgb[0], &rgb[1], &rgb[2])) == 6 ||
               fields == 9) {
        KhdaysLayoutText *t = (KhdaysLayoutText *)grow(l->texts, l->text_count, sizeof(KhdaysLayoutText));
        if (t != NULL) {
            l->texts = t;
            snprintf(t[l->text_count].name, KHDAYS_LAYOUT_NAME, "%s", name);
            t[l->text_count].x = a;
            t[l->text_count].y = b;
            t[l->text_count].size = c;
            t[l->text_count].align = strcmp(align, "centre") == 0 ? 1 : strcmp(align, "right") == 0 ? 2 : 0;
            t[l->text_count].grey = grey;
            for (int k = 0; k < 3; ++k) {
                t[l->text_count].colour[k] = fields == 9 ? rgb[k] : grey;
            }
            ++l->text_count;
        }
    } else if (SDL_sscanf(line, "atlas %255s", file) == 1) {
        char full[1300];
        snprintf(full, sizeof(full), "%s%s", folder, file);
        l->atlas = khdays_layout_texture(l, full, 0, &l->aw, &l->ah);
    } else if ((fields = SDL_sscanf(line, "font %255s %f %f %f %15s", file, &a, &b, &c, kind)) >= 4) {
        char full[1300];
        l->font_colours = fields == 5 && strcmp(kind, "rgba") == 0;
        snprintf(full, sizeof(full), "%s%s", folder, file);
        l->font = khdays_layout_texture(l, full, !l->font_colours, &l->fw, &l->fh);
        l->cell = a;
        l->ink_top = b;
        l->ink_bottom = c;
    } else if (SDL_sscanf(line, "glyph %u %f %f %f %f %f", &code, &a, &b, &c, &e, &g) == 6) {
        KhdaysLayoutGlyph *gl = (KhdaysLayoutGlyph *)grow(l->glyphs, l->glyph_count, sizeof(KhdaysLayoutGlyph));
        if (gl != NULL) {
            l->glyphs = gl;
            gl[l->glyph_count].code = code;
            gl[l->glyph_count].x = a;
            gl[l->glyph_count].y = b;
            gl[l->glyph_count].w = c;
            gl[l->glyph_count].left = e;
            gl[l->glyph_count].advance = g;
            ++l->glyph_count;
        }
    } else {
        return 0;
    }
    return 1;
}

int khdays_layout_ready(KhdaysLayout *l)
{
    if (l->glyph_count > 0) {
        qsort(l->glyphs, (size_t)l->glyph_count, sizeof(KhdaysLayoutGlyph), compare_glyphs);
    }
    return l->atlas != NULL && l->font != NULL && l->piece_count > 0;
}

const KhdaysLayoutPiece *khdays_layout_piece(const KhdaysLayout *l, const char *name)
{
    for (int i = 0; i < l->piece_count; ++i) {
        if (strcmp(l->pieces[i].name, name) == 0) {
            return &l->pieces[i];
        }
    }
    return NULL;
}

const KhdaysLayoutPlace *khdays_layout_place(const KhdaysLayout *l, const char *name)
{
    for (int i = 0; i < l->place_count; ++i) {
        if (strcmp(l->places[i].name, name) == 0) {
            return &l->places[i];
        }
    }
    return NULL;
}

const KhdaysLayoutText *khdays_layout_text(const KhdaysLayout *l, const char *name)
{
    for (int i = 0; i < l->text_count; ++i) {
        if (strcmp(l->texts[i].name, name) == 0) {
            return &l->texts[i];
        }
    }
    return NULL;
}

static const KhdaysLayoutGlyph *glyph(const KhdaysLayout *l, unsigned code)
{
    KhdaysLayoutGlyph key;
    key.code = code;
    return (const KhdaysLayoutGlyph *)bsearch(&key, l->glyphs, (size_t)l->glyph_count, sizeof(KhdaysLayoutGlyph),
                                              compare_glyphs);
}

/* ---- The game's strings ----------------------------------------------------------------------- */

void khdays_layout_strings(KhdaysRomReader *reader, const char *path, KhdaysLayoutStrings *out, const char *who)
{
    u8 *data = NULL;
    u32 size = 0, at, count;
    if (!khdays_rom_file_data(reader, path, -1, &data, &size) || size < 8) {
        SDL_Log("%s: %s not read", who, path);
        free(data);
        return;
    }
    at = (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
    count = (u32)data[4] | (u32)data[5] << 8 | (u32)data[6] << 16 | (u32)data[7] << 24;
    out->text = (unsigned short **)calloc(count, sizeof(*out->text));
    for (u32 i = 0; out->text != NULL && i < count && at + 4 <= size; ++i) {
        const u32 len = (u32)data[at] | (u32)data[at + 1] << 8 | (u32)data[at + 2] << 16 | (u32)data[at + 3] << 24;
        const u32 units = len >= 4 && at + len <= size ? (len - 4) / 2 : 0;
        unsigned short *t = (unsigned short *)calloc(units + 1, sizeof(*t));
        if (t == NULL || len < 4) {
            free(t);
            break;
        }
        for (u32 k = 0; k < units; ++k) {
            t[k] = (unsigned short)(data[at + 4 + 2 * k] | data[at + 5 + 2 * k] << 8);
        }
        out->text[out->count++] = t;
        at += len;
    }
    free(data);
}

void khdays_layout_strings_free(KhdaysLayoutStrings *s)
{
    for (int i = 0; i < s->count; ++i) {
        free(s->text[i]);
    }
    free(s->text);
    s->text = NULL;
    s->count = 0;
}

const unsigned short *khdays_layout_string(const KhdaysLayoutStrings *s, int record)
{
    static const unsigned short none[1] = {0};
    return record >= 0 && record < s->count ? s->text[record] : none;
}

/* ---- Drawing --------------------------------------------------------------------------------- */

void khdays_layout_begin(SDL_Renderer *renderer, KhdaysLayoutFrame *fr)
{
    int w = 0, h = 0;
    /* in the window's own pixels: the picture's logical size set aside while the screen draws */
    fr->logical_w = fr->logical_h = 0;
    fr->mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer, &fr->logical_w, &fr->logical_h, &fr->mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    fr->scale = fminf((float)w / KHDAYS_LAYOUT_W, (float)h / KHDAYS_LAYOUT_H);
    fr->ox = ((float)w - KHDAYS_LAYOUT_W * fr->scale) / 2;
    fr->oy = ((float)h - KHDAYS_LAYOUT_H * fr->scale) / 2;
    /* the black around the picture */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

void khdays_layout_begin_over(SDL_Renderer *renderer, KhdaysLayoutFrame *fr)
{
    int w = 0, h = 0;
    fr->logical_w = fr->logical_h = 0;
    fr->mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer, &fr->logical_w, &fr->logical_h, &fr->mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    fr->scale = fminf((float)w / KHDAYS_LAYOUT_W, (float)h / KHDAYS_LAYOUT_H);
    fr->ox = ((float)w - KHDAYS_LAYOUT_W * fr->scale) / 2;
    fr->oy = ((float)h - KHDAYS_LAYOUT_H * fr->scale) / 2;
    /* black only around the picture: what is under it stays */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    {
        const SDL_FRect bars[4] = {{0, 0, fr->ox, (float)h},
                                   {fr->ox + KHDAYS_LAYOUT_W * fr->scale, 0, (float)w, (float)h},
                                   {0, 0, (float)w, fr->oy},
                                   {0, fr->oy + KHDAYS_LAYOUT_H * fr->scale, (float)w, (float)h}};
        for (int i = 0; i < 4; ++i) {
            if (bars[i].w > 0 && bars[i].h > 0) {
                SDL_RenderFillRect(renderer, &bars[i]);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

void khdays_layout_end(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr)
{
    SDL_SetRenderLogicalPresentation(renderer, fr->logical_w, fr->logical_h, fr->mode);
}

SDL_FRect khdays_layout_to_window(const KhdaysLayoutFrame *fr, float x, float y, float w, float h)
{
    SDL_FRect r;
    r.x = fr->ox + x * fr->scale;
    r.y = fr->oy + y * fr->scale;
    r.w = w * fr->scale;
    r.h = h * fr->scale;
    return r;
}

void khdays_layout_draw_tinted(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *name, float x, float y,
                               float w, float h, const Uint8 colour[3], float alpha)
{
    const KhdaysLayoutPiece *p = khdays_layout_piece(l, name);
    SDL_FRect src, dst;
    if (p == NULL) {
        return;
    }
    src.x = p->u;
    src.y = p->v;
    src.w = p->w;
    src.h = p->h;
    dst = khdays_layout_to_window(fr, x, y, w, h);
    SDL_SetTextureAlphaModFloat(l->atlas, alpha);
    if (colour != NULL) {
        SDL_SetTextureColorMod(l->atlas, colour[0], colour[1], colour[2]);
    }
    SDL_RenderTexture(l->renderer, l->atlas, &src, &dst);
    SDL_SetTextureColorMod(l->atlas, 255, 255, 255);
    SDL_SetTextureAlphaModFloat(l->atlas, 1.0f);
}

void khdays_layout_draw_piece(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *name, float x, float y,
                              float w, float h, float alpha)
{
    khdays_layout_draw_tinted(l, fr, name, x, y, w, h, NULL, alpha);
}

void khdays_layout_draw_placed(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *name, float dx, float dy,
                               float alpha)
{
    const KhdaysLayoutPlace *pl = khdays_layout_place(l, name);
    if (pl != NULL) {
        khdays_layout_draw_piece(l, fr, name, dx + pl->x, dy + pl->y, pl->w, pl->h, alpha);
    }
}

/* What a piece of a line is: a glyph, or an input's icon in place of one of the DS's buttons. */
typedef struct Icon {
    SDL_Texture *texture;
    SDL_FRect src;
    unsigned short label[24]; /* a key's name when no icon is there */
    float h;                  /* with the Menu Overhaul, its height on every screen (the layout's units), or 0 */
} Icon;

/* With the Menu Overhaul (runtime/mods/installed.h) every button's icon is one size on every screen, as the KH
 * collections show theirs: a pad's at its picture's size at 1080 lines (the 36-texel face buttons ICON_PAD
 * high), a key's at ICON_KEY, the Switch's at ICON_PAD; in a line, ICON_GAP either side. Else in a line
 * 1.5 times the capitals' height, and given a place, its height. */
#define ICON_PAD 38.0f
#define ICON_FACE 36.0f
#define ICON_KEY 42.0f
#define ICON_GAP 4.0f

static int overhaul(void)
{
    return khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL);
}

/* An icon's height in a line whose capitals are `ink` high. */
static float line_icon_height(const Icon *icon, float ink)
{
    return overhaul() && icon->h > 0 ? icon->h : ink * 1.5f;
}

static SDL_Texture *kenney_icon(SDL_Renderer *renderer, const char *name)
{
    const unsigned char *png;
    unsigned size = 0;
    SDL_Texture *texture = NULL;
    if (renderer != kenney_renderer) {
        kenney_icon_count = 0; /* another renderer's: gone with it */
        kenney_renderer = renderer;
    }
    for (int i = 0; i < kenney_icon_count; ++i) {
        if (strcmp(kenney_icons[i].name, name) == 0) {
            return kenney_icons[i].texture;
        }
    }
    png = khdays_pad_icon_png(name, &size);
    if (png != NULL) {
        SDL_IOStream *io = SDL_IOFromConstMem(png, size);
        SDL_Surface *surface = io != NULL ? SDL_LoadPNG_IO(io, true) : NULL;
        if (surface != NULL) {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_DestroySurface(surface);
        }
        if (texture != NULL) {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        }
    }
    {
        void *more = grow(kenney_icons, kenney_icon_count, sizeof(*kenney_icons));
        if (more != NULL) {
            kenney_icons = more;
            snprintf(kenney_icons[kenney_icon_count].name, KHDAYS_LAYOUT_NAME, "%s", name);
            kenney_icons[kenney_icon_count++].texture = texture;
        }
    }
    return texture;
}

/* The icon of one input (a pad's `on_pad`, else a key), or its name in `label` when it has none; 0
 * when it has neither. */
static int icon_of_input(const KhdaysLayout *l, SDL_Gamepad *pad, int on_pad, int input, Icon *out)
{
    char name[KHDAYS_LAYOUT_NAME + 8];
    const KhdaysLayoutPiece *p;
    memset(out, 0, sizeof(*out));
    if (on_pad) {
        const SDL_GamepadType type = SDL_GetGamepadType(pad);
        const char *input_name = khdays_pad_input_name(input);
        if (input_name == NULL) {
            return 0;
        }
        if (type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE || type == SDL_GAMEPAD_TYPE_PS3 ||
            type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5) {
            const int xbox = type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE;
            snprintf(name, sizeof(name), "icon %s:%s", xbox ? "xbox" : "playstation", input_name);
            p = khdays_layout_piece(l, name);
            if (p != NULL) {
                out->texture = l->atlas;
                out->src.x = p->u;
                out->src.y = p->v;
                out->src.w = p->w;
                out->src.h = p->h;
                out->h = p->h * ICON_PAD / ICON_FACE;
                return 1;
            }
            {
                /* not among the layout's pieces (the pieces have no Guide, nor the sticks pressed): Kenney's, as
                 * the Controls section shows it (a PlayStation's PS button has none: the action's next input) */
                const char *kenney = khdays_pad_icon_name(input);
                SDL_Texture *t = kenney != NULL ? kenney_icon(l->renderer, kenney) : NULL;
                if (t != NULL) {
                    out->texture = t;
                    out->src.x = out->src.y = 0;
                    SDL_GetTextureSize(t, &out->src.w, &out->src.h);
                    out->h = ICON_PAD;
                    return 1;
                }
            }
        } else {
            /* any other pad: Switch's letters, the DS's in the same places */
            static const char *const switch_names[KHDAYS_PAD_COUNT] = {[KHDAYS_PAD_SOUTH] = "switch_button_b",
                                                                       [KHDAYS_PAD_EAST] = "switch_button_a",
                                                                       [KHDAYS_PAD_WEST] = "switch_button_y",
                                                                       [KHDAYS_PAD_NORTH] = "switch_button_x",
                                                                       [KHDAYS_PAD_DPUP] = "switch_dpad_up",
                                                                       [KHDAYS_PAD_DPDOWN] = "switch_dpad_down",
                                                                       [KHDAYS_PAD_DPLEFT] = "switch_dpad_left",
                                                                       [KHDAYS_PAD_DPRIGHT] = "switch_dpad_right",
                                                                       [KHDAYS_PAD_LEFTSHOULDER] = "switch_button_l",
                                                                       [KHDAYS_PAD_RIGHTSHOULDER] = "switch_button_r",
                                                                       [KHDAYS_PAD_LEFTTRIGGER] = "switch_button_zl",
                                                                       [KHDAYS_PAD_RIGHTTRIGGER] = "switch_button_zr",
                                                                       [KHDAYS_PAD_LEFTSTICK] = "switch_stick_l_press",
                                                                       [KHDAYS_PAD_RIGHTSTICK] = "switch_stick_r_press",
                                                                       [KHDAYS_PAD_START] = "switch_button_plus",
                                                                       [KHDAYS_PAD_BACK] = "switch_button_minus",
                                                                       [KHDAYS_PAD_GUIDE] = "switch_button_home"};
            const char *icon = input < KHDAYS_PAD_COUNT ? switch_names[input] : NULL;
            SDL_Texture *t = icon != NULL ? kenney_icon(l->renderer, icon) : NULL;
            if (t != NULL) {
                out->texture = t;
                out->src.x = out->src.y = 0;
                SDL_GetTextureSize(t, &out->src.w, &out->src.h);
                out->h = ICON_PAD;
                return 1;
            }
        }
        khdays_layout_utf8(input_name, out->label, (int)(sizeof(out->label) / sizeof(out->label[0])));
        return 1;
    }
    if (input >= KHDAYS_MOUSE_INPUT && input < KHDAYS_KEYBOARD_INPUTS) {
        /* a mouse button: Kenney's, as the Controls section shows it */
        static const char *const mouse[KHDAYS_MOUSE_COUNT] = {
            [KHDAYS_MOUSE_LEFT] = "mouse_left",
            [KHDAYS_MOUSE_MIDDLE] = "mouse_scroll",
            [KHDAYS_MOUSE_RIGHT] = "mouse_right",
            [KHDAYS_MOUSE_X1] = "mouse_side_back",
            [KHDAYS_MOUSE_X2] = "mouse_side_forward",
            [KHDAYS_MOUSE_WHEEL_UP] = "mouse_scroll_up",
            [KHDAYS_MOUSE_WHEEL_DOWN] = "mouse_scroll_down",
        };
        const char *icon = mouse[input - KHDAYS_MOUSE_INPUT];
        SDL_Texture *t = icon != NULL ? kenney_icon(l->renderer, icon) : NULL;
        if (t == NULL) {
            return 0;
        }
        out->texture = t;
        out->src.x = out->src.y = 0;
        SDL_GetTextureSize(t, &out->src.w, &out->src.h);
        return 1;
    }
    snprintf(name, sizeof(name), "icon key:%d", input);
    p = khdays_layout_piece(l, name);
    if (p != NULL) {
        out->texture = l->atlas;
        out->src.x = p->u;
        out->src.y = p->v;
        out->src.w = p->w;
        out->src.h = p->h;
        out->h = ICON_KEY;
        return 1;
    }
    khdays_layout_utf8(SDL_GetScancodeName((SDL_Scancode)input), out->label,
                       (int)(sizeof(out->label) / sizeof(out->label[0])));
    return out->label[0] != 0;
}

/* The icon of the input the player has for DS button `action` (KHDAYS_ACT_*) on the device played: of
 * its first input with one (the pad's Guide has none, its L3 does), else the first's name. */
static int input_icon(const KhdaysLayout *l, int action, Icon *out)
{
    const KhdaysProfile *profile = khdays_devices_profile();
    SDL_Gamepad *pad = khdays_devices_gamepad();
    const int on_pad = pad != NULL && khdays_devices_pad_in_use();
    const int16_t *inputs =
        profile->input[on_pad ? KHDAYS_DEVICE_GAMEPAD : KHDAYS_DEVICE_KEYBOARD][KHDAYS_LAYER_BASE][action];
    int named = 0;
    memset(out, 0, sizeof(*out));
    for (int k = 0; k < KHDAYS_INPUTS_PER_ACTION; ++k) {
        Icon icon;
        if (inputs[k] == KHDAYS_NO_INPUT || !icon_of_input(l, pad, on_pad, inputs[k], &icon)) {
            continue;
        }
        if (icon.texture != NULL) {
            *out = icon;
            return 1;
        }
        if (!named) {
            *out = icon;
            named = 1;
        }
    }
    return named;
}

/* With the Menu Overhaul, a DS button's own letter for a button the player has no input with an icon or a name
 * for (the mouse's): A, B, X or Y in brackets rather than nothing. */
static int button_letter(int action, Icon *out)
{
    const char letter = !overhaul()              ? 0
                        : action == KHDAYS_ACT_A ? 'A'
                        : action == KHDAYS_ACT_B ? 'B'
                        : action == KHDAYS_ACT_X ? 'X'
                        : action == KHDAYS_ACT_Y ? 'Y'
                                                 : 0;
    memset(out, 0, sizeof(*out));
    if (letter == 0) {
        return 0;
    }
    out->label[0] = (unsigned short)letter;
    return 1;
}

/* The DS's button the text has here: its actions (one or two) and its glyphs' count; 0 none. */
static int buttons_at(const unsigned short *t, int *length, int action[2])
{
    *length = 1;
    for (size_t b = 0; b < sizeof(BUTTON_GLYPHS) / sizeof(BUTTON_GLYPHS[0]); ++b) {
        int i = 0;
        while (i < BUTTON_GLYPHS[b].length && t[i] == BUTTON_GLYPHS[b].glyph[i]) {
            ++i;
        }
        if (i == BUTTON_GLYPHS[b].length) {
            *length = i;
            action[0] = BUTTON_GLYPHS[b].action[0];
            action[1] = BUTTON_GLYPHS[b].action[1];
            return BUTTON_GLYPHS[b].actions;
        }
    }
    return 0;
}

float khdays_layout_line_width(const KhdaysLayout *l, const unsigned short *t, float size)
{
    float w = 0;
    const float ink = (l->ink_bottom - l->ink_top) * size, gap = overhaul() ? ICON_GAP : 0.0f;
    for (; *t != 0; ++t) {
        Icon icon;
        int length, action[2], drawn = 0;
        const int buttons = buttons_at(t, &length, action);
        for (int b = 0; b < buttons; ++b) {
            if (input_icon(l, action[b], &icon) || button_letter(action[b], &icon)) {
                drawn = 1;
                if (icon.texture != NULL) {
                    w += icon.src.w * (line_icon_height(&icon, ink) / icon.src.h) + gap;
                } else {
                    w += khdays_layout_line_width(l, icon.label, size) + 2 * size * 4;
                }
            }
        }
        if (drawn) {
            t += length - 1;
            continue;
        }
        {
            const KhdaysLayoutGlyph *g = glyph(l, *t);
            w += (g != NULL ? g->advance : l->cell * 0.3f) * size;
        }
    }
    return w;
}

static void draw_line_rgb(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y,
                          float size, const float rgb[3], float alpha, int shadow);

static float draw_icon(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const Icon *got, float x, float y, float h,
                       float alpha);

float khdays_layout_draw_action(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, int action, float x, float y,
                                float h, float alpha)
{
    Icon icon;
    if (!input_icon(l, action, &icon)) {
        return 0;
    }
    return draw_icon(l, fr, &icon, x, y, h, alpha);
}

float khdays_layout_draw_action_key(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, int action, int key, float x,
                                    float y, float h, float alpha)
{
    SDL_Gamepad *pad = khdays_devices_gamepad();
    const int on_pad = pad != NULL && khdays_devices_pad_in_use();
    if (!on_pad && overhaul()) {
        const int16_t *inputs = khdays_devices_profile()->input[KHDAYS_DEVICE_KEYBOARD][KHDAYS_LAYER_BASE][action];
        for (int k = 0; k < KHDAYS_INPUTS_PER_ACTION; ++k) {
            Icon icon;
            if (inputs[k] == key && icon_of_input(l, pad, 0, inputs[k], &icon)) {
                return draw_icon(l, fr, &icon, x, y, h, alpha);
            }
        }
    }
    return khdays_layout_draw_action(l, fr, action, x, y, h, alpha);
}

static float draw_icon(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const Icon *got, float x, float y, float h,
                       float alpha)
{
    const Icon icon = *got;
    if (icon.texture != NULL) {
        const float ih = overhaul() && icon.h > 0 ? icon.h : h, w = icon.src.w * (ih / icon.src.h);
        const SDL_FRect dst = khdays_layout_to_window(fr, x, y + (h - ih) / 2, w, ih);
        SDL_SetTextureAlphaModFloat(icon.texture, alpha);
        SDL_RenderTexture(l->renderer, icon.texture, &icon.src, &dst);
        SDL_SetTextureAlphaModFloat(icon.texture, 1.0f);
        return w;
    }
    {
        /* its name in brackets, its capitals as tall as the icon's middle half */
        const float size = h / 2 / (l->ink_bottom - l->ink_top);
        const unsigned short open[2] = {'[', 0}, close[2] = {']', 0};
        const float y0 = y + h / 4;
        float at = x;
        khdays_layout_draw_line(l, fr, open, at, y0, size, 255, alpha, 0);
        at += khdays_layout_line_width(l, open, size);
        khdays_layout_draw_line(l, fr, icon.label, at, y0, size, 255, alpha, 0);
        at += khdays_layout_line_width(l, icon.label, size);
        khdays_layout_draw_line(l, fr, close, at, y0, size, 255, alpha, 0);
        at += khdays_layout_line_width(l, close, size);
        return at - x;
    }
}

void khdays_layout_draw_line(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const unsigned short *t, float x,
                             float y, float size, float grey, float alpha, int shadow)
{
    const float rgb[3] = {grey, grey, grey};
    draw_line_rgb(l, fr, t, x, y, size, rgb, alpha, shadow);
}

void khdays_layout_draw_line_rgb(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const unsigned short *t, float x,
                                 float y, float size, const Uint8 rgb[3], float alpha, int shadow)
{
    const float c[3] = {rgb[0], rgb[1], rgb[2]};
    draw_line_rgb(l, fr, t, x, y, size, c, alpha, shadow);
}

/* A line in a colour (0-255 each): a plain font's grey made it, an outlined font's white. */
static void draw_line_rgb(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const unsigned short *t, float x, float y,
                          float size, const float rgb[3], float alpha, int shadow)
{
    const float ink = (l->ink_bottom - l->ink_top) * size;
    const float top = y - l->ink_top * size; /* the cells' top */
    SDL_Vertex v[4];
    static const int order[6] = {0, 1, 2, 2, 1, 3};
    const float offset = shadow ? 3.0f : 0.0f;
    const SDL_FColor colour = {shadow ? 0.0f : rgb[0] / 255.0f, shadow ? 0.0f : rgb[1] / 255.0f,
                               shadow ? 0.0f : rgb[2] / 255.0f, alpha * (shadow ? 0.7f : 1.0f)};
    for (; *t != 0; ++t) {
        Icon icon;
        const KhdaysLayoutGlyph *gl;
        int length, action[2], drawn = 0;
        const int buttons = buttons_at(t, &length, action);
        for (int b = 0; b < buttons; ++b) {
            if (!input_icon(l, action[b], &icon) && !button_letter(action[b], &icon)) {
                continue;
            }
            drawn = 1;
            if (icon.texture != NULL) {
                const float h = line_icon_height(&icon, ink), w = icon.src.w * (h / icon.src.h);
                const float gap = overhaul() ? ICON_GAP : 0.0f;
                if (!shadow) {
                    const SDL_FRect dst = khdays_layout_to_window(fr, x + gap / 2, y + ink / 2 - h / 2, w, h);
                    SDL_SetTextureAlphaModFloat(icon.texture, alpha);
                    SDL_RenderTexture(l->renderer, icon.texture, &icon.src, &dst);
                    SDL_SetTextureAlphaModFloat(icon.texture, 1.0f);
                }
                x += w + gap;
            } else {
                const unsigned short open[2] = {'[', 0}, close[2] = {']', 0};
                draw_line_rgb(l, fr, open, x, y, size, rgb, alpha, shadow);
                x += khdays_layout_line_width(l, open, size);
                draw_line_rgb(l, fr, icon.label, x, y, size, rgb, alpha, shadow);
                x += khdays_layout_line_width(l, icon.label, size);
                draw_line_rgb(l, fr, close, x, y, size, rgb, alpha, shadow);
                x += khdays_layout_line_width(l, close, size);
            }
        }
        if (drawn) {
            t += length - 1;
            continue;
        }
        gl = glyph(l, *t);
        if (gl == NULL) {
            x += l->cell * 0.3f * size;
            continue;
        }
        if (gl->w > 0) {
            const float gx = x + gl->left * size + offset, gy = top + offset;
            const SDL_FRect r = khdays_layout_to_window(fr, gx, gy, gl->w * size, l->cell * size);
            const float u0 = gl->x / (float)l->fw, v0 = gl->y / (float)l->fh;
            const float u1 = (gl->x + gl->w) / (float)l->fw, v1 = (gl->y + l->cell) / (float)l->fh;
            const float px[4] = {r.x, r.x + r.w, r.x, r.x + r.w}, py[4] = {r.y, r.y, r.y + r.h, r.y + r.h};
            const float tu[4] = {u0, u1, u0, u1}, tv[4] = {v0, v0, v1, v1};
            for (int i = 0; i < 4; ++i) {
                v[i].position.x = px[i];
                v[i].position.y = py[i];
                v[i].color = colour;
                v[i].tex_coord.x = tu[i];
                v[i].tex_coord.y = tv[i];
            }
            SDL_RenderGeometry(l->renderer, l->font, v, 4, order, 6);
        }
        x += gl->advance * size;
    }
}

void khdays_layout_draw_text(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *style,
                             const unsigned short *t, float dx, float dy, float alpha)
{
    const KhdaysLayoutText *st = khdays_layout_text(l, style);
    float x, y, w;
    if (st == NULL || t == NULL || t[0] == 0) {
        return;
    }
    w = khdays_layout_line_width(l, t, st->size);
    x = dx + st->x - (st->align == 1 ? w / 2 : st->align == 2 ? w : 0);
    y = dy + st->y - (st->align == 1 ? (l->ink_bottom - l->ink_top) * st->size / 2 : 0);
    {
        const float rgb[3] = {(float)st->colour[0], (float)st->colour[1], (float)st->colour[2]};
        if (!l->font_colours) {
            draw_line_rgb(l, fr, t, x, y, st->size, rgb, alpha, 1);
        }
        draw_line_rgb(l, fr, t, x, y, st->size, rgb, alpha, 0);
    }
}

/* The points around an oval, or around a rectangle with round corners (clockwise from the left),
 * in the window's pixels. */
enum {
    OVAL_STEPS = 48,
    ARC_STEPS = 10,
    PATH_MOST = 4 * (ARC_STEPS + 1) > OVAL_STEPS ? 4 * (ARC_STEPS + 1) : OVAL_STEPS
};

static int oval_path(SDL_FRect c, float *xy)
{
    for (int i = 0; i < OVAL_STEPS; ++i) {
        const float a = 3.14159265f + (float)i * 6.2831853f / OVAL_STEPS;
        xy[2 * i] = c.x + SDL_cosf(a) * c.w;
        xy[2 * i + 1] = c.y + SDL_sinf(a) * c.h;
    }
    return OVAL_STEPS;
}

static int rounded_path(SDL_FRect r, float rad, float *xy)
{
    /* the corners' middles, clockwise from the top left; each corner's arc a quarter turn on */
    const float mx[4] = {r.x + rad, r.x + r.w - rad, r.x + r.w - rad, r.x + rad};
    const float my[4] = {r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad};
    int n = 0;
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= ARC_STEPS; ++i) {
            const float a = 3.14159265f * (1.0f + 0.5f * (float)corner + 0.5f * (float)i / ARC_STEPS);
            xy[2 * n] = mx[corner] + SDL_cosf(a) * rad;
            xy[2 * n + 1] = my[corner] + SDL_sinf(a) * rad;
            ++n;
        }
    }
    return n;
}

/* The band between two paths of n points each (the inner one NULL: the shape filled). */
static void fill_band(SDL_Renderer *renderer, const float *outer, const float *inner, int n, const Uint8 colour[3],
                      float alpha)
{
    SDL_Vertex v[2 * PATH_MOST + 1];
    int order[6 * PATH_MOST];
    const SDL_FColor c = {colour[0] / 255.0f, colour[1] / 255.0f, colour[2] / 255.0f, alpha};
    int count = 0, k = 0;
    float cx = 0, cy = 0;
    for (int i = 0; i < n; ++i) {
        cx += outer[2 * i] / (float)n;
        cy += outer[2 * i + 1] / (float)n;
    }
    for (int i = 0; i < n; ++i) {
        v[count].position.x = outer[2 * i];
        v[count++].position.y = outer[2 * i + 1];
        v[count].position.x = inner != NULL ? inner[2 * i] : cx;
        v[count++].position.y = inner != NULL ? inner[2 * i + 1] : cy;
    }
    for (int i = 0; i < count; ++i) {
        v[i].color = c;
        v[i].tex_coord.x = v[i].tex_coord.y = 0;
    }
    for (int i = 0; i < n; ++i) {
        const int a = 2 * i, b = 2 * ((i + 1) % n);
        order[k++] = a;
        order[k++] = b;
        order[k++] = a + 1;
        order[k++] = a + 1;
        order[k++] = b;
        order[k++] = b + 1;
    }
    SDL_RenderGeometry(renderer, NULL, v, count, order, k);
}

void khdays_layout_fill_oval(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float cx, float cy, float rx,
                             float ry, const Uint8 colour[3], float alpha)
{
    float xy[2 * PATH_MOST];
    const int n = oval_path(khdays_layout_to_window(fr, cx, cy, rx, ry), xy);
    fill_band(renderer, xy, NULL, n, colour, alpha);
}

void khdays_layout_ring_oval(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float cx, float cy, float rx,
                             float ry, float thickness, const Uint8 colour[3], float alpha)
{
    float outer[2 * PATH_MOST], inner[2 * PATH_MOST];
    const int n = oval_path(khdays_layout_to_window(fr, cx, cy, rx, ry), outer);
    oval_path(khdays_layout_to_window(fr, cx, cy, rx - thickness, ry - thickness), inner);
    fill_band(renderer, outer, inner, n, colour, alpha);
}

static float corner(const KhdaysLayoutFrame *fr, SDL_FRect r, float radius)
{
    const SDL_FRect k = khdays_layout_to_window(fr, 0, 0, radius, radius);
    return SDL_max(0.0f, SDL_min(k.w, SDL_min(r.w, r.h) / 2));
}

void khdays_layout_fill_rounded(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float x, float y, float w, float h,
                                float radius, const Uint8 colour[3], float alpha)
{
    float xy[2 * PATH_MOST];
    const SDL_FRect r = khdays_layout_to_window(fr, x, y, w, h);
    const int n = rounded_path(r, corner(fr, r, radius), xy);
    fill_band(renderer, xy, NULL, n, colour, alpha);
}

void khdays_layout_ring_rounded(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float x, float y, float w, float h,
                                float radius, float thickness, const Uint8 colour[3], float alpha)
{
    float outer[2 * PATH_MOST], inner[2 * PATH_MOST];
    const SDL_FRect r = khdays_layout_to_window(fr, x, y, w, h);
    const SDL_FRect in =
        khdays_layout_to_window(fr, x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness);
    const int n = rounded_path(r, corner(fr, r, radius), outer);
    rounded_path(in, corner(fr, in, radius - thickness), inner);
    fill_band(renderer, outer, inner, n, colour, alpha);
}

void khdays_layout_shade(SDL_Renderer *renderer, int master)
{
    if (master != 0) {
        const float a = (float)(master < 0 ? -master : master) / 16.0f;
        const Uint8 c = master > 0 ? 255 : 0;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, c, c, c, (Uint8)(a * 255.0f + 0.5f));
        SDL_RenderFillRect(renderer, NULL);
    }
}

/* ---- KH2's firefly (layout_screen.h) ------------------------------------------------------------ */

static struct {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int tried;
} firefly;

static struct {
    double born, seen, moved; /* seconds: grown in, drawn last, set off to its place */
    float x, y, from_x, from_y;
} flies[KHDAYS_FIREFLY_COUNT];

static void firefly_forget(void)
{
    if (firefly.texture != NULL) {
        SDL_DestroyTexture(firefly.texture);
    }
    firefly.texture = NULL;
    firefly.renderer = NULL;
    firefly.tried = 0;
}

static SDL_Texture *firefly_picture(SDL_Renderer *renderer)
{
    char path[1024];
    if (renderer != firefly.renderer) {
        firefly_forget();
        firefly.renderer = renderer;
    }
    if (!firefly.tried) {
        firefly.tried = 1;
        if (overhaul() && khdays_mods_find("layouts/kh2pause/firefly.png", path, (int)sizeof(path))) {
            const char *error = NULL;
            int w = 0, h = 0;
            uint8_t *px = khdays_png_read(path, &w, &h, &error);
            if (px != NULL) {
                firefly.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
                if (firefly.texture != NULL) {
                    SDL_UpdateTexture(firefly.texture, NULL, px, w * 4);
                    /* KH2 adds its light to the picture (the animations' blend 1) */
                    SDL_SetTextureBlendMode(firefly.texture, SDL_BLENDMODE_ADD);
                    SDL_SetTextureScaleMode(firefly.texture, SDL_SCALEMODE_LINEAR);
                }
                khdays_png_free(px);
            } else {
                SDL_Log("firefly: %s: %s", path, error != NULL ? error : "not read");
            }
        }
    }
    return firefly.texture;
}

static float smooth01(float t)
{
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

void khdays_layout_firefly(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, int which, float x, float y, float unit,
                           float alpha)
{
    /* pause.2ld: group 7 grows it in over 16 frames from (-10, -5), its scale 0 to 1.25 and its light
     * 0 to full; group 8 then wanders it, 8 pixels each way, 26 and 34 swings over 1500 frames; the
     * picture is the scaled 1.25's (20 pixels across) */
    const double now = (double)SDL_GetTicksNS() / 1e9;
    SDL_Texture *t = renderer != NULL ? firefly_picture(renderer) : NULL;
    float grow, frames, wander, cx, cy, size, k;
    SDL_FRect dst;
    if (t == NULL || which < 0 || which >= KHDAYS_FIREFLY_COUNT || alpha <= 0.0f) {
        return;
    }
    if (now - flies[which].seen > 0.25) {
        flies[which].born = now;
        flies[which].moved = now - 1.0;
        flies[which].x = flies[which].from_x = x;
        flies[which].y = flies[which].from_y = y;
    } else if (fabsf(x - flies[which].x) > 0.5f || fabsf(y - flies[which].y) > 0.5f) {
        /* the selection moved: off from where it shows now */
        k = smooth01((float)((now - flies[which].moved) / 0.16));
        flies[which].from_x += (flies[which].x - flies[which].from_x) * k;
        flies[which].from_y += (flies[which].y - flies[which].from_y) * k;
        flies[which].x = x;
        flies[which].y = y;
        flies[which].moved = now;
    }
    flies[which].seen = now;
    k = smooth01((float)((now - flies[which].moved) / 0.16));
    cx = flies[which].from_x + (x - flies[which].from_x) * k;
    cy = flies[which].from_y + (y - flies[which].from_y) * k;
    frames = (float)((now - flies[which].born) * 60.0);
    grow = frames / 16.0f > 1.0f ? 1.0f : frames / 16.0f;
    wander = frames > 16.0f ? frames - 16.0f : 0.0f;
    wander = fmodf(wander, 1500.0f);
    cx += unit * (8.0f * sinf(3.14159265f * 26.0f * wander / 1500.0f) - 10.0f * (1.0f - grow));
    cy += unit * (8.0f * sinf(3.14159265f * 34.0f * wander / 1500.0f) - 5.0f * (1.0f - grow));
    size = 20.0f * unit * grow;
    if (size <= 0.0f) {
        return;
    }
    dst = khdays_layout_to_window(fr, cx - size / 2, cy - size / 2, size, size);
    SDL_SetTextureAlphaModFloat(t, alpha * grow);
    SDL_RenderTexture(renderer, t, NULL, &dst);
    SDL_SetTextureAlphaModFloat(t, 1.0f);
}

void khdays_layout_renderer_gone(void)
{
    for (int i = 0; i < kenney_icon_count; ++i) {
        if (kenney_icons[i].texture != NULL) {
            SDL_DestroyTexture(kenney_icons[i].texture);
        }
    }
    kenney_icon_count = 0;
    kenney_renderer = NULL;
    firefly_forget();
}

float khdays_layout_glove_sway(void)
{
    /* KH2's glove (camp.2ld's group 22, pause.2ld's 6: 4 of its screen's dots each way, three times in 89
     * frames), as large as on a full screen of the picture */
    const double period = KHDAYS_GLOVE_FRAMES / 60.0;
    const double t = fmod((double)SDL_GetTicks() / 1000.0, period) / period;
    if (!overhaul()) {
        return 0.0f;
    }
    return KHDAYS_GLOVE_MIDDLE - KHDAYS_GLOVE_REACH * sinf((float)(t * 6.28318530718));
}
