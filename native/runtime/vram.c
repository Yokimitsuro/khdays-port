/* The DS's nine VRAM banks (GBATEK, "DS Memory Control - VRAM").
 *
 * A bank mapped where the ARM9 sees it (LCDC, or a BG/OBJ window) lives in
 * that window of the address space -- memory.c commits 0x06000000-0x068a3fff
 * as plain memory -- so the game's stores and DMA reach it directly. A bank
 * the CPU does not see (extended palettes, textures, the ARM7, disabled)
 * lives in `store`. When VRAMCNT changes a mapping the contents move: every
 * visible bank is saved from its old window, the old windows are cleared
 * (the hardware reads zero where no bank is mapped), and the banks are loaded
 * into their new windows. The display hardware reads through page tables that
 * point at wherever each bank lives. */
#include "vram.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

static u8 store[KHDAYS_VRAM_TOTAL];
static u8 current[KHDAYS_VRAM_BANKS];  /* the VRAMCNT values in effect */
static KhdaysBankPlace place[KHDAYS_VRAM_BANKS];
static KhdaysVramPages pages;

static u8 *bank_memory(int bank)
{
    return place[bank].cpu != 0 ? (u8 *)place[bank].cpu : store + khdays_vram_bank_offset[bank];
}

static void rebuild_pages(void)
{
    u8 *memory[KHDAYS_VRAM_BANKS];
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        memory[bank] = bank_memory(bank);
    }
    if (khdays_vram_build_pages(current, memory, &pages) != 0) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "vram: two banks mapped to the same place (the hardware ORs their "
                            "reads; here the later bank wins)\n");
        }
    }
}

void khdays_vram_reset(void)
{
    memset(current, 0, sizeof(current));
    memset(place, 0, sizeof(place));
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        place[bank].area = -1;
    }
    rebuild_pages();
}

void khdays_vram_control_written(void)
{
    u8 next[KHDAYS_VRAM_BANKS];
    int changed = 0;
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        next[bank] = IO8(khdays_vram_cnt_reg[bank]);
        changed |= next[bank] != current[bank];
    }
    if (!changed) {
        return;
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        if (place[bank].cpu != 0) {
            memcpy(store + khdays_vram_bank_offset[bank], (const void *)place[bank].cpu,
                   khdays_vram_bank_size[bank]);
        }
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        if (place[bank].cpu != 0) {
            memset((void *)place[bank].cpu, 0, khdays_vram_bank_size[bank]);
        }
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        current[bank] = next[bank];
        if (!khdays_vram_place(bank, next[bank], &place[bank])) {
            fprintf(stderr, "vram: bank %c control 0x%02x is not a mapping GBATEK lists; "
                            "the bank is left unmapped\n", 'A' + bank, next[bank]);
        }
    }
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        if (place[bank].cpu != 0) {
            memcpy((void *)place[bank].cpu, store + khdays_vram_bank_offset[bank],
                   khdays_vram_bank_size[bank]);
        }
    }
    rebuild_pages();
}

const KhdaysVramPages *khdays_vram_pages(void)
{
    return &pages;
}

const u8 *khdays_vram_bank(int bank)
{
    return bank_memory(bank);
}
