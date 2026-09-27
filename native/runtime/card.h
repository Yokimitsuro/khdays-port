/* The game card's bus (card.c). */
#ifndef KHDAYS_CARD_H
#define KHDAYS_CARD_H

#include "../hal/hal.h"

/* The card's chip ID. It depends on the physical cartridge, which a ROM file
 * does not record; the game only compares the card's answer to the copy the
 * firmware kept at boot (CARDi_CheckPulledOutCore), so the card and boot.c
 * share one value. */
#define KHDAYS_CARD_CHIP_ID 0u

/* ROMCTRL was written with its start bit. */
void khdays_card_start(void);

/* A read of the data port (0x04100010). */
u32 khdays_card_read_word(void);

#endif
