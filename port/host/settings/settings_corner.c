/* What the settings menu shows in the window while it is closed
 * (settings_ui.h): the FPS and ping displays, and the notices kept for when
 * the menu opens. They are drawn in the game's own font. */
#include "settings_ui.h"
#include "../screens/ui_font.h"
#include "../screens/ui_text.h"

#include <math.h>

#define NOTICES 4 /* how many of the newest notices are kept (all of them are in the log) */

static char notices[NOTICES][160];
static int notice_count;

void khdays_settings_add_notice(const char *text)
{
    if (notice_count == NOTICES) {
        for (int n = 1; n < NOTICES; ++n) {
            SDL_strlcpy(notices[n - 1], notices[n], sizeof(notices[0])); /* drop the oldest */
        }
        --notice_count;
    }
    SDL_strlcpy(notices[notice_count++], text, sizeof(notices[0]));
}

int khdays_settings_notice_count(void)
{
    return notice_count;
}

const char *khdays_settings_notice(int n)
{
    return n >= 0 && n < notice_count ? notices[n] : "";
}

typedef struct Color {
    Uint8 r, g, b;
} Color;

static const Color white = {240, 240, 240};

/* Draws one line of text at (x, y). */
static void text(SDL_Renderer *renderer, float x, float y, float scale, Color c, const char *s)
{
    int w = 0, h = 0;
    SDL_Texture *t = khdays_ui_font_text(renderer, s, &w, &h);
    if (t != NULL) {
        const SDL_FRect dst = {x, y, (float)w * scale, (float)h * scale};
        /* sharp pixels at whole scales, smoothed at fractional ones */
        SDL_SetTextureScaleMode(t, scale == floorf(scale) ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        SDL_SetTextureColorMod(t, c.r, c.g, c.b);
        SDL_RenderTexture(renderer, t, NULL, &dst);
        return;
    }
    /* without the game's font, SDL's own 8x8 letters */
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
    SDL_RenderDebugText(renderer, x / scale, y / scale, s);
    SDL_SetRenderScale(renderer, 1, 1);
}

static void fill(SDL_Renderer *renderer, SDL_FRect r, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderFillRect(renderer, &r);
}

/* The displays' scale: whole multiples of the game's font as the window grows. */
static int display_scale(SDL_Renderer *renderer)
{
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    return h >= 600 ? h / 300 : 1;
}

/* Draws a display's line in the top-left corner on its dark box; `row` 0 is
 * the first line, 1 the one below it. */
static void corner_line(SDL_Renderer *renderer, const char *line, int row)
{
    SDL_RendererLogicalPresentation mode;
    int lw = 0, lh = 0;
    SDL_FRect r;
    int scale;
    float box;
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    khdays_ui_font_load();
    scale = display_scale(renderer);
    box = (float)((khdays_ui_font_line_height() + 8) * scale);
    r = (SDL_FRect){(float)(6 * scale), (float)(6 * scale) + (float)row * (box + (float)(4 * scale)),
                    (float)((khdays_ui_font_width(line) + 12) * scale), box};
    fill(renderer, r, 12, 14, 24, 190);
    text(renderer, r.x + (float)(6 * scale), r.y + (float)(4 * scale), (float)scale, white, line);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}

void khdays_settings_fps_draw(SDL_Renderer *renderer, int pictures)
{
    char line[64];
    SDL_snprintf(line, sizeof(line), khdays_text(TXT_FPS_LINE), pictures);
    corner_line(renderer, line, 0);
}

void khdays_settings_ping_draw(SDL_Renderer *renderer, const char *line, int under_fps)
{
    corner_line(renderer, line, under_fps ? 1 : 0);
}

void khdays_settings_corner_draw(SDL_Renderer *renderer, const char *line, int row)
{
    corner_line(renderer, line, row);
}
