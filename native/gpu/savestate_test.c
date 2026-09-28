/* Renders a DeSmuME savestate's display state (extracted by
 * native/tools/savestate_gpu.py) with the native 2D engines and compares
 * each engine with the framebuffer DeSmuME showed.
 *
 *   khdays-gpu-test STATE.bin OUT.ppm
 *
 * OUT.ppm: the rendered upper and lower screens, the upper one being engine A
 * or B according to which matches DeSmuME's upper framebuffer better (the
 * savestate does not keep POWCNT1's swap bit). */
#define _CRT_SECURE_NO_WARNINGS
#include "gpu2d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IO_SIZE 0x2000u
#define PAL_SIZE 0x800u
#define OAM_SIZE 0x800u
#define FB_PIXELS (KHDAYS_SCREEN_W * KHDAYS_SCREEN_H)

/* Pixels that differ, and of those how many by more than 1 in a channel (the
 * master brightness works on 6 bits here, on 5 in DeSmuME). */
static int differences(const uint16_t *a, const uint16_t *fb, int *first, int *beyond_one)
{
    int n = 0;
    *first = -1;
    *beyond_one = 0;
    for (int i = 0; i < FB_PIXELS; ++i) {
        if ((a[i] & 0x7fff) != (fb[i] & 0x7fff)) {
            int far = 0;
            for (int shift = 0; shift < 15; shift += 5) {
                const int d = ((a[i] >> shift) & 31) - ((fb[i] >> shift) & 31);
                far |= d > 1 || d < -1;
            }
            *beyond_one += far;
            if (n++ == 0) {
                *first = i;
            }
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    static uint8_t blob[IO_SIZE + PAL_SIZE + OAM_SIZE + KHDAYS_VRAM_TOTAL + 2 * FB_PIXELS * 2];
    static KhdaysVramPages pages;
    static uint32_t rgb[2][FB_PIXELS];
    static uint16_t c555[2][FB_PIXELS];
    const uint8_t *io = blob, *pal = io + IO_SIZE, *oam = pal + PAL_SIZE;
    uint8_t *vram = (uint8_t *)oam + OAM_SIZE;
    const uint16_t *fb = (const uint16_t *)(vram + KHDAYS_VRAM_TOTAL);
    uint8_t *memory[KHDAYS_VRAM_BANKS];
    uint8_t cnt[KHDAYS_VRAM_BANKS];
    KhdaysGpuInput in;
    KhdaysGpu2dState state[2];
    int top, diff[2][2], far[2][2], first[2][2], overlaps;
    FILE *f;

    if (argc != 3) {
        fprintf(stderr, "usage: khdays-gpu-test STATE.bin OUT.ppm\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL || fread(blob, 1, sizeof(blob), f) != sizeof(blob)) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    fclose(f);

    for (int b = 0; b < KHDAYS_VRAM_BANKS; ++b) {
        memory[b] = vram + khdays_vram_bank_offset[b];
        cnt[b] = io[khdays_vram_cnt_reg[b]];
    }
    overlaps = khdays_vram_build_pages(cnt, memory, &pages);
    memset(&in, 0, sizeof(in));
    in.io = io;
    in.palette = pal;
    in.oam = oam;
    in.vram = &pages;
    for (int b = 0; b < 4; ++b) {
        in.bank[b] = memory[b];
    }
    /* The window flags start as the previous frame left them: render twice. */
    memset(state, 0, sizeof(state));
    for (int pass = 0; pass < 2; ++pass) {
        khdays_gpu2d_frame(&in, &state[0], 0, rgb[0], c555[0]);
        khdays_gpu2d_frame(&in, &state[1], 1, rgb[1], c555[1]);
    }
    for (int engine = 0; engine < 2; ++engine) {
        for (int screen = 0; screen < 2; ++screen) {
            diff[engine][screen] = differences(c555[engine], fb + screen * FB_PIXELS, &first[engine][screen],
                                                &far[engine][screen]);
        }
    }
    top = diff[0][0] + diff[1][1] <= diff[1][0] + diff[0][1] ? 0 : 1;
    printf("VRAMCNT %02x %02x %02x %02x %02x %02x %02x %02x %02x%s\n", cnt[0], cnt[1], cnt[2], cnt[3],
           cnt[4], cnt[5], cnt[6], cnt[7], cnt[8], overlaps ? " (banks overlap)" : "");
    for (int screen = 0; screen < 2; ++screen) {
        const int engine = screen == 0 ? top : !top;
        const int i = first[engine][screen];
        printf("%s screen = engine %c: %d of %d pixels differ, %d by more than 1",
               screen == 0 ? "upper" : "lower", 'A' + engine, diff[engine][screen], FB_PIXELS,
               far[engine][screen]);
        if (i >= 0) {
            printf(" (first at %d,%d: rendered %04x, DeSmuME %04x)", i % KHDAYS_SCREEN_W,
                   i / KHDAYS_SCREEN_W, c555[engine][i], fb[screen * FB_PIXELS + i] & 0x7fff);
        }
        printf("\n");
    }

    f = fopen(argv[2], "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", KHDAYS_SCREEN_W, 2 * KHDAYS_SCREEN_H);
    for (int screen = 0; screen < 2; ++screen) {
        const uint32_t *p = rgb[screen == 0 ? top : !top];
        for (int i = 0; i < FB_PIXELS; ++i) {
            const uint8_t px[3] = {(uint8_t)(p[i] >> 16), (uint8_t)(p[i] >> 8), (uint8_t)p[i]};
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}
