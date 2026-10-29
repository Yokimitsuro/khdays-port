/* A BG layer as the parts read it (bg.h). */
#include "bg.h"
#include "../runtime/core/io.h"
#include "../runtime/core/origin.h"
#include "../runtime/core/vram.h"

const uint8_t *khdays_bg_memory(int engine, uint32_t address)
{
    const KhdaysVramPages *pages = khdays_vram_pages();
    const uint8_t *page = pages->page[engine ? KHDAYS_AREA_BBG : KHDAYS_AREA_ABG][(address / KHDAYS_VRAM_PAGE) % 32u];
    return page != NULL ? page + address % KHDAYS_VRAM_PAGE : NULL;
}

uint32_t khdays_bg_chars_base(int engine, int bg)
{
    const uint32_t io = engine ? 0x1000u : 0u;
    const uint32_t cnt = IO16(io + 0x08u + 2u * (uint32_t)bg), dispcnt = IO32(io);
    return (engine ? 0u : ((dispcnt >> 24) & 7u) * 0x10000u) + ((cnt >> 2) & 15u) * 0x4000u;
}

int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *l)
{
    static const int8_t family[8][4] = {
        {0, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 1},   {0, 0, 0, 2},
        {0, 0, 1, 2}, {0, 0, 2, 2}, {0, -1, 3, -1}, {-1, -1, -1, -1},
    };
    const uint32_t io = engine ? 0x1000u : 0u;
    const uint32_t cnt = IO16(io + 0x08u + 2u * (uint32_t)bg), dispcnt = IO32(io);
    const uint32_t mode = dispcnt & 7u;
    if (!(dispcnt & (0x100u << bg)) || (engine && mode >= 6u) || family[mode][bg] != 0 ||
        (!engine && bg == 0 && (dispcnt & 0x08u))) {
        return 0;
    }
    l->map = (engine ? 0u : ((dispcnt >> 27) & 7u) * 0x10000u) + ((cnt >> 8) & 31u) * 0x800u;
    l->chars = khdays_bg_chars_base(engine, bg);
    l->wt = (cnt >> 14) & 1 ? 64 : 32;
    l->ht = (cnt >> 15) & 1 ? 64 : 32;
    l->hofs = (int)(IO16(io + 0x10u + 4u * (uint32_t)bg) & 0x1ffu);
    l->vofs = (int)(IO16(io + 0x12u + 4u * (uint32_t)bg) & 0x1ffu);
    l->colors256 = (cnt >> 7) & 1;
    l->priority = (int)(cnt & 3u);
    return 1;
}

uint32_t khdays_bg_map_entry(int engine, const KhdaysBgLayer *l, int cx, int cy)
{
    uint32_t a;
    const uint8_t *p;
    cx = ((cx % l->wt) + l->wt) % l->wt;
    cy = ((cy % l->ht) + l->ht) % l->ht;
    /* the map in 32x32-cell blocks: across first, then down */
    a = l->map + (uint32_t)(cy >> 5) * (l->wt == 64 ? 2u : 1u) * 0x800u + (uint32_t)(cx >> 5) * 0x800u +
        (uint32_t)((cy & 31) * 32 + (cx & 31)) * 2u;
    p = khdays_bg_memory(engine, a);
    return p != NULL ? (uint32_t)(p[0] | p[1] << 8) : 0u;
}

int khdays_bg_cell_shows(int engine, const KhdaysBgLayer *l, uint32_t entry)
{
    const uint32_t size = l->colors256 ? 64u : 32u;
    const uint32_t at = l->chars + (entry & 0x3ffu) * size;
    for (uint32_t k = 0; k < size; ++k) {
        const uint8_t *p = khdays_bg_memory(engine, at + k);
        if (p != NULL && *p != 0) {
            return 1;
        }
    }
    return 0;
}

int khdays_bg_source(int engine, uint32_t address, const char **file, uint32_t *offset, uint32_t *run)
{
    const uint8_t *at = khdays_bg_memory(engine, address);
    if (at == NULL) {
        return 0;
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        const uint8_t *base = khdays_vram_bank(bank);
        if (base != NULL && at >= base && at < base + khdays_vram_bank_size[bank]) {
            return khdays_origin_of_bank(bank, (u32)(at - base), file, offset, run);
        }
    }
    return 0;
}

const char *khdays_bg_origin(int engine, uint32_t address)
{
    const char *file;
    uint32_t offset, run;
    return khdays_bg_source(engine, address, &file, &offset, &run) ? file : NULL;
}
