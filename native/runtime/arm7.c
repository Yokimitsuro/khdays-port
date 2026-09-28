#include "arm7.h"
#include "input.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* The FIFOs hold 16 words each way. */
#define FIFO_DEPTH 16

static u32 sync_nibble;
static u32 recv_fifo[FIFO_DEPTH];
static int recv_head, recv_count;

/* OSSystemWork.pxiHandleChecker[PXI_PROC_ARM7] (0x027ffc00 + 0x38c): one bit
 * per PXI tag the ARM7 has a receive callback for. The ARM9 polls it before
 * talking to a service. */
#define ARM7_HANDLE_CHECKER (*(volatile u32 *)0x027fff8c)

/* PXI tags (NitroSDK PXIFifoTag). */
enum {
    TAG_RTC = 5,
    TAG_TP = 6,
    TAG_SOUND = 7,
    TAG_PM = 8,
    TAG_OS = 12,
    TAG_CTRDG = 13,
};

static void unimplemented(u32 tag, u32 data, const char *what)
{
    fprintf(stderr, "arm7: tag %u, data 0x%07x: %s\n", tag, data, what);
    fflush(stderr);
    exit(6);
}

/* A word to the ARM9: tag in bits 0-4, error in bit 5, data in bits 6-31. */
static void reply(u32 tag, u32 data, u32 err)
{
    if (recv_count == FIFO_DEPTH) {
        unimplemented(tag, data, "receive FIFO overflow");
    }
    recv_fifo[(recv_head + recv_count) % FIFO_DEPTH] = tag | err << 5 | data << 6;
    ++recv_count;
    /* IPCFIFOCNT bit 10: IRQ 18 while the receive FIFO is not empty. */
    if (IO16(0x184) & 0x0400) {
        khdays_io_request_irq(1u << 18);
    }
}

/* --- Services ---------------------------------------------------------------
 * Each answers what the ARM9 side (decompiled) sends and waits for. */

/* OS (tag 12): the ARM9 installs a callback at boot (func_02003894) that takes
 * the ARM7's reset notices; it sends here only to reset the system. */
static void os_service(u32 data, u32 err)
{
    (void)err;
    unimplemented(TAG_OS, data, "OS message (system reset) not implemented");
}

/* CTRDG (tag 13): CTRDG_Init sends INIT_MODULE_INFO (command 1, the address
 * of its header buffer as parameter) and waits for the callback
 * (func_0200f980) to see command 1 come back. With the GBA slot empty the
 * ARM7 has nothing to record. */
static void ctrdg_service(u32 data, u32 err)
{
    (void)err;
    if ((data & 0x3f) == 1) {
        reply(TAG_CTRDG, 1, 0);
        return;
    }
    unimplemented(TAG_CTRDG, data, "cartridge command");
}

/* RTC (tag 5): the ARM9 sends a command in bits 8-14 and takes the reply
 * (command << 8 | result, 0 = success) in func_0200d720; data travels through
 * OSSystemWork.real_time_clock (0x027ffde8). The game issues only two
 * commands: 0x12, read the time (RTCRawTime at +4: BCD hour in bits 0-5,
 * minute in 8-14, second in 16-22), and 0x27, write status 2 (the alarm and
 * interrupt mode, which nothing here raises). The DS's clock is the PC's. */
#define RTC_BUF ((volatile u32 *)0x027ffde8)

static u32 bcd(u32 value)
{
    return (value / 10) << 4 | (value % 10);
}

static void rtc_service(u32 data, u32 err)
{
    const u32 command = (data >> 8) & 0x7f;
    (void)err;
    if (command == 0x12) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        RTC_BUF[1] = bcd(now.wHour) | bcd(now.wMinute) << 8 | bcd(now.wSecond) << 16;
        reply(TAG_RTC, command << 8, 0);
        return;
    }
    if (command == 0x27) {
        reply(TAG_RTC, command << 8, 0);
        return;
    }
    unimplemented(TAG_RTC, data, "RTC command");
}

/* Touch panel (tag 6), NitroSDK's SPI protocol as the ARM9 side speaks it
 * (libs/nitro/spi): a request is one or more words, the first with bit 25
 * (start), the last with bit 24 (end), the command in bits 8-14 of the first
 * and the word's index in bits 16-23 of the rest. The reply is end | command
 * << 8 | result; a sample travels through 0x027fffaa (x in bits 0-11, y in
 * 12-23 as 12-bit ADC values, touched in 24, validity in 25-26). Commands:
 * 0 sample once, 1 sample automatically (frequency per frame, at a line),
 * 2 stop that, 3 set the stability range. The position is the host's, in
 * the calibration boot.c writes (ADC = pixel * 16). */
#define TP_BUF ((volatile u16 *)0x027fffaa)

static u32 tp_words[4];
static int tp_count;
static u32 tp_auto_frequency;  /* 0 = off */

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
        return;  /* more words to come */
    }
    command = (tp_words[0] >> 8) & 0x7f;
    switch (command) {
    case 0:
        tp_sample();
        break;
    case 1:
        tp_auto_frequency = tp_words[0] & 0xff;
        break;
    case 2:
        tp_auto_frequency = 0;
        break;
    case 3:
        break;
    default:
        unimplemented(TAG_TP, data, "touch panel command");
    }
    reply(TAG_TP, 0x1000000 | command << 8, 0);
}

/* Automatic sampling: `frequency` samples a frame, each announced with
 * command 0x10. */
void khdays_arm7_frame(void)
{
    *(volatile u16 *)0x027fffa8 = khdays_input_xy();
    for (u32 i = 0; i < tp_auto_frequency; ++i) {
        tp_sample();
        reply(TAG_TP, 0x10u << 8, 0);
    }
}

/* Sound (tag 7), NitroSDK's snd_command.c as the ARM9 side speaks it: a word
 * is the address of a list of commands (SNDCommand: next, id, arg[4]) or 0,
 * a request to process what was sent. The ARM7's sound driver runs each list
 * and then advances the finished-command tag, the first word of the shared
 * work that command 0x1d (SHARED_WORK) names; the ARM9 waits on that tag.
 * No sound is made yet: each list is taken as run, and each command id seen
 * is reported once, so the driver is written against what the game sends. */
static u32 snd_shared_work;
static u32 snd_ids_seen[8];  /* a bit per command id */

static void sound_service(u32 data, u32 err)
{
    (void)err;
    if (data == 0) {
        return;  /* everything sent is already run */
    }
    for (u32 command = data; command != 0; command = *(volatile u32 *)command) {
        const u32 id = *(volatile u32 *)(command + 4);
        if (id == 0x1d) {
            snd_shared_work = *(volatile u32 *)(command + 8);
        }
        if (id < 256 && !(snd_ids_seen[id / 32] & (1u << (id % 32)))) {
            snd_ids_seen[id / 32] |= 1u << (id % 32);
            fprintf(stderr, "arm7: sound command 0x%02x (not played yet)\n", id);
        }
    }
    if (snd_shared_work != 0) {
        ++*(volatile u32 *)snd_shared_work;
    }
}

/* Backup memory (tag 11, as CARDi_Request speaks it): a request is a word
 * with the error bit set -- the request type -- and INIT is followed by the
 * address of the shared CARDiCommandArg (result +0x00, type +0x04, src +0x0c,
 * dst +0x10, len +0x14). The ARM7 runs it on the chip and answers on the same
 * tag, error bit set; CARDi_OnFifoRecv wakes the requester, which reads
 * `result`. Types: 0 INIT, 2 IDENTIFY (the backup type in `type`: device in
 * bits 0-7, log2 of the size in 8-15), 6 read (chip src -> RAM dst), 7 write
 * (RAM src -> chip dst), 8 program, 9 verify, 10-12 and 15 erase. The chip is
 * saves/khdays.sav, written through on every change. */
#define TAG_FS 11
#define CARD_CMD(offset) (*(volatile u32 *)(card_cmd + (offset)))

static u32 card_cmd;
static int card_expect_address;
static u8 *backup;
static u32 backup_size;
static u32 backup_device;

static void backup_save(u32 offset, u32 length)
{
    HANDLE file;
    DWORD done;
    CreateDirectoryA("saves", NULL);
    file = CreateFileA("saves\\khdays.sav", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        unimplemented(TAG_FS, 0, "cannot write saves/khdays.sav");
    }
    SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN);
    WriteFile(file, backup + offset, length, &done, NULL);
    CloseHandle(file);
}

static void backup_identify(u32 type)
{
    HANDLE file;
    DWORD done = 0;
    backup_device = type & 0xff;
    backup_size = 1u << ((type >> 8) & 0xff);
    free(backup);
    backup = (u8 *)malloc(backup_size);
    /* never written: the chip as it leaves the factory, all ones */
    for (u32 i = 0; i < backup_size; ++i) {
        backup[i] = 0xff;
    }
    file = CreateFileA("saves\\khdays.sav", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        ReadFile(file, backup, backup_size, &done, NULL);
        CloseHandle(file);
    }
}

static int backup_range(u32 offset, u32 length)
{
    return backup != NULL && offset <= backup_size && length <= backup_size - offset;
}

static void card_service(u32 data, u32 err)
{
    u32 result = 0;  /* CARD_RESULT_SUCCESS */
    if (!err) {
        unimplemented(TAG_FS, data, "a backup word without the request flag");
    }
    if (card_expect_address) {
        card_cmd = data;
        card_expect_address = 0;
        reply(TAG_FS, 0, 1);
        return;
    }
    switch (data) {
    case 0:  /* INIT: the command block's address comes next */
        card_expect_address = 1;
        return;
    case 2:
        backup_identify(CARD_CMD(0x04));
        break;
    case 6: {
        const u32 src = CARD_CMD(0x0c), dst = CARD_CMD(0x10), len = CARD_CMD(0x14);
        if (!backup_range(src, len)) {
            unimplemented(TAG_FS, data, "a backup read outside the chip");
        }
        for (u32 i = 0; i < len; ++i) {
            *(volatile u8 *)(dst + i) = backup[src + i];
        }
        break;
    }
    case 7:
    case 8: {
        const u32 src = CARD_CMD(0x0c), dst = CARD_CMD(0x10), len = CARD_CMD(0x14);
        if (!backup_range(dst, len)) {
            unimplemented(TAG_FS, data, "a backup write outside the chip");
        }
        for (u32 i = 0; i < len; ++i) {
            const u8 value = *(volatile u8 *)(src + i);
            /* flash programs by clearing bits; an EEPROM just stores */
            backup[dst + i] = (data == 8 && backup_device == 2) ? (u8)(backup[dst + i] & value) : value;
        }
        backup_save(dst, len);
        break;
    }
    case 9: {
        const u32 src = CARD_CMD(0x0c), dst = CARD_CMD(0x10), len = CARD_CMD(0x14);
        if (!backup_range(dst, len)) {
            unimplemented(TAG_FS, data, "a backup verify outside the chip");
        }
        for (u32 i = 0; i < len; ++i) {
            if (backup[dst + i] != *(volatile u8 *)(src + i)) {
                result = 1;  /* CARD_RESULT_FAILURE */
                break;
            }
        }
        break;
    }
    default:
        unimplemented(TAG_FS, data, "backup request");
    }
    CARD_CMD(0x00) = result;
    reply(TAG_FS, data, 1);
}

/* PM (tag 8): power management. */
static void pm_service(u32 data, u32 err)
{
    (void)err;
    unimplemented(TAG_PM, data, "power-management command");
}

static void (*const services[32])(u32 data, u32 err) = {
    [TAG_RTC] = rtc_service,
    [TAG_TP] = tp_service,
    [TAG_SOUND] = sound_service,
    [TAG_PM] = pm_service,
    [TAG_FS] = card_service,
    [TAG_OS] = os_service,
    [TAG_CTRDG] = ctrdg_service,
};

void khdays_arm7_reset(void)
{
    sync_nibble = 0;
    recv_head = recv_count = 0;
    ARM7_HANDLE_CHECKER = 0;
    *(volatile u16 *)0x027fffa8 = khdays_input_xy();  /* X, Y, hinge */
    for (u32 tag = 0; tag < 32; ++tag) {
        if (services[tag] != NULL) {
            ARM7_HANDLE_CHECKER |= 1u << tag;
        }
    }
}

/* PXI_InitFifo's handshake: the ARM9 echoes the ARM7's nibble back on its own
 * output and waits for the ARM7 to change it, until it has seen five changes
 * in a row and reads 0. The ARM7 answers each echo with the next value. */
void khdays_arm7_sync_written(u32 arm9_nibble)
{
    sync_nibble = (arm9_nibble + 1) & 0xf;
}

u32 khdays_arm7_sync_nibble(void)
{
    return sync_nibble;
}

void khdays_arm7_receive(u32 word)
{
    const u32 tag = word & 0x1f;
    const u32 err = (word >> 5) & 1;
    const u32 data = word >> 6;
    if (services[tag] == NULL) {
        unimplemented(tag, data, "no ARM7 service for this tag");
    }
    services[tag](data, err);
}

int khdays_arm7_recv_count(void)
{
    return recv_count;
}

u32 khdays_arm7_recv_pop(void)
{
    u32 word;
    if (recv_count == 0) {
        /* Reading an empty FIFO; the SDK checks the empty bit first. */
        unimplemented(0, 0, "the ARM9 read an empty receive FIFO");
    }
    word = recv_fifo[recv_head];
    recv_head = (recv_head + 1) % FIFO_DEPTH;
    --recv_count;
    return word;
}

/* The ARM7 takes every word as it is sent, so the send FIFO is always empty. */
int khdays_arm7_send_count(void)
{
    return 0;
}

void khdays_arm7_send_clear(void)
{
}
