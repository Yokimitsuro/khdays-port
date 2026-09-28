/* An ARMv5TE interpreter for the few routines the game runs as machine code
 * the native build cannot compile, and for the ARM7's sound driver (arm.c). */
#ifndef KHDAYS_ARM_H
#define KHDAYS_ARM_H

#include "../hal/hal.h"

/* Runs the ARM (or, with bit 0 set, Thumb) routine at `entry` with the
 * ARM calling convention: `args[0..3]` in r0-r3, the rest on the stack.
 * Returns r1:r0. Calls it makes to native code (addresses outside DS memory)
 * are made natively. */
u64 khdays_arm_call(u32 entry, const u32 *args, int count);

/* Catches native calls into DS memory (a DEP fault on code there) and runs
 * them in the interpreter. */
void khdays_arm_init(void);

/* KHDAYS_TRACE_ARM: prints the calls per entry so far. */
void khdays_arm_report(void);

/* --- The ARM7 ------------------------------------------------------------------
 * Code on the ARM7's bus: main RAM through its mirrors, its WRAM
 * (0x037f8000-0x0380ffff) and its own I/O registers (sound.c). A branch
 * anywhere else stops the run. */

/* A native stand-in for an ARM7 routine: r0-r3 hold the arguments and r13
 * the stack with the rest; results go in r0 (and r1). It returns 1 to suspend
 * the thread that called it, which resumes as if the call had returned. */
typedef int (*KhdaysArmHook)(u32 *r);
void khdays_arm7cpu_hook(u32 address, KhdaysArmHook hook);

/* The ARM7 BIOS: SWI `number` with the registers; nonzero when handled. */
typedef int (*KhdaysArmSwi)(u32 number, u32 *r);
void khdays_arm7cpu_bios(KhdaysArmSwi swi);

/* Runs the ARM7 routine at `entry` on the stack below `sp`; returns r1:r0. */
u64 khdays_arm7cpu_call(u32 entry, const u32 *args, int count, u32 sp);

/* An ARM7 thread: `entry(arg)` on the stack below `sp`, run a stretch at a
 * time. Resuming runs it until a hook suspends it (0) or it returns (1). */
typedef struct KhdaysArmThread KhdaysArmThread;
KhdaysArmThread *khdays_arm7cpu_thread(u32 entry, u32 arg, u32 sp);
int khdays_arm7cpu_resume(KhdaysArmThread *thread);

#endif
