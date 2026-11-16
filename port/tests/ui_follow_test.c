/* Supplementary text images follow a particular wrapped canvas through HUD
 * attachment and explicit layouts, without joining unrelated widescreen groups. */
#include "../ui/elements.c"

static uint8_t test_io[0x2000];
uint8_t *khdays_io_host = test_io;
uint8_t *khdays_window;
static KhdaysVramPages test_pages;
static KhdaysTextureReplacement textures[16];
static int texture_count, failures, explicit_text;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "line %d: %s\n", __LINE__, #c);                                                            \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

int khdays_host_frame_scale(void)
{
    return 1;
}
int khdays_ui_mode(int part)
{
    (void)part;
    return KHDAYS_UI_ON;
}
const KhdaysVramPages *khdays_vram_pages(void)
{
    return &test_pages;
}
const KhdaysTextureReplacement *khdays_textures_replacement(int id)
{
    return &textures[id];
}
int khdays_textures_add_changing(const char *name, int w, int h, uint32_t info)
{
    (void)name;
    KhdaysTextureReplacement *t = &textures[texture_count];
    t->width = w;
    t->height = h;
    t->info = info;
    t->rgba = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
    return texture_count++;
}
uint32_t *khdays_textures_change_begin(int id)
{
    return (uint32_t *)textures[id].rgba;
}
void khdays_textures_change_end(int id)
{
    (void)id;
}
void khdays_ui_layout_frame(const char *const *names, int count)
{
    (void)names;
    (void)count;
}
int khdays_ui_layout_names(const char *id)
{
    return strncmp(id, "hud.", 4) == 0;
}
int khdays_ui_layout_place(const char *id, int *screen, int upper, int margin, const float *group, float *x, float *y,
                           float *w, float *h, uint32_t *slot, int *order)
{
    (void)screen;
    (void)upper;
    (void)margin;
    (void)group;
    (void)slot;
    if (strcmp(id, "hud.bottom") == 0) {
        *x = 100;
        *y = 200;
        *w *= 2;
        *h *= 2;
        *order = 7;
        return 1;
    }
    if (strcmp(id, "hud.top") == 0) {
        *x = 300;
        *y = 300;
        *order = 8;
        return 1;
    }
    if (explicit_text && strcmp(id, "text.canvas") == 0) {
        *x = 30;
        *y = 40;
        *w *= 2;
        *h *= 2;
        *order = 5;
        return 1;
    }
    return 0;
}

static int base_image, extra_image;

static void palette_selection(void)
{
    khdays_window = (uint8_t *)calloc(0x05000800u, 1);
    CHECK(khdays_window);
    if (!khdays_window)
        return;
    for (int engine = 0; engine < 2; ++engine) {
        const int io = engine ? 0x1000 : 0;
        const uint32_t original = khdays_ui_palette_hash(engine);
        test_io[io + 3] = 0x40; /* Extended palettes enabled, no palette bytes changed. */
        const uint32_t extended = khdays_ui_palette_hash(engine);
        CHECK(extended != original);
        for (int bg = 0; bg < 2; ++bg) {
            test_io[io + 9 + 2 * bg] = 0x20; /* Alternate extended palette slot. */
            CHECK(khdays_ui_palette_hash(engine) != extended);
            test_io[io + 9 + 2 * bg] = 0;
        }
        test_io[io + 3] = 0;
        CHECK(khdays_ui_palette_hash(engine) == original);
    }
    free(khdays_window);
    khdays_window = NULL;
}

static void emit_test(void)
{
    KhdaysUiElement a = {"text.canvas", 0, 1, 8, -144, 64, 192, base_image};
    khdays_ui_emit(KHDAYS_UI_TEXT, &a);
    a.y = 112;
    khdays_ui_emit(KHDAYS_UI_TEXT, &a);
    KhdaysUiElement e = {"text.canvas", 0, 1 | KES_DIRECT, 0, 0, 264, 200, extra_image};
    khdays_ui_emit_following(KHDAYS_UI_TEXT, &e, &a);
    e = (KhdaysUiElement){"hud.bottom", 0, 1, 0, 128, 100, 64, base_image};
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
    e = (KhdaysUiElement){"hud.top", 0, 1, 0, 0, 100, 64, base_image};
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}

int main(void)
{
    palette_selection();
    base_image = khdays_ui_image_get(1, 1);
    extra_image = khdays_ui_image_get(1, 1);
    khdays_ui_register(KHDAYS_UI_TEXT, emit_test);
    for (explicit_text = 0; explicit_text < 2; ++explicit_text) {
        khdays_ui_collect(64, 0);
        CHECK(group_of[2] == -1);
        const Entry *anchor = NULL, *extra = NULL;
        for (int i = 0; i < entry_count; ++i) {
            if (strcmp(entries[i].e.id, "text.canvas") == 0 && entries[i].dy == 112)
                anchor = &entries[i];
            if (entries[i].e.image == extra_image)
                extra = &entries[i];
        }
        CHECK(anchor && extra);
        if (anchor && extra) {
            CHECK(extra->e.x == anchor->e.x - 8 * 2);
            CHECK(extra->e.y == anchor->e.y - 112 * 2);
            CHECK(extra->e.w == 528 && extra->e.h == 400);
            CHECK(extra->e.slot == (1 | KES_DIRECT) && extra->order == anchor->order);
            if (!explicit_text)
                CHECK(extra->e.x == 100 && extra->e.y == -56);
        }
    }
    {
        /* an image a frame's elements were given with is written into another replacement: the frame's own keeps
         * its texels; one whose replacement a part asked for by number stays in it */
        const int given = images[base_image].replacement;
        uint32_t *texels = khdays_ui_image_begin(base_image);
        CHECK(texels != NULL && images[base_image].replacement != given);
        if (texels != NULL) {
            texels[0] = 7;
            khdays_ui_image_end(base_image);
            CHECK(textures[given].rgba[0] == 0 && textures[images[base_image].replacement].rgba[0] == 7);
        }
        const int fixed = khdays_ui_image_replacement(extra_image);
        khdays_ui_collect(64, 0);
        texels = khdays_ui_image_begin(extra_image);
        CHECK(texels == textures[fixed].rgba && images[extra_image].replacement == fixed);
        khdays_ui_image_end(extra_image);
    }
    for (int i = 0; i < texture_count; ++i)
        free((void *)textures[i].rgba);
    free(images);
    free(entries);
    free(group_of);
    free(group_x);
    for (int screen = 0; screen < 2; ++screen)
        for (int list = 0; list < 2; ++list) {
            free(records[screen][list]);
            free(record_ids[screen][list]);
        }
    printf("HD overlay attachment: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
