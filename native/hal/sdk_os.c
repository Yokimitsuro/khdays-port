/* NitroSDK OS routines the decompilation keeps as ARM assembly
 * (libs/nitro/os/asm_stubs, src/calls/DC_FlushRange.c). What they do to
 * hardware (CPSR, caches, the protection unit) has no native counterpart; what
 * they do to memory is kept exactly. */
#include "hal.h"

/* --- CPSR interrupt bits --------------------------------------------------
 * The emulated CPSR (hal.h): interrupts reach the game only where the runtime
 * delivers them, so what matters is the I/F state callers save and restore
 * (OSIntrMode = the CPSR I bit, 0x80; F is 0x40) and that thread switches
 * carry it (OS_SaveContext / OS_LoadContext). crt0 leaves the CPU in system
 * mode with both enabled (`msr cpsr_csfx, #0x1f`). */
u32 khdays_cpsr = 0x1f;

u32 OS_DisableInterrupts(void)
{
    const u32 old = khdays_cpsr & 0x80;
    khdays_cpsr |= 0x80;
    return old;
}

/* Lifting the I bit lets a pending IRQ in at once, as on the CPU. */
static void unmasked(u32 old)
{
    if (old != 0 && (khdays_cpsr & 0x80) == 0) {
        khdays_irq_poll();
    }
}

u32 OS_EnableInterrupts(void)
{
    const u32 old = khdays_cpsr & 0x80;
    khdays_cpsr &= ~0x80u;
    unmasked(old);
    return old;
}

u32 OS_RestoreInterrupts(u32 state)
{
    const u32 old = khdays_cpsr & 0x80;
    khdays_cpsr = (khdays_cpsr & ~0x80u) | (state & 0x80);
    unmasked(old);
    return old;
}

u32 OS_DisableInterrupts_IrqAndFiq(void)
{
    const u32 old = khdays_cpsr & 0xc0;
    khdays_cpsr |= 0xc0;
    return old;
}

u32 OS_RestoreInterrupts_IrqAndFiq(u32 state)
{
    const u32 old = khdays_cpsr & 0xc0;
    khdays_cpsr = (khdays_cpsr & ~0xc0u) | (state & 0xc0);
    unmasked(old & 0x80);
    return old;
}

u32 OS_GetCpsrIrq(void)
{
    return khdays_cpsr & 0x80;
}

u32 OS_GetProcMode(void)
{
    return khdays_cpsr & 0x1f;
}

/* --- Caches, write buffer, protection unit: no native counterpart --------- */
void DC_InvalidateAll(void) {}
void DC_StoreAll(void) {}
void DC_FlushAll(void) {}
void DC_InvalidateRange(void *start, u32 size) { (void)start; (void)size; }
void DC_StoreRange(const void *start, u32 size) { (void)start; (void)size; }
void DC_FlushRange(const void *start, u32 size) { (void)start; (void)size; }
void DC_WaitWriteBufferEmpty(void) {}
void IC_InvalidateAll(void) {}
void IC_InvalidateRange(void *start, u32 size) { (void)start; (void)size; }
void OS_EnableProtectionUnit(void) {}
void OS_DisableProtectionUnit(void) {}
void OS_SetProtectionRegion1(u32 param) { (void)param; }
void OS_SetProtectionRegion2(u32 param) { (void)param; }
void OS_SetDPermissionsForProtectionRegion(u32 set, u32 flags) { (void)set; (void)flags; }
void OS_UnLockCartridge(u16 lockId) { (void)lockId; }

/* The DTCM sits where the process maps it. */
u32 OS_GetDTCMAddress(void)
{
    return 0x027e0000;
}

/* --- Lock ids: the two shared flag words at 0x027fffb0 -------------------
 * A set bit is a free id; the highest free bit is taken (CLZ). Ids 0x40..0x5f
 * come from the first word, 0x60.. from the second; 0xfffffffd
 * (OS_LOCK_ID_ERROR) when both are full. */
static u32 count_leading_zeros(u32 x)
{
    u32 n = 0;
    if (x == 0) {
        return 32;
    }
    while ((x & 0x80000000u) == 0) {
        x <<= 1;
        ++n;
    }
    return n;
}

s32 OS_GetLockID(void)
{
    volatile u32 *flags = (volatile u32 *)0x027fffb0;
    u32 base = 0x40;
    u32 zeros = count_leading_zeros(flags[0]);
    if (zeros == 32) {
        ++flags;
        zeros = count_leading_zeros(flags[0]);
        if (zeros == 32) {
            return (s32)0xfffffffd;
        }
        base = 0x60;
    }
    *flags &= ~(0x80000000u >> zeros);
    return (s32)(base + zeros);
}

void OS_ReleaseLockID(s32 id)
{
    volatile u32 *flags = (volatile u32 *)0x027fffb0;
    if (id >= 0x60) {
        ++flags;
        id -= 0x60;
    } else {
        id -= 0x40;
    }
    *flags |= 0x80000000u >> id;
}

s32 OsCountZeroBits(u32 value)
{
    return (s32)count_leading_zeros(value);
}

/* OS_SpinWait: burns cycles; nothing to wait for natively. */
void OS_SpinWait(u32 cycles)
{
    (void)cycles;
}

/* OS_Halt: CP15 wait-for-interrupt. */
void OS_Halt(void)
{
    khdays_runtime_wait();
}

/* OSi_CancelDma0 (its assembly): with IME off, wait for VCOUNT to read 0 --
 * the first line of a frame -- then restore IME. The wait is the runtime's,
 * not a spin on the trapped register. */
void OSi_CancelDma0(void)
{
    volatile u32 *const ime = (volatile u32 *)0x04000208;
    const u32 saved = *ime;
    *ime = 0x04000000;  /* the register base, as the assembly stores it: bit 0 clear */
    khdays_wait_for_line(0);
    *ime = saved;
}

/* OSi_AlarmHandler: the timer interrupt's entry for OS alarms, an assembly
 * shim that keeps r0/lr around OSi_ArrangeTimer (0x020036a0). */
extern void OSi_ArrangeTimer(void);

void OSi_AlarmHandler(void)
{
    OSi_ArrangeTimer();
}

/* --- Left to the runtime (threads, interrupts, reset) -------------------- */
KHDAYS_HAL_TODO(OSi_ExceptionHandler)
KHDAYS_HAL_TODO(OSi_GetAndDisplayContext)
KHDAYS_HAL_TODO(OSi_SetExContext)
KHDAYS_HAL_TODO(func_0200302c)  /* OSi_DisplayExContext */
/* OS_ResetSystem (0x02003948): a restart of the whole console, the
 * parameter left for OS_GetResetParameter (runtime/start.c). */
extern void khdays_reset_system(u32 parameter);

void func_02003948(u32 parameter)
{
    khdays_reset_system(parameter);
}
KHDAYS_HAL_TODO(func_01ff8330)  /* OSi_DoBoot */
