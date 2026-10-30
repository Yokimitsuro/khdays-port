/* The display (display.h): the whole frame drawn when VBlank begins, from the registers, VRAM, palettes
 * and OAM as the game left them, sent to the window as a frame package. With a window a VBlank is not
 * drawn again when nothing it reads changed, or both screens stay the same single colours. */
#include "display.h"
#include "../core/events.h"
#include "frame_package.h"
#include "field_wipe.h"
#include "../../gfx/describe.h"
#include "gfx_check.h"
#include "../../gpu/gpu2d.h"
#include "../../gfx/g3d.h"
#include "../mods/sheet_colours.h"
#include "../mods/tiles2d.h"
#include "../movies/video.h"
#include "../movies/scene_trace.h"
#include "../mods/textures.h"
#include "../core/window.h"
#include "../../ui/elements.h"
#include "../../ui/parts.h"
#include "../../ui/hud.h"
#include "../../ui/kh2_hp_hud.h"
#include "../../ui/kh2_portrait_hud.h"
#include "../../ui/kh2_party_hud.h"
#include "../../ui/kh2_hud_labels.h"
#include "../../ui/kh2_enemy_hud.h"
#include "../../ui/kh2_reticles.h"
#include "../../ui/heart_points.h"
#include "../../ui/prize_effects.h"
#include "../../ui/kh2_information.h"
#include "../../ui/kh2_obtained.h"
#include "../../ui/hud_options.h"
#include "../../ui/kh2_mission_panel.h"
#include "../../ui/minimap.h"
#include "../../ui/pause.h"
#include "../../ui/text.h"
#include "../../ui/situation.h"
#include "../core/session_log.h"
#include "../../gpu/gpu3d.h"
#include "../../host/app/host.h"
#include "../../host/app/instance.h"
#include "../core/io.h"
#include "../menus/retry.h"
#include "../core/runtime.h"
#include "../core/vram.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

_Static_assert(KSI_3D_PROGRAMS == KHDAYS_RETICLE_COUNT + KHDAYS_HEART_PROGRAMS + KHDAYS_PRIZE_PROGRAMS,
               "3D sequence package capacity");

#define PIXELS (KHDAYS_SCREEN_W * KHDAYS_SCREEN_H)

static void ui_report(void); /* the check's counts at exit */

static uint32_t layer3d[PIXELS]; /* what the 3D engine drew at the last VBlank */
static KhdaysGpu2dState states[2];

/* `local` is the package filled when there is no window (with a window the
 * host gives one, khdays_host_frame_begin). `prepared` is the package that
 * already holds the 3D the next frame shows (captured when it was drawn, at
 * the VBlank before). */
static KhdaysFramePackage *local;
static KhdaysFramePackage *prepared;

/* The texture VRAM as the 3D engine last read it, and for each 16 KB page a
 * count of its changes (frame_package.h). */
#define TEX_PAGES (KHDAYS_FRAME_TEX_BYTES / KHDAYS_FRAME_VRAM_PAGE)
#define TEXPAL_PAGES (KHDAYS_FRAME_TEXPAL_BYTES / KHDAYS_FRAME_VRAM_PAGE)
static uint8_t tex_seen[KHDAYS_FRAME_TEX_BYTES], texpal_seen[KHDAYS_FRAME_TEXPAL_BYTES];
static uint32_t tex_version[TEX_PAGES], texpal_version[TEXPAL_PAGES];

/* For KHDAYS_WANT_2D: the 2D memory as last seen, its change counts, and the
 * flags of each screen's windows (moved on at the same frames as the
 * reference's). */
static uint8_t mem2d_seen[KHDAYS_FRAME_2D_PAGES * KHDAYS_FRAME_VRAM_PAGE];
static uint32_t mem2d_version[KHDAYS_FRAME_2D_PAGES];
static KhdaysGfxLatch latch2d[2];

/* KHDAYS_RECORD_FRAMES=DIR: each package drawn (with KHDAYS_SHOTS, only those frames) written there
 * whole as frame_NNNNN.pkg -- the player's game data: saves/frames, never the repository.
 * KHDAYS_CHECK_COMPOSITION: every frame drawn, even headless, and engine A composed again from its
 * records and the 3D, which must give the same picture (and the wide records the same records). */
static const char *record_dir;
static int check_composition;

/* KHDAYS_WIDE=COLUMNS: without a window, make the 3D lists that many DS
 * pixels wide, as the window does in widescreen (for recordings). */
static int wide_columns;

/* KHDAYS_TRACE_3D_CALLERS=FRAME: the polygons the game sends in the eight
 * VBlanks up to that frame, grouped by the native call chain that sent them.
 * Each chain is printed once with its first polygon (its box on the screen,
 * its w, its attributes); at the end, how many polygons each chain sent. */
static u32 trace_3d_frame;
#define MAX_CHAINS 96
static struct {
    char chain[960];
    int polygons;
} chains[MAX_CHAINS];
static int chain_count;

static void polygon_caller(const KhdaysG3dPolygon *p, const KhdaysG3dVertex *v)
{
    char chain[960];
    float x0 = 1e9f, x1 = -1e9f, w0 = 1e9f, w1 = -1e9f;
    khdays_diag_callers(chain, sizeof(chain), 2); /* not this hook, not emit_polygon */
    if (v[0].w == 4096.0f) {
        /* a quad drawn in screen space (an orthographic projection: w 1) */
        fprintf(stderr, "display: screen-space polygon x %.1f-%.1f y %.1f-%.1f attr %08x from %s\n", v[0].x,
                v[p->count > 2 ? 2 : 0].x, p->ymin, p->ymax, p->attr, chain);
    }
    for (int i = 0; i < chain_count; ++i) {
        if (strcmp(chains[i].chain, chain) == 0) {
            ++chains[i].polygons;
            return;
        }
    }
    for (int k = 0; k < p->count; ++k) {
        x0 = v[k].x < x0 ? v[k].x : x0;
        x1 = v[k].x > x1 ? v[k].x : x1;
        w0 = v[k].w < w0 ? v[k].w : w0;
        w1 = v[k].w > w1 ? v[k].w : w1;
    }
    fprintf(stderr, "display: 3D polygons from %s\n  first: x %.1f-%.1f y %.1f-%.1f, w %.0f-%.0f, attr %08x tex %08x\n",
            chain, x0, x1, p->ymin, p->ymax, w0, w1, p->attr, p->tex);
    if (chain_count < MAX_CHAINS) {
        strncpy_s(chains[chain_count].chain, sizeof(chains[0].chain), chain, _TRUNCATE);
        chains[chain_count++].polygons = 1;
    }
}

/* What the display read at the last frame drawn (for `unchanged`). */
static struct {
    int valid;
    u8 io_a[0x70], io_b[0x70], vramcnt[10], io3d[0x100];
    u8 palette[0x800], oam[0x800];
    u8 vram[KHDAYS_VRAM_TOTAL];
    u32 layer3d_version;
    u32 alike; /* VBlanks since then that showed the same */
} last_drawn;
static u32 layer3d_version;    /* counts the changes of layer3d */
static u32 layer3d_list = ~0u; /* the list layer3d holds (list3d_serial) */
static int software_3d_off;    /* the window draws the 3D itself, so layer3d is left empty */

/* Whether this VBlank shows the last frame drawn again: nothing the display
 * reads has changed since (and no new 3D list is swapped in now). When
 * something has changed, what it reads now is kept for the next VBlank's
 * comparison. */
static int unchanged(void)
{
    int same = last_drawn.valid && !khdays_g3d_swap_pending() && last_drawn.layer3d_version == layer3d_version &&
               memcmp(last_drawn.io_a, khdays_io_host, sizeof(last_drawn.io_a)) == 0 &&
               memcmp(last_drawn.io_b, khdays_io_host + 0x1000, sizeof(last_drawn.io_b)) == 0 &&
               memcmp(last_drawn.vramcnt, khdays_io_host + 0x240, sizeof(last_drawn.vramcnt)) == 0 &&
               memcmp(last_drawn.io3d, khdays_io_host + 0x300, sizeof(last_drawn.io3d)) == 0 &&
               memcmp(last_drawn.palette, (const u8 *)KHDAYS_W(0x05000000), sizeof(last_drawn.palette)) == 0 &&
               memcmp(last_drawn.oam, (const u8 *)KHDAYS_W(0x07000000), sizeof(last_drawn.oam)) == 0;
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS && same; ++bank) {
        same = memcmp(last_drawn.vram + khdays_vram_bank_offset[bank], khdays_vram_bank(bank),
                      khdays_vram_bank_size[bank]) == 0;
    }
    if (same) {
        ++last_drawn.alike;
        return 1;
    }
    memcpy(last_drawn.io_a, khdays_io_host, sizeof(last_drawn.io_a));
    memcpy(last_drawn.io_b, khdays_io_host + 0x1000, sizeof(last_drawn.io_b));
    memcpy(last_drawn.vramcnt, khdays_io_host + 0x240, sizeof(last_drawn.vramcnt));
    memcpy(last_drawn.io3d, khdays_io_host + 0x300, sizeof(last_drawn.io3d));
    memcpy(last_drawn.palette, (const u8 *)KHDAYS_W(0x05000000), sizeof(last_drawn.palette));
    memcpy(last_drawn.oam, (const u8 *)KHDAYS_W(0x07000000), sizeof(last_drawn.oam));
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        memcpy(last_drawn.vram + khdays_vram_bank_offset[bank], khdays_vram_bank(bank), khdays_vram_bank_size[bank]);
    }
    last_drawn.layer3d_version = layer3d_version;
    last_drawn.valid = 1;
    return 0;
}

/* Whether engine `engine`'s whole screen is one colour now, and which one
 * (as gpu2d.c draws it: the display off or blanked is white; MASTER_BRIGHT all
 * the way, factor 16, brightens every intensity to 63 or darkens it to 0). */
static int one_colour(int engine, uint32_t *colour)
{
    const u32 dispcnt = IO32(engine ? 0x1000 : 0x000);
    const u16 master = IO16(engine ? 0x106c : 0x06c);
    const unsigned display = (dispcnt >> 16) & (engine ? 1u : 3u);
    const unsigned mode = master >> 14, factor = master & 31;
    if (display == 0 || (display == 1 && (dispcnt & 0x80))) {
        *colour = 0xffffff;
        return 1;
    }
    if (factor >= 16 && (mode == 1 || mode == 2)) {
        *colour = mode == 1 ? 0xffffff : 0x000000;
        return 1;
    }
    return 0;
}

/* The colours of the last frame drawn, when both its screens were a single
 * colour each (`shown_one_colour`). */
static int shown_one_colour;
static uint32_t shown_colour[2];

/* KHDAYS_TRACE_2D="FRAME FRAME ...": at those frames, print what both 2D
 * engines show (khdays_gpu2d_describe) on stderr. Headless, also list them in
 * KHDAYS_SHOTS so they are drawn. */
static const char *trace_2d;
static int trace_situation; /* KHDAYS_TRACE_SITUATION: the one-screen inputs taken and traced */

/* KHDAYS_PROFILE: every 300 frames, the time a frame takes to draw and the 2 KB pages of VRAM,
 * palettes and OAM that changed since the frame before. */
static int profile;

#define GATE_PAGE 2048u
static u8 *gate_copy; /* the banks, the palettes and OAM as last seen */
static u32 gate_bytes;

/* Counts the pages that changed since the last call: [0] VRAM, [1] palettes, [2] OAM. */
static void gate_pages(u32 changed[3])
{
    u32 at = 0;
    changed[0] = changed[1] = changed[2] = 0;
    if (gate_copy == NULL) {
        gate_bytes = 0x800 + 0x800;
        for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
            gate_bytes += khdays_vram_bank_size[bank];
        }
        gate_copy = (u8 *)calloc(gate_bytes, 1);
        if (gate_copy == NULL) {
            return;
        }
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        const u8 *now = khdays_vram_bank(bank);
        for (u32 page = 0; page < khdays_vram_bank_size[bank]; page += GATE_PAGE, at += GATE_PAGE) {
            if (memcmp(gate_copy + at, now + page, GATE_PAGE) != 0) {
                memcpy(gate_copy + at, now + page, GATE_PAGE);
                ++changed[0];
            }
        }
    }
    if (memcmp(gate_copy + at, (const u8 *)KHDAYS_W(0x05000000), 0x800) != 0) {
        memcpy(gate_copy + at, (const u8 *)KHDAYS_W(0x05000000), 0x800);
        ++changed[1];
    }
    at += 0x800;
    if (memcmp(gate_copy + at, (const u8 *)KHDAYS_W(0x07000000), 0x800) != 0) {
        memcpy(gate_copy + at, (const u8 *)KHDAYS_W(0x07000000), 0x800);
        ++changed[2];
    }
}

static u32 *shots;
static int shot_count;
static const char *shot_dir;
static int engine_upper; /* engine A on the upper screen at the VBlank before */
static u32 swap_history; /* bit n: engine A changed screens n VBlanks ago */
static u32 engine_swaps; /* how many of the last 8 VBlanks engine A changed screens in */

/* KHDAYS_HEADLESS: only the frames a shot saves are drawn, with the two before them (they settle the
 * windows' vertical latch). The game reads nothing from the drawing, so it runs the same. */
static int headless;

/* F12 (khdays_display_mark, from the window's thread): the frame two VBlanks later saved as the DS's
 * two screens in <saves>/marks/, with engine A's layer and blending registers and the pause's pieces
 * in the session log. The frame before it is drawn whole too (its 3D is the marked frame's). */
static volatile LONG mark_requests;
static u32 mark_frame = ~0u;
static unsigned mark_count;

void khdays_display_mark(void)
{
    InterlockedIncrement(&mark_requests);
}

static int marked(u32 frame)
{
    return mark_frame != ~0u && (frame == mark_frame || frame + 1u == mark_frame);
}

/* KHDAYS_SCENE_SHOTS=<dir>: while a scene of the field's event runs (runtime/movies/scene_trace.h),
 * both screens every 30 frames as <dir>/<scene>_<frame>.bmp. */
static const char *scene_shot_dir;

static int scene_shot_at(u32 frame)
{
    return scene_shot_dir != NULL && frame % 30u == 0u && khdays_scene_current()[0] != '\0';
}

/* Whether KHDAYS_SHOTS (or F12) saves this frame. */
static int shot_at(u32 frame)
{
    if (marked(frame) || scene_shot_at(frame)) {
        return 1;
    }
    for (int n = 0; n < shot_count; ++n) {
        if (shots[n] == frame) {
            return 1;
        }
    }
    return 0;
}

/* Whether this frame is drawn at all (headless, only the frames something
 * needs). */
static int drawn(u32 frame)
{
    if (!headless || check_composition || khdays_gfx_check_at(frame) || khdays_ui_any_checked()) {
        return 1; /* (a part under check compares every frame) */
    }
    for (int n = 0; n < shot_count; ++n) {
        if (frame <= shots[n] && frame + 2 >= shots[n]) {
            return 1;
        }
    }
    if (scene_shot_dir != NULL && khdays_scene_current()[0] != '\0' && (frame % 30u == 0u || frame % 30u >= 28u)) {
        return 1;
    }
    return 0;
}

void khdays_display_init(void)
{
    const char *text = getenv("KHDAYS_SHOTS");
    shot_dir = getenv("KHDAYS_SHOT_DIR");
    scene_shot_dir = getenv("KHDAYS_SCENE_SHOTS");
    if (scene_shot_dir != NULL) {
        CreateDirectoryA(scene_shot_dir, NULL);
    }
    profile = getenv("KHDAYS_PROFILE") != NULL;
    headless = getenv("KHDAYS_HEADLESS") != NULL;
    record_dir = getenv("KHDAYS_RECORD_FRAMES");
    check_composition = getenv("KHDAYS_CHECK_COMPOSITION") != NULL;
    wide_columns = getenv("KHDAYS_WIDE") != NULL ? atoi(getenv("KHDAYS_WIDE")) : 0;
    trace_2d = getenv("KHDAYS_TRACE_2D");
    trace_situation = getenv("KHDAYS_TRACE_SITUATION") != NULL;
    trace_3d_frame = getenv("KHDAYS_TRACE_3D_CALLERS") != NULL ? (u32)atoi(getenv("KHDAYS_TRACE_3D_CALLERS")) : 0;
    if (record_dir != NULL) {
        CreateDirectoryA(record_dir, NULL);
    }
    khdays_ui_parts_init();
    khdays_ui_text_init();
    khdays_ui_hud_init();
    khdays_ui_pause_init();
    if (khdays_ui_any_checked()) {
        atexit(ui_report);
    }
    if (shot_dir == NULL) {
        shot_dir = "shots";
    }
    if (text != NULL) {
        const char *p = text;
        shots = (u32 *)calloc(strlen(text) + 1, sizeof(u32));
        while (shots != NULL && *p) {
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

/* Writes both screens, upper above lower, as a 24-bit BMP. */
static int write_bmp(const char *path, const uint32_t *upper, const uint32_t *lower)
{
    FILE *f;
    const unsigned w = KHDAYS_SCREEN_W, h = 2 * KHDAYS_SCREEN_H;
    if (fopen_s(&f, path, "wb") != 0) {
        fprintf(stderr, "display: cannot write %s\n", path);
        return 0;
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
    for (int y = (int)h - 1; y >= 0; --y) { /* bottom-up */
        const uint32_t *row = y < KHDAYS_SCREEN_H ? upper + y * w : lower + (y - KHDAYS_SCREEN_H) * w;
        for (unsigned x = 0; x < w; ++x) {
            fputc((int)(row[x] & 0xff), f);
            fputc((int)((row[x] >> 8) & 0xff), f);
            fputc((int)((row[x] >> 16) & 0xff), f);
        }
    }
    fclose(f);
    return 1;
}

/* Saves both screens as KHDAYS_SHOT_DIR/frame_NNNNN.bmp. */
static void save_shot(u32 frame, const uint32_t *upper, const uint32_t *lower)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/frame_%05u.bmp", shot_dir, frame);
    if (!write_bmp(path, upper, lower)) {
        return;
    }
    fprintf(stderr,
            "display: frame %u saved to %s (DISPCNT A %08x B %08x, MASTER_BRIGHT A %04x B %04x, "
            "POWCNT1 %04x)\n",
            frame, path, IO32(0x000), IO32(0x1000), IO16(0x06c), IO16(0x106c), IO16(0x304));
}

/* The marked frame (F12): its screens in <saves>/marks/, and in the session log engine A's
 * layers (DISPCNT, each BG's control and scroll), its blending and windows, and the pause's
 * pieces as the pause part reads them. */
static void save_mark(u32 frame, const uint32_t *upper, const uint32_t *lower)
{
    char dir[MAX_PATH], path[MAX_PATH];
    KhdaysPauseView pv;
    snprintf(dir, sizeof(dir), "%s/marks", khdays_host_user_folder());
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof(path), "%s/mark_%u_frame_%05u.bmp", dir, ++mark_count, frame);
    if (write_bmp(path, upper, lower)) {
        khdays_log("mark %u: frame %u saved to %s", mark_count, frame, path);
    }
    khdays_log("mark %u: engine A DISPCNT %08x, BG0-3 CNT %04x %04x %04x %04x, scroll %u,%u %u,%u %u,%u %u,%u, "
               "BLDCNT %04x BLDALPHA %04x BLDY %04x, WININ %04x WINOUT %04x; engine B DISPCNT %08x",
               mark_count, IO32(0x000), IO16(0x008), IO16(0x00a), IO16(0x00c), IO16(0x00e), IO16(0x010) & 0x1ffu,
               IO16(0x012) & 0x1ffu, IO16(0x014) & 0x1ffu, IO16(0x016) & 0x1ffu, IO16(0x018) & 0x1ffu,
               IO16(0x01a) & 0x1ffu, IO16(0x01c) & 0x1ffu, IO16(0x01e) & 0x1ffu, IO16(0x050), IO16(0x052), IO16(0x054),
               IO16(0x048), IO16(0x04a), IO32(0x1000));
    if (khdays_pause_view(&pv)) {
        for (int i = 0; i < pv.count; ++i) {
            khdays_log("mark %u: the pause in BG%d, piece %d key %d at cells %d,%d size %dx%d lit %d", mark_count,
                       pv.layer, i, pv.piece[i].key, pv.piece[i].x, pv.piece[i].y, pv.piece[i].w, pv.piece[i].h,
                       pv.piece[i].lit);
        }
    } else {
        khdays_log("mark %u: no pause the pause part reads", mark_count);
    }
}

/* The HUD's own 3D drawing starts (1, or 2 shown over the 2D) or ends (0): prepare.py's hooks in the
 * field's panel and the game over screen. */
void khdays_hud_3d(int on)
{
    khdays_g3d_set_hud(on);
}

/* Whether `frame` is one of the numbers in `list`. */
static int listed(const char *list, u32 frame)
{
    while (*list) {
        char *end;
        const unsigned long n = strtoul(list, &end, 10);
        if (end == list) {
            ++list;
            continue;
        }
        if (n == frame) {
            return 1;
        }
        list = end;
    }
    return 0;
}

static u32 list3d_serial; /* moves on with each new list shown */
static u64 list3d_time;   /* the VBlank the list was first shown at */
static int list3d_cut;    /* a cut: nothing to draw between it and the list before */
static u32 list3d_scene = ~0u;

u32 khdays_display_3d_serial(void)
{
    return list3d_serial;
}

/* VBlank for the 3D: the list a SwapBuffers closed is shown from now on, and
 * its textures are named (textures.h). Returns whether there was one. */
static int show_list(void)
{
    const int swapped = khdays_g3d_vblank();
    if (swapped) {
        /* the scene controller's current id (runtime.c): a new scene is a cut */
        const u32 scene = *(const volatile u32 *)KHDAYS_W(0x0204bdb0);
        khdays_textures_shown(khdays_g3d_shown());
        ++list3d_serial;
        list3d_time = khdays_events_vblank_time();
        list3d_cut = scene != list3d_scene;
        list3d_scene = scene;
    }
    return swapped;
}

/* The package to fill: the host's, or `local` without a window. */
static KhdaysFramePackage *package(void)
{
    KhdaysFramePackage *p = khdays_host_frame_begin();
    if (p == NULL) {
        if (local == NULL) {
            local = (KhdaysFramePackage *)calloc(1, sizeof(*local));
            if (local == NULL) {
                fprintf(stderr, "display: no memory for the frame package\n");
                abort();
            }
        }
        p = local;
    }
    return p;
}

/* Copies one area's pages, as the 3D engine reads them, into `seen`, counting
 * each page whose bytes changed; then copies into `out` the pages it holds an
 * older count of. */
static void track_pages(int area, uint8_t *seen, uint32_t *version, int pages, uint8_t *out, uint32_t *out_version)
{
    static const uint8_t zero[KHDAYS_FRAME_VRAM_PAGE] = {0};
    const KhdaysVramPages *vram = khdays_vram_pages();
    for (int n = 0; n < pages; ++n) {
        const uint8_t *now = vram->page[area][n] ? vram->page[area][n] : zero;
        uint8_t *mine = seen + (size_t)n * KHDAYS_FRAME_VRAM_PAGE;
        if (memcmp(mine, now, KHDAYS_FRAME_VRAM_PAGE) != 0) {
            memcpy(mine, now, KHDAYS_FRAME_VRAM_PAGE);
            ++version[n];
        }
        if (out_version[n] != version[n]) {
            memcpy(out + (size_t)n * KHDAYS_FRAME_VRAM_PAGE, mine, KHDAYS_FRAME_VRAM_PAGE);
            out_version[n] = version[n];
        }
    }
}

/* The same as track_pages, for `pages` pages of memory at `from` (NULL: none
 * mapped, read as zeros). */
static void track_memory(const uint8_t *from, uint8_t *seen, uint32_t *version, int pages, uint8_t *out,
                         uint32_t *out_version)
{
    static const uint8_t zero[KHDAYS_FRAME_VRAM_PAGE] = {0};
    for (int n = 0; n < pages; ++n) {
        const uint8_t *now = from != NULL ? from + (size_t)n * KHDAYS_FRAME_VRAM_PAGE : zero;
        uint8_t *mine = seen + (size_t)n * KHDAYS_FRAME_VRAM_PAGE;
        if (memcmp(mine, now, KHDAYS_FRAME_VRAM_PAGE) != 0) {
            memcpy(mine, now, KHDAYS_FRAME_VRAM_PAGE);
            ++version[n];
        }
        if (out_version[n] != version[n]) {
            memcpy(out + (size_t)n * KHDAYS_FRAME_VRAM_PAGE, mine, KHDAYS_FRAME_VRAM_PAGE);
            out_version[n] = version[n];
        }
    }
}

/* Puts the port's own 2D into the package: both screens' descriptions and the
 * memory they read. */
static void capture_2d(KhdaysFramePackage *p, const KhdaysGpuInput *in)
{
    static const struct {
        int area, first, pages;
    } areas[] = {
        {KHDAYS_AREA_ABG, KHDAYS_2D_ABG, 32},      {KHDAYS_AREA_AOBJ, KHDAYS_2D_AOBJ, 16},
        {KHDAYS_AREA_BBG, KHDAYS_2D_BBG, 8},       {KHDAYS_AREA_BOBJ, KHDAYS_2D_BOBJ, 8},
        {KHDAYS_AREA_ABGEXT, KHDAYS_2D_ABGEXT, 2}, {KHDAYS_AREA_AOBJEXT, KHDAYS_2D_AOBJEXT, 1},
        {KHDAYS_AREA_BBGEXT, KHDAYS_2D_BBGEXT, 2}, {KHDAYS_AREA_BOBJEXT, KHDAYS_2D_BOBJEXT, 1},
    };
    for (int e = 0; e < 2; ++e) {
        khdays_gfx_describe(in->io, in->palette, in->oam, e, &latch2d[e], p->desc2d[e]);
        khdays_video_subtitles_out(in->io, e, p->desc2d[e]);
    }
    if (p->wants & KHDAYS_WANT_3D) {
        /* a 3D list wider than the DS's view: engine A covers its columns, the HUD goes to the sides */
        const int columns = (int)lroundf(p->list3d.widen * KHDAYS_SCREEN_W);
        if (columns > KHDAYS_SCREEN_W && columns <= KHDAYS_GPU2D_MAX_COLUMNS) {
            KhdaysGfxMemory memory;
            memset(&memory, 0, sizeof(memory));
            memory.vram = in->vram;
            memory.palette = in->palette;
            for (int bank = 0; bank < 4; ++bank) {
                memory.bank[bank] = in->bank[bank];
            }
            memory.layer3d = in->layer3d;
            khdays_gfx_describe_wide(p->desc2d[0], &memory, columns & ~1);
        }
    }
    memcpy(p->palette2d, in->palette, sizeof(p->palette2d));
    for (size_t i = 0; i < sizeof(areas) / sizeof(areas[0]); ++i) {
        const size_t at = (size_t)areas[i].first * KHDAYS_FRAME_VRAM_PAGE;
        track_pages(areas[i].area, mem2d_seen + at, mem2d_version + areas[i].first, areas[i].pages, p->mem2d + at,
                    p->mem2d_version + areas[i].first);
    }
    {
        const size_t at = (size_t)KHDAYS_2D_VRAM * KHDAYS_FRAME_VRAM_PAGE;
        track_memory(in->bank[p->desc2d[0][KD_VRAM] & 3], mem2d_seen + at, mem2d_version + KHDAYS_2D_VRAM, 8,
                     p->mem2d + at, p->mem2d_version + KHDAYS_2D_VRAM);
    }
    khdays_tiles2d_map(khdays_vram_pages(), mem2d_version, p->hd2d, p->hd2d_version); /* HD 2D from mods */
    p->wants |= KHDAYS_WANT_2D;
    if (khdays_video_kept()) {
        p->wants |= KHDAYS_WANT_MOVIE; /* a movie's picture is the port's own */
    }
}

/* What the host reads of a frame besides its pictures (gpu2d.h
 * KhdaysGpu2dInfo), taken from the registers as the 2D engines would give it. */
static void fill_info(int engine, KhdaysGpu2dInfo *info)
{
    const u32 base = engine ? 0x1000 : 0;
    const u32 dispcnt = IO32(base);
    const u32 display = (dispcnt >> 16) & (engine ? 1u : 3u);
    const u32 bldalpha = IO16(base + 0x52), evy = IO16(base + 0x54) & 31;
    info->eva = (uint8_t)((bldalpha & 31) > 16 ? 16 : bldalpha & 31);
    info->evb = (uint8_t)(((bldalpha >> 8) & 31) > 16 ? 16 : (bldalpha >> 8) & 31);
    info->evy = (uint8_t)(evy > 16 ? 16 : evy);
    info->is3d = (uint8_t)(engine == 0 && (dispcnt & 0x08) && (dispcnt & 0x100) && display == 1 && !(dispcnt & 0x80));
    info->master_bright = IO16(base + 0x6c);
    info->bg0_hofs = (uint16_t)(IO16(base + 0x10) & 0x1ff);
    info->backdrop = (uint16_t)(*(const u16 *)KHDAYS_W(0x05000000 + (engine ? 0x400 : 0)) & 0x7fff);
    info->bldcnt = IO16(base + 0x50);
}

/* KHDAYS_GFX=check: the host compares the port's 2D with the 2D engines',
 * which then draw, with the software's 3D. */
static int host_checks(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("KHDAYS_GFX");
        on = v != NULL && strcmp(v, "check") == 0;
    }
    return on;
}

/* Whether the 2D engines (port/gpu, the reference) draw this frame. When
 * the host draws the port's own 2D, they draw only for what compares with
 * them or takes their pictures: the checks, the recorded packages, the 1x
 * shots, KHDAYS_TRACE_2D. */
static int reference_draws(u32 frame, int want)
{
    return !(want & KHDAYS_WANT_2D) || host_checks() || khdays_gfx_check_on() || check_composition ||
           record_dir != NULL || trace_2d != NULL || shot_at(frame);
}

/* Right after the 3D engine drew: puts what it drew, and what it drew it
 * from, into the package of the frame that shows it (the next one). */
static void capture_3d(KhdaysFramePackage *p)
{
    memcpy(p->layer3d, layer3d, sizeof(layer3d));
    memcpy(p->io3d, khdays_io_host + 0x300, sizeof(p->io3d));
    p->disp3dcnt = IO16(0x060);
    if (!khdays_g3d_list_copy(&p->list3d, khdays_g3d_shown())) {
        static int said;
        if (!said) {
            fprintf(stderr, "display: no memory for the 3D list in the frame package (drawn empty)\n");
            said = 1;
        }
    }
    khdays_textures_mark(&p->list3d); /* the textures drawn instead (HD graphics) */
    khdays_reticles_commands(&p->list3d, p->sequence3d_images);
    khdays_heart_commands(&p->list3d, p->sequence3d_images + KHDAYS_RETICLE_COUNT);
    khdays_prize_commands(&p->list3d, p->sequence3d_images + KHDAYS_RETICLE_COUNT + KHDAYS_HEART_PROGRAMS);
    p->list3d_serial = list3d_serial;
    p->list3d_time = list3d_time;
    p->list3d_cut = (uint8_t)list3d_cut;
    track_pages(KHDAYS_AREA_TEX, tex_seen, tex_version, TEX_PAGES, p->tex, p->tex_version);
    track_pages(KHDAYS_AREA_TEXPAL, texpal_seen, texpal_version, TEXPAL_PAGES, p->texpal, p->texpal_version);
    p->wants = KHDAYS_WANT_3D;
}

/* KHDAYS_CHECK_COMPOSITION: composes engine A again from its records and the
 * 3D, and compares. */
static void check_records(const KhdaysFramePackage *p)
{
    static u32 frames, bad_frames;
    int bad, first;
    if (!p->info[0].is3d || !(p->wants & KHDAYS_WANT_COMPOSITION)) {
        return;
    }
    bad = khdays_frame_check_composition(p, layer3d, &first);
    ++frames;
    if (bad) {
        ++bad_frames;
        fprintf(stderr,
                "display: frame %u: %d pixels of engine A differ when composed again (first at %d,%d: "
                "%06x, record role %u effect %u)\n",
                p->frame, bad, first % KHDAYS_SCREEN_W, first / KHDAYS_SCREEN_W, p->screens[0][first],
                p->composition[first].role, p->composition[first].effect);
    }
    if (frames % 300 == 0) {
        fprintf(stderr, "display: composition check: %u frames with the 3D, %u differ\n", frames, bad_frames);
    }
}

/* --- The port's elements --------------------------------------------------------------- */

static uint32_t ui_game[KHDAYS_FRAME_PIXELS], ui_port[KHDAYS_FRAME_PIXELS];
static uint32_t ui_desc[KHDAYS_DESC_WORDS];
static const char **ui_reported; /* the elements a difference was already reported for */
static int ui_reported_count, ui_reported_room;
static u32 ui_frames_checked[2], ui_frames_differing[2];

static void ui_memory(KhdaysGfxMemory *m, const KhdaysGpuInput *in, int engine)
{
    memset(m, 0, sizeof(*m));
    m->vram = in->vram;
    m->palette = in->palette;
    for (int bank = 0; bank < 4; ++bank) {
        m->bank[bank] = in->bank[bank];
    }
    m->layer3d = engine == 0 ? in->layer3d : NULL;
    m->element_texels = khdays_ui_texels;
}

static int ui_said(const char *id)
{
    for (int i = 0; i < ui_reported_count; ++i) {
        if (strcmp(ui_reported[i], id) == 0) {
            return 1;
        }
    }
    if (ui_reported_count == ui_reported_room) {
        const int room = ui_reported_room ? ui_reported_room * 2 : 16;
        const char **grown = (const char **)realloc((void *)ui_reported, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return 1;
        }
        ui_reported = grown;
        ui_reported_room = room;
    }
    ui_reported[ui_reported_count++] = id;
    return 0;
}

/* Saves a 256x192 picture as a 24-bit BMP (the check's two pictures). */
static void ui_write_bmp(const char *path, const uint32_t *pixels)
{
    FILE *f;
    if (fopen_s(&f, path, "wb") != 0) {
        return;
    }
    fputc('B', f);
    fputc('M', f);
    put32(f, 54 + 256 * 192 * 3);
    put32(f, 0);
    put32(f, 54);
    put32(f, 40);
    put32(f, 256);
    put32(f, 192);
    put16(f, 1);
    put16(f, 24);
    put32(f, 0);
    put32(f, 256 * 192 * 3);
    put32(f, 2835);
    put32(f, 2835);
    put32(f, 0);
    put32(f, 0);
    for (int y = 191; y >= 0; --y) {
        for (int x = 0; x < 256; ++x) {
            const uint32_t c = pixels[y * 256 + x];
            fputc((int)(c & 0xff), f);
            fputc((int)((c >> 8) & 0xff), f);
            fputc((int)((c >> 16) & 0xff), f);
        }
    }
    fclose(f);
}

static void ui_report(void)
{
    for (int e = 0; e < 2; ++e) {
        if (ui_frames_checked[e] != 0) {
            fprintf(stderr, "ui check: engine %c: %u frames checked, %u with a difference\n", e ? 'B' : 'A',
                    ui_frames_checked[e], ui_frames_differing[e]);
            khdays_log("ui check: engine %c: %u frames checked, %u with a difference", e ? 'B' : 'A',
                       ui_frames_checked[e], ui_frames_differing[e]);
        }
    }
}

/* Composes screen `e` twice on the CPU from this frame's description and
 * compares: the game's picture (the checked parts drawn by the game) and the
 * port's (the checked parts drawn by their elements instead). The other parts
 * that are on show in both. */
static void ui_check(const KhdaysFramePackage *p, const KhdaysGpuInput *in, int e, u32 frame)
{
    KhdaysGfxMemory m;
    uint32_t on, all;
    const uint32_t *on_records = khdays_ui_records(e, 0, &on);
    const uint32_t *all_records = khdays_ui_records(e, 1, &all);
    const uint8_t *left_bg = khdays_ui_left_out_cell_bits(e, 1), *left_obj = khdays_ui_left_out_obj_bits(e, 1);
    int first = -1, x0 = 0, y0 = 0, x1 = 256, y1 = 192;
    if (all == on) {
        return; /* no checked part on this screen */
    }
    /* only every 60th frame, where the checked elements are: composing twice every frame would change
     * the walks' timing */
    if (frame % 60 != 0 || !khdays_ui_checked_bounds(e, &x0, &y0, &x1, &y1)) {
        return;
    }
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > 256 ? 256 : x1, y1 = y1 > 192 ? 192 : y1;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    ui_memory(&m, in, e);
    memcpy(ui_desc, p->desc2d[e], sizeof(ui_desc));
    ui_desc[KD_ELEMENTS] = on;
    m.elements = on_records;
    m.left_out_cells = khdays_ui_left_out_cell_bits(e, 0);
    m.left_out_obj = khdays_ui_left_out_obj_bits(e, 0);
    khdays_gfx_compose_cpu_rect(ui_desc, &m, ui_game, x0, y0, x1, y1);
    ui_desc[KD_ELEMENTS] = all;
    m.elements = all_records;
    m.left_out_cells = left_bg;
    m.left_out_obj = left_obj;
    khdays_gfx_compose_cpu_rect(ui_desc, &m, ui_port, x0, y0, x1, y1);
    ++ui_frames_checked[e];
    if (ui_frames_checked[e] == 1 || ui_frames_checked[e] == 10 || ui_frames_checked[e] % 60 == 0) {
        ui_report();
    }
    for (int y = y0; y < y1 && first < 0; ++y) {
        for (int x = x0; x < x1; ++x) {
            const int i = y * KHDAYS_FRAME_W + x;
            if ((ui_game[i] & 0xffffff) != (ui_port[i] & 0xffffff)) {
                first = i;
                break;
            }
        }
    }
    if (first >= 0) {
        const int x = first % KHDAYS_FRAME_W, y = first / KHDAYS_FRAME_W;
        const char *id = khdays_ui_element_at(e, 1, (float)x + 0.5f, (float)y + 0.5f);
        ++ui_frames_differing[e];
        if (!ui_said(id != NULL ? id : "(no element)")) {
            if (shot_dir != NULL) {
                /* both pictures whole, for looking at (KHDAYS_SHOT_DIR) */
                char path[MAX_PATH];
                KhdaysGfxMemory whole;
                ui_memory(&whole, in, e);
                memcpy(ui_desc, p->desc2d[e], sizeof(ui_desc));
                ui_desc[KD_ELEMENTS] = on;
                whole.elements = on_records;
                whole.left_out_cells = khdays_ui_left_out_cell_bits(e, 0);
                whole.left_out_obj = khdays_ui_left_out_obj_bits(e, 0);
                khdays_gfx_compose_cpu(ui_desc, &whole, ui_game);
                ui_desc[KD_ELEMENTS] = all;
                whole.elements = all_records;
                whole.left_out_cells = left_bg;
                whole.left_out_obj = left_obj;
                khdays_gfx_compose_cpu(ui_desc, &whole, ui_port);
                snprintf(path, sizeof(path), "%s/uicheck_%05u_%c_game.bmp", shot_dir, frame, e ? 'b' : 'a');
                ui_write_bmp(path, ui_game);
                snprintf(path, sizeof(path), "%s/uicheck_%05u_%c_port.bmp", shot_dir, frame, e ? 'b' : 'a');
                ui_write_bmp(path, ui_port);
                for (uint32_t r = 0; r < all; ++r) {
                    const uint32_t *w = all_records + r * KE_WORDS;
                    float fx, fy, fw, fh;
                    memcpy(&fx, &w[KE_X], 4), memcpy(&fy, &w[KE_Y], 4);
                    memcpy(&fw, &w[KE_W], 4), memcpy(&fh, &w[KE_H], 4);
                    fprintf(stderr, "ui check:   element %u: slot %u, %.0f,%.0f %.0fx%.0f, image %u (%ux%u)\n", r,
                            w[KE_SLOT], fx, fy, fw, fh, w[KE_IMAGE], w[KE_IW], w[KE_IH]);
                }
            }
            fprintf(stderr,
                    "ui check: frame %u, engine %c, %s: pixel %d,%d is %06x drawn by the game, %06x by the "
                    "port\n",
                    frame, e ? 'B' : 'A', id != NULL ? id : "no element there", x, y, ui_game[first] & 0xffffff,
                    ui_port[first] & 0xffffff);
            khdays_log("ui check: frame %u, engine %c, %s: pixel %d,%d is %06x drawn by the game, %06x by the port",
                       frame, e ? 'B' : 'A', id != NULL ? id : "no element there", x, y, ui_game[first] & 0xffffff,
                       ui_port[first] & 0xffffff);
        }
    }
}

/* Puts this frame's elements into the package. Where the reference drew the
 * screens (the game's picture), the port's composition of them replaces it;
 * under check, the two are compared. */
static void ui_frame(KhdaysFramePackage *p, const KhdaysGpuInput *in, int reference, u32 frame)
{
    /* widescreen moves the elements to the sides like the HUD's pieces; on one screen they stay at their
     * DS places and the composition places them */
    khdays_ui_kh2_hp_reference(reference);
    khdays_ui_kh2_portrait_reference(reference);
    khdays_ui_kh2_party_reference(reference);
    khdays_ui_kh2_labels_reference(reference);
    p->hud_options = khdays_ui_hud_options();
    if (!khdays_ui_information_keep_hud())
        p->hud_options &= ~KHDAYS_HUD_KEEP_INFORMATION;
    khdays_ui_kh2_enemy_reference(reference);
    khdays_ui_collect((p->wants & KHDAYS_WANT_SITUATION) ? 0 : (int)p->desc2d[0][KD_WIDE], p->upper);
    khdays_ui_kh2_hp_commands(&p->sequence_image);
    khdays_ui_kh2_portrait_commands(p->portrait_images);
    khdays_ui_kh2_party_commands(p->party_images);
    khdays_ui_kh2_labels_commands(p->label_images);
    khdays_ui_kh2_enemy_commands(&p->enemy_image);
    khdays_ui_information_commands(p->information_images);
    khdays_ui_obtained_commands(&p->obtained_image);
    khdays_ui_notification_commands(p->notification_images);
    khdays_ui_mission_panel_commands(&p->mission_panel_image);
    khdays_minimap_commands(p->minimap_images);
    khdays_minimap_radar_commands(&p->radar_image);
    p->mission_panel_state = khdays_ui_mission_panel_state();
    p->mission_panel_backdrop = khdays_ui_mission_panel_backdrop();
    p->information_hold_frames = khdays_ui_information_hold_frames();
    p->information_state = khdays_ui_information_state();
    for (int e = 0; e < 2; ++e) {
        KhdaysFrameElements *fe = &p->elements[e];
        uint32_t n;
        const uint32_t *r = khdays_ui_records(e, 0, &n);
        if (n > fe->capacity) {
            uint32_t *grown = (uint32_t *)realloc(fe->records, (size_t)n * KE_WORDS * sizeof(uint32_t));
            if (grown == NULL) {
                n = 0;
            } else {
                fe->records = grown;
                fe->capacity = n;
            }
        }
        if (n > 0) {
            memcpy(fe->records, r, (size_t)n * KE_WORDS * sizeof(uint32_t));
        }
        fe->count = n;
        p->desc2d[e][KD_ELEMENTS] = n;
        {
            const uint8_t *bg = khdays_ui_left_out_cell_bits(e, 0), *obj = khdays_ui_left_out_obj_bits(e, 0);
            p->desc2d[e][KD_LEFT_OUT] = bg != NULL || obj != NULL;
            if (bg != NULL)
                memcpy(p->left_out_cells[e], bg, sizeof(p->left_out_cells[e]));
            else
                memset(p->left_out_cells[e], 0, sizeof(p->left_out_cells[e]));
            if (obj != NULL)
                memcpy(p->left_out_obj[e], obj, sizeof(p->left_out_obj[e]));
            else
                memset(p->left_out_obj[e], 0, sizeof(p->left_out_obj[e]));
        }
        if (reference && (n > 0 || p->desc2d[e][KD_LEFT_OUT])) {
            KhdaysGfxMemory m;
            ui_memory(&m, in, e);
            m.elements = fe->records;
            m.left_out_cells = p->desc2d[e][KD_LEFT_OUT] ? p->left_out_cells[e] : NULL;
            m.left_out_obj = p->desc2d[e][KD_LEFT_OUT] ? p->left_out_obj[e] : NULL;
            khdays_gfx_compose_cpu(p->desc2d[e], &m, p->screens[e]);
        }
        if (khdays_ui_any_checked()) {
            ui_check(p, in, e, frame);
        }
    }
}

/* KHDAYS_RECORD_FRAMES: writes the package, as it is in memory, after a header. */
static void record_package(const KhdaysFramePackage *p)
{
    char path[MAX_PATH];
    FILE *f;
    /* version 14: a vertex has an alpha (ten words) */
    const u32 header[4] = {0x474b5046u /* "FPKG" */, 14, (u32)KHDAYS_FRAME_RECORDED, p->frame};
    snprintf(path, sizeof(path), "%s/frame_%05u.pkg", record_dir, p->frame);
    if (fopen_s(&f, path, "wb") != 0) {
        fprintf(stderr, "display: cannot write %s\n", path);
        return;
    }
    fwrite(header, sizeof(header), 1, f);
    fwrite(p, KHDAYS_FRAME_RECORDED, 1, f); /* not the situation's inputs */
    /* the 3D list's polygons and vertices, which the package points at */
    fwrite(p->list3d.polys, sizeof(p->list3d.polys[0]), (size_t)p->list3d.npolys, f);
    fwrite(p->list3d.verts, sizeof(p->list3d.verts[0]), (size_t)p->list3d.nverts, f);
    /* and the same polygons before clipping (version 10) */
    fwrite(p->list3d.pre, sizeof(p->list3d.pre[0]), (size_t)p->list3d.npre, f);
    fwrite(p->list3d.pre_info, sizeof(p->list3d.pre_info[0]), (size_t)p->list3d.npre, f);
    fwrite(p->list3d.pre_verts, sizeof(p->list3d.pre_verts[0]), (size_t)p->list3d.npre_verts, f);
    /* the elements' records (version 12), then each image they draw once:
     * its KE_IMAGE, width, height and texels */
    {
        uint32_t *ids =
            (uint32_t *)malloc(((size_t)p->elements[0].count + p->elements[1].count + 1) * sizeof(uint32_t));
        uint32_t nids = 0;
        for (int e = 0; e < 2; ++e) {
            fwrite(p->elements[e].records, sizeof(uint32_t) * KE_WORDS, p->elements[e].count, f);
        }
        for (int e = 0; e < 2 && ids != NULL; ++e) {
            for (uint32_t i = 0; i < p->elements[e].count; ++i) {
                const uint32_t id = p->elements[e].records[i * KE_WORDS + KE_IMAGE];
                uint32_t k = 0;
                while (k < nids && ids[k] != id)
                    ++k;
                if (k == nids)
                    ids[nids++] = id;
            }
        }
        fwrite(&nids, sizeof(nids), 1, f);
        for (uint32_t k = 0; k < nids; ++k) {
            const KhdaysTextureReplacement *r = khdays_textures_replacement((int)ids[k]);
            const uint32_t head[3] = {ids[k], r != NULL ? (uint32_t)r->width : 0, r != NULL ? (uint32_t)r->height : 0};
            fwrite(head, sizeof(head), 1, f);
            if (r != NULL) {
                fwrite(r->rgba, sizeof(uint32_t), (size_t)r->width * (size_t)r->height, f);
            }
        }
        free(ids);
    }
    fclose(f);
}

/* The traces and the session log's lines that look at every VBlank, drawn or
 * not. */
static void trace_vblank(u32 frame)
{
    khdays_ui_hud_vblank(frame); /* KHDAYS_TRACE_HUD */
    if (trace_situation) {
        /* KHDAYS_TRACE_SITUATION: every VBlank's inputs, drawn or not */
        static KhdaysSituationInputs traced_inputs;
        khdays_situation_take(&traced_inputs, frame);
    }
    if (trace_3d_frame != 0 && frame + 8 == trace_3d_frame) {
        khdays_g3d_set_polygon_hook(polygon_caller);
    } else if (trace_3d_frame != 0 && frame == trace_3d_frame) {
        khdays_g3d_set_polygon_hook(NULL);
        for (int i = 0; i < chain_count; ++i) {
            fprintf(stderr, "display: %5d polygons from %s\n", chains[i].polygons, chains[i].chain);
        }
    }
}

/* Whether only the window looks at this frame: no recording, check, shot or
 * trace wants it. Only then may a frame the window already shows be left
 * undrawn. */
static int only_shown(u32 frame)
{
    return !headless && record_dir == NULL && !check_composition && !khdays_gfx_check_on() && trace_2d == NULL &&
           !shot_at(frame) && !khdays_ui_any_checked();
}

/* Whether both screens are each one colour now, the same ones as in the last
 * frame drawn (a fade or a load): then the 2D need not be drawn again (the 3D
 * goes on: the first frame after the fade needs it). Remembers this frame's
 * colours for the next. */
static int same_colours_again(void)
{
    uint32_t a = 0, b = 0;
    const int one = one_colour(0, &a) && one_colour(1, &b);
    const int same = one && shown_one_colour && a == shown_colour[0] && b == shown_colour[1];
    if (same) {
        ++last_drawn.alike;
    }
    shown_one_colour = one;
    shown_colour[0] = one ? a : 0;
    shown_colour[1] = one ? b : 0;
    return same;
}

/* What the 2D engines read: the registers, palettes, OAM, VRAM and the 3D
 * engine's picture. */
static void gpu_input(KhdaysGpuInput *in)
{
    memset(in, 0, sizeof(*in));
    in->io = khdays_io_host;
    in->palette = (const u8 *)KHDAYS_W(0x05000000);
    in->oam = (const u8 *)KHDAYS_W(0x07000000);
    in->vram = khdays_vram_pages();
    for (int bank = 0; bank < 4; ++bank) {
        in->bank[bank] = khdays_vram_bank(bank);
    }
    in->layer3d = layer3d;
}

/* A frame with the same colours as the one shown: nothing is drawn, only the
 * windows' flags move on. */
static void skip_2d_frame(KhdaysGpuInput *in)
{
    khdays_gpu2d_skip_frame(in, &states[0], 0);
    khdays_gpu2d_skip_frame(in, &states[1], 1);
    khdays_gfx_describe_skip(in->io, 0, &latch2d[0]);
    khdays_gfx_describe_skip(in->io, 1, &latch2d[1]);
    if (khdays_gfx_check_on()) {
        khdays_gfx_check_skip(in, 0);
        khdays_gfx_check_skip(in, 1);
    }
}

/* The captures taken into each VRAM bank (display_capture), the count after the last that did not read
 * the bank before (a blend's first), and a sum of what the last one wrote: the host shows its own copy of
 * a bank only while the bank still holds that capture. */
static u32 bank_captures[4], bank_plain[4], bank_sums[4], bank_sum_at[4], bank_sum_bytes[4];

static u32 bank_sum(int bank)
{
    const u8 *bytes = khdays_vram_bank(bank);
    u32 sum = 2166136261u; /* FNV-1a over the words */
    for (u32 n = 0; n < bank_sum_bytes[bank]; n += 4) {
        u32 word;
        memcpy(&word, bytes + ((bank_sum_at[bank] + n) & 0x1ffffu), 4);
        sum = (sum ^ word) * 16777619u;
    }
    return sum;
}

/* The capture as the host takes it again (frame_package.h): the one armed now, the count of each bank,
 * and whether the bank of engine A's VRAM block (the one it shows, and source B) holds its last. */
static void package_capture(KhdaysFramePackage *p)
{
    const u32 cnt = IO32(0x64), dispcnt = IO32(0x000);
    const int bank = (int)((dispcnt >> 18) & 3);
    const int used = ((dispcnt >> 16) & 3) == 2 || (cnt & 0x80000000u); /* a bank shown, or a capture */
    p->dispcapcnt = (cnt & 0x80000000u) ? cnt : 0u;
    memcpy(p->bank_captures, bank_captures, sizeof(bank_captures));
    memcpy(p->bank_plain, bank_plain, sizeof(bank_plain));
    p->bank_intact = used && bank_captures[bank] != 0 && bank_sum(bank) == bank_sums[bank] ? 1u << bank : 0u;
}

/* Starts filling package `p` for this frame: its number and time, which
 * engine is on the upper screen, whether the capture is on, and the
 * one-screen picture's inputs. */
static void begin_package(KhdaysFramePackage *p, u32 frame, int want, int capture)
{
    if (p != prepared) {
        p->wants = 0; /* no 3D captured for it */
    }
    p->frame = frame;
    p->sequence_image.words[KSI_COUNT] = 0;
    memset(p->information_images, 0, sizeof(p->information_images));
    memset(&p->obtained_image, 0, sizeof(p->obtained_image));
    memset(p->notification_images, 0, sizeof(p->notification_images));
    memset(&p->mission_panel_image, 0, sizeof(p->mission_panel_image));
    memset(p->minimap_images, 0, sizeof(p->minimap_images));
    memset(&p->radar_image, 0, sizeof(p->radar_image));
    p->mission_panel_state = 0;
    p->mission_panel_backdrop = -1;
    p->information_hold_frames = 0;
    p->information_state = 0;
    p->hud_options = 0;
    p->enemy_image.words[KSI_COUNT] = p->enemy_image.words[KSI_CLEAR] = p->enemy_image.words[KSI_ANCHORED] = 0;
    for (int i = 0; i < 4; ++i) {
        p->portrait_images[i].words[KSI_COUNT] = p->portrait_images[i].words[KSI_CLEAR] = 0;
    }
    for (int i = 0; i < 3; ++i)
        p->party_images[i].words[KSI_COUNT] = 0;
    for (int i = 0; i < 5; ++i)
        p->label_images[i].words[KSI_COUNT] = p->label_images[i].words[KSI_CLEAR] = 0;
    p->time = khdays_events_vblank_time();
    p->alike = last_drawn.alike;
    last_drawn.alike = 0;
    /* POWCNT1 bit 15: 1 = engine A on the upper screen */
    p->upper = (IO16(0x304) & 0x8000) ? 0 : 1;
    /* the game uses the display capture: for the 3D on both screens the host shows engine A's
     * kept picture, at its own resolution (gfx_render.c) */
    p->capture = (uint8_t)capture;
    package_capture(p);
    p->field_wipe = (uint32_t)khdays_field_wipe_running(frame);
    if (want & KHDAYS_WANT_SITUATION) {
        /* the one-screen picture's inputs: a few of the game's bytes */
        khdays_situation_take(&p->situation, frame);
        p->situation.engine_swaps = engine_swaps;
        p->wants |= KHDAYS_WANT_SITUATION;
    } else {
        p->situation.taken = 0;
    }
}

/* The 2D engines (port/gpu, the reference) draw both screens into `p`, with
 * engine A's composition records when the host wants them. `ta` gets the time
 * engine A was done. */
static void draw_reference_2d(KhdaysFramePackage *p, KhdaysGpuInput *in, u32 frame, int want, LARGE_INTEGER *ta)
{
    {
        /* a 3D list wider than the DS's view: the 2D over its columns too */
        KhdaysGpu2dWide wide;
        const int columns = (int)lroundf(p->list3d.widen * KHDAYS_SCREEN_W);
        const int use_wide = (want & KHDAYS_WANT_COMPOSITION) && (p->wants & KHDAYS_WANT_3D) &&
                             columns > KHDAYS_SCREEN_W && columns <= KHDAYS_GPU2D_MAX_COLUMNS;
        memset(&wide, 0, sizeof(wide));
        wide.columns = columns;
        wide.records = p->wide;
        wide.check = check_composition;
        khdays_gpu2d_frame_ex(in, &states[0], 0, p->screens[0], NULL,
                              (want & KHDAYS_WANT_COMPOSITION) ? p->composition : NULL, use_wide ? &wide : NULL,
                              &p->info[0]);
        p->wide_columns = use_wide && p->info[0].is3d ? columns : 0;
        if (wide.mismatches != 0) {
            fprintf(stderr, "display: frame %u: %d widescreen records differ from their shortcuts\n", frame,
                    wide.mismatches);
        }
        p->wide_pieces = wide.pieces;
        memcpy(p->wide_piece, wide.piece, sizeof(p->wide_piece));
    }
    QueryPerformanceCounter(ta);
    khdays_gpu2d_frame_ex(in, &states[1], 1, p->screens[1], NULL, NULL, NULL, &p->info[1]);
    if (want & KHDAYS_WANT_2D) {
        capture_2d(p, in);
    } else {
        khdays_gfx_describe_skip(in->io, 0, &latch2d[0]);
        khdays_gfx_describe_skip(in->io, 1, &latch2d[1]);
    }
    if (khdays_gfx_check_at(frame)) {
        khdays_gfx_check_screen(in, 0, frame, p->screens[0]);
        khdays_gfx_check_screen(in, 1, frame, p->screens[1]);
    } else if (khdays_gfx_check_on()) {
        khdays_gfx_check_skip(in, 0);
        khdays_gfx_check_skip(in, 1);
    }
}

/* KHDAYS_TRACE_2D: what both 2D engines show at this frame, and where the
 * HUD's pieces go in widescreen. */
static void trace_2d_frame(const KhdaysFramePackage *p, KhdaysGpuInput *in, u32 frame)
{
    fprintf(stderr, "display: frame %u, the upper screen is engine %c\n", frame, p->upper ? 'B' : 'A');
    khdays_gpu2d_describe(in, 0, stderr);
    for (int n = 0; n < p->wide_pieces && p->wide_columns > 0; ++n) {
        static const char *const sides[3] = {"to the left edge", "stays", "to the right edge"};
        const KhdaysGpu2dPiece *piece = &p->wide_piece[n];
        fprintf(stderr, "  HUD piece %d: DS pixels %d-%d x %d-%d, %s\n", n, piece->x0 * 8, piece->x1 * 8 + 7,
                piece->y0 * 8, piece->y1 * 8 + 7, sides[piece->side + 1]);
    }
    khdays_gpu2d_describe(in, 1, stderr);
}

/* Draws this frame into a package and hands it to the host (or shows it
 * directly without a window). `t0`, `ta` and `t1` get the times the drawing
 * started, engine A was done, and the 2D was done. */
static void draw_frame(u32 frame, int want, int capture, KhdaysGpuInput *in, LARGE_INTEGER *t0, LARGE_INTEGER *ta,
                       LARGE_INTEGER *t1)
{
    KhdaysFramePackage *p = package();
    const uint32_t *upper, *lower;
    int reference;
    begin_package(p, frame, want, capture);
    QueryPerformanceCounter(t0);
    reference = reference_draws(frame, want);
    if (!reference) {
        /* the port's own 2D, which the host draws: only described here */
        khdays_gpu2d_skip_frame(in, &states[0], 0);
        khdays_gpu2d_skip_frame(in, &states[1], 1);
        fill_info(0, &p->info[0]);
        fill_info(1, &p->info[1]);
        capture_2d(p, in);
        QueryPerformanceCounter(ta);
    } else {
        draw_reference_2d(p, in, frame, want, ta);
    }
    if (khdays_ui_any()) {
        ui_frame(p, in, reference, frame);
    } else {
        p->elements[0].count = p->elements[1].count = 0;
    }
    if (want & KHDAYS_WANT_COMPOSITION) {
        p->wants |= KHDAYS_WANT_COMPOSITION;
    }
    QueryPerformanceCounter(t1);
    upper = p->screens[p->upper];
    lower = p->screens[!p->upper];
    if (check_composition) {
        check_records(p);
    }
    if (record_dir != NULL && (shot_count == 0 || shot_at(frame))) {
        record_package(p); /* with KHDAYS_SHOTS, only the frames it lists */
    }
    for (int n = 0; n < shot_count; ++n) {
        if (shots[n] == frame) {
            save_shot(frame, upper, lower);
        }
    }
    if (scene_shot_at(frame)) {
        /* the scene's key as a file's name: EV_S#4 -> EV_S_4 */
        char name[64], path[MAX_PATH];
        int n = 0;
        for (const char *k = khdays_scene_current(); *k != '\0' && n + 1 < (int)sizeof(name); ++k) {
            name[n++] = (*k == '#' || *k == '/' || *k == ':') ? '_' : *k;
        }
        name[n] = '\0';
        snprintf(path, sizeof(path), "%s/%s_%05u.bmp", scene_shot_dir, name, frame);
        write_bmp(path, upper, lower);
    }
    if (frame == mark_frame) {
        save_mark(frame, upper, lower);
        mark_frame = ~0u;
    }
    if (trace_2d != NULL && listed(trace_2d, frame)) {
        trace_2d_frame(p, in, frame);
    }
    if (p == local) {
        khdays_host_present(upper, lower);
    } else {
        khdays_host_frame_publish(p);
    }
}

/* The 3D engine renders during the next frame what it has now: in software, unless the window draws
 * the 3D itself from the list and nothing here needs the software's picture (no recording, check,
 * shot or trace). */
static void draw_3d_for_next(u32 frame, int want)
{
    if ((want & KHDAYS_WANT_3D) && record_dir == NULL && !check_composition && !host_checks() && trace_2d == NULL &&
        !shot_at(frame + 1)) {
        const int swapping = show_list();
        if (!software_3d_off) {
            memset(layer3d, 0, sizeof(layer3d));
            software_3d_off = 1;
        }
        if (swapping) {
            ++layer3d_version; /* a new list: the next VBlank shows something new */
        }
    } else {
        static uint32_t before[PIXELS];
        software_3d_off = 0;
        memcpy(before, layer3d, sizeof(layer3d));
        show_list();
        khdays_gpu3d_draw(khdays_g3d_shown(), khdays_io_host, khdays_vram_pages(), layer3d);
        layer3d_list = list3d_serial;
        if (memcmp(before, layer3d, sizeof(layer3d)) != 0) {
            ++layer3d_version; /* the next VBlank shows something new */
        }
    }
}

/* KHDAYS_PROFILE: every 300 frames drawn, the time the drawing took and the
 * pages of memory that changed. */
static void profile_frame(LARGE_INTEGER t0, LARGE_INTEGER ta, LARGE_INTEGER t1, LARGE_INTEGER t2)
{
    LARGE_INTEGER hz;
    static double ms2d, ms_a, ms3d;
    static int frames;
    static u32 pages[3], most_pages;
    u32 c[3];
    gate_pages(c);
    for (int i = 0; i < 3; ++i) {
        pages[i] += c[i];
    }
    if (c[0] > most_pages) {
        most_pages = c[0];
    }
    QueryPerformanceFrequency(&hz);
    ms2d += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
    ms_a += (double)(ta.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
    ms3d += (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / (double)hz.QuadPart;
    if (++frames == 300) {
        fprintf(stderr, "display: %.2f ms 2D (engine A %.2f, with its records), %.2f ms 3D per frame\n", ms2d / frames,
                ms_a / frames, ms3d / frames);
        fprintf(stderr,
                "display: per frame drawn, pages of 2 KB changed: VRAM %.1f (at most %u), palettes %.2f, "
                "OAM %.2f\n",
                (double)pages[0] / frames, most_pages, (double)pages[1] / frames, (double)pages[2] / frames);
        ms2d = ms_a = ms3d = 0;
        frames = 0;
        memset(pages, 0, sizeof(pages));
        most_pages = 0;
    }
}

/* --- The display capture ------------------------------------------------------------------- */

/* DISPCAPCNT (GBATEK, "DS Video Display Capture"): once armed, the DS takes engine A's next frame -- its
 * BG+3D+OBJ or its 3D alone (source A), a VRAM bank's picture (source B), or both blended by EVA and
 * EVB -- into a VRAM bank, and clears the enable. The game shows such banks (the VRAM display mode,
 * engine B's bitmaps) for its screen blends, its motion blur and the 3D on both screens. Taken here at
 * the VBlank of that frame, after it is drawn: a bank shown and written at once shows the frame before. */
static int capture_used;               /* the game has armed a capture (the host's p->capture) */
static uint16_t capture_a[PIXELS];     /* source A, BGR555 with its alpha bit */
static uint32_t capture_layer[PIXELS]; /* the frame's 3D, when layer3d does not hold it */
static u32 *capture_kinds;             /* the kinds logged: DISPCAPCNT without its enable, EVA and EVB */
static int capture_kind_count, capture_kind_room;

/* Each kind of capture once in the session log. */
static void log_capture(u32 cnt)
{
    const u32 kind = cnt & 0x7fffe0e0u;
    for (int i = 0; i < capture_kind_count; ++i) {
        if (capture_kinds[i] == kind) {
            return;
        }
    }
    if (capture_kind_count == capture_kind_room) {
        const int room = capture_kind_room ? capture_kind_room * 2 : 8;
        u32 *grown = (u32 *)realloc(capture_kinds, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        capture_kinds = grown;
        capture_kind_room = room;
    }
    capture_kinds[capture_kind_count++] = kind;
    khdays_log("display: capture DISPCAPCNT %08x, engine A on the %s screen, DISPCNT A %08x", (unsigned)cnt,
               (IO16(0x304) & 0x8000) ? "upper" : "lower", (unsigned)IO32(0x000));
}

/* Whether anything shows VRAM bank `bank` (0 = A): an engine or the 3D has it, or engine A shows it in
 * the VRAM display mode. */
static int bank_shown(int bank)
{
    const u8 cnt = IO8(khdays_vram_cnt_reg[bank]);
    const u32 dispcnt = IO32(0x000);
    return ((cnt & 0x80) && (cnt & 7) != 0) || (((dispcnt >> 16) & 3) == 2 && ((dispcnt >> 18) & 3) == (u32)bank);
}

/* GBATEK's blend: each intensity (A * alpha A * EVA + B * alpha B * EVB) / 16, the alpha set where a
 * source with its alpha set has a factor. */
static uint16_t capture_blend(uint16_t a, uint16_t b, unsigned eva, unsigned evb)
{
    const unsigned alpha_a = a >> 15, alpha_b = b >> 15;
    uint16_t out = (uint16_t)(((alpha_a && eva) || (alpha_b && evb)) ? 0x8000u : 0u);
    for (int shift = 0; shift < 15; shift += 5) {
        const unsigned c = (((a >> shift) & 31u) * alpha_a * eva + ((b >> shift) & 31u) * alpha_b * evb) / 16u;
        out |= (uint16_t)((c > 31 ? 31 : c) << shift);
    }
    return out;
}

/* Source A: engine A's frame as BGR555 with the alpha bit (the 2D always opaque, the 3D where drawn). The
 * 3D is the list this frame shows: layer3d, or drawn now when the window draws the 3D or layer3d holds an
 * older list. */
static void capture_source_a(u32 cnt, const KhdaysGpuInput *in, const KhdaysGpu2dState *window_a)
{
    const uint32_t *shown3d = layer3d;
    if (software_3d_off || layer3d_list != list3d_serial) {
        khdays_gpu3d_draw(khdays_g3d_shown(), khdays_io_host, khdays_vram_pages(), capture_layer);
        shown3d = capture_layer;
    }
    if (cnt & (1u << 24)) {
        for (int i = 0; i < PIXELS; ++i) {
            const uint32_t p = shown3d[i];
            capture_a[i] = (uint16_t)(((p >> 24) & 31 ? 0x8000u : 0u) | ((p >> 1) & 31) | ((p >> 9) & 31) << 5 |
                                      ((p >> 17) & 31) << 10);
        }
    } else {
        KhdaysGpuInput with3d = *in;
        with3d.layer3d = shown3d;
        with3d.left_out_cells = khdays_ui_captured_cell_bits(0);
        khdays_gpu2d_graphics(&with3d, window_a, capture_a);
        for (int i = 0; i < PIXELS; ++i) {
            capture_a[i] |= 0x8000u;
        }
    }
}

/* The capture armed for this frame, if any. `window_a`: engine A's windows at the frame's start. */
static void display_capture(const KhdaysGpuInput *in, const KhdaysGpu2dState *window_a)
{
    static const int sizes[4][2] = {{128, 128}, {256, 64}, {256, 128}, {256, 192}};
    const u32 cnt = IO32(0x64);
    const unsigned mode = (cnt >> 29) & 3; /* 0 source A, 1 source B, 2-3 both blended */
    const unsigned eva = (cnt & 31) > 16 ? 16 : cnt & 31, evb = ((cnt >> 8) & 31) > 16 ? 16 : (cnt >> 8) & 31;
    const int w = sizes[(cnt >> 20) & 3][0], h = sizes[(cnt >> 20) & 3][1];
    const int dest = (int)((cnt >> 16) & 3);
    const u32 write_at = ((cnt >> 18) & 3) * 0x8000u, read_at = ((cnt >> 26) & 3) * 0x8000u;
    const u8 *source_b = khdays_vram_bank((int)((IO32(0x000) >> 18) & 3)); /* engine A's VRAM block */
    const int from_memory = (cnt & (1u << 25)) != 0;
    u8 *to = khdays_vram_bank_written(dest);
    if (!(cnt & 0x80000000u)) {
        return;
    }
    IO32(0x64) = cnt & ~0x80000000u;
    memcpy(last_drawn.io_a + 0x64, khdays_io_host + 0x64, 4); /* the register alone changes no picture */
    log_capture(cnt);
    {
        /* the DS writes a capture only into a bank mapped to the LCDC (GBATEK, DISPCAPCNT's write block): the
         * mission review's last capture (ov003, armed every VBlank until its teardown) is taken after the
         * results have mapped that bank to their BG and loaded their characters there */
        const u8 bank_cnt = IO8(khdays_vram_cnt_reg[dest]);
        if (!(bank_cnt & 0x80) || (bank_cnt & 7) != 0) {
            static int said;
            if (!said) {
                said = 1;
                khdays_log(
                    "display: a capture into bank %c, its VRAMCNT %02x not the LCDC's: not written, as on the DS",
                    'A' + dest, bank_cnt);
            }
            return;
        }
    }
    if (mode != 1) {
        capture_source_a(cnt, in, window_a);
    }
    if (mode != 0 && from_memory) {
        static int said;
        if (!said) {
            said = 1;
            khdays_log("display: a capture from the main memory display FIFO is not modelled (read as black)");
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u32 n = (u32)(y * w + x) * 2u; /* both banks read and written in order */
            const uint16_t a = capture_a[y * KHDAYS_SCREEN_W + x];
            const uint16_t b = from_memory ? 0 : *(const uint16_t *)(source_b + ((read_at + n) & 0x1ffffu));
            *(uint16_t *)(to + ((write_at + n) & 0x1ffffu)) = mode == 0   ? a
                                                              : mode == 1 ? b
                                                                          : capture_blend(a, b, eva, evb);
        }
    }
    ++bank_captures[dest];
    if (mode == 0 || (mode != 1 && evb == 0)) {
        bank_plain[dest] = bank_captures[dest]; /* engine A's picture alone */
    }
    bank_sum_at[dest] = write_at;
    bank_sum_bytes[dest] = (u32)(w * h * 2);
    bank_sums[dest] = bank_sum(dest);
    if (!bank_shown(dest) && last_drawn.valid) {
        /* nothing shows the bank: the next frame shows the same as this one */
        memcpy(last_drawn.vram + khdays_vram_bank_offset[dest], to, khdays_vram_bank_size[dest]);
    }
}

void khdays_display_vblank(u32 frame)
{
    KhdaysGpuInput in;
    int want;
    LARGE_INTEGER t0, ta, t1, t2;
    int skip_2d = 0;
    const KhdaysGpu2dState window_a = states[0]; /* engine A's windows at the frame's start */
    if (IO32(0x64) & 0x80000000u) {
        capture_used = 1;
    }
    {
        /* the 3D on both screens: the game swaps engine A's screen (POWCNT1 bit 15) every VBlank,
         * skipping one now and then when its frame runs late */
        const int a_upper = (IO16(0x304) & 0x8000) != 0;
        engine_swaps = khdays_situation_swaps(&swap_history, a_upper != engine_upper);
        engine_upper = a_upper;
    }
    if (InterlockedExchange(&mark_requests, 0) != 0 && mark_frame == ~0u) {
        mark_frame = frame + 2u; /* F12: two VBlanks on, so the frame before it draws its 3D */
    }
    trace_vblank(frame);
    khdays_sheet_colours_frame(); /* the colours each 2D sheet is drawn with, while the dump is on (drawn or not) */
    {
        /* a wider picture, from the next list the game swaps in; on two screens only where the 3D is
         * the game's world (situation.h) */
        float widen = wide_columns > 256 && headless ? (float)wide_columns / 256.0f : khdays_host_frame_widen();
        if (widen > 1.0f && !(khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN)) {
            static KhdaysSituationInputs now;
            khdays_situation_take(&now, frame);
            now.engine_swaps = engine_swaps;
            if (!khdays_situation_widens(&now)) {
                widen = 1.0f;
            }
        }
        khdays_g3d_set_widen(widen);
    }
    khdays_ui_hud_settings_frame();
    khdays_ui_kh2_hp_frame(frame);
    khdays_ui_information_frame(frame);
    khdays_ui_obtained_frame(frame);
    khdays_ui_mission_panel_frame(frame);
    khdays_minimap_frame(frame);
    khdays_reticles_frame(frame);
    khdays_ui_kh2_enemy_frame(frame);
    khdays_ui_kh2_party_frame(frame);
    khdays_ui_kh2_labels_frame(frame);
    khdays_ui_kh2_portrait_frame(frame);
    if (!drawn(frame)) {
        gpu_input(&in);
        display_capture(&in, &window_a);
        show_list();
        prepared = NULL;
        return;
    }
    /* The same frame again: nothing to swap in, nothing new to draw (the parts' elements follow VRAM and
     * the registers, so they are the same too, unless a part said its own state changed). */
    const int parts_changed = khdays_ui_take_changed();
    if (only_shown(frame) && !parts_changed && unchanged()) {
        gpu_input(&in);
        display_capture(&in, &window_a);
        show_list();
        return;
    }
    if (only_shown(frame)) {
        skip_2d = same_colours_again();
    }
    want = khdays_host_frame_wants();
    if (record_dir != NULL || check_composition) {
        want |= KHDAYS_WANT_3D | KHDAYS_WANT_COMPOSITION;
    }
    if (khdays_ui_any()) {
        want |= KHDAYS_WANT_2D; /* the screens' descriptions, which the elements are composed with */
    }
    gpu_input(&in);
    if (skip_2d) {
        QueryPerformanceCounter(&t0);
        skip_2d_frame(&in);
        ta = t1 = t0;
    } else {
        draw_frame(frame, want, capture_used, &in, &t0, &ta, &t1);
    }
    display_capture(&in, &window_a);
    draw_3d_for_next(frame, want);
    prepared = NULL;
    if (want & KHDAYS_WANT_3D) {
        prepared = package();
        capture_3d(prepared);
    }
    QueryPerformanceCounter(&t2);
    if (profile) {
        profile_frame(t0, ta, t1, t2);
    }
}
