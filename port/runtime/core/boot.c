/* What the BIOS and firmware leave in main RAM before the game's crt0 runs (GBATEK, "BIOS RAM
 * Usage"). Only documented values are written; a field that depends on the physical cartridge or
 * console stays zero until something needs it. */
#include "../../hal/hal.h"
#include "card.h"
#include "rom.h"
#include "runtime.h"
#include "window.h"
#include "../../host/app/host.h"
#include "../../host/app/instance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define U16(address) (*(volatile u16 *)KHDAYS_W(address))
#define U32(address) (*(volatile u32 *)KHDAYS_W(address))

/* The firmware's user settings at 0x027ffc80 (GBATEK, "DS Firmware User Settings"): the player's
 * language and nickname, and the port's own touch calibration (12-bit ADC = pixel * 16, as arm7.c
 * converts). The rest stays zero. */
static u32 language_code(void)
{
    return (u32)khdays_rom_language(khdays_settings_game_language()); /* NDS codes: 1 English .. 5 Spanish */
}

static void console_mac(volatile u8 *mac);

/* The parameter the game's OS_ResetSystem left (start.c), for the boot after it. */
static u32 reset_parameter;
static int reset_given;

void khdays_boot_reset_parameter(u32 parameter)
{
    reset_parameter = parameter;
    reset_given = 1;
}

static void user_settings(void)
{
    volatile u8 *const s = (volatile u8 *)KHDAYS_W(0x027ffc80);
    const u32 x1 = 0x20, y1 = 0x20, x2 = 0xe0, y2 = 0xa0;
    for (int i = 0; i < 0x70; ++i) {
        s[i] = 0;
    }
    *(volatile u16 *)(s + 0x00) = 5;                    /* version */
    *(volatile u16 *)(s + 0x58) = (u16)(x1 * 16);       /* adc.x1 */
    *(volatile u16 *)(s + 0x5a) = (u16)(y1 * 16);       /* adc.y1 */
    s[0x5c] = (u8)x1;                                   /* scr.x1 */
    s[0x5d] = (u8)y1;                                   /* scr.y1 */
    *(volatile u16 *)(s + 0x5e) = (u16)(x2 * 16);       /* adc.x2 */
    *(volatile u16 *)(s + 0x60) = (u16)(y2 * 16);       /* adc.y2 */
    s[0x62] = (u8)x2;                                   /* scr.x2 */
    s[0x63] = (u8)y2;                                   /* scr.y2 */
    *(volatile u16 *)(s + 0x64) = (u16)language_code(); /* language */
    khdays_boot_owner_name();
    console_mac(s + 0x74);
}

/* The owner's nickname, 10 UTF-16 characters and their count (+0x06, +0x1a): the name Mission Mode's
 * multiplayer shows the others. The player's, from the settings; written at boot and again when the
 * settings close. */
void khdays_boot_owner_name(void)
{
    volatile u8 *const s = (volatile u8 *)KHDAYS_W(0x027ffc80);
    u16 name[10];
    char nickname[64];
    int length;
    khdays_settings_nickname(nickname, sizeof(nickname));
    length = khdays_host_player_name(nickname, name, 10);
    for (int i = 0; i < 10; ++i) {
        *(volatile u16 *)(s + 0x06 + 2 * i) = i < length ? name[i] : 0;
    }
    *(volatile u16 *)(s + 0x1a) = (u16)length;
}

/* The console's Wi-Fi MAC address at 0x027ffcf4: KHDAYS_MAC ("00:09:bf:12:34:56") or DeSmuME's
 * default. It must not be all zero: DS Protect (ov028) takes that for an emulator, and the story then
 * never starts. */
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

/* The game's one ARM9 BIOS read (func_0200f78c) copies the Nintendo logo at 0xffff0020, the same 156
 * bytes as the cartridge header's at 0xc0, served from there. Any other BIOS read stops. */
void khdays_bios_read(unsigned int address, void *destination, unsigned int size)
{
    if (address >= 0xffff0020u && address + size <= 0xffff0020u + 156) {
        memcpy(destination, khdays_rom_header() + 0xc0 + (address - 0xffff0020u), size);
        return;
    }
    khdays_hal_unimplemented("a read of the ARM9 BIOS other than its logo");
}

void khdays_boot_environment(void)
{
    const u8 *header = khdays_rom_header();
    const u16 header_crc = (u16)(header[0x15e] | header[0x15f] << 8);

    /* The cartridge header, 0x170 bytes of it. */
    memcpy(KHDAYS_W(0x027ffe00), header, 0x170);

    /* Boot results, and their copies at 0x027ffc00. */
    U32(0x027ff800) = KHDAYS_CARD_CHIP_ID; /* chip ID 1, as the card answers it */
    U32(0x027ffc00) = KHDAYS_CARD_CHIP_ID;
    U16(0x027ff808) = header_crc; /* header CRC (verified) */
    U16(0x027ff80c) = 0;          /* cart CRC okay */
    U16(0x027ff80e) = 0;          /* secure area okay */
    U16(0x027ff810) = 0xffff;     /* boot handler task number at cart boot */
    U16(0x027ff850) = 0x5835;     /* NDS7 BIOS CRC */
    U32(0x027ff880) = 7;          /* message from NDS9 to NDS7 at cart boot */
    U32(0x027ff884) = 6;          /* NDS7 boot task at cart boot */
    U16(0x027ffc08) = header_crc;
    U16(0x027ffc0c) = 0;
    U16(0x027ffc0e) = 0;
    U16(0x027ffc10) = 0x5835;
    U16(0x027ffc40) = 1; /* boot indicator: normal */

    /* After OS_ResetSystem the game finds the parameter it left at 0x027ffc20; KHDAYS_RESET_PARAMETER
     * gives one to a game started afresh. */
    if (reset_given) {
        U32(0x027ffc20) = reset_parameter;
    } else {
        const char *reset = getenv("KHDAYS_RESET_PARAMETER");
        if (reset != NULL) {
            U32(0x027ffc20) = (u32)strtoul(reset, NULL, 0);
        }
    }

    user_settings();
}
