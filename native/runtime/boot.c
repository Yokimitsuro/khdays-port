/* What the BIOS and firmware leave in main RAM when they start a cartridge,
 * before the game's crt0 runs (GBATEK, "BIOS RAM Usage"). Only documented
 * values are written; a field whose value depends on the physical cartridge
 * or console and is not known here stays zero until something needs it. */
#include "../hal/hal.h"
#include "card.h"
#include "rom.h"
#include "runtime.h"

#include <string.h>

#define U16(address) (*(volatile u16 *)(address))
#define U32(address) (*(volatile u32 *)(address))

void khdays_boot_environment(void)
{
    const u8 *header = khdays_rom_header();
    const u16 header_crc = (u16)(header[0x15e] | header[0x15f] << 8);

    /* The cartridge header, 0x170 bytes of it. */
    memcpy((void *)0x027ffe00, header, 0x170);

    /* Boot results, and their copies at 0x027ffc00. */
    U32(0x027ff800) = KHDAYS_CARD_CHIP_ID;  /* chip ID 1, as the card answers it */
    U32(0x027ffc00) = KHDAYS_CARD_CHIP_ID;
    U16(0x027ff808) = header_crc;  /* header CRC (verified) */
    U16(0x027ff80c) = 0;           /* cart CRC okay */
    U16(0x027ff80e) = 0;           /* secure area okay */
    U16(0x027ff810) = 0xffff;      /* boot handler task number at cart boot */
    U16(0x027ff850) = 0x5835;      /* NDS7 BIOS CRC */
    U32(0x027ff880) = 7;           /* message from NDS9 to NDS7 at cart boot */
    U32(0x027ff884) = 6;           /* NDS7 boot task at cart boot */
    U16(0x027ffc08) = header_crc;
    U16(0x027ffc0c) = 0;
    U16(0x027ffc0e) = 0;
    U16(0x027ffc10) = 0x5835;
    U16(0x027ffc40) = 1;           /* boot indicator: normal */
}
