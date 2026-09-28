/* The ARM7, high level: the other side of the IPC registers. The DS's ARM7
 * runs Nintendo's system component (sound, touch, RTC, power, backup
 * memory); natively each service it offers the ARM9 over PXI is answered
 * here, one message at a time. */
#ifndef KHDAYS_ARM7_H
#define KHDAYS_ARM7_H

#include "../hal/hal.h"

void khdays_arm7_reset(void);

/* IPCSYNC: the ARM9 wrote its output nibble (bits 8-11); the ARM7's output
 * nibble is what the ARM9 reads in bits 0-3. */
void khdays_arm7_sync_written(u32 arm9_nibble);
u32 khdays_arm7_sync_nibble(void);

/* The send FIFO: a word the ARM9 pushed. */
void khdays_arm7_receive(u32 word);

/* The receive FIFO, as the ARM9 sees it through IPCFIFOCNT/IPCFIFORECV. */
int khdays_arm7_recv_count(void);
u32 khdays_arm7_recv_pop(void);
int khdays_arm7_send_count(void);
void khdays_arm7_send_clear(void);

/* Once a frame (VBlank): what the ARM7 does periodically. */
void khdays_arm7_frame(void);

#endif
