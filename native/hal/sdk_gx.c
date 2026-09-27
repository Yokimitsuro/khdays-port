/* NitroSDK GX/CP routines the decompilation keeps as ARM assembly
 * (libs/nitro/gx/asm_stubs), translated store for store. Their targets are
 * hardware registers (the geometry FIFO, the divide/sqrt unit); the memory
 * map gives those addresses their hardware side effects. */
#include "hal.h"

/* 32 x `stmia r0, {r1-r3, r12}` of zero: the same four words, 32 times. */
void GXi_NopClearFifo128_(void *pDest)
{
    volatile u32 *d = (volatile u32 *)pDest;
    for (int i = 0; i < 32; ++i) {
        d[0] = 0;
        d[1] = 0;
        d[2] = 0;
        d[3] = 0;
    }
}

/* Four `ldmia r0!, {r2, r3, ip}` / `stmia r1, {r2, r3, ip}`: the source
 * advances, the destination (a FIFO port and its mirrors) does not. */
void GX_SendFifo48B(const void *src, void *dst)
{
    const volatile u32 *s = (const volatile u32 *)src;
    volatile u32 *d = (volatile u32 *)dst;
    for (int i = 0; i < 4; ++i) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        s += 3;
    }
}

#define REG_DIVCNT      (*(volatile u16 *)0x04000280)
#define REG_DIV_NUMER   ((volatile u32 *)0x04000290)  /* numer lo/hi, denom lo/hi */
#define REG_SQRTCNT     (*(volatile u16 *)0x040002b0)
#define REG_SQRT_PARAM  ((volatile u32 *)0x040002b8)  /* param lo/hi */

/* Context: +0x00 DIV_NUMER/DIV_DENOM (4 words), +0x10 SQRT_PARAM (2 words),
 * +0x18 DIVCNT & 3, +0x1a SQRTCNT & 1. */
void CP_SaveContext(void *pContext)
{
    u32 *c = (u32 *)pContext;
    c[0] = REG_DIV_NUMER[0];
    c[1] = REG_DIV_NUMER[1];
    c[2] = REG_DIV_NUMER[2];
    c[3] = REG_DIV_NUMER[3];
    {
        const u16 divcnt = REG_DIVCNT;
        c[4] = REG_SQRT_PARAM[0];
        c[5] = REG_SQRT_PARAM[1];
        ((u16 *)pContext)[0x18 / 2] = (u16)(divcnt & 3);
        ((u16 *)pContext)[0x1a / 2] = (u16)(REG_SQRTCNT & 1);
    }
}

/* Same order as the assembly: operands, then the two control registers, then
 * the square-root parameter. */
void CPi_RestoreContext(const void *pContext)
{
    const u32 *c = (const u32 *)pContext;
    REG_DIV_NUMER[0] = c[0];
    REG_DIV_NUMER[1] = c[1];
    REG_DIV_NUMER[2] = c[2];
    REG_DIV_NUMER[3] = c[3];
    REG_DIVCNT = ((const u16 *)pContext)[0x18 / 2];
    REG_SQRTCNT = ((const u16 *)pContext)[0x1a / 2];
    REG_SQRT_PARAM[0] = c[4];
    REG_SQRT_PARAM[1] = c[5];
}
