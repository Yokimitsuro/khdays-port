/* An ARMv5TE interpreter for the few routines the game runs as machine code
 * the native build cannot compile (arm.c). */
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

#endif
