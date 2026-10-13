/* NitroSDK GX/CP routines the decompilation keeps as ARM assembly
 * (libs/nitro/gx/asm_stubs), translated store for store. Their targets are
 * hardware registers (the geometry FIFO, the divide/sqrt unit); the memory
 * map gives those addresses their hardware side effects. */
#include "hal.h"
#include "../runtime/io.h"

/* A word to a FIFO port: to the register model directly (runtime/io.h),
 * without the trap's fault per word. */
static void store(volatile u32 *d, u32 value)
{
    if (khdays_io_is_register((u32)d)) {
        khdays_io_store((u32)d, 4, value);
    } else {
        *d = value;
    }
}

/* 32 x `stmia r0, {r1-r3, r12}` of zero: the same four words, 32 times. */
void GXi_NopClearFifo128_(void *pDest)
{
    volatile u32 *d = (volatile u32 *)pDest;
    for (int i = 0; i < 32; ++i) {
        store(&d[0], 0);
        store(&d[1], 0);
        store(&d[2], 0);
        store(&d[3], 0);
    }
}

/* Four `ldmia r0!, {r2, r3, ip}` / `stmia r1, {r2, r3, ip}`: the source
 * advances, the destination (a FIFO port and its mirrors) does not. */
void GX_SendFifo48B(const void *src, void *dst)
{
    const volatile u32 *s = (const volatile u32 *)src;
    volatile u32 *d = (volatile u32 *)dst;
    for (int i = 0; i < 4; ++i) {
        store(&d[0], s[0]);
        store(&d[1], s[1]);
        store(&d[2], s[2]);
        s += 3;
    }
}

/* The divide/sqrt unit's registers, through the register model (runtime/
 * io.h): the same reads and writes as the assembly's loads and stores,
 * without a fault each -- the context is saved and restored on every
 * thread switch. */
#define DIVCNT     0x04000280u
#define DIV_NUMER  0x04000290u  /* numer lo/hi, denom lo/hi */
#define SQRTCNT    0x040002b0u
#define SQRT_PARAM 0x040002b8u  /* param lo/hi */

/* Context: +0x00 DIV_NUMER/DIV_DENOM (4 words), +0x10 SQRT_PARAM (2 words),
 * +0x18 DIVCNT & 3, +0x1a SQRTCNT & 1. */
void CP_SaveContext(void *pContext)
{
    u32 *c = (u32 *)pContext;
    c[0] = khdays_io_load(DIV_NUMER, 4);
    c[1] = khdays_io_load(DIV_NUMER + 4, 4);
    c[2] = khdays_io_load(DIV_NUMER + 8, 4);
    c[3] = khdays_io_load(DIV_NUMER + 12, 4);
    {
        const u16 divcnt = (u16)khdays_io_load(DIVCNT, 2);
        c[4] = khdays_io_load(SQRT_PARAM, 4);
        c[5] = khdays_io_load(SQRT_PARAM + 4, 4);
        ((u16 *)pContext)[0x18 / 2] = (u16)(divcnt & 3);
        ((u16 *)pContext)[0x1a / 2] = (u16)(khdays_io_load(SQRTCNT, 2) & 1);
    }
}

/* Same order as the assembly: operands, then the two control registers, then
 * the square-root parameter. */
void CPi_RestoreContext(const void *pContext)
{
    const u32 *c = (const u32 *)pContext;
    khdays_io_store(DIV_NUMER, 4, c[0]);
    khdays_io_store(DIV_NUMER + 4, 4, c[1]);
    khdays_io_store(DIV_NUMER + 8, 4, c[2]);
    khdays_io_store(DIV_NUMER + 12, 4, c[3]);
    khdays_io_store(DIVCNT, 2, ((const u16 *)pContext)[0x18 / 2]);
    khdays_io_store(SQRTCNT, 2, ((const u16 *)pContext)[0x1a / 2]);
    khdays_io_store(SQRT_PARAM, 4, c[4]);
    khdays_io_store(SQRT_PARAM + 4, 4, c[5]);
}
