/* The 3D's texture memory past the DS's (runtime/display/texture_banks.c): a file set up in a bank of its
 * own gets its texels, 4x4 texels, 4x4 indices and palettes at the places its keys name, marked loaded as
 * the game's uploads mark them, the DS's managers given back; a draw of a model in the file sends its
 * textures from the bank; the bank's textures are decoded once and shared by what they show; a file set up
 * where the file was drops the bank. */
#include "../runtime/core/window.h"
#include "../runtime/display/texture_banks.h"
#include "../runtime/mods/textures.h"
#include "../gpu/texdecode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *khdays_window;
static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("texture_banks_test:%d failed: %s\n", __LINE__, #c);                                                \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#define WINDOW_BYTES 0x02200000u

/* A reference: into the test's game memory, or (the managers' states, on the stack) one of the stubs'
 * own, numbered from 0xf0000000. */
static void *stack_hosts[16];
static int nstack_hosts;

KhdaysRef khdays_reference(const void *host)
{
    const unsigned char *at = (const unsigned char *)host;
    if (at >= khdays_window && at < khdays_window + WINDOW_BYTES) {
        return (KhdaysRef)(at - khdays_window);
    }
    stack_hosts[nstack_hosts % 16] = (void *)host;
    return 0xf0000000u + (KhdaysRef)(nstack_hosts++ % 16);
}

static unsigned char *state_at(KhdaysRef state)
{
    return (unsigned char *)stack_hosts[state - 0xf0000000u];
}

static char logged[512];
void khdays_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(logged, sizeof(logged), format, args);
    va_end(args);
}

int khdays_origin_on(void)
{
    return 0;
}

int khdays_origin_of_ram(unsigned address, const char **file, unsigned *file_offset, unsigned *run)
{
    (void)address;
    (void)file;
    (void)file_offset;
    (void)run;
    return 0;
}

int khdays_names_texture(const char *file, unsigned offset, int format, const char *pltt_file, unsigned pltt_offset,
                         char *out, int size)
{
    (void)file;
    (void)offset;
    (void)format;
    (void)pltt_file;
    (void)pltt_offset;
    (void)out;
    (void)size;
    return 0;
}

int khdays_textures_hd(void)
{
    return 0;
}

int khdays_textures_mod_replacement(const char *name, uint32_t param)
{
    (void)name;
    (void)param;
    return -1;
}

/* The replacements: by name, the first texels kept (textures.c's add). */
static KhdaysTextureReplacement added[16];
static char added_names[16][64];
static int nadded;

int khdays_textures_add_info(const char *name, int width, int height, const uint32_t *texels, uint32_t info)
{
    for (int i = 0; i < nadded; ++i) {
        if (strcmp(added_names[i], name) == 0)
            return i;
    }
    snprintf(added_names[nadded], sizeof(added_names[nadded]), "%s", name);
    added[nadded].width = width;
    added[nadded].height = height;
    added[nadded].rgba = texels;
    added[nadded].info = info;
    return nadded++;
}

const KhdaysTextureReplacement *khdays_textures_replacement(int id)
{
    return &added[id];
}

uint32_t *khdays_textures_decode_pages(const KhdaysVramPages *pages, uint32_t param, uint32_t pltt)
{
    int width, height;
    uint32_t *texels;
    khdays_tex_size(param, &width, &height);
    texels = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)width * (size_t)height);
    khdays_tex_decode(pages, param, pltt, texels); /* the DS's 5-bit colours: enough to compare */
    return texels;
}

/* The draws' bank (gfx/g3d_geometry.c). */
static int32_t sent_bank = -1;
void khdays_g3d_texture_bank(int32_t bank)
{
    sent_bank = bank;
}

/* The NitroSystem managers: their state 1 (the DS's) or 2 (reset), as every byte of a kept state. */
static int tex_state = 1, pltt_state = 1;
void NNS_GfdResetFrmTexVramState(void)
{
    tex_state = 2;
}
void NNS_GfdGetFrmTexVramState(KhdaysRef state)
{
    memset(state_at(state), tex_state, 0x28);
}
void NNS_GfdSetFrmTexVramState(KhdaysRef state)
{
    tex_state = state_at(state)[0x27];
}
void NNS_GfdResetFrmPlttVramState(void)
{
    pltt_state = 2;
}
void GFXi_SaveStateTo(KhdaysRef state)
{
    memset(state_at(state), pltt_state, 8);
}
void NNS_GfdSetFrmPlttVramState(KhdaysRef state)
{
    pltt_state = state_at(state)[7];
}

static void put32(unsigned char *at, uint32_t v)
{
    memcpy(at, &v, 4);
}

static void put16(unsigned char *at, uint32_t v)
{
    at[0] = (unsigned char)v;
    at[1] = (unsigned char)(v >> 8);
}

static uint32_t get16(const unsigned char *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8;
}

/* A model file at `ref`: a Nitro header of `size` bytes, its texture block at +0x40 with an 8x8 256-colour
 * texture (texel i colour i % 16) for texture place 0x100, an 8x8 4x4-compressed one for 0x40008 and its
 * indices, and 16 colours (colour i red i + `shade`) for palette place 0x20 -- the keys as
 * NNS_G3dTexSetTexKey and the palette key's setter leave them. Its texture block. */
static unsigned char *model_file(KhdaysRef ref, uint32_t size, int shade)
{
    unsigned char *file = (unsigned char *)KHDAYS_W(ref);
    unsigned char *tex = file + 0x40;
    memcpy(file, "BMD0", 4);
    put32(file + 8, size);
    put32(tex + 0x08, 0x100 >> 3);   /* texInfo.vramKey */
    put16(tex + 0x0c, 64 >> 3);      /* sizeTex */
    put32(tex + 0x14, 0x100);        /* ofsTex */
    put32(tex + 0x18, 0x40008 >> 3); /* tex4x4Info.vramKey */
    put16(tex + 0x1c, 16 >> 3);      /* sizeTex */
    put32(tex + 0x24, 0x180);        /* ofsTex */
    put32(tex + 0x28, 0x1a0);        /* ofsTexPlttIdx */
    put32(tex + 0x2c, 0x20 >> 3);    /* the palettes' vramKey */
    put16(tex + 0x30, 32 >> 3);      /* size */
    put32(tex + 0x38, 0x200);        /* ofs */
    for (int i = 0; i < 64; ++i)
        tex[0x100 + i] = (unsigned char)(i % 16);
    memset(tex + 0x180, 0x55, 16); /* every 4x4 texel: its block's colour 1 */
    for (int i = 0; i < 8; i += 2) {
        tex[0x1a0 + i] = 0x01; /* each block's: 4 colours (mode 2) from palette colour 2 */
        tex[0x1a0 + i + 1] = 0x80;
    }
    for (int i = 0; i < 16; ++i)
        put16(tex + 0x200 + i * 2, (uint32_t)(i + shade) & 0x7fff); /* colour i: red i (+ shade) */
    return tex;
}

/* The join's steps (prepare.py, G3dRes_DefaultSetup) for a file the DS's VRAM did not hold. */
static void set_up_in_a_bank(KhdaysRef ref, unsigned char *tex, int allocated)
{
    khdays_texture_banks_file(KHDAYS_W(ref));
    CHECK(khdays_texture_banks_open());
    CHECK(tex_state == 2 && pltt_state == 2); /* the allocations come from empty managers */
    khdays_texture_banks_close(KHDAYS_W(ref), tex, allocated);
    CHECK(tex_state == 1 && pltt_state == 1); /* the DS's given back */
}

int main(void)
{
    const uint32_t param = 0x100 >> 3 | 0u << 20 | 0u << 23 | 4u << 26; /* 8x8, 256 colours, at 0x100 */
    unsigned char *tex;
    int32_t id, again;
    khdays_window = (unsigned char *)calloc(1, WINDOW_BYTES);
    if (khdays_window == NULL) {
        printf("texture_banks_test: no memory\n");
        return 1;
    }

    /* the file in bank 1: its bytes at its keys' places, marked loaded */
    tex = model_file(0x02100000u, 0x400, 0);
    set_up_in_a_bank(0x02100000u, tex, 1);
    CHECK(strstr(logged, "bank 1") != NULL);
    CHECK((get16(tex + 0x10) & 1) && (get16(tex + 0x20) & 1) && (get16(tex + 0x32) & 1));

    /* a draw of a model in the file: its textures from bank 1; one outside it inside that draw, from the
     * DS's; and back */
    khdays_texture_banks_draw_begin(KHDAYS_W(0x02100010u));
    CHECK(sent_bank == 1);
    khdays_texture_banks_draw_begin(KHDAYS_W(0x02000000u));
    CHECK(sent_bank == 0);
    khdays_texture_banks_draw_end();
    CHECK(sent_bank == 1);
    khdays_texture_banks_draw_end();
    CHECK(sent_bank == 0);

    /* the bank's texture decoded: texel i is colour i % 16 of its palette (red i % 16), once */
    id = khdays_texture_banks_replacement(1, param | 1u << 16, 0x20 >> 4);
    CHECK(id == 0 && nadded == 1);
    if (id == 0) {
        CHECK(added[0].width == 8 && added[0].height == 8);
        for (int i = 0; i < 64; ++i)
            CHECK((added[0].rgba[i] & 31) == (uint32_t)(i % 16));
    }
    again = khdays_texture_banks_replacement(1, param, 0x20 >> 4); /* how it repeats is the polygon's */
    CHECK(again == id && nadded == 1);
    CHECK(khdays_texture_banks_replacement(1, param & ~(7u << 26), 0x20 >> 4) == -1); /* untextured */
    CHECK(khdays_texture_banks_replacement(9, param, 0x20 >> 4) == -1);               /* no such bank */

    /* the 4x4 indices where the hardware reads them (slot 1's second half for slot 2's texels): colour 1
     * from palette colour 2 is colour 3 (red 3); read from anywhere else, colour 1 */
    {
        const int32_t id4 = khdays_texture_banks_replacement(1, 0x40008 >> 3 | 5u << 26, 0x20 >> 4);
        CHECK(id4 == 1 && nadded == 2);
        if (id4 == 1) {
            for (int i = 0; i < 64; ++i)
                CHECK((added[1].rgba[i] & 31) == 3);
        }
    }

    /* a second file showing the same texture: its own bank, the same replacement */
    tex = model_file(0x02110000u, 0x400, 0);
    set_up_in_a_bank(0x02110000u, tex, 1);
    CHECK(strstr(logged, "bank 2") != NULL);
    CHECK(khdays_texture_banks_replacement(2, param, 0x20 >> 4) == id && nadded == 2);

    /* a third with other colours: another */
    tex = model_file(0x02120000u, 0x400, 5);
    set_up_in_a_bank(0x02120000u, tex, 1);
    CHECK(khdays_texture_banks_replacement(3, param, 0x20 >> 4) == 2 && nadded == 3);

    /* a file set up after the first one's model, where its texture images were (the game drops them once
     * in VRAM, ModelArchive_DropTextureImage): bank 1 kept */
    khdays_texture_banks_file(KHDAYS_W(0x02100200u));
    CHECK(khdays_texture_banks_replacement(1, param, 0x20 >> 4) == id);

    /* one set up where the first one's model was (in the DS's VRAM): bank 1 dropped, its slot taken again */
    khdays_texture_banks_file(KHDAYS_W(0x02100100u));
    CHECK(tex_state == 1 && pltt_state == 1);
    CHECK(khdays_texture_banks_replacement(1, param, 0x20 >> 4) == -1);
    khdays_texture_banks_draw_begin(KHDAYS_W(0x02100010u));
    CHECK(sent_bank == 0);
    khdays_texture_banks_draw_end();
    tex = model_file(0x02130000u, 0x400, 0);
    set_up_in_a_bank(0x02130000u, tex, 1);
    CHECK(strstr(logged, "bank 1") != NULL);

    /* no place even in an empty bank: no bank, the DS's managers given back */
    tex = model_file(0x02140000u, 0x400, 0);
    set_up_in_a_bank(0x02140000u, tex, 0);
    CHECK(strstr(logged, "no place") != NULL);
    khdays_texture_banks_draw_begin(KHDAYS_W(0x02140010u));
    CHECK(sent_bank == 0);
    khdays_texture_banks_draw_end();

    free(khdays_window);
    if (failures == 0) {
        printf("texture_banks_test: ok\n");
    }
    return failures != 0;
}
