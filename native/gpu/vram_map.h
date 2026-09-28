/* Where the DS's nine VRAM banks appear, from the VRAMCNT registers (GBATEK,
 * "DS Memory Control - VRAM"). Pure: the game runtime (runtime/vram.c) and
 * the savestate test both build their page tables with it. */
#ifndef KHDAYS_VRAM_MAP_H
#define KHDAYS_VRAM_MAP_H

#include <stdint.h>

#define KHDAYS_VRAM_BANKS 9       /* A-I */
#define KHDAYS_VRAM_TOTAL 0xa4000u /* 656 KB, banks in LCDC order */
#define KHDAYS_VRAM_PAGE  0x4000u  /* 16 KB: the smallest bank */

/* The spaces the banks can be placed in, as the display hardware sees them. */
enum {
    KHDAYS_AREA_ABG,     /* engine A BG, 512 KB (CPU 0x06000000) */
    KHDAYS_AREA_AOBJ,    /* engine A OBJ, 256 KB (CPU 0x06400000) */
    KHDAYS_AREA_BBG,     /* engine B BG, 128 KB (CPU 0x06200000) */
    KHDAYS_AREA_BOBJ,    /* engine B OBJ, 128 KB (CPU 0x06600000) */
    KHDAYS_AREA_ABGEXT,  /* engine A BG extended palettes, slots 0-3 of 8 KB */
    KHDAYS_AREA_AOBJEXT, /* engine A OBJ extended palette, 8 KB */
    KHDAYS_AREA_BBGEXT,
    KHDAYS_AREA_BOBJEXT,
    KHDAYS_AREA_TEX,     /* 3D texture image slots 0-3 of 128 KB */
    KHDAYS_AREA_TEXPAL,  /* 3D texture palette slots 0-5 of 16 KB */
    KHDAYS_AREA_COUNT
};

#define KHDAYS_AREA_MAX_PAGES 32

/* Each area's 16 KB pages: the memory holding that page, or NULL where no
 * bank is mapped (the hardware reads zero there). */
typedef struct KhdaysVramPages {
    uint8_t *page[KHDAYS_AREA_COUNT][KHDAYS_AREA_MAX_PAGES];
} KhdaysVramPages;

extern const uint32_t khdays_vram_bank_offset[KHDAYS_VRAM_BANKS]; /* in LCDC order */
extern const uint32_t khdays_vram_bank_size[KHDAYS_VRAM_BANKS];
extern const uint32_t khdays_vram_area_size[KHDAYS_AREA_COUNT];

/* Where bank `bank` sits for its control value `cnt`: the area and the byte
 * offset in it (area -1: nowhere the display hardware reads, e.g. disabled,
 * LCDC or the ARM7), and the ARM9 address it is visible at (0: none). */
typedef struct KhdaysBankPlace {
    int area;
    uint32_t offset;
    uint32_t cpu;
} KhdaysBankPlace;

/* Returns 0 for a control value GBATEK does not define (the bank then maps
 * nowhere). */
int khdays_vram_place(int bank, uint8_t cnt, KhdaysBankPlace *place);

/* Builds the page tables. `memory[b]` holds bank b's contents. Returns the
 * number of pages two banks were mapped to at once (the hardware would OR
 * their reads; here the later bank wins). */
int khdays_vram_build_pages(const uint8_t cnt[KHDAYS_VRAM_BANKS],
                            uint8_t *const memory[KHDAYS_VRAM_BANKS],
                            KhdaysVramPages *pages);

/* The VRAMCNT register offsets (from 0x04000000) of banks A-I. */
extern const uint32_t khdays_vram_cnt_reg[KHDAYS_VRAM_BANKS];

#endif
