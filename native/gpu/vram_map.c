#include "vram_map.h"

#include <string.h>

const uint32_t khdays_vram_bank_offset[KHDAYS_VRAM_BANKS] = {
    0x00000, 0x20000, 0x40000, 0x60000, 0x80000, 0x90000, 0x94000, 0x98000, 0xa0000};
const uint32_t khdays_vram_bank_size[KHDAYS_VRAM_BANKS] = {
    0x20000, 0x20000, 0x20000, 0x20000, 0x10000, 0x4000, 0x4000, 0x8000, 0x4000};
const uint32_t khdays_vram_area_size[KHDAYS_AREA_COUNT] = {
    0x80000, 0x40000, 0x20000, 0x20000, 0x8000, 0x4000, 0x8000, 0x4000, 0x80000, 0x18000};
const uint32_t khdays_vram_cnt_reg[KHDAYS_VRAM_BANKS] = {
    0x240, 0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x248, 0x249};

static const uint32_t area_cpu[KHDAYS_AREA_COUNT] = {
    0x06000000, 0x06400000, 0x06200000, 0x06600000};

enum { A, B, C, D, E, F, G, H, I };

/* GBATEK's table, bank by bank. */
int khdays_vram_place(int bank, uint8_t cnt, KhdaysBankPlace *place)
{
    const unsigned ofs = (cnt >> 3) & 3;
    unsigned mst = cnt & 7;
    int area = -1;
    uint32_t offset = 0;
    place->area = -1;
    place->offset = 0;
    place->cpu = 0;
    if (!(cnt & 0x80)) {
        return 1;  /* disabled */
    }
    if (bank == A || bank == B || bank == H || bank == I) {
        mst &= 3;  /* bit 2 is not used by these */
    }
    if (mst == 0) {  /* LCDC: plain CPU access */
        place->cpu = 0x06800000 + khdays_vram_bank_offset[bank];
        return 1;
    }
    switch (bank) {
    case A:
    case B:
        if (mst == 1) { area = KHDAYS_AREA_ABG; offset = 0x20000 * ofs; }
        else if (mst == 2) { area = KHDAYS_AREA_AOBJ; offset = 0x20000 * (ofs & 1); }
        else { area = KHDAYS_AREA_TEX; offset = 0x20000 * ofs; }
        break;
    case C:
    case D:
        if (mst == 1) { area = KHDAYS_AREA_ABG; offset = 0x20000 * ofs; }
        else if (mst == 2) { return 1; }  /* the ARM7's work RAM */
        else if (mst == 3) { area = KHDAYS_AREA_TEX; offset = 0x20000 * ofs; }
        else if (mst == 4) { area = bank == C ? KHDAYS_AREA_BBG : KHDAYS_AREA_BOBJ; }
        else { return 0; }
        break;
    case E:
        if (mst == 1) { area = KHDAYS_AREA_ABG; }
        else if (mst == 2) { area = KHDAYS_AREA_AOBJ; }
        else if (mst == 3) { area = KHDAYS_AREA_TEXPAL; }
        else if (mst == 4) { area = KHDAYS_AREA_ABGEXT; }  /* only the lower 32 KB */
        else { return 0; }
        break;
    case F:
    case G:
        if (mst == 1 || mst == 2) {
            area = mst == 1 ? KHDAYS_AREA_ABG : KHDAYS_AREA_AOBJ;
            offset = 0x4000 * (ofs & 1) + 0x10000 * (ofs >> 1);
        } else if (mst == 3) {
            area = KHDAYS_AREA_TEXPAL;
            offset = 0x4000 * ((ofs & 1) + (ofs >> 1) * 4);
        } else if (mst == 4) {
            area = KHDAYS_AREA_ABGEXT;
            offset = 0x4000 * (ofs & 1);
        } else if (mst == 5) {
            area = KHDAYS_AREA_AOBJEXT;
        } else {
            return 0;
        }
        break;
    case H:
        if (mst == 1) { area = KHDAYS_AREA_BBG; }
        else if (mst == 2) { area = KHDAYS_AREA_BBGEXT; }
        else { return 0; }
        break;
    case I:
        if (mst == 1) { area = KHDAYS_AREA_BBG; offset = 0x8000; }
        else if (mst == 2) { area = KHDAYS_AREA_BOBJ; }
        else { area = KHDAYS_AREA_BOBJEXT; }
        break;
    default:
        return 0;
    }
    place->area = area;
    place->offset = offset;
    if (area <= KHDAYS_AREA_BOBJ) {
        place->cpu = area_cpu[area] + offset;
    }
    return 1;
}

int khdays_vram_build_pages(const uint8_t cnt[KHDAYS_VRAM_BANKS],
                            uint8_t *const memory[KHDAYS_VRAM_BANKS], KhdaysVramPages *pages)
{
    int overlaps = 0;
    memset(pages, 0, sizeof(*pages));
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        KhdaysBankPlace place;
        uint32_t size;
        if (!khdays_vram_place(bank, cnt[bank], &place) || place.area < 0) {
            continue;
        }
        size = khdays_vram_bank_size[bank];
        if (size > khdays_vram_area_size[place.area]) {
            size = khdays_vram_area_size[place.area];  /* E as extended palettes: 32 KB */
        }
        for (uint32_t at = 0; at < size; at += KHDAYS_VRAM_PAGE) {
            const uint32_t page = (place.offset + at) / KHDAYS_VRAM_PAGE;
            if (page >= khdays_vram_area_size[place.area] / KHDAYS_VRAM_PAGE) {
                break;
            }
            if (pages->page[place.area][page] != NULL) {
                ++overlaps;
            }
            pages->page[place.area][page] = memory[bank] + at;
        }
    }
    return overlaps;
}
