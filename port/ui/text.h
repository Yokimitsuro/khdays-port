/* BG canvas text tracking: independent glyph commands and preserved graphics follow
 * character transfers into the PC compositor. Hook arguments are host pointers to
 * the rewritten game's 32-bit-reference structures. See docs/HD_FONTS.md. */
#ifndef KHDAYS_CANVAS_TEXT_H
#define KHDAYS_CANVAS_TEXT_H

#include <stdint.h>

/* Registers the part's elements (at start). */
void khdays_ui_text_init(void);
void khdays_ui_text_settings_changed(void);

/* What the hooks follow: a text renderer attached to a layer and let go; a canvas added to a tile
 * surface; a block of the game's heaps freed (a canvas whose buffer it was is gone); and characters
 * loaded into a BG layer's memory or the sprites' (GX/GXS_LoadBGnChar, GX/GXS_LoadOBJ). */
void khdays_ui_text_renderer(const void *renderer, int layer, const void *resource, const void *frame, int result);
void khdays_ui_text_renderer_gone(const void *renderer, int result);
void khdays_ui_text_surface_canvas(const void *surface, int current, const void *canvas_object);
void khdays_ui_text_freed(const void *heap, const void *block);
void khdays_ui_text_chars_a0(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_a1(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_a2(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_a3(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b0(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b1(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b2(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_chars_b3(const void *src, unsigned int offset, unsigned int size);
void khdays_ui_text_obj_a(const void *src, uint32_t offset, uint32_t size);
void khdays_ui_text_obj_b(const void *src, uint32_t offset, uint32_t size);

/* After SDK CPU fills: records write ranges even if their bytes did not change. */
unsigned int khdays_ui_text_copy_owner(const void *source, const void *destination, unsigned int size);
void khdays_ui_text_memory_copy(const void *source, void *destination, unsigned int size, unsigned int owner);
void khdays_ui_text_memory_clear(const void *destination, unsigned int size);
void khdays_ui_text_scroll_before(const void *window, int amount);
void khdays_ui_text_scroll(const void *window, int amount);

/* NitroSystem G2D BG canvas hooks (from port/tools/prepare.py). */
void khdays_ui_text_glyph_before(const void *canvas, const void *font, int x, int y, int cl, const void *glyph);
void khdays_ui_text_glyph(const void *canvas, const void *font, int x, int y, int cl, const void *glyph);
void khdays_ui_text_clear(const void *canvas, int cl);
void khdays_ui_text_clear_area(const void *canvas, int cl, int x, int y, int w, int h);
void khdays_ui_text_font_init(const void *font, const void *file);
void khdays_ui_text_font_loaded(void *font, const char *name, int arg2, int arg3, int result);
void khdays_ui_text_font_bound(void *font, const void *name);
int khdays_ui_text_metric(const void *font, int glyph, int field, int fallback);
int khdays_ui_text_kerning(const void *font, int left, int right);
int khdays_ui_text_extent(const void *font, int glyph, int left, int width, int advance);
double khdays_ui_text_metric_f(const void *font, int glyph, int field, double fallback);
double khdays_ui_text_kerning_f(const void *font, int left, int right);
double khdays_ui_text_extent_f(const void *font, int glyph, int left, int width, int advance);
int khdays_ui_text_round(double value);
void khdays_ui_text_position(const void *font, double x, double y);
double khdays_ui_text_pen_x(int x);
int khdays_ui_text_numeric(const void *font, int layout);
double khdays_ui_text_fit_width(const void *font, int layout, double *offset);
int khdays_ui_text_ink(const void *font, int glyph, int w, int h, int ink, double *bounds);
void khdays_ui_text_fit_begin(const void *font, double origin, double left, double factor, double offset);
void khdays_ui_text_fit_end(void);
void khdays_ui_text_crawl_begin(const void *context, const void *text, int index, int x, int y, double kerning);
int khdays_ui_text_crawl_advance(double advance);
void khdays_ui_text_field_tutorial_before(int step, int arg);
void khdays_ui_text_field_tutorial(int step, int arg);
void khdays_ui_text_scope_push(const char *scope);
void khdays_ui_text_scope_pop(void);
int khdays_ui_text_obtained_wants(unsigned int slot, int obtained);
void khdays_ui_text_obtained_capture(const void *surface);

#endif
