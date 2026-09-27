/* The DS I/O registers: trapped game accesses (io_trap.c) and what each
 * register does (io_regs.c). */
#ifndef KHDAYS_IO_H
#define KHDAYS_IO_H

#include "../hal/hal.h"

#define KHDAYS_IO_BASE  0x04000000u
#define KHDAYS_IO_SIZE  0x10000u
#define KHDAYS_IO2_BASE 0x04100000u  /* IPCFIFORECV, card data */
#define KHDAYS_IO2_SIZE 0x10000u

/* Untrapped views of the two regions, for the runtime (memory.c). */
extern u8 *khdays_io_host;
extern u8 *khdays_io2_host;

#define IO8(offset)  (*(volatile u8 *)(khdays_io_host + (offset)))
#define IO16(offset) (*(volatile u16 *)(khdays_io_host + (offset)))
#define IO32(offset) (*(volatile u32 *)(khdays_io_host + (offset)))

/* Arms the trap on the game's view (io_trap.c). */
void khdays_io_trap_init(void);

/* Before the game reads [address, address + size): bring the values up to
 * date. After it wrote them (`before` holds the bytes they replaced): apply
 * what the write means. */
void khdays_io_read(u32 address, int size);
void khdays_io_write(u32 address, int size, const u8 *before);

/* Initial register values, as the ARM9 sees them when crt0 starts. */
void khdays_io_reset(void);

/* Raise interrupt request bits (IF). */
void khdays_io_request_irq(u32 bits);

#endif
