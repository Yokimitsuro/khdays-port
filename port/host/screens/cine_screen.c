/* MODO CINE's list on the screen (cine_screen.h), in the theater's pieces: a file
 * tools/hd/theater_cine.py writes (docs/HD_SCREENS.md, "MODO CINE's list"). The rows are the theater's
 * chapters (the port's list) or the game's page, each one's name and length; behind them the selected
 * one's artwork, read on a thread of its own, fades in over FADE_MS. */
#include "cine_screen.h"

#include "layout_screen.h"
#include "../app/host.h"
#include "../render/png_io.h"
#include "../../runtime/menus/cinelist.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/input/input.h"
#include "../../runtime/core/rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FADE_MS 250.0f
#define THR_HELP 0
#define THR_NAMES 1
#define THR_LENGTHS (THR_NAMES + KHDAYS_CINE_SCENES)
#define REPEAT_AFTER_MS 400u /* a direction held repeats after this, then every REPEAT_MS */
#define REPEAT_MS 100u
#define SOUND_MOVE 0 /* the game's list's (Ov000_List_CursorNextWrap, Ov000_ActivateSceneObject) */
#define SOUND_CHOICE 1

typedef struct Scene {
    int movie, day, art; /* art: arts' index, -1 none */
} Scene;

/* One of the theater's chapters: its day, artwork, name, videos in a row and their length. */
typedef struct Chapter {
    int day, art;
    unsigned short *name; /* UTF-16, as the layout's words */
    char **stems;
    int stem_count;
    int seconds;
} Chapter;

/* An artwork: its file, read on the loader's thread, then a texture on the window's. */
typedef struct Art {
    char name[64];
    char path[1024];
    uint8_t *rgba;
    int w, h;
    SDL_AtomicInt read; /* 1 when rgba is there (or could not be) */
    SDL_Texture *texture;
} Art;

static struct {
    int tried, ok;
    KhdaysLayout layout;
    KhdaysLayoutStrings strings;
    float row_top, pitch;
    int rows_shown;
    float digits[4];
    Uint8 digit_colour[3];
    Scene *scenes;
    int scene_count;
    Chapter *chapters; /* those whose videos are all there */
    int chapter_count;
    int cursor, first; /* the port's list: the chapter selected, the first of the page shown */
    uint16_t held;     /* the buttons last seen */
    Uint64 repeat_at;  /* when a held direction moves again */
    Art *arts;
    int art_count;
    int *order; /* the arts in the order the thread reads them */
    SDL_Thread *reader;
    SDL_AtomicInt stop;
    int art_now, art_before; /* arts' indices, -1 none */
    Uint64 art_since;        /* when art_now came in */
} s;

/* ---- The artworks ----------------------------------------------------------------------------- */

static int SDLCALL read_arts(void *unused)
{
    (void)unused;
    for (int i = 0; i < s.art_count && !SDL_GetAtomicInt(&s.stop); ++i) {
        Art *a = &s.arts[s.order[i]];
        const char *error = NULL;
        a->rgba = khdays_png_read(a->path, &a->w, &a->h, &error);
        if (a->rgba == NULL) {
            SDL_Log("theater list: %s: %s", a->path, error != NULL ? error : "not read");
        }
        SDL_SetAtomicInt(&a->read, 1);
    }
    return 0;
}

static void stop_reading(void)
{
    if (s.reader != NULL) {
        SDL_SetAtomicInt(&s.stop, 1);
        SDL_WaitThread(s.reader, NULL);
        s.reader = NULL;
    }
}

/* The arts read, starting with the one of the scene selected and going on from it. */
static void start_reading(int first_art)
{
    int n = 0;
    s.order = (int *)calloc((size_t)(s.art_count > 0 ? s.art_count : 1), sizeof(int));
    if (s.order == NULL) {
        return;
    }
    for (int i = 0; i < s.art_count; ++i) {
        s.order[n++] = (first_art + i) % s.art_count;
    }
    SDL_SetAtomicInt(&s.stop, 0);
    s.reader = SDL_CreateThread(read_arts, "khdays theater arts", NULL);
    if (s.reader == NULL) {
        read_arts(NULL); /* no thread: read them now */
    }
}

/* The art's texture, made once it is read; NULL until then (or when it cannot be). */
static SDL_Texture *art_texture(int i)
{
    Art *a = i >= 0 && i < s.art_count ? &s.arts[i] : NULL;
    if (a == NULL || !SDL_GetAtomicInt(&a->read)) {
        return NULL;
    }
    if (a->texture == NULL && a->rgba != NULL) {
        a->texture = SDL_CreateTexture(s.layout.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, a->w, a->h);
        if (a->texture != NULL) {
            SDL_UpdateTexture(a->texture, NULL, a->rgba, a->w * 4);
            SDL_SetTextureBlendMode(a->texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(a->texture, SDL_SCALEMODE_LINEAR);
        }
        khdays_png_free(a->rgba);
        a->rgba = NULL;
    }
    return a->texture;
}

static int art_named(const char *name)
{
    char relative[128];
    Art *more;
    for (int i = 0; i < s.art_count; ++i) {
        if (strcmp(s.arts[i].name, name) == 0) {
            return i;
        }
    }
    snprintf(relative, sizeof(relative), "layouts/cine/art/%s", name);
    more = (Art *)realloc(s.arts, sizeof(Art) * (size_t)(s.art_count + 1));
    if (more == NULL) {
        return -1;
    }
    s.arts = more;
    memset(&s.arts[s.art_count], 0, sizeof(Art));
    snprintf(s.arts[s.art_count].name, sizeof(s.arts[s.art_count].name), "%s", name);
    if (!khdays_mods_find(relative, s.arts[s.art_count].path, (int)sizeof(s.arts[s.art_count].path))) {
        SDL_Log("theater list: no mod gives %s", relative);
        SDL_SetAtomicInt(&s.arts[s.art_count].read, 1);
    }
    return s.art_count++;
}

/* ---- The chapters ----------------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

/* An MP4's length in whole seconds, from its movie header (moov's mvhd); 0 when it cannot be read.
 * Only its boxes' headers and the moov box are read. */
static int mp4_seconds(const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    Sint64 at = 0, end;
    int seconds = 0;
    if (io == NULL) {
        return 0;
    }
    end = SDL_GetIOSize(io);
    while (at + 8 <= end && seconds == 0) {
        uint8_t head[16];
        uint64_t size;
        int header = 8;
        if (SDL_SeekIO(io, at, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, head, 8) != 8) {
            break;
        }
        size = be32(head);
        if (size == 1) {
            if (SDL_ReadIO(io, head + 8, 8) != 8) {
                break;
            }
            size = (uint64_t)be32(head + 8) << 32 | be32(head + 12);
            header = 16;
        } else if (size == 0) {
            size = (uint64_t)(end - at);
        }
        if (size < (uint64_t)header) {
            break;
        }
        if (memcmp(head + 4, "moov", 4) == 0 && size - (uint64_t)header <= (uint64_t)(end - at)) {
            const size_t n = (size_t)(size - (uint64_t)header);
            uint8_t *moov = (uint8_t *)malloc(n);
            if (moov != NULL && SDL_ReadIO(io, moov, n) == n) {
                for (size_t i = 0; i + 8 <= n; i += be32(moov + i) > 8 ? be32(moov + i) : n) {
                    if (memcmp(moov + i + 4, "mvhd", 4) == 0 && i + 40 <= n) {
                        const uint8_t *m = moov + i + 8; /* version, flags, then its times */
                        const uint32_t scale = m[0] == 1 ? be32(m + 20) : be32(m + 12);
                        const uint64_t length =
                            m[0] == 1 ? (uint64_t)be32(m + 24) << 32 | be32(m + 28) : (uint64_t)be32(m + 16);
                        seconds = scale != 0 ? (int)((length + scale / 2) / scale) : 0;
                        break;
                    }
                }
            }
            free(moov);
            break;
        }
        at += (Sint64)size;
    }
    SDL_CloseIO(io);
    return seconds;
}

static void chapter_free(Chapter *c)
{
    for (int i = 0; i < c->stem_count; ++i) {
        SDL_free(c->stems[i]);
    }
    free(c->stems);
    SDL_free(c->name);
    memset(c, 0, sizeof(*c));
}

/* A `chapter DAY ART VIDEO... | NAME` line, kept when every video is there. */
static void add_chapter(const char *line)
{
    Chapter c;
    char art[64], *copy, *bar, *word, *rest = NULL;
    int ok = 1;
    memset(&c, 0, sizeof(c));
    if (SDL_sscanf(line, "chapter %d %63s", &c.day, art) != 2 || (bar = strchr(line, '|')) == NULL ||
        (copy = SDL_strdup(line)) == NULL) {
        return;
    }
    copy[bar - line] = '\0';
    {
        const char *name = bar[1] == ' ' ? bar + 2 : bar + 1;
        c.name = (unsigned short *)SDL_iconv_string("UTF-16LE", "UTF-8", name, SDL_strlen(name) + 1);
    }
    word = SDL_strtok_r(copy, " \t", &rest); /* "chapter", its day, its artwork, then its videos */
    for (int k = 0; word != NULL && ok; ++k, word = SDL_strtok_r(NULL, " \t", &rest)) {
        char file[1024], **more;
        if (k < 3) {
            continue;
        }
        khdays_movie_hd_scene_file(word, file, sizeof(file));
        more = (char **)realloc(c.stems, sizeof(char *) * (size_t)(c.stem_count + 1));
        ok = more != NULL && SDL_GetPathInfo(file, NULL);
        if (more != NULL) {
            c.stems = more;
            c.stems[c.stem_count] = SDL_strdup(word);
            ok = ok && c.stems[c.stem_count] != NULL;
            c.seconds += ok ? mp4_seconds(file) : 0;
            ++c.stem_count;
        }
    }
    SDL_free(copy);
    if (ok && c.stem_count > 0 && c.name != NULL) {
        Chapter *more = (Chapter *)realloc(s.chapters, sizeof(Chapter) * (size_t)(s.chapter_count + 1));
        if (more != NULL) {
            c.art = art_named(art);
            s.chapters = more;
            s.chapters[s.chapter_count++] = c;
            return;
        }
    }
    chapter_free(&c);
}

/* The port's list shows: the theater's chapters there, and Video `hd` to play them. */
static int chapters_on(void)
{
    return s.chapter_count > 0 && (khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD);
}

/* ---- The pieces' file ------------------------------------------------------------------------- */

static void forget(void)
{
    stop_reading();
    for (int i = 0; i < s.chapter_count; ++i) {
        chapter_free(&s.chapters[i]);
    }
    free(s.chapters);
    s.chapters = NULL;
    s.chapter_count = 0;
    for (int i = 0; i < s.art_count; ++i) {
        if (s.arts[i].texture != NULL) {
            SDL_DestroyTexture(s.arts[i].texture);
        }
        khdays_png_free(s.arts[i].rgba);
    }
    free(s.arts);
    free(s.order);
    free(s.scenes);
    s.arts = NULL;
    s.order = NULL;
    s.scenes = NULL;
    s.art_count = s.scene_count = 0;
    s.art_now = s.art_before = -1;
    khdays_layout_forget(&s.layout);
    khdays_layout_strings_free(&s.strings);
}

static int load(SDL_Renderer *renderer, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    static KhdaysRomReader *reader; /* its own: the window's thread */
    char relative[96], path[1024], folder[1024], art[64];
    char *file_text, *next, *slash;
    size_t file_size = 0;
    forget();
    s.layout.who = "theater list";
    s.layout.renderer = renderer;
    if (language < 1 || language > 5) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/cine/%s/cine.txt", codes[language]);
    if (!khdays_mods_find(relative, path, (int)sizeof(path)) ||
        (file_text = (char *)SDL_LoadFile(path, &file_size)) == NULL) {
        return 0;
    }
    snprintf(folder, sizeof(folder), "%s", path);
    slash = strrchr(folder, '/');
    if (strrchr(folder, '\\') > slash) {
        slash = strrchr(folder, '\\');
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
    for (char *line = file_text; line != NULL && *line != '\0'; line = next) {
        float a, b, c, d;
        int shown, movie, day, r, g, bl;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (khdays_layout_line(&s.layout, line, folder)) {
            continue;
        }
        if (SDL_sscanf(line, "rows %f %f %d", &a, &b, &shown) == 3) {
            s.row_top = a;
            s.pitch = b;
            s.rows_shown = shown > 0 ? shown : 1;
        } else if (SDL_sscanf(line, "digits %f %f %f %f %d %d %d", &a, &b, &c, &d, &r, &g, &bl) == 7) {
            s.digits[0] = a;
            s.digits[1] = b;
            s.digits[2] = c;
            s.digits[3] = d;
            s.digit_colour[0] = (Uint8)r;
            s.digit_colour[1] = (Uint8)g;
            s.digit_colour[2] = (Uint8)bl;
        } else if (SDL_strncmp(line, "chapter ", 8) == 0) {
            add_chapter(line);
        } else if (SDL_sscanf(line, "scene %d %d %63s", &movie, &day, art) == 3) {
            Scene *more = (Scene *)realloc(s.scenes, sizeof(Scene) * (size_t)(s.scene_count + 1));
            if (more != NULL) {
                s.scenes = more;
                s.scenes[s.scene_count].movie = movie;
                s.scenes[s.scene_count].day = day;
                s.scenes[s.scene_count].art = art_named(art);
                ++s.scene_count;
            }
        }
    }
    SDL_free(file_text);
    if (!khdays_layout_ready(&s.layout) || s.rows_shown == 0 || s.scene_count == 0) {
        SDL_Log("theater list: %s is incomplete", path);
        forget();
        return 0;
    }
    if (reader == NULL) {
        reader = khdays_rom_reader_new();
    }
    snprintf(path, sizeof(path), "UI/thr/thr_%s.s.z", codes[language]);
    khdays_layout_strings(reader, path, &s.strings, s.layout.who);
    return 1;
}

static const Scene *scene_of(int movie)
{
    for (int i = 0; i < s.scene_count; ++i) {
        if (s.scenes[i].movie == movie) {
            return &s.scenes[i];
        }
    }
    return NULL;
}

/* ---- Drawing --------------------------------------------------------------------------------- */

static void draw_art(const KhdaysLayoutFrame *fr, int art, float alpha)
{
    SDL_Texture *t = art_texture(art);
    if (t != NULL) {
        const SDL_FRect dst = khdays_layout_to_window(fr, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H);
        SDL_SetTextureAlphaModFloat(t, alpha);
        SDL_RenderTexture(s.layout.renderer, t, NULL, &dst);
        SDL_SetTextureAlphaModFloat(t, 1.0f);
    }
}

/* The selected scene's artwork, coming in over the last one. */
static void draw_background(const KhdaysLayoutFrame *fr, const Scene *selected)
{
    const int wanted = selected != NULL ? selected->art : -1;
    const Uint64 now = SDL_GetTicks();
    float t;
    if (wanted != s.art_now && (wanted < 0 || SDL_GetAtomicInt(&s.arts[wanted].read))) {
        s.art_before = s.art_now;
        s.art_now = wanted;
        s.art_since = now;
    }
    t = (float)(now - s.art_since) / FADE_MS;
    if (t < 1.0f && s.art_before >= 0) {
        draw_art(fr, s.art_before, 1.0f);
        draw_art(fr, s.art_now, t);
    } else {
        draw_art(fr, s.art_now, 1.0f);
    }
}

static void draw_day(const KhdaysLayoutFrame *fr, int day, float top)
{
    char digits[12];
    const int n = snprintf(digits, sizeof(digits), "%d", day);
    for (int k = 0; k < n; ++k) {
        char name[16];
        const KhdaysLayoutPiece *p;
        snprintf(name, sizeof(name), "digit%c", digits[n - 1 - k]);
        p = khdays_layout_piece(&s.layout, name);
        if (p != NULL && p->h > 0) {
            const float h = s.digits[2], w = p->w * (h / p->h);
            khdays_layout_draw_tinted(&s.layout, fr, name, s.digits[0] - (float)k * s.digits[3] - w / 2,
                                      top + s.digits[1], w, h, s.digit_colour, 1.0f);
        }
    }
}

/* KH2's firefly at the selected row's right end (layout_screen.h), as large beside the glove as KH2's
 * beside its own. */
static void firefly_at(const KhdaysLayoutFrame *fr, float right, float middle)
{
    const KhdaysLayoutPlace *g = khdays_layout_place(&s.layout, "glove");
    const float unit = KHDAYS_FIREFLY_UNIT * (g != NULL && g->h > 0 ? g->h / 91.0f : 1.0f);
    khdays_layout_firefly(s.layout.renderer, fr, KHDAYS_FIREFLY_CINE, right - 15.0f * unit, middle, unit, 1.0f);
}

static void firefly_row(const KhdaysLayoutFrame *fr, float top)
{
    const KhdaysLayoutPlace *r = khdays_layout_place(&s.layout, "row_selected");
    if (r != NULL) {
        firefly_at(fr, r->x + r->w, top + r->y + r->h / 2);
    }
}

static void draw_row(const KhdaysLayoutFrame *fr, const KhdaysCineView *v, int index, float top)
{
    const Scene *sc = scene_of(v->movies[index]);
    const Scene *before = index > 0 ? scene_of(v->movies[index - 1]) : NULL;
    khdays_layout_draw_placed(&s.layout, fr, index == v->cursor ? "row_selected" : "row", 0, top, 1.0f);
    if (sc != NULL && (before == NULL || before->day != sc->day)) {
        /* a day's first scene, as the theater's chapters show theirs */
        khdays_layout_draw_placed(&s.layout, fr, "square", 0, top, 1.0f);
        if (sc->day > 0) {
            khdays_layout_draw_placed(&s.layout, fr, "day", 0, top, 1.0f);
            draw_day(fr, sc->day, top);
        }
    }
    if (index == v->cursor) {
        khdays_layout_draw_placed(&s.layout, fr, "glove", khdays_layout_glove_sway(), top, 1.0f);
        firefly_row(fr, top);
    }
    khdays_layout_draw_text(&s.layout, fr, "name", khdays_layout_string(&s.strings, THR_NAMES + index), 0, top, 1.0f);
    khdays_layout_draw_text(&s.layout, fr, "length", khdays_layout_string(&s.strings, THR_LENGTHS + index), 0, top,
                            1.0f);
}

/* A row of the port's list: the chapter's name and length, its day's square and DAY on a day's first. */
static void draw_chapter_row(const KhdaysLayoutFrame *fr, int index, float top)
{
    const Chapter *c = &s.chapters[index];
    char length[32];
    unsigned short length16[32];
    khdays_layout_draw_placed(&s.layout, fr, index == s.cursor ? "row_selected" : "row", 0, top, 1.0f);
    if (index == 0 || s.chapters[index - 1].day != c->day) {
        khdays_layout_draw_placed(&s.layout, fr, "square", 0, top, 1.0f);
        if (c->day > 0) {
            khdays_layout_draw_placed(&s.layout, fr, "day", 0, top, 1.0f);
            draw_day(fr, c->day, top);
        }
    }
    if (index == s.cursor) {
        khdays_layout_draw_placed(&s.layout, fr, "glove", khdays_layout_glove_sway(), top, 1.0f);
        firefly_row(fr, top);
    }
    snprintf(length, sizeof(length), "%02d:%02d", c->seconds / 60, c->seconds % 60);
    for (int k = 0; k < (int)sizeof(length); ++k) {
        length16[k] = (unsigned char)length[k]; /* digits and a colon */
    }
    khdays_layout_draw_text(&s.layout, fr, "name", c->name, 0, top, 1.0f);
    khdays_layout_draw_text(&s.layout, fr, "length", length16, 0, top, 1.0f);
}

static void draw_scroll(const KhdaysLayoutFrame *fr, int first, int total)
{
    const KhdaysLayoutPlace *trough = khdays_layout_place(&s.layout, "trough");
    const KhdaysLayoutPlace *thumb = khdays_layout_place(&s.layout, "thumb");
    const int pages = total - s.rows_shown;
    if (trough == NULL || thumb == NULL) {
        return;
    }
    khdays_layout_draw_placed(&s.layout, fr, "trough", 0, 0, 1.0f);
    khdays_layout_draw_placed(&s.layout, fr, "end", 0, 0, 1.0f);
    {
        /* from the trough's top to its bottom as the game's page goes (the port's) */
        const float travel = trough->h - thumb->h;
        const float t = pages > 0 ? (float)first / (float)pages : 0.0f;
        khdays_layout_draw_piece(&s.layout, fr, "thumb", thumb->x, trough->y + travel * (t > 1.0f ? 1.0f : t), thumb->w,
                                 thumb->h, 1.0f);
    }
}

int khdays_cine_screen_draw(SDL_Renderer *renderer)
{
    KhdaysCineView v;
    KhdaysLayoutFrame fr;
    khdays_cinelist_view(&v);
    if (!v.shown) {
        if (s.ok) {
            /* the list gone (a movie, the title): its artworks let go until it shows again */
            s.tried = 0;
            s.ok = 0;
            forget();
        }
        return 0;
    }
    if (renderer != s.layout.renderer || v.language != s.tried) {
        const Scene *sc;
        s.tried = v.language;
        s.ok = load(renderer, v.language);
        s.cursor = s.first = 0;
        sc = s.ok ? scene_of(v.movies[v.cursor]) : NULL;
        if (s.ok) {
            start_reading(chapters_on() ? SDL_max(s.chapters[0].art, 0) : sc != NULL && sc->art >= 0 ? sc->art : 0);
        }
    }
    if (!s.ok || khdays_movie_hd_showing()) {
        return 0; /* (a chapter's videos, or the game's movie, over it) */
    }
    khdays_layout_begin(renderer, &fr);
    if (chapters_on()) {
        const Chapter *c = &s.chapters[s.cursor];
        const Scene art = {0, c->day, c->art};
        draw_background(&fr, &art);
    } else {
        draw_background(&fr, scene_of(v.movies[v.cursor]));
    }
    khdays_layout_draw_placed(&s.layout, &fr, "veil", 0, 0, 1.0f);
    khdays_layout_draw_placed(&s.layout, &fr, "heading", 0, 0, 1.0f);
    khdays_layout_draw_placed(&s.layout, &fr, "help_bar", 0, 0, 1.0f);
    if (chapters_on()) {
        for (int i = 0; i < s.rows_shown && s.first + i < s.chapter_count; ++i) {
            draw_chapter_row(&fr, s.first + i, s.row_top + (float)i * s.pitch);
        }
        draw_scroll(&fr, s.first, s.chapter_count);
    } else {
        for (int i = 0; i < s.rows_shown && v.first + i < KHDAYS_CINE_SCENES; ++i) {
            draw_row(&fr, &v, v.first + i, s.row_top + (float)i * s.pitch);
        }
        draw_scroll(&fr, v.first, KHDAYS_CINE_SCENES);
    }
    khdays_layout_draw_text(&s.layout, &fr, "help", khdays_layout_string(&s.strings, THR_HELP), 0, 0, 1.0f);
    khdays_layout_shade(renderer, v.master);
    khdays_layout_end(renderer, &fr);
    return 1;
}

/* The port's list's cursor moved by `step`, round at its ends when `wrap` (a row, as the game's list),
 * else held there (a page); the page follows it. */
static void move_cursor(int step, int wrap)
{
    const int to = wrap ? ((s.cursor + step) % s.chapter_count + s.chapter_count) % s.chapter_count
                        : SDL_clamp(s.cursor + step, 0, s.chapter_count - 1);
    if (to == s.cursor) {
        return;
    }
    s.cursor = to;
    if (s.cursor < s.first) {
        s.first = s.cursor;
    } else if (s.cursor >= s.first + s.rows_shown) {
        s.first = s.cursor - s.rows_shown + 1;
    }
    khdays_cinelist_sound(SOUND_MOVE);
}

void khdays_cine_screen_buttons(uint16_t buttons)
{
    const uint16_t directions = KHDAYS_KEY_UP | KHDAYS_KEY_DOWN | KHDAYS_KEY_LEFT | KHDAYS_KEY_RIGHT;
    const Uint64 now = SDL_GetTicks();
    uint16_t pressed = (uint16_t)(buttons & ~s.held);
    KhdaysCineView v;
    khdays_cinelist_view(&v);
    const int mine = v.shown && s.ok && chapters_on();
    khdays_cinelist_take(mine);
    s.held = buttons;
    if (!mine || khdays_movie_hd_showing() || v.master != 0) {
        return; /* the game's list, a chapter's videos, or the list's fade */
    }
    if (pressed & directions) {
        s.repeat_at = now + REPEAT_AFTER_MS;
    } else if ((buttons & directions) && now >= s.repeat_at) {
        pressed |= (uint16_t)(buttons & directions);
        s.repeat_at = now + REPEAT_MS;
    }
    if (pressed & KHDAYS_KEY_A) {
        /* (no choice's sound: the videos start at once, the game held and silent under them) */
        char key[32];
        snprintf(key, sizeof(key), "chapter %d", s.cursor + 1);
        khdays_movie_hd_chapter(key, s.chapters[s.cursor].stems, s.chapters[s.cursor].stem_count);
    } else if (pressed & KHDAYS_KEY_UP) {
        move_cursor(-1, 1);
    } else if (pressed & KHDAYS_KEY_DOWN) {
        move_cursor(1, 1);
    } else if (pressed & KHDAYS_KEY_LEFT) {
        move_cursor(-s.rows_shown, 0);
    } else if (pressed & KHDAYS_KEY_RIGHT) {
        move_cursor(s.rows_shown, 0);
    }
}

void khdays_cine_screen_renderer_gone(void)
{
    forget();
    s.tried = 0;
    s.ok = 0;
    s.layout.renderer = NULL;
    khdays_layout_renderer_gone();
}
