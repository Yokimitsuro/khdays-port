/* The HD pictures on the graphics card (host/render/gpu_hd.c): a larger buffer keeps every picture it had,
 * past the most it holds the 3D's pictures (KHDAYS_REPLACEMENT_ON_DEMAND) leave the card and come back as
 * the polygons drawn need them, a larger table there makes room by letting pictures go, and no picture ever
 * covers another's texels or the white texel those not on the card read.
 *   ctest --test-dir build/port/obj-release-x64 -R khdays-gpu-hd --output-on-failure */
#include "../host/render/gpu_hd.h"
#include "../runtime/mods/textures.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static SDL_GPUDevice *device;

/* The replacements, as runtime/mods/textures.c keeps them: each picture one colour of its own. */
static KhdaysTextureReplacement *pictures;
static int npictures, room;

int khdays_textures_replacements(void)
{
    return npictures;
}

const KhdaysTextureReplacement *khdays_textures_replacement(int id)
{
    return id >= 0 && id < npictures ? &pictures[id] : NULL;
}

uint32_t khdays_textures_version(int id)
{
    (void)id;
    return 0;
}

void khdays_textures_read_begin(int id)
{
    (void)id;
}

void khdays_textures_read_end(int id)
{
    (void)id;
}

void khdays_log(const char *format, ...)
{
    va_list a;
    va_start(a, format);
    vfprintf(stderr, format, a);
    va_end(a);
    fputc('\n', stderr);
}

static uint32_t colour_of(int id)
{
    return 0xff000000u | ((uint32_t)(id + 1) * 0x2f3b47u & 0x7fffffu);
}

static int add(int w, int h, uint32_t info)
{
    uint32_t *texels = (uint32_t *)malloc((size_t)w * (size_t)h * 4u);
    if (npictures == room) {
        room = room ? room * 2 : 64;
        pictures = (KhdaysTextureReplacement *)realloc(pictures, (size_t)room * sizeof(*pictures));
    }
    for (int i = 0; i < w * h; ++i) {
        texels[i] = colour_of(npictures);
    }
    pictures[npictures].width = w;
    pictures[npictures].height = h;
    pictures[npictures].rgba = texels;
    pictures[npictures].info = info;
    return npictures++;
}

static void forget_all(void)
{
    for (int i = 0; i < npictures; ++i) {
        free((void *)pictures[i].rgba);
    }
    npictures = 0;
}

/* `count` words of the card's buffer from word `at`. */
static uint32_t *read_words(uint32_t at, uint32_t count)
{
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *copy;
    SDL_GPUBufferRegion from;
    SDL_GPUTransferBufferLocation to;
    SDL_GPUFence *fence;
    uint32_t *out = (uint32_t *)malloc(count * 4u);
    SDL_zero(info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = count * 4u;
    transfer = SDL_CreateGPUTransferBuffer(device, &info);
    cmd = SDL_AcquireGPUCommandBuffer(device);
    copy = SDL_BeginGPUCopyPass(cmd);
    from.buffer = khdays_gpu_hd_buffer();
    from.offset = at * 4u;
    from.size = count * 4u;
    to.transfer_buffer = transfer;
    to.offset = 0;
    SDL_DownloadFromGPUBuffer(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    memcpy(out, SDL_MapGPUTransferBuffer(device, transfer, false), count * 4u);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return out;
}

/* The words picture `id` takes on the card (gpu_hd.c words_of: a still picture's smaller copies follow). */
static uint32_t words_of(int id)
{
    uint32_t w = (uint32_t)pictures[id].width, h = (uint32_t)pictures[id].height, words = w * h;
    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        words += w * h;
    }
    return words;
}

/* Every picture checked: on the card with its own texels whole (its smaller copies too, one colour) as
 * khdays_gpu_hd_on_card says, or its entry the white texel. The pictures on the card. */
static int check_all(const char *when)
{
    int on = 0;
    for (int id = 0; id < npictures; ++id) {
        uint32_t *entry = read_words((uint32_t)id * 4u, 4);
        const int card = khdays_gpu_hd_on_card((uint32_t)id);
        if (card) {
            const uint32_t n = words_of(id);
            uint32_t *t = read_words(entry[0], n), wrong = 0;
            for (uint32_t i = 0; i < n; ++i) {
                wrong += t[i] != colour_of(id);
            }
            if (wrong != 0 || entry[1] != (uint32_t)pictures[id].width || entry[2] != (uint32_t)pictures[id].height) {
                fprintf(stderr, "%s: picture %d (%dx%d) at %u: %u of %u texels wrong, entry %ux%u\n", when, id,
                        pictures[id].width, pictures[id].height, entry[0], wrong, n, entry[1], entry[2]);
                ++failures;
            }
            free(t);
            ++on;
        } else {
            uint32_t *t = read_words(entry[0], 1);
            if (entry[1] != 1 || entry[2] != 1 || t[0] != 0xffffffffu) {
                fprintf(stderr, "%s: picture %d not on the card: entry %ux%u at %u, texel %08x (not white)\n", when, id,
                        entry[1], entry[2], entry[0], t[0]);
                ++failures;
            }
            free(t);
        }
        free(entry);
    }
    return on;
}

static int sync_drawn(const int *ids, int n)
{
    KhdaysG3dPolygon *polys = (KhdaysG3dPolygon *)calloc((size_t)(n > 0 ? n : 1), sizeof(KhdaysG3dPolygon));
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUFence *fence;
    int ok;
    for (int i = 0; i < n; ++i) {
        polys[i].replacement = ids[i];
    }
    khdays_gpu_hd_next_frame();
    ok = khdays_gpu_hd_sync_drawn(cmd, polys, n);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    free(polys);
    return ok;
}

static int on_card(const int *ids, int n)
{
    int on = 0;
    for (int i = 0; i < n; ++i) {
        on += khdays_gpu_hd_on_card((uint32_t)ids[i]);
    }
    return on;
}

/* A larger buffer keeps what the card had: texels alone (the table as it was), then the table too. */
static void growth_keeps(void)
{
    khdays_gpu_hd_set_most(0);
    CHECK(khdays_gpu_hd_open(device));
    for (int i = 0; i < 10; ++i) {
        add(16, 16, 0);
    }
    CHECK(sync_drawn(NULL, 0));
    CHECK(check_all("ten") == 10);
    add(256, 256, 0); /* past the first buffer's texels, not its table */
    CHECK(sync_drawn(NULL, 0));
    CHECK(check_all("texels grown") == 11);
    for (int i = 0; i < 60; ++i) {
        add(8, 8, 0);
    }
    CHECK(sync_drawn(NULL, 0));
    CHECK(check_all("table grown") == 71);
    khdays_gpu_hd_close();
    forget_all();
}

/* Past the most: the 3D's pictures drawn now on the card, the others let go as room is needed. */
static void come_and_go(void)
{
    int p[7], all[6];
    /* the table's 64 entries, the white texel, two kept pictures (341 words each) and three 3D pictures (1365) */
    khdays_gpu_hd_set_most(4u * (64u * 4u + 1u + 2u * 341u + 3u * 1365u));
    CHECK(khdays_gpu_hd_open(device));
    add(16, 16, 0);
    add(16, 16, 0);
    for (int i = 0; i < 6; ++i) {
        p[i] = all[i] = add(32, 32, KHDAYS_REPLACEMENT_ON_DEMAND);
    }
    CHECK(sync_drawn(p, 3));
    CHECK(on_card(p, 3) == 3 && on_card(p + 3, 3) == 0);
    CHECK(check_all("three drawn") == 5);
    CHECK(sync_drawn(p + 3, 2));
    CHECK(on_card(p + 3, 2) == 2 && on_card(p, 3) == 1);
    CHECK(check_all("two others") == 5);
    {
        const int some[3] = {p[0], p[5], p[3]};
        CHECK(sync_drawn(some, 3));
        CHECK(on_card(some, 3) == 3);
        CHECK(check_all("one kept, two back") == 5);
    }
    CHECK(sync_drawn(all, 6)); /* more than fits at once: three drawn, three white */
    CHECK(on_card(all, 6) == 3);
    CHECK(check_all("all six") == 5);
    p[6] = add(64, 64, KHDAYS_REPLACEMENT_ON_DEMAND); /* larger than all the room: never on the card */
    CHECK(sync_drawn(p + 6, 1));
    CHECK(!khdays_gpu_hd_on_card((uint32_t)p[6]));
    check_all("too large");
    khdays_gpu_hd_close();
    forget_all();
}

/* A larger table at the most: the texels' top let go for it, the pictures drawn placed the next time. */
static void table_at_the_most(void)
{
    int first[60], more[10];
    khdays_gpu_hd_set_most(4u * (64u * 4u + 6000u));
    CHECK(khdays_gpu_hd_open(device));
    for (int i = 0; i < 60; ++i) {
        first[i] = add(8, 8, KHDAYS_REPLACEMENT_ON_DEMAND); /* 85 words each */
    }
    CHECK(sync_drawn(first, 60));
    CHECK(on_card(first, 60) == 60);
    for (int i = 0; i < 10; ++i) {
        more[i] = add(8, 8, KHDAYS_REPLACEMENT_ON_DEMAND);
    }
    CHECK(sync_drawn(more, 10)); /* the table grows: room taken from the top's pictures */
    check_all("table grown at the most");
    CHECK(sync_drawn(more, 10));
    CHECK(on_card(more, 10) == 10);
    check_all("drawn again");
    khdays_gpu_hd_close();
    khdays_gpu_hd_set_most(0);
    forget_all();
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
                                 true, NULL);
    if (device == NULL) {
        fprintf(stderr, "no graphics card: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }
    growth_keeps();
    come_and_go();
    table_at_the_most();
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    free(pictures);
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    printf("HD pictures on the card: all checks passed\n");
    return 0;
}
