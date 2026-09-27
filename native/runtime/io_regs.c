/* What the ARM9's I/O registers do. A register nothing here handles is plain
 * storage: the game reads back what it wrote, which is right for the display
 * and control registers the renderer reads. A status register that should
 * change by itself and is not modelled keeps its last value, so a game that
 * polls it stalls -- visibly, with KHDAYS_STALL_SECONDS -- instead of
 * reading an invented value. */
#include "io.h"
#include "arm7.h"
#include "card.h"
#include "clock.h"
#include "events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void unimplemented(const char *what, u32 address)
{
    fprintf(stderr, "io: %s (0x%08x) is not modelled\n", what, address);
    fflush(stderr);
    exit(7);
}

static int overlaps(u32 offset, int size, u32 reg, u32 reg_size)
{
    return offset < reg + reg_size && reg < offset + (u32)size;
}

/* --- Interrupts ------------------------------------------------------------ */

void khdays_io_request_irq(u32 bits)
{
    IO32(0x214) |= bits;
}

/* --- Timers ---------------------------------------------------------------- */

static const u32 prescale[4] = {1, 64, 256, 1024};

typedef struct {
    u16 reload;
    u16 control;
    u64 start;     /* cycle the counter last held `reload`... */
    u16 stopped;   /* ...or the value it froze at */
} Timer;
static Timer timers[4];

static u32 timer_period(const Timer *t)
{
    return (0x10000u - t->reload) * prescale[t->control & 3];
}

static u16 timer_counter(const Timer *t, u64 now)
{
    if ((t->control & 0x80) == 0) {
        return t->stopped;
    }
    return (u16)(t->reload + (now - t->start) % timer_period(t) / prescale[t->control & 3]);
}

u64 khdays_timer_next_overflow(int n, u64 after)
{
    const Timer *t = &timers[n];
    u64 period;
    if ((t->control & 0xc0) != 0xc0) {  /* running with its IRQ enabled */
        return ~0ull;
    }
    period = timer_period(t);
    return t->start + ((after - t->start) / period + 1) * period;
}

static void timer_control_written(int n, u16 value, u64 now)
{
    Timer *t = &timers[n];
    const u16 old = t->control;
    if (value & 0x04) {
        unimplemented("timer count-up (cascade) mode", 0x04000102 + 4 * n);
    }
    if ((old & 0x80) && !(value & 0x80)) {
        t->stopped = timer_counter(t, now);
    }
    if (!(old & 0x80) && (value & 0x80)) {
        t->start = now;  /* starting reloads the counter */
    } else if ((old & 0x80) && (value & 0x80) && (old & 3) != (value & 3)) {
        unimplemented("changing a running timer's prescaler", 0x04000102 + 4 * n);
    }
    t->control = value & 0xc7;
}

/* Overflow IRQs since `last` up to `now` set the timer's IF bit. */
void khdays_timers_update(u64 last, u64 now)
{
    for (int n = 0; n < 4; ++n) {
        const u64 next = khdays_timer_next_overflow(n, last);
        if (next <= now) {
            khdays_io_request_irq(1u << (3 + n));
        }
    }
}

/* --- Divider and square root ---------------------------------------------- */

/* Result words (DIV_RESULT lo/hi, DIVREM_RESULT lo/hi: bits 0-3) whose value
 * in the current case GBATEK does not pin down. The game writes the operands
 * a word at a time, so such a state can be transient; only reading one of
 * these words in it is an error. */
static u32 division_unmodelled;

/* GBATEK, "DS Maths": the DIV0 flag (bit 14) tests the full 64-bit DENOM in
 * every mode. Dividing by zero leaves REMAIN = NUMER and RESULT = +1 or -1,
 * the sign opposite to NUMER's; in 32/32 mode the upper half of the
 * sign-expanded result is inverted. -80000000h / -1 in 32-bit mode gives
 * -MAX. */
static void divide(void)
{
    const u32 mode = IO16(0x280) & 3;
    const s64 numer64 = (s64)((u64)IO32(0x290) | (u64)IO32(0x294) << 32);
    const s64 denom64 = (s64)((u64)IO32(0x298) | (u64)IO32(0x29c) << 32);
    s64 numer, denom;
    u64 quotient, remainder;
    if (denom64 == 0) {
        IO16(0x280) |= 0x4000;
    } else {
        IO16(0x280) &= ~0x4000;
    }
    if (mode == 0) {
        numer = (s32)IO32(0x290);
        denom = (s32)IO32(0x298);
    } else if (mode == 2) {
        numer = numer64;
        denom = denom64;
    } else {
        numer = numer64;
        denom = (s32)IO32(0x298);
    }
    division_unmodelled = 0;
    if (denom == 0) {
        quotient = (u64)(numer < 0 ? 1 : -1);
        remainder = (u64)numer;
        if (mode == 0) {
            quotient ^= 0xffffffff00000000ull;
            division_unmodelled = 1u << 3;  /* the remainder's upper half */
        }
    } else if (mode == 0 && numer == -0x80000000ll && denom == -1) {
        quotient = 0x80000000u;  /* -MAX, as its low word */
        remainder = 0;
        division_unmodelled = (1u << 1) | (1u << 3);
    } else if (numer == (s64)0x8000000000000000ull && denom == -1) {
        quotient = remainder = 0;
        division_unmodelled = 0xf;  /* the 64-bit overflow is not documented */
    } else {
        quotient = (u64)(numer / denom);
        remainder = (u64)(numer % denom);
    }
    IO32(0x2a0) = (u32)quotient;
    IO32(0x2a4) = (u32)(quotient >> 32);
    IO32(0x2a8) = (u32)remainder;
    IO32(0x2ac) = (u32)(remainder >> 32);
}

static void square_root(void)
{
    u64 value = (u64)IO32(0x2b8);
    u64 root = 0, bit = 1ull << 62;
    if (IO16(0x2b0) & 1) {
        value |= (u64)IO32(0x2bc) << 32;
    }
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    IO32(0x2b4) = (u32)root;
}

/* --- DMA ------------------------------------------------------------------- */

static int is_io_address(u32 address)
{
    return (address >= KHDAYS_IO_BASE && address < KHDAYS_IO_BASE + KHDAYS_IO_SIZE) ||
           (address >= KHDAYS_IO2_BASE && address < KHDAYS_IO2_BASE + KHDAYS_IO2_SIZE);
}

static u8 *host_address(u32 address)
{
    if (address >= KHDAYS_IO2_BASE) {
        return khdays_io2_host + (address - KHDAYS_IO2_BASE);
    }
    return khdays_io_host + (address - KHDAYS_IO_BASE);
}

/* Where a DMA access lands. DMA is on the system bus, which does not see the
 * CPU's TCMs: below main RAM there is nothing (0), and at the DTCM's address
 * it reaches the main RAM mirror underneath (0x027e0000 -> 0x023e0000). */
static u32 dma_bus(u32 address)
{
    if (address < 0x02000000u) {
        return 0;
    }
    if (address >= 0x027e0000u && address < 0x027e4000u) {
        return address - 0x00400000u;
    }
    return address;
}

/* One DMA unit, with the register semantics when either side is I/O. */
static void dma_unit(u32 source, u32 destination, int size)
{
    u8 value[4];
    source = dma_bus(source);
    destination = dma_bus(destination);
    if (destination == 0) {
        return;  /* a write to nothing */
    }
    if (source == 0) {
        unimplemented("a DMA reading below main RAM into memory", destination);
    }
    if (is_io_address(source)) {
        khdays_io_read(source, size);
        memcpy(value, host_address(source), size);
    } else {
        memcpy(value, (const void *)source, size);
    }
    if (is_io_address(destination)) {
        u8 before[4];
        memcpy(before, host_address(destination), size);
        memcpy(host_address(destination), value, size);
        khdays_io_write(destination, size, before);
    } else {
        memcpy((void *)destination, value, size);
    }
}

/* Each channel's own address counters: loaded from SAD/DAD when the channel
 * is enabled, then advanced by its transfers (a repeating channel keeps
 * them between bursts, reloading DAD only in "increment/reload" mode). */
static struct {
    u32 source;
    u32 destination;
} dma[4];

/* One burst: the CNT word count of units. */
void khdays_dma_run(int channel)
{
    const u32 base = 0x0b0 + 12 * channel;
    const u32 control = IO32(base + 8);
    const int size = (control & (1u << 26)) ? 4 : 2;
    const u32 dst_mode = (control >> 21) & 3;
    const u32 src_mode = (control >> 23) & 3;
    u32 count = control & 0x1fffff;
    if (count == 0) {
        count = 0x200000;
    }
    for (u32 i = 0; i < count; ++i) {
        dma_unit(dma[channel].source & ~(u32)(size - 1),
                 dma[channel].destination & ~(u32)(size - 1), size);
        if (src_mode == 0) dma[channel].source += size;
        else if (src_mode == 1) dma[channel].source -= size;
        if (dst_mode == 0 || dst_mode == 3) dma[channel].destination += size;
        else if (dst_mode == 1) dma[channel].destination -= size;
    }
    if (control & (1u << 25)) {
        if (dst_mode == 3) {
            dma[channel].destination = IO32(base + 4);
        }
    } else {
        IO32(base + 8) = control & ~0x80000000u;
    }
    if (control & (1u << 30)) {
        khdays_io_request_irq(1u << (8 + channel));
    }
}

/* A channel armed for the card: a burst per data word the card has ready. */
int khdays_dma_card_channel(void)
{
    for (int channel = 0; channel < 4; ++channel) {
        const u32 control = IO32(0x0b8 + 12 * channel);
        if ((control & 0x80000000u) && ((control >> 27) & 7) == 5) {
            return channel;
        }
    }
    return -1;
}

static void dma_control_written(int channel, int was_enabled)
{
    const u32 control = IO32(0x0b8 + 12 * channel);
    if (!(control & 0x80000000u) || was_enabled) {
        return;
    }
    dma[channel].source = IO32(0x0b0 + 12 * channel);
    dma[channel].destination = IO32(0x0b4 + 12 * channel);
    switch ((control >> 27) & 7) {
    case 0:  /* immediately */
    case 7:  /* geometry FIFO: it drains as fast as it is fed natively */
        if (control & (1u << 25)) {
            unimplemented("a repeating DMA started immediately", 0x040000b8 + 12 * channel);
        }
        khdays_dma_run(channel);
        break;
    case 1:  /* at the next VBlank */
        khdays_events_dma_at_vblank(channel);
        break;
    case 5:  /* when the card has a word: card.c runs it */
        break;
    default:
        unimplemented("DMA start mode", 0x040000b8 + 12 * channel);
    }
}

/* --- Reset, reads and writes ----------------------------------------------- */

void khdays_io_reset(void)
{
    memset(khdays_io_host, 0, KHDAYS_IO_SIZE);
    memset(khdays_io2_host, 0, KHDAYS_IO2_SIZE);
    memset(timers, 0, sizeof(timers));
    IO16(0x130) = 0x03ff;  /* KEYINPUT: active low, nothing held */
    IO16(0x184) = 0x0101;  /* IPCFIFOCNT: both FIFOs empty */
    IO8(0x300) = 0x01;     /* POSTFLG: the firmware finished booting */
    khdays_arm7_reset();
}

void khdays_io_read(u32 address, int size)
{
    u32 offset;
    const u64 now = khdays_clock_cycles();
    if (address >= KHDAYS_IO2_BASE) {
        offset = address - KHDAYS_IO2_BASE;
        if (overlaps(offset, size, 0x000, 4)) {  /* IPCFIFORECV */
            if (khdays_arm7_recv_count() > 0) {
                *(u32 *)khdays_io2_host = khdays_arm7_recv_pop();
            }
        }
        if (overlaps(offset, size, 0x010, 4)) {  /* the card's data port */
            *(u32 *)(khdays_io2_host + 0x010) = khdays_card_read_word();
        }
        return;
    }
    offset = address - KHDAYS_IO_BASE;
    khdays_events_update();
    if (overlaps(offset, size, 0x004, 4)) {
        const u32 line = khdays_clock_line();
        const u16 dispstat = IO16(0x004);
        const u32 setting = (dispstat >> 8) | ((dispstat & 0x80) << 1);
        IO16(0x004) = (u16)((dispstat & ~7u) |
                            (line >= KHDAYS_VBLANK_LINE && line < KHDAYS_LINES_PER_FRAME - 1) |
                            (line == setting ? 4 : 0));
        IO16(0x006) = (u16)line;
    }
    for (int n = 0; n < 4; ++n) {
        if (overlaps(offset, size, 0x100 + 4 * n, 2)) {
            IO16(0x100 + 4 * n) = timer_counter(&timers[n], now);
        }
    }
    for (int word = 0; word < 4; ++word) {
        if ((division_unmodelled & (1u << word)) && overlaps(offset, size, 0x2a0 + 4 * word, 4)) {
            unimplemented("a division result word GBATEK leaves undefined", address);
        }
    }
    if (overlaps(offset, size, 0x180, 2)) {
        IO16(0x180) = (u16)((IO16(0x180) & ~0xfu) | khdays_arm7_sync_nibble());
    }
    if (overlaps(offset, size, 0x184, 2)) {
        const int recv = khdays_arm7_recv_count();
        const int send = khdays_arm7_send_count();
        IO16(0x184) = (u16)((IO16(0x184) & 0x8404) | (send == 0 ? 0x0001 : 0) |
                            (send == 16 ? 0x0002 : 0) | (recv == 0 ? 0x0100 : 0) |
                            (recv == 16 ? 0x0200 : 0));
    }
}

void khdays_io_write(u32 address, int size, const u8 *before)
{
    u32 offset;
    const u64 now = khdays_clock_cycles();
    if (address >= KHDAYS_IO2_BASE) {
        unimplemented("write to the IPC/card port region", address);
    }
    offset = address - KHDAYS_IO_BASE;
#define BEFORE8(reg) (before[(reg) - offset])
    if (overlaps(offset, size, 0x004, 2)) {  /* DISPSTAT: bits 0-2 are status */
        const u32 lo = offset <= 0x004 ? BEFORE8(0x004) : IO8(0x004);
        IO8(0x004) = (u8)((IO8(0x004) & ~7u) | (lo & 7u));
    }
    if (overlaps(offset, size, 0x006, 2)) {
        unimplemented("writing VCOUNT", 0x04000006);
    }
    for (int n = 0; n < 4; ++n) {
        const u32 high = 0x0b8 + 12 * n + 3;  /* the byte holding the enable bit */
        if (overlaps(offset, size, 0x0b8 + 12 * n + 2, 2)) {
            const int was_enabled =
                overlaps(offset, size, high, 1) ? (BEFORE8(high) & 0x80) != 0 : 1;
            dma_control_written(n, was_enabled);
        }
    }
    for (int n = 0; n < 4; ++n) {
        const u32 reg = 0x100 + 4 * n;
        if (overlaps(offset, size, reg, 2)) {
            timers[n].reload = IO16(reg);
        }
        if (overlaps(offset, size, reg + 2, 2)) {
            timer_control_written(n, IO16(reg + 2), now);
        }
    }
    if (overlaps(offset, size, 0x180, 2)) {  /* IPCSYNC */
        const u16 value = IO16(0x180);
        if (value & 0x2000) {
            unimplemented("IPCSYNC IRQ to the ARM7", 0x04000180);
        }
        IO16(0x180) = (u16)((value & 0x4f00) | khdays_arm7_sync_nibble());
        khdays_arm7_sync_written((value >> 8) & 0xf);
    }
    if (overlaps(offset, size, 0x184, 2)) {  /* IPCFIFOCNT */
        const u16 value = IO16(0x184);
        if (value & 0x0008) {
            khdays_arm7_send_clear();
        }
        IO16(0x184) = (u16)(value & 0x8404);
        khdays_io_read(KHDAYS_IO_BASE + 0x184, 2);  /* status bits */
    }
    if (overlaps(offset, size, 0x188, 4)) {  /* IPCFIFOSEND */
        if (IO16(0x184) & 0x8000) {
            khdays_arm7_receive(IO32(0x188));
        }
    }
    for (u32 reg = 0x214; reg < 0x218; ++reg) {  /* IF: writing 1 clears */
        if (overlaps(offset, size, reg, 1)) {
            IO8(reg) = (u8)(BEFORE8(reg) & ~IO8(reg));
        }
    }
    if (overlaps(offset, size, 0x1a2, 2)) {
        unimplemented("the card's SPI (backup) port", 0x040001a2);
    }
    if (overlaps(offset, size, 0x1a7, 1) && (IO32(0x1a4) & 0x80000000u)) {
        khdays_card_start();  /* ROMCTRL written with its start bit */
    }
    if (overlaps(offset, size, 0x280, 0x20)) {
        divide();
    }
    if (overlaps(offset, size, 0x2b0, 2) || overlaps(offset, size, 0x2b8, 8)) {
        square_root();
    }
#undef BEFORE8
}
