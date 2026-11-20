/* The deck, the game's side (deck_link.h): the cells, and the panels' records read from the game's message
 * databases. docs/RUNTIME_MAP.md, "The deck". */
#include "deck_link.h"

#include "game_link.h"

#include "../core/session_log.h"
#include "../menus/camp.h"
#include "../core/game_memory.h"
#include "khdays_ds_symbols.h"

#include <stdlib.h>

#define STATE_DECK 0xee0u /* GameState: the deck's cells, an item id each */
#define DB_ROW 0x14u      /* data_0204c238's rows (MsgDb_InitTable): +8 the records, +0xc their count */
#define DB_RECORDS 0x8u
#define DB_COUNT 0xcu
#define DB_PANELS 0x15     /* the items' records, from id 1 (MsgDb_LoadRecord15) */
#define DB_SHAPES 0x1b     /* the panels' shapes, a 64-bit cell mask each (MsgDb_FetchEntryPair0x1b) */
#define DB_PARAMETERS 0x13 /* the battle's parameters, from 1: 4 bytes each, +2 the highest level (MsgDb_DecodeDb13) */
#define DB_SLOT 0xe        /* where the game loads them (Ov008_BuildMenuGrid) */
#define PANEL_BYTES 0x64u  /* +0 category, +6 kind, +8 shape (signed), +9 / +0xa the panel's cell in it, */
#define PANEL_MAGIC 0x18u  /* its magics from +0x18: 3 bytes each, the magic's battle index, level, casts */
#define PARAMETER_BYTES 4u
#define PARAMETER_CAP 2u
#define CAMP_MENU 0         /* khdays_camp_screen's menu */
#define CATEGORY_MATERIAL 2 /* the synthesis materials, recipes and techs: no panels of the deck */

extern int MsgDb_IsLoaded(int id);
extern int MsgDb_LoadDb(int id, int slot);
extern int ResSlot_Release_2(int id);
extern int Ov008_IsGridItemEligible(int itemId);

static KhdaysLinkPanel *panels; /* by id - 1 */
static int panel_count, panels_read;
static unsigned char *caps; /* each parameter's highest level, by its number - 1 */
static int cap_count;

/* The panels' records and the parameters' highest levels, read once from the game's databases (loaded for it when
 * they are not). */
static void read_panels(void)
{
    const unsigned table = rd32(KHDAYS_DS_data_0204c238);
    const int dbs[3] = {DB_PANELS, DB_SHAPES, DB_PARAMETERS};
    int loaded[3] = {0, 0, 0};
    if (table == 0 || khdays_link_state() == 0) {
        return; /* the game is not up yet: another time */
    }
    panels_read = 1;
    for (int d = 0; d < 3; ++d) {
        if (!MsgDb_IsLoaded(dbs[d])) {
            MsgDb_LoadDb(dbs[d], DB_SLOT);
            loaded[d] = 1;
        }
    }
    if (MsgDb_IsLoaded(DB_PANELS) && MsgDb_IsLoaded(DB_SHAPES)) {
        const unsigned records = rd32(table + DB_PANELS * DB_ROW + DB_RECORDS);
        const unsigned shapes = rd32(table + DB_SHAPES * DB_ROW + DB_RECORDS);
        const int shape_count = (int)rd16(table + DB_SHAPES * DB_ROW + DB_COUNT);
        const int count = (int)rd16(table + DB_PANELS * DB_ROW + DB_COUNT);
        panels = count > 0 ? calloc((size_t)count, sizeof(*panels)) : NULL;
        panel_count = panels != NULL ? count : 0;
        for (int i = 0; i < panel_count; ++i) {
            const unsigned r = records + (unsigned)i * PANEL_BYTES;
            KhdaysLinkPanel *p = &panels[i];
            p->category = (int)rd16(r);
            p->kind = (short)rd16(r + 6u);
            p->shape = (signed char)rd8(r + 8u);
            p->col = (int)rd8(r + 9u);
            p->row = (int)rd8(r + 0xau);
            p->level = (int)rd8(r + PANEL_MAGIC + 1u);
            p->casts = (int)rd8(r + PANEL_MAGIC + 2u);
            if (p->shape >= shape_count) {
                p->shape = -1;
            }
            if (p->shape >= 0) {
                const unsigned at = shapes + (unsigned)p->shape * 8u;
                p->cells = (unsigned long long)rd32(at) | (unsigned long long)rd32(at + 4u) << 32;
            }
        }
    }
    if (MsgDb_IsLoaded(DB_PARAMETERS)) {
        const unsigned records = rd32(table + DB_PARAMETERS * DB_ROW + DB_RECORDS);
        const int count = (int)rd16(table + DB_PARAMETERS * DB_ROW + DB_COUNT);
        caps = count > 0 ? calloc((size_t)count, 1) : NULL;
        cap_count = caps != NULL ? count : 0;
        for (int i = 0; i < cap_count; ++i) {
            caps[i] = (unsigned char)rd8(records + (unsigned)i * PARAMETER_BYTES + PARAMETER_CAP);
        }
    }
    for (int d = 2; d >= 0; --d) {
        if (loaded[d]) {
            ResSlot_Release_2(dbs[d]);
        }
    }
    khdays_log("mods: %d panels' records read", panel_count);
}

int khdays_link_magic_cap(int magic)
{
    if (!panels_read) {
        read_panels();
    }
    return magic >= 1 && magic <= cap_count ? caps[magic - 1] : 0;
}

int khdays_link_panel(int id, KhdaysLinkPanel *out)
{
    if (!panels_read) {
        read_panels();
    }
    if (id < 1 || id > panel_count || !Ov008_IsGridItemEligible(id) || panels[id - 1].category == CATEGORY_MATERIAL) {
        return 0;
    }
    *out = panels[id - 1];
    return 1;
}

int khdays_link_deck(unsigned short *cells)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return 0;
    }
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        cells[c] = (unsigned short)rd16(state + STATE_DECK + 2u * (unsigned)c);
    }
    return 1;
}

const char *khdays_link_deck_busy(void)
{
    int story;
    const int screen = khdays_camp_screen(&story);
    return screen == KHDAYS_CAMP_NONE || screen == CAMP_MENU ? NULL : "not now";
}

void khdays_link_set_deck(const unsigned short *cells)
{
    const unsigned state = khdays_link_state();
    for (int c = 0; state != 0 && c < KHDAYS_LINK_DECK_CELLS; ++c) {
        *(volatile unsigned short *)KHDAYS_W(state + STATE_DECK + 2u * (unsigned)c) = cells[c];
    }
}
