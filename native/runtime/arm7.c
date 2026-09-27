#include "arm7.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>

/* The FIFOs hold 16 words each way. */
#define FIFO_DEPTH 16

static u32 sync_nibble;
static u32 recv[FIFO_DEPTH];
static int recv_head, recv_count;

/* OSSystemWork.pxiHandleChecker[PXI_PROC_ARM7] (0x027ffc00 + 0x38c): one bit
 * per PXI tag the ARM7 has a receive callback for. The ARM9 polls it before
 * talking to a service. */
#define ARM7_HANDLE_CHECKER (*(volatile u32 *)0x027fff8c)

/* PXI tags (NitroSDK PXIFifoTag). */
enum {
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
    recv[(recv_head + recv_count) % FIFO_DEPTH] = tag | err << 5 | data << 6;
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

/* PM (tag 8): power management. */
static void pm_service(u32 data, u32 err)
{
    (void)err;
    unimplemented(TAG_PM, data, "power-management command");
}

static void (*const services[32])(u32 data, u32 err) = {
    [TAG_PM] = pm_service,
    [TAG_OS] = os_service,
    [TAG_CTRDG] = ctrdg_service,
};

void khdays_arm7_reset(void)
{
    sync_nibble = 0;
    recv_head = recv_count = 0;
    ARM7_HANDLE_CHECKER = 0;
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
    word = recv[recv_head];
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
