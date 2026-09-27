/* The ARM9 IRQ path: what the BIOS IRQ vector does, NitroSDK's dispatcher
 * (func_01ff8148, OS_IrqHandler) and its return path (func_01ff81a0,
 * OS_IrqHandler_ThreadSwitch), translated from libs/nitro/os/asm_stubs/calls.
 *
 * Natively an interrupt is taken where the runtime delivers it (khdays_irq_
 * deliver): the interrupted code is whatever fiber is running there, and a
 * thread switch at the end of the IRQ is a fiber switch, returned from when
 * that thread is scheduled again -- as the DS resumes it from its saved
 * context. */
#include "hal.h"

#define REG_IME (*(volatile u32 *)0x04000208)
#define REG_IE  (*(volatile u32 *)0x04000210)
#define REG_IF  (*(volatile u32 *)0x04000214)

/* OS_IRQTable: one handler per IE bit, at the start of the DTCM. */
#define OS_IRQ_TABLE ((volatile u32 *)0x027e0000)
/* The BIOS jumps through the word at DTCM + 0x3ffc (crt0 stores
 * OS_IrqHandler there). */
#define BIOS_IRQ_VECTOR (*(volatile u32 *)0x027e3ffc)

/* OSi_IrqThreadQueue (head, tail): threads sleeping until an IRQ. */
#define IRQ_THREAD_QUEUE ((volatile u32 *)0x027e006c)

/* OSThreadInfo, data_02044330. */
typedef struct {
    u16 isNeedRescheduling;
    u16 irqDepth;
    u32 current;
    u32 list;
    void (*switchCallback)(u32 from, u32 to);
} OSThreadInfo;
extern OSThreadInfo data_02044330;

/* OSThread fields the return path touches. */
#define THREAD_STATE(t)      (*(volatile u32 *)((t) + 0x64))
#define THREAD_STATE_HALF(t) (*(volatile u16 *)((t) + 0x64))
#define THREAD_NEXT(t)       (*(volatile u32 *)((t) + 0x68))
#define THREAD_QUEUE(t)      (*(volatile u32 *)((t) + 0x78))
#define THREAD_LINK_PREV(t)  (*(volatile u32 *)((t) + 0x7c))
#define THREAD_LINK_NEXT(t)  (*(volatile u32 *)((t) + 0x80))

extern int OS_SaveContext(void *context);
extern void OS_LoadContext(void *context);

/* The CPSR the IRQ interrupted (the assembly reads it back as spsr). */
static u32 interrupted_cpsr;

void func_01ff81a0(void);

/* OS_IrqHandler: acknowledge the lowest pending enabled IRQ and call its
 * handler; the handler returns into func_01ff81a0. */
void func_01ff8148(void)
{
    u32 pending;
    u32 index = 0;
    if (REG_IME == 0) {
        return;
    }
    pending = REG_IE & REG_IF;
    if (pending == 0) {
        return;
    }
    while ((pending & (1u << index)) == 0) {
        ++index;
    }
    REG_IF = 1u << index;  /* write-one-to-clear */
    ((void (*)(void))OS_IRQ_TABLE[index])();
    func_01ff81a0();
}

/* OS_IrqHandler_ThreadSwitch: wake the threads waiting for an IRQ, then, if a
 * reschedule is due, switch to the first ready thread in priority order. */
void func_01ff81a0(void)
{
    OSThreadInfo *info = &data_02044330;
    u32 thread = IRQ_THREAD_QUEUE[0];
    u32 next;
    u32 current;
    if (thread != 0) {
        do {
            THREAD_STATE(thread) = 1;  /* OS_THREAD_STATE_READY */
            THREAD_QUEUE(thread) = 0;
            THREAD_LINK_PREV(thread) = 0;
            next = THREAD_LINK_NEXT(thread);
            THREAD_LINK_NEXT(thread) = 0;
            thread = next;
        } while (thread != 0);
        IRQ_THREAD_QUEUE[0] = 0;
        IRQ_THREAD_QUEUE[1] = 0;
        info->isNeedRescheduling = 1;
    }
    if (info->isNeedRescheduling == 0) {
        return;
    }
    info->isNeedRescheduling = 0;
    thread = info->list;
    while (thread != 0 && THREAD_STATE_HALF(thread) != 1) {
        thread = THREAD_NEXT(thread);
    }
    if (thread == 0) {
        return;
    }
    current = info->current;
    if (thread == current) {
        return;
    }
    if (info->switchCallback != 0) {
        info->switchCallback(current, thread);
    }
    info->current = thread;
    /* Save the interrupted thread (with the CPSR it was interrupted in) and
     * resume the chosen one. The OSContext is the first member of OSThread. */
    OS_SaveContext((void *)current);
    *(volatile u32 *)current = interrupted_cpsr;
    OS_LoadContext((void *)thread);
}

/* Take the IRQ exception if one is pending and unmasked: the CPU enters IRQ
 * mode with IRQs disabled and the BIOS calls through its vector. Returns 1 if
 * it did. */
int khdays_irq_deliver(void)
{
    u32 saved;
    if ((khdays_cpsr & 0x80) != 0 || REG_IME == 0 || (REG_IE & REG_IF) == 0) {
        return 0;
    }
    saved = khdays_cpsr;
    interrupted_cpsr = saved;
    khdays_cpsr = (saved & ~0x1fu) | 0x80 | 0x12;
    ((void (*)(void))BIOS_IRQ_VECTOR)();
    khdays_cpsr = saved;
    return 1;
}
