/* NitroSDK thread contexts (libs/nitro/os/asm_stubs: OS_InitContext,
 * OS_SaveContext, OS_LoadContext) on Windows fibers.
 *
 * The SDK's scheduler is decompiled C and runs unchanged; only the register
 * save/restore underneath is native. Each OSContext that has run owns one
 * fiber. The SDK's idiom is
 *
 *     if (OS_SaveContext(&cur->context)) return;   // resumed later: returns 1
 *     ...
 *     OS_LoadContext(&next->context);              // does not return
 *
 * Natively OS_SaveContext returns 0 and records which fiber `cur` is;
 * OS_LoadContext switches to `next`'s fiber. When `cur` is scheduled again its
 * fiber resumes inside that OS_LoadContext call, which returns to the caller --
 * the same place the DS's "SaveContext returned 1" path reaches. Every thread
 * runs on its fiber's native stack; the DS stack the game allocated for it is
 * left unused (its guard words are still written and checked by the SDK). */
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* KHDAYS_TRACE_THREADS: log every thread creation and switch, with the SDK's
 * thread list (OSi_ThreadInfo.list at 0x02044338; OSThread state +0x64, next
 * +0x68, priority +0x70). */
static int trace_threads;

static void trace_list(void)
{
    u32 t = *(volatile u32 *)0x02044338;
    fprintf(stderr, "  list:");
    for (int n = 0; t != 0 && n < 32; ++n) {
        fprintf(stderr, " %08x(s%u,p%u)", t, *(volatile u32 *)(t + 0x64), *(volatile u32 *)(t + 0x70));
        t = *(volatile u32 *)(t + 0x68);
    }
    fprintf(stderr, "\n");
}

/* OSContext: cpsr, r0-r12, sp, lr, pc+4, the SVC sp, then the CP (divider)
 * context at +0x48. */
typedef struct OSContext {
    u32 cpsr;
    u32 r[13];
    u32 sp;
    u32 lr;
    u32 pc_plus4;
    u32 sp_svc;
    u8 cp_context[0x1c];
} OSContext;

extern void CP_SaveContext(void *pContext);
extern void CPi_RestoreContext(const void *pContext);

#define MAX_CONTEXTS 64

static struct {
    OSContext *context;
    void *fiber;
} fibers[MAX_CONTEXTS];

/* A fiber replaced while it was running (a thread re-initialising its own
 * context to run its destructor), deleted once another fiber runs. */
static void *retired;

static void delete_retired(void)
{
    if (retired != NULL && retired != GetCurrentFiber()) {
        DeleteFiber(retired);
        retired = NULL;
    }
}

static int slot_of(const OSContext *context)
{
    for (int i = 0; i < MAX_CONTEXTS; ++i) {
        if (fibers[i].context == context) {
            return i;
        }
    }
    return -1;
}

static int free_slot(void)
{
    for (int i = 0; i < MAX_CONTEXTS; ++i) {
        if (fibers[i].context == NULL) {
            return i;
        }
    }
    khdays_hal_unimplemented("more thread contexts than the fiber table holds");
}

/* The thread's first run: enter its function with r0 as the argument, and on
 * return go where lr points (the SDK sets it to the thread-exit routine). */
static void CALLBACK thread_fiber(void *parameter)
{
    OSContext *context = (OSContext *)parameter;
    const u32 entry = context->pc_plus4 - 4;
    const u32 argument = context->r[0];
    const u32 exit_hook = context->lr;
    delete_retired();
    ((void (*)(u32))entry)(argument);
    if (exit_hook != 0) {
        ((void (*)(void))exit_hook)();
    }
    khdays_hal_unimplemented("a thread function returned with no exit hook");
}

/* The assembly's register image, kept for code that reads it; the fiber is
 * what actually runs. */
void OS_InitContext(OSContext *context, u32 newpc, u32 newsp)
{
    int slot;
    if (trace_threads) {
        fprintf(stderr, "thread: init %p entry %08x stack %08x\n", (void *)context, newpc, newsp);
        trace_list();
    }
    newpc += 4;
    context->pc_plus4 = newpc;
    context->sp_svc = newsp;
    newsp -= 0x40;  /* HW_SVC_STACK_SIZE */
    if (newsp & 4) {
        newsp -= 4;
    }
    context->sp = newsp;
    context->cpsr = (newpc & 1) ? 0x3f : 0x1f;  /* SYS mode, Thumb or ARM */
    for (int i = 0; i < 13; ++i) {
        context->r[i] = 0;
    }
    context->lr = 0;

    slot = slot_of(context);
    if (slot < 0) {
        slot = free_slot();
    } else if (fibers[slot].fiber == GetCurrentFiber()) {
        retired = fibers[slot].fiber;
    } else {
        DeleteFiber(fibers[slot].fiber);
    }
    fibers[slot].context = context;
    fibers[slot].fiber = CreateFiber(0, thread_fiber, context);
    if (fibers[slot].fiber == NULL) {
        khdays_hal_unimplemented("CreateFiber failed");
    }
}

BOOL OS_SaveContext(OSContext *context)
{
    int slot = slot_of(context);
    if (slot < 0) {
        /* A context that was never initialised is the thread already running
         * (the launcher thread): this fiber is it. */
        slot = free_slot();
        fibers[slot].context = context;
        fibers[slot].fiber = GetCurrentFiber();
    }
    CP_SaveContext(context->cp_context);
    context->cpsr = khdays_cpsr;
    return 0;
}

void OS_LoadContext(OSContext *context)
{
    const int slot = slot_of(context);
    if (slot < 0) {
        khdays_hal_unimplemented("OS_LoadContext of a context with no thread");
    }
    if (trace_threads) {
        fprintf(stderr, "thread: switch to %p (entry %08x, cpsr %02x -> %02x)\n",
                (void *)context, context->pc_plus4 - 4, khdays_cpsr, context->cpsr);
        trace_list();
    }
    CPi_RestoreContext(context->cp_context);
    /* The thread resumes with its own mode and interrupt state (the assembly
     * returns through spsr = the saved cpsr). */
    khdays_cpsr = context->cpsr;
    if (fibers[slot].fiber != GetCurrentFiber()) {
        SwitchToFiber(fibers[slot].fiber);
        delete_retired();
    }
}

/* The runtime starts the game on a fiber so the scheduler can switch away
 * from it. */
void khdays_threads_init(void)
{
    trace_threads = getenv("KHDAYS_TRACE_THREADS") != NULL;
    if (ConvertThreadToFiber(NULL) == NULL) {
        khdays_hal_unimplemented("ConvertThreadToFiber failed");
    }
}
