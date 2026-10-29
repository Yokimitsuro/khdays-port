/* Synthetic operation/VRAM tests; no game assets. Includes the implementation
 * to exercise canvas lifetime and transfers with a small fake DS memory. */
#include "../ui/text.c"
static int information_enabled, information_calls;
static int information_engine, information_bg;
static uint32_t information_ink;
static int information_right;
static unsigned information_revision;
static unsigned information_epoch = 1;
static int mission_panel_owned;
void khdays_ui_mission_panel_emit(void) {}
int khdays_ui_mission_panel_cell(int engine, int bg, int x, int y)
{
    (void)x;
    return mission_panel_owned && engine == 1 && (bg == 0 || bg == 2) && y >= 19 && y < 24;
}
static int obtained_sequence_enabled, obtained_sequence_owned, obtained_sequence_calls, retiring_notice;
static unsigned obtained_sequence_ink;
static KhdaysUiElement notification_anchors[3];
int khdays_ui_obtained_enabled(void)
{
    return obtained_sequence_enabled;
}
int khdays_ui_obtained_configured(void)
{
    return obtained_sequence_enabled;
}
void khdays_ui_obtained_request(int announcement)
{
    if (!announcement)
        obtained_sequence_owned = 0;
}
void khdays_ui_obtained_begin(void) {}
int khdays_ui_obtained_owns(void)
{
    return obtained_sequence_owned;
}
void khdays_ui_obtained_end(void) {}
int khdays_ui_obtained_text(const KhdaysUiElement *e, const unsigned *rgba, int w, int h, unsigned version)
{
    notification_anchors[0] = *e;
    (void)version;
    obtained_sequence_owned = 1;
    ++obtained_sequence_calls;
    obtained_sequence_ink = 0;
    for (int i = 0; i < w * h; ++i)
        obtained_sequence_ink |= rgba[i];
    return 1;
}
static int notification_enabled[4], notification_owned[4], notification_calls[4];
int khdays_ui_notification_enabled(int k)
{
    return notification_enabled[k];
}
int khdays_ui_notification_configured(int k)
{
    return notification_enabled[k];
}
void khdays_ui_notification_request(int k, int present)
{
    if (!present)
        notification_owned[k] = 0;
}
int khdays_ui_notification_owns(int k)
{
    return notification_owned[k];
}
int khdays_ui_notification_box(int k, KhdaysUiElement *out)
{
    if (!notification_enabled[k])
        return 0;
    *out = (KhdaysUiElement){"hud.notification", 0, 3, 0, 64, 96, 16, -1};
    return 1;
}
int khdays_ui_notification_cell(int engine, int bg, int x, int y)
{
    return !engine && (bg == 1 || bg == 3) && (notification_owned[1] || obtained_sequence_owned || retiring_notice) &&
           x < 13 && y >= 7 && y < 10;
}
int khdays_ui_notification_text(int k, const KhdaysUiElement *e, const unsigned *rgba, int w, int h, unsigned v)
{
    if (k == 1 || k == 2)
        notification_anchors[k] = *e;
    (void)v;
    unsigned ink = 0;
    for (int i = 0; i < w * h; ++i)
        ink |= rgba[i];
    if (ink)
        ++notification_calls[k];
    notification_owned[k] = 1;
    return 1;
}
unsigned khdays_ui_information_epoch(void)
{
    return information_epoch;
}
void khdays_ui_information_begin(void) {}
void khdays_ui_information_end(void) {}
int khdays_ui_information_wants(void)
{
    return information_enabled;
}
int khdays_ui_information_active(int engine)
{
    (void)engine;
    return information_enabled;
}
int khdays_ui_information_text(const KhdaysUiElement *e, const unsigned *rgba, int w, int h, unsigned version,
                               float first_x, float first_y, unsigned coverage)
{
    information_engine = e->screen;
    information_bg = (int)(e->slot & KES_NUMBER_MASK);
    information_revision = version;
    (void)first_x;
    (void)first_y;
    (void)coverage;
    ++information_calls;
    information_ink = 0;
    information_right = 0;
    for (int i = 0; i < w * h; ++i) {
        information_ink |= rgba[i];
        if ((rgba[i] >> 24) && i % w >= information_right)
            information_right = i % w + 1;
    }
    return 1;
}

static unsigned char small_ram[65536], vram[65536];
static unsigned char *ram = small_ram;
static size_t ram_bytes = sizeof(small_ram);
static uint32_t graphics_map[2][sizeof(vram) / 32];
static uint32_t graphics_pixels[32 * 16];
static KhdaysTextureReplacement graphics_replacement;
uint32_t khdays_tiles2d_bg_tile(int engine, uint32_t address)
{
    return address < sizeof(vram) ? graphics_map[engine][address / 32] : 0;
}
const KhdaysTextureReplacement *khdays_textures_replacement(int id)
{
    return id == 0 ? &graphics_replacement : NULL;
}
static int palette_reads, palette_hashes;
static uint32_t slot_tint[4];
unsigned char *khdays_window = small_ram;
int khdays_game_ram_ref(unsigned int reference)
{
    return reference > 0 && reference < ram_bytes;
}
static const char *font_origin;
static uint32_t font_origin_info, font_origin_offset;
static int font_probes;
static int observing_font, font_layout_override = -1;
int khdays_hd_font_layout_override(int mode)
{
    const int previous = font_layout_override;
    font_layout_override = mode;
    return previous;
}
int khdays_hd_font_observe(int enabled)
{
    const int previous = observing_font;
    observing_font = enabled;
    return previous;
}
static int font_available(void)
{
    return font_layout_override >= 0 ? font_layout_override : observing_font || khdays_hud_setting(KHDAYS_OPT_FONT);
}
static const char *test_scope = "";
static int test_camp_screen = KHDAYS_CAMP_NONE, test_camp_story = 1;
static char selected_scope[80];
static int command_route, popup_route = 1, test_mappings = 1;
int khdays_camp_screen(int *story)
{
    *story = test_camp_story;
    return test_camp_screen;
}
int khdays_hd_font_mappings_enabled(void)
{
    return font_available() && test_mappings;
}
void khdays_camp_font_scope(uint32_t font, char *scope, int size)
{
    (void)font;
    snprintf(scope, (size_t)size, "%s", test_scope);
}
int khdays_hd_font_select(const char *resource, const char *scope, char *selected, int size)
{
    (void)resource;
    if (!font_available())
        return 0;
    snprintf(selected_scope, sizeof(selected_scope), "%s", scope);
    if ((strcmp(scope, "field.command") == 0 && command_route) ||
        (strcmp(scope, "field.obtained") == 0 && popup_route) || strcmp(scope, "shop") == 0) {
        snprintf(selected, (size_t)size, "@font-route/%s", scope);
        return 1;
    }
    if (strstr(scope, ".mission_select.summary") || strstr(scope, ".mission_select.objectives") ||
        strstr(scope, ".mission_select.rewards")) {
        snprintf(selected, (size_t)size, "@font-route/mission-body");
        return 1;
    }
    if (strncmp(scope, "field.hud.", 10) == 0 || strncmp(scope, "mission.results.", 16) == 0) {
        snprintf(selected, (size_t)size, "@font-route/hud-results");
        return 1;
    }
    if (strcmp(scope, "field.tutorial") == 0) {
        snprintf(selected, (size_t)size, "@font-route/tutorial");
        return 1;
    }
    if (strcmp(scope, "field.dialogue.speaker") == 0) {
        snprintf(selected, (size_t)size, "@font-route/speaker");
        return 1;
    }
    if (strcmp(scope, "camp.story.screen.2") != 0)
        return 0;
    snprintf(selected, (size_t)size, "@font-route/test");
    return 1;
}
static int metric_override, text_mode = KHDAYS_UI_ON;
unsigned short NNS_G2dFontFindGlyphIndex(KhdaysRef f, unsigned ch)
{
    (void)f;
    return ch == 'V' ? 1 : 0;
}
KhdaysRef NNS_G2dFontGetCharWidthsFromIndex(KhdaysRef f, unsigned glyph_index)
{
    (void)f;
    return 512 + glyph_index * 3;
}
static int numeric_layout_enabled;
static double fit_widths[2];
static double fit_offsets[2];
static int native_blits;
static const void *native_hostrefs[64];
static int native_hostref_count;
static void *native_hostref(KhdaysRef ref)
{
    return ref >= 0xffff0000u && ref < 0xffff0040u ? (void *)native_hostrefs[ref - 0xffff0000u] : KHDAYS_W(ref);
}
double khdays_hd_font_fit_layout(int layout, double *offset)
{
    if (!font_available()) {
        if (offset)
            *offset = 0;
        return 0;
    }
    if (offset)
        *offset = layout >= 0 && layout < 2 ? fit_offsets[layout] : 0;
    return layout >= 0 && layout < 2 ? fit_widths[layout] : 0;
}
int khdays_hd_font_ink(const char *path, int index, int w, int h, int ink, double *bounds)
{
    (void)path;
    (void)index;
    (void)w;
    (void)h;
    bounds[0] = 0;
    bounds[1] = ink;
    return 1;
}
void DrawGlyphLine(KhdaysRef canvas_ref, KhdaysRef font_ref, int x, int y, int colour, KhdaysRef glyph_ref)
{
    (void)font_ref;
    (void)x;
    (void)y;
    (void)glyph_ref;
    DsCanvas *ds = native_hostref(canvas_ref);
    Canvas shape = {0};
    shape.bpp = ds->dst_bpp;
    shape.param = (int)ds->param;
    ++native_blits;
    for (int py = 0; py < 8; ++py)
        for (int px = 0; px < 4; ++px)
            put_pixel(&shape, native_hostref(ds->char_base), px, py, (unsigned)colour & 15u);
}
static int glyph_width_override = -1;
static int large_geometry;
static unsigned int available_glyphs = 1u;
static int baseline_shift;
int khdays_hd_font_baseline(const char *path, int index, int days_baseline)
{
    (void)path;
    (void)index;
    (void)days_baseline;
    return font_available() ? baseline_shift : 0;
}
double khdays_hd_font_baseline_f(const char *path, int index, int days_baseline)
{
    return khdays_hd_font_baseline(path, index, days_baseline);
}
int khdays_ui_mode(int part)
{
    (void)part;
    return text_mode;
}
static int mapping_ready = 1, mapping_count;
static uint32_t mapped_codes[4];
int khdays_hd_font_map_ready(const char *path)
{
    (void)path;
    return mapping_ready;
}
void khdays_hd_font_bind_charmap(const char *path, const uint32_t *codes, int count)
{
    (void)path;
    mapping_count = count;
    if (count <= 4)
        memcpy(mapped_codes, codes, (size_t)count * sizeof(uint32_t));
}
int khdays_hd_font_metric(const char *path, int index, int field, int fallback)
{
    if (!font_available())
        return fallback;
    if (field == KHDF_SIZED)
        return large_geometry;
    if (path && index == 0 && field == KHDF_WIDTH && glyph_width_override >= 0)
        return glyph_width_override;
    if (path && large_geometry) {
        if (field == KHDF_CELL_WIDTH || field == KHDF_WIDTH)
            return 18;
        if (field == KHDF_CELL_HEIGHT)
            return 24;
    }
    static const int metrics[] = {-2, 13, 11, 17};
    return path && metric_override && index <= 0 && field >= 0 && field < 4 ? metrics[field] : fallback;
}
int khdays_hd_font_geometry(const char *path, int index, int days_w, int days_h, int ink_w, int *out)
{
    (void)path;
    (void)index;
    (void)days_w;
    (void)days_h;
    (void)ink_w;
    if (!large_geometry)
        return 0;
    out[0] = 18;
    out[1] = 24;
    out[2] = 18;
    return 1;
}
int khdays_hd_font_kerning(const char *path, unsigned int left, unsigned int right)
{
    return font_available() && path && metric_override && left == 'A' && right == 'V' ? -2 : 0;
}
double khdays_hd_font_metric_f(const char *path, int index, int field, double fallback)
{
    return khdays_hd_font_metric(path, index, field, (int)fallback);
}
double khdays_hd_font_kerning_f(const char *path, unsigned int left, unsigned int right)
{
    return khdays_hd_font_kerning(path, left, right);
}
double khdays_hd_font_extent_f(const char *path, int index, int left, int width, int advance)
{
    return khdays_hd_font_extent(path, index, left, width, advance);
}
int khdays_hd_font_extent(const char *path, int index, int left, int width, int advance)
{
    (void)path;
    (void)index;
    (void)left;
    (void)width;
    return advance;
}
void khdays_origin_want(void) {}
int khdays_origin_of_ram(uint32_t at, const char **name, uint32_t *offset, uint32_t *run)
{
    if (!font_origin || at != font_origin_info)
        return 0;
    *name = font_origin;
    *offset = font_origin_offset;
    *run = 32;
    return 1;
}
static uint32_t *test_images[32];
static int image_ends, paint_calls, failures;
static int image_requests, test_scale = 1, fail_direct_images;
static int test_hofs, test_vofs, test_colors256;
static int virtual_caption_scroll;
int khdays_ui_information_text_scroll(int *x, int *y)
{
    if (!virtual_caption_scroll)
        return 0;
    *x = test_hofs;
    *y = test_vofs + 216;
    return 1;
}
static int test_map_bg = -1, test_bg_colors256[4] = {-1, -1, -1, -1};
static size_t image_bytes[32], live_bytes, peak_bytes;
static uint16_t map_cells[1024] = {1, 2};
static int map_width = 2, map_height = 1;
static KhdaysUiElement submissions[128];
static int submission_count;
static unsigned char suppressed[32 * 32];
static unsigned char suppressed_layers[4][32 * 32];
static uint32_t tint = 0xff123456u;
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

KhdaysRef khdays_reference(const void *p)
{
    /* the snapshots' canvas and pixels on the stack (fitted glyphs, buttons' glyphs) */
    if ((uintptr_t)p < (uintptr_t)ram || (uintptr_t)p >= (uintptr_t)ram + ram_bytes) {
        const int slot = native_hostref_count++ % 64;
        native_hostrefs[slot] = p;
        return 0xffff0000u + (unsigned)slot;
    }
    return (KhdaysRef)((const unsigned char *)p - ram);
}
void khdays_log(const char *format, ...)
{
    (void)format;
}
int khdays_hd_font_has(const char *path)
{
    ++font_probes;
    return font_available() && path && strcmp(path, "text/missing.nftr") != 0;
}
int khdays_hd_font_numeric_layout(int layout)
{
    (void)layout;
    return numeric_layout_enabled;
}
int khdays_hd_font_glyph_available(const char *path, int index)
{
    (void)path;
    return font_available() && index >= 0 && index < 32 && (available_glyphs & (1u << index)) != 0;
}
/* the player's A as a 2x2 red icon while button_icon_on; the buttons asked for, a bit each */
static int button_icon_on;
static uint32_t button_signature, buttons_asked;
static const uint8_t button_icon_red[2 * 2 * 4] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
const uint8_t *khdays_button_icon(int button, int field, int *width, int *height)
{
    (void)field;
    buttons_asked |= 1u << button;
    if (!button_icon_on || button != KHDAYS_BUTTON_A)
        return NULL;
    *width = *height = 2;
    return button_icon_red;
}
uint32_t khdays_button_icons_signature(void)
{
    return button_signature;
}
int khdays_ui_scale(void)
{
    return test_scale;
}
void khdays_ui_register(int part, void (*fn)(void))
{
    (void)part;
    (void)fn;
}
void khdays_ui_emit(int part, const KhdaysUiElement *e)
{
    (void)part;
    if (submission_count < 128)
        submissions[submission_count++] = *e;
}
void khdays_ui_emit_following(int part, const KhdaysUiElement *e, const KhdaysUiElement *anchor)
{
    (void)anchor;
    khdays_ui_emit(part, e);
}
uint32_t khdays_ui_palette_hash(int engine)
{
    ++palette_hashes;
    (void)engine;
    return tint;
}
void khdays_ui_left_out_cell(int part, int screen, int bg, int x, int y)
{
    (void)part;
    (void)screen;
    if (x >= 0 && x < 32 && y >= 0 && y < 32) {
        suppressed[y * 32 + x] = 1;
        if (bg >= 0 && bg < 4)
            suppressed_layers[bg][y * 32 + x] = 1;
    }
}
uint32_t khdays_ui_palette_colour(int engine, uint32_t slot, uint32_t indexed)
{
    ++palette_reads;
    (void)engine;
    return tint ^ ((indexed & 0xf0u) << 8) ^ slot_tint[slot];
}
const uint8_t *khdays_bg_memory(int engine, uint32_t at)
{
    (void)engine;
    return at < sizeof(vram) ? vram + at : NULL;
}
uint32_t khdays_bg_chars_base(int engine, int bg)
{
    (void)engine;
    (void)bg;
    return 0;
}
int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *l)
{
    (void)engine;
    memset(l, 0, sizeof(*l));
    l->map = (uint32_t)bg;
    l->wt = l->ht = 32;
    l->hofs = test_hofs;
    l->vofs = test_vofs;
    l->colors256 = test_bg_colors256[bg] < 0 ? test_colors256 : test_bg_colors256[bg];
    return 1;
}
uint32_t khdays_bg_map_entry(int engine, const KhdaysBgLayer *l, int x, int y)
{
    (void)engine;
    if (test_map_bg >= 0 && l->map != (uint32_t)test_map_bg)
        return 0;
    x = (x % 32 + 32) % 32;
    y = (y % 32 + 32) % 32;
    return y >= 0 && y < map_height && x >= 0 && x < map_width ? map_cells[y * map_width + x] : 0;
}
int khdays_ui_image_get(int w, int h)
{
    ++image_requests;
    if (fail_direct_images && (canvas_count && canvases[0].image >= 0))
        return -1;
    const size_t bytes = (size_t)w * h * sizeof(uint32_t);
    if (bytes + live_bytes > 96u * 1024u * 1024u)
        return -1;
    for (int i = 0; i < 32; ++i)
        if (!test_images[i]) {
            test_images[i] = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
            image_bytes[i] = bytes;
            live_bytes += bytes;
            if (live_bytes > peak_bytes)
                peak_bytes = live_bytes;
            return i;
        }
    return -1;
}
void khdays_ui_image_put(int image)
{
    if (image >= 0) {
        free(test_images[image]);
        live_bytes -= image_bytes[image];
        image_bytes[image] = 0;
        test_images[image] = NULL;
    }
}
uint32_t *khdays_ui_image_begin(int image)
{
    return image >= 0 ? test_images[image] : NULL;
}
void khdays_ui_image_end(int image)
{
    (void)image;
    ++image_ends;
}
int khdays_hd_font_paint_clipped(const char *path, int index, double x, double y, int w, int h, uint32_t colour,
                                 uint32_t *out, int width, int height, int scale, int l, int t, int r, int b)
{
    (void)path;
    (void)index;
    (void)height;
    if (!font_available())
        return 0;
    ++paint_calls;
    /* A replacement on the RIGHT half: original ink is on the LEFT half. */
    for (int py = t * scale; py < b * scale; ++py)
        for (int px = l * scale; px < r * scale; ++px)
            if (px >= (x + w / 2) * scale && px < (x + w) * scale && py >= y * scale && py < (y + h) * scale)
                out[(size_t)py * width + px] = colour;
    return 1;
}

int khdays_hd_font_paint_fitted(const char *path, int index, double x, double y, int w, int h, uint32_t colour,
                                uint32_t *out, int width, int height, int scale, int l, int t, int r, int b,
                                double factor)
{
    (void)path;
    (void)index;
    (void)height;
    if (!font_available())
        return 0;
    ++paint_calls;
    for (int py = t * scale; py < b * scale; ++py)
        for (int px = l * scale; px < r * scale; ++px)
            if (px >= (x + w * factor / 2) * scale && px < (x + w * factor) * scale && py >= y * scale &&
                py < (y + h) * scale)
                out[(size_t)py * width + px] = colour;
    return 1;
}

static DsCanvas *cc;
static Canvas *c;
static uint32_t *font, *glyph;
static KhdaysBgLayer layer;

static uint32_t primary_hd_pixel(size_t offset)
{
    return c->hd_image >= 0 ? test_images[c->hd_image][offset] : 0;
}

static void setup(int bpp)
{
    khdays_window = ram;
    khdays_ui_text_crawl_font(NULL);
    obtained_reset();
    while (canvas_count)
        forget(&canvases[0]);
    memset(ram, 0, ram_bytes);
    memset(vram, 0, sizeof(vram));
    memset(map_cells, 0, sizeof(map_cells));
    memset(suppressed, 0, sizeof(suppressed));
    memset(graphics_map, 0, sizeof(graphics_map));
    memset(&graphics_replacement, 0, sizeof(graphics_replacement));
    fail_direct_images = 0;
    memset(slot_tint, 0, sizeof(slot_tint));
    submission_count = 0;
    test_hofs = test_vofs = 0;
    test_map_bg = -1;
    for (int bg = 0; bg < 4; ++bg)
        test_bg_colors256[bg] = -1;
    test_scale = 1;
    test_colors256 = bpp == 8;
    metric_override = 0;
    mapping_ready = 1;
    mapping_count = 0;
    glyph_width_override = -1;
    large_geometry = 0;
    available_glyphs = 1u;
    baseline_shift = 0;
    text_mode = KHDAYS_UI_ON;
    font_origin = NULL;
    font_origin_info = font_origin_offset = 0;
    map_width = 2;
    map_height = 1;
    map_cells[0] = 1;
    map_cells[1] = 2;
    cc = (DsCanvas *)(ram + 16);
    cc->char_base = 1024;
    cc->area_width = 2;
    cc->area_height = 1;
    cc->param = 2;
    cc->dst_bpp = (uint8_t)bpp;
    font = (uint32_t *)(ram + 64);
    font[0] = 128;
    *(uint32_t *)(ram + 128 + 8) = 256;
    ram[256] = 8;
    ram[257] = 8;
    ram[258] = 8;
    glyph = (uint32_t *)(ram + 600);
    glyph[0] = 512;
    glyph[1] = 264;
    ram[513] = 4; /* original glyphWidth intentionally less than the PNG cell */
    binding_count = 0;
    bind_font(font, "text/synthetic.nftr");
    c = canvas_of(cc, "test");
    khdays_bg_text_layer(0, 0, &layer);
    layer.colors256 = bpp == 8;
    if (bpp == 8) {
        map_cells[0] = 1;
        map_cells[1] = 2;
    }
}

static void draw_xy(int x, int y)
{
    khdays_ui_text_glyph_before(cc, font, x, y, 1, glyph);
    for (int py = y; py < y + 8; ++py)
        for (int px = x; px < x + 4; ++px)
            put_pixel(c, KHDAYS_W(cc->char_base), px, py, 1);
    khdays_ui_text_glyph(cc, font, x, y, 1, glyph);
}
static void draw(int x)
{
    draw_xy(x, 0);
}
static void upload(uint32_t at, uint32_t size)
{
    const uint32_t dest = 8u * c->bpp;
    memcpy(vram + dest + at, ram + 1024 + at, size);
    chars_loaded(0, 0, ram + 1024 + at, dest + at, size);
}
static void render(void)
{
    observe_vram(c);
    make_image(c, &layer, 0, 0);
    build_hd(c, 1, khdays_ui_palette_hash(c->engine));
    make_image(c, &layer, 0, 0);
}

static void scroll_text(int amount)
{
    const void *window = (const uint8_t *)cc - RENDERER_CANVAS;
    khdays_ui_text_scroll_before(window, amount);
    const int height = c->area_h * 8;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < c->area_w * 8; ++x)
            put_pixel(c, ram + 1024, x, y, y + amount < height ? pixel(c, ram + 1024, x, y + amount) : 0);
    khdays_ui_text_clear_area(cc, 0, 0, height - amount, c->area_w * 8, amount);
    khdays_ui_text_memory_clear(ram + 1024 + c->bytes - 1, 1);
    khdays_ui_text_scroll(window, amount);
}
static int displayed_at(int x, int y)
{
    for (int i = 0; i < c->display_count; ++i) {
        const GlyphOp *a = &c->display_ops[i];
        if (x >= a->clip_l && x < a->clip_r && y >= a->clip_t && y < a->clip_b)
            return 1;
    }
    return 0;
}

static int submitted_hd_pixel(int x, int y, uint32_t colour)
{
    for (int i = 0; i < submission_count; ++i) {
        const KhdaysUiElement *e = &submissions[i];
        if (!(e->slot & KES_DIRECT) || x < e->x || y < e->y || x >= e->x + e->w || y >= e->y + e->h)
            continue;
        const int sx = (x - (int)e->x) * test_scale, sy = (y - (int)e->y) * test_scale;
        if (test_images[e->image][sy * (int)e->w * test_scale + sx] == colour)
            return 1;
    }
    return 0;
}

static void png_sheet(int indexed)
{
    graphics_replacement.width = 32;
    graphics_replacement.height = 16;
    graphics_replacement.rgba = graphics_pixels;
    graphics_replacement.info =
        KHDAYS_REPLACEMENT_TILES | (2u << 8) | (2u << 16) | (indexed ? KHDAYS_REPLACEMENT_INDEXED : 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x)
            graphics_pixels[y * 32 + x] = indexed            ? (uint32_t)((x + y) % 3)
                                          : (x + y) % 3 == 0 ? 0
                                          : (x + y) % 3 == 1 ? 0x80402010u
                                                             : 0xff806040u;
}

static void native_planes(void)
{
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        setup(bpp);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                put_pixel(c, ram + 1024, x, y, (unsigned)(x + y * 3) & (bpp == 4 ? 15u : 255u));
        upload(0, c->bytes);
        map_cells[0] |= 0x1400u;
        emit();
        uint32_t expected[16 * 8];
        memcpy(expected, test_images[c->image], sizeof(expected));
        const int image = c->image, uploads = image_ends;
        for (int scale = 2; scale <= 8; ++scale) {
            test_scale = scale;
            emit();
            CHECK(c->image == image && image_bytes[image] == sizeof(expected));
            CHECK(memcmp(test_images[image], expected, sizeof(expected)) == 0);
            CHECK(image_ends == uploads);
        }
    }
}

static void graphics_replacements(void)
{
    /* Initial baked graphics need their PNG even without captured text. */
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        for (int indexed = 0; indexed <= 1; ++indexed) {
            setup(bpp);
            png_sheet(indexed);
            memset(ram + 1024, 0x11, c->bytes);
            upload(0, c->bytes);
            graphics_map[0][c->vram / 32] = (1u << 20) | 1u;
            for (int scale = 1; scale <= 8; scale *= 2) {
                test_scale = scale;
                for (uint16_t flip = 0; flip <= 0xc00u; flip += 0x400u) {
                    map_cells[0] = 1u | flip | 0x3000u;
                    emit();
                    CHECK(c->display_count == 0 && c->hd_image >= 0);
                    CHECK(c->graphics_count == 1 && suppressed[0]);
                    for (int y = 0; y < 8 * scale; ++y) {
                        for (int x = 0; x < 8 * scale; ++x) {
                            const int sx = (2 * x + 1) / scale, sy = (2 * y + 1) / scale;
                            const int ux = 16 + (flip & 0x400u ? 15 - sx : sx);
                            const int uy = flip & 0x800u ? 15 - sy : sy;
                            uint32_t colour = graphics_pixels[uy * 32 + ux];
                            if (indexed && colour)
                                colour = tint ^ (bpp == 4 ? 0x3000u : 0);
                            CHECK(test_images[c->image][(y / scale) * 16 + x / scale] == 0);
                            CHECK(primary_hd_pixel((size_t)y * 16 * scale + x) == colour);
                        }
                    }
                    const int uploads = image_ends, paints = paint_calls;
                    emit();
                    CHECK(image_ends == uploads && paint_calls == paints);
                }
            }
            /* Palette-only changes refresh indexed artwork, not its geometry. */
            const uint32_t saved = tint;
            const int palette_uploads = image_ends;
            tint ^= 0x00100000u;
            emit();
            if (indexed)
                CHECK(submitted_hd_pixel(0, 0, tint ^ (bpp == 4 ? 0x3000u : 0)));
            else
                CHECK(image_ends == palette_uploads);
            tint = saved;
            /* Switching HD graphics off restores native data without stale PNGs. */
            graphics_map[0][c->vram / 32] = 0;
            emit();
            CHECK(c->graphics_count == 0 && c->hd_image < 0);
            CHECK(test_images[c->image][0] ==
                  (bpp == 4 ? KET_OPAQUE | 0x31u : KET_OPAQUE | KET_EXTENDED | (3u << KET_EXT_PAL_SHIFT) | 0x11u));
        }
    }
    /* Four-bit BGs select the low nibble of an indexed replacement's sample. */
    setup(4);
    png_sheet(1);
    graphics_pixels[32 + 17] = 0x71u;
    upload(0, c->bytes);
    graphics_map[0][1] = (1u << 20) | 1u;
    map_cells[0] |= 0x2000u;
    emit();
    CHECK(primary_hd_pixel(0) == (tint ^ 0x2000u));
    /* Results use engine B; mappings must follow the engine and actual BG slot. */
    setup(4);
    png_sheet(0);
    memcpy(vram + 32, ram + 1024, c->bytes);
    khdays_ui_text_chars_b2(ram + 1024, 32, c->bytes);
    graphics_map[0][1] = 1u << 20;
    graphics_map[1][1] = (1u << 20) | 1u;
    emit();
    CHECK(c->engine == 1 && c->bg == 2);
    CHECK(c->hd_image >= 0 && primary_hd_pixel(1) == graphics_pixels[32 + 19]);
    /* An unavailable RGBA image must leave the ordinary renderer in charge. */
    setup(4);
    png_sheet(0);
    upload(0, c->bytes);
    graphics_map[0][c->vram / 32] = 1u << 20;
    fail_direct_images = 1;
    emit();
    CHECK(c->hd_image < 0 && !suppressed[0]);
    CHECK(test_images[c->image][0] == 0);
    fail_direct_images = 0;
    emit();
    CHECK(c->hd_image >= 0 && suppressed[0]);
    if (!hd_enabled())
        return;

    /* Reproduce a partial ordinary upload beside a retained HD glyph. */
    setup(4);
    png_sheet(0);
    test_scale = 2;
    draw(0);
    upload(0, c->bytes);
    emit();
    memset(ram + 2048, 0x22, 32);
    memcpy(vram + 64, ram + 2048, 32);
    khdays_ui_text_chars_a0(ram + 2048, 64, 32);
    graphics_map[0][2] = (1u << 20) | 1u;
    emit();
    CHECK(c->display_count == 1 && submitted_hd_pixel(7, 0, tint));
    CHECK(test_images[c->image][8] == 0);
    CHECK(primary_hd_pixel(16) == graphics_pixels[16]);
    CHECK(primary_hd_pixel(17) == graphics_pixels[17]);
    CHECK(primary_hd_pixel(18) == 0); /* PNG transparency never exposes native ink. */
    const int uploads = image_ends, paints = paint_calls;
    emit();
    CHECK(image_ends == uploads && paint_calls == paints);
    graphics_map[0][2] = 1u << 20; /* New mapping, unchanged native bytes. */
    emit();
    CHECK(primary_hd_pixel(17) == graphics_pixels[1]);
    CHECK(submitted_hd_pixel(7, 0, tint));
    khdays_ui_text_freed(NULL, ram + 1024);
    CHECK(c->detached);
    emit();
    CHECK(primary_hd_pixel(17) == graphics_pixels[1] && submitted_hd_pixel(7, 0, tint));
    /* A full overwrite ends retention and returns all graphics to native submission. */
    memset(ram + 2048, 0x22, c->bytes);
    memcpy(vram + 32, ram + 2048, c->bytes);
    khdays_ui_text_chars_a0(ram + 2048, 32, c->bytes);
    submission_count = 0;
    memset(suppressed, 0, sizeof(suppressed));
    emit();
    CHECK(canvas_count == 0 && submission_count == 0 && !suppressed[0] && !suppressed[1]);

    /* Text may overlay graphics; supplementary placements keep both PNGs. */
    setup(4);
    png_sheet(0);
    test_scale = 2;
    draw(0);
    draw(8);
    upload(0, c->bytes);
    graphics_map[0][2] = (1u << 20) | 1u;
    map_width = 5;
    map_height = 4;
    memset(map_cells, 0, sizeof(map_cells));
    map_cells[0] = 1;
    map_cells[1] = 2;
    map_cells[2 * 5 + 3] = 2 | 0xc00;
    emit();
    CHECK(c->extra && c->extra->source[2 * EXTRA_COLS + 3] == 1);
    CHECK(primary_hd_pixel(16) == graphics_pixels[16]);
    CHECK(submitted_hd_pixel(15, 0, tint));
    CHECK(submitted_hd_pixel(24, 16, tint)); /* Flipped glyph on the left. */
    CHECK(submitted_hd_pixel(31, 16, graphics_pixels[15 * 32 + 17]));
    test_hofs = 3;
    test_vofs = 5;
    submission_count = 0;
    emit();
    CHECK(submitted_hd_pixel(21, 11, tint));
    CHECK(submitted_hd_pixel(28, 11, graphics_pixels[15 * 32 + 17]));
    khdays_ui_image_put(c->extra->hd_image);
    c->extra->hd_image = -1;
    fail_direct_images = 1;
    memset(suppressed, 0, sizeof(suppressed));
    emit();
    CHECK(c->extra->hd_image < 0 && !suppressed[2 * 32 + 3]);
    CHECK(test_images[c->extra->image][16 * EXTRA_COLS * 8 + 24] == 0);
    fail_direct_images = 0;
    emit();
    CHECK(c->extra->hd_image >= 0 && suppressed[2 * 32 + 3]);

    /* Adjacent canvas owners must not paint each other's translucent pixels. */
    uint32_t left[16 * 16] = {0}, right[16 * 16] = {0};
    c->owned[1] = UINT64_C(0x0f0f0f0f0f0f0f0f);
    underlay_graphics(c, 1, 0, 2, left, 16);
    c->owned[1] = ~c->owned[1];
    underlay_graphics(c, 1, 0, 2, right, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            CHECK(!(left[y * 16 + x] && right[y * 16 + x]));
            CHECK((left[y * 16 + x] | right[y * 16 + x]) == graphics_pixels[y * 32 + 16 + x]);
        }
    CHECK(over_graphics(0x800000ffu, 0x80ff0000u) == 0xc05500aau);
    printf("Ordinary PNG graphics in font canvases: %s\n", failures ? "FAIL" : "PASS");
}

/* The game's header layout: one 8x24 canvas, three 8x2 strips, selected
   indent two cells instead of one. Only tilemap entries change on selection. */
static void command_menu_selections(int primary_row)
{
    setup(4);
    cc->area_width = cc->param = 8;
    cc->area_height = 24;
    c = canvas_of(cc, "command row test");
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 8; ++col)
            draw_xy(col * 8, (row == 0 ? primary_row : row + 2) * 16 + 3);
    memcpy(vram + 0xd0 * 32, ram + 1024, c->bytes);
    chars_loaded(0, 1, ram + 1024, 0xd0 * 32, c->bytes);
    CHECK(c->display_count == 24);
    khdays_ui_text_freed(NULL, ram + 1024);
    CHECK(c->detached);
    memset(ram + 1024, 0xee, 6144);
    map_width = map_height = 32;
    uint16_t items_entries[16] = {0};
    for (int cycle = 0; cycle < 300; ++cycle) {
        const int selected = cycle % 3;
        memset(map_cells, 0, sizeof(map_cells));
        for (int row = 0; row < 3; ++row) {
            const int indent = row == selected ? 2 : 1;
            const int dst_y = 18 + row * 2;
            const int tile = 0xd0 + (row == 0 ? primary_row : row + 2) * 16;
            for (int dy = 0; dy < 2; ++dy)
                for (int col = 0; col < 8; ++col)
                    map_cells[(dst_y + dy) * 32 + indent + col] = (uint16_t)((tile + dy * 8 + col) | 0xf000);
        }
        for (int dy = 0; dy < 2; ++dy) {
            for (int col = 0; col < 8; ++col) {
                const uint16_t value = map_cells[(22 + dy) * 32 + 1 + col];
                if (selected == 0)
                    items_entries[dy * 8 + col] = value;
                if (selected == 1)
                    CHECK(items_entries[dy * 8 + col] == value);
            }
        }
        memset(suppressed, 0, sizeof(suppressed));
        submission_count = 0;
        emit();
        CHECK(c->extra != NULL);
        int ox = -1, oy = -1;
        CHECK(shown_at(c, &layer, &ox, &oy));
        for (int row = 0; row < 3; ++row) {
            const int indent = row == selected ? 2 : 1;
            const int source_y = (row == 0 ? primary_row : row + 2) * 2;
            const int origin_y = 18 + row * 2 - source_y;
            for (int dy = 0; dy < 2; ++dy)
                for (int col = 0; col < 8; ++col) {
                    const int x = indent + col, y = 18 + row * 2 + dy;
                    const int k = (source_y + dy) * 8 + col;
                    CHECK(canvas_char(c, &layer, map_cells[y * 32 + x]) == k);
                    CHECK(suppressed[y * 32 + x]);
                    if (ox == indent && oy == origin_y) {
                        CHECK(c->cells[k] == 0xf000 && c->hd_image >= 0);
                    } else {
                        CHECK(c->extra && c->extra->source[y * EXTRA_COLS + x] == k);
                        CHECK(c->extra && c->extra->cells[y * EXTRA_COLS + x] == 0xf000);
                        CHECK(c->extra && c->extra->hd_image >= 0);
                    }
                }
            CHECK(!submitted_hd_pixel(indent * 8 + 1, (18 + row * 2) * 8 + 3, tint ^ 0xf000u));
            CHECK(submitted_hd_pixel(indent * 8 + 7, (18 + row * 2) * 8 + 3, tint ^ 0xf000u));
        }
        CHECK(c->display_count == 24);
        CHECK(c->engine == 0 && c->bg == 1 && c->vram == 0x1a00);
        const int stable_uploads = image_ends, stable_paints = paint_calls;
        submission_count = 0;
        emit();
        CHECK(image_ends == stable_uploads && paint_calls == stable_paints);
    }
    /* Empty selections remove references while published HD glyphs still exist. */
    uint16_t saved_map[1024];
    memcpy(saved_map, map_cells, sizeof(saved_map));
    memset(map_cells, 0, sizeof(map_cells));
    submission_count = 0;
    memset(suppressed, 0, sizeof(suppressed));
    emit();
    CHECK(submission_count == 0 && c->extra == NULL && c->display_count == 24);
    for (int i = 0; i < 1024; ++i)
        CHECK(!suppressed[i]);
    memcpy(map_cells, saved_map, sizeof(saved_map));
    emit();
    /* Palette changes must reach every placement, including shared row origins. */
    tint ^= 0x00101010u;
    submission_count = 0;
    emit();
    for (int row = 0; row < 3; ++row)
        CHECK(submitted_hd_pixel((row == 2 ? 2 : 1) * 8 + 7, (18 + row * 2) * 8 + 3, tint ^ 0xf000u));
    /* Overwriting Magic must clear it without losing Talk or unchanged Items. */
    memset(vram + c->vram + 48 * 32, 0, 16 * 32);
    submission_count = 0;
    emit();
    CHECK(c->display_count == 16);
    CHECK(submitted_hd_pixel(15, 18 * 8 + 3, tint ^ 0xf000u));
    CHECK(!submitted_hd_pixel(15, 20 * 8 + 3, tint ^ 0xf000u));
    CHECK(submitted_hd_pixel(23, 22 * 8 + 3, tint ^ 0xf000u));
    /* A VRAM overwrite invalidates every cached placement of the same glyphs. */
    memset(vram + c->vram, 0, c->bytes);
    submission_count = 0;
    emit();
    CHECK(c->display_count == 0);
    for (int i = 0; i < submission_count; ++i)
        CHECK(!(submissions[i].slot & KES_DIRECT));
}

static void obtained_popup(void)
{
    {
        Canvas panel = {0};
        KhdaysBgLayer panel_layer = {0};
        panel.engine = 1;
        panel.bg = 0;
        mission_panel_owned = 1;
        CHECK(obtained_cell(&panel, &panel_layer, 1, 20));
        CHECK(!obtained_cell(&panel, &panel_layer, 1, 10));
        panel.bg = 1;
        CHECK(!obtained_cell(&panel, &panel_layer, 1, 20));
        mission_panel_owned = 0;
        panel.bg = 0;
        CHECK(!obtained_cell(&panel, &panel_layer, 1, 20));
    }

    ram_bytes = 0x02400000u;
    ram = (unsigned char *)calloc(1, ram_bytes);
    CHECK(ram != NULL);
    if (!ram)
        exit(1);
    setup(4);
    *(uint32_t *)KHDAYS_W(0x0207f620u) = 0x02100000u;
    *(uint16_t *)KHDAYS_W(0x021004aeu) = 24;
    *(uint16_t *)KHDAYS_W(0x021004b8u) = 123;
    cc->area_width = cc->param = 8;
    cc->area_height = 2;
    c = canvas_of(cc, "command item cache");
    command_route = 1;
    khdays_ui_text_scope_push("field.command");
    draw(0);
    khdays_ui_text_scope_pop();
    CHECK(c->command_cache && strcmp(c->ops[0].resource, "@font-route/field.command") == 0);
    const uint32_t address = 0x250u * 32;
    memcpy(vram + address, ram + 1024, c->bytes);
    chars_loaded(0, 1, ram + 1024, address, c->bytes);
    CHECK(khdays_ui_text_obtained_wants(0, 1));
    cc = (DsCanvas *)(ram + 8200);
    *cc = *(const DsCanvas *)(ram + 16);
    cc->char_base = 9000;
    c = canvas_of(cc, "private pickup");
    khdays_ui_text_scope_push("field.obtained");
    draw(16); /* The pickup's different advance/bearing puts its artwork elsewhere. */
    glyph[1] += 8;
    draw(32); /* A missing PNG glyph retains the real native write in this private layout. */
    glyph[1] -= 8;
    khdays_ui_text_scope_pop();
    uint32_t *surface = (uint32_t *)(ram + 8000);
    surface[6] = 8192;
    const int count = canvas_count;
    khdays_ui_text_obtained_capture(surface);
    CHECK(canvas_count == count - 1 && obtained.canvas.display_count == 1);
    CHECK(strcmp(obtained.canvas.display_ops[0].resource, "@font-route/field.obtained") == 0);
    khdays_ui_text_freed(NULL, ram + 9000);
    cc = (DsCanvas *)(ram + 16);
    c = find(KHDAYS_REF(cc));
    CHECK(c && strcmp(c->display_ops[0].resource, "@font-route/field.command") == 0);
    map_width = map_height = 32;
    test_map_bg = 1;
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 8; ++col) {
            map_cells[row * 32 + col + 12] = (uint16_t)(0x250 + row * 8 + col);
            map_cells[(row + 8) * 32 + col + 1] = (uint16_t)(0x250 + row * 8 + col);
        }
    submission_count = 0;
    emit();
    CHECK(obtained.ready);
    CHECK(submitted_hd_pixel(100, 0, tint));
    CHECK(submitted_hd_pixel(28, 64, tint));
    CHECK(!submitted_hd_pixel(12, 64, tint));
    CHECK(test_images[obtained.canvas.image][32] == (KET_OPAQUE | 1));
    CHECK(test_images[obtained.canvas.image][16] == 0);
    const int paints = paint_calls, requests = image_requests, updates = image_ends;
    for (int frame = 0; frame < 20; ++frame) {
        CHECK(!khdays_ui_text_obtained_wants(0, 1));
        submission_count = 0;
        emit();
    }
    CHECK(paint_calls == paints && image_requests == requests && image_ends == updates);
    test_hofs = 3;
    test_vofs = 5;
    submission_count = 0;
    emit();
    CHECK(submitted_hd_pixel(25, 59, tint));
    map_cells[8 * 32 + 1] = 0; /* Partial tilemap clears must immediately remove the private overlay. */
    submission_count = 0;
    emit();
    CHECK(!obtained.ready && !submitted_hd_pixel(25, 59, tint));
    map_cells[8 * 32 + 1] = 0x250;
    emit();
    CHECK(obtained.ready);
    graphics_map[0][address / 32] = 1u << 20;
    emit();
    CHECK(!obtained.ready); /* An ordinary PNG mapping always wins. */
    graphics_map[0][address / 32] = 0;
    emit();
    CHECK(obtained.ready);
    external_range(c, address, 1, 0); /* Even an identical-byte explicit overwrite ends the private capture. */
    emit();
    CHECK(!obtained.ready);
    chars_loaded(0, 1, ram + 1024, address, c->bytes);
    emit();
    CHECK(obtained.ready); /* Republishing the real item cache restores the existing layout. */
    khdays_ui_text_clear(cc, 0);
    chars_loaded(0, 1, ram + 1024, address, c->bytes);
    emit();
    CHECK(!obtained.ready); /* Republishing an explicitly cleared source cannot revive old popup text. */
    *(uint16_t *)KHDAYS_W(0x021004b8u) = 456; /* The same slot in another field can name a different item. */
    emit();
    CHECK(!obtained.ready);
    CHECK(khdays_ui_text_obtained_wants(0, 1));
    CHECK(obtained.key == 456);
    CHECK(!khdays_ui_text_obtained_wants(0xffffu, 1));
    CHECK(!obtained.active && obtained.canvas.image < 0);
    *(uint32_t *)KHDAYS_W(0x0207f620u) = 0;
    CHECK(!khdays_ui_text_obtained_wants(0, 1));
    *(uint32_t *)KHDAYS_W(0x0207f620u) = 0x02100000u;
    command_route = popup_route = 0;
    CHECK(!khdays_ui_text_obtained_wants(0, 1));
    CHECK(!obtained.active);
    /* Shared fonts still need separate captures for independent fitting rules. */
    test_mappings = 0;
    fit_widths[0] = 53;
    CHECK(khdays_ui_text_obtained_wants(0, 1));
    obtained_reset();
    fit_widths[1] = 53;
    CHECK(!khdays_ui_text_obtained_wants(0, 1));
    fit_offsets[1] = 1;
    CHECK(khdays_ui_text_obtained_wants(0, 1));
    obtained_reset();
    fit_widths[0] = fit_widths[1] = fit_offsets[1] = 0;
    fit_widths[1] = 64;
    CHECK(khdays_ui_text_obtained_wants(0, 1));
    obtained_reset();
    fit_widths[1] = 0;
    /* A sequence requests a private native-font capture even with identical font routes. */
    obtained_sequence_enabled = 1;
    available_glyphs = 0;
    CHECK(khdays_ui_text_obtained_wants(0, 1));
    cc = (DsCanvas *)(ram + 8200);
    *cc = *(const DsCanvas *)(ram + 16);
    cc->char_base = 9000;
    c = canvas_of(cc, "sequence pickup");
    khdays_ui_text_scope_push("field.obtained");
    draw(0);
    khdays_ui_text_scope_pop();
    CHECK(c->op_count && c->ops[0].native_glyph);
    khdays_ui_text_obtained_capture(surface);
    c = find(KHDAYS_REF(ram + 16));
    cc = (DsCanvas *)(ram + 16);
    khdays_ui_text_scope_push("field.command");
    draw(0);
    khdays_ui_text_scope_pop();
    memcpy(vram + address, ram + 1024, c->bytes);
    chars_loaded(0, 1, ram + 1024, address, c->bytes);
    memset(suppressed_layers, 0, sizeof(suppressed_layers));
    const uint16_t saved_item_cell = map_cells[8 * 32 + 1];
    map_cells[8 * 32 + 1] = 0; /* INFORMATION returns BG3 before the aliased BG1 item tiles. */
    const int before_restore = obtained_sequence_calls;
    emit();
    CHECK(!obtained.ready && obtained_sequence_calls == before_restore + 1 && obtained_sequence_ink);
    map_cells[8 * 32 + 1] = saved_item_cell;
    emit();
    CHECK(obtained_sequence_calls && obtained_sequence_ink);
    CHECK(suppressed[7 * 32] && suppressed[9 * 32 + 12] && !suppressed[9 * 32 + 13] && !suppressed[10 * 32 + 12]);
    for (int bg = 0; bg < 4; ++bg)
        CHECK(suppressed_layers[bg][7 * 32 + 12] == (bg == 1 || bg == 3));
    obtained_sequence_owned = 0;
    retiring_notice = 1;
    const int ended_count = submission_count;
    emit_obtained();
    CHECK(submission_count == ended_count); /* Never revive the old private text plane after a sequence. */
    KhdaysBgLayer ending_layer;
    CHECK(khdays_bg_text_layer(0, 1, &ending_layer));
    CHECK(obtained_cell(c, &ending_layer, 12, 7));
    retiring_notice = 0;
    obtained_sequence_owned = 1;
    KhdaysBgLayer pickup_layer;
    CHECK(khdays_bg_text_layer(0, 1, &pickup_layer));
    CHECK(obtained_cell(c, &pickup_layer, 12, 7) && !obtained_cell(c, &pickup_layer, 13, 7));
    Canvas picture_canvas = {0};
    picture_canvas.bg = 3;
    CHECK(obtained_cell(&picture_canvas, &pickup_layer, 12, 7));
    picture_canvas.engine = 1;
    CHECK(!obtained_cell(&picture_canvas, &pickup_layer, 12, 7));
    picture_canvas.engine = 0;
    obtained_sequence_owned = obtained_sequence_enabled = 0;
    CHECK(!obtained_cell(c, &pickup_layer, 12, 7));
    CHECK(!obtained_cell(&picture_canvas, &pickup_layer, 12, 7));
    memset(suppressed_layers, 0, sizeof(suppressed_layers));
    emit();
    CHECK(!suppressed_layers[3][7 * 32 + 12]);
    /* Backpack announcements alias the same item cache one tile farther right. */
    notification_enabled[1] = 1;
    CHECK(khdays_ui_text_obtained_wants(0, 0));
    cc = (DsCanvas *)(ram + 8200);
    *cc = *(const DsCanvas *)(ram + 16);
    cc->char_base = 9000;
    c = canvas_of(cc, "backpack name");
    khdays_ui_text_scope_push("field.obtained");
    draw(0);
    khdays_ui_text_scope_pop();
    khdays_ui_text_obtained_capture(surface);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 8; ++col)
            map_cells[(row + 8) * 32 + col + 2] = (uint16_t)(0xf250u + row * 8 + col);
    const uint16_t saved_backpack_cell = map_cells[8 * 32 + 2];
    map_cells[8 * 32 + 2] = 0;
    const int backpack_before_restore = notification_calls[1];
    emit();
    CHECK(!obtained.ready && notification_calls[1] == backpack_before_restore + 1);
    map_cells[8 * 32 + 2] = saved_backpack_cell;
    emit();
    CHECK(notification_calls[1] && notification_owned[1]);
    CHECK(suppressed_layers[3][7 * 32 + 12]);
    notification_enabled[1] = notification_owned[1] = 0;
    const int fallback_count = submission_count;
    emit_obtained();
    CHECK(submission_count == fallback_count); /* Keep the native backpack text if its replacement fails. */
    /* The recon line keeps native glyphs even without an HD font mapping. */
    notification_enabled[2] = 1;
    c = canvas_of(cc, "breakthrough line");
    khdays_ui_text_scope_push("field.breakthrough");
    draw(0);
    khdays_ui_text_scope_pop();
    CHECK(c->notification == 2 && c->op_count && c->ops[0].native_glyph);
    memcpy(vram + address, ram + 9000, c->bytes);
    chars_loaded(0, 1, ram + 9000, address, c->bytes);
    CHECK(c->display_notification == 2);
    prepare_breakthrough(2);
    CHECK(notification_calls[2]);
    notification_enabled[2] = notification_owned[2] = 0;
    available_glyphs = 1;
    obtained_reset();
    test_mappings = 1;
    command_route = 0;
    popup_route = 1;
    unsigned char *large_ram = ram;
    ram = small_ram;
    ram_bytes = sizeof(small_ram);
    setup(4);
    free(large_ram);
}

/* A DS button's glyph (the font's U+3349, A) is drawn as the player's input's icon, fitted to its
   cell; with no icon, as itself. */
static void button_icon_tests(void)
{
    setup(4);
    /* the synthetic font's CMAP: U+3349 is glyph 0; its glyph table holds two */
    *(uint32_t *)(ram + 128 + 16) = 700;
    *(uint32_t *)(ram + 252) = 16 + 2 * 8;
    uint16_t *map = (uint16_t *)(ram + 700);
    map[0] = map[1] = 0x3349;
    map[2] = 0; /* direct: table[0] + code - first */
    *(uint32_t *)(map + 4) = 0;
    map[6] = 0;
    available_glyphs = 0; /* no artwork for it: a snapshot is kept */
    button_icon_on = 1;
    button_signature = 1;
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1 && c->display_ops[0].button == KHDAYS_BUTTON_A);
    /* the glyph's cell is 4 x 8: the icon 4 x 4, its texels 2 x 2, from y 2 */
    CHECK(c->hd_image >= 0 && primary_hd_pixel(3 * 16 + 1) == 0xff0000ffu);
    CHECK(primary_hd_pixel(0 * 16 + 1) == 0); /* above it: neither icon nor glyph */
    /* no icon now (another device): the glyph itself again */
    button_icon_on = 0;
    button_signature = 2;
    render();
    CHECK(primary_hd_pixel(0 * 16 + 1) == tint && primary_hd_pixel(3 * 16 + 1) == tint);
    /* in a tutorial an X tells the field's controls (cycling the commands), but not among the
       face buttons: the CMAP's pairs, U+3349 (A) glyph 0 and U+3322 (X) glyph 1 */
    setup(4);
    *(uint32_t *)(ram + 128 + 16) = 700;
    *(uint32_t *)(ram + 252) = 16 + 2 * 8;
    map = (uint16_t *)(ram + 700);
    map[0] = 0x3322;
    map[1] = 0x3349;
    map[2] = 2;
    *(uint32_t *)(map + 4) = 0;
    map[6] = 2;
    map[7] = 0x3349, map[8] = 0;
    map[9] = 0x3322, map[10] = 1;
    available_glyphs = 0;
    test_camp_screen = 6;
    glyph[1] = 272;
    draw(8);
    CHECK(c->op_count == 1 && c->ops[0].button == KHDAYS_BUTTON_X && c->ops[0].field);
    glyph[1] = 264;
    draw(0);
    glyph[1] = 272;
    draw(8); /* drawn again where it was: the earlier one replaced */
    CHECK(c->op_count == 2 && c->ops[0].button == KHDAYS_BUTTON_A && c->ops[0].field);
    CHECK(c->ops[1].button == KHDAYS_BUTTON_X && !c->ops[1].field);
    glyph[1] = 264;
    /* the lock-on tutorial: an R with another a line below it is the next target's, the other the
       lock-on's (U+3327, R, glyph 0) */
    setup(4);
    cc->area_height = 3;
    c = canvas_of(cc, "lock-on test");
    map_height = 3;
    for (int i = 0; i < 6; ++i)
        map_cells[i] = (uint16_t)(i + 1);
    *(uint32_t *)(ram + 128 + 16) = 700;
    *(uint32_t *)(ram + 252) = 16 + 2 * 8;
    map = (uint16_t *)(ram + 700);
    map[0] = map[1] = 0x3327;
    map[2] = 0;
    *(uint32_t *)(map + 4) = 0;
    map[6] = 0;
    available_glyphs = 0;
    button_icon_on = 1;
    buttons_asked = 0;
    draw_xy(0, 0);
    draw_xy(0, 16);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 2 && c->display_ops[0].field && c->display_ops[1].field);
    CHECK((buttons_asked & (1u << KHDAYS_BUTTON_TARGET_NEXT)) && (buttons_asked & (1u << KHDAYS_BUTTON_R)));
    button_icon_on = 0;
    test_camp_screen = KHDAYS_CAMP_NONE;
    available_glyphs = 1u;
}

static void fitted_capture_tests(void)
{
    setup(4);
    fit_widths[0] = 53;
    double offset = 7;
    CHECK(khdays_ui_text_fit_width(font, KHDF_FIT_ITEMS, &offset) == 53 && offset == 0);
    CHECK(khdays_ui_text_fit_width(font, KHDF_FIT_OBTAINED, &offset) == 0);
    text_mode = KHDAYS_UI_OFF;
    CHECK(khdays_ui_text_fit_width(font, KHDF_FIT_ITEMS, &offset) == 0);
    text_mode = KHDAYS_UI_ON;
    available_glyphs = 0;
    const int before = native_blits;
    khdays_ui_text_fit_begin(font, 0, 0, .25, 0);
    khdays_ui_text_position(font, 40.25, 0);
    khdays_ui_text_glyph_before(cc, font, 40, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 40, 0, 1, glyph);
    khdays_ui_text_position(NULL, 0, 0);
    khdays_ui_text_fit_end();
    CHECK(native_blits == before + 1 && c->op_count == 1 && c->ops[0].native_glyph);
    CHECK(fabs(c->ops[0].x - 10.0625) < 1e-9 && c->ops[0].clip_r == 12);
    upload(0, c->bytes);
    test_scale = 4;
    emit();
    CHECK(c->hd_image >= 0 && test_images[c->hd_image][40] == tint);
    const int uploads = image_ends, paints = paint_calls;
    emit();
    CHECK(image_ends == uploads && paint_calls == paints && native_blits == before + 1);
    scroll_text(1);
    upload(0, c->bytes);
    emit();
    CHECK(test_images[c->hd_image][40] == tint && test_images[c->hd_image][7 * 4 * 64 + 40] == 0);
    khdays_ui_text_clear_area(cc, 0, 10, 0, 1, 8);
    upload(0, c->bytes);
    emit();
    CHECK(test_images[c->hd_image][40] == 0);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    emit();
    CHECK(c->display_count == 0);
    available_glyphs = 1;
    khdays_ui_text_fit_begin(font, 0, 0, .25, 0);
    khdays_ui_text_position(font, 40.25, 0);
    khdays_ui_text_glyph_before(cc, font, 40, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 40, 0, 1, glyph);
    khdays_ui_text_position(NULL, 0, 0);
    khdays_ui_text_fit_end();
    CHECK(c->op_count == 1 && !c->ops[0].native_glyph && c->ops[0].x_scale == .25);
    upload(0, c->bytes);
    emit();
    CHECK(c->hd_image >= 0 && test_images[c->hd_image][45] == tint);
    fit_widths[0] = 0;
}

static void fitted_duplicate_tail_tests(void)
{
    setup(4);
    forget(c);
    cc->area_width = cc->param = 8;
    c = canvas_of(cc, "fitted duplicate tail");
    map_width = map_height = 32;
    for (int col = 0; col < 8; ++col) {
        map_cells[col + 1] = (uint16_t)(col + 1);
        map_cells[2 * 32 + col + 1] = (uint16_t)(col + 1);
    }
    put_pixel(c, ram + 1024, 63, 7, 2); /* Non-text art in the tail tile survives. */
    observe_source(c);
    fit_widths[0] = 53;
    double offset = 0;
    CHECK(khdays_ui_text_fit_width(font, KHDF_FIT_ITEMS, &offset) == 53);
    khdays_ui_text_fit_begin(font, 0, 0, 53.0 / 64, 0);
    draw(0);
    draw(57); /* Full cell/filter padding exceeds the measured ink boundary. */
    khdays_ui_text_fit_end();
    CHECK(c->op_count == 2 && c->ops[1].source_x == 57 && c->ops[1].source_w == 4 && c->ops[1].clip_r == 53);
    upload(0, c->bytes);
    emit();
    CHECK(c->extra && c->extra->source[2 * EXTRA_COLS + 8] == 7);
    CHECK(suppressed[2 * 32 + 8]);
    CHECK(pixel(c, c->display_base, 57, 0) == 0 && pixel(c, c->seen, 57, 0) == 1);
    CHECK(pixel(c, c->display_base, 63, 7) == 2);
    CHECK(c->extra && test_images[c->extra->image][23 * EXTRA_COLS * 8 + 71] != 0);
    for (test_scale = 1; test_scale <= 8; test_scale *= 2) {
        emit();
        int primary_right = -1, duplicate_right = -1;
        const int stride = EXTRA_COLS * 8 * test_scale;
        for (int y = 0; y < 8 * test_scale; ++y) {
            for (int x = 0; x < 64 * test_scale; ++x) {
                if (test_images[c->hd_image][y * 64 * test_scale + x])
                    primary_right = x > primary_right ? x : primary_right;
                if (test_images[c->extra->hd_image][(y + 16 * test_scale) * stride + x + 8 * test_scale])
                    duplicate_right = x > duplicate_right ? x : duplicate_right;
            }
        }
        CHECK(primary_right == 53 * test_scale - 1 && duplicate_right == primary_right);
    }
    test_scale = 8;
    emit(); /* Settle the primary image's scale-change invalidation. */
    const int images = image_requests, updates = image_ends, paints = paint_calls;
    emit();
    CHECK(image_requests == images && image_ends == updates && paint_calls == paints);
    put_pixel(c, ram + 1024, 57, 0, 0);
    observe_source(c);
    CHECK(c->op_count == 1); /* Invalidate by native writes, not fitted coordinates. */
    memset(map_cells, 0, sizeof(map_cells));
    memset(suppressed, 0, sizeof(suppressed));
    submission_count = 0;
    emit();
    CHECK(!c->extra && submission_count == 0 && !suppressed[2 * 32 + 8]);
    fit_widths[0] = 0;
}

static void information_planes(void)
{
    for (int variation = 0; variation < 6; ++variation) {
        const int native = variation & 1, engine = variation < 4 ? variation >> 1 : 0, bg = engine ? 0 : 1;
        setup(4);
        virtual_caption_scroll = variation >= 4;
        test_map_bg = bg;
        const int base_vofs = engine ? 216 : 0;
        test_vofs = base_vofs;
        if (engine || virtual_caption_scroll) {
            // SetCaptionText builds at tile row 28 of sub BG0, scrolled to y=8 on screen.
            map_width = map_height = 32;
            memset(map_cells, 0, sizeof(map_cells));
            map_cells[28 * 32] = 1;
            map_cells[28 * 32 + 1] = 2;
        }
        test_mappings = !native;
        mapping_ready = !native;
        available_glyphs = native ? 0 : 1;
        khdays_ui_text_scope_push("field.hud.information");
        draw(0);
        draw(8);
        khdays_ui_text_scope_pop();
        CHECK(c->op_count == 2 && c->ops[0].native_glyph == native);
        const unsigned bytes = c->bytes;
        memcpy(ram + 4096, ram + 1024, bytes);
        khdays_ui_text_memory_copy(ram + 1024, ram + 4096, bytes, 4096);
        CHECK(canvas_count == 2 && canvases[1].information);
        memcpy(vram + 32, ram + 4096, bytes);
        chars_loaded(engine, bg, ram + 4096, 32, bytes);
        c = &canvases[0];
        map_cells[(engine || virtual_caption_scroll ? 28 * 32 : 0) + 1] =
            0; // Retain offscreen glyphs for the KH2 marquee.
        information_enabled = 1;
        information_calls = 0;
        emit();
        CHECK(c->information && information_calls == 1 && information_ink);
        CHECK(information_right > 8 && canvases[1].engine == engine && canvases[1].bg == bg);
        CHECK(information_engine == engine && information_bg == bg);
        const int paints = paint_calls;
        emit();
        CHECK(information_calls == 2 && paint_calls == paints);
        const unsigned previous_revision = information_revision;
        int preferences[KHDAYS_OPT_COUNT];
        khdays_hud_settings_default(preferences);
        preferences[KHDAYS_OPT_FONT] = 0;
        khdays_hud_settings_publish(preferences);
        khdays_ui_text_settings_changed();
        emit();
        CHECK(information_revision != previous_revision && information_ink);
        const unsigned native_revision = information_revision;
        preferences[KHDAYS_OPT_FONT] = 1;
        khdays_hud_settings_publish(preferences);
        khdays_ui_text_settings_changed();
        emit();
        CHECK(information_revision != native_revision && information_ink);
        information_calls -= 2;
        // Canvas placement wraps with the tilemap, including the upper crawl's saved rows.
        test_hofs = 512;
        test_vofs = base_vofs + 256;
        emit();
        CHECK(information_calls == 3);
        test_hofs = 0;
        test_vofs = base_vofs;
        ++information_epoch;
        emit();
        CHECK(information_calls == 3); // Retained glyphs from an old map cannot supply the new caption.
        khdays_ui_text_scope_push("field.hud.information");
        draw(0);
        draw(8);
        khdays_ui_text_scope_pop();
        memcpy(ram + 4096, ram + 1024, bytes);
        khdays_ui_text_memory_copy(ram + 1024, ram + 4096, bytes, 4096);
        memcpy(vram + 32, ram + 4096, bytes);
        chars_loaded(engine, bg, ram + 4096, 32, bytes);
        emit();
        CHECK(information_calls == 4);
        memcpy(ram + 4096, ram + 8192, bytes);
        khdays_ui_text_memory_copy(ram + 8192, ram + 4096, bytes, 4096);
        CHECK(!canvases[1].information && canvases[1].display_information);
        emit();
        CHECK(information_calls == 5); // Unpublished RAM reuse must not turn the on-screen caption back into DS art.
        memcpy(vram + 32, ram + 4096, bytes);
        chars_loaded(engine, bg, ram + 4096, 32, bytes);
        CHECK(!canvases[1].display_information);
        information_enabled = 0;
        virtual_caption_scroll = 0;
        test_mappings = mapping_ready = 1;
    }
}

static void live_layout_switch(void)
{
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    for (int mode = 0; mode < 2; ++mode) {
        preferences[KHDAYS_OPT_FONT] = mode;
        khdays_hud_settings_publish(preferences);
        setup(4);
        cc->area_width = cc->param = 8;
        c = canvas_of(cc, "paired layout");
        const int token = khdays_ui_text_layout_begin();
        CHECK(token);
        for (int i = 0; i < 2; ++i)
            CHECK(khdays_ui_text_layout_glyph(cc, font, (mode ? 20 : 10) + i * (mode ? 6 : 11), 0, 'A'));
        khdays_ui_text_layout_actual(token);
        for (int i = 0; i < 2; ++i) {
            const int x = (mode ? 10 : 20) + i * (mode ? 11 : 6);
            CHECK(!khdays_ui_text_layout_glyph(cc, font, x, 0, 'A'));
            draw(x);
        }
        khdays_ui_text_layout_end(token);
        upload(0, c->bytes);
        for (int pass = 0; pass < 4; ++pass) {
            const int hd = pass & 1;
            preferences[KHDAYS_OPT_FONT] = hd;
            khdays_hud_settings_publish(preferences);
            khdays_ui_text_settings_changed();
            CHECK(c->display_count == 2);
            CHECK(c->display_ops[0].x == (hd ? 10 : 20));
            CHECK(c->display_ops[1].x == (hd ? 21 : 26));
            CHECK(c->display_ops[0].w == (hd ? 8 : 4));
        }
    }
    khdays_hud_settings_default(preferences);
    khdays_hud_settings_publish(preferences);
}

static void live_font_switch(void)
{
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    for (int initially_enabled = 0; initially_enabled <= 1; ++initially_enabled) {
        preferences[KHDAYS_OPT_FONT] = initially_enabled;
        khdays_hud_settings_publish(preferences);
        setup(4);
        draw(0);
        upload(0, c->bytes);
        CHECK(c->display_count == 1 && c->display_ops[0].native_saved);
        for (int pass = 0; pass < 4; ++pass) {
            const int enabled = pass & 1;
            preferences[KHDAYS_OPT_FONT] = enabled;
            khdays_hud_settings_publish(preferences);
            khdays_ui_text_settings_changed();
            render();
            CHECK(c->base_hd);
            CHECK(c->display_count == 1); /* No new game text draw or transfer. */
            if (enabled) {
                CHECK(c->hd_ready && c->hd_image >= 0);
                CHECK(test_images[c->hd_image][7] == tint);
                CHECK(test_images[c->image][0] == 0);
            } else {
                CHECK(test_images[c->image][0] == 0);
                CHECK(c->hd_image >= 0 && test_images[c->hd_image][0] != 0);
            }
            uint32_t glyph_pixels[16 * 8] = {0};
            paint_op(c, &c->display_ops[0], 0, 0, 0, glyph_pixels, 16, 8, 1, 0, 0, 8, 8);
            CHECK(enabled ? glyph_pixels[7] == tint : glyph_pixels[0] != 0 && glyph_pixels[7] == 0);
        }
    }
    preferences[KHDAYS_OPT_FONT] = 1;
    khdays_hud_settings_publish(preferences);
}

int main(int argc, char **argv)
{
    graphics_replacements();
    native_planes();
    if (argc > 1 && strcmp(argv[1], "--graphics-only") == 0) {
        while (canvas_count)
            forget(&canvases[0]);
        free(canvases);
        return failures ? 1 : 0;
    }
    live_layout_switch();
    live_font_switch();
    obtained_popup();
    KhdaysUiElement ordinary_anchors[3];
    memcpy(ordinary_anchors, notification_anchors, sizeof(ordinary_anchors));
    information_enabled = virtual_caption_scroll = 1;
    obtained_popup(); // Command labels and obtained/backpack/recon text must also survive a visible upper caption.
    for (int k = 0; k < 3; ++k) {
        CHECK(notification_anchors[k].x == ordinary_anchors[k].x && notification_anchors[k].y == ordinary_anchors[k].y);
        CHECK(notification_anchors[k].w == ordinary_anchors[k].w && notification_anchors[k].h == ordinary_anchors[k].h);
    }
    information_enabled = virtual_caption_scroll = 0;
    information_planes();
    button_icon_tests();
    fitted_capture_tests();
    fitted_duplicate_tail_tests();
    setup(4);
    CHECK(!khdays_ui_text_numeric(font, KHDF_LAYOUT_PANEL_COUNTER));
    numeric_layout_enabled = 1;
    CHECK(khdays_ui_text_numeric(font, KHDF_LAYOUT_PANEL_COUNTER));
    text_mode = KHDAYS_UI_OFF;
    CHECK(!khdays_ui_text_numeric(font, KHDF_LAYOUT_PANEL_COUNTER));
    text_mode = KHDAYS_UI_ON;
    bind_font(font, "text/missing.nftr");
    CHECK(!khdays_ui_text_numeric(font, KHDF_LAYOUT_PANEL_COUNTER));
    CHECK(!khdays_ui_text_numeric(NULL, KHDF_LAYOUT_PANEL_COUNTER));
    numeric_layout_enabled = 0;
    /* Native-resolution supplementary DS planes equal the prior expanded planes. */
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        setup(bpp);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                put_pixel(c, ram + 1024, x, y, (unsigned)(x + y * 3) & (bpp == 4 ? 15u : 255u));
        upload(0, c->bytes);
        c->owned[0] = UINT64_C(0x0123456789abcdef);
        for (uint16_t flip = 0; flip <= 0xc00u; flip += 0x400u) {
            uint32_t native[64], expanded[64 * 64];
            const uint16_t cell = flip | 0x3000u;
            for (int hide = 0; hide <= 1; ++hide) {
                mapped_tile(c, 0, cell, 1, 0, hide, native);
                for (int scale = 1; scale <= 8; ++scale) {
                    mapped_tile(c, 0, cell, scale, 0, hide, expanded);
                    for (int y = 0; y < 8 * scale; ++y)
                        for (int x = 0; x < 8 * scale; ++x)
                            CHECK(expanded[y * 8 * scale + x] == native[(y / scale) * 8 + x / scale]);
                }
            }
        }
    }
    /* Block ownership masking equals the old per-output-pixel test, including padding. */
    for (int scale = 1; scale <= 8; ++scale) {
        for (uint16_t flip = 0; flip <= 0xc00u; flip += 0x400u) {
            const uint64_t masks[] = {0, UINT64_MAX, UINT64_C(0x0123456789abcdef), 1, UINT64_C(1) << 63};
            for (int pattern = 0; pattern < 5; ++pattern) {
                uint32_t actual[70 * 66], expected[70 * 66];
                for (int i = 0; i < 70 * 66; ++i)
                    actual[i] = expected[i] = 0x12345678u + (uint32_t)i;
                for (int y = 0; y < 8 * scale; ++y)
                    for (int x = 0; x < 8 * scale; ++x) {
                        const int sx = flip & 0x400u ? 7 - x / scale : x / scale;
                        const int sy = flip & 0x800u ? 7 - y / scale : y / scale;
                        if (!(masks[pattern] & (UINT64_C(1) << (sy * 8 + sx))))
                            expected[(y + 1) * 70 + x + 1] = 0;
                    }
                mask_tile(actual + 71, 70, scale, flip, masks[pattern]);
                CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
            }
        }
    }
    /* A graphics-only primary placement must not allocate an empty HD image or
       prevent the same canvas's text from appearing at a supplementary placement. */
    setup(4);
    cc->area_width = cc->param = 4;
    c = canvas_of(cc, "graphics primary");
    draw(0);
    upload(0, c->bytes);
    map_width = map_height = 32;
    for (int i = 0; i < 1024; ++i)
        map_cells[i] = 2;
    map_cells[10] = 2;
    map_cells[11] = 3;
    map_cells[12] = 4;
    map_cells[33] = 1;
    emit();
    CHECK(c->hd_ready && c->hd_image < 0 && c->base_hd);
    CHECK(c->extra && c->extra->hd_image >= 0);
    CHECK(submitted_hd_pixel(15, 11, tint));
    const int graphics_uploads = image_ends, graphics_paints = paint_calls;
    submission_count = 0;
    emit();
    CHECK(image_ends == graphics_uploads && paint_calls == graphics_paints);
    map_cells[33] = 2;
    submission_count = 0;
    emit();
    CHECK(c->hd_ready && c->hd_image < 0 && c->extra == NULL);
    CHECK(!submitted_hd_pixel(15, 11, tint));
    setup(4);
    khdays_ui_text_position(font, .35, .25);
    draw(0);
    khdays_ui_text_position(NULL, 0, 0);
    CHECK(c->op_count == 1 && c->ops[0].x == .35 && c->ops[0].y == .25);
    CHECK(c->ops[0].source_x == 0 && c->ops[0].source_y == 0 && c->ops[0].source_w == 4);
    upload(0, c->bytes);
    render();
    const int fractional_paints = paint_calls, fractional_uploads = image_ends;
    render();
    CHECK(paint_calls == fractional_paints && image_ends == fractional_uploads);
    scroll_text(1);
    CHECK(c->ops[0].y == -.75 && c->ops[0].source_y == -1);
    /* A crawl keeps fractional advances across frames without changing the
       game's integer cursor or leaking a prior stream's residual. */
    int crawl_cursor = 0;
    for (int i = 0; i < 100; ++i) {
        khdays_ui_text_crawl_begin(cc, ram + 700, i, crawl_cursor, 20, 0);
        CHECK(fabs(khdays_ui_text_pen_x(crawl_cursor) - i * 3.85) < 1e-10);
        crawl_cursor += khdays_ui_text_crawl_advance(3.85);
        CHECK(crawl_cursor == khdays_ui_text_round((i + 1) * 3.85));
    }
    khdays_ui_text_crawl_begin(cc, ram + 700, 0, 0, 20, 0);
    CHECK(khdays_ui_text_pen_x(0) == 0);
    CHECK(khdays_ui_text_crawl_advance(6) == 6); // Native fallback advance is exact.
    /* HD / original / HD / original / original: stable indices, native ink
       retained in the graphics plane while only HD artwork is suppressed. */
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        setup(bpp);
        forget(c);
        cc->area_width = cc->param = 6;
        c = canvas_of(cc, "mixed original fallback");
        available_glyphs = (1u << 0) | (1u << 2);
        map_width = 6;
        for (int i = 0; i < 6; ++i)
            map_cells[i] = (uint16_t)(i + 1);
        for (int index = 0; index < 5; ++index) {
            glyph[1] = 264 + (uint32_t)index * 8;
            draw(index * 8);
        }
        CHECK(c->op_count == 2 && c->ops[0].glyph == 0 && c->ops[1].glyph == 2);
        upload(0, c->bytes);
        render();
        CHECK(c->display_count == 2);
        for (int index = 0; index < 5; ++index) {
            const int x = index * 8;
            const int native = index == 1 || index >= 3;
            CHECK(pixel(c, c->display_base, x + 1, 0) == (unsigned)native);
            CHECK((test_images[c->image][x + 1] != 0) == native);
            CHECK(primary_hd_pixel(x + 7) == (native ? 0u : tint));
        }
        /* Equal-byte redraws remain native and full clears remove both planes. */
        glyph[1] = 264 + 8;
        draw(8);
        upload(0, c->bytes);
        render();
        CHECK(c->display_count == 2 && test_images[c->image][9] != 0);
        memset(ram + 1024, 0, c->bytes);
        khdays_ui_text_clear(cc, 0);
        upload(0, c->bytes);
        render();
        CHECK(c->display_count == 0 && c->hd_image < 0 && test_images[c->image][9] == 0);
    }
    /* Actual party-portrait geometry: 15x15 8bpp tiles, two overlapping
       name passes near the bottom, copied into an NCGR and uploaded at tile 21. */
    setup(8);
    forget(c);
    cc->area_width = cc->area_height = cc->param = 15;
    c = canvas_of(cc, "portrait regression");
    ram[513] = 10;
    memset(ram + 1024, 0xe1, c->bytes);
    observe_source(c);
    for (int pass = 0; pass < 2; ++pass) {
        for (int letter = 0; letter < 5; ++letter) {
            const int x = 77 + letter * 4 - pass, y = 108 - pass;
            khdays_ui_text_glyph_before(cc, font, x, y, 0xf1 + pass, glyph);
            for (int py = y; py < y + 8; ++py)
                for (int px = x; px < x + 10; ++px)
                    put_pixel(c, ram + 1024, px, py, 0xf1 + pass);
            khdays_ui_text_glyph(cc, font, x, y, 0xf1 + pass, glyph);
        }
    }
    CHECK(c->op_count == 10);
    memcpy(ram + 16384, ram + 1024, c->bytes);
    khdays_ui_text_memory_copy(ram + 1024, ram + 16384, c->bytes, 16384);
    CHECK(canvas_count == 2 && canvases[1].op_count == 10);
    memcpy(vram + 21 * 64, ram + 16384, c->bytes);
    chars_loaded(0, 3, ram + 16384, 21 * 64, c->bytes);
    /* The real upload names BG3 (4bpp), but BG2's 8bpp tilemap
       displays the shared character VRAM. The transfer is not the display. */
    test_map_bg = 2;
    test_bg_colors256[3] = 0;
    map_width = map_height = 32;
    for (int row = 0; row < 15; ++row)
        for (int col = 0; col < 15; ++col)
            map_cells[(row + 8) * 32 + col + 17] = (uint16_t)(0xc000 | (21 + row * 15 + col));
    submission_count = 0;
    emit();
    CHECK(canvases[1].display_count == 10);
    CHECK(canvases[1].bg == 2);
    CHECK(submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    for (int i = 0; i < submission_count; ++i)
        CHECK((submissions[i].slot & KES_NUMBER_MASK) == 2);
    const int portrait_images = image_requests, portrait_uploads = image_ends, portrait_paints = paint_calls;
    for (int frame = 0; frame < 30; ++frame) {
        submission_count = 0;
        emit();
        CHECK(submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    }
    CHECK(image_requests == portrait_images && image_ends == portrait_uploads && paint_calls == portrait_paints);
    /* An eligible upload layer with no referencing cells must also be skipped. */
    test_bg_colors256[3] = 1;
    chars_loaded(0, 3, ram + 16384, 21 * 64, c->bytes);
    submission_count = 0;
    emit();
    CHECK(canvases[1].bg == 2 && submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    /* Removing the map removes the overlay; reassigning it follows the actual
       display layer without another upload or another canvas/image allocation. */
    test_map_bg = 0;
    test_bg_colors256[0] = 0;
    submission_count = 0;
    emit();
    CHECK(submission_count == 0);
    test_map_bg = 1;
    submission_count = 0;
    emit();
    CHECK(canvases[1].bg == 1 && submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    CHECK(image_requests == portrait_images);
    /* The portrait name is drawn into scratch RAM, copied into an NCGR,
       and later uploaded from that NCGR rather than the original canvas. */
    setup(8);
    draw(0);
    const uint32_t staged_size = c->bytes;
    font_origin = "UI/btl/face.p2@0";
    font_origin_info = 4096;
    font_origin_offset = 96;
    const unsigned int portrait_owner = khdays_ui_text_copy_owner(ram + 1024, ram + 4096, staged_size);
    CHECK(portrait_owner == 4000);
    font_origin = NULL;
    memcpy(ram + 4096, ram + 1024, staged_size);
    khdays_ui_text_memory_copy(ram + 1024, ram + 4096, staged_size, portrait_owner);
    CHECK(canvas_count == 2 && canvases[1].copied && canvases[1].op_count == 1);
    CHECK(pixel(&canvases[1], canvases[1].base, 0, 0) == 0);
    memcpy(ram + 8192, ram + 4096, staged_size);
    khdays_ui_text_memory_copy(ram + 4096, ram + 8192, staged_size, 8192);
    CHECK(canvas_count == 3 && canvases[2].op_count == 1);
    khdays_ui_text_freed(NULL, ram + 4000); // Frees an allocation containing interior character data.
    CHECK(canvas_count == 2 && canvases[1].buffer == 8192);
    memset(ram + 1024, 0, staged_size);
    khdays_ui_text_clear(cc, 0); // Reusing scratch must not change the copied name.
    memcpy(vram + 64, ram + 8192, staged_size);
    chars_loaded(0, 0, ram + 8192, 64, staged_size);
    emit();
    CHECK(canvases[1].display_count == 1 && submitted_hd_pixel(7, 3, tint));
    const int copied_uploads = image_ends, copied_paints = paint_calls;
    for (int frame = 0; frame < 10; ++frame) {
        submission_count = 0;
        emit();
    }
    CHECK(image_ends == copied_uploads && paint_calls == copied_paints);
    /* A second full copy of empty scratch removes the name, even with identical bytes. */
    memcpy(ram + 8192, ram + 1024, staged_size);
    khdays_ui_text_memory_copy(ram + 1024, ram + 8192, staged_size, 8192);
    CHECK(canvases[1].op_count == 0 && canvases[1].display_count == 1);
    memcpy(vram + 64, ram + 8192, staged_size);
    chars_loaded(0, 0, ram + 8192, 64, staged_size);
    submission_count = 0;
    emit();
    CHECK(canvases[1].display_count == 0 && !submitted_hd_pixel(7, 3, tint));
    /* VRAM copies are published by the BG loader, never registered as RAM staging. */
    draw(0);
    const int ram_canvases = canvas_count;
    khdays_ui_text_memory_copy(ram + 1024, vram + 64, staged_size, 0);
    CHECK(canvas_count == ram_canvases);
    /* Repeated copies reuse the staging record. */
    for (int pass = 0; pass < 50; ++pass) {
        memcpy(ram + 8192, ram + 1024, staged_size);
        khdays_ui_text_memory_copy(ram + 1024, ram + 8192, staged_size, 8192);
    }
    CHECK(canvas_count == ram_canvases && canvases[1].op_count == 1);
    khdays_ui_text_freed(NULL, ram + 8192);
    CHECK(canvases[1].detached); // Already published VRAM survives until overwritten.
    for (int i = 0; i < 24; ++i) {
        const int dst = 16384 + i * (int)staged_size;
        memcpy(ram + dst, ram + 1024, staged_size);
        khdays_ui_text_memory_copy(ram + 1024, ram + dst, staged_size, (unsigned int)dst);
    }
    CHECK(canvas_count == ram_canvases + 24); // A record for each copy while its memory lives.
    setup(4);
    mapping_ready = 0;
    *(uint32_t *)(ram + 252) = 32;  // CGLP: two original glyphs.
    *(uint32_t *)(ram + 144) = 768; // FINF.pMap
    uint16_t *original_map = (uint16_t *)(ram + 768);
    original_map[0] = 65;
    original_map[1] = 66;
    original_map[6] = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapping_count == 2 && mapped_codes[0] == 65 && mapped_codes[1] == 66);
    original_map[1] = 67;
    original_map[2] = 1;
    original_map[6] = 1;
    original_map[7] = 0xffff;
    original_map[8] = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapped_codes[0] == 67 && mapped_codes[1] == 65);
    original_map[2] = 2;
    original_map[6] = 2;
    original_map[7] = 0xff21;
    original_map[8] = 1;
    original_map[9] = 0x2019;
    original_map[10] = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapped_codes[0] == 0x2019 && mapped_codes[1] == 0xff21);
    *(uint32_t *)(ram + 776) = 768;
    mapping_count = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapping_count == 0); // A malformed cycle must not register a partial map.
    bind_font(font, "text/synthetic.nftr");
    CHECK(font_path(KHDAYS_REF(font)) == NULL);
    CHECK(khdays_ui_text_metric_f(font, 0, KHDF_ADVANCE, 8) == 8);
    setup(8);
    cc->area_height = 4;
    c = canvas_of(cc, "baseline rows");
    map_height = 4;
    for (int k = 0; k < 8; ++k)
        map_cells[k] = (uint16_t)(k + 1);
    baseline_shift = -3;
    draw_xy(0, 1);  // Cropped at the actual canvas top, not the old Days cell top.
    draw_xy(8, 17); // Right-aligned second line retains the same baseline translation.
    CHECK(c->op_count == 2 && c->ops[0].y == -2 && c->ops[1].y == 14);
    CHECK(c->ops[0].clip_t == 0 && c->ops[1].clip_t == 14);
    draw_xy(0, 1);
    CHECK(c->op_count == 2); // Reissuing the source position replaces the shifted operation.
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(4) == tint);
    CHECK(primary_hd_pixel(6 * 16 + 4) == 0);
    CHECK(primary_hd_pixel(14 * 16 + 12) == tint);
    CHECK(primary_hd_pixel(22 * 16 + 12) == 0);
    uint32_t baseline_tile[64];
    mapped_tile(c, 3, 0, 1, 1, 1, baseline_tile);
    CHECK(baseline_tile[6 * 8 + 4] == tint && baseline_tile[5 * 8 + 4] == 0);
    khdays_ui_text_clear_area(cc, 0, 8, 14, 8, 2);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(14 * 16 + 12) == 0);
    CHECK(primary_hd_pixel(16 * 16 + 12) == tint);
    scroll_text(8);
    CHECK(c->ops[0].source_y == 9 && c->ops[0].y == 6);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(8 * 16 + 12) == tint);
    baseline_shift = 3;
    draw_xy(0, 20);
    CHECK(c->ops[c->op_count - 1].y == 23);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(23 * 16 + 4) == tint);
    /* Initial mixed-canvas population: artwork overlaps neighbouring storage,
       but graphics uploaded there do not overwrite the original glyph draw. */
    setup(8);
    large_geometry = 1;
    cc->area_width = cc->param = 20;
    cc->area_height = 32;
    c = canvas_of(cc, "initial panel population");
    map_width = map_height = 32;
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 20; ++col)
            map_cells[row * 32 + col] = (uint16_t)(256 + row * 20 + col);
    for (int row = 0; row < 8; ++row) {
        draw_xy(16, row * 16 + 3); // Name and right-aligned quantity in the same canvas.
        draw_xy(112, row * 16 + 3);
        put_pixel(c, ram + 1024, 24, row * 16 + 5, 7); // Late source graphics inside PNG overhang.
        put_pixel(c, ram + 1024, 120, row * 16 + 5, 7);
    }
    observe_source(c);
    CHECK(c->op_count == 16);
    memcpy(vram + 0x4000, ram + 1024, c->bytes);
    chars_loaded(1, 2, ram + 1024, 0x4000, c->bytes);
    CHECK(c->display_count == 16);
    for (int row = 0; row < 8; ++row) {
        put_pixel(c, vram + 0x4000, 25, row * 16 + 5, 6); // Late VRAM graphics, same condition.
        put_pixel(c, vram + 0x4000, 121, row * 16 + 5, 6);
    }
    emit();
    CHECK(c->display_count == 16);
    for (int row = 0; row < 8; ++row) {
        CHECK(submitted_hd_pixel(30, row * 16 + 8, tint));
        CHECK(submitted_hd_pixel(126, row * 16 + 8, tint));
    }
    const int initial_paints = paint_calls, initial_uploads = image_ends;
    for (int frame = 0; frame < 10; ++frame) {
        submission_count = 0;
        emit();
    }
    CHECK(paint_calls == initial_paints && image_ends == initial_uploads);
    /* Scrolling's redraw sees the already-populated graphics bytes. This used
       to make missing first-frame text suddenly appear without changing PNGs. */
    for (int row = 0; row < 8; ++row) {
        draw_xy(16, row * 16 + 3);
        draw_xy(112, row * 16 + 3);
    }
    memcpy(vram + 0x4000, ram + 1024, c->bytes);
    chars_loaded(1, 2, ram + 1024, 0x4000, c->bytes);
    submission_count = 0;
    emit();
    CHECK(c->op_count == 16 && c->display_count == 16);
    for (int row = 0; row < 8; ++row) {
        CHECK(submitted_hd_pixel(30, row * 16 + 8, tint));
        CHECK(submitted_hd_pixel(126, row * 16 + 8, tint));
    }
    /* Overwriting the native footprint still retires the complete glyph. */
    put_pixel(c, vram + 0x4000, 16, 3, 0);
    submission_count = 0;
    emit();
    CHECK(c->display_count == 15);
    CHECK(!submitted_hd_pixel(30, 8, tint));
    setup(4);
    large_geometry = 1;
    cc->area_width = cc->param = 4;
    cc->area_height = 4;
    c = canvas_of(cc, "large art");
    map_width = map_height = 4;
    khdays_bg_text_layer(0, 0, &layer);
    for (int k = 0; k < 16; ++k)
        map_cells[k] = (uint16_t)(k + 1);
    draw(0);
    CHECK(c->ops[0].w == 18 && c->ops[0].h == 24 && c->ops[0].clip_b == 24);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(23 * 32 + 17) == tint);
    uint32_t large_tile[64];
    mapped_tile(c, 10, map_cells[10], 1, 1, 1, large_tile);
    CHECK(large_tile[7 * 8 + 1] == tint);
    khdays_ui_text_clear_area(cc, 0, 16, 16, 2, 8);
    CHECK(displayed_at(17, 23)); // Source clears stay unpublished until upload.
    upload(10 * 32, 32);
    render();
    CHECK(primary_hd_pixel(23 * 32 + 17) == 0);
    CHECK(primary_hd_pixel(23 * 32 + 15) == tint);
    scroll_text(5);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(18 * 32 + 15) == tint);
    CHECK(primary_hd_pixel(23 * 32 + 15) == 0);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    setup(4);
    metric_override = 1;
    CHECK(khdays_ui_text_metric(font, 0, KHDF_LEFT, 0) == -2);
    CHECK(khdays_ui_text_metric(font, 0, KHDF_WIDTH, 8) == 13);
    CHECK(khdays_ui_text_metric(font, 0, KHDF_ADVANCE, 8) == 11);
    CHECK(khdays_ui_text_metric(font, -1, KHDF_LINE_FEED, 8) == 17);
    CHECK(khdays_ui_text_kerning(font, 'A', 'V') == -2);
    text_mode = KHDAYS_UI_OFF;
    CHECK(khdays_ui_text_metric(font, 0, KHDF_ADVANCE, 8) == 8);
    CHECK(khdays_ui_text_kerning(font, 'A', 'V') == 0);
    setup(4);
    glyph_width_override = 6;
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1 && c->display_ops[0].paint_w == 6);
    CHECK(primary_hd_pixel(4) == tint && primary_hd_pixel(5) == tint);
    CHECK(primary_hd_pixel(6) == 0 && primary_hd_pixel(7) == 0);
    uint32_t mapped[64];
    mapped_tile(c, 0, map_cells[0], 1, 1, 1, mapped);
    CHECK(mapped[4] == tint && mapped[5] == tint && mapped[6] == 0 && mapped[7] == 0);
    glyph_width_override = 0;
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1 && c->display_ops[0].paint_w == 0);
    CHECK(c->hd_ready && c->hd_image < 0 && c->base_hd);
    for (int px = 0; px < 16 * 8; ++px)
        CHECK(test_images[c->image][px] == 0);
    CHECK(pixel(c, c->seen, 0, 0) == 1 && pixel(c, c->display_base, 0, 0) == 0);
    mapped_tile(c, 0, map_cells[0], 1, 1, 1, mapped);
    for (int px = 0; px < 64; ++px)
        CHECK(mapped[px] == 0);
    mapped_tile(c, 0, map_cells[0], 1, 0, 1, mapped);
    for (int px = 0; px < 64; ++px)
        CHECK(mapped[px] == 0); /* Zero PNG width must not expose original DS ink. */
    setup(4);
    bind_font(font, "/text/synthetic.NFTR");
    CHECK(font_path(KHDAYS_REF(font)) && strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
    bind_font(font, "text/synthetic.nftr.png");
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
    bind_font(font, "/text/synthetic.nftr");
    const char *bound = font_path(KHDAYS_REF(font));
    CHECK(bound && strcmp(bound, "text/synthetic.nftr") == 0);
    test_scope = "camp.story.screen.2";
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/test") == 0);
    draw(0);
    CHECK(c->op_count && strcmp(c->ops[c->op_count - 1].resource, "@font-route/test") == 0);
    test_scope = "camp.story.screen.4";
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
    CHECK(strcmp(c->ops[c->op_count - 1].resource, "@font-route/test") == 0);
    test_scope = "";
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
    font_bindings[binding_count - 1].replacement = 0;
    test_scope = "camp.story.screen.2";
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/test") == 0);
    test_scope = "";
    CHECK(font_path(KHDAYS_REF(font)) == NULL); /* Alternate works without a global HD replacement. */
    font_bindings[binding_count - 1].replacement = 1;
    for (int page = 0; page < 3; ++page) {
        khdays_ui_text_field_tutorial_before(page, 0);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/tutorial") == 0);
        draw(page * 2);
        CHECK(strcmp(c->ops[c->op_count - 1].resource, "@font-route/tutorial") == 0);
        khdays_ui_text_field_tutorial_before(page, 1);
        khdays_ui_text_field_tutorial(page, 1);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/tutorial") == 0);
        khdays_ui_text_field_tutorial(page, 0);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
        CHECK(strcmp(c->ops[c->op_count - 1].resource, "@font-route/tutorial") == 0);
    }
    test_scope = "camp.story.screen.2";
    khdays_ui_text_field_tutorial_before(0, 0);
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/tutorial") == 0);
    khdays_ui_text_field_tutorial(0, 0);
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/test") == 0);
    test_scope = "";
    khdays_ui_text_scope_push("field.dialogue.speaker");
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/speaker") == 0);
    draw(0);
    khdays_ui_text_scope_pop();
    CHECK(strcmp(c->ops[c->op_count - 1].resource, "@font-route/speaker") == 0);
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
    draw(4);
    CHECK(strcmp(c->ops[c->op_count - 1].resource, "text/synthetic.nftr") == 0);
    test_scope = "camp.story.screen.2";
    /* Absolute HUD/results scopes isolate shared fonts from camp and dialogue.
       Captured choices survive scope exit, and nested draws restore their owner. */
    const char *hud_scopes[] = {"field.hud.information", "field.hud.status", "field.hud.area",
                                "mission.results.summary", "mission.results.rewards"};
    for (int role = 0; role < 5; ++role) {
        khdays_ui_text_scope_push(hud_scopes[role]);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/hud-results") == 0);
        CHECK(strcmp(selected_scope, hud_scopes[role]) == 0);
        draw(0);
        CHECK(strcmp(c->ops[c->op_count - 1].resource, "@font-route/hud-results") == 0);
        khdays_ui_text_field_tutorial_before(0, 0);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/tutorial") == 0);
        khdays_ui_text_field_tutorial(0, 0);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/hud-results") == 0);
        khdays_ui_text_scope_pop();
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/test") == 0);
        CHECK(strcmp(c->ops[c->op_count - 1].resource, "@font-route/hud-results") == 0);
    }
    test_scope = "camp.story.screen.1";
    const char *roles[] = {"instruction", "world", "header", "summary", "objectives", "rewards", "buttons"};
    for (int role = 0; role < 7; ++role) {
        char operation[48], expected_scope[80];
        snprintf(operation, sizeof(operation), "mission_select.%s", roles[role]);
        snprintf(expected_scope, sizeof(expected_scope), "camp.story.screen.1.%s", operation);
        khdays_ui_text_scope_push(operation);
        const char *expected = role >= 3 && role <= 5 ? "@font-route/mission-body" : "text/synthetic.nftr";
        CHECK(strcmp(font_path(KHDAYS_REF(font)), expected) == 0);
        CHECK(strcmp(selected_scope, expected_scope) == 0);
        draw(0);
        CHECK(strcmp(c->ops[c->op_count - 1].resource, expected) == 0);
        khdays_ui_text_field_tutorial_before(0, 0);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/tutorial") == 0);
        khdays_ui_text_field_tutorial(0, 0);
        CHECK(strcmp(font_path(KHDAYS_REF(font)), expected) == 0);
        khdays_ui_text_scope_pop();
        CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
        CHECK(strcmp(c->ops[c->op_count - 1].resource, expected) == 0);
    }
    test_scope = "";
    test_camp_screen = 1;
    khdays_ui_text_scope_push("mission_select.summary");
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "@font-route/mission-body") == 0);
    CHECK(strcmp(selected_scope, "camp.story.screen.1.mission_select.summary") == 0);
    khdays_ui_text_scope_pop();
    test_camp_screen = KHDAYS_CAMP_NONE;
    for (int depth = 0; depth < 20; ++depth)
        khdays_ui_text_scope_push("mission_select.summary");
    for (int depth = 0; depth < 20; ++depth)
        khdays_ui_text_scope_pop();
    CHECK(operation_depth == 0 && operation_overflow == 0);
    khdays_ui_text_font_init(font, NULL);
    CHECK(font_path(KHDAYS_REF(font)) != NULL);
    uint32_t *copied_font = (uint32_t *)(ram + 700);
    copied_font[0] = font[0];
    CHECK(font_path(KHDAYS_REF(copied_font)) != NULL);
    const uint32_t saved_info = font[0];
    font[0] = 900; /* Reusing an object for another NFTR cannot retain the old path. */
    CHECK(font_path(KHDAYS_REF(font)) == NULL);
    font[0] = saved_info;
    binding_count = 0;
    font_origin = "/text/synthetic.nftr";
    font_origin_info = saved_info;
    khdays_ui_text_font_init(font, ram + saved_info);
    CHECK(font_path(KHDAYS_REF(font)) != NULL);
    CHECK(font_path(KHDAYS_REF(copied_font)) != NULL);
    font_origin = "text/second.nftr";
    khdays_ui_text_font_init(font, ram + saved_info);
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/second.nftr") == 0);
    khdays_ui_text_freed(NULL, ram + saved_info);
    font_origin = NULL;
    CHECK(font_path(KHDAYS_REF(copied_font)) == NULL);
    font_origin = "/text/missing.nftr";
    const int probes_before = font_probes;
    for (int i = 0; i < 100; ++i)
        CHECK(font_path(KHDAYS_REF(copied_font)) == NULL);
    CHECK(font_probes == probes_before + 1); /* Missing atlases are cached by identity too. */
    setup(4);
    ram[513] = 0; /* PNG artwork is independent of a zero original glyphWidth. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1);
    CHECK(c->hd_image >= 0 && primary_hd_pixel(7) == tint);
    setup(4);
    draw(0);
    glyph[1] += 8; /* Adjacent DS fallback overlaps only the end of the HD cell. */
    draw(6);
    upload(0, c->bytes);
    render();
    CHECK(displayed_at(4, 0));
    CHECK(c->hd_image >= 0 && primary_hd_pixel(4) == tint);
    glyph[1] -= 8;
    setup(4);
    cc->area_height = 3;
    c = canvas_of(cc, "scroll test");
    map_height = 3;
    for (int i = 0; i < 6; ++i)
        map_cells[i] = (uint16_t)(i + 1);
    put_pixel(c, ram + 1024, 15, 18, 3);
    draw_xy(0, 8);
    draw_xy(8, 16);
    upload(0, c->bytes);
    render();
    scroll_text(5);
    CHECK(c->op_count == 2 && c->ops[0].y == 3 && c->ops[1].y == 11);
    CHECK(pixel(c, c->base, 15, 13) == 3);
    CHECK(displayed_at(7, 8)); /* publication still follows the transfer */
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(3 * 16 + 7) == tint);
    CHECK(primary_hd_pixel(11 * 16 + 15) == tint);
    CHECK(test_images[c->image][13 * 16 + 15] == (KET_OPAQUE | 3));
    scroll_text(8);
    upload(0, c->bytes);
    render();
    CHECK(c->op_count == 2); /* the first glyph survives as a clipped top fragment */
    CHECK(primary_hd_pixel(3 * 16 + 15) == tint);
    scroll_text(24);
    CHECK(c->op_count == 0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0 && c->hd_image < 0);
    setup(4);
    /* Mixed graphics survive even where the original DS ink overwrites them. */
    put_pixel(c, ram + 1024, 0, 0, 3);
    put_pixel(c, ram + 1024, 7, 7, 2);
    draw(0);
    CHECK(c->op_count == 1);
    CHECK(c->display_count == 0);
    CHECK(pixel(c, c->base, 0, 0) == 3);
    CHECK(pixel(c, ram + 1024, 0, 0) == 1);
    upload(0, c->bytes);
    render();
    CHECK(test_images[c->image][0] == (KET_OPAQUE | 3));
    CHECK(test_images[c->image][7 * 16 + 7] == (KET_OPAQUE | 2));
    CHECK(primary_hd_pixel(0) == 0);
    CHECK(primary_hd_pixel(7) == tint);
    CHECK(test_images[c->image][1] == 0); /* no original silhouette leaks */
    int paints = paint_calls, ends = image_ends;
    render();
    CHECK(paint_calls == paints);
    CHECK(image_ends == ends);
    /* Clears affect the source now, and the displayed text only on upload. */
    for (int y = 0; y < 8; ++y)
        for (int x = 6; x < 8; ++x)
            put_pixel(c, ram + 1024, x, y, 0);
    khdays_ui_text_clear_area(cc, 0, 6, 0, 2, 8);
    CHECK(displayed_at(7, 0));
    upload(0, 32);
    render();
    CHECK(!displayed_at(7, 0));
    CHECK(displayed_at(4, 0));
    /* Each mapped tile controls palette, clipping and flips independently. */
    map_cells[0] |= 0x1400;
    render();
    CHECK(primary_hd_pixel(3) == (tint ^ 0x1000u));
    map_cells[0] = 0;
    render();
    for (int x = 0; x < 8; ++x)
        CHECK(primary_hd_pixel(x) == 0);
    map_cells[0] = 1;
    render();
    CHECK(primary_hd_pixel(4) == tint);
    /* Direct blanking retires the entire replacement, including PNG-only ink. */
    memset(ram + 1024, 0, c->bytes);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    CHECK(c->hd_image < 0);
    /* Full clear even when DS bytes are unchanged removes transparent-source glyphs. */
    draw(0);
    upload(0, c->bytes);
    render();
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(!c->display_count);
    /* Thousands of menu rebuilds retain bounded active commands. */
    for (int i = 0; i < 1500; ++i) {
        memset(ram + 1024, 0, c->bytes);
        khdays_ui_text_clear(cc, 0);
        draw(0);
        draw(8);
        upload(0, c->bytes);
        CHECK(c->op_count == 2);
        CHECK(c->display_count == 2);
        CHECK(c->op_room <= 64);
        render();
    }
    render();
    int unchanged_uploads = image_ends;
    upload(0, c->bytes);
    render();
    CHECK(image_ends == unchanged_uploads);
    /* An unhooked VRAM erase must remove PNG ink outside the original silhouette. */
    memset(vram + 32, 0, 32);
    observe_vram(c);
    render();
    CHECK(!displayed_at(7, 0));
    CHECK(displayed_at(15, 0));
    /* Partial uploads preserve an old glyph on the tile not uploaded. */
    draw(0);
    upload(0, 32);
    render();
    CHECK(displayed_at(7, 0));
    CHECK(displayed_at(15, 0));
    memcpy(vram + 32, ram + 4096, 32);
    chars_loaded(0, 0, ram + 4096, 32, 32);
    render();
    CHECK(!displayed_at(7, 0));
    CHECK(displayed_at(15, 0));
    khdays_ui_text_freed(NULL, ram + 1024);
    CHECK(c->detached);
    CHECK(!c->source && !c->base && !c->ops);
    render();
    CHECK(displayed_at(15, 0));
    memset(vram + 32, 0, 64);
    chars_loaded(0, 0, ram + 4096, 32, 64);
    emit();
    CHECK(canvas_count == 0);
    setup(8);
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(7) == tint);
    CHECK(test_images[c->image][0] == 0);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    setup(4);
    /* A glyph may have an entirely empty original bitmap. PNG still renders. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(7) == tint);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(!c->display_count);
    /* SDK fills carry a write event even for a PNG over an empty DS bitmap. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_memory_clear(ram + 1024, c->bytes);
    CHECK(c->op_count == 0 && c->display_count == 1);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    memset(vram + 32, 0, c->bytes);
    khdays_ui_text_memory_clear(vram + 32, c->bytes);
    render();
    CHECK(c->display_count == 0 && c->hd_image < 0);
    /* Missing glyph fallback copies equal-valued DS pixels as well as deltas. */
    draw(0);
    upload(0, c->bytes);
    render();
    glyph[1] += 8; // synthetic glyph index 1 has no PNG override
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(!c->display_count);
    CHECK(test_images[c->image][1] == (KET_OPAQUE | 1));
    glyph[1] -= 8;
    /* Two canvases sharing a partial tile must have disjoint transfer ownership. */
    setup(4);
    draw(0);
    upload(0, c->bytes);
    render();
    Canvas *old = c;
    DsCanvas *other = (DsCanvas *)(ram + 2048);
    *other = *cc;
    other->char_base = 8192;
    memset(ram + 8192, 0x55, 64);
    Canvas *newer = canvas_of(other, "overlap test");
    memcpy(vram + 33, ram + 8193, 1);
    chars_loaded(0, 0, ram + 8193, 33, 1);
    CHECK((old->owned[0] & 12u) == 0);
    CHECK(newer->owned[0] == 12u);
    c = old;
    render();
    CHECK(test_images[c->image][2] == 0);
    CHECK(primary_hd_pixel(7) == tint);
    c = newer;
    render();
    CHECK(test_images[c->image][2] == (KET_OPAQUE | 5));
    CHECK(test_images[c->image][7] == 0);
    /* A changed palette updates the PNG tint without source glyph inspection. */
    c = old;
    tint = 0xffabcdefu;
    render();
    CHECK(primary_hd_pixel(7) == tint);
    int cached_paints = paint_calls;
    const int cached_palette_reads = palette_reads;
    render();
    CHECK(paint_calls == cached_paints);
    CHECK(palette_reads == cached_palette_reads);
    slot_tint[1] = 0x1000u;
    c->bg = 1; /* Same memory and map attributes, different BG palette slot. */
    render();
    CHECK(primary_hd_pixel(7) == (tint ^ slot_tint[1]));
    setup(4);
    /* First upload of a few bytes preserves the rest of the native tile. */
    memset(vram + 32, 0x22, 64);
    ram[1024] = 0x11;
    upload(0, 1);
    render();
    CHECK(test_images[c->image][0] == (KET_OPAQUE | 1));
    CHECK(test_images[c->image][7] == (KET_OPAQUE | 2));
    setup(4);
    cc->area_width = cc->param = 16;
    cc->area_height = 8;
    c = canvas_of(cc, "text-heavy stress");
    map_width = 16;
    map_height = 8;
    for (int i = 0; i < map_width * map_height; ++i)
        map_cells[i] = (uint16_t)(i + 1);
    for (int cycle = 0; cycle < 100; ++cycle) {
        memset(ram + 1024, 0, c->bytes);
        khdays_ui_text_clear(cc, 0);
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 16; ++col)
                draw_xy(col * 8, row * 8);
        upload(0, c->bytes);
        render();
        CHECK(c->op_count == 128 && c->display_count == 128);
        CHECK(c->op_room == 128 && c->display_room == 128);
        const int stable = image_ends;
        upload(0, c->bytes);
        render();
        CHECK(image_ends == stable);
    }
    setup(4);
    draw(0);
    const GlyphOp repeated = c->ops[0];
    for (int i = 1; i < 20000; ++i) /* the fragments grow as needed */
        CHECK(record(c, &repeated));
    CHECK(c->op_count == 20000 && c->op_room >= 20000 && !c->ops_disabled);
    khdays_ui_text_clear(cc, 0);
    CHECK(!c->ops_disabled && record(c, &repeated) && c->op_count == 1);
    c->op_count = 0;
    for (int i = 0; i < 4096; ++i) {
        GlyphOp part = repeated;
        part.source_x = i % 2 ? 8 : 0;
        part.clip_l = i % 2 ? 8 : 12; // An overhanging fragment still belongs to its native draw.
        part.clip_r = part.clip_l + 1;
        CHECK(record(c, &part));
    }
    invalidate_draws(c, 0, 0, 1, 1);
    CHECK(c->op_count == 2048);
    for (int i = 0; i < c->op_count; ++i)
        CHECK(c->ops[i].source_x == 8);
    invalidate_draws(c, 8, 0, 1, 1);
    CHECK(c->op_count == 0);
    command_menu_selections(1);
    command_menu_selections(2);
    /* Blank tile 0 repeated over a screen must not allocate one canvas per cell. */
    setup(4);
    cc->area_width = cc->param = 32;
    cc->area_height = 24;
    c = canvas_of(cc, "repeated background tile");
    memcpy(vram, ram + 1024, c->bytes);
    chars_loaded(0, 1, ram + 1024, 0, c->bytes);
    map_width = map_height = 32;
    memset(map_cells, 0, sizeof(map_cells));
    test_scale = 4;
    image_requests = 0;
    peak_bytes = live_bytes;
    submission_count = 0;
    emit();
    CHECK(image_requests <= 4);
    CHECK(peak_bytes <= 8u * 1024u * 1024u);
    CHECK(submission_count <= 10);
    const int blank_requests = image_requests, blank_uploads = image_ends;
    for (int frame = 0; frame < 10; ++frame) {
        submission_count = 0;
        emit();
    }
    CHECK(image_requests == blank_requests && image_ends == blank_uploads);
    /* Even a PNG over blank tile 0 repeated 768 times needs only four images. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    chars_loaded(0, 1, ram + 1024, 0, c->bytes);
    image_requests = 0;
    submission_count = 0;
    const int repeated_paints = paint_calls;
    emit();
    CHECK(paint_calls - repeated_paints <= 2); /* One primary tile and one supplementary tile, not 768 paints. */
    CHECK(image_requests <= 4);
    CHECK(peak_bytes <= 16u * 1024u * 1024u);
    CHECK(submission_count <= 10);
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 32; ++col)
            CHECK(submitted_hd_pixel(col * 8 + 7, row * 8 + 3, tint));
    const int stable_requests = image_requests, stable_uploads = image_ends, stable_paints = paint_calls;
    for (int frame = 0; frame < 100; ++frame) {
        submission_count = 0;
        const int previous_hashes = palette_hashes;
        emit();
        CHECK(palette_hashes - previous_hashes <= 2);
    }
    CHECK(image_requests == stable_requests && image_ends == stable_uploads && paint_calls == stable_paints);
    map_cells[33] = 0x1400; /* supplementary copy: palette 1 and horizontal flip */
    submission_count = 0;
    emit();
    CHECK(submitted_hd_pixel(9, 11, tint ^ 0x1000u));
    CHECK(!submitted_hd_pixel(15, 11, tint ^ 0x1000u));
    map_cells[33] = 0;
    submission_count = 0;
    emit();
    CHECK(submitted_hd_pixel(15, 11, tint));
    test_scale = 8;
    image_requests = 0;
    peak_bytes = 0;
    submission_count = 0;
    const int scale_paints = paint_calls;
    emit();
    CHECK(paint_calls - scale_paints <= 2);
    CHECK(image_requests == 3);
    CHECK(c->extra && image_bytes[c->extra->image] == EXTRA_CELLS * 64 * sizeof(uint32_t));
    CHECK(peak_bytes <= 64u * 1024u * 1024u);
    for (int frame = 0; frame < 32; ++frame) {
        test_hofs = frame;
        test_vofs = frame / 2;
        submission_count = 0;
        const int scroll_paints = paint_calls;
        emit();
        CHECK(paint_calls - scroll_paints <= 2);
        CHECK(image_requests == 3);
        CHECK(submission_count <= 10);
        for (int row = 1; row < 23; ++row)
            for (int col = 1; col < 31; ++col)
                CHECK(submitted_hd_pixel(col * 8 + 7 - (test_hofs & 7), row * 8 + 3 - (test_vofs & 7), tint));
    }
    /* Reuse keys include palette and flips, and expire before the next update. */
    test_hofs = test_vofs = 0;
    for (int i = 0; i < 1024; ++i)
        map_cells[i] = (uint16_t)((i % 3) * 0x1400u);
    submission_count = 0;
    const int variants_paints = paint_calls;
    emit();
    CHECK(paint_calls - variants_paints <= 4);
    CHECK(submitted_hd_pixel(9, 3, tint ^ 0x1000u));
    CHECK(submitted_hd_pixel(23, 4, tint ^ 0x2000u));
    const uint32_t old_tint = tint;
    tint ^= 0x00001100u;
    memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
    submission_count = 0;
    const int changed_paints = paint_calls;
    emit();
    CHECK(paint_calls - changed_paints <= 4);
    CHECK(submitted_hd_pixel(9, 3, tint ^ 0x1000u));
    CHECK(!submitted_hd_pixel(9, 3, old_tint ^ 0x1000u));
    tint = old_tint;
    printf("HD repeated-tile stress: four images, %.2f MiB peak at scale 8\n", peak_bytes / (1024.0 * 1024.0));
    setup(8);
    draw(0);
    upload(0, c->bytes);
    map_width = map_height = 32;
    for (int i = 0; i < 1024; ++i)
        map_cells[i] = 1;
    emit();
    CHECK(submitted_hd_pixel(15, 11, tint));
    CHECK(c->extra && c->extra->hd_image >= 0);
    slot_tint[2] = 0x1000u;
    test_map_bg = 2;
    submission_count = 0;
    emit();
    CHECK(c->bg == 2 && submitted_hd_pixel(15, 11, tint ^ slot_tint[2]));
    while (canvas_count)
        forget(&canvases[0]);
    free(canvases);
    printf("HD text operations: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
