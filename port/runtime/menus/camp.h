/* The camp menus (camp.c), Mission Mode's and the story's, for the port to draw on one screen
 * (host/screens/camp_screen.c, a mod's layouts/camp/<lang>/camp.txt). The game's own menu runs
 * underneath (its options, cursor, sounds and pages); the port shows its menu page while the game is
 * on it, its Preferences page in the menu's place, and the game's screens everywhere else. */
#ifndef KHDAYS_CAMP_H
#define KHDAYS_CAMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_CAMP_OPTIONS 7   /* each camp's seven: Mission Mode's entries 101-107, the story's 1-6 and 9 */
#define KHDAYS_CAMP_FIELDS 0x15 /* the status panel's fields (Ov008_DrawStatusField's 0-0x14) */
#define KHDAYS_CAMP_CHARACTERS 19
#define KHDAYS_CAMP_PLACES 4 /* a group's: the game's four */
#define KHDAYS_CAMP_NAME 12  /* a player's name: the game's 11 UTF-16 characters and a NUL */
/* where a member is (the game's pages: the member's record +3): the menu, an option's page (1-6:
 * the options' order), or ready (8: it accepted the mission) */
enum { KHDAYS_CAMP_IN_MENU = 0, KHDAYS_CAMP_READY = 8 };
#define KHDAYS_CAMP_SETTINGS 25 /* the Preferences page's table: the game's 25 entries */
#define KHDAYS_CAMP_VALUES 3    /* a setting's values: an entry holds three */
#define KHDAYS_CAMP_SECTIONS 5  /* the page's tabs: a list of them holds five */
#define KHDAYS_CAMP_PANEL_PAGES 3  /* the panels' grid (the game state's +0xee0): three pages */
#define KHDAYS_CAMP_PANEL_CELLS 40 /* of five columns and eight rows */
#define KHDAYS_CAMP_TEXT 200       /* a description's UTF-16 units, its NUL among them */

/* A setting of the Preferences page (an entry of the game's table, its words cfg_&.s records):
 * its name and help, its values' names and helps, the one chosen; the game's flag `enabled` off,
 * the game shows its name and values as "?" (record 3). */
typedef struct KhdaysCampSetting {
    int enabled;
    int name, help;
    int values;
    int value_name[KHDAYS_CAMP_VALUES], value_help[KHDAYS_CAMP_VALUES];
    int value;
} KhdaysCampSetting;

/* The Preferences page: its sections (the game's tabs, those the game gives one title together:
 * a cfg_&.s record each, their settings in setting[] from `first`), the one the game's tab is in,
 * the setting the game's cursor is on. */
typedef struct KhdaysCampPage {
    int sections;
    int title[KHDAYS_CAMP_SECTIONS];
    int first[KHDAYS_CAMP_SECTIONS], count[KHDAYS_CAMP_SECTIONS];
    int section;
    int settings;
    KhdaysCampSetting setting[KHDAYS_CAMP_SETTINGS];
    int chosen;
} KhdaysCampPage;

/* The story camp's Files pages (its screen 7: Roxas's Diary, Enemy Profiles -- Ov025_Reports_*, the
 * list on the lower screen, the entry on the upper): the list as the game has it, for the port to draw
 * its own (host/screens/camp_screen.c, "The Files pages"). A row: the diary's day (the enemies' its
 * place in the list, from 1), its title (the enemy's name; while A is held on the cursor's row, its
 * other version's, as the game shows it), whether the enemy is not met yet (its name the game's "?"),
 * the game's NEW badge: -1 none, 0 never viewed, 1 viewed on this visit (gone at the next), whether A
 * shows another page of it while held (the diary's entry rewritten, an enemy's drops: the game's A mark
 * on the row). */
#define KHDAYS_CAMP_FILES_MOST 192
#define KHDAYS_CAMP_FILES_TITLE 40
typedef struct KhdaysCampFileRow {
    int16_t number;
    int8_t locked;
    int8_t badge;
    int8_t alternate;
    uint16_t title[KHDAYS_CAMP_FILES_TITLE];
} KhdaysCampFileRow;

typedef struct KhdaysCampFiles {
    int mode;        /* 0 Roxas's Diary, 1 Enemy Profiles */
    int count;       /* the game's rows (row[] holds the first KHDAYS_CAMP_FILES_MOST) */
    int top, cursor; /* the game's first row shown (of its ten), the row chosen */
    int held;        /* A held: the chosen row's other page shown */
    KhdaysCampFileRow row[KHDAYS_CAMP_FILES_MOST];
} KhdaysCampFiles;

/* A field of the status panel as the game last drew it: its values (fields 0x11 and 0x12 have two;
 * 0xc is Q12, 0x13 a record of status_&.s), and the change the Panels page previews with a panel held (the
 * game's Ov025_PreviewDropSummary: what the field would become with the panel dropped at the cursor, less its
 * value; 0 none, 0xc's in whole points as the DS draws it). */
typedef struct KhdaysCampField {
    int has;
    uint32_t value[2];
    int32_t delta;
} KhdaysCampField;

/* A member of the group (a place of the session): whether one is there, where it is
 * (KHDAYS_CAMP_IN_MENU, an option's page 1-6 or KHDAYS_CAMP_READY), its character, its player's name
 * (empty when the game has none). */
typedef struct KhdaysCampMember {
    int present;
    int page;
    int character;
    uint16_t name[KHDAYS_CAMP_NAME];
} KhdaysCampMember;

typedef struct KhdaysCampView {
    int shown;
    int leaving; /* the menu closes (B at its root: the camp's next screen -1, Ov025_MenuBack): the view the
                  * last before it while the game takes the menu down (host/screens/camp_screen.c fades it) */
    int next;    /* the camp's next screen (SetTargetSlot's word: the screen it goes to, then the one it is
                  * on): 5 while the menu goes to its Preferences page, 0 while the page comes back, -1 out
                  * of the camp; KHDAYS_CAMP_NONE unknown. The window's screen slides the two into each
                  * other on it, in place of the game's black between them */
    int story;                       /* the story's camp (ov025): its options, its day; no group */
    int sub;                         /* the story's: 0 its options, 1 Holo-Missions' two, 2 Files' two (selected
                           * the one of them the cursor is on) */
    int day;                         /* the story's day (the game state's field 9) */
    int difficulty;                  /* the story's (the game state's 0x40a, two bits: 0-2 Beginner, Standard, Proud) */
    int language;                    /* the game's (1-5) */
    int16_t master;                  /* the game's fade: its brightness, -16 black to 16 white */
    int selected;                    /* the option selected, 0 on (the game's entry - 101); -1 none (waiting) */
    int description;                 /* the record of root_&.s the game shows for it, -1 none */
    int waiting;                     /* Mission Mode's: the mission accepted, the group waits for it (the game's
                           * entry 9): no option selected, all dimmed */
    int notice;                      /* Mission Mode's: the line the DS shows over its menu, a root_&.s record
                           * (0x16-0x1c: Ov008_Menu_UpdateDirectionalPrompt -- whether the host is
                           * choosing a mission, START to join or to begin, ...), -1 none */
    int question;                    /* Title Screen's question open (quit the game?): its answers Yes and No */
    int locked[KHDAYS_CAMP_OPTIONS]; /* the rows shown the game has locked (it shows them dimmed and
                                      * its cursor passes them) */
    int yes;                         /* the answer the cursor is on: 1 Yes, 0 No */
    int config;                      /* the Preferences page is on (screen 5): `page` instead of the menu's */
    KhdaysCampPage page;
    int files; /* the story's Files page is on (screen 7): `file` instead of the menu's */
    KhdaysCampFiles file;
    int save;        /* the camp's save screen is on (the story's screen 4, Mission Mode's own): the
                                      * port's save list (savelist.h) in the camp's look; `selected` the menu's
                                      * Save, `description` its */
    int save_screen; /* the camp's save screen's index (its `next` going there), -1 unknown */
    int files_new;   /* the story's menu: what of Files has an entry never viewed (the game's NEW
                                      * inside): bit 0 Roxas's Diary, bit 1 Enemy Profiles */
    int panel_cells; /* the story's menu: the panels' slots open (item 1's count + 15, at most 120;
                                      * the rest locked), and the panels installed in them (Ov025_LoadGridFromSave's
                                      * table: an item id a cell, row by row, 0 none; a link's id at its anchor,
                                      * its footprint's other cells the panels linked into it) */
    uint16_t panel[KHDAYS_CAMP_PANEL_PAGES][KHDAYS_CAMP_PANEL_CELLS];
    int panels;       /* the story's Panels page is on (screen 2): its grid and list on the lower screen, the
                       * status page on the upper one (host/screens/camp_screen.c shows them as cards) */
    int panels_view;  /* that upper screen's view: 1 the numbers (the default; the port's status panel in its
                       * place), 0 the list of what the panels give (X: Ov025_ChangeMenuSelection) */
    int panels_slide; /* its slide between the two (dots): 192 the numbers, 0 the list, between while it moves */
    int panels_early; /* the page setting up: its upper screen shows the list (the numbers' view coming), its words
                       * drawn -- the port takes the list from it (camp.c read_panels) */
    uint16_t panel_text[KHDAYS_CAMP_TEXT]; /* the description of the panel under the grid's cursor, or the list's
                                            * row's, or the one carried (the lower screen's own line) */
    uint32_t munny, hearts;
    int self;      /* this copy's place */
    int character; /* its character, 0-18 (mlt_&.s's order), -1 unknown */
    KhdaysCampField field[KHDAYS_CAMP_FIELDS];
    KhdaysCampMember member[KHDAYS_CAMP_PLACES];
} KhdaysCampView;

/* The game's thread: an object's update ran (diag.c, its native address). */
void khdays_camp_update_ran(uintptr_t update);
/* The game's thread: the status panel draws a field (prepare.py's hook in Ov008_DrawStatusField and
 * Ov025_DrawStatusField; `values` the game's own, two words). Returns the change the game is to draw beside it:
 * `delta`, or 0 while the port draws the Panels page (its status card shows the change instead of the DS's
 * upper screen). */
unsigned int khdays_camp_field(void *panel, int field, unsigned int delta, int style, void *values, int animate);
/* The window's thread: whether the port drew the Panels page (with its status card) this frame. */
void khdays_camp_set_preview_drawn(int drawn);
/* The game's thread, Ov008/Ov025_DrawPageBWidget (prepare.py's hook): the change its up and down arrows show
 * (Ov025_SetPairSelection: the up one for more, the down one for less, none for 0): `delta`, or 0 while the
 * port draws the Panels page. */
int khdays_camp_preview_arrows(int delta);
/* The game's thread: the story camp's grid menu wrote a row of its text panel (prepare.py's hook in
 * Ov025_RepaintTextRow: the panel, the row, the UTF-16 text or 0, its style; DS addresses). */
void khdays_camp_text_row(unsigned int self, int row, unsigned int text, int style);
/* Any thread: the menu as last published. */
void khdays_camp_view(KhdaysCampView *out);
/* Any thread: the game's fade now, as the view's `master` (-16 black to 16 white). */
int khdays_camp_game_brightness(void);
/* The game's thread: the loaded camp's screen (its table A's index: 0 the menu, 1 Holo-Missions,
 * 5 Preferences, -1 between two; the others an option's page), *story whether it is the story's
 * camp; KHDAYS_CAMP_NONE with no camp loaded. */
#define KHDAYS_CAMP_NONE (-2)
int khdays_camp_screen(int *story);
/* HD routing for the camp's four handles and the shops' three, never a shared NFTR file. */
void khdays_camp_font_scope(uint32_t font, char *scope, int size);
/* The window's thread: whether the port drew the Preferences page this frame (host/screens/camp_screen.c):
 * only then the page's directions and L and R are the port's (khdays_camp_buttons). */
void khdays_camp_set_page_drawn(int drawn);
/* The window's thread: whether the port drew the story's Files page (the Menu Overhaul's): only then X opens its
 * Enemy Profiles' model viewer (enemy_viewer.h). */
void khdays_camp_set_files_drawn(int drawn);
/* The game's thread, Ov025_Hub_InitEntries (prepare.py's hook): whether the menu's start hovers its entry (its
 * help drawn at once: with the Menu Overhaul). */
int khdays_camp_hover_at_start(void);
/* The window's thread: the story's menu drawn with the panels' grid of `pages` pages (0 not drawn): Y turns
 * them (the game's Y kept from it there, the Panels page's sound played, as Ov025_TickIdleTouchSample does);
 * and how many times Y turned them. */
void khdays_camp_set_panels_drawn(int pages);
unsigned khdays_camp_panel_turns(void);
/* The game's thread, at Pad_Sample (input.c): the buttons the game gets. On the Preferences page
 * the port's sections join the game's tabs: UP and DOWN go over a section's settings, L and R
 * from section to section, the port pressing the game's own buttons to take its cursor there;
 * the rest are the player's. */
uint16_t khdays_camp_buttons(uint16_t buttons);

#ifdef __cplusplus
}
#endif

#endif
