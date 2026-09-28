/* The frame the DS shows is drawn line by line while the game prepares the
 * next one; natively the game's frame work finishes long before the display
 * would have, so the whole frame is drawn at once when VBlank begins -- from
 * the registers, VRAM, palettes and OAM as the game left them for it, before
 * the VBlank interrupt and VBlank DMA change them for the next frame. */
#include "display.h"
#include "../gpu/gpu2d.h"
#include "../gpu/gpu3d.h"
#include "../host/host.h"
#include "io.h"
#include "vram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define PIXELS (KHDAYS_SCREEN_W * KHDAYS_SCREEN_H)

static uint32_t screens[2][PIXELS];  /* engine A, engine B */
static uint32_t layer3d[PIXELS];     /* what the 3D engine drew at the last VBlank */
static KhdaysGpu2dState states[2];

/* KHDAYS_PROFILE: the time the frame's rendering takes, every 300 frames */
static int profile;

static u32 *shots;
static int shot_count;
static const char *shot_dir;

/* KHDAYS_HEADLESS: no one sees the frames, so only those a shot saves are
 * drawn (with the two before them, which settle the windows' vertical latch,
 * the one state a frame carries into the next). Nothing the game reads comes
 * from the drawing -- display capture is not modelled -- so the game runs the
 * same; the 3D engine still swaps its buffers at each VBlank. */
static int headless;

static int drawn(u32 frame)
{
    if (!headless) {
        return 1;
    }
    for (int n = 0; n < shot_count; ++n) {
        if (frame <= shots[n] && frame + 2 >= shots[n]) {
            return 1;
        }
    }
    return 0;
}

void khdays_display_init(void)
{
    const char *text = getenv("KHDAYS_SHOTS");
    shot_dir = getenv("KHDAYS_SHOT_DIR");
    profile = getenv("KHDAYS_PROFILE") != NULL;
    headless = getenv("KHDAYS_HEADLESS") != NULL;
    if (shot_dir == NULL) {
        shot_dir = "shots";
    }
    if (text != NULL) {
        const char *p = text;
        shots = (u32 *)calloc(strlen(text) + 1, sizeof(u32));
        while (*p) {
            char *end;
            const unsigned long frame = strtoul(p, &end, 10);
            if (end == p) {
                ++p;
                continue;
            }
            shots[shot_count++] = (u32)frame;
            p = end;
        }
        CreateDirectoryA(shot_dir, NULL);
    }
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)(v >> 8), f);
}

static void put32(FILE *f, unsigned v)
{
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

/* Both screens, upper above lower, as a 24-bit BMP. */
static void save_shot(u32 frame, const uint32_t *upper, const uint32_t *lower)
{
    char path[MAX_PATH];
    FILE *f;
    const unsigned w = KHDAYS_SCREEN_W, h = 2 * KHDAYS_SCREEN_H;
    snprintf(path, sizeof(path), "%s/frame_%05u.bmp", shot_dir, frame);
    if (fopen_s(&f, path, "wb") != 0) {
        fprintf(stderr, "display: cannot write %s\n", path);
        return;
    }
    fputc('B', f);
    fputc('M', f);
    put32(f, 54 + w * h * 3);
    put32(f, 0);
    put32(f, 54);
    put32(f, 40);
    put32(f, w);
    put32(f, h);
    put16(f, 1);
    put16(f, 24);
    put32(f, 0);
    put32(f, w * h * 3);
    put32(f, 2835);
    put32(f, 2835);
    put32(f, 0);
    put32(f, 0);
    for (int y = (int)h - 1; y >= 0; --y) {  /* bottom-up */
        const uint32_t *row = y < KHDAYS_SCREEN_H ? upper + y * w : lower + (y - KHDAYS_SCREEN_H) * w;
        for (unsigned x = 0; x < w; ++x) {
            fputc((int)(row[x] & 0xff), f);
            fputc((int)((row[x] >> 8) & 0xff), f);
            fputc((int)((row[x] >> 16) & 0xff), f);
        }
    }
    fclose(f);
    fprintf(stderr, "display: frame %u saved to %s (DISPCNT A %08x B %08x, MASTER_BRIGHT A %04x B %04x, "
                    "POWCNT1 %04x)\n",
            frame, path, IO32(0x000), IO32(0x1000), IO16(0x06c), IO16(0x106c), IO16(0x304));
}

void khdays_display_vblank(u32 frame)
{
    KhdaysGpuInput in;
    const uint32_t *upper, *lower;
    LARGE_INTEGER t0, t1, t2;
    if (!drawn(frame)) {
        khdays_gpu3d_vblank(khdays_io_host, khdays_vram_pages(), NULL);
        return;
    }
    memset(&in, 0, sizeof(in));
    in.io = khdays_io_host;
    in.palette = (const u8 *)0x05000000;
    in.oam = (const u8 *)0x07000000;
    in.vram = khdays_vram_pages();
    for (int bank = 0; bank < 4; ++bank) {
        in.bank[bank] = khdays_vram_bank(bank);
    }
    in.layer3d = layer3d;
    QueryPerformanceCounter(&t0);
    khdays_gpu2d_frame(&in, &states[0], 0, screens[0], NULL);
    khdays_gpu2d_frame(&in, &states[1], 1, screens[1], NULL);
    QueryPerformanceCounter(&t1);
    /* the 3D engine renders during the next frame what it has now */
    khdays_gpu3d_vblank(khdays_io_host, khdays_vram_pages(), layer3d);
    QueryPerformanceCounter(&t2);
    if (profile) {
        LARGE_INTEGER hz;
        static double ms2d, ms3d;
        static int frames;
        QueryPerformanceFrequency(&hz);
        ms2d += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
        ms3d += (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / (double)hz.QuadPart;
        if (++frames == 300) {
            fprintf(stderr, "display: %.2f ms 2D, %.2f ms 3D per frame\n", ms2d / frames, ms3d / frames);
            ms2d = ms3d = 0;
            frames = 0;
        }
    }
    /* POWCNT1 bit 15: 1 = engine A on the upper screen */
    if (IO16(0x304) & 0x8000) {
        upper = screens[0];
        lower = screens[1];
    } else {
        upper = screens[1];
        lower = screens[0];
    }
    khdays_host_present(upper, lower);
    for (int n = 0; n < shot_count; ++n) {
        if (shots[n] == frame) {
            save_shot(frame, upper, lower);
        }
    }
}
