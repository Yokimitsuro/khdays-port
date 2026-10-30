/* The port's save list (savelist.c): the game's list drawn in the port's pieces
 * (host/screens/save_screen.c, layouts/save/<lang>/save.txt), with as many files as saveslots.h keeps.
 * The game's list runs underneath, hidden: the port keeps its own cursor and brings the file chosen
 * under the game's slot 1; prompts, reading, saving, deleting and fades are the game's. */
#ifndef KHDAYS_SAVELIST_H
#define KHDAYS_SAVELIST_H

#include "saveslots.h"

#include <stdint.h>

enum { KHDAYS_SAVELIST_NONE, KHDAYS_SAVELIST_LOAD, KHDAYS_SAVELIST_SAVE };

/* Whose words the screen shows: the title's list's (UI/load/lrd_&.s.z) or the camp's save
 * screen's (UI/cm/str/sav_&.s.z). */
enum { KHDAYS_WORDS_LOAD, KHDAYS_WORDS_SAVE };

/* What a file holds, for its row: as the game's record of a slot has it
 * (Ov000_RecordLoadResult). */
typedef struct KhdaysSaveRow {
    uint8_t state;      /* KHDAYS_SLOT_* */
    uint8_t crown;      /* the story's end seen: field 0x44e is 6 */
    uint8_t face;       /* field 0xc4b, rolled at each save (BeginSaveToSlot): the plate's face */
    uint8_t difficulty; /* field 0x40a: 0 beginner, 1 standard, 2 proud */
    uint16_t day;       /* field 0 (9 bits) */
    uint16_t level;     /* the panels' count plus one, as the game counts them */
    uint32_t munny;     /* the state's +8 */
    uint32_t seconds;   /* the play time: the state's +0 */
    uint32_t counter;   /* its save counter (fields 0xc77, 0xc87): its world (saveworld.h) */
} KhdaysSaveRow;

/* The list as the game's thread last left it. */
typedef struct KhdaysSaveView {
    int screen;            /* KHDAYS_SAVELIST_* */
    int words;             /* KHDAYS_WORDS_*: the strings ask[] names */
    int language;          /* the game's, 1-5 (English, French, German, Italian, Spanish) */
    int guest;             /* the guest's row after the files (Mission Mode's list) */
    int cursor;            /* the row selected: a file, or KHDAYS_SLOT_COUNT the guest's */
    int chosen;            /* the file under the game's slot 1 */
    int help;              /* the list's help shown (the title's list's states 1 and 2, the save screen's slots) */
    int ask[2];            /* the game's words in the prompt: records of its strings (`words`), -1 none */
    int ask_file;          /* the file their %d names (1 on) */
    int buttons;           /* the prompt's buttons: 0 none, 2 yes and no */
    int yes;               /* the button selected: 1 yes, 0 no */
    int16_t master;        /* the lower screen's master brightness (the game's fades): -16 black to 16 white */
    uint32_t rows_changed; /* counts each time the rows are made again */
    KhdaysSaveRow rows[KHDAYS_SLOT_COUNT];
} KhdaysSaveView;

/* The game's buttons this frame, at Pad_Sample (input.c): those the port's
 * list takes are its, the rest the game's. */
uint16_t khdays_savelist_buttons(uint16_t buttons);

/* The list for the window's thread: a copy. Its screen NONE when none shows. */
void khdays_savelist_view(KhdaysSaveView *out);

/* Whether the list's pieces are there for `language` (1-5): a mod's
 * layouts/save/<lang>/save.txt. */
int khdays_savelist_available(int language);

/* The [save] settings (host/app/profile.h KhdaysSave) for the game's thread: whether the game's own
 * lists page through the 99 files (without the list's pieces) or show the DS's three, and the
 * autosave's mode (KHDAYS_AUTOSAVE_*'s values). The window's thread gives them at its start and when
 * the settings change; without a window the defaults (the 99 files, the autosave into file 99). */
void khdays_savelist_set_settings(int files, int autosave);

/* The number the game's questions show for its slot `slot` (0-2): the file's in the page view, the
 * game's own (slot + 1) otherwise. Called from the game's text (port/tools/prepare.py HOOKS). */
int khdays_savelist_file_number(int slot);

/* The file being played (0 on): the one the title loaded or a camp's save screen wrote last;
 * KHDAYS_PLAYED_NONE on the title and in a game never saved, KHDAYS_PLAYED_GUEST in Mission Mode's guest
 * game (no file). Set by the autosave when it writes the file being played. */
enum { KHDAYS_PLAYED_NONE = -1, KHDAYS_PLAYED_GUEST = -2 };
int khdays_savelist_played_file(void);
void khdays_savelist_set_played_file(int file);
/* The level of the file the title loaded, as its list counted it (the game's own count of its panels;
 * 1 for Mission Mode's guest game); 0 when not known. */
int khdays_savelist_played_level(void);

/* The camp whose menu waits for the player now (8 Mission Mode's, ov008; 25 the story's, ov025): its
 * screen 0 with an option or a question's answer under its cursor. 0 when none. `*screen` and `*entry`
 * what was read (-1 nothing), for traces. */
int khdays_savelist_camp_menu(int *screen, int *entry);

/* The screen index of camp overlay `overlay`'s save screen (8 Mission Mode's, 25 the story's: its
 * screens' table's), -1 when that camp is not loaded. The game's thread. */
int khdays_savelist_camp_save_screen(int overlay);

#endif
