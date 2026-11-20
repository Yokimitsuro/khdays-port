/* The game's memory read and written at a reference (window.h) by the port's C: volatile, as the game's own thread
 * may change it between two reads. One copy for every file that reaches into the game's structures. */
#ifndef KHDAYS_GAME_MEMORY_H
#define KHDAYS_GAME_MEMORY_H

#include "window.h"

static inline unsigned rd32(KhdaysRef at)
{
    return *(const volatile unsigned *)KHDAYS_W(at);
}

static inline unsigned short rd16(KhdaysRef at)
{
    return *(const volatile unsigned short *)KHDAYS_W(at);
}

static inline unsigned char rd8(KhdaysRef at)
{
    return *(const volatile unsigned char *)KHDAYS_W(at);
}

static inline void wr32(KhdaysRef at, unsigned value)
{
    *(volatile unsigned *)KHDAYS_W(at) = value;
}

static inline void wr16(KhdaysRef at, unsigned value)
{
    *(volatile unsigned short *)KHDAYS_W(at) = (unsigned short)value;
}

static inline void wr8(KhdaysRef at, unsigned value)
{
    *(volatile unsigned char *)KHDAYS_W(at) = (unsigned char)value;
}

/* The same reads, 0 for a reference outside the game's RAM (khdays_game_ram): what a script or a table names may be
 * gone or never was */
static inline unsigned checked_rd32(KhdaysRef at)
{
    return khdays_game_ram(at) ? rd32(at) : 0;
}

static inline unsigned short checked_rd16(KhdaysRef at)
{
    return khdays_game_ram(at) ? rd16(at) : 0;
}

static inline unsigned char checked_rd8(KhdaysRef at)
{
    return khdays_game_ram(at) ? rd8(at) : 0;
}

#endif
