/* What the BIOS and firmware leave in main RAM when they start a cartridge,
 * before the game's crt0 runs (GBATEK, "BIOS RAM Usage"). Only documented
 * values are written; a field whose value depends on the physical cartridge
 * or console and is not known here stays zero until something needs it. */
#include "../hal/hal.h"
#include "card.h"
#include "rom.h"
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define U16(address) (*(volatile u16 *)(address))
#define U32(address) (*(volatile u32 *)(address))

/* The firmware's user settings at 0x027ffc80 (GBATEK, "DS Firmware User
 * Settings"). The language is the player's (KHDAYS_LANGUAGE, else the
 * system's, as the port chooses it). The touch calibration is the port's own
 * pair, the one the touch service converts host positions with: 12-bit ADC =
 * pixel * 16 (arm7.c). Nothing else of it is known to matter to the game and
 * stays zero. */
static u32 language_code(void)
{
    /* NDS codes: 1 English, 2 French, 3 German, 4 Italian, 5 Spanish */
    char name[16] = "";
    const char *env = getenv("KHDAYS_LANGUAGE");
    if (env != NULL) {
        /* (not strncpy: the game's own C library, linked in, defines it) */
        for (int i = 0; i < (int)sizeof(name) - 1 && env[i] != '\0'; ++i) {
            name[i] = env[i];
        }
    } else {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH];
        if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0) {
            name[0] = (char)locale[0];
            name[1] = (char)locale[1];
        }
    }
    if (_strnicmp(name, "fr", 2) == 0) return 2;
    if (_strnicmp(name, "de", 2) == 0) return 3;
    if (_strnicmp(name, "it", 2) == 0) return 4;
    if (_strnicmp(name, "es", 2) == 0) return 5;
    return 1;  /* English, as the port defaults */
}

static void console_mac(volatile u8 *mac);

static void user_settings(void)
{
    volatile u8 *const s = (volatile u8 *)0x027ffc80;
    const u32 x1 = 0x20, y1 = 0x20, x2 = 0xe0, y2 = 0xa0;
    for (int i = 0; i < 0x70; ++i) {
        s[i] = 0;
    }
    *(volatile u16 *)(s + 0x00) = 5;                      /* version */
    *(volatile u16 *)(s + 0x58) = (u16)(x1 * 16);         /* adc.x1 */
    *(volatile u16 *)(s + 0x5a) = (u16)(y1 * 16);         /* adc.y1 */
    s[0x5c] = (u8)x1;                                     /* scr.x1 */
    s[0x5d] = (u8)y1;                                     /* scr.y1 */
    *(volatile u16 *)(s + 0x5e) = (u16)(x2 * 16);         /* adc.x2 */
    *(volatile u16 *)(s + 0x60) = (u16)(y2 * 16);         /* adc.y2 */
    s[0x62] = (u8)x2;                                     /* scr.x2 */
    s[0x63] = (u8)y2;                                     /* scr.y2 */
    *(volatile u16 *)(s + 0x64) = (u16)language_code();   /* language */
    console_mac(s + 0x74);
}

/* The console's Wi-Fi MAC address, which the firmware copies after the user
 * settings (0x027ffcf4; OS_GetMacAddress reads it). A real console has its
 * own; this one is KHDAYS_MAC ("00:09:bf:12:34:56" form) or else DeSmuME's
 * default, the one found in every savestate of this game. It matters: DS
 * Protect (ov028) takes an all-zero MAC for an emulator, and the story's
 * start (Ov000_BootRunSelector) then requests no scene at all. */
static void console_mac(volatile u8 *mac)
{
    static const u8 fallback[6] = {0x00, 0x09, 0xbf, 0x12, 0x34, 0x56};
    const char *env = getenv("KHDAYS_MAC");
    unsigned v[6];
    if (env != NULL && sscanf_s(env, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        for (int i = 0; i < 6; ++i) {
            mac[i] = (u8)v[i];
        }
        return;
    }
    for (int i = 0; i < 6; ++i) {
        mac[i] = fallback[i];
    }
}

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

    /* After OS_ResetSystem the game finds the parameter it left at
     * 0x027ffc20 (OS_GetResetParameter); the process that reset passes it
     * on (start.c). */
    {
        const char *reset = getenv("KHDAYS_RESET_PARAMETER");
        if (reset != NULL) {
            U32(0x027ffc20) = (u32)strtoul(reset, NULL, 0);
        }
    }

    user_settings();
}
