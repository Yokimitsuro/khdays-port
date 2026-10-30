/* The Menu Overhaul's day card (day_screen.h): the theater's between chapters, from that mod's layouts/day, each
 * node's keys from its control's start, timed as the collection's movie shows it (runtime/menus/daycard.h has
 * its pace, measured frame by frame on a capture of it). The game's counter only says the days; its rolling
 * is given the same pace so its card holds on as long, and its fade-out is laid over this one at the end. */
#include "day_screen.h"

#include "layout_screen.h"
#include "../app/host.h"
#include "../../runtime/menus/daycard.h"
#include "../../runtime/mods/installed.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/core/session_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 60.0f
#define ROLL_FIRST ((float)KHDAYS_DAYCARD_SLIDE_FIRST) /* the rolls' keys move from this frame */
#define ROLL_LAST ((float)KHDAYS_DAYCARD_SLIDE_LAST)   /* to this one */
#define HEART_FALLS 50.0f                              /* the heart's control's frame its fall starts at */
#define FADE_IN 20.0f                                  /* the card in from black, straight (the theater's) */
#define FADING_OUT 3 /* the game's card's phase fading it out (Ov004_FadeOutTransition) */

typedef struct Key {
    float x, y, w, h, alpha;
    int fx, fy, frame;
} Key;

typedef struct Roll {
    float dy, alpha;
} Roll;

enum { ROLL_IN_UP, ROLL_OUT_UP, ROLL_OUT_DOWN, ROLL_IN_DOWN, ROLL_KINDS };

/* A piece the card moves: its name in the layout and its key at each frame, 0 to the card's length. */
typedef struct Element {
    char name[KHDAYS_LAYOUT_NAME];
    Key *keys;
} Element;

/* The card as its layout gives it, each list as long as the layout's lines make it. */
static struct {
    int tried, ok;
    KhdaysLayout layout;
    int length; /* the elements' last frame */
    Element *elements;
    int element_count;
    SDL_FRect *slots; /* the day's digits, the ones first */
    int slot_count;
    Roll *rolls[ROLL_KINDS]; /* each kind's frames, roll_frames of them */
    int roll_frames;
    float title_x, title_y, title_top; /* its ink's middle, its capitals' top; theirs in its pieces */
    float *title_alpha;
    /* the card shown: since when, for which days; when the heart's and the title's controls start */
    Uint64 since;
    int from, to;
    float heart_at;
} s;

static void forget(void)
{
#pragma warning(push)
#pragma warning(disable : 6001) /* each of the first element_count elements was zeroed when grown (grow_to) */
    for (int i = 0; i < s.element_count; ++i) {
        free(s.elements[i].keys);
    }
#pragma warning(pop)
    free(s.elements);
    s.elements = NULL;
    s.element_count = 0;
    free(s.slots);
    s.slots = NULL;
    s.slot_count = 0;
    for (int k = 0; k < ROLL_KINDS; ++k) {
        free(s.rolls[k]);
        s.rolls[k] = NULL;
    }
    s.roll_frames = 0;
    free(s.title_alpha);
    s.title_alpha = NULL;
    s.length = 0;
    khdays_layout_forget(&s.layout);
}

/* `list` (of `*count` items of `size` bytes) made `count_wanted` long, the new items zero; 0 if no memory. */
static int grow_to(void **list, int *count, int count_wanted, size_t size)
{
    void *grown;
    if (count_wanted <= *count) {
        return 1;
    }
    grown = realloc(*list, (size_t)count_wanted * size);
    if (grown == NULL) {
        return 0;
    }
    memset((char *)grown + (size_t)*count * size, 0, (size_t)(count_wanted - *count) * size);
    *list = grown;
    *count = count_wanted;
    return 1;
}

static int roll_kind(int control)
{
    switch (control) {
    case 1: return ROLL_IN_UP;
    case 2: return ROLL_OUT_UP;
    case 4: return ROLL_OUT_DOWN;
    case 5: return ROLL_IN_DOWN;
    default: return -1;
    }
}

static int read_line(char *line, const char *folder)
{
    int a, b, c, d, e;
    float x, y, w, h, al;
    char name[KHDAYS_LAYOUT_NAME];
    if (khdays_layout_line(&s.layout, line, folder)) {
        return 1;
    }
    if (SDL_sscanf(line, "length %d", &a) == 1) {
        /* every element's keys and the title's alpha, a frame each from 0 to the length */
        s.length = SDL_max(a, 1);
        for (int i = 0; i < s.element_count; ++i) {
            free(s.elements[i].keys);
            s.elements[i].keys = (Key *)calloc((size_t)s.length + 1, sizeof(Key));
        }
        free(s.title_alpha);
        s.title_alpha = (float *)calloc((size_t)s.length + 1, sizeof(float));
        return s.title_alpha != NULL;
    }
    if (SDL_sscanf(line, "element %d %39s", &a, name) == 2 && a >= 0) {
        if (!grow_to((void **)&s.elements, &s.element_count, a + 1, sizeof(Element))) {
            return 0;
        }
        snprintf(s.elements[a].name, sizeof(s.elements[a].name), "%s", name);
        if (s.length > 0 && s.elements[a].keys == NULL) {
            s.elements[a].keys = (Key *)calloc((size_t)s.length + 1, sizeof(Key));
        }
        return 1;
    }
    if (SDL_sscanf(line, "key %d %d %f %f %f %f %f %d %d %d", &a, &b, &x, &y, &w, &h, &al, &c, &d, &e) == 10) {
        if (a >= 0 && a < s.element_count && b >= 0 && b <= s.length && s.elements[a].keys != NULL) {
            const Key k = {x, y, w, h, al, c, d, e};
            s.elements[a].keys[b] = k;
        }
        return 1;
    }
    if (SDL_sscanf(line, "slot %d %f %f %f %f", &a, &x, &y, &w, &h) == 5) {
        if (a >= 0 && grow_to((void **)&s.slots, &s.slot_count, a + 1, sizeof(SDL_FRect))) {
            const SDL_FRect r = {x, y, w, h};
            s.slots[a] = r;
        }
        return 1;
    }
    if (SDL_sscanf(line, "roll %d %d %f %f", &a, &b, &y, &al) == 4) {
        const int kind = roll_kind(a);
        if (kind >= 0 && b >= 0 && b + 1 > s.roll_frames) {
            /* every kind as long as the longest */
            for (int k = 0; k < ROLL_KINDS; ++k) {
                int frames = s.roll_frames;
                if (!grow_to((void **)&s.rolls[k], &frames, b + 1, sizeof(Roll))) {
                    return 0;
                }
            }
            s.roll_frames = b + 1;
        }
        if (kind >= 0 && b >= 0) {
            s.rolls[kind][b].dy = y;
            s.rolls[kind][b].alpha = al;
        }
        return 1;
    }
    if (SDL_sscanf(line, "title_at %f %f %f", &x, &y, &w) == 3) {
        s.title_x = x;
        s.title_y = y;
        s.title_top = w;
        return 1;
    }
    if (SDL_sscanf(line, "title_alpha %d %f", &a, &al) == 2) {
        if (a >= 0 && a <= s.length && s.title_alpha != NULL) {
            s.title_alpha[a] = al;
        }
        return 1;
    }
    return 0;
}

static int load(SDL_Renderer *renderer, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    char relative[96], path[1024], folder[1024];
    char *file_text, *next, *slash;
    size_t file_size = 0;
    forget();
    s.layout.who = "day card";
    s.layout.renderer = renderer;
    if (language < 1 || language > 5 || !khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return 0; /* (the Menu Overhaul's card: without that mod, the game's own) */
    }
    snprintf(relative, sizeof(relative), "layouts/day/%s/day.txt", codes[language]);
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
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (*line != '#') {
            read_line(line, folder);
        }
    }
    SDL_free(file_text);
    khdays_layout_ready(&s.layout); /* (its glyphs, none: the card has no font) */
    if (s.layout.atlas == NULL || s.layout.piece_count == 0 || s.length == 0 || s.element_count == 0 ||
        s.roll_frames < 2 || khdays_layout_piece(&s.layout, "digit0") == NULL) {
        SDL_Log("day card: %s is incomplete", path);
        forget();
        return 0;
    }
    return 1;
}

/* ---- Drawing --------------------------------------------------------------------------------- */

static void draw_turned(const KhdaysLayoutFrame *fr, const char *name, float x, float y, float w, float h, float alpha,
                        int fx, int fy)
{
    const KhdaysLayoutPiece *p = khdays_layout_piece(&s.layout, name);
    SDL_FRect src, dst;
    SDL_FlipMode flip = SDL_FLIP_NONE;
    if (p == NULL || alpha <= 0.0f || w <= 0.0f || h <= 0.0f) {
        return;
    }
    src.x = p->u;
    src.y = p->v;
    src.w = p->w;
    src.h = p->h;
    dst = khdays_layout_to_window(fr, x, y, w, h);
    if (fx) {
        flip = (SDL_FlipMode)(flip | SDL_FLIP_HORIZONTAL);
    }
    if (fy) {
        flip = (SDL_FlipMode)(flip | SDL_FLIP_VERTICAL);
    }
    SDL_SetTextureAlphaModFloat(s.layout.atlas, SDL_min(alpha, 1.0f));
    SDL_RenderTextureRotated(s.layout.renderer, s.layout.atlas, &src, &dst, 0.0, NULL, flip);
    SDL_SetTextureAlphaModFloat(s.layout.atlas, 1.0f);
}

/* The elements, as the theater's keys have them: the line's and DAY's at `frame` of the card, the
 * heart's at `heart` of its own control (turning only while it falls, a day with no title too). */
static void draw_elements(const KhdaysLayoutFrame *fr, int frame, int heart)
{
    for (int i = 0; i < s.element_count; ++i) {
        const Element *el = &s.elements[i];
        const int is_heart = strcmp(el->name, "heart") == 0;
        const Key *k = el->keys != NULL ? &el->keys[SDL_clamp(is_heart ? heart : frame, 0, s.length)] : NULL;
        char name[KHDAYS_LAYOUT_NAME + 8];
        if (k == NULL || el->name[0] == '\0') {
            continue;
        }
        if (is_heart) {
            snprintf(name, sizeof(name), "heart%d", k->frame);
        } else {
            snprintf(name, sizeof(name), "%s", el->name);
        }
        draw_turned(fr, name, k->x, k->y, k->w, k->h, k->alpha, k->fx, k->fy);
    }
}

/* A roll's offset and alpha `t` of the way (0-1) through its moving keys. */
static Roll roll_at(int kind, float t)
{
    const float f = ROLL_FIRST + SDL_clamp(t, 0.0f, 1.0f) * (ROLL_LAST - ROLL_FIRST);
    const int a = SDL_clamp((int)f, 0, s.roll_frames - 1), b = SDL_min(a + 1, s.roll_frames - 1);
    const float u = f - (float)a;
    Roll r;
    r.dy = s.rolls[kind][a].dy + (s.rolls[kind][b].dy - s.rolls[kind][a].dy) * u;
    r.alpha = s.rolls[kind][a].alpha + (s.rolls[kind][b].alpha - s.rolls[kind][a].alpha) * u;
    return r;
}

static int digit_of(int day, int slot)
{
    for (int i = 0; i < slot; ++i) {
        day /= 10;
    }
    return day % 10;
}

/* Whether a day shows a digit in the slot: the ones always, the others while they are not leading zeros. */
static int shows(int day, int slot)
{
    int place = 1;
    for (int i = 0; i < slot && place <= day; ++i) {
        place *= 10;
    }
    return slot == 0 || day >= place;
}

static void draw_digit(const KhdaysLayoutFrame *fr, int slot, int digit, float dy, float alpha)
{
    char name[16];
    const SDL_FRect *r = &s.slots[slot];
    snprintf(name, sizeof(name), "digit%d", digit);
    draw_turned(fr, name, r->x, r->y + dy, r->w, r->h, alpha, 0, 0);
}

/* The day: `old` going and `next` coming `t` of the way (rising when the days go up); old < 0 none. */
static void draw_digits(const KhdaysLayoutFrame *fr, int old, int next, float t, int rising)
{
    for (int slot = 0; slot < s.slot_count; ++slot) {
        const int a = old >= 0 ? digit_of(old, slot) : -1, b = digit_of(next, slot);
        const int sa = old >= 0 && shows(old, slot), sb = shows(next, slot);
        if (a == b && sa == sb) {
            if (sa) {
                draw_digit(fr, slot, a, 0.0f, 1.0f);
            }
            continue;
        }
        if (sa) {
            const Roll r = roll_at(rising ? ROLL_OUT_UP : ROLL_OUT_DOWN, t);
            draw_digit(fr, slot, a, r.dy, r.alpha);
        }
        if (sb) {
            const Roll r = roll_at(rising ? ROLL_IN_UP : ROLL_IN_DOWN, t);
            draw_digit(fr, slot, b, r.dy, r.alpha);
        }
    }
}

/* The digits at `frame` of the card. No day changing: the day stands. One: the slide (its control from
 * COUNT_AT). More: from the slide's first moving frame a quick count to the day before, each day by the
 * slide's own curve at its share, the count leaving at its mean pace and slowing to rest (a cubic: speed
 * 1 at its start, 0 at its end, a third faster at its fastest), then the last day's slide. */
static void draw_counter(const KhdaysLayoutFrame *fr, int from, int to, float frame)
{
    const int days = from > to ? from - to : to - from, rising = to >= from;
    const float count = frame - (float)KHDAYS_DAYCARD_COUNT_AT;
    const float fast = (float)khdays_daycard_fast_frames(days);
    float t, value;
    int old;
    if (days == 0) {
        draw_digits(fr, to, to, 0.0f, 1);
        return;
    }
    if (days == 1 || count - ROLL_FIRST >= fast) {
        /* the last day's slide: its control from the count's start (one day) or near the quick count's end */
        const float at = days == 1 ? 0.0f : ROLL_FIRST + fast - (float)KHDAYS_DAYCARD_FINAL_EARLY;
        draw_digits(fr, rising ? to - 1 : to + 1, to, (count - at - ROLL_FIRST) / (ROLL_LAST - ROLL_FIRST), rising);
        return;
    }
    t = (count - ROLL_FIRST) / fast;
    if (t <= 0.0f) {
        draw_digits(fr, from, from, 0.0f, rising);
        return;
    }
    value = (float)(days - 1) * (t + t * t - t * t * t);
    old = (int)SDL_floorf(value);
    draw_digits(fr, rising ? from + old : from - old, rising ? from + old + 1 : from - old - 1, value - (float)old,
                rising);
}

static void draw_title(const KhdaysLayoutFrame *fr, int day, int frame)
{
    char name[24];
    const KhdaysLayoutPiece *p;
    int f;
    if (day < 0) {
        return;
    }
    snprintf(name, sizeof(name), "title_d%d", day);
    p = khdays_layout_piece(&s.layout, name);
    f = SDL_clamp(frame, 0, s.length);
    if (p != NULL && s.title_alpha[f] > 0.0f) {
        draw_turned(fr, name, s.title_x - p->w / 2, s.title_y - s.title_top, p->w, p->h, s.title_alpha[f], 0, 0);
    }
}

int khdays_day_screen_draw(SDL_Renderer *renderer)
{
    KhdaysDayCardView v;
    KhdaysLayoutFrame fr;
    Uint64 now;
    float frame;
    khdays_daycard_view(&v);
    if (!v.shown) {
        s.since = 0;
        return 0;
    }
    if (renderer != s.layout.renderer || v.language != s.tried) {
        s.tried = v.language;
        s.ok = load(renderer, v.language);
    }
    if (!s.ok || khdays_movie_hd_showing()) {
        return 0;
    }
    now = SDL_GetTicks();
    khdays_daycard_hd_drawn();
    if (s.since == 0 || v.from != s.from || v.to != s.to) {
        char name[24];
        const int arrival = khdays_daycard_arrival(v.from, v.to);
        s.since = now;
        s.from = v.from;
        s.to = v.to;
        s.heart_at = arrival > 0 ? (float)arrival - HEART_FALLS : 0.0f;
        snprintf(name, sizeof(name), "title_d%d", v.title_day);
        khdays_log("day card: day %d to %d, its title %s (day %d); the day arrives at frame %d", v.from, v.to,
                   v.title_day >= 0 && khdays_layout_piece(&s.layout, name) != NULL ? "shown" : "none", v.title_day,
                   arrival);
    }
    frame = (float)(now - s.since) * RATE / 1000.0f;
    khdays_layout_begin(renderer, &fr);
    draw_elements(&fr, (int)frame, (int)SDL_floorf(frame - s.heart_at));
    draw_counter(&fr, v.from, v.to, frame);
    draw_title(&fr, v.title_day, (int)SDL_floorf(frame - s.heart_at));
    if (frame < FADE_IN) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)((1.0f - frame / FADE_IN) * 255.0f + 0.5f));
        SDL_RenderFillRect(renderer, NULL);
    }
    if (v.phase >= FADING_OUT) {
        khdays_layout_shade(renderer, v.master);
    }
    khdays_layout_end(renderer, &fr);
    return 1;
}

void khdays_day_screen_renderer_gone(void)
{
    forget();
    s.tried = 0;
    s.ok = 0;
}
