#ifndef KHDAYS_HD_FONT_RUNTIME_H
#define KHDAYS_HD_FONT_RUNTIME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int khdays_hd_font_observe(int enabled);
int khdays_hd_font_layout_override(int mode);
int khdays_hd_font_map_ready(const char *resource);
void khdays_hd_font_bind_charmap(const char *resource, const uint32_t *codes, int count);
int khdays_hd_font_has(const char *resource);
int khdays_hd_font_select(const char *resource, const char *scope, char *selected, int size);
int khdays_hd_font_mappings_enabled(void);
enum { KHDF_LAYOUT_PANEL_COUNTER, KHDF_LAYOUT_LOAD_TIME, KHDF_LAYOUT_SAVE_TIME, KHDF_LAYOUT_COUNT };
int khdays_hd_font_numeric_layout(int layout);
enum { KHDF_FIT_ITEMS, KHDF_FIT_OBTAINED };
double khdays_hd_font_fit_layout(int layout, double *offset);
int khdays_hd_font_ink(const char *resource, int index, int w, int h, int ink, double *bounds);
int khdays_hd_font_paint_fitted(const char *resource, int index, double x, double y, int logical_width,
                                int logical_height, uint32_t rgba_color, uint32_t *rgba, int width, int height,
                                int scale, int clip_l, int clip_t, int clip_r, int clip_b, double x_scale);
int khdays_hd_font_glyph_available(const char *resource, int index);
enum { KHDF_LEFT, KHDF_WIDTH, KHDF_ADVANCE, KHDF_LINE_FEED, KHDF_SIZED, KHDF_CELL_WIDTH, KHDF_CELL_HEIGHT };
int khdays_hd_font_baseline(const char *resource, int index, int days_baseline);
int khdays_hd_font_geometry(const char *resource, int index, int days_w, int days_h, int days_ink_w, int *geometry);
int khdays_hd_font_metric(const char *resource, int index, int field, int fallback);
int khdays_hd_font_extent(const char *resource, int index, int left, int width, int advance);
int khdays_hd_font_kerning(const char *resource, unsigned int left, unsigned int right);
double khdays_hd_font_metric_f(const char *resource, int index, int field, double fallback);
double khdays_hd_font_kerning_f(const char *resource, unsigned int left, unsigned int right);
double khdays_hd_font_extent_f(const char *resource, int index, int left, int width, int advance);
double khdays_hd_font_baseline_f(const char *resource, int index, int days_baseline);
int khdays_hd_font_paint(const char *resource, int index, double x, double y, int logical_width, int logical_height,
                         uint32_t rgba_color, uint32_t *rgba, int width, int height, int scale);
int khdays_hd_font_paint_clipped(const char *resource, int index, double x, double y, int logical_width,
                                 int logical_height, uint32_t rgba_color, uint32_t *rgba, int width, int height,
                                 int scale, int clip_l, int clip_t, int clip_r, int clip_b);
#ifdef __cplusplus
}
#endif
#endif
