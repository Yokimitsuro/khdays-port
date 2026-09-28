/* Interrupts that arrive while the game is busy.
 *
 * On the DS an interrupt stops the CPU between any two instructions. The
 * runtime delivers them where the game unmasks interrupts or waits; a loop
 * that does neither -- spinning on a counter only an interrupt advances, as
 * the opening movie's playback loop does on the VBlank count -- would never
 * see one. A watcher thread checks every millisecond whether an interrupt is
 * due and the game could take it; if the game thread is then running the
 * game's own code (the decompiled C sits in its own section,
 * compat/decomp_compat.h), it is suspended and made to call the interrupt
 * dispatcher at that instruction (irq_trampoline.asm), exactly as the ARM9
 * would have taken the interrupt there. KHDAYS_ASYNC_IRQ=0 turns it off. */
#include "../hal/hal.h"
#include "events.h"
#include "io.h"
#include "clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* The bounds of the game's code: the decomp's functions are in
 * .text$khdays_game, which the linker sorts between these two. */
#pragma code_seg(".text$khdays_gama")
__declspec(noinline) void khdays_game_code_begin(void)
{
}
#pragma code_seg(".text$khdays_gamz")
__declspec(noinline) void khdays_game_code_end(void)
{
}
#pragma code_seg()

extern u32 khdays_cpsr;
extern void khdays_irq_trampoline(void);
extern int khdays_io_trap_busy(void);

/* events.c: the cycle of the next timed event; set while the game waits in
 * the runtime (which delivers interrupts itself). */
extern volatile LONG64 khdays_next_event;
extern volatile LONG khdays_game_waiting;

/* Nonzero while the runtime runs game code from its own machinery (the ARM
 * interpreter's native calls): no interrupt is injected then. */
volatile LONG khdays_async_irq_blocked;

static HANDLE game_thread;
static volatile LONG injected;  /* one is on its way and has not run yet */

/* The interrupted instruction; the trampoline pushes it as its return
 * address. The watcher never writes the game thread's stack: a thread
 * suspended on its way into an exception has the kernel's exception frame
 * right below its stack pointer. */
volatile DWORD khdays_irq_return;

/* The trampoline's call, on the game thread. */
void khdays_irq_async(void)
{
    InterlockedExchange(&injected, 0);
    khdays_irq_poll();
}

/* An exception handler's first call (io_trap.c, arm.c). If the thread was
 * suspended on its way to an exception -- the faulting instruction was in
 * the game's code -- the redirected context reaches the handler, and the
 * kernel reports the exception at the trampoline too. The trampoline's first
 * instruction cannot fault that way, so a context there is always such a
 * redirect: undo it (the interrupted address is khdays_irq_return) so the handler
 * sees the real state; the interrupt is taken on a later try. */
void khdays_async_irq_undo(CONTEXT *context, const void *address)
{
    (void)address;
    if (context->Eip == (DWORD)(size_t)&khdays_irq_trampoline) {
        context->Eip = khdays_irq_return;
        InterlockedExchange(&injected, 0);
    }
}

static int due(void)
{
    if ((khdays_cpsr & 0x80) != 0 || (IO32(0x208) & 1) == 0) {
        return 0;  /* the game has interrupts off */
    }
    if (IO32(0x210) & IO32(0x214)) {
        return 1;
    }
    return khdays_clock_cycles() >= (u64)InterlockedCompareExchange64(&khdays_next_event, 0, 0);
}

static DWORD WINAPI watcher(LPVOID unused)
{
    const DWORD begin = (DWORD)(size_t)&khdays_game_code_begin;
    const DWORD end = (DWORD)(size_t)&khdays_game_code_end;
    const int trace = getenv("KHDAYS_TRACE_IRQ") != NULL;
    unsigned ticks = 0, waiting = 0, pending = 0, blocked = 0, not_due = 0, outside = 0, done = 0, lost = 0;
    DWORD last_eip = 0;
    (void)unused;
    if (trace) {
        fprintf(stderr, "irq watcher: game code 0x%08lx-0x%08lx\n", begin, end);
    }
    for (;;) {
        CONTEXT context;
        Sleep(1);
        if (trace && ++ticks % 1000 == 0) {
            fprintf(stderr, "irq watcher: %u waiting, %u pending, %u blocked, %u not due, %u outside "
                            "(last 0x%08lx), %u injected, %u lost; cpsr %02x ime %u ie %08x if %08x\n",
                    waiting, pending, blocked, not_due, outside, last_eip, done, lost, khdays_cpsr & 0xff,
                    IO32(0x208) & 1, IO32(0x210), IO32(0x214));
            waiting = pending = blocked = not_due = outside = done = lost = 0;
        }
        if (khdays_game_waiting) { ++waiting; continue; }
        if (injected) {
            /* A context change made while the thread was in the kernel (an
             * exception being dispatched) can be dropped: after a few
             * milliseconds without the trampoline running, try again. */
            if (++pending % 4 == 0) {
                InterlockedExchange(&injected, 0);
                ++lost;
            }
            continue;
        }
        if (khdays_async_irq_blocked) { ++blocked; continue; }
        if (!due()) { ++not_due; continue; }
        if (SuspendThread(game_thread) == (DWORD)-1) {
            continue;
        }
        context.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(game_thread, &context) && !khdays_game_waiting && !injected &&
            !khdays_async_irq_blocked && !khdays_io_trap_busy() && !(context.EFlags & 0x100) &&
            context.Eip >= begin && context.Eip < end) {
            khdays_irq_return = context.Eip;  /* the trampoline returns here */
            context.Eip = (DWORD)(size_t)&khdays_irq_trampoline;
            InterlockedExchange(&injected, 1);
            SetThreadContext(game_thread, &context);
            ++done;
        } else {
            ++outside;
            last_eip = context.Eip;
        }
        ResumeThread(game_thread);
    }
}

void khdays_async_irq_start(void)
{
    const char *setting = getenv("KHDAYS_ASYNC_IRQ");
    if (setting != NULL && atoi(setting) == 0) {
        return;
    }
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &game_thread, 0,
                    FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(NULL, 0, watcher, NULL, 0, NULL);
}
