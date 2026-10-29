/* The ARM7's side of the DS (arm7.h): the services the game's ARM9 talks to through PXI -- clock,
 * touch panel, sound, backup memory, power, cartridge -- answered on the PC. */
#include "arm7.h"
#include "clock.h"
#include "events.h"
#include "../input/input.h"
#include "io.h"
#include "pxi.h"
#include "runtime.h"
#include "session_log.h"
#include "window.h"
#include "../sound/sound_bridge.h"
#include "../menus/saveslots.h"
#include "../menus/saveworld.h"
#include "../missions/missions.h"
#include "../scripts/scripts.h"
#include "../../host/app/files.h"
#include "../../host/app/instance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OSSystemWork.pxiHandleChecker for the ARM7: one bit per tag that has a service. The ARM9 polls it
 * before talking to one. */
#define ARM7_HANDLE_CHECKER (*(volatile u32 *)KHDAYS_W(0x027fff8c))

/* PXI tags (NitroSDK PXIFifoTag). */
enum {
    TAG_RTC = 5,
    TAG_TP = 6,
    TAG_SOUND = 7,
    TAG_PM = 8,
    TAG_OS = 12,
    TAG_CTRDG = 13,
};

__declspec(noreturn) static void unimplemented(u32 tag, u32 data, const char *what)
{
    fprintf(stderr, "arm7: tag %u, data 0x%07x: %s\n", tag, data, what);
    fflush(stderr);
    exit(6);
}

/* Replies wait in the port's transport (pxi.h) in order and are never lost, as the DS's FIFO keeps
 * them. */

/* KHDAYS_TRACE_PXI=1: counts the messages by direction, tag and kind, prints the first of each kind
 * with its callers, and the counts every 3600 VBlanks and at exit. */
typedef struct {
    u32 key; /* direction << 31 | tag << 24 | kind */
    u32 count;
} PxiKind;

static int pxi_trace = -1;
static PxiKind *pxi_kinds;
static int pxi_kind_count, pxi_kind_room;
static u32 pxi_skipped; /* touch samples skipped while the ARM9 was behind */

static u32 pxi_kind_of(u32 tag, u32 data, u32 err)
{
    switch (tag) {
    case TAG_RTC: return (data >> 8) & 0x7f;
    case TAG_TP: return (data & 0x2000000) ? 0x100 | ((data >> 8) & 0x7f) : 0x200 | ((data >> 8) & 0x7f);
    case TAG_SOUND: return data == 0 ? 0 : 1; /* process / a list's address */
    case 11: return err << 16 | (data < 0x100 ? data : 0xff);
    default: return err << 16 | (data & 0xffff);
    }
}

static void pxi_report(void)
{
    fprintf(stderr, "pxi: %d kinds of message so far, %u touch samples skipped:\n", pxi_kind_count, pxi_skipped);
    for (int i = 0; i < pxi_kind_count; ++i) {
        fprintf(stderr, "pxi:   %s tag %2u kind 0x%05x: %u\n",
                (pxi_kinds[i].key >> 31) ? "to the ARM9 " : "to the ARM7 ", (pxi_kinds[i].key >> 24) & 0x1f,
                pxi_kinds[i].key & 0xffffff, pxi_kinds[i].count);
    }
    fflush(stderr);
}

static void pxi_note(int to_arm9, u32 tag, u32 data, u32 err)
{
    u32 key;
    int i;
    if (pxi_trace < 0) {
        pxi_trace = getenv("KHDAYS_TRACE_PXI") != NULL;
        if (pxi_trace) {
            atexit(pxi_report);
        }
    }
    if (!pxi_trace) {
        return;
    }
    key = (u32)to_arm9 << 31 | tag << 24 | (pxi_kind_of(tag, data, err) & 0xffffff);
    for (i = 0; i < pxi_kind_count && pxi_kinds[i].key != key; ++i) {
    }
    if (i == pxi_kind_count) {
        char callers[512];
        if (pxi_kind_count == pxi_kind_room) {
            const int room = pxi_kind_room ? pxi_kind_room * 2 : 32;
            PxiKind *grown = (PxiKind *)realloc(pxi_kinds, (size_t)room * sizeof *grown);
            if (grown == NULL) {
                return;
            }
            pxi_kinds = grown;
            pxi_kind_room = room;
        }
        pxi_kinds[pxi_kind_count].key = key;
        pxi_kinds[pxi_kind_count].count = 0;
        ++pxi_kind_count;
        khdays_diag_callers(callers, sizeof(callers), 2);
        fprintf(stderr, "pxi: first %s tag %u kind 0x%x (data 0x%07x, error %u) at VBlank %u: %s\n",
                to_arm9 ? "to the ARM9" : "to the ARM7", tag, key & 0xffffff, data, err, khdays_events_vblank_count(),
                callers);
        fflush(stderr);
    }
    ++pxi_kinds[i].count;
    {
        static u32 reported;
        const u32 vblank = khdays_events_vblank_count();
        if (vblank / 3600 != reported / 3600) {
            reported = vblank;
            pxi_report();
        }
    }
}

/* A word to the ARM9: tag in bits 0-4, error in bit 5, data in bits 6-31. */
void khdays_arm7_reply(u32 tag, u32 data, u32 err)
{
    const u32 word = tag | err << 5 | data << 6;
    pxi_note(1, tag, data, err);
    khdays_pxi_queue(word);
}

/* A request the port does not answer: reported once per tag and kind, with its callers. The caller
 * then refuses it; the SDK's callbacks take the error bit as a failed request. */
static void said_once(u32 tag, u32 data, const char *what)
{
    static u32 *said;
    static int count, room;
    const u32 key = tag << 24 | (pxi_kind_of(tag, data, 1) & 0xffffff);
    char callers[512];
    for (int i = 0; i < count; ++i) {
        if (said[i] == key) {
            return;
        }
    }
    if (count == room) {
        const int grown_room = room != 0 ? room * 2 : 16;
        u32 *grown = (u32 *)realloc(said, sizeof(u32) * (size_t)grown_room);
        if (grown == NULL) {
            return;
        }
        said = grown;
        room = grown_room;
    }
    said[count++] = key;
    khdays_diag_callers(callers, sizeof(callers), 2);
    fprintf(stderr, "arm7: tag %u, data 0x%07x: %s (said once) -- from %s\n", tag, data, what, callers);
    fflush(stderr);
    khdays_log("arm7: tag %u, data 0x%07x: %s", tag, data, what);
}

static void refused(u32 tag, u32 data, const char *what)
{
    said_once(tag, data, what);
    khdays_arm7_reply(tag, data, 1);
}

/* --- Services ---------------------------------------------------------------
 * Each one answers what the ARM9 side (decompiled) sends and waits for. */

/* OS (tag 12): only the DS's system reset sends here, and the port replaces it (hal/sdk_os.c). */
static void os_service(u32 data, u32 err)
{
    (void)err;
    refused(TAG_OS, data, "an OS message (the DS's system reset), not answered here: refused");
}

/* CTRDG (tag 13): CTRDG_Init's INIT_MODULE_INFO gets its command back; the GBA slot is empty. */
static void ctrdg_service(u32 data, u32 err)
{
    (void)err;
    if ((data & 0x3f) == 1) {
        khdays_arm7_reply(TAG_CTRDG, 1, 0);
        return;
    }
    refused(TAG_CTRDG, data, "a cartridge command, not answered here: refused");
}

/* RTC (tag 5): the command in bits 8-14, the reply command << 8 | result. The game links two: 0x12
 * reads the time (BCD, into OSSystemWork.real_time_clock + 4), 0x27 writes status 2. The time is the
 * PC's. */
#define RTC_BUF ((volatile u32 *)KHDAYS_W(0x027ffde8))

static u32 bcd(u32 value)
{
    return (value / 10) << 4 | (value % 10);
}

static void rtc_service(u32 data, u32 err)
{
    const u32 command = (data >> 8) & 0x7f;
    (void)err;
    if (command == 0x12) {
        int hour, minute, second;
        khdays_host_local_time(&hour, &minute, &second);
        RTC_BUF[1] = bcd((u32)hour) | bcd((u32)minute) << 8 | bcd((u32)second) << 16;
        khdays_arm7_reply(TAG_RTC, command << 8, 0);
        return;
    }
    if (command == 0x27) {
        khdays_arm7_reply(TAG_RTC, command << 8, 0);
        return;
    }
    refused(TAG_RTC, data, "an RTC command, not answered here: refused");
}

/* Touch panel (tag 6), the SPI protocol: a request is words from start (bit 25) to end (bit 24), the
 * command in bits 8-14 of the first. A sample goes to 0x027fffaa (x and y as 12-bit ADC values,
 * touched in bit 24). Commands: 0 sample, 1 sample automatically, 2 stop, 3 stability range. */
#define TP_BUF ((volatile u16 *)KHDAYS_W(0x027fffaa))

static u32 tp_words[4];
static int tp_count;
static u32 tp_auto_frequency; /* 0 = off */

static void tp_sample(void)
{
    int x, y;
    u32 sample = 0;
    if (khdays_input_touch(&x, &y)) {
        sample = (u32)(x * 16 + 8) | (u32)(y * 16 + 8) << 12 | 1u << 24;
    }
    TP_BUF[0] = (u16)sample;
    TP_BUF[1] = (u16)(sample >> 16);
}

static void tp_service(u32 data, u32 err)
{
    u32 command;
    (void)err;
    if (data & 0x2000000) {
        tp_count = 0;
    }
    if (tp_count < 4) {
        tp_words[tp_count++] = data;
    }
    if (!(data & 0x1000000)) {
        return; /* more words to come */
    }
    command = (tp_words[0] >> 8) & 0x7f;
    switch (command) {
    case 0: tp_sample(); break;
    case 1: tp_auto_frequency = tp_words[0] & 0xff; break;
    case 2: tp_auto_frequency = 0; break;
    case 3: break;
    default: refused(TAG_TP, tp_words[0], "a touch panel command, not answered here: refused"); return;
    }
    khdays_arm7_reply(TAG_TP, 0x1000000 | command << 8, 0);
}

/* Automatic sampling: `frequency` samples a frame, each announced with command 0x10. Skipped while the
 * ARM9 is behind (the port's choice); KHDAYS_TRACE_PXI counts them. */
void khdays_arm7_frame(void)
{
    *(volatile u16 *)KHDAYS_W(0x027fffa8) = khdays_input_xy();
    if (tp_auto_frequency != 0 && khdays_pxi_waiting(TAG_TP) > 0) {
        pxi_skipped += tp_auto_frequency;
        return;
    }
    for (u32 i = 0; i < tp_auto_frequency; ++i) {
        tp_sample();
        khdays_arm7_reply(TAG_TP, 0x10u << 8, 0);
    }
}

/* Sound (tag 7): passed to the ARM7's sound driver (sound_bridge.c), which replies through the bridge.
 *
 * With the driver off (KHDAYS_SOUND=0) each command list counts as run: the finished-command tag in
 * the shared work (command 0x1d) moves on, which is what the ARM9 waits for. */
static u32 snd_shared_work;

static void sound_service(u32 data, u32 err)
{
    if (khdays_sound_bridge_word(data, err) || data == 0) {
        return;
    }
    for (u32 command = data; command != 0; command = *(volatile u32 *)KHDAYS_W(command)) {
        if (*(volatile u32 *)KHDAYS_W(command + 4) == 0x1d) {
            snd_shared_work = *(volatile u32 *)KHDAYS_W(command + 8);
        }
    }
    if (snd_shared_work != 0) {
        ++*(volatile u32 *)KHDAYS_W(snd_shared_work);
    }
}

/* Backup memory (tag 11, CARDi_Request): a request type with the error bit set (0 INIT, 2 IDENTIFY,
 * 6 read, 7 write, 8 program, 9 verify), INIT followed by the shared CARDiCommandArg's address. The chip
 * lives in memory from saves/khdays.sav, written back whole and atomically when a save ends. */
#define TAG_FS 11
#define CARD_CMD(offset) (*(volatile u32 *)KHDAYS_W(card_cmd + (offset)))

/* saves/khdays.sav, or saves/player<N>/ for a further player on the PC (host/app/instance.h) */
static const char *save_file(void)
{
    static char path[96];
    if (path[0] == 0) {
        snprintf(path, sizeof(path), "%s/khdays.sav", khdays_host_user_folder());
    }
    return path;
}
#define SAVE_FILE save_file()

static u32 card_cmd;
static int card_expect_address;
static u8 *backup;
static u32 backup_size;
static u32 backup_device;
static u32 backup_extent; /* the file's length: as loaded, raised to each write's end */
static int backup_changed;

static void backup_identify(u32 type)
{
    long read;
    backup_device = type & 0xff;
    backup_size = 1u << ((type >> 8) & 0xff);
    free(backup);
    backup = (u8 *)malloc(backup_size);
    if (backup == NULL) {
        unimplemented(TAG_FS, type, "no memory for the backup chip");
    }
    /* never written: the chip as it leaves the factory, all ones */
    memset(backup, 0xff, backup_size);
    read = khdays_host_read_file(SAVE_FILE, backup, backup_size);
    backup_extent = read > 0 ? (u32)read : 0;
    backup_changed = 0;
    /* the save files beyond the chip's three, and the view the game reads its slots through */
    khdays_slots_attach(backup, backup_size, khdays_host_user_folder());
    khdays_saveworld_attach(khdays_host_user_folder(), khdays_slots_highest_counter()); /* the saves' worlds */
}

/* The files a save action wrote: the new missions' progress beside each (runtime/missions/). */
static void saved_files(void)
{
    for (int file; (file = khdays_slots_next_written()) >= 0;) {
        khdays_missions_saved(file);
    }
}

int khdays_backup_commit(void)
{
    u64 start;
    int ok;
    const int slots = khdays_slots_changed();
    if (!khdays_slots_commit()) {
        return 0;
    }
    if (!backup_changed || backup == NULL) {
        if (slots) {
            saved_files();
            khdays_scripts_saved();
            khdays_saveworld_saved(khdays_slots_highest_counter());
        }
        return 1;
    }
    start = khdays_clock_cycles();
    ok = khdays_host_write_file_atomic(SAVE_FILE, backup, backup_extent);
    if (!ok) {
        fprintf(stderr, "arm7: %s could not be written; the game is told its save failed\n", SAVE_FILE);
        fflush(stderr);
        khdays_log("backup: %s could not be written; the game is told its save failed", SAVE_FILE);
        return 0;
    }
    backup_changed = 0;
    khdays_log("backup: %s written whole, %u bytes, in %.1f ms", SAVE_FILE, backup_extent,
               (double)(khdays_clock_cycles() - start) * 1000.0 / KHDAYS_CLOCK_HZ);
    saved_files();
    khdays_scripts_saved();
    khdays_saveworld_saved(khdays_slots_highest_counter());
    return 1;
}

static int backup_range(u32 offset, u32 length)
{
    return backup != NULL && offset <= backup_size && length <= backup_size - offset;
}

static void card_service(u32 data, u32 err)
{
    u32 result = 0; /* CARD_RESULT_SUCCESS */
    if (!err) {
        /* the backup's requests always carry it (CARDi_Request) */
        said_once(TAG_FS, data, "a backup word without the request flag: ignored");
        return;
    }
    if (card_expect_address) {
        card_cmd = data;
        card_expect_address = 0;
        khdays_arm7_reply(TAG_FS, 0, 1);
        return;
    }
    switch (data) {
    case 0: /* INIT: the command block's address comes next */ card_expect_address = 1; return;
    case 2: backup_identify(CARD_CMD(0x04)); break;
    case 6: {
        const u32 src = CARD_CMD(0x0c), dst = CARD_CMD(0x10), len = CARD_CMD(0x14);
        if (!backup_range(src, len)) {
            said_once(TAG_FS, data, "a backup read outside the chip: CARD_RESULT_INVALID_PARAM");
            result = 2; /* CARD_RESULT_INVALID_PARAM */
            break;
        }
        for (u32 i = 0; i < len; ++i) {
            u32 at;
            const u8 *b = khdays_slots_byte(src + i, 0, &at);
            *(volatile u8 *)KHDAYS_W(dst + i) = b != NULL ? *b : 0xff;
        }
        break;
    }
    case 7:
    case 8: {
        const u32 src = CARD_CMD(0x0c), dst = CARD_CMD(0x10), len = CARD_CMD(0x14);
        if (!backup_range(dst, len)) {
            said_once(TAG_FS, data, "a backup write outside the chip: CARD_RESULT_INVALID_PARAM");
            result = 2; /* CARD_RESULT_INVALID_PARAM */
            break;
        }
        for (u32 i = 0; i < len; ++i) {
            const u8 value = *(volatile u8 *)KHDAYS_W(src + i);
            u32 at;
            u8 *b = khdays_slots_byte(dst + i, 1, &at);
            if (b == NULL) {
                continue;
            }
            /* flash programs by clearing bits; an EEPROM just stores */
            *b = (data == 8 && backup_device == 2) ? (u8)(*b & value) : value;
            if (at != 0xffffffffu) {
                /* the chip's: the file keeps its length or grows to the write's end */
                backup_changed = 1;
                backup_extent = at + 1 > backup_extent ? at + 1 : backup_extent;
            }
        }
        break;
    }
    case 9: {
        const u32 src = CARD_CMD(0x0c), dst = CARD_CMD(0x10), len = CARD_CMD(0x14);
        if (!backup_range(dst, len)) {
            said_once(TAG_FS, data, "a backup verify outside the chip: CARD_RESULT_INVALID_PARAM");
            result = 2; /* CARD_RESULT_INVALID_PARAM */
            break;
        }
        for (u32 i = 0; i < len; ++i) {
            u32 at;
            const u8 *b = khdays_slots_byte(dst + i, 0, &at);
            if (b == NULL || *b != *(volatile u8 *)KHDAYS_W(src + i)) {
                result = 1; /* CARD_RESULT_FAILURE */
                break;
            }
        }
        break;
    }
    default:
        /* the erases (10-12, 15) and anything else: the game makes none */
        said_once(TAG_FS, data, "a backup request not answered here: CARD_RESULT_UNSUPPORTED");
        result = 3; /* CARD_RESULT_UNSUPPORTED */
        break;
    }
    CARD_CMD(0x00) = result;
    khdays_arm7_reply(TAG_FS, data, 1);
}

/* PM (tag 8): power management. The game asks only behind the DS's closed lid, which a PC lacks; a
 * request that comes anyway is refused once complete. */
static u32 pm_first;

static void pm_service(u32 data, u32 err)
{
    (void)err;
    if (data & 0x2000000) {
        pm_first = data;
    }
    if (data & 0x1000000) {
        refused(TAG_PM, pm_first, "a power-management request (a PC has no lid): refused");
    }
}

static void (*const services[32])(u32 data, u32 err) = {
    [TAG_RTC] = rtc_service, [TAG_TP] = tp_service, [TAG_SOUND] = sound_service, [TAG_PM] = pm_service,
    [TAG_FS] = card_service, [TAG_OS] = os_service, [TAG_CTRDG] = ctrdg_service,
};

void khdays_arm7_reset(void)
{
    /* the services as at power-on (and at the game's reset); the backup chip keeps what it holds */
    tp_count = 0;
    tp_auto_frequency = 0;
    snd_shared_work = 0;
    card_cmd = 0;
    card_expect_address = 0;
    pm_first = 0;
    khdays_pxi_reset();
    ARM7_HANDLE_CHECKER = 0;
    *(volatile u16 *)KHDAYS_W(0x027fffa8) = khdays_input_xy(); /* X, Y, hinge */
    for (u32 tag = 0; tag < 32; ++tag) {
        if (services[tag] != NULL) {
            ARM7_HANDLE_CHECKER |= 1u << tag;
        }
    }
}

void khdays_arm7_receive(u32 word)
{
    const u32 tag = word & 0x1f;
    const u32 err = (word >> 5) & 1;
    const u32 data = word >> 6;
    pxi_note(0, tag, data, err);
    if (services[tag] == NULL) {
        /* the SDK sends only once the tag's service is ready (PXI_IsCallbackReady) */
        said_once(tag, data, "no ARM7 service for this tag: not answered");
        return;
    }
    services[tag](data, err);
}
