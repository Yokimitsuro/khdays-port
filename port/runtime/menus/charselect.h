/* Mission Mode's character select (charselect.c) on one screen (host/screens/select_screen.c, a
 * mod's layouts/select/<lang>/select.txt). The game's screen runs underneath (its places, choices, A,
 * B, START, waits and messages); the port shows the characters the game lets be chosen in a grid of
 * its own and moves this copy's choice with the game's own request. The grid ends with a random
 * choice; Y chooses at random there and on the game's own screen too. */
#ifndef KHDAYS_CHARSELECT_H
#define KHDAYS_CHARSELECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_SELECT_CHARACTERS 19 /* the game's: their names are mlt_&.s's records 0x0b on */
#define KHDAYS_SELECT_PLACES 4
#define KHDAYS_SELECT_STATS 7
#define KHDAYS_SELECT_NAME 12 /* a player's name: the game's 11 UTF-16 characters and a NUL */

typedef struct KhdaysSelectPlace {
    int present;                       /* a player is in the place */
    int ready;                         /* the player chose (the game's A) */
    int character;                     /* the character the place is on: the game's 0-18, a mod's
                                        * number (runtime/characters), -1 none */
    uint16_t name[KHDAYS_SELECT_NAME]; /* the player's (empty when the game has none) */
} KhdaysSelectPlace;

typedef struct KhdaysSelectView {
    int shown;
    int language;       /* the game's (1-5) */
    int16_t master;     /* the game's fade: its brightness, -16 black to 16 white */
    int self;           /* this copy's place */
    int character;      /* this copy's character as shown: the one moved to, or the game's; -1 on the
                         * random choice */
    int random;         /* this copy is on the grid's random choice */
    uint32_t choosable; /* a bit for each character the game lets be chosen */
    KhdaysSelectPlace places[KHDAYS_SELECT_PLACES];
    int heading, help;                   /* the game's messages: mlt_&.s's records */
    int stat_label[KHDAYS_SELECT_STATS]; /* the gauges' names (records): the left column's four, then
                                          * the right's three, as the game lays them out */
    int stat[KHDAYS_SELECT_STATS];       /* this copy's character's gauges (0-10), in that order */
} KhdaysSelectView;

/* The game's thread: an object's update ran (diag.c, its native address). */
void khdays_charselect_update_ran(uintptr_t update);
/* The game's thread, once a frame (input.c): the buttons the game gets (the port takes the
 * directions while it shows the screen). */
uint16_t khdays_charselect_buttons(uint16_t buttons);
/* The game's thread, from its own screen's text (prepare.py's hooks): its help line `game` (message
 * `message`), with "Y Random" after it while the player chooses, written into `out` (`units` UTF-16
 * units on the game's stack); otherwise `game`. */
void *khdays_charselect_help(int message, void *game, void *out, int units);
/* The character at `position` of the port's grid: the ones the game lets be chosen in its order, the
 * mods' after them (runtime/characters), then the random choice (KHDAYS_SELECT_CHARACTERS); -1 past
 * it. */
int khdays_charselect_at(const KhdaysSelectView *v, int position);
/* Any thread: the screen as last published. */
void khdays_charselect_view(KhdaysSelectView *out);
/* The window's thread: the columns of the port's grid (its layout's), for the directions. */
void khdays_charselect_columns(int columns);

#ifdef __cplusplus
}
#endif

#endif
