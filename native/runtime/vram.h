/* The VRAM banks behind the ARM9's VRAM windows (vram.c). */
#ifndef KHDAYS_VRAM_H
#define KHDAYS_VRAM_H

#include "../gpu/vram_map.h"
#include "../hal/hal.h"

/* Every bank disabled, as at power-on. */
void khdays_vram_reset(void);

/* After a write to VRAMCNT_A-I (0x04000240-0x04000249): move the banks whose
 * mapping changed. */
void khdays_vram_control_written(void);

/* What the display hardware reads (the page tables of the current mapping). */
const KhdaysVramPages *khdays_vram_pages(void);

/* Where bank `bank` (0 = A) holds its contents now. */
const u8 *khdays_vram_bank(int bank);

#endif
