/* The native hardware layer: what stands in for the DS below NitroSDK. */
#ifndef KHDAYS_HAL_H
#define KHDAYS_HAL_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef short s16;
typedef int s32;
typedef long long s64;
typedef s32 fx32;

#ifdef __cplusplus
extern "C" {
#endif

/* Stops the process naming a routine the port has not implemented yet. The
 * HAL never guesses at behaviour: a path that reaches one of these is a gap to
 * fill from the SDK/hardware documentation, not something to paper over. */
__declspec(noreturn) void khdays_hal_unimplemented(const char *what);

#define KHDAYS_HAL_TODO(name) \
    void name(void) { khdays_hal_unimplemented(#name); }

/* The emulated ARM9 CPSR: the mode (bits 0-4) and the IRQ/FIQ disable bits
 * (0x80/0x40). Nothing else of it has a native meaning. */
extern u32 khdays_cpsr;

/* Where the game would halt the CPU or spin until an interrupt: the runtime
 * delivers what is pending (VBlank, PXI replies) and returns once at least
 * one interrupt has been delivered. */
void khdays_runtime_wait(void);

/* Blocks until the display is on the given line (VCOUNT), clock.c. */
void khdays_wait_for_line(u32 line);

/* Interrupts were just unmasked: take what is pending (events.c). */
void khdays_irq_poll(void);

#ifdef __cplusplus
}
#endif

#endif
