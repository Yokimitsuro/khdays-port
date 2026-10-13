/* The game card on its bus (Slot-1): ROMCTRL (0x040001a4), the 8-byte
 * command (0x040001a8), and the data port (0x04100010). The card is in main
 * data mode, as the firmware leaves it, and answers from the rebuilt ROM
 * (rom.cpp). KEY2 scrambling is done by the hardware on both ends of the bus,
 * so the CPU sees plain commands and data; the seeds it writes are ignored.
 *
 * A transfer completes as soon as its data is read: a card DMA channel (start
 * mode 5) is run once per word, as the hardware triggers it; otherwise the
 * CPU pops the words itself. */
#include "card.h"
#include "io.h"
#include "rom.h"

#include <stdio.h>
#include <stdlib.h>

extern int khdays_dma_card_channel(void);
extern void khdays_dma_run(int channel);

static u8 buffer[0x4000];
static u32 length;
static u32 position;

static void unimplemented(const char *what, u32 value)
{
    fprintf(stderr, "card: %s (0x%08x) is not modelled\n", what, value);
    fflush(stderr);
    exit(9);
}

static void complete(void)
{
    IO32(0x1a4) &= ~(0x80000000u | 0x00800000u);  /* not busy, no word ready */
    if (IO16(0x1a0) & 0x4000) {                    /* AUXSPICNT: IRQ on completion */
        khdays_io_request_irq(1u << 19);
    }
}

void khdays_card_start(void)
{
    const u32 control = IO32(0x1a4);
    const u32 block = (control >> 24) & 7;
    const u8 command = IO8(0x1a8);
    length = block == 0 ? 0 : block == 7 ? 4 : 0x100u << block;
    position = 0;
    if (control & 0x40000000u) {
        unimplemented("a write to the card", control);
    }
    switch (command) {
    case 0xb7: {  /* read: address in bytes 1-4, big-endian */
        u32 address = (u32)IO8(0x1a9) << 24 | (u32)IO8(0x1aa) << 16 |
                      (u32)IO8(0x1ab) << 8 | IO8(0x1ac);
        if (address < 0x8000) {
            /* the secure area cannot be read in main data mode */
            address = 0x8000 + (address & 0x1ff);
        }
        /* a read stays inside its 4 KB page, wrapping at its end; taken in
         * runs up to the wrap (a byte at a time costs a file seek each) */
        for (u32 done = 0; done < length;) {
            const u32 at = (address + done) & 0xfffu;
            u32 run = 0x1000u - at;
            if (run > length - done) {
                run = length - done;
            }
            khdays_rom_read((address & ~0xfffu) | at, &buffer[done], run);
            done += run;
        }
        break;
    }
    case 0xb8:  /* chip ID: the value the firmware recorded at boot (boot.c) */
        for (u32 i = 0; i < length; ++i) {
            buffer[i] = (u8)(KHDAYS_CARD_CHIP_ID >> (8 * (i & 3)));
        }
        break;
    default:
        unimplemented("card command", command);
    }
    if (length == 0) {
        complete();
        return;
    }
    IO32(0x1a4) |= 0x00800000u;  /* a word is ready */
    for (;;) {
        const int channel = khdays_dma_card_channel();
        if (channel < 0 || position >= length) {
            break;
        }
        khdays_dma_run(channel);
    }
}

/* CARDi_ReadRomSyncCore's transfer done in one copy (prepare.py HOOKS).
 * The SDK moves it a word at a time through ROMCTRL and the data port, each
 * access a trapped register access here, while the music streams and every
 * load read the card that way: the game thread spent most of its time on it.
 * The request is CARDiCommon's src/dst/len (data_020464e0 +0x1c/+0x20/+0x24);
 * afterwards they stand where the SDK's loop leaves them. */
#define CARD_COMMON 0x020464e0u
#define CARD_COMMON_SRC 0x1c
#define CARD_COMMON_DST 0x20
#define CARD_COMMON_LEN 0x24

void khdays_card_read_direct(void)
{
    u32 *const src = (u32 *)(size_t)(CARD_COMMON + CARD_COMMON_SRC);
    u32 *const dst = (u32 *)(size_t)(CARD_COMMON + CARD_COMMON_DST);
    u32 *const len = (u32 *)(size_t)(CARD_COMMON + CARD_COMMON_LEN);
    if (*len != 0) {
        khdays_rom_read(*src, (u8 *)(size_t)*dst, *len);
        *src += *len;
        *dst += *len;
        *len = 0;
    }
}

/* The CPU (or a DMA) reads the data port: the next word of the transfer. */
u32 khdays_card_read_word(void)
{
    u32 word;
    if (position >= length) {
        unimplemented("reading the card data port with no transfer", position);
    }
    word = buffer[position] | buffer[position + 1] << 8 | buffer[position + 2] << 16 |
           (u32)buffer[position + 3] << 24;
    position += 4;
    if (position >= length) {
        complete();
    }
    return word;
}
