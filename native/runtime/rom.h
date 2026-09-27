/* The game card's ROM as the card bus serves it, assembled from the port's
 * extracted data and mods (rom.cpp). */
#ifndef KHDAYS_ROM_H
#define KHDAYS_ROM_H

#include "../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Finds the extracted data (data/extracted/<hash>) and indexes it. Returns 0
 * and says why on failure. */
int khdays_rom_init(void);

/* The 0x200-byte cartridge header. */
const u8 *khdays_rom_header(void);

/* `size` bytes of the ROM at `offset`. */
void khdays_rom_read(u32 offset, u8 *destination, u32 size);

#ifdef __cplusplus
}
#endif

#endif
