/* BG canvas text: captured commands and a text-free graphics plane follow the game's
 * character transfers into VRAM. The compositor draws both at the original logical size.
 * Original glyph samples never determine replacement coverage. See docs/HD_FONTS.md. */
#include "text.h"
#include "../host/app/hud_settings.h"
#include "bg.h"
#include "button_icons.h"
#include "elements.h"
#include "parts.h"
#include "kh2_information.h"
#include "hud_options.h"
#include "kh2_obtained.h"
#include "kh2_mission_panel.h"
#include "../gpu/gpu2d.h"
#include "../runtime/mods/font_runtime.h"
#include "../runtime/mods/tiles2d.h"
#include "../runtime/mods/textures.h"
#include "../runtime/menus/camp.h"
#include "../runtime/core/io.h"
#include "../runtime/core/origin.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/vram.h"
#include "../runtime/core/window.h"
#include "../host/app/host.h"

#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int khdays_game_ram_ref(unsigned int reference);
extern void DrawGlyphLine(KhdaysRef canvas, KhdaysRef font, int x, int y, int colour, KhdaysRef glyph);

/* --- The game's structures, as the DS lays them out ----------------------------- */

typedef struct DsCanvas { /* NNSG2dCharCanvas */
    uint32_t char_base;   /* u8 *: the canvas's characters */
    int32_t area_width;   /* in characters */
    int32_t area_height;
    uint8_t dst_bpp; /* 4 or 8 */
    uint8_t reserved[3];
    uint32_t param; /* characters per row of the canvas */
    uint32_t vtable;
} DsCanvas;

typedef struct DsFrame { /* TileTextRenderer_Init's Frame */
    uint16_t x, y, width, height, char_base, palette, field_0c, field_0e;
} DsFrame;

/* a TileTextRenderer's canvas at +0x04; a tile surface's list object's at +0x08 */
#define RENDERER_CANVAS 0x04u
#define SURFACE_OBJECT_CANVAS 0x08u

/* --- The canvases ------------------------------------------------------------------ */

typedef struct GlyphLayout {
    double x, y, x_scale;
    int w, h, paint_w, clip_l, clip_t, clip_r, clip_b;
} GlyphLayout;

typedef struct GlyphOp {
    int source_x, source_y, source_w, source_h; /* Native write footprint, independent of PNG geometry. */
    double x, y;                                /* Exact artwork position; native write bounds remain integer. */
    int w, h, cl, glyph;
    int paint_w;       /* Edited logical width clips artwork; ownership keeps the full cell. */
    double x_scale;    /* Zero retains the original geometry; fitting is an explicit run operation. */
    int native_glyph;  /* This glyph deliberately keeps the original artwork. */
    int button, piece; /* the DS button the glyph draws (KHDAYS_BUTTON_*: its icon instead), which of its glyphs */
    int field;         /* the text tells the field's controls (khdays_button_icon) */
    /* The replacement artwork stays at x/y, but only this surviving rectangle
       is visible after partial clears. Coordinates are in DS pixels. */
    int clip_l, clip_t, clip_r, clip_b;
    unsigned char native_pixels[256]; /* Original small glyph, retained across font preference changes. */
    int native_saved;
    GlyphLayout layouts[2];
    int layout_mode;
    char resource[160];
} GlyphOp;

typedef struct FontBinding {
    KhdaysRef font;
    KhdaysRef info, file;
    int replacement;
    char path[160];
    char scope[80], selected[160];
    int scoped, route_enabled;
} FontBinding;

static FontBinding *font_bindings;
static int binding_count, binding_room;

/* One viewport-sized pair covers every tile outside the primary translation. */
#define EXTRA_COLS 33
#define EXTRA_ROWS 25
#define EXTRA_CELLS (EXTRA_COLS * EXTRA_ROWS)
typedef struct MappedImage {
    int image, hd_image, scale, hide_ds, invalid;
    int source[EXTRA_CELLS];
    uint16_t cells[EXTRA_CELLS];
    uint32_t palette, map_hash;
} MappedImage;

typedef struct Canvas {
    KhdaysRef cc;     /* the NNSG2dCharCanvas */
    KhdaysRef buffer; /* its characters' buffer */
    int area_w, area_h, bpp, param;
    uint32_t bytes;
    int engine, bg;       /* the layer its transfers went to */
    uint32_t vram;        /* where its first character is in the engine's BG memory */
    int pending;          /* it was transferred since the last VBlank */
    int in_vram;          /* VRAM holds what its transfers put there */
    KhdaysRef copy_owner; /* Allocation containing a copied resource's character data. */
    int copied;           /* A full RAM copy retains commands without a DS canvas object. */
    int detached;         /* original DS canvas buffer is gone; VRAM snapshot still valid */
    int hd_reported;      /* trace the first successful HD overlay */
    int hd_last_state;    /* diagnostic: -1 unknown, 0 fallback, 1 HD */
    uint8_t *seen;        /* what VRAM held of it at the VBlank after its last transfer */
    uint32_t version;     /* counts the changes of `seen` */
    uint16_t *cells;      /* for each of the image's cells, the map entry over it (0xffff: not the canvas's) */
    int image;
    uint32_t image_version;
    int said_shown; /* the trace: 1 reported where it is shown, 2 reported that it is not */
    int said_obj;   /* the trace: reported that it was loaded into the sprites' characters */
    char id[48];
    GlyphOp *ops;
    int op_count, op_room, ops_disabled;
    int base_hd;
    int hd_image, hd_scale;
    int hd_ready; /* Replacement masking can be ready without any primary glyphs on screen. */
    /* Source observations and text-free graphics; neither contains PNG coverage. */
    uint8_t *source, *base, *display_base, *dirty;
    uint32_t *tile_hash;
    uint32_t *graphics; /* Ordinary PNG tile mappings, independent of font coverage. */
    int graphics_count;
    uint32_t hd_palette;
    uint64_t *owned; /* transferred pixels in each tile, independent of glyph alpha */
    GlyphOp *display_ops;
    int display_count, display_room;
    int before_glyph;
    int scrolling;       /* the explicit scroll hook owns source updates until it completes */
    int command_cache;   /* Names cached by Ov002_DrawPanelRowText. */
    int editing_display; /* temporary command-list views can dirty published images */
    MappedImage *extra;
    uint32_t button_signature; /* khdays_button_icons_signature its button icons were drawn with */
    int information, display_information;
    int notification, display_notification;
    unsigned information_epoch, display_information_epoch;
    uint32_t *information_pixels;
    size_t information_room;
    uint32_t information_version;
    int information_scale;
} Canvas;

static struct {
    Canvas canvas;
    int active, slot, ready, bound, source_valid, notification, sequence;
    unsigned key;
    uint32_t address;
    KhdaysRef owner, owner_buffer;
} obtained = {.canvas = {.image = -1, .hd_image = -1}, .slot = -1};

static Canvas *canvases;
static int canvas_count, canvas_room;
static int trace = -1;
static KhdaysRef positioned_font;
static double positioned_x, positioned_y;
static int tracing(void);
static void say(const char *format, ...);
static struct {
    KhdaysRef font;
    double origin, left, factor, offset, width;
    double ink_left, ink_right;
    int report, ink_count;
} fit_run;
static int fitting_native;

/* The alternate pass runs only scalar SDK layout. It never calls a game's blitter. */
typedef struct LayoutPlacement {
    KhdaysRef owner, font;
    unsigned ch;
    double x, y, origin, left, factor, offset, width;
} LayoutPlacement;
static LayoutPlacement *layout_placements, *layout_pending;
static int layout_depth, layout_probe, layout_count, layout_room, layout_cursor, layout_previous;

int khdays_ui_text_layout_begin(void)
{
    if (layout_depth++)
        return 0;
    layout_count = layout_cursor = 0;
    layout_pending = NULL;
    layout_probe = 1;
    layout_previous = khdays_hd_font_layout_override(!khdays_hud_setting(KHDAYS_OPT_FONT));
    return 1;
}
void khdays_ui_text_layout_actual(int token)
{
    if (!token)
        return;
    khdays_hd_font_layout_override(layout_previous);
    layout_probe = 0;
    layout_cursor = 0;
}
void khdays_ui_text_layout_end(int token)
{
    if (layout_depth)
        --layout_depth;
    if (token) {
        khdays_hd_font_layout_override(layout_previous);
        layout_pending = NULL;
        layout_probe = 0;
    }
}
int khdays_ui_text_layout_glyph(const void *owner, const void *font, double x, double y, unsigned ch)
{
    if (layout_probe) {
        if (layout_count == layout_room) {
            const int room = layout_room ? layout_room * 2 : 128;
            LayoutPlacement *grown = room <= 8192 ? realloc(layout_placements, (size_t)room * sizeof(*grown)) : NULL;
            if (!grown)
                return 1;
            layout_placements = grown;
            layout_room = room;
        }
        layout_placements[layout_count++] = (LayoutPlacement){KHDAYS_REF(owner),
                                                              KHDAYS_REF(font),
                                                              ch,
                                                              x,
                                                              y,
                                                              fit_run.origin,
                                                              fit_run.left,
                                                              fit_run.font == KHDAYS_REF(font) ? fit_run.factor : 0,
                                                              fit_run.offset,
                                                              fit_run.width};
        return 1;
    }
    layout_pending = NULL;
    if (layout_depth && layout_cursor < layout_count) {
        LayoutPlacement *p = &layout_placements[layout_cursor++];
        if (p->owner == KHDAYS_REF(owner) && p->font == KHDAYS_REF(font) && p->ch == ch)
            layout_pending = p;
    }
    return 0;
}

void khdays_ui_text_fit_begin(const void *font, double origin, double left, double factor, double offset)
{
    fit_run.font = KHDAYS_REF(font);
    fit_run.origin = origin;
    fit_run.left = left;
    fit_run.factor = factor;
    fit_run.offset = offset;
    fit_run.report = tracing();
    fit_run.ink_count = 0;
}

void khdays_ui_text_fit_end(void)
{
    if (fit_run.report)
        say("HD fit: font %08x origin %.3f factor %.9f, captured ink [%.3f, %.3f), %d glyph passes", fit_run.font,
            fit_run.origin, fit_run.factor, fit_run.ink_left, fit_run.ink_right, fit_run.ink_count);
    memset(&fit_run, 0, sizeof(fit_run));
}

typedef struct CrawlPen {
    KhdaysRef context, text;
    int index, next_x, y;
    double residual;
    KhdaysRef font;
    double origin_x, origin_y, positions[2][2];
} CrawlPen;
static CrawlPen crawl_pens[32];
static CrawlPen *crawl_active;
static unsigned int crawl_next;
static int crawl_x;
static double crawl_offset;
static KhdaysRef crawl_font;
static const char *font_path(KhdaysRef font);
extern unsigned short NNS_G2dFontFindGlyphIndex(KhdaysRef font, unsigned ch);
extern KhdaysRef NNS_G2dFontGetCharWidthsFromIndex(KhdaysRef font, unsigned glyph);

void khdays_ui_text_crawl_font(const void *font)
{
    crawl_font = font ? KHDAYS_REF(font) : 0;
}

static void crawl_measure(CrawlPen *pen, int index, int hd, double *x, double *y)
{
    const int previous = khdays_hd_font_layout_override(hd);
    const unsigned short *text = KHDAYS_W(pen->text);
    const char *path = font_path(pen->font);
    const KhdaysRef info = *(const unsigned *)KHDAYS_W(pen->font);
    const KhdaysRef table = *(const unsigned *)((const unsigned char *)KHDAYS_W(info) + 8);
    const int feed = ((const unsigned char *)KHDAYS_W(table))[1];
    *x = pen->origin_x;
    *y = pen->origin_y;
    unsigned before = 0;
    for (int i = 0; i <= index && i < 4096 && text[i]; ++i) {
        const unsigned ch = text[i];
        if (ch == 10) {
            if (i == index)
                break;
            *x = 0;
            *y += khdays_hd_font_metric_f(path, -1, KHDF_LINE_FEED, feed) + 3;
            before = 0;
            continue;
        }
        *x += khdays_hd_font_kerning_f(path, before, ch);
        if (i == index)
            break;
        unsigned glyph = NNS_G2dFontFindGlyphIndex(pen->font, ch);
        if (glyph == 0xffff)
            glyph = *(const unsigned short *)((const unsigned char *)KHDAYS_W(info) + 2);
        const signed char *metrics = KHDAYS_W(NNS_G2dFontGetCharWidthsFromIndex(pen->font, glyph));
        *x += khdays_hd_font_metric_f(path, (int)glyph, KHDF_ADVANCE, metrics[2]);
        before = ch;
    }
    khdays_hd_font_layout_override(previous);
}

double khdays_ui_text_layout_x(double x)
{
    if (!crawl_active || !crawl_active->font)
        return x;
    const int hd = layout_probe ? !khdays_hud_setting(KHDAYS_OPT_FONT) : khdays_hud_setting(KHDAYS_OPT_FONT);
    return crawl_active->positions[hd != 0][0];
}
double khdays_ui_text_layout_y(double y)
{
    if (!crawl_active || !crawl_active->font)
        return y;
    const int hd = layout_probe ? !khdays_hud_setting(KHDAYS_OPT_FONT) : khdays_hud_setting(KHDAYS_OPT_FONT);
    return crawl_active->positions[hd != 0][1];
}

int khdays_ui_text_round(double value)
{
    /* Repeated sums must not move an exact half-pixel tie below the boundary. */
    return (int)lround(value + copysign(1e-9, value));
}

void khdays_ui_text_position(const void *font, double x, double y)
{
    positioned_font = font ? KHDAYS_REF(font) : 0;
    positioned_x = x;
    positioned_y = y;
}

double khdays_ui_text_pen_x(int x)
{
    return x + (crawl_active ? crawl_offset : 0);
}

/* A crawl spans frames, but the game's persistent cursor remains an integer.
   Carry only its residual; a stream, index or cursor change resets it. */
void khdays_ui_text_crawl_begin(const void *context, const void *text, int index, int x, int y, double kerning)
{
    const KhdaysRef key = KHDAYS_REF(context), stream = KHDAYS_REF(text);
    CrawlPen *pen = NULL;
    for (int i = 0; i < 32; ++i)
        if (crawl_pens[i].context == key && crawl_pens[i].text == stream) {
            pen = &crawl_pens[i];
            break;
        }
    if (!pen) {
        pen = &crawl_pens[crawl_next++ % 32];
        memset(pen, 0, sizeof(*pen));
        pen->context = key;
        pen->text = stream;
        pen->index = -1;
    }
    if (!index || pen->index + 1 != index || pen->next_x != x || pen->y != y)
        pen->residual = 0;
    if (crawl_font) {
        pen->font = crawl_font;
        if (!index || pen->index < 0) {
            pen->origin_x = x;
            pen->origin_y = y;
        }
        for (int hd = 0; hd < 2; ++hd)
            crawl_measure(pen, index, hd, &pen->positions[hd][0], &pen->positions[hd][1]);
    }
    pen->index = index;
    pen->y = y;
    crawl_x = x;
    crawl_offset = pen->residual + kerning;
    crawl_active = pen;
}

int khdays_ui_text_crawl_advance(double advance)
{
    if (!crawl_active)
        return khdays_ui_text_round(advance);
    const double next = crawl_active->font
                            ? crawl_active->positions[khdays_hud_setting(KHDAYS_OPT_FONT) != 0][0] + advance
                            : crawl_x + crawl_offset + advance;
    const int rounded = khdays_ui_text_round(next);
    crawl_active->next_x = rounded;
    crawl_active->residual = next - rounded;
    crawl_active = NULL;
    return rounded - crawl_x;
}

static int tracing(void)
{
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_TEXT") != NULL;
    }
    return trace;
}

static int profiling(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("KHDAYS_PROFILE_TEXT") != NULL;
    return on;
}

/* Whether the mods' fonts are drawn: [video] hd_fonts as the game started (the player's choice), and the A/B
 * diagnostic switch KHDAYS_HD_FONT_DISABLE. Off, the DS's text is left unmodified. */
static int hd_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *v = getenv("KHDAYS_HD_FONT_DISABLE");
        enabled = !(v && *v && strcmp(v, "0") != 0);
    }
    return enabled && khdays_host_hd_fonts();
}

static int font_display_enabled(void)
{
    return hd_enabled() && khdays_hud_setting(KHDAYS_OPT_FONT);
}

static void say(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "ui text: %s\n", line);
    khdays_log("ui text: %s", line);
}

static Canvas *find(KhdaysRef cc)
{
    for (int i = 0; i < canvas_count; ++i) {
        if (canvases[i].cc == cc && !canvases[i].detached) {
            return &canvases[i];
        }
    }
    return NULL;
}

static void release_extra(Canvas *c)
{
    if (c->extra) {
        khdays_ui_image_put(c->extra->image);
        khdays_ui_image_put(c->extra->hd_image);
        free(c->extra);
        c->extra = NULL;
    }
}

static void dispose_canvas(Canvas *c)
{
    free(c->information_pixels);
    release_extra(c);
    free(c->seen);
    free(c->cells);
    free(c->ops);
    free(c->display_ops);
    free(c->source);
    free(c->base);
    free(c->display_base);
    free(c->dirty);
    free(c->tile_hash);
    free(c->graphics);
    free(c->owned);
    khdays_ui_image_put(c->hd_image);
    khdays_ui_image_put(c->image);
}

static void forget(Canvas *c)
{
    dispose_canvas(c);
    *c = canvases[--canvas_count];
}

static void obtained_reset(void)
{
    dispose_canvas(&obtained.canvas);
    memset(&obtained, 0, sizeof(obtained));
    obtained.canvas.image = obtained.canvas.hd_image = -1;
    obtained.slot = -1;
}

static void obtained_written(int engine, uint32_t address, uint32_t bytes)
{
    if (engine == 0 && obtained.bound && address < obtained.address + 512 &&
        (uint64_t)address + bytes > obtained.address) {
        obtained.source_valid = obtained.ready = 0;
    }
}

static int pickup_enabled(void)
{
    return obtained.notification ? khdays_ui_notification_enabled(1) : khdays_ui_obtained_enabled();
}
static int pickup_owns(void)
{
    return obtained.notification ? khdays_ui_notification_owns(1) : khdays_ui_obtained_owns();
}
static int obtained_cell(const Canvas *c, const KhdaysBgLayer *l, int x, int y)
{
    if (khdays_ui_mission_panel_cell(c->engine, c->bg, x, y) ||
        (c != &obtained.canvas &&
         khdays_ui_notification_cell(c->engine, c->bg, (x % l->wt + l->wt) % l->wt, (y % l->ht + l->ht) % l->ht)))
        return 1;
    return !obtained.notification && !obtained.sequence && obtained.ready && c != &obtained.canvas && c->engine == 0 &&
           c->bg == 1 && (x % l->wt + l->wt) % l->wt >= 1 && (x % l->wt + l->wt) % l->wt < 9 &&
           (y % l->ht + l->ht) % l->ht >= 8 && (y % l->ht + l->ht) % l->ht < 10;
}

/* A canvas can outlive its source buffer: the game often uploads its characters
 * to BG VRAM, then immediately frees the source. The port needs the recorded
 * glyphs until those VRAM characters are replaced. Never dereference its
 * original DS canvas or buffer again after detaching. */
static void detach_or_forget(Canvas *c)
{
    if (c->pending || c->in_vram) {
        c->detached = 1;
        free(c->source);
        c->source = NULL;
        free(c->base);
        c->base = NULL;
        free(c->ops);
        c->ops = NULL;
        c->op_count = c->op_room = 0;
        if (tracing())
            say("HD canvas %08x retained after release (engine %c BG%d, %d glyph operations)", c->cc,
                c->engine ? 'B' : 'A', c->bg, c->display_count);
    } else {
        forget(c);
    }
}

static Canvas *new_canvas(KhdaysRef ref, KhdaysRef buffer, int width, int height, int bpp, int param,
                          const char *made_by)
{
    Canvas *c;
    if (canvas_count == canvas_room) {
        const int room = canvas_room ? canvas_room * 2 : 16;
        Canvas *grown = (Canvas *)realloc(canvases, (size_t)room * sizeof(Canvas));
        if (grown == NULL) {
            return NULL;
        }
        canvases = grown;
        canvas_room = room;
    }
    c = &canvases[canvas_count++];
    memset(c, 0, sizeof(*c));
    c->cc = ref;
    c->buffer = buffer;
    c->area_w = width;
    c->area_h = height;
    c->bpp = bpp;
    c->param = param;
    c->bytes = (uint32_t)c->param * (uint32_t)c->area_h * (uint32_t)(8 * bpp);
    c->source = (uint8_t *)malloc(c->bytes);
    c->base = (uint8_t *)malloc(c->bytes);
    c->display_base = (uint8_t *)calloc(c->bytes, 1);
    c->seen = (uint8_t *)calloc(c->bytes, 1);
    c->dirty = (uint8_t *)malloc((size_t)c->area_w * c->area_h);
    c->tile_hash = (uint32_t *)calloc((size_t)c->area_w * c->area_h, sizeof(uint32_t));
    c->graphics = (uint32_t *)calloc((size_t)c->area_w * c->area_h, sizeof(uint32_t));
    c->owned = (uint64_t *)calloc((size_t)c->area_w * c->area_h, sizeof(uint64_t));
    c->image = -1;
    c->hd_image = -1;
    if (!c->source || !c->base || !c->display_base || !c->seen || !c->dirty || !c->tile_hash || !c->graphics ||
        !c->owned) {
        forget(c);
        return NULL;
    }
    memcpy(c->source, KHDAYS_W(c->buffer), c->bytes);
    memcpy(c->base, c->source, c->bytes);
    memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
    c->hd_last_state = -1;
    snprintf(c->id, sizeof(c->id), "text.canvas");
    if (tracing()) {
        say("canvas %08x (%dx%d characters, %d bits, buffer %08x) made by %s", c->cc, c->area_w, c->area_h, c->bpp,
            c->buffer, made_by);
    }
    return c;
}

/* The canvas for the game's structure at `cc_host`: made the first time, and
 * made again when its buffer or its size changed. */
static Canvas *canvas_of(const void *cc_host, const char *made_by)
{
    const DsCanvas *cc = (const DsCanvas *)cc_host;
    const KhdaysRef ref = KHDAYS_REF(cc_host);
    const int bpp = cc->dst_bpp == 8 ? 8 : 4;
    const uint64_t end = (uint64_t)cc->char_base + (uint64_t)cc->param * (uint32_t)cc->area_height * 8u * (unsigned)bpp;
    /* any size, as long as its characters lie in the game's memory: anything else is no canvas */
    if (cc->area_width <= 0 || cc->area_height <= 0 || cc->param < (uint32_t)cc->area_width || end > UINT32_MAX ||
        !khdays_game_ram_ref(cc->char_base) || !khdays_game_ram_ref((uint32_t)end - 1u))
        return NULL;
    Canvas *c = find(ref);
    if (c != NULL && (c->buffer != cc->char_base || c->area_w != cc->area_width || c->area_h != cc->area_height ||
                      c->bpp != bpp || c->param != (int)cc->param)) {
        detach_or_forget(c);
        c = NULL;
    }
    if (c == NULL) {
        for (int i = 0; i < canvas_count; ++i) {
            Canvas *copy = &canvases[i];
            if (copy->copied && !copy->detached && copy->buffer == cc->char_base && copy->area_w == cc->area_width &&
                copy->area_h == cc->area_height && copy->bpp == bpp && copy->param == (int)cc->param) {
                copy->cc = ref;
                copy->copied = 0;
                return copy;
            }
        }
        c = new_canvas(ref, cc->char_base, cc->area_width, cc->area_height, bpp, (int)cc->param, made_by);
    }
    return c;
}

/* --- Glyph recordings. Source names are original NitroFS paths. -------------------- */
static const char *font_from_origin(const void *font, const void *file);
static void bind_charmap(const char *path, const void *font);
static const char *operation_scopes[16];
static unsigned operation_depth, operation_overflow;

void khdays_ui_text_scope_push(const char *scope)
{
    if (operation_depth == 16)
        ++operation_overflow;
    else
        operation_scopes[operation_depth++] = scope;
}

void khdays_ui_text_scope_pop(void)
{
    if (operation_overflow)
        --operation_overflow;
    else if (operation_depth)
        --operation_depth;
}

static int obtained_wants(unsigned int slot, int announcement, unsigned key, int notification)
{
    const int sequence = notification ? khdays_ui_notification_configured(1) : khdays_ui_obtained_configured();
    if (!announcement || slot >= 24 || (!hd_enabled() && !sequence)) {
        obtained_reset();
        return 0;
    }
    if (obtained.active && obtained.slot == (int)slot && obtained.key == key && obtained.notification == notification &&
        obtained.sequence == sequence && obtained.canvas.seen)
        return 0;
    obtained_reset();
    double command_offset, popup_offset;
    const double command_width = khdays_hd_font_fit_layout(KHDF_FIT_ITEMS, &command_offset);
    const double popup_width = khdays_hd_font_fit_layout(KHDF_FIT_OBTAINED, &popup_offset);
    int different = command_width != popup_width || (command_width > 0 && command_offset != popup_offset);
    if (!sequence && !different && !khdays_hd_font_mappings_enabled())
        return 0;
    int replacement = 0;
    const char *sources[] = {"text/font_eu_08.nftr", "text/font_eu_08s.nftr"};
    for (int i = 0; i < 2; ++i) {
        char command[160], popup[160];
        if (!khdays_hd_font_select(sources[i], "field.command", command, sizeof(command)))
            snprintf(command, sizeof(command), "%s", sources[i]);
        if (!khdays_hd_font_select(sources[i], "field.obtained", popup, sizeof(popup)))
            snprintf(popup, sizeof(popup), "%s", sources[i]);
        different |= strcmp(command, popup) != 0;
        if (different)
            replacement |= khdays_hd_font_has(command) || khdays_hd_font_has(popup);
    }
    if (!sequence && (!different || !replacement))
        return 0;
    obtained.active = 1;
    obtained.notification = notification;
    obtained.sequence = sequence;
    obtained.slot = (int)slot;
    obtained.key = key;
    return 1;
}

static int obtained_item_key(unsigned slot, unsigned *key)
{
    if (slot >= 24 || !khdays_game_ram_ref(0x0207f620u))
        return 0;
    const uint32_t context = *(const uint32_t *)KHDAYS_W(0x0207f620u);
    if (!context || context > UINT32_MAX - 0x5d8u || !khdays_game_ram_ref(context) ||
        !khdays_game_ram_ref(context + 0x5d7u))
        return 0;
    if (slot >= *(const uint16_t *)KHDAYS_W(context + 0x4aeu))
        return 0;
    *key = *(const uint16_t *)KHDAYS_W(context + 0x4b8u + slot * 12u);
    return 1;
}

int khdays_ui_text_obtained_wants(unsigned int slot, int announcement)
{
    unsigned key = 0;
    const int valid = obtained_item_key(slot, &key);
    const int backpack = !announcement && khdays_ui_notification_enabled(1);
    khdays_ui_obtained_request(valid && announcement);
    khdays_ui_notification_request(1, valid && backpack);
    return obtained_wants(slot, valid && (announcement || backpack), key, backpack);
}

void khdays_ui_text_obtained_capture(const void *surface)
{
    const KhdaysRef object = *(const uint32_t *)((const uint8_t *)surface + 0x18);
    Canvas *c = object ? find(object + SURFACE_OBJECT_CANVAS) : NULL;
    if (!obtained.active || !c || c->bpp != 4 || c->area_w != 8 || c->area_h != 2 || c->param != 8)
        return;
    memcpy(c->seen, c->source, c->bytes);
    memcpy(c->display_base, c->base, c->bytes);
    c->display_ops = c->ops;
    c->display_count = c->op_count;
    c->display_room = c->op_room;
    c->ops = NULL;
    c->op_count = c->op_room = 0;
    memset(c->owned, 0xff, 16 * sizeof(uint64_t));
    snprintf(c->id, sizeof(c->id), "text.a1.1.8");
    obtained.canvas = *c;
    *c = canvases[--canvas_count];
    obtained.canvas.detached = 1;
    if (tracing())
        say("HD obtained capture: slot %d, %d glyphs, independent native fallback", obtained.slot,
            obtained.canvas.display_count);
}

void khdays_ui_text_field_tutorial_before(int step, int arg)
{
    (void)step;
    (void)arg;
    khdays_ui_text_scope_push("field.tutorial");
}

void khdays_ui_text_field_tutorial(int step, int arg)
{
    (void)step;
    (void)arg;
    khdays_ui_text_scope_pop();
}

static const char *selected_font_path(FontBinding *binding, KhdaysRef font)
{
    if (!binding->route_enabled)
        return binding->replacement ? binding->path : NULL;
    char scope[80];
    const char *operation = operation_depth && !operation_overflow ? operation_scopes[operation_depth - 1] : NULL;
    if (operation && strncmp(operation, "mission_select.", 15) != 0)
        snprintf(scope, sizeof(scope), "%s", operation);
    else {
        khdays_camp_font_scope(font, scope, sizeof(scope));
        if (operation) {
            char base[80];
            snprintf(base, sizeof(base), "%s", scope);
            if (!base[0]) {
                int story;
                const int screen = khdays_camp_screen(&story);
                if (screen != KHDAYS_CAMP_NONE)
                    snprintf(base, sizeof(base), "camp.%s.screen.%d", story ? "story" : "mission", screen);
            }
            if (base[0])
                snprintf(scope, sizeof(scope), "%s.%s", base, operation);
        }
    }
    if (strcmp(scope, binding->scope) != 0) {
        snprintf(binding->scope, sizeof(binding->scope), "%s", scope);
        const int observing = khdays_hd_font_observe(1);
        const int layout = khdays_hd_font_layout_override(1);
        binding->scoped =
            scope[0] && khdays_hd_font_select(binding->path, scope, binding->selected, sizeof(binding->selected));
        if (binding->scoped) {
            bind_charmap(binding->selected, KHDAYS_W(font));
            binding->scoped = khdays_hd_font_map_ready(binding->selected);
        }
        khdays_hd_font_layout_override(layout);
        khdays_hd_font_observe(observing);
        if (tracing() && scope[0])
            say("HD font scope %s: %s -> %s", scope, binding->path,
                binding->scoped ? binding->selected : binding->path);
    }
    return binding->scoped ? binding->selected : binding->replacement ? binding->path : NULL;
}

static const char *font_path(KhdaysRef font)
{
    if (!font)
        return NULL;
    const KhdaysRef info = *(const uint32_t *)KHDAYS_W(font);
    for (int i = binding_count - 1; i >= 0; --i)
        if (font_bindings[i].font == font && font_bindings[i].info == info)
            return selected_font_path(&font_bindings[i], font);
    for (int i = binding_count - 1; i >= 0; --i)
        if (info && font_bindings[i].info == info)
            return selected_font_path(&font_bindings[i], font);
    return font_from_origin(KHDAYS_W(font), NULL);
}

static void unbind_font(KhdaysRef font)
{
    for (int i = 0; i < binding_count; ++i)
        if (font_bindings[i].font == font) {
            font_bindings[i] = font_bindings[--binding_count];
            break;
        }
}

/* Each glyph's character by the font's CMAP blocks (UINT32_MAX: none), `*count` glyphs (free it);
   NULL when its tables cannot be read. */
static uint32_t *font_codes(const void *font, uint32_t *count_out)
{
    const uint32_t info = *(const uint32_t *)font;
    if (!info)
        return NULL;
    const uint32_t *finf = (const uint32_t *)KHDAYS_W(info);
    if (finf[2] < 8 || !finf[4])
        return NULL;
    const uint8_t *cell = (const uint8_t *)KHDAYS_W(finf[2]);
    const uint32_t stride = cell[2] | (uint32_t)cell[3] << 8;
    const uint32_t bytes = *(const uint32_t *)(cell - 4);
    if (!stride || bytes < 16 || (bytes - 16) / stride > 16384)
        return NULL;
    const uint32_t count = (bytes - 16) / stride;
    if (!count)
        return NULL;
    uint32_t *codes = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    if (!codes)
        return NULL;
    for (uint32_t i = 0; i < count; ++i)
        codes[i] = UINT32_MAX;
    uint32_t map = finf[4];
    int valid = 1;
    for (int block = 0; map && block < 128; ++block) {
        const uint16_t *m = (const uint16_t *)KHDAYS_W(map);
        const uint32_t first = m[0], last = m[1], method = m[2];
        const uint16_t *table = m + 6;
        if (last < first || method > 2) {
            valid = 0;
            break;
        }
        if (method == 2) {
            for (uint32_t i = 0; i < table[0]; ++i) {
                const uint32_t code = table[1 + i * 2], glyph = table[2 + i * 2];
                if (glyph < count && codes[glyph] == UINT32_MAX)
                    codes[glyph] = code;
            }
        } else {
            for (uint32_t code = first; code <= last; ++code) {
                const uint32_t glyph = method == 0 ? table[0] + code - first : table[code - first];
                if (glyph < count && codes[glyph] == UINT32_MAX)
                    codes[glyph] = code;
            }
        }
        const uint32_t next = *(const uint32_t *)(m + 4);
        if (next == map) {
            valid = 0;
            break;
        }
        map = next;
    }
    if (!valid || map) {
        free(codes);
        return NULL;
    }
    *count_out = count;
    return codes;
}

/* FINF/CMAP index identity is independent of the replacement atlas order. */
static void bind_charmap(const char *path, const void *font)
{
    if (khdays_hd_font_map_ready(path))
        return;
    uint32_t count = 0;
    uint32_t *codes = font_codes(font, &count);
    if (codes)
        khdays_hd_font_bind_charmap(path, codes, (int)count);
    free(codes);
}

/* The DS buttons the game's fonts' glyphs draw (button_icons.h), by font (its tables' places and its
   first CMAP block's range and method: another font loaded where one was differs there): for each
   glyph its button and piece (button | piece << 5), 0 for the others; `buttons` NULL when its tables
   were unreadable. */
typedef struct ButtonFont {
    uint32_t info, glyphs, map, head, count;
    uint8_t *buttons;
} ButtonFont;
static ButtonFont *button_fonts;
static int button_font_count, button_font_room;

/* The tiles `op` covers are drawn again. */
static void dirty_op(Canvas *c, const GlyphOp *op)
{
    if (op->clip_l >= op->clip_r || op->clip_t >= op->clip_b)
        return;
    const int col0 = op->clip_l < 0 ? 0 : op->clip_l / 8, row0 = op->clip_t < 0 ? 0 : op->clip_t / 8;
    for (int row = row0; row <= (op->clip_b - 1) / 8 && row < c->area_h; ++row)
        for (int col = col0; col <= (op->clip_r - 1) / 8 && col < c->area_w; ++col)
            c->dirty[row * c->area_w + col] |= 3;
}

/* Whether the text drawn now tells the field's controls: a field tutorial's page, or the camp's
   Tutorials (screen 6, docs/FONT_MAPPING.md). */
static int field_controls_text(void)
{
    int story;
    for (unsigned i = 0; i < operation_depth; ++i)
        if (strcmp(operation_scopes[i], "field.tutorial") == 0)
            return 1;
    return khdays_camp_screen(&story) == 6;
}

static int glyph_button(const void *font, int glyph, int *piece)
{
    const uint32_t info = *(const uint32_t *)font;
    *piece = 0;
    if (!info)
        return KHDAYS_BUTTON_NONE;
    const uint32_t *finf = (const uint32_t *)KHDAYS_W(info);
    const uint16_t *block = finf[4] ? (const uint16_t *)KHDAYS_W(finf[4]) : NULL;
    const uint32_t head = block ? ((uint32_t)block[0] | (uint32_t)block[1] << 16) ^ block[2] : 0;
    ButtonFont *f = NULL;
    for (int i = 0; i < button_font_count && !f; ++i)
        if (button_fonts[i].info == info && button_fonts[i].glyphs == finf[2] && button_fonts[i].map == finf[4])
            f = &button_fonts[i];
    if (!f) {
        if (button_font_count == button_font_room) {
            const int room = button_font_room ? button_font_room * 2 : 8;
            ButtonFont *grown = (ButtonFont *)realloc(button_fonts, (size_t)room * sizeof(*grown));
            if (!grown)
                return KHDAYS_BUTTON_NONE;
            button_fonts = grown;
            button_font_room = room;
        }
        f = &button_fonts[button_font_count++];
        f->buttons = NULL;
        f->head = ~head; /* read below */
    }
    if (f->head != head) { /* new, or another font loaded where this one was */
        free(f->buttons);
        memset(f, 0, sizeof(*f));
        f->info = info;
        f->glyphs = finf[2];
        f->map = finf[4];
        f->head = head;
        uint32_t *codes = font_codes(font, &f->count);
        if (codes && (f->buttons = (uint8_t *)calloc(f->count, 1)) != NULL)
            for (uint32_t g = 0; g < f->count; ++g) {
                int p = 0;
                const int b = codes[g] != UINT32_MAX ? khdays_button_glyph(codes[g], &p) : KHDAYS_BUTTON_NONE;
                f->buttons[g] = (uint8_t)(b | p << 5);
            }
        free(codes);
    }
    if (!f->buttons || glyph < 0 || (uint32_t)glyph >= f->count)
        return KHDAYS_BUTTON_NONE;
    *piece = f->buttons[glyph] >> 5;
    return f->buttons[glyph] & 31;
}

static const char *bind_font_file_observed(const void *font, const char *name, KhdaysRef file)
{
    size_t n = 0;
    char path[160];
    if (!font || !name)
        return NULL;
    while (*name == '/' || *name == '\\')
        ++name; /* NitroFS paths may start at the archive root. */
    while (n < sizeof(path) - 1 && name[n] && (unsigned char)name[n] >= 32) {
        path[n] = name[n] == '\\' ? '/' : name[n];
        ++n;
    }
    if (n == sizeof(path) - 1 || n == 0)
        return NULL;
    path[n] = 0;
    if (n < 5)
        return NULL;
    for (size_t i = n - 5; i < n; ++i)
        if (path[i] >= 'A' && path[i] <= 'Z')
            path[i] = (char)(path[i] + ('a' - 'A'));
    if (strcmp(path + n - 5, ".nftr") != 0)
        return NULL;
    unbind_font(KHDAYS_REF(font));
    int replacement = khdays_hd_font_has(path);
    if (replacement) {
        bind_charmap(path, font);
        replacement = khdays_hd_font_map_ready(path);
    }
    const KhdaysRef ref = KHDAYS_REF(font);
    int pos = 0;
    while (pos < binding_count && font_bindings[pos].font != ref)
        ++pos;
    if (pos == binding_count) {
        if (binding_count == binding_room) {
            const int room = binding_room ? binding_room * 2 : 32;
            FontBinding *grown = (FontBinding *)realloc(font_bindings, (size_t)room * sizeof(*grown));
            if (!grown)
                return NULL; /* out of memory: the game's own font */
            font_bindings = grown;
            binding_room = room;
        }
        ++binding_count;
    }
    font_bindings[pos].font = ref;
    font_bindings[pos].info = *(const uint32_t *)font;
    font_bindings[pos].file = file;
    font_bindings[pos].replacement = replacement;
    font_bindings[pos].scope[0] = 0;
    font_bindings[pos].scoped = 0;
    font_bindings[pos].route_enabled = khdays_hd_font_mappings_enabled();
    memcpy(font_bindings[pos].path, path, n + 1);
    if (tracing() && replacement)
        say("HD font %08x bound to %s", ref, path);
    return selected_font_path(&font_bindings[pos], ref);
}

static const char *bind_font_file(const void *font, const char *name, KhdaysRef file)
{
    const int observing = khdays_hd_font_observe(1);
    const int layout = khdays_hd_font_layout_override(1);
    const char *path = bind_font_file_observed(font, name, file);
    khdays_hd_font_layout_override(layout);
    khdays_hd_font_observe(observing);
    return path;
}

static void bind_font(void *font, const char *name)
{
    /* Both named game loaders pass their 12-byte resource handle. */
    if (font)
        (void)bind_font_file(font, name, ((const uint32_t *)font)[2]);
}

static const char *font_from_origin(const void *font, const void *file)
{
    if (!font)
        return NULL;
    const KhdaysRef info = *(const uint32_t *)font;
    const char *name;
    uint32_t offset, run;
    if (info && khdays_origin_of_ram(info, &name, &offset, &run)) {
        const char *path = bind_font_file(font, name, info - offset);
        if (path)
            return path;
    }
    const KhdaysRef ref = file ? KHDAYS_REF(file) : 0;
    if (ref && khdays_origin_of_ram(ref, &name, &offset, &run))
        return bind_font_file(font, name, ref - offset);
    return NULL;
}

void khdays_ui_text_font_init(const void *font, const void *file)
{
    if (!font)
        return;
    if (font_from_origin(font, file))
        return; /* A reused heap can put a different NFTR at identical addresses. */
    const KhdaysRef ref = KHDAYS_REF(font);
    for (int i = 0; i < binding_count; ++i)
        if (font_bindings[i].font == ref) {
            if (font_bindings[i].info == *(const uint32_t *)font &&
                (!file || font_bindings[i].file == KHDAYS_REF(file)))
                return;
            unbind_font(ref);
            break;
        }
}

void khdays_ui_text_font_loaded(void *font, const char *name, int arg2, int arg3, int result)
{
    (void)arg2;
    (void)arg3;
    if (result)
        bind_font(font, name);
}

void khdays_ui_text_font_bound(void *font, const void *name)
{
    bind_font(font, (const char *)name);
}

int khdays_ui_text_metric(const void *font, int glyph, int field, int fallback)
{
    if (!font || !hd_enabled() || khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return fallback;
    return khdays_hd_font_metric(font_path(KHDAYS_REF(font)), glyph, field, fallback);
}

int khdays_ui_text_numeric(const void *font, int layout)
{
    if (!font || !khdays_hd_font_numeric_layout(layout) || !hd_enabled() ||
        khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return 0;
    const char *path = font_path(KHDAYS_REF(font));
    return path && khdays_hd_font_has(path);
}

double khdays_ui_text_fit_width(const void *font, int layout, double *offset)
{
    if (fit_run.font)
        return 0;
    fit_run.width = 0;
    if (!font || !hd_enabled() || khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return 0;
    const double width = khdays_hd_font_fit_layout(layout, offset);
    const char *path = width > 0 ? font_path(KHDAYS_REF(font)) : NULL;
    /* Retain the selected limit through begin/end, including filtering margins. */
    return fit_run.width = path && khdays_hd_font_has(path) ? width : 0;
}

int khdays_ui_text_ink(const void *font, int glyph, int w, int h, int ink, double *bounds)
{
    bounds[0] = 0;
    bounds[1] = ink;
    return khdays_hd_font_ink(font_path(KHDAYS_REF(font)), glyph, w, h, ink, bounds);
}

int khdays_ui_text_kerning(const void *font, int left, int right)
{
    if (!font || !hd_enabled() || khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return 0;
    return khdays_hd_font_kerning(font_path(KHDAYS_REF(font)), (unsigned)left, (unsigned)right);
}

int khdays_ui_text_extent(const void *font, int glyph, int left, int width, int advance)
{
    if (!font || !hd_enabled() || khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return advance;
    return khdays_hd_font_extent(font_path(KHDAYS_REF(font)), glyph, left, width, advance);
}

double khdays_ui_text_metric_f(const void *font, int glyph, int field, double fallback)
{
    if (!font || !hd_enabled() || khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return fallback;
    return khdays_hd_font_metric_f(font_path(KHDAYS_REF(font)), glyph, field, fallback);
}

double khdays_ui_text_kerning_f(const void *font, int left, int right)
{
    if (!font || !hd_enabled() || khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return 0;
    return khdays_hd_font_kerning_f(font_path(KHDAYS_REF(font)), (unsigned)left, (unsigned)right);
}

double khdays_ui_text_extent_f(const void *font, int glyph, int left, int width, int advance)
{
    if (!font || !hd_enabled() || khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_OFF)
        return advance;
    return khdays_hd_font_extent_f(font_path(KHDAYS_REF(font)), glyph, left, width, advance);
}

static int record(Canvas *c, const GlyphOp *op)
{
    if (c->ops_disabled)
        return 0;
    if (c->op_count >= c->op_room) {
        const int room = c->op_room ? c->op_room * 2 : 64;
        GlyphOp *grown = (GlyphOp *)realloc(c->ops, (size_t)room * sizeof(GlyphOp));
        if (!grown) { /* out of memory: the canvas keeps the game's own drawing */
            c->ops_disabled = 1;
            c->op_count = 0;
            return 0;
        }
        c->ops = grown;
        c->op_room = room;
    }
    c->ops[c->op_count++] = *op;
    return 1;
}

static void clear_hd_rect(Canvas *c, int x, int y, int w, int h);
static double painted_width(const GlyphOp *op);
static void observe_source(Canvas *c);
static unsigned pixel(const Canvas *c, const uint8_t *data, int x, int y);
static void put_pixel(const Canvas *c, uint8_t *data, int x, int y, unsigned value);
static void fallback_glyph(Canvas *c, const void *font, const void *glyph, int x, int y);

static void dirty_rect(Canvas *c, int l, int t, int r, int b)
{
    if (l < 0)
        l = 0;
    if (t < 0)
        t = 0;
    if (r > c->area_w * 8)
        r = c->area_w * 8;
    if (b > c->area_h * 8)
        b = c->area_h * 8;
    if (l >= r || t >= b)
        return;
    for (int row = t / 8; row < (b + 7) / 8; ++row)
        for (int col = l / 8; col < (r + 7) / 8; ++col)
            c->dirty[row * c->area_w + col] |= 3;
}

void khdays_ui_text_glyph_before(const void *canvas, const void *font, int x, int y, int cl, const void *glyph)
{
    if (fitting_native)
        return;
    (void)font;
    (void)x;
    (void)y;
    (void)cl;
    (void)glyph;
    if (!canvas)
        return;
    Canvas *c = canvas_of(canvas, "DrawGlyphLine input");
    if (!c)
        return;
    if (operation_depth && !operation_overflow && strcmp(operation_scopes[operation_depth - 1], "field.command") == 0)
        c->command_cache = 1;
    observe_source(c);
    c->before_glyph = 1;
}

static GlyphLayout glyph_layout(const Canvas *c, const GlyphOp *op, const unsigned char *table,
                                const unsigned char *widths, const char *path, double x, double y, double origin,
                                double left, double factor, double offset, double limit, int hd)
{
    const int previous = khdays_hd_font_layout_override(hd);
    GlyphLayout g = {0};
    g.x = x;
    g.y = y + khdays_hd_font_baseline_f(path, op->glyph, (int)(int8_t)table[4]);
    g.w = table[0];
    g.h = table[1];
    g.paint_w = khdays_hd_font_metric(path, op->glyph, KHDF_WIDTH, widths[1]);
    if (g.paint_w == widths[1])
        g.paint_w = g.w;
    if (khdays_hd_font_metric(path, op->glyph, KHDF_SIZED, 0)) {
        g.w = (int)ceil(khdays_hd_font_metric_f(path, op->glyph, KHDF_CELL_WIDTH, g.w));
        g.h = (int)ceil(khdays_hd_font_metric_f(path, op->glyph, KHDF_CELL_HEIGHT, g.h));
        g.paint_w = (int)ceil(khdays_hd_font_metric_f(path, op->glyph, KHDF_WIDTH, g.paint_w));
    }
    if (!hd || op->native_glyph) {
        g.w = g.paint_w = widths[1];
        g.h = table[1];
    }
    g.clip_l = (int)floor(g.x < op->source_x ? g.x : op->source_x);
    g.clip_t = (int)floor(g.y < op->source_y ? g.y : op->source_y);
    g.clip_r = (int)ceil(fmax(g.x + g.w, op->source_x + table[0]));
    g.clip_b = (int)ceil(fmax(g.y + g.h, op->source_y + table[1]));
    if (factor > 0 && factor < 1) {
        g.x = origin + (g.x - origin - left) * factor + offset;
        g.x_scale = factor;
        g.clip_l = (int)floor(g.x);
        g.clip_r = (int)ceil(g.x + g.w * factor);
        if (limit > 0 && g.clip_r > (int)ceil(origin + offset + limit))
            g.clip_r = (int)ceil(origin + offset + limit);
    }
    if (g.clip_l < 0)
        g.clip_l = 0;
    if (g.clip_t < 0)
        g.clip_t = 0;
    if (g.clip_r > c->area_w * 8)
        g.clip_r = c->area_w * 8;
    if (g.clip_b > c->area_h * 8)
        g.clip_b = c->area_h * 8;
    khdays_hd_font_layout_override(previous);
    return g;
}

static void apply_layout(GlyphOp *op, int mode, int retain_clip)
{
    const GlyphLayout *old = &op->layouts[op->layout_mode], *g = &op->layouts[mode];
    const int l = retain_clip ? op->clip_l - old->clip_l : 0;
    const int t = retain_clip ? op->clip_t - old->clip_t : 0;
    const int r = retain_clip ? old->clip_r - op->clip_r : 0;
    const int b = retain_clip ? old->clip_b - op->clip_b : 0;
    op->x = g->x;
    op->y = g->y;
    op->x_scale = g->x_scale;
    op->w = g->w;
    op->h = g->h;
    op->paint_w = g->paint_w;
    op->clip_l = g->clip_l + l;
    op->clip_t = g->clip_t + t;
    op->clip_r = g->clip_r - r;
    op->clip_b = g->clip_b - b;
    op->layout_mode = mode;
}

static void capture_glyph(const void *canvas, const void *font, int x, int y, int cl, const void *glyph)
{
    if (fitting_native)
        return;
    Canvas *c = find(KHDAYS_REF(canvas));
    const char *path = font_path(KHDAYS_REF(font));
    if (!c || !c->before_glyph)
        return;
    c->before_glyph = 0;
    if (operation_depth && !operation_overflow)
        c->information = strcmp(operation_scopes[operation_depth - 1], "field.hud.information") == 0;
    if (operation_depth && !operation_overflow)
        c->notification = strcmp(operation_scopes[operation_depth - 1], "field.breakthrough") == 0 ? 2 : 0;
    if (c->information)
        c->information_epoch = khdays_ui_information_epoch();
    const int pickup = operation_depth && !operation_overflow &&
                       strcmp(operation_scopes[operation_depth - 1], "field.obtained") == 0 &&
                       (khdays_ui_obtained_configured() || khdays_ui_notification_configured(1));
    const int notice = c->notification && khdays_ui_notification_configured(c->notification);
    if ((c->information || pickup || notice) && !path)
        path = "";
    if ((!hd_enabled() && !c->information && !pickup && !notice) || !path || !font || !glyph || c->ops_disabled) {
        fallback_glyph(c, font, glyph, x, y);
        return;
    }
    /* The rewritten game's NNSG2dGlyph has two 32-bit references, including
       when it lives on the game's stack. Width comes from the actual draw. */
    const uint32_t *g = (const uint32_t *)glyph;
    const uint32_t info_ref = *(const uint32_t *)font;
    if (!info_ref || !g[0]) {
        fallback_glyph(c, font, glyph, x, y);
        return;
    }
    const uint32_t table_ref = *(const uint32_t *)((const uint8_t *)KHDAYS_W(info_ref) + 8);
    if (!table_ref) {
        fallback_glyph(c, font, glyph, x, y);
        return;
    }
    const uint8_t *table = (const uint8_t *)KHDAYS_W(table_ref);
    const uint32_t stride = table[2] | (uint32_t)table[3] << 8;
    const uint32_t start = table_ref + 8u;
    if (!stride || g[1] < start || (g[1] - start) % stride != 0) {
        fallback_glyph(c, font, glyph, x, y);
        return;
    }
    const uint8_t *widths = (const uint8_t *)KHDAYS_W(g[0]);
    const int index = (int)((g[1] - start) / stride);
    const int native = !hd_enabled() || !path[0] || !khdays_hd_font_glyph_available(path, index);
    const int fitted = fit_run.font == KHDAYS_REF(font) && fit_run.factor > 0 && fit_run.factor < 1;
    const int small = table[0] <= 16 && table[1] <= 16 && widths[1] <= 16;
    /* a DS button's glyph is kept as an operation (a snapshot of it when the font has no artwork for
       it): the player's input is drawn in its place, the glyph itself when there is no icon */
    int piece = 0;
    const int button = !native || small ? glyph_button(font, index, &piece) : KHDAYS_BUTTON_NONE;
    if (native && !button && ((!fitted && !layout_pending && !c->information && !pickup && !notice) || !small)) {
        fallback_glyph(c, font, glyph, x, y);
        return;
    }
    if (!table[0] || !table[1]) {
        fallback_glyph(c, font, glyph, x, y);
        return;
    }
    GlyphOp op = {0};
    op.glyph = index;
    op.button = button;
    op.piece = piece;
    op.source_x = x;
    op.source_y = y;
    op.source_w = widths[1];
    op.source_h = table[1];
    op.cl = cl;
    snprintf(op.resource, sizeof(op.resource), "%s", path);
    if (native || (table[0] <= 16 && table[1] <= 16 && widths[1] <= 16)) {
        unsigned char pixels[256] = {0};
        DsCanvas scratch = *(const DsCanvas *)canvas;
        scratch.char_base = KHDAYS_REF(pixels);
        scratch.area_width = scratch.area_height = scratch.param = 2;
        fitting_native = 1;
        DrawGlyphLine(KHDAYS_REF(&scratch), KHDAYS_REF(font), 0, 0, cl, KHDAYS_REF(glyph));
        fitting_native = 0;
        memcpy(op.native_pixels, pixels, sizeof(pixels));
        op.native_saved = c->bpp;
    }
    op.native_glyph = native;
    const int mode = khdays_hud_setting(KHDAYS_OPT_FONT) != 0;
    const double layout_x = positioned_font == KHDAYS_REF(font) ? positioned_x : x;
    const double layout_y = positioned_font == KHDAYS_REF(font) ? positioned_y : y;
    for (int hd = 0; hd < 2; ++hd) {
        const LayoutPlacement *p = hd != mode ? layout_pending : NULL;
        const int fitting = fit_run.font == KHDAYS_REF(font);
        op.layouts[hd] = glyph_layout(c, &op, table, widths, path, p ? p->x : layout_x, p ? p->y : layout_y,
                                      p ? p->origin : fit_run.origin, p ? p->left : fit_run.left,
                                      p         ? p->factor
                                      : fitting ? fit_run.factor
                                                : 0,
                                      p ? p->offset : fit_run.offset, p ? p->width : fit_run.width, hd);
    }
    apply_layout(&op, mode, 0);
    if (fitted && fit_run.report) {
        double bounds[2] = {0, widths[1]};
        if (!native)
            khdays_hd_font_ink(path, op.glyph, table[0], table[1], widths[1], bounds);
        if (bounds[1] > bounds[0]) {
            const double low = op.x + bounds[0] * op.x_scale, high = op.x + bounds[1] * op.x_scale;
            if (!fit_run.ink_count || low < fit_run.ink_left)
                fit_run.ink_left = low;
            if (!fit_run.ink_count || high > fit_run.ink_right)
                fit_run.ink_right = high;
            ++fit_run.ink_count;
        }
    }
    /* Reissuing at the same pen position replaces that draw, including colour. */
    for (int i = 0; i < c->op_count;) {
        if (c->ops[i].source_x == op.source_x && c->ops[i].source_y == op.source_y && c->ops[i].x == op.x &&
            c->ops[i].y == op.y) {
            memmove(&c->ops[i], &c->ops[i + 1], (size_t)(c->op_count - i - 1) * sizeof(*c->ops));
            --c->op_count;
        } else
            ++i;
    }
    /* a tutorial's buttons are the field's controls, but for an X among the face buttons ("A B X or
       Y": another of them just before it on its line) */
    int listed = 0;
    for (int i = 0; button == KHDAYS_BUTTON_X && i < c->op_count && !listed; ++i) {
        const GlyphOp *o = &c->ops[i];
        listed = (o->button == KHDAYS_BUTTON_A || o->button == KHDAYS_BUTTON_B || o->button == KHDAYS_BUTTON_Y) &&
                 o->source_y == op.source_y && o->source_x < op.source_x && op.source_x - o->source_x <= 64;
    }
    op.field = button && !listed && field_controls_text();
    /* an R just above this one becomes the next target's (paint_button) */
    for (int i = 0; button == KHDAYS_BUTTON_R && op.field && i < c->op_count; ++i)
        if (c->ops[i].button == KHDAYS_BUTTON_R && op.source_y - c->ops[i].source_y >= 8 &&
            op.source_y - c->ops[i].source_y <= 40)
            dirty_op(c, &c->ops[i]);
    /* START and SELECT are three glyphs: the first one's icon covers the others' */
    for (int i = op.piece > 0 ? c->op_count - 1 : -1; i >= 0; --i) {
        GlyphOp *first = &c->ops[i];
        if (first->button == op.button && first->piece == 0 && first->source_y == op.source_y &&
            first->source_x < op.source_x && op.source_x - first->source_x <= 48) {
            const double right = op.x + painted_width(&op);
            if (right > first->x + painted_width(first))
                first->paint_w = (int)ceil((right - first->x) / (first->x_scale > 0 ? first->x_scale : 1));
            if (first->clip_r < (int)ceil(right))
                first->clip_r = (int)ceil(right) < c->area_w * 8 ? (int)ceil(right) : c->area_w * 8;
            for (int hd = 0; hd < 2; ++hd) {
                GlyphLayout *group = &first->layouts[hd];
                const GlyphLayout *tail = &op.layouts[hd];
                const double right_edge = tail->x + tail->paint_w * (tail->x_scale > 0 ? tail->x_scale : 1);
                const int extent = (int)ceil((right_edge - group->x) / (group->x_scale > 0 ? group->x_scale : 1));
                if (group->paint_w < extent)
                    group->paint_w = extent;
                if (group->clip_r < (int)ceil(right_edge))
                    group->clip_r = (int)ceil(right_edge);
            }
            break;
        }
    }
    if (record(c, &op)) {
        /* Observe the game's output, but leave the graphics plane untouched.
           No original glyph samples are inspected or used as an alpha mask. */
        const uint8_t *now = (const uint8_t *)KHDAYS_W(c->buffer);
        for (int py = y > 0 ? y : 0; py < y + table[1] && py < c->area_h * 8; ++py)
            for (int px = x > 0 ? x : 0; px < x + widths[1] && px < c->area_w * 8; ++px)
                put_pixel(c, c->source, px, py, pixel(c, now, px, py));
    } else
        fallback_glyph(c, font, glyph, x, y);
}

void khdays_ui_text_glyph(const void *canvas, const void *font, int x, int y, int cl, const void *glyph)
{
    const int observing = khdays_hd_font_observe(1);
    capture_glyph(canvas, font, x, y, cl, glyph);
    khdays_hd_font_observe(observing);
}

void khdays_ui_text_clear(const void *canvas, int cl)
{
    (void)cl;
    Canvas *c = canvas_of(canvas, "ClearContinuous");
    if (!c || c->scrolling)
        return;
    c->command_cache = 0;
    memcpy(c->source, KHDAYS_W(c->buffer), c->bytes);
    memcpy(c->base, c->source, c->bytes);
    c->op_count = 0;
    c->ops_disabled = 0;
}

/* Partial clears are not history events: NitroSystem has already erased the
 * canvas pixels. Discard the erased portions of old HD glyphs immediately.
 * Splitting a partly covered rectangle preserves surviving ink while bounding
 * replay cost by the amount of text that can actually remain on the canvas. */
static void clear_hd_rect(Canvas *c, int x, int y, int w, int h)
{
    const int left = x, top = y, right = x + w, bottom = y + h;
    const int old_count = c->op_count;
    int intersects = 0;
    size_t capacity = 0;
    for (int i = 0; i < old_count; ++i) {
        const GlyphOp *a = &c->ops[i];
        if (a->clip_l < right && a->clip_r > left && a->clip_t < bottom && a->clip_b > top) {
            intersects = 1;
            const int ix0 = a->clip_l > left ? a->clip_l : left;
            const int iy0 = a->clip_t > top ? a->clip_t : top;
            const int ix1 = a->clip_r < right ? a->clip_r : right;
            const int iy1 = a->clip_b < bottom ? a->clip_b : bottom;
            capacity += (a->clip_t < iy0) + (iy1 < a->clip_b) + (a->clip_l < ix0) + (ix1 < a->clip_r);
        } else
            ++capacity;
    }
    if (!intersects)
        return;
    if (c->editing_display)
        dirty_rect(c, left, top, right, bottom);
    int out_count = 0;
    GlyphOp *next;
    if (!capacity) {
        c->op_count = 0;
        return;
    }
    next = (GlyphOp *)malloc(capacity * sizeof(*next));
    if (!next) {
        c->ops_disabled = 1;
        c->op_count = 0;
        return;
    }
    for (int i = 0; i < old_count; ++i) {
        const GlyphOp *a = &c->ops[i];
        const int ix0 = a->clip_l > left ? a->clip_l : left;
        const int iy0 = a->clip_t > top ? a->clip_t : top;
        const int ix1 = a->clip_r < right ? a->clip_r : right;
        const int iy1 = a->clip_b < bottom ? a->clip_b : bottom;
        if (ix0 >= ix1 || iy0 >= iy1) {
            next[out_count++] = *a;
            continue;
        }
        /* Up to four non-overlapping pieces around the cleared intersection. */
        if (a->clip_t < iy0) {
            GlyphOp part = *a;
            part.clip_b = iy0;
            next[out_count++] = part;
        }
        if (iy1 < a->clip_b) {
            GlyphOp part = *a;
            part.clip_t = iy1;
            next[out_count++] = part;
        }
        if (a->clip_l < ix0) {
            GlyphOp part = *a;
            part.clip_t = iy0;
            part.clip_r = ix0;
            part.clip_b = iy1;
            next[out_count++] = part;
        }
        if (ix1 < a->clip_r) {
            GlyphOp part = *a;
            part.clip_l = ix1;
            part.clip_t = iy0;
            part.clip_b = iy1;
            next[out_count++] = part;
        }
    }
    free(c->ops);
    c->ops = next;
    c->op_count = out_count;
    c->op_room = (int)capacity;
}

/* Unattributed writes retire draws whose native storage was overwritten.
   PNG overhang is artwork, not evidence that neighbouring storage belongs to text. */
static void invalidate_draws(Canvas *c, int x, int y, int w, int h)
{
    int kept = 0;
    for (int i = 0; i < c->op_count; ++i) {
        const GlyphOp *op = &c->ops[i];
        /* Every fragment retains the complete native write footprint. */
        if (op->source_x < x + w && op->source_x + op->source_w > x && op->source_y < y + h &&
            op->source_y + op->source_h > y) {
            if (c->editing_display)
                dirty_rect(c, op->clip_l, op->clip_t, op->clip_r, op->clip_b);
        } else
            c->ops[kept++] = *op;
    }
    c->op_count = kept;
}

static uint32_t pixel_offset(const Canvas *c, int x, int y)
{
    return ((uint32_t)(y / 8) * c->param + x / 8) * (8u * c->bpp) + (y & 7) * c->bpp + (x & 7) / (8 / c->bpp);
}

static unsigned pixel(const Canvas *c, const uint8_t *data, int x, int y)
{
    const unsigned shift = c->bpp == 4 ? (x & 1) * 4 : 0;
    return (data[pixel_offset(c, x, y)] >> shift) & (c->bpp == 4 ? 15u : 255u);
}

static void put_pixel(const Canvas *c, uint8_t *data, int x, int y, unsigned value)
{
    const uint32_t off = pixel_offset(c, x, y);
    const unsigned shift = c->bpp == 4 ? (x & 1) * 4 : 0;
    const unsigned mask = (c->bpp == 4 ? 15u : 255u) << shift;
    data[off] = (uint8_t)((data[off] & ~mask) | (value << shift));
}

/* Reconcile unhooked writes as storage changes, never as font-ink tests.
   Runs limit clipping work when a menu replaces an entire buffer directly. */
static void observe_source(Canvas *c)
{
    const uint8_t *now = (const uint8_t *)KHDAYS_W(c->buffer);
    if (!memcmp(c->source, now, c->bytes))
        return;
    for (int y = 0; y < c->area_h * 8; ++y) {
        int run = -1;
        for (int x = 0; x <= c->area_w * 8; ++x) {
            const int changed = x < c->area_w * 8 && pixel(c, now, x, y) != pixel(c, c->source, x, y);
            if (changed) {
                put_pixel(c, c->base, x, y, pixel(c, now, x, y));
                if (run < 0)
                    run = x;
            } else if (run >= 0) {
                invalidate_draws(c, run, y, x - run, 1);
                run = -1;
            }
        }
    }
    memcpy(c->source, now, c->bytes);
}

/* SDK full-buffer copies retain text identity as well as bytes. Only exact,
   non-overlapping canvas copies create staging records; partial writes reconcile normally. */
unsigned int khdays_ui_text_copy_owner(const void *src_host, const void *dst_host, unsigned int size)
{
    if (size < 32 || !canvas_count || src_host == dst_host || !khdays_game_ram_ref(KHDAYS_REF(dst_host)))
        return 0;
    for (int i = 0; i < canvas_count; ++i) {
        const Canvas *c = &canvases[i];
        if (!c->detached && c->op_count && c->bytes == size && KHDAYS_W(c->buffer) == src_host) {
            const char *name;
            uint32_t offset, run;
            const KhdaysRef ref = KHDAYS_REF(dst_host);
            return khdays_origin_of_ram(ref, &name, &offset, &run) && offset <= ref ? ref - offset : ref;
        }
    }
    return 0;
}

void khdays_ui_text_memory_copy(const void *src_host, void *dst_host, unsigned int size, unsigned int owner)
{
    if (size < 32 || !canvas_count || src_host == dst_host || !khdays_game_ram_ref(KHDAYS_REF(dst_host)))
        return;
    const uintptr_t src = (uintptr_t)src_host, dst = (uintptr_t)dst_host;
    if (src + size < src || dst + size < dst || (src < dst + size && dst < src + size))
        return;
    int from = -1, to = -1;
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (c->detached)
            continue;
        if (!c->copied && ((const DsCanvas *)KHDAYS_W(c->cc))->char_base != c->buffer)
            continue;
        if ((uintptr_t)KHDAYS_W(c->buffer) == src && c->bytes == size)
            from = i;
        if ((uintptr_t)KHDAYS_W(c->buffer) == dst && c->bytes == size)
            to = i;
    }
    if (from < 0) {
        if (to >= 0 && !canvases[to].scrolling) {
            Canvas *target = &canvases[to];
            target->information = 0;
            target->notification = 0;
            target->op_count = target->ops_disabled = 0;
            memcpy(target->source, dst_host, size);
            memcpy(target->base, dst_host, size);
        }
        return;
    }
    Canvas *source = &canvases[from];
    if (source->scrolling)
        return;
    observe_source(source);
    if (tracing())
        say("canvas copy %08x -> %08x: %u bytes, %d glyph operations, target %d", source->buffer, KHDAYS_REF(dst_host),
            size, source->op_count, to);
    if (to < 0) {
        /* a copy is kept while its memory lives (khdays_ui_text_freed drops it) */
        if (!source->op_count)
            return;
        Canvas *created = new_canvas(0, KHDAYS_REF(dst_host), source->area_w, source->area_h, source->bpp,
                                     source->param, "SDK canvas copy");
        if (!created)
            return;
        created->copied = 1;
        created->copy_owner = owner ? owner : KHDAYS_REF(dst_host);
        to = canvas_count - 1;
        source = &canvases[from]; /* new_canvas can move the array. */
    }
    Canvas *target = &canvases[to];
    target->information = source->information;
    target->notification = source->notification;
    target->information_epoch = source->information_epoch;
    if (target->scrolling || target->area_w != source->area_w || target->area_h != source->area_h ||
        target->param != source->param || target->bpp != source->bpp) {
        if (!target->scrolling) {
            target->op_count = target->ops_disabled = 0;
            memcpy(target->source, dst_host, size);
            memcpy(target->base, dst_host, size);
        }
        return;
    }
    target->op_count = target->ops_disabled = 0;
    memcpy(target->source, dst_host, size);
    memcpy(target->base, source->base, size);
    if (source->ops_disabled) {
        memcpy(target->base, target->source, size);
        target->ops_disabled = 1;
        return;
    }
    for (int i = 0; i < source->op_count; ++i)
        if (!record(target, &source->ops[i])) {
            memcpy(target->base, target->source, size);
            break;
        }
}

/* A missing replacement remains exact DS artwork, including samples that
   happen to equal the previous glyph. Only the captured draw rectangle is copied. */
static void fallback_glyph(Canvas *c, const void *font, const void *glyph, int x, int y)
{
    if (!font || !glyph) {
        observe_source(c);
        return;
    }
    const uint32_t info = *(const uint32_t *)font;
    const uint32_t widths = *(const uint32_t *)glyph;
    if (!info || !widths) {
        observe_source(c);
        return;
    }
    const uint32_t table = *(const uint32_t *)((const uint8_t *)KHDAYS_W(info) + 8);
    if (!table) {
        observe_source(c);
        return;
    }
    const int w = ((const uint8_t *)KHDAYS_W(widths))[1];
    const int h = ((const uint8_t *)KHDAYS_W(table))[1];
    for (int i = 0; i < c->op_count;) {
        if (c->ops[i].source_x == x && c->ops[i].source_y == y) {
            memmove(&c->ops[i], &c->ops[i + 1], (size_t)(c->op_count - i - 1) * sizeof(*c->ops));
            --c->op_count;
        } else
            ++i;
    }
    clear_hd_rect(c, x, y, w, h);
    const uint8_t *now = (const uint8_t *)KHDAYS_W(c->buffer);
    for (int py = y > 0 ? y : 0; py < y + h && py < c->area_h * 8; ++py)
        for (int px = x > 0 ? x : 0; px < x + w && px < c->area_w * 8; ++px) {
            const unsigned value = pixel(c, now, px, py);
            put_pixel(c, c->base, px, py, value);
            put_pixel(c, c->source, px, py, value);
        }
    observe_source(c);
}

void khdays_ui_text_clear_area(const void *canvas, int cl, int x, int y, int w, int h)
{
    (void)cl;
    Canvas *c = canvas_of(canvas, "ClearAreaLine");
    if (!c || c->scrolling || w <= 0 || h <= 0)
        return;
    c->command_cache = 0;
    if (x <= 0 && y <= 0 && (int64_t)x + w >= c->area_w * 8 && (int64_t)y + h >= c->area_h * 8) {
        khdays_ui_text_clear(canvas, cl);
        return;
    }
    const uint8_t *now = (const uint8_t *)KHDAYS_W(c->buffer);
    const int r = (int64_t)x + w < c->area_w * 8 ? x + w : c->area_w * 8;
    const int b = (int64_t)y + h < c->area_h * 8 ? y + h : c->area_h * 8;
    clear_hd_rect(c, x, y, w, h);
    for (int py = y > 0 ? y : 0; py < b; ++py)
        for (int px = x > 0 ? x : 0; px < r; ++px) {
            const unsigned value = pixel(c, now, px, py);
            put_pixel(c, c->source, px, py, value);
            put_pixel(c, c->base, px, py, value);
        }
    observe_source(c);
}

/* --- Following the game's text surfaces ------------------------------------------- */

void khdays_ui_text_scroll_before(const void *window, int amount)
{
    if (amount <= 0)
        return;
    Canvas *c = canvas_of((const uint8_t *)window + RENDERER_CANVAS, "TextWindow_ScrollUp input");
    if (c) {
        observe_source(c);
        c->scrolling = 1;
    }
}

void khdays_ui_text_scroll(const void *window, int amount)
{
    Canvas *c = find(KHDAYS_REF((const uint8_t *)window + RENDERER_CANVAS));
    if (!c || !c->scrolling)
        return;
    c->scrolling = 0;
    const int height = c->area_h * 8;
    if (amount > height)
        amount = height;
    const uint8_t *now = (const uint8_t *)KHDAYS_W(c->buffer);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < c->area_w * 8; ++x)
            put_pixel(c, c->base, x, y, y + amount < height ? pixel(c, c->base, x, y + amount) : pixel(c, now, x, y));
    int kept = 0;
    for (int i = 0; i < c->op_count; ++i) {
        GlyphOp op = c->ops[i];
        op.y -= amount;
        op.source_y -= amount;
        for (int hd = 0; hd < 2; ++hd) {
            op.layouts[hd].y -= amount;
            op.layouts[hd].clip_t -= amount;
            op.layouts[hd].clip_b -= amount;
            if (op.layouts[hd].clip_t < 0)
                op.layouts[hd].clip_t = 0;
        }
        op.clip_t -= amount;
        op.clip_b -= amount;
        if (op.clip_t < 0)
            op.clip_t = 0;
        if (op.clip_t < op.clip_b)
            c->ops[kept++] = op;
    }
    c->op_count = kept;
    memcpy(c->source, now, c->bytes);
}

void khdays_ui_text_renderer(const void *renderer, int layer, const void *resource, const void *frame, int result)
{
    const DsFrame *f = (const DsFrame *)frame;
    Canvas *c = canvas_of((const uint8_t *)renderer + RENDERER_CANVAS, "TileTextRenderer_Init");
    (void)resource;
    (void)result;
    if (c != NULL && tracing()) {
        say("renderer %08x: canvas %08x for engine %c's BG%d, cells %u,%u %ux%u from tile %u, palette %u",
            KHDAYS_REF(renderer), c->cc, layer >= 4 ? 'B' : 'A', layer & 3, f->x, f->y, f->width, f->height,
            f->char_base, f->palette);
    }
}

void khdays_ui_text_renderer_gone(const void *renderer, int result)
{
    Canvas *c = find(KHDAYS_REF((const uint8_t *)renderer + RENDERER_CANVAS));
    (void)result;
    if (c != NULL) {
        if (tracing()) {
            say("renderer %08x let go (canvas %08x)", KHDAYS_REF(renderer), c->cc);
        }
        detach_or_forget(c);
    }
}

void khdays_ui_text_surface_canvas(const void *surface, int current, const void *canvas_object)
{
    (void)surface;
    (void)current;
    if (canvas_object != NULL) {
        (void)canvas_of((const uint8_t *)canvas_object + SURFACE_OBJECT_CANVAS, "TileSurface_AddCanvas");
    }
}

/* A canvas is gone once its buffer is freed: the freed structure still names its buffer, and the next
 * file read there would pass for the canvas's characters (the tutorial list's heading, read where the
 * camp menu's canvas was, drew torn). */
void khdays_ui_text_freed(const void *heap, const void *block)
{
    const KhdaysRef ref = KHDAYS_REF(block);
    (void)heap;
    for (int i = 0; i < binding_count;)
        if (ref && font_bindings[i].file == ref)
            font_bindings[i] = font_bindings[--binding_count];
        else
            ++i;
    for (int i = 0; i < canvas_count;) {
        if (ref && (canvases[i].buffer == ref || canvases[i].copy_owner == ref) && !canvases[i].detached) {
            if (tracing()) {
                say("canvas %08x let go: its buffer %08x freed", canvases[i].cc, ref);
            }
            const int retained = canvases[i].pending || canvases[i].in_vram;
            detach_or_forget(&canvases[i]);
            if (retained)
                ++i; /* still has BG VRAM characters to display */
            /* otherwise forget() moved the final entry into this slot */
        } else {
            ++i;
        }
    }
}

/* Follows a load of `size` bytes from `src` into the characters of engine
 * `engine`'s BG `bg`, at `offset` from their base (G2_GetBGnCharPtr's, as the
 * registers say now). */
/* Publish only the transferred storage range. Source edits are deliberately
   invisible until their transfer; detached canvases need only this state. */
static void publish_rect(Canvas *c, int x, int y, int w, int h)
{
    Canvas display = *c;
    display.editing_display = 1;
    display.ops = c->display_ops;
    display.op_count = c->display_count;
    display.op_room = c->display_room;
    display.ops_disabled = 0;
    clear_hd_rect(&display, x, y, w, h);
    if (!c->ops_disabled) {
        for (int i = 0; i < c->op_count; ++i) {
            GlyphOp part = c->ops[i];
            if (part.clip_l < x)
                part.clip_l = x;
            if (part.clip_t < y)
                part.clip_t = y;
            if (part.clip_r > x + w)
                part.clip_r = x + w;
            if (part.clip_b > y + h)
                part.clip_b = y + h;
            if (part.clip_l < part.clip_r && part.clip_t < part.clip_b)
                record(&display, &part);
        }
    }
    c->display_ops = display.ops;
    c->display_count = display.op_count;
    c->display_room = display.op_room;
    if (display.ops_disabled) {
        /* Allocation/fragmentation failure: restore exact DS fallback. */
        c->display_count = 0;
        memcpy(c->display_base, c->seen, c->bytes);
        memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
    }
    for (int py = y; py < y + h && py < c->area_h * 8; ++py)
        for (int px = x; px < x + w && px < c->area_w * 8; ++px)
            c->owned[(py / 8) * c->area_w + px / 8] |= UINT64_C(1) << ((py & 7) * 8 + (px & 7));
    dirty_rect(c, x, y, x + w, y + h);
}

static void publish_range(Canvas *c, uint32_t at, uint32_t size)
{
    c->display_information = c->information;
    c->display_notification = c->notification;
    c->display_information_epoch = c->information_epoch;
    const uint32_t end = size < c->bytes - at ? at + size : c->bytes;
    const uint32_t tile_bytes = 8u * c->bpp;
    if (!at && end == c->bytes) {
        memset(c->owned, 0xff, (size_t)c->area_w * c->area_h * sizeof(uint64_t));
        const int count = c->ops_disabled ? 0 : c->op_count;
        const uint8_t *graphics = c->ops_disabled ? c->source : c->base;
        int changed = count != c->display_count || memcmp(c->display_base, graphics, c->bytes) != 0;
        if (!changed && count)
            changed = memcmp(c->display_ops, c->ops, (size_t)count * sizeof(GlyphOp)) != 0;
        if (count > c->display_room) {
            GlyphOp *grown = (GlyphOp *)realloc(c->display_ops, (size_t)count * sizeof(GlyphOp));
            if (!grown) {
                c->display_count = 0;
                memcpy(c->display_base, c->source, c->bytes);
                memcpy(c->seen, c->source, c->bytes);
                memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
                ++c->version;
                return;
            }
            c->display_ops = grown;
            c->display_room = count;
        }
        if (changed) {
            for (int i = 0; i < c->display_count; ++i) {
                const GlyphOp *a = &c->display_ops[i];
                dirty_rect(c, a->clip_l, a->clip_t, a->clip_r, a->clip_b);
            }
            for (int i = 0; i < count; ++i) {
                const GlyphOp *a = &c->ops[i];
                dirty_rect(c, a->clip_l, a->clip_t, a->clip_r, a->clip_b);
            }
            for (int row = 0; row < c->area_h; ++row)
                for (int col = 0; col < c->area_w; ++col) {
                    const uint32_t off = ((uint32_t)row * c->param + col) * tile_bytes;
                    if (memcmp(c->display_base + off, graphics + off, tile_bytes))
                        c->dirty[row * c->area_w + col] |= 3;
                }
        }
        c->display_count = count;
        if (count)
            memcpy(c->display_ops, c->ops, (size_t)count * sizeof(GlyphOp));
        memcpy(c->display_base, graphics, c->bytes);
        memcpy(c->seen, c->source, c->bytes);
        if (changed)
            ++c->version;
        return;
    }
    while (at < end) {
        const uint32_t tile = at / tile_bytes;
        const int x = (int)(tile % c->param) * 8, y = (int)(tile / c->param) * 8;
        const uint32_t local = at % tile_bytes;
        uint32_t n;
        int rx, ry, rw, rh;
        if (!local && end - at >= tile_bytes) {
            n = (c->param - tile % c->param) * tile_bytes;
            if (n > end - at)
                n = (end - at) / tile_bytes * tile_bytes;
            rx = x;
            ry = y;
            rw = (int)(n / tile_bytes) * 8;
            rh = 8;
        } else {
            n = c->bpp - local % c->bpp;
            if (n > end - at)
                n = end - at;
            rx = x + (int)(local % c->bpp) * (8 / c->bpp);
            ry = y + (int)(local / c->bpp);
            rw = (int)n * (8 / c->bpp);
            rh = 1;
        }
        memcpy(c->display_base + at, (c->ops_disabled ? c->source : c->base) + at, n);
        memcpy(c->seen + at, c->source + at, n);
        if (x < c->area_w * 8)
            publish_rect(c, rx, ry, rw, rh);
        at += n;
    }
    ++c->version;
}

static void external_range(Canvas *c, uint32_t to, uint32_t size, int relinquish)
{
    obtained_written(c->engine, to, size);
    const uint32_t first = to > c->vram ? to - c->vram : 0;
    const uint32_t end = (uint64_t)to + size < (uint64_t)c->vram + c->bytes ? to + size - c->vram : c->bytes;
    const uint32_t tile_bytes = 8u * c->bpp;
    Canvas display = *c;
    display.editing_display = 1;
    display.ops = c->display_ops;
    display.op_count = c->display_count;
    display.op_room = c->display_room;
    display.ops_disabled = 0;
    for (uint32_t at = first; at < end;) {
        const uint32_t tile = at / tile_bytes;
        const int x = (int)(tile % c->param) * 8;
        const int y = (int)(tile / c->param) * 8;
        const uint32_t local = at % tile_bytes;
        uint32_t n = c->bpp - local % c->bpp;
        if (n > end - at)
            n = end - at;
        const int px = x + (int)(local % c->bpp) * (8 / c->bpp);
        const int py = y + (int)(local / c->bpp);
        const int w = (int)n * (8 / c->bpp);
        if (x < c->area_w * 8) {
            clear_hd_rect(&display, px, py, w, 1);
            dirty_rect(c, px, py, px + w, py + 1);
            if (relinquish) {
                for (int i = 0; i < w; ++i)
                    c->owned[(y / 8) * c->area_w + x / 8] &= ~(UINT64_C(1) << ((py & 7) * 8 + ((px + i) & 7)));
            }
        }
        for (uint32_t i = 0; i < n; ++i) {
            const uint8_t *now = khdays_bg_memory(c->engine, c->vram + at + i);
            if (now)
                c->display_base[at + i] = c->seen[at + i] = *now;
        }
        at += n;
    }
    c->display_ops = display.ops;
    c->display_count = display.op_count;
    c->display_room = display.op_room;
    if (display.ops_disabled) {
        c->display_count = 0;
        memcpy(c->display_base, c->seen, c->bytes);
        memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
    }
    ++c->version;
}

/* SDK fills are explicit write operations, including identical-byte clears.
   Source clears publish later; fills of mapped VRAM take effect immediately. */
void khdays_ui_text_memory_clear(const void *destination, unsigned int size)
{
    if (!size || !canvas_count)
        return;
    const uintptr_t first = (uintptr_t)destination, end = first + size;
    if (end < first)
        return;
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (!c->detached && !c->scrolling) {
            const uintptr_t buffer = (uintptr_t)KHDAYS_W(c->buffer);
            if (first < buffer + c->bytes && buffer < end) {
                c->command_cache = 0;
                const uint32_t at = first > buffer ? (uint32_t)(first - buffer) : 0;
                const uint32_t last = end < buffer + c->bytes ? (uint32_t)(end - buffer) : c->bytes;
                memcpy(c->source + at, (const void *)(buffer + at), last - at);
                memcpy(c->base + at, c->source + at, last - at);
                if (!at && last == c->bytes) {
                    c->op_count = 0;
                    c->ops_disabled = 0;
                } else {
                    for (uint32_t off = at; off < last;) {
                        const uint32_t tile = off / (8u * c->bpp);
                        const uint32_t local = off % (8u * c->bpp);
                        uint32_t n = c->bpp - local % c->bpp;
                        if (n > last - off)
                            n = last - off;
                        const int x = (int)(tile % c->param) * 8 + (int)(local % c->bpp) * (8 / c->bpp);
                        const int y = (int)(tile / c->param) * 8 + (int)(local / c->bpp);
                        clear_hd_rect(c, x, y, (int)n * (8 / c->bpp), 1);
                        off += n;
                    }
                }
            }
        }
        if (!(c->pending || c->in_vram))
            continue;
        for (uint32_t off = 0; off < c->bytes;) {
            uint32_t n = KHDAYS_VRAM_PAGE - (c->vram + off) % KHDAYS_VRAM_PAGE;
            if (n > c->bytes - off)
                n = c->bytes - off;
            const uint8_t *page = khdays_bg_memory(c->engine, c->vram + off);
            if (page) {
                const uintptr_t address = (uintptr_t)page;
                if (first < address + n && address < end) {
                    const uint32_t start = first > address ? (uint32_t)(first - address) : 0;
                    const uint32_t last = end < address + n ? (uint32_t)(end - address) : n;
                    external_range(c, c->vram + off + start, last - start, 0);
                }
            }
            off += n;
        }
    }
}

/* A first partial transfer must preserve the native pixels in the rest of
   its cells. Claim their graphics only where no existing canvas owns them. */
static void initial_display(Canvas *c, int engine, uint32_t address)
{
    c->display_count = 0;
    memset(c->owned, 0xff, (size_t)c->area_w * c->area_h * sizeof(uint64_t));
    for (uint32_t at = 0; at < c->bytes; ++at) {
        const uint8_t *p = khdays_bg_memory(engine, address + at);
        c->seen[at] = c->display_base[at] = p ? *p : 0;
    }
    for (int i = 0; i < canvas_count; ++i) {
        const Canvas *other = &canvases[i];
        if (other == c || !(other->pending || other->in_vram) || other->engine != engine || other->bpp != c->bpp)
            continue;
        for (int row = 0; row < other->area_h; ++row) {
            for (int col = 0; col < other->area_w; ++col) {
                const uint64_t mask = other->owned[row * other->area_w + col];
                if (!mask)
                    continue;
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        if (!(mask & (UINT64_C(1) << (y * 8 + x))))
                            continue;
                        const uint32_t byte = other->vram + pixel_offset(other, col * 8 + x, row * 8 + y);
                        if (byte < address || byte - address >= c->bytes)
                            continue;
                        const uint32_t at = byte - address;
                        const uint32_t tile = at / (8u * c->bpp);
                        const int cx = (int)(tile % c->param);
                        const int cy = (int)(tile / c->param);
                        if (cx >= c->area_w)
                            continue;
                        const int px = (int)(at % c->bpp) * (8 / c->bpp) + (c->bpp == 4 ? x & 1 : 0);
                        const int py = (int)((at % (8u * c->bpp)) / c->bpp);
                        c->owned[cy * c->area_w + cx] &= ~(UINT64_C(1) << (py * 8 + px));
                    }
                }
            }
        }
    }
    memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
}

static void chars_loaded(int engine, int bg, const void *src_host, uint32_t offset, uint32_t size)
{
    const KhdaysRef src = KHDAYS_REF(src_host);
    const uint32_t to = khdays_bg_chars_base(engine, bg) + offset; /* where the load wrote */
    obtained_written(engine, to, size);
    Canvas *mine = NULL;
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (mine == NULL && !c->detached && src >= c->buffer && src < c->buffer + c->bytes &&
            (c->copied || ((const DsCanvas *)KHDAYS_W(c->cc))->char_base == c->buffer)) {
            mine = c; /* (a canvas that was let go, whose memory is used again, no longer names it) */
        }
    }
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (c != mine && (c->in_vram || c->pending) && c->engine == engine && to < c->vram + c->bytes &&
            c->vram < to + size) {
            if (tracing()) {
                say("canvas %08x's characters (engine %c's BG memory +%u) written over by a load from %08x", c->cc,
                    engine ? 'B' : 'A', c->vram, src);
            }
            if (to <= c->vram && (uint64_t)to + size >= (uint64_t)c->vram + c->bytes)
                c->in_vram = c->pending = 0;
            else
                external_range(c, to, size, mine != NULL);
        }
    }
    if (mine != NULL) {
        observe_source(mine);
        if (engine == 0 && mine->command_cache && obtained.bound && to <= obtained.address &&
            (uint64_t)to + size >= obtained.address + 512)
            obtained.source_valid = 1;
        const uint32_t at = src - mine->buffer;
        if (tracing() &&
            (!(mine->in_vram || mine->pending) || mine->engine != engine || mine->bg != bg || mine->vram != to - at)) {
            say("canvas %08x loaded: engine %c's BG%d characters, from +%u, %u bytes, to +%u (BG memory +%u)", mine->cc,
                engine ? 'B' : 'A', bg, at, size, offset, to);
        }
        if (mine->engine != engine || mine->vram != to - at || (!mine->pending && !mine->in_vram)) {
            initial_display(mine, engine, to - at);
        }
        mine->engine = engine;
        mine->bg = bg;
        mine->vram = to - at;
        publish_range(mine, at, size);
        mine->pending = 1;
    }
}

void khdays_ui_text_chars_a0(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(0, 0, src, offset, size);
}
void khdays_ui_text_chars_a1(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(0, 1, src, offset, size);
}
void khdays_ui_text_chars_a2(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(0, 2, src, offset, size);
}
void khdays_ui_text_chars_a3(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(0, 3, src, offset, size);
}
void khdays_ui_text_chars_b0(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(1, 0, src, offset, size);
}
void khdays_ui_text_chars_b1(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(1, 1, src, offset, size);
}
void khdays_ui_text_chars_b2(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(1, 2, src, offset, size);
}
void khdays_ui_text_chars_b3(const void *src, unsigned int offset, unsigned int size)
{
    chars_loaded(1, 3, src, offset, size);
}

/* A load into the sprites' characters: only reported for now (a canvas shown
 * by sprites is not an element yet). */
static void obj_loaded(int engine, const void *src_host, uint32_t offset, uint32_t size)
{
    const KhdaysRef src = KHDAYS_REF(src_host);
    if (!tracing()) {
        return;
    }
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (src >= c->buffer && src < c->buffer + c->bytes) {
            if (!c->said_obj) {
                say("canvas %08x loaded into engine %c's sprite characters: from +%u, %u bytes, to +%u", c->cc,
                    engine ? 'B' : 'A', src - c->buffer, size, offset);
                c->said_obj = 1;
            }
            return;
        }
    }
}

void khdays_ui_text_obj_a(const void *src, uint32_t offset, uint32_t size)
{
    obj_loaded(0, src, offset, size);
}
void khdays_ui_text_obj_b(const void *src, uint32_t offset, uint32_t size)
{
    obj_loaded(1, src, offset, size);
}

/* --- Published text and graphics in BG character memory ------------------------- */

/* Unknown VRAM writes change graphics and text lifetime at the written pixels.
   This compares storage snapshots, independent of any font's bitmap or alpha. */
static void observe_vram(Canvas *c)
{
    Canvas display = *c;
    display.editing_display = 1;
    display.ops = c->display_ops;
    display.op_count = c->display_count;
    display.op_room = c->display_room;
    display.ops_disabled = 0;
    int changed = 0;
    for (uint32_t at = 0; at < c->bytes; ++at) {
        if (!at || !((c->vram + at) % KHDAYS_VRAM_PAGE)) {
            uint32_t n = KHDAYS_VRAM_PAGE - (c->vram + at) % KHDAYS_VRAM_PAGE;
            if (n > c->bytes - at)
                n = c->bytes - at;
            const uint8_t *page = khdays_bg_memory(c->engine, c->vram + at);
            if (!page || !memcmp(page, c->seen + at, n)) {
                at += n - 1;
                continue;
            }
        }
        const uint8_t *now = khdays_bg_memory(c->engine, c->vram + at);
        if (!now || *now == c->seen[at])
            continue;
        obtained_written(c->engine, c->vram + at, 1);
        const uint32_t tile = at / (8u * c->bpp);
        const int x = (int)(tile % c->param) * 8 + (int)(at % c->bpp) * (8 / c->bpp);
        const int y = (int)(tile / c->param) * 8 + (int)((at % (8u * c->bpp)) / c->bpp);
        for (int n = 0; n < 8 / c->bpp; ++n) {
            const unsigned mask = c->bpp == 4 ? 15u : 255u;
            const unsigned value = (*now >> (n * c->bpp)) & mask;
            if (value != ((c->seen[at] >> (n * c->bpp)) & mask) && x + n < c->area_w * 8) {
                invalidate_draws(&display, x + n, y, 1, 1);
                put_pixel(c, c->display_base, x + n, y, value);
                dirty_rect(c, x + n, y, x + n + 1, y + 1);
            }
        }
        c->seen[at] = *now;
        changed = 1;
    }
    c->display_ops = display.ops;
    c->display_count = display.op_count;
    c->display_room = display.op_room;
    if (display.ops_disabled) {
        c->display_count = 0;
        memcpy(c->display_base, c->seen, c->bytes);
        memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
    }
    if (changed)
        ++c->version;
}

static uint32_t glyph_colour(const Canvas *c, const GlyphOp *op, uint16_t cell)
{
    const uint32_t index =
        c->bpp == 4 ? ((cell >> 12) * 16u + ((uint32_t)op->cl & 15u))
                    : (KET_EXTENDED | ((uint32_t)(cell >> 12) << KET_EXT_PAL_SHIFT) | ((uint32_t)op->cl & 255u));
    return khdays_ui_palette_colour(c->engine, (uint32_t)c->bg, KET_OPAQUE | index);
}

/* Ownership is at DS pixel resolution; clear each missing pixel's output block once. */
static void mask_tile(uint32_t *out, int stride, int scale, uint16_t cell, uint64_t owned)
{
    if (owned == UINT64_MAX)
        return;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int sx = cell & 0x400u ? 7 - x : x;
            const int sy = cell & 0x800u ? 7 - y : y;
            if (owned & (UINT64_C(1) << (sy * 8 + sx)))
                continue;
            for (int dy = 0; dy < scale; ++dy)
                memset(out + (size_t)(y * scale + dy) * stride + x * scale, 0, (size_t)scale * sizeof(*out));
        }
    }
}

/* Only mappings accepted by the ordinary replacement system can supply artwork. */
static const KhdaysTextureReplacement *graphics_sheet(uint32_t word)
{
    const KhdaysTextureReplacement *r = word ? khdays_textures_replacement((int)(word >> 20) - 1) : NULL;
    return r && r->rgba && (r->info & KHDAYS_REPLACEMENT_TILES) && ((r->info >> 8) & 255u) && (r->info >> 16) ? r
                                                                                                              : NULL;
}

static void observe_graphics(Canvas *c)
{
    c->graphics_count = 0;
    for (int row = 0; row < c->area_h; ++row) {
        for (int col = 0; col < c->area_w; ++col) {
            const int tile = row * c->area_w + col;
            const uint32_t at = c->vram + (uint32_t)(row * c->param + col) * 8u * c->bpp;
            uint32_t word = khdays_tiles2d_bg_tile(c->engine, at);
            if (word != c->graphics[tile] && !graphics_sheet(word))
                word = 0;
            if (word != c->graphics[tile]) {
                c->graphics[tile] = word;
                c->dirty[tile] |= 3;
                ++c->version;
            }
            c->graphics_count += word != 0;
        }
    }
}

/* Straight-alpha foreground over background, preserving both PNGs' alpha. */
static uint32_t over_graphics(uint32_t foreground, uint32_t background)
{
    const uint32_t a = foreground >> 24, b = background >> 24;
    if (a == 255 || !b)
        return foreground;
    if (!a)
        return background;
    const uint32_t alpha = a * 255 + b * (255 - a);
    uint32_t result = ((alpha + 127) / 255) << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t colour =
            ((foreground >> shift) & 255u) * a * 255 + ((background >> shift) & 255u) * b * (255 - a);
        result |= ((colour + alpha / 2) / alpha) << shift;
    }
    return result;
}

/* Graphics share canvas ownership, so overlapping canvases cannot double-blend a PNG. */
static void underlay_graphics(const Canvas *c, int tile, uint16_t cell, int scale, uint32_t *out, int stride)
{
    const uint32_t word = c->graphics[tile];
    const KhdaysTextureReplacement *r = graphics_sheet(word);
    if (!r)
        return;
    const int rs = (int)((r->info >> 8) & 255u), columns = (int)(r->info >> 16), n = 8 * scale;
    const uint32_t index = word & 0xfffffu;
    const int x0 = (int)(index % columns) * 8 * rs, y0 = (int)(index / columns) * 8 * rs;
    if (!(r->info & KHDAYS_REPLACEMENT_INDEXED) && !(cell & 0xc00u) && rs == scale && c->owned[tile] == UINT64_MAX &&
        x0 + n <= r->width && y0 + n <= r->height) {
        for (int y = 0; y < n; ++y) {
            const uint32_t *source = r->rgba + (size_t)(y0 + y) * r->width + x0;
            uint32_t *dest = out + (size_t)y * stride;
            if (!c->display_count) {
                for (int x = 0; x < n; ++x)
                    dest[x] = (source[x] >> 24) ? source[x] : 0;
            } else {
                for (int x = 0; x < n; ++x)
                    dest[x] = over_graphics(dest[x], source[x]);
            }
        }
        return;
    }
    uint32_t colours[256] = {0};
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const int ox = cell & 0x400u ? 7 - x / scale : x / scale;
            const int oy = cell & 0x800u ? 7 - y / scale : y / scale;
            if (!(c->owned[tile] & (UINT64_C(1) << (oy * 8 + ox))))
                continue;
            const int sx = (2 * x + 1) * rs / (2 * scale), sy = (2 * y + 1) * rs / (2 * scale);
            const int ux = x0 + (cell & 0x400u ? 8 * rs - 1 - sx : sx);
            const int uy = y0 + (cell & 0x800u ? 8 * rs - 1 - sy : sy);
            uint32_t colour = ux < r->width && uy < r->height ? r->rgba[(size_t)uy * r->width + ux] : 0;
            if (r->info & KHDAYS_REPLACEMENT_INDEXED) {
                const uint32_t v = colour & 255u, pal = (uint32_t)cell >> 12;
                if (v && !colours[v])
                    colours[v] = khdays_ui_palette_colour(c->engine, (uint32_t)c->bg,
                                                          c->bpp == 4 ? KET_OPAQUE | pal * 16u | (v & 15u)
                                                                      : KET_OPAQUE | KET_EXTENDED |
                                                                            pal << KET_EXT_PAL_SHIFT | v);
                colour = colours[v];
            }
            uint32_t *pixel = out + (size_t)y * stride + x;
            *pixel = over_graphics(*pixel, colour);
        }
    }
}

static double painted_width(const GlyphOp *op)
{
    return op->paint_w * (op->x_scale > 0 ? op->x_scale : 1);
}

/* The icon of the player's input for the DS button `op` draws (its first glyph: over all of
   START's), as large as the glyph's cell allows, centred, within DS pixels [l, r) x [t, b). Drawn a
   pixel down and right of the same button (the game's shadow pass), its shape in the shadow's
   colour. 0 when there is no icon: the glyph itself is drawn. */
static int paint_button(const Canvas *c, const GlyphOp *op, uint16_t cell, double x, double y, uint32_t *out, int width,
                        int height, int scale, int l, int t, int r, int b)
{
    int iw = 0, ih = 0, button = op->button;
    /* the lock-on tutorial's R with another just below it (in every language, before "press it
       twice to leave") is the one that cycles the targets; the shadow pass's a pixel below is not */
    for (int i = 0; button == KHDAYS_BUTTON_R && op->field && i < c->display_count; ++i) {
        const GlyphOp *o = &c->display_ops[i];
        if (o->button == KHDAYS_BUTTON_R && o->field && o->source_y - op->source_y >= 8 &&
            o->source_y - op->source_y <= 40)
            button = KHDAYS_BUTTON_TARGET_NEXT;
    }
    const uint8_t *icon = khdays_button_icon(button, op->field, &iw, &ih);
    if (!icon || iw <= 0 || ih <= 0)
        return 0;
    if (op->piece > 0)
        return 1; /* the first glyph's icon covers it */
    int shadow = 0;
    for (int i = 0; i < c->display_count && !shadow; ++i) {
        const GlyphOp *o = &c->display_ops[i];
        shadow = o->button == op->button && o->piece == 0 && o->source_x == op->source_x - 1 &&
                 o->source_y == op->source_y - 1;
    }
    const uint32_t ink = shadow ? glyph_colour(c, op, cell) : 0;
    double fx, fy, texel;
    khdays_button_fit(painted_width(op), op->h, iw, ih, &fx, &fy, &texel);
    const double ox = x + fx, oy = y + fy;
    /* a few samples a pixel where the icon is shrunk, so its edges stay smooth */
    const int n = texel * scale < 1 ? (int)ceil(1 / (texel * scale)) < 4 ? (int)ceil(1 / (texel * scale)) : 4 : 1;
    for (int py = t * scale; py < b * scale && py < height; ++py) {
        for (int px = l * scale; px < r * scale && px < width; ++px) {
            double sum[4] = {0, 0, 0, 0}; /* premultiplied red, green, blue, and alpha */
            for (int s = 0; s < n * n; ++s) {
                const double u = ((px + (s % n + .5) / n) / scale - ox) / texel - .5;
                const double v = ((py + (s / n + .5) / n) / scale - oy) / texel - .5;
                const int u0 = (int)floor(u), v0 = (int)floor(v);
                for (int k = 0; k < 4; ++k) {
                    const int iu = u0 + (k & 1), iv = v0 + (k >> 1);
                    const double w = (k & 1 ? u - u0 : 1 - (u - u0)) * (k >> 1 ? v - v0 : 1 - (v - v0));
                    if (iu < 0 || iv < 0 || iu >= iw || iv >= ih || w <= 0)
                        continue;
                    const uint8_t *p = icon + ((size_t)iv * iw + iu) * 4;
                    const double a = p[3] * w;
                    sum[0] += p[0] * a;
                    sum[1] += p[1] * a;
                    sum[2] += p[2] * a;
                    sum[3] += a;
                }
            }
            const double alpha = sum[3] / (n * n);
            if (alpha < .5)
                continue;
            uint32_t colour;
            if (shadow)
                colour = (ink & 0xffffffu) | (uint32_t)(alpha * (ink >> 24) / 255 + .5) << 24;
            else
                colour = (uint32_t)(sum[0] / sum[3] + .5) | (uint32_t)(sum[1] / sum[3] + .5) << 8 |
                         (uint32_t)(sum[2] / sum[3] + .5) << 16 | (uint32_t)(alpha + .5) << 24;
            uint32_t *pixel = out + (size_t)py * width + px;
            *pixel = over_graphics(colour, *pixel);
        }
    }
    return 1;
}

static void paint_op(const Canvas *c, const GlyphOp *op, uint16_t cell, double x, double y, uint32_t *out, int width,
                     int height, int scale, int l, int t, int r, int b)
{
    /* Sequence text still needs the native font when only font replacement is off. */
    if (!font_display_enabled() && !op->native_glyph && !op->native_saved) {
        for (int py = t * scale; py < b * scale && py < height; ++py) {
            const int sy = (int)floor((py + .5) / scale - y) + op->source_y;
            for (int px = l * scale; px < r * scale && px < width; ++px) {
                const int sx = (int)floor(((px + .5) / scale - x) / (op->x_scale > 0 ? op->x_scale : 1)) + op->source_x;
                if (sx < op->source_x || sy < op->source_y || sx >= op->source_x + op->source_w ||
                    sy >= op->source_y + op->source_h || sx < 0 || sy < 0 || sx >= c->area_w * 8 || sy >= c->area_h * 8)
                    continue;
                const unsigned value = pixel(c, c->seen, sx, sy);
                if (!value)
                    continue;
                const unsigned pal = (unsigned)cell >> 12;
                const unsigned ink = c->bpp == 4 ? KET_OPAQUE | pal * 16u | value
                                                 : KET_OPAQUE | KET_EXTENDED | pal << KET_EXT_PAL_SHIFT | value;
                uint32_t *dest = out + (size_t)py * width + px;
                *dest = over_graphics(khdays_ui_palette_colour(c->engine, (uint32_t)c->bg, ink), *dest);
            }
        }
        return;
    }
    if (op->button && paint_button(c, op, cell, x, y, out, width, height, scale, l, t, r, b))
        return;
    const double factor = op->x_scale > 0 ? op->x_scale : 1;
    if (!op->native_glyph && font_display_enabled()) {
        if (factor == 1)
            khdays_hd_font_paint_clipped(op->resource, op->glyph, x, y, op->w, op->h, glyph_colour(c, op, cell), out,
                                         width, height, scale, l, t, r, b);
        else
            khdays_hd_font_paint_fitted(op->resource, op->glyph, x, y, op->w, op->h, glyph_colour(c, op, cell), out,
                                        width, height, scale, l, t, r, b, factor);
        return;
    }
    uint32_t colours[256] = {0};
    const int bpp = op->native_saved;
    const unsigned char *pixels = op->native_pixels;
    for (int py = t * scale; py < b * scale && py < height; ++py) {
        const int sy = (int)floor((py + .5) / scale - y);
        if (sy < 0 || sy >= op->h)
            continue;
        for (int px = l * scale; px < r * scale && px < width; ++px) {
            const int sx = (int)floor(((px + .5) / scale - x) / factor);
            if (sx < 0 || sx >= op->w)
                continue;
            const int at = ((sy / 8) * 2 + sx / 8) * (8 * bpp) + (sy & 7) * bpp + (sx & 7) / (8 / bpp);
            const unsigned value = bpp == 4 ? (pixels[at] >> ((sx & 1) * 4)) & 15u : pixels[at];
            if (!value)
                continue;
            if (!colours[value]) {
                const unsigned pal = (unsigned)cell >> 12;
                const unsigned ink = bpp == 4 ? KET_OPAQUE | pal * 16u | value
                                              : KET_OPAQUE | KET_EXTENDED | pal << KET_EXT_PAL_SHIFT | value;
                colours[value] = khdays_ui_palette_colour(c->engine, (uint32_t)c->bg, ink);
            }
            out[(size_t)py * width + px] = over_graphics(colours[value], out[(size_t)py * width + px]);
        }
    }
}

static int primary_graphics_visible(const Canvas *c)
{
    if (c->graphics_count)
        for (int tile = 0; tile < c->area_w * c->area_h; ++tile)
            if (c->graphics[tile] && c->cells[tile] != 0xffffu)
                return 1;
    return 0;
}

/* Rasterize dirty tiles only, with the map's palette, visibility and flips.
   The immutable glyph cache remains independent of canvas and font backend. */
static int primary_text_visible(const Canvas *c)
{
    for (int i = 0; i < c->display_count; ++i) {
        const GlyphOp *op = &c->display_ops[i];
        int right = op->clip_r;
        const int ink_right = (int)ceil(op->x + painted_width(op));
        if (right > ink_right)
            right = ink_right;
        if (op->paint_w <= 0 || op->clip_l >= right || op->clip_t >= op->clip_b)
            continue;
        const int left = op->clip_l < 0 ? 0 : op->clip_l / 8;
        const int top = op->clip_t < 0 ? 0 : op->clip_t / 8;
        const int col_end = (right + 7) / 8 < c->area_w ? (right + 7) / 8 : c->area_w;
        const int row_end = (op->clip_b + 7) / 8 < c->area_h ? (op->clip_b + 7) / 8 : c->area_h;
        for (int row = top; row < row_end; ++row)
            for (int col = left; col < col_end; ++col)
                if (c->cells[row * c->area_w + col] != 0xffffu)
                    return 1;
    }
    return 0;
}

/* When what the button icons show changes (the device played, the bindings), the tiles of the
   buttons' glyphs are drawn again. */
static void refresh_buttons(Canvas *c)
{
    int any = 0;
    for (int i = 0; i < c->display_count && !any; ++i)
        any = c->display_ops[i].button != KHDAYS_BUTTON_NONE;
    const uint32_t signature = any ? khdays_button_icons_signature() : c->button_signature;
    if (signature == c->button_signature)
        return;
    c->button_signature = signature;
    for (int i = 0; i < c->display_count; ++i)
        if (c->display_ops[i].button != KHDAYS_BUTTON_NONE)
            dirty_op(c, &c->display_ops[i]);
}

static void build_hd(Canvas *c, int scale, uint32_t palette)
{
    refresh_buttons(c);
    palette = (palette ^ (uint32_t)c->bg) * 16777619u;
    const int profile = profiling();
    const clock_t started = profile ? clock() : 0;
    int tiles = 0, paints = 0;
    const int text = hd_enabled() && c->display_count;
    const int graphics = primary_graphics_visible(c);
    if (!text && !graphics) {
        khdays_ui_image_put(c->hd_image);
        c->hd_image = -1;
        c->hd_ready = 0;
        return;
    }
    if (!graphics && !primary_text_visible(c)) {
        khdays_ui_image_put(c->hd_image);
        c->hd_image = -1;
        c->hd_ready = 1;
        for (int tile = 0; tile < c->area_w * c->area_h; ++tile)
            c->dirty[tile] &= 0xfeu;
        return;
    }
    const int width = c->area_w * 8 * scale, height = c->area_h * 8 * scale;
    if (c->hd_image < 0 || c->hd_scale != scale) {
        khdays_ui_image_put(c->hd_image);
        c->hd_image = khdays_ui_image_get(width, height);
        c->hd_scale = scale;
        memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
    }
    c->hd_ready = c->hd_image >= 0;
    if (!c->hd_ready)
        return;
    uint32_t *out = NULL;
    for (int row = 0; row < c->area_h; ++row) {
        for (int col = 0; col < c->area_w; ++col) {
            const int tile = row * c->area_w + col;
            if (!(c->dirty[tile] & 1) && c->hd_palette == palette)
                continue;
            const uint16_t cell = c->cells[tile];
            const int l = col * 8, t = row * 8, r = l + 8, b = t + 8;
            uint32_t hash = (cell ^ c->graphics[tile]) * 16777619u;
            const KhdaysTextureReplacement *sheet = graphics_sheet(c->graphics[tile]);
            if (sheet && (sheet->info & KHDAYS_REPLACEMENT_INDEXED))
                hash = (hash ^ palette) * 16777619u;
            if (cell != 0xffffu) {
                for (int i = 0; text && i < c->display_count; ++i) {
                    const GlyphOp *op = &c->display_ops[i];
                    if (op->clip_l < r && op->clip_r > l && op->clip_t < b && op->clip_b > t)
                        hash = (hash ^ glyph_colour(c, op, cell)) * 16777619u;
                }
            }
            if (!(c->dirty[tile] & 1) && c->tile_hash[tile] == hash)
                continue;
            ++tiles;
            if (!out)
                out = khdays_ui_image_begin(c->hd_image);
            if (!out)
                return;
            for (int y = t * scale; y < b * scale; ++y)
                memset(out + (size_t)y * width + l * scale, 0, (size_t)8 * scale * sizeof(*out));
            if (cell == 0xffffu) {
                c->dirty[tile] &= 0xfeu;
                c->tile_hash[tile] = hash;
                continue;
            }
            if (cell != 0xffffu) {
                for (int i = 0; text && i < c->display_count; ++i) {
                    const GlyphOp *op = &c->display_ops[i];
                    const int cl = op->clip_l > l ? op->clip_l : l;
                    const int ct = op->clip_t > t ? op->clip_t : t;
                    int cr = op->clip_r < r ? op->clip_r : r;
                    if (cr > op->x + painted_width(op))
                        cr = (int)ceil(op->x + painted_width(op));
                    const int cb = op->clip_b < b ? op->clip_b : b;
                    if (cl >= cr || ct >= cb)
                        continue;
                    paint_op(c, op, cell, op->x, op->y, out, width, height, scale, cl, ct, cr, cb);
                    ++paints;
                }
                /* Flip the finished tile, including transparent PNG padding. */
                const int n = 8 * scale;
                if (cell & 0xc00u)
                    for (int y = 0; y < n; ++y) {
                        for (int x = 0; x < n; ++x) {
                            const int fx = cell & 0x400u ? n - 1 - x : x;
                            const int fy = cell & 0x800u ? n - 1 - y : y;
                            if (fy * n + fx <= y * n + x)
                                continue;
                            uint32_t *a = out + (size_t)(t * scale + y) * width + l * scale + x;
                            uint32_t *z = out + (size_t)(t * scale + fy) * width + l * scale + fx;
                            const uint32_t v = *a;
                            *a = *z;
                            *z = v;
                        }
                    }
            }
            mask_tile(out + (size_t)t * scale * width + l * scale, width, scale, cell, c->owned[tile]);
            underlay_graphics(c, tile, cell, scale, out + (size_t)t * scale * width + l * scale, width);
            c->dirty[tile] &= 0xfeu;
            c->tile_hash[tile] = hash;
        }
    }
    if (out)
        khdays_ui_image_end(c->hd_image);
    c->hd_palette = palette;
    if (profile && clock() - started >= CLOCKS_PER_SEC / 500)
        khdays_log("text raster timing: canvas %08x, %.1f ms, image %dx%d, %d dirty tiles, %d paints, %d glyphs", c->cc,
                   (clock() - started) * 1000.0 / CLOCKS_PER_SEC, width, height, tiles, paints, c->display_count);
}

/* The canvas's character that the map's `entry` shows, as its index among the
 * canvas's characters, or -1 if it is not one of the canvas's. */
static int canvas_char(const Canvas *c, const KhdaysBgLayer *l, uint32_t entry)
{
    const uint32_t char_size = (uint32_t)(8 * c->bpp);
    const uint32_t at = l->chars + (entry & 0x3ffu) * char_size;
    if (at < c->vram || at >= c->vram + c->bytes || !khdays_bg_memory(c->engine, at)) {
        return -1;
    }
    const int k = (int)((at - c->vram) / char_size);
    const int col = k % c->param, row = k / c->param;
    return col < c->area_w && c->owned[row * c->area_w + col] ? k : -1;
}

/* The primary translation preserves the canvas's existing layout anchor.
   Other visible cells share one bounded image, regardless of their translations. */
static int shown_at(const Canvas *c, const KhdaysBgLayer *l, int *ox, int *oy)
{
    uint16_t votes[64 * 64] = {0};
    int best = 0;
    const int x0 = l->hofs >> 3, y0 = l->vofs >> 3;
    const int x1 = (l->hofs + KHDAYS_SCREEN_W - 1) >> 3, y1 = (l->vofs + KHDAYS_SCREEN_H - 1) >> 3;
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            const int k = canvas_char(c, l, khdays_bg_map_entry(c->engine, l, cx, cy));
            if (k < 0)
                continue;
            const int x = ((cx - k % c->param) % l->wt + l->wt) % l->wt;
            const int y = ((cy - k / c->param) % l->ht + l->ht) % l->ht;
            const int n = ++votes[y * 64 + x];
            if (n > best) {
                best = n;
                *ox = x;
                *oy = y;
            }
        }
    }
    return best != 0;
}

/* BG character memory is shared. An upload entry point does not identify
   the layer whose tilemap actually displays those characters. */
static int display_layer(Canvas *c, KhdaysBgLayer *l, int *ox, int *oy)
{
    const int uploaded = c->bg;
    for (int attempt = -1; attempt < 4; ++attempt) {
        const int bg = attempt < 0 ? uploaded : attempt;
        if (attempt >= 0 && bg == uploaded)
            continue;
        if (!khdays_bg_text_layer(c->engine, bg, l) || l->colors256 != (c->bpp == 8))
            continue;
        int caption_x, caption_y;
        if (c->display_information && khdays_ui_information_wants() && !c->engine && bg == 1 &&
            khdays_ui_information_text_scroll(&caption_x, &caption_y)) {
            l->hofs = caption_x;
            l->vofs = caption_y;
        }
        if (!shown_at(c, l, ox, oy))
            continue;
        if (tracing() && bg != uploaded)
            say("canvas %08x displayed on engine %c BG%d via shared character memory (upload BG%d)", c->cc,
                c->engine ? 'B' : 'A', bg, uploaded);
        c->bg = bg;
        return 1;
    }
    return 0;
}

/* Paint one mapped tile in tile-local coordinates, including PNG-only artwork. */
static void mapped_tile(const Canvas *c, int k, uint16_t cell, int scale, int hd, int hide_ds, uint32_t *out)
{
    const int col = k % c->param, row = k / c->param;
    const int l = col * 8, t = row * 8, n = 8 * scale;
    const int tile = row * c->area_w + col;
    memset(out, 0, (size_t)n * n * sizeof(uint32_t));
    if (hd) {
        for (int i = 0; hd_enabled() && i < c->display_count; ++i) {
            const GlyphOp *op = &c->display_ops[i];
            const int cl = op->clip_l > l ? op->clip_l - l : 0;
            const int ct = op->clip_t > t ? op->clip_t - t : 0;
            int cr = op->clip_r < l + 8 ? op->clip_r - l : 8;
            if (cr > op->x + painted_width(op) - l)
                cr = (int)ceil(op->x + painted_width(op) - l);
            const int cb = op->clip_b < t + 8 ? op->clip_b - t : 8;
            if (cl < cr && ct < cb)
                paint_op(c, op, cell, op->x - l, op->y - t, out, n, n, scale, cl, ct, cr, cb);
        }
    } else if (!c->graphics[tile]) {
        const uint8_t *ch = (hide_ds ? c->display_base : c->seen) + (size_t)k * 8 * c->bpp;
        for (int py = 0; py < 8; ++py) {
            for (int px = 0; px < 8; ++px) {
                const uint32_t v = c->bpp == 4 ? (ch[py * 4 + px / 2] >> ((px & 1) * 4)) & 15u : ch[py * 8 + px];
                const uint32_t pal = (uint32_t)cell >> 12;
                if (v) {
                    const uint32_t colour = c->bpp == 4 ? KET_OPAQUE | pal * 16u | v
                                                        : KET_OPAQUE | KET_EXTENDED | pal << KET_EXT_PAL_SHIFT | v;
                    for (int dy = 0; dy < scale; ++dy)
                        for (int dx = 0; dx < scale; ++dx)
                            out[(py * scale + dy) * n + px * scale + dx] = colour;
                }
            }
        }
    }
    if (cell & 0xc00u)
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const int fx = cell & 0x400u ? n - 1 - x : x;
                const int fy = cell & 0x800u ? n - 1 - y : y;
                if (fy * n + fx > y * n + x) {
                    const uint32_t v = out[y * n + x];
                    out[y * n + x] = out[fy * n + fx];
                    out[fy * n + fx] = v;
                }
            }
        }
    mask_tile(out, n, scale, cell, c->owned[tile]);
    if (hd)
        underlay_graphics(c, tile, cell, scale, out, n);
}

/* Update only changed visible tiles. Repeated blank tiles cannot multiply images. */
static void build_extra(Canvas *c, const KhdaysBgLayer *l, int ox, int oy, int scale, uint32_t palette)
{
    palette = (palette ^ (uint32_t)c->bg) * 16777619u;
    if (!hd_enabled() || !c->display_count) {
        release_extra(c);
        return;
    }
    uint8_t text_tiles[16384 / 8] = {0}; /* One bit per possible canvas tile. */
    for (int i = 0; i < c->display_count; ++i) {
        const GlyphOp *op = &c->display_ops[i];
        /* Fitting moves artwork, but duplicate placements must also hide the
           native text's tail. Both footprints belong to the captured draw. */
        const int fitted = op->x_scale > 0 && op->x_scale < 1;
        for (int pass = 0; pass <= fitted; ++pass) {
            int left = pass ? op->source_x : op->clip_l;
            int top = pass ? op->source_y : op->clip_t;
            int right = pass ? op->source_x + op->source_w : op->clip_r;
            int bottom = pass ? op->source_y + op->source_h : op->clip_b;
            if (left < 0)
                left = 0;
            if (top < 0)
                top = 0;
            if (right > c->area_w * 8)
                right = c->area_w * 8;
            if (bottom > c->area_h * 8)
                bottom = c->area_h * 8;
            if (left >= right || top >= bottom)
                continue;
            for (int row = top / 8; row <= (bottom - 1) / 8; ++row)
                for (int col = left / 8; col <= (right - 1) / 8; ++col) {
                    const int tile = row * c->area_w + col;
                    text_tiles[tile / 8] |= (uint8_t)(1u << (tile & 7));
                }
        }
    }
    int sources[EXTRA_CELLS], count = 0;
    uint16_t cells[EXTRA_CELLS];
    const int x0 = l->hofs >> 3, y0 = l->vofs >> 3;
    const int cols = ((l->hofs & 7) + KHDAYS_SCREEN_W + 7) / 8;
    const int rows = ((l->vofs & 7) + KHDAYS_SCREEN_H + 7) / 8;
    for (int y = 0; y < EXTRA_ROWS; ++y) {
        for (int x = 0; x < EXTRA_COLS; ++x) {
            const int at = y * EXTRA_COLS + x;
            sources[at] = -1;
            cells[at] = 0;
            if (x >= cols || y >= rows)
                continue;
            if (obtained_cell(c, l, x0 + x, y0 + y))
                continue;
            const uint32_t entry = khdays_bg_map_entry(c->engine, l, x0 + x, y0 + y);
            const int k = canvas_char(c, l, entry);
            if (k < 0)
                continue;
            const int tile = (k / c->param) * c->area_w + k % c->param;
            if (!(text_tiles[tile / 8] & (1u << (tile & 7))))
                continue;
            const int vx = ((x0 + x - k % c->param) % l->wt + l->wt) % l->wt;
            const int vy = ((y0 + y - k / c->param) % l->ht + l->ht) % l->ht;
            if (vx == ox && vy == oy)
                continue;
            sources[at] = k;
            cells[at] = (uint16_t)(entry & 0xfc00u);
            ++count;
        }
    }
    if (!count) {
        release_extra(c);
        return;
    }
    if (c->extra && c->extra->scale != scale)
        release_extra(c);
    if (!c->extra) {
        c->extra = (MappedImage *)calloc(1, sizeof(MappedImage));
        if (!c->extra)
            return;
        c->extra->image = c->extra->hd_image = -1;
        c->extra->scale = scale;
        for (int i = 0; i < EXTRA_CELLS; ++i)
            c->extra->source[i] = -1;
    }
    MappedImage *m = c->extra;
    const int width = EXTRA_COLS * 8 * scale, height = EXTRA_ROWS * 8 * scale;
    int fresh = m->invalid, complete = 1;
    if (m->image < 0) {
        m->image = khdays_ui_image_get(EXTRA_COLS * 8, EXTRA_ROWS * 8);
        fresh = 1;
    }
    if (m->hd_image < 0) {
        m->hd_image = khdays_ui_image_get(width, height);
        fresh = 1;
    }
    if (m->image < 0)
        return;
    const int hide_ds = hd_enabled() && (!c->display_count || m->hd_image >= 0);
    uint32_t *base = NULL, *hd = NULL;
    /* Submission is serial; share one scratch tile at the maximum UI scale. */
    static uint32_t tile[64 * 64];
    int rendered[EXTRA_CELLS], rendered_count = 0;
    for (int i = 0; i < EXTRA_CELLS; ++i) {
        const int k = sources[i];
        const int dirty = k >= 0 && c->dirty[(k / c->param) * c->area_w + k % c->param];
        if (!fresh && !dirty && m->source[i] == k && m->cells[i] == cells[i] && m->hide_ds == hide_ds &&
            m->palette == palette)
            continue;
        if (k < 0 && m->source[i] < 0 && !fresh)
            continue;
        if (!base)
            base = khdays_ui_image_begin(m->image);
        if (!hd && m->hd_image >= 0)
            hd = khdays_ui_image_begin(m->hd_image);
        if (!base || (m->hd_image >= 0 && !hd)) {
            complete = 0;
            break;
        }
        int reuse = -1;
        for (int j = 0; j < rendered_count; ++j) {
            const int previous = rendered[j];
            if (sources[previous] == k && cells[previous] == cells[i]) {
                reuse = previous;
                break;
            }
        }
        for (int plane = 0; plane < 2; ++plane) {
            uint32_t *out = plane ? hd : base;
            if (!out)
                continue;
            const int plane_scale = plane ? scale : 1;
            const int n = 8 * plane_scale, row_width = EXTRA_COLS * n;
            if (reuse < 0) {
                if (k >= 0)
                    mapped_tile(c, k, cells[i], plane_scale, plane, hide_ds, tile);
                else
                    memset(tile, 0, (size_t)n * n * sizeof(uint32_t));
            }
            for (int y = 0; y < n; ++y)
                memcpy(out + (size_t)((i / EXTRA_COLS) * n + y) * row_width + (i % EXTRA_COLS) * n,
                       reuse < 0 ? tile + y * n
                                 : out + (size_t)((reuse / EXTRA_COLS) * n + y) * row_width + (reuse % EXTRA_COLS) * n,
                       (size_t)n * sizeof(uint32_t));
        }
        /* Repeated map entries share the finished tile within this update only. */
        if (reuse < 0)
            rendered[rendered_count++] = i;
        m->source[i] = k;
        m->cells[i] = cells[i];
    }
    if (base)
        khdays_ui_image_end(m->image);
    if (hd)
        khdays_ui_image_end(m->hd_image);
    if (base && count >= 64 && profiling())
        khdays_log("text mapped update: canvas %08x, %d visible copies, %d distinct tile updates, scale %d", c->cc,
                   count, rendered_count, scale);
    m->hide_ds = hide_ds;
    m->palette = palette;
    m->invalid = !complete;
    if (tracing()) {
        uint32_t hash = 2166136261u;
        for (int i = 0; i < EXTRA_CELLS; ++i)
            hash = ((hash ^ (uint32_t)sources[i]) * 16777619u ^ cells[i]) * 16777619u;
        if (hash != m->map_hash)
            say("HD mapped overlay: canvas %08x engine %c BG%d primary %d,%d, %d supplementary text cells, %s", c->cc,
                c->engine ? 'B' : 'A', c->bg, ox, oy, count, m->hd_image >= 0 ? "HD" : "DS fallback");
        m->map_hash = hash;
    }
}

/* Indexed DS graphics stay at native resolution, with each visible cell's palette
 * and flips. The compositor scales them; PNG graphics and text use the RGBA plane. */
static void make_image(Canvas *c, const KhdaysBgLayer *l, int ox, int oy)
{
    const int w = c->area_w * 8, h = c->area_h * 8;
    const uint32_t char_size = (uint32_t)(8 * c->bpp);
    const size_t ncells = (size_t)c->area_w * (size_t)c->area_h;
    const int hide_ds = hd_enabled() && (!c->display_count || c->hd_ready);
    int changed = c->base_hd != hide_ds;
    uint32_t *texels;
    if (changed)
        for (size_t i = 0; i < ncells; ++i)
            c->dirty[i] |= 2;
    if (c->cells == NULL) {
        c->cells = (uint16_t *)malloc(ncells * sizeof(uint16_t));
        if (c->cells == NULL) {
            return;
        }
        memset(c->cells, 0xff, ncells * sizeof(uint16_t));
        changed = 1;
    }
    if (c->image < 0) {
        khdays_ui_image_put(c->image);
        c->image = khdays_ui_image_get(w, h);
        memset(c->dirty, 3, ncells);
        changed = 1;
    }
    if (c->image < 0) {
        return;
    }
    for (int row = 0; row < c->area_h; ++row) {
        for (int col = 0; col < c->area_w; ++col) {
            const uint32_t entry = khdays_bg_map_entry(c->engine, l, ox + col, oy + row);
            const uint16_t cell =
                !obtained_cell(c, l, ox + col, oy + row) && canvas_char(c, l, entry) == row * c->param + col
                    ? (uint16_t)(entry & 0xfc00u)
                    : 0xffffu;
            if (c->cells[row * c->area_w + col] != cell) {
                c->cells[row * c->area_w + col] = cell;
                c->dirty[row * c->area_w + col] |= 3;
                changed = 1;
            }
            if (c->dirty[row * c->area_w + col] & 2)
                changed = 1;
        }
    }
    if (!changed && c->image_version == c->version) {
        return;
    }
    texels = khdays_ui_image_begin(c->image);
    if (texels == NULL) {
        return;
    }
    for (int row = 0; row < c->area_h; ++row) {
        for (int col = 0; col < c->area_w; ++col) {
            const int tile = row * c->area_w + col;
            if (!(c->dirty[tile] & 2))
                continue;
            const uint16_t cell = c->cells[tile];
            const uint8_t *ch = (hide_ds ? c->display_base : c->seen) + ((uint32_t)row * c->param + col) * char_size;
            for (int py = row * 8; py < row * 8 + 8; ++py) {
                for (int px = col * 8; px < col * 8 + 8; ++px) {
                    uint32_t t = 0;
                    if (cell != 0xffffu && !c->graphics[tile]) {
                        const int fx = cell & 0x400u ? 7 - (px & 7) : px & 7;
                        const int fy = cell & 0x800u ? 7 - (py & 7) : py & 7;
                        const uint32_t pal = (uint32_t)cell >> 12;
                        if (c->bpp == 4) {
                            const uint32_t v = (ch[fy * 4 + fx / 2] >> ((fx & 1) * 4)) & 15u;
                            t = v ? KET_OPAQUE | (pal * 16u + v) : 0u;
                        } else {
                            const uint32_t v = ch[fy * 8 + fx];
                            t = v ? KET_OPAQUE | KET_EXTENDED | pal << KET_EXT_PAL_SHIFT | v : 0u;
                        }
                    }
                    const int owned_x = cell & 0x400u ? 7 - (px & 7) : px & 7;
                    const int owned_y = cell & 0x800u ? 7 - (py & 7) : py & 7;
                    if (!(c->owned[tile] & (UINT64_C(1) << (owned_y * 8 + owned_x))))
                        t = 0;
                    texels[(size_t)py * w + px] = t;
                }
            }
            c->dirty[tile] &= 0xfdu;
        }
    }
    khdays_ui_image_end(c->image);
    c->base_hd = hide_ds;
    c->image_version = c->version;
}

/* The pickup keeps a separate layout, but only while the game's cached item
 * tiles still occupy its banner. No comparison with native glyph ink is used. */
static void prepare_obtained(int scale)
{
    obtained.ready = 0;
    Canvas *c = &obtained.canvas;
    KhdaysBgLayer l;
    if (!obtained.active || !c->seen || (!hd_enabled() && !pickup_enabled()) || !khdays_bg_text_layer(0, 1, &l) ||
        l.colors256)
        return;
    unsigned key;
    if (!obtained_item_key((unsigned)obtained.slot, &key) || key != obtained.key)
        return;
    const uint32_t first = 0x250u + (uint32_t)obtained.slot * 16u;
    const uint32_t address = l.chars + first * 32u;
    Canvas *owner = NULL;
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *candidate = &canvases[i];
        if (candidate->command_cache && candidate->engine == 0 && candidate->bpp == 4 &&
            (candidate->in_vram || candidate->pending) && candidate->vram <= address &&
            (uint64_t)candidate->vram + candidate->bytes >= address + 512) {
            owner = candidate;
            break;
        }
    }
    if (!owner)
        return;
    if (!obtained.bound || obtained.owner != owner->cc || obtained.owner_buffer != owner->buffer ||
        obtained.address != address) {
        obtained.bound = obtained.source_valid = 1;
        obtained.address = address;
        obtained.owner = owner->cc;
        obtained.owner_buffer = owner->buffer;
    }
    observe_vram(owner);
    if (!obtained.source_valid)
        return;
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 8; ++col) {
            const uint32_t offset = (uint32_t)(row * 8 + col);
            const uint32_t k = (address - owner->vram) / 32u + offset;
            const uint32_t tile = (k / (uint32_t)owner->param) * (uint32_t)owner->area_w + k % (uint32_t)owner->param;
            const uint32_t entry = khdays_bg_map_entry(0, &l, col + (obtained.notification ? 2 : 1), row + 8);
            if ((entry & 0x3ffu) != first + offset || tile >= (uint32_t)(owner->area_w * owner->area_h) ||
                (!pickup_enabled() && owner->owned[tile] != UINT64_MAX) ||
                !khdays_bg_memory(0, address + offset * 32u) || khdays_tiles2d_bg_tile(0, address + offset * 32u))
                return;
        }
    c->engine = 0;
    c->bg = 1;
    c->vram = address;
    const uint32_t palette = khdays_ui_palette_hash(0);
    make_image(c, &l, obtained.notification ? 2 : 1, 8);
    if (c->cells)
        build_hd(c, scale, palette);
    make_image(c, &l, obtained.notification ? 2 : 1, 8);
    obtained.ready = c->image >= 0 && c->cells && (!c->display_count || c->hd_ready || pickup_enabled());
}

/* Cache glyph-only planes for both pickup variants and the recon announcement. */
static int notification_glyphs(Canvas *c, int scale, unsigned key, unsigned *version)
{
    const int w = c->area_w * 8 * scale, h = c->area_h * 8 * scale;
    const size_t count = (size_t)w * h;
    uint32_t signature = (khdays_ui_palette_hash(0) ^ c->version) * 16777619u ^ (unsigned)scale;
    signature = (signature ^ key ^ (unsigned)font_display_enabled()) * 16777619u;
    for (int i = 0; i < c->display_count; ++i) {
        const unsigned char *bytes = (const unsigned char *)&c->display_ops[i];
        for (size_t k = 0; k < sizeof(GlyphOp); ++k)
            signature = (signature ^ bytes[k]) * 16777619u;
    }
    if (count > c->information_room) {
        uint32_t *grown = realloc(c->information_pixels, count * sizeof(uint32_t));
        if (!grown)
            return 0;
        c->information_pixels = grown;
        c->information_room = count;
        c->information_scale = 0;
    }
    if (c->information_version != signature || c->information_scale != scale) {
        memset(c->information_pixels, 0, count * sizeof(uint32_t));
        for (int i = 0; i < c->display_count; ++i) {
            const GlyphOp *op = &c->display_ops[i];
            const int left = op->clip_l < 0 ? 0 : op->clip_l, top = op->clip_t < 0 ? 0 : op->clip_t;
            const int right = op->clip_r > w / scale ? w / scale : op->clip_r;
            const int bottom = op->clip_b > h / scale ? h / scale : op->clip_b;
            if (left < right && top < bottom)
                paint_op(c, op, 0xf000u, op->x, op->y, c->information_pixels, w, h, scale, left, top, right, bottom);
        }
        c->information_version = signature;
        c->information_scale = scale;
    }
    *version = signature;
    return 1;
}

/* Retain glyphs independently of native tiles so the sequence can finish its own exit. */
static void prepare_obtained_sequence(int scale)
{
    Canvas *c = &obtained.canvas;
    KhdaysBgLayer l;
    unsigned key;
    /* The private glyphs do not depend on the aliased item tiles being restored after INFORMATION. */
    if (!obtained.active || !pickup_enabled() || !c->display_count ||
        !obtained_item_key((unsigned)obtained.slot, &key) || key != obtained.key || !khdays_bg_text_layer(0, 1, &l))
        return;
    unsigned signature;
    if (!notification_glyphs(c, scale, obtained.key, &signature))
        return;
    const int w = c->area_w * 8 * scale, h = c->area_h * 8 * scale;
    const int lw = l.wt * 8, lh = l.ht * 8;
    const int x = ((-l.hofs % lw) + lw) % lw, y = (((56 - l.vofs) % lh) + lh) % lh;
    KhdaysUiElement e = {
        "hud.obtained", 0, 1, x < 256 ? (float)x : (float)(x - lw), y < 192 ? (float)y : (float)(y - lh), 104, 24, -1};
    if (obtained.notification)
        khdays_ui_notification_text(1, &e, c->information_pixels, w, h, signature);
    else
        khdays_ui_obtained_text(&e, c->information_pixels, w, h, signature);
}

static void prepare_breakthrough(int scale)
{
    KhdaysUiElement box;
    if (!khdays_ui_notification_box(2, &box))
        return;
    for (int index = 0; index < canvas_count; ++index) {
        Canvas *c = &canvases[index];
        if (c->display_notification != 2 || !c->display_count || c->area_w > 64 || c->area_h > 16 ||
            (!c->pending && !c->in_vram))
            continue;
        unsigned signature;
        if (!notification_glyphs(c, scale, 2, &signature))
            return;
        const int w = c->area_w * 8 * scale, h = c->area_h * 8 * scale;
        khdays_ui_notification_text(2, &box, c->information_pixels, w, h, signature);
        break;
    }
}

static void emit_obtained(void)
{
    Canvas *c = &obtained.canvas;
    KhdaysBgLayer l;
    if (obtained.notification || obtained.sequence || pickup_owns() || !obtained.ready ||
        !khdays_bg_text_layer(0, 1, &l))
        return;
    const int lw = l.wt * 8, lh = l.ht * 8;
    const int x = ((8 - l.hofs) % lw + lw) % lw;
    const int y = ((64 - l.vofs) % lh + lh) % lh;
    KhdaysUiElement e = {c->id, 0, 1, 0, 0, 64, 16, c->image};
    for (int ex = x - lw; ex < KHDAYS_SCREEN_W; ex += lw)
        for (int ey = y - lh; ey < KHDAYS_SCREEN_H; ey += lh)
            if (ex + 64 > 0 && ey + 16 > 0) {
                e.x = (float)ex;
                e.y = (float)ey;
                e.image = c->image;
                e.slot = 1;
                khdays_ui_emit(KHDAYS_UI_TEXT, &e);
                if (c->hd_image >= 0) {
                    e.image = c->hd_image;
                    e.slot |= KES_DIRECT | KES_FINE;
                    khdays_ui_emit(KHDAYS_UI_TEXT, &e);
                }
            }
    for (int row = 8; row < 10; ++row)
        for (int col = 1; col < 9; ++col)
            khdays_ui_left_out_cell(KHDAYS_UI_TEXT, 0, 1, col, row);
}

/* A separate glyph-only plane shares the existing painter and changes only with text/palette/scale. */
static int emit_information(Canvas *c, int ox, int oy, const KhdaysBgLayer *layer, uint32_t palette, int scale)
{
    /* SetCaptionText maps the lower caption to BG0; the upper BeginTextCrawl uses BG1. */
    if (!c->display_information || c->bg != (c->engine ? 0 : 1) || !khdays_ui_information_wants())
        return 0;
    if (c->display_information_epoch != khdays_ui_information_epoch())
        return khdays_ui_information_active(c->engine);
    if (!c->display_count)
        return khdays_ui_information_active(c->engine);
    KhdaysBgLayer caption_layer;
    int caption_x, caption_y;
    if (!c->engine && khdays_ui_information_text_scroll(&caption_x, &caption_y)) {
        caption_layer = *layer;
        caption_layer.hofs = caption_x;
        caption_layer.vofs = caption_y;
        layer = &caption_layer;
    }
    const int lw = layer->wt * 8, lh = layer->ht * 8;
    int px = ((ox * 8 - layer->hofs) % lw + lw) % lw;
    int py = ((oy * 8 - layer->vofs) % lh + lh) % lh;
    if (px >= 256)
        px -= lw;
    if (py >= 192)
        py -= lh;
    if (py < -16 || py >= 24 || px >= 256 || px + c->area_w * 8 <= 0)
        return 0;
    const int width = c->area_w * 8 * scale, height = c->area_h * 8 * scale;
    if (width > 4096 || height > 512)
        return 0;
    unsigned coverage = 0;
    for (int i = 0; i < c->display_count; ++i) {
        const GlyphOp *op = &c->display_ops[i];
        for (int wrap = -1; wrap <= 1; ++wrap) {
            int l = px + op->source_x + wrap * lw, r = l + op->source_w;
            int t = py + op->source_y, b = t + op->source_h;
            if (l < 0)
                l = 0;
            if (r > 256)
                r = 256;
            if (t < 0)
                t = 0;
            if (b > 24)
                b = 24;
            if (l < r && t < b)
                coverage += (unsigned)((r - l) * (b - t));
        }
    }
    if (!coverage)
        return khdays_ui_information_active(c->engine);
    const size_t count = (size_t)width * height;
    uint32_t signature = (palette ^ (uint32_t)c->display_count ^ (unsigned)font_display_enabled()) * 16777619u;
    for (int i = 0; i < c->display_count; ++i) {
        const unsigned char *p = (const unsigned char *)&c->display_ops[i];
        for (size_t k = 0; k < sizeof(GlyphOp); ++k)
            signature = (signature ^ p[k]) * 16777619u;
    }
    uint16_t colour_cell = 0;
    for (int i = 0; i < c->area_w * c->area_h; ++i)
        if (c->cells[i] != 0xffffu) {
            colour_cell = c->cells[i] & 0xf000u;
            break;
        }
    signature = (signature ^ colour_cell) * 16777619u;
    if (count > c->information_room) {
        uint32_t *grown = realloc(c->information_pixels, count * sizeof(uint32_t));
        if (!grown)
            return 0;
        c->information_pixels = grown;
        c->information_room = count;
        c->information_scale = 0;
    }
    if (c->information_version != signature || c->information_scale != scale) {
        memset(c->information_pixels, 0, count * sizeof(uint32_t));
        for (int i = 0; i < c->display_count; ++i) {
            const GlyphOp *op = &c->display_ops[i];
            for (int row = 0; row < c->area_h; ++row)
                for (int col = 0; col < c->area_w; ++col) {
                    // Keep offscreen glyphs: the replacement owns the horizontal reveal and marquee.
                    const uint16_t cell = colour_cell;
                    int l = col * 8, t = row * 8, r = l + 8, b = t + 8;
                    if (l < op->clip_l)
                        l = op->clip_l;
                    if (t < op->clip_t)
                        t = op->clip_t;
                    if (r > op->clip_r)
                        r = op->clip_r;
                    if (b > op->clip_b)
                        b = op->clip_b;
                    if (l < r && t < b)
                        paint_op(c, op, cell, op->x, op->y, c->information_pixels, width, height, scale, l, t, r, b);
                }
        }
        c->information_version = signature;
        c->information_scale = scale;
    }
    KhdaysUiElement e = {"hud.info", c->engine, (uint32_t)c->bg, 0, 0, 256, 24, c->image};
    /* BG scrolling belongs to the message, not the KH2 window's root. */
    return khdays_ui_information_text(&e, c->information_pixels, width, height, signature ^ (unsigned)scale, 8, 3,
                                      coverage);
}

static void emit(void)
{
    const int profile = profiling();
    clock_t started = profile ? clock() : 0, stamp = started;
    clock_t elapsed[5] = {0};
    int profiled_glyphs = 0;
    const int scale = khdays_ui_scale();
    uint32_t palettes[2] = {0};
    unsigned palettes_valid = 0;
    khdays_ui_information_begin();
    khdays_ui_mission_panel_emit();
    khdays_ui_obtained_begin();
    prepare_obtained(scale);
    prepare_obtained_sequence(scale);
    prepare_breakthrough(scale);
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        KhdaysUiElement e;
        KhdaysBgLayer l;
        int ox, oy, lw, lh, x, y;
        if (!c->in_vram && !c->pending) {
            if (c->detached || (!c->copied && ((const DsCanvas *)KHDAYS_W(c->cc))->char_base != c->buffer)) {
                forget(c); /* let go, and no longer in VRAM */
                --i;
            }
            continue;
        }
        if (c->detached) {
            int owned = 0;
            for (int k = 0; k < c->area_w * c->area_h; ++k)
                owned |= c->owned[k] != 0;
            if (!owned) {
                forget(c);
                --i;
                continue;
            }
        }
        if (c->bytes == 0 || c->area_w == 0) {
            continue;
        }
        if (c->seen == NULL && (c->seen = (uint8_t *)calloc(c->bytes, 1)) == NULL) {
            continue;
        }
        if (profile)
            stamp = clock();
        observe_vram(c);
        if (profile) {
            elapsed[0] += clock() - stamp;
            stamp = clock();
            profiled_glyphs += c->display_count;
        }
        c->pending = 0;
        c->in_vram = 1;
        const int placed = display_layer(c, &l, &ox, &oy);
        if (profile)
            elapsed[1] += clock() - stamp;
        if (!placed) {
            release_extra(c);
            if (tracing() && c->said_shown != 2) {
                say("canvas %08x (engine %c's BG%d, BG memory +%u) not on the screen", c->cc, c->engine ? 'B' : 'A',
                    c->bg, c->vram);
                c->said_shown = 2;
            }
            continue;
        }
        observe_graphics(c);
        if (strcmp(c->id, "text.canvas") == 0)
            snprintf(c->id, sizeof(c->id), "text.%c%d.%d.%d", c->engine ? 'b' : 'a', c->bg, ox, oy);
        if (profile)
            stamp = clock();
        if (!(palettes_valid & (1u << c->engine))) {
            palettes[c->engine] = khdays_ui_palette_hash(c->engine);
            palettes_valid |= 1u << c->engine;
        }
        build_extra(c, &l, ox, oy, scale, palettes[c->engine]);
        if (profile)
            elapsed[2] += clock() - stamp;
        if (tracing() && c->said_shown != 1) {
            say("canvas %08x shown on engine %c's BG%d from cell %d,%d (%dx%d characters, %d bits)", c->cc,
                c->engine ? 'B' : 'A', c->bg, ox, oy, c->area_w, c->area_h, c->bpp);
            c->said_shown = 1;
        }
        if (profile)
            stamp = clock();
        make_image(c, &l, ox, oy);
        if (profile) {
            elapsed[3] += clock() - stamp;
            stamp = clock();
        }
        if (c->cells)
            build_hd(c, scale, palettes[c->engine]);
        if (profile) {
            elapsed[4] += clock() - stamp;
            stamp = clock();
        }
        make_image(c, &l, ox, oy);
        if (profile)
            elapsed[3] += clock() - stamp;
        if (tracing() && c->hd_image >= 0 && !c->hd_reported) {
            say("HD overlay ready: canvas %08x engine %c BG%d, %d glyph operations, retained=%d", c->cc,
                c->engine ? 'B' : 'A', c->bg, c->display_count, c->detached);
            c->hd_reported = 1;
        }
        if (tracing()) {
            const int state = c->hd_image >= 0 ? 1 : 0;
            if (state != c->hd_last_state) {
                say("HD display: canvas %08x engine %c BG%d at %d,%d: %s (%d operations, detached=%d)", c->cc,
                    c->engine ? 'B' : 'A', c->bg, ox, oy,
                    state ? "overlay"
                          : (!hd_enabled()     ? "FALLBACK: HD disabled by environment"
                             : c->ops_disabled ? "FALLBACK: operation limit"
                                               : "FALLBACK: no captured glyphs"),
                    c->display_count, c->detached);
                c->hd_last_state = state;
            }
        }
        if (c->image < 0 || !c->cells) {
            continue;
        }
        const int information = emit_information(c, ox, oy, &l, palettes[c->engine], scale);
        /* Follow layer scroll and repeat where the tilemap wraps onto the screen. */
        lw = l.wt * 8;
        lh = l.ht * 8;
        x = ((ox * 8 - l.hofs) % lw + lw) % lw;
        y = ((oy * 8 - l.vofs) % lh + lh) % lh;
        e.id = c->id;
        e.screen = c->engine;
        e.slot = (uint32_t)c->bg;
        e.w = (float)(c->area_w * 8);
        e.h = (float)(c->area_h * 8);
        e.image = c->image;
        KhdaysUiElement anchor = e;
        int anchor_cells = -1;
        for (int ex = x - lw; !information && ex < KHDAYS_SCREEN_W; ex += lw) {
            for (int ey = y - lh; ey < KHDAYS_SCREEN_H; ey += lh) {
                if (ex + c->area_w * 8 > 0 && ey + c->area_h * 8 > 0) {
                    e.x = (float)ex;
                    e.y = (float)ey;
                    if (c->extra) {
                        int visible = 0;
                        for (int row = 0; row < c->area_h; ++row)
                            for (int col = 0; col < c->area_w; ++col)
                                visible += c->cells[row * c->area_w + col] != 0xffffu &&
                                           ex + col * 8 < KHDAYS_SCREEN_W && ex + col * 8 + 8 > 0 &&
                                           ey + row * 8 < KHDAYS_SCREEN_H && ey + row * 8 + 8 > 0;
                        if (visible > anchor_cells) {
                            anchor_cells = visible;
                            anchor = e;
                        }
                    }
                    khdays_ui_emit(KHDAYS_UI_TEXT, &e);
                    if (c->hd_image >= 0) {
                        e.image = c->hd_image;
                        e.slot = (uint32_t)c->bg | KES_DIRECT | KES_FINE;
                        khdays_ui_emit(KHDAYS_UI_TEXT, &e);
                        e.image = c->image;
                        e.slot = (uint32_t)c->bg;
                    }
                }
            }
        }
        /* leave out the game's drawing of it: the cells its characters are shown in */
        for (int row = 0; row < c->area_h; ++row) {
            for (int col = 0; col < c->area_w; ++col) {
                const int tile = row * c->area_w + col;
                if (c->cells[tile] != 0xffffu && (!c->graphics[tile] || c->hd_image >= 0)) {
                    khdays_ui_left_out_cell(KHDAYS_UI_TEXT, c->engine, c->bg, (ox + col) % l.wt, (oy + row) % l.ht);
                }
            }
        }
        if (!information && c->extra && c->extra->image >= 0) {
            MappedImage *m = c->extra;
            e.x = (float)-(l.hofs & 7);
            e.y = (float)-(l.vofs & 7);
            e.w = EXTRA_COLS * 8.0f;
            e.h = EXTRA_ROWS * 8.0f;
            e.image = m->image;
            e.slot = (uint32_t)c->bg;
            khdays_ui_emit_following(KHDAYS_UI_TEXT, &e, &anchor);
            if (m->hd_image >= 0) {
                e.image = m->hd_image;
                e.slot |= KES_DIRECT | KES_FINE;
                khdays_ui_emit_following(KHDAYS_UI_TEXT, &e, &anchor);
            }
            for (int my = 0; my < EXTRA_ROWS; ++my) {
                for (int mx = 0; mx < EXTRA_COLS; ++mx) {
                    const int k = m->source[my * EXTRA_COLS + mx];
                    if (k >= 0 && (!c->graphics[(k / c->param) * c->area_w + k % c->param] || m->hd_image >= 0))
                        khdays_ui_left_out_cell(KHDAYS_UI_TEXT, c->engine, c->bg, ((l.hofs >> 3) + mx) % l.wt,
                                                ((l.vofs >> 3) + my) % l.ht);
                }
            }
        }
        if (!c->display_count)
            for (int k = 0; k < c->area_w * c->area_h; ++k)
                c->dirty[k] &= 0xfeu;
    }
    khdays_ui_information_end();
    emit_obtained();
    for (int bg = 1; bg <= 3; bg += 2)
        for (int row = 0; row < 24; ++row)
            for (int col = 0; col < 32; ++col)
                if (khdays_ui_notification_cell(0, bg, col, row))
                    khdays_ui_left_out_cell(KHDAYS_UI_TEXT, 0, bg, col, row);
    khdays_ui_obtained_end();
    if (profile) {
        const double ms = 1000.0 / CLOCKS_PER_SEC;
        const double total = (clock() - started) * ms;
        if (total >= 2)
            khdays_log("text profile: %.1f ms, VRAM %.1f, placement %.1f, mapped %.1f, base %.1f, raster %.1f; "
                       "%d canvases, %d glyphs, scale %d",
                       total, elapsed[0] * ms, elapsed[1] * ms, elapsed[2] * ms, elapsed[3] * ms, elapsed[4] * ms,
                       canvas_count, profiled_glyphs, scale);
    }
}

void khdays_ui_text_init(void)
{
    khdays_origin_want(); /* SDK font initialization can bypass the named loaders. */
    khdays_ui_register(KHDAYS_UI_TEXT, emit);
}

void khdays_ui_text_settings_changed(void)
{
    for (int i = 0; i <= canvas_count; ++i) {
        Canvas *c = i < canvas_count ? &canvases[i] : &obtained.canvas;
        if (!c->dirty || !c->tile_hash)
            continue;
        const int mode = khdays_hud_setting(KHDAYS_OPT_FONT) != 0;
        for (int k = 0; k < c->op_count; ++k)
            apply_layout(&c->ops[k], mode, 1);
        for (int k = 0; k < c->display_count; ++k)
            apply_layout(&c->display_ops[k], mode, 1);
        memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
        memset(c->tile_hash, 0, (size_t)c->area_w * c->area_h * sizeof(*c->tile_hash));
        ++c->version;
        c->information_scale = 0;
    }
}
