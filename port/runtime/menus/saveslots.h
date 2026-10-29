/* The save files beyond the DS's three (saveslots.c). The chip holds three slots of two copies (0x2018
 * bytes each) after a 0x20-byte header: files 1-3, in saves/khdays.sav; files 4 on are the port's,
 * saves/slots/slotNN.sav. The game reaches its slots through a view (game slot g is file view[g]):
 * the chip's 1-3; the list's, slot 0 the file chosen and 1-2 blank; the page view's, top + g. Files are
 * counted from 0 here. */
#ifndef KHDAYS_SAVESLOTS_H
#define KHDAYS_SAVESLOTS_H

#include "../../hal/hal.h"

#define KHDAYS_SLOT_COUNT 99     /* the save list's files: two digits, as DDD numbers its own */
#define KHDAYS_GAME_SLOTS 3      /* the game's own (and the chip's) */
#define KHDAYS_SLOT_COPY 0x2018u /* one copy: magic, SHA-1, the game's state */
#define KHDAYS_SLOT_BYTES (2u * KHDAYS_SLOT_COPY)
#define KHDAYS_SLOTS_START 0x20u   /* after the chip's header */
#define KHDAYS_STATE_BYTES 0x1cacu /* the game's state, signed, from a copy's +0x18 */

/* What a file holds, as the game's read tells it (Ov000_PollSaveLoad): copy 0
 * whose first eight words are zero is no save; a copy with the magic and its
 * SHA-1 a save; copy 1 tried when copy 0 is neither; neither copy a broken
 * file. */
enum { KHDAYS_SLOT_EMPTY, KHDAYS_SLOT_SAVED, KHDAYS_SLOT_BROKEN };

/* The chip was loaded (CARD IDENTIFY): `chip` its `size` bytes, the port's
 * files read from `folder` ("saves", "saves/player2"), the chip's own view.
 * Again on a reset. */
void khdays_slots_attach(u8 *chip, u32 size, const char *folder);

/* The byte at chip offset `offset` as the game sees it, for a read or a
 * write: its storage. `*chip_at` is where it lies in the chip, or
 * 0xffffffff when it is a port's file's (or blank: a write there is dropped).
 * NULL when nothing is attached. */
u8 *khdays_slots_byte(u32 offset, int writing, u32 *chip_at);

/* After a save action of the game's (khdays_backup_commit): the port's files
 * it wrote, written whole and atomically. 0 if one could not be. */
int khdays_slots_commit(void);

/* A file the game wrote to since the last asked (0 on), taken; -1 when none is left. */
int khdays_slots_next_written(void);

/* Whether a save action wrote to one of the port's files not committed yet. */
int khdays_slots_changed(void);

/* The port's view on or off (the chip's own), and whether it is on. On, game
 * slot 0 is the file shown last if it holds a save, else the file saved
 * last. */
void khdays_slots_set_port_view(int on);
int khdays_slots_port_view(void);

/* The page view (the game's own lists in pages, without the port's list): game slot g is file
 * top + g, `top` kept within 0..KHDAYS_SLOT_COUNT - 3; the port's view or the chip's own
 * (khdays_slots_set_port_view) ends it. khdays_slots_page: its top, -1 when it is not on. */
void khdays_slots_show_page(int top);
int khdays_slots_page(void);

/* The file game slot 0 is (the port's view), and making it `file`. */
int khdays_slots_shown(void);
void khdays_slots_show(int file);

/* What file `file` holds, and its state's bytes (KHDAYS_STATE_BYTES, the copy
 * the game would take) when it is a save. */
int khdays_slots_state(int file, const u8 **state);

/* A game state's field, as GameState_GetField reads it: `bits` bits from bit
 * `bit` of the bit array at the state's +0x10 (32-bit words, the first bit
 * the word's highest). */
u32 khdays_slots_field(const u8 *state, int bit, int bits);

/* The file saved last: the highest save counter (fields 0xc77 and 0xc87, the
 * camp's BeginSaveToSlot), the first of equals; -1 when none is a save. */
int khdays_slots_latest(void);

/* The highest save counter of all the files (as khdays_slots_latest reads it), 0 when none is a save. */
u32 khdays_slots_highest_counter(void);

/* The file game slot `slot` (0-2) is in the view now, -1 for a blank one. */
int khdays_slots_file_of(int slot);

/* The view as it is, kept to be put back after a write of the port's own through the game's code
 * (runtime/menus/autosave.c). */
typedef struct KhdaysSlotsView {
    int port_view, chosen, page, view[KHDAYS_GAME_SLOTS];
} KhdaysSlotsView;
void khdays_slots_keep_view(KhdaysSlotsView *kept);
void khdays_slots_put_view(const KhdaysSlotsView *kept);

/* A count that changes whenever the game writes one of its slots (a file's bytes may have
 * changed since it was last read). */
u32 khdays_slots_generation(void);

#endif
