#include "../runtime/core/window.h"
#include "../runtime/menus/camp.h"
#include "../runtime/input/input.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#undef PlaySound
unsigned char *khdays_window;
unsigned char io_bytes[0x10000];
unsigned char *khdays_io_host = io_bytes;
static int failures;
static int overlay, played_sounds;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("camp_memory_test:%d failed: %s\n", __LINE__, #c);                                                  \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
void khdays_camp_remember_entry(int story, int entry);
int khdays_camp_restore_entry(int story, int fallback);
void Ov008_RouteCommittedPageState(void) {}
void Ov025_GetIdleHandler(void) {}
int Ov008_SelectTierIfActive(int a, unsigned b, int c)
{
    (void)a;
    (void)b;
    (void)c;
    return 0;
}
int Ov025_SelectTierIfActive(int a, unsigned b, int c)
{
    (void)a;
    (void)b;
    (void)c;
    return 0;
}
void khdays_log(const char *format, ...)
{
    (void)format;
}
int GetLanguage(void)
{
    return 1;
}
/* the game state's fields the camp reads: the day (0), the chapter (0x44e), the Files' seen words */
static int fields_at[8], fields_value[8], fields_n;
static void set_field(int at, int value)
{
    for (int i = 0; i < fields_n; ++i)
        if (fields_at[i] == at) {
            fields_value[i] = value;
            return;
        }
    fields_at[fields_n] = at;
    fields_value[fields_n++] = value;
}
int GameState_GetField(int a, int b)
{
    (void)b;
    for (int i = 0; i < fields_n; ++i)
        if (fields_at[i] == a)
            return fields_value[i];
    return 0;
}
int Ov008_GetLocalPlayerIndex(void)
{
    return 0;
}
int Ov025_GetLocalPlayerCharacter(int a)
{
    (void)a;
    return 12;
}
int GetFrameRateMode(void)
{
    return 0;
}
int GameState_IsFlagSet(int a)
{
    (void)a;
    return 0;
}
int Ov008_IsSessionReady(void)
{
    return 1;
}
int khdays_language_wanted(void)
{
    return 1;
}
unsigned khdays_events_vblank_count(void)
{
    return 500;
}
int khdays_overlay_loaded(int a)
{
    return a == overlay;
}
int PlaySound(int a, int b)
{
    (void)a;
    (void)b;
    ++played_sounds;
    return 1;
}
int khdays_camp_navigation_sound(int kind, int sound);
/* no mods' characters (runtime/characters/characters.h): the members are the game's */
int khdays_characters_known(int number)
{
    (void)number;
    return 0;
}
/* the Menu Overhaul installed (runtime/mods/installed.h): its camp's memory and sounds tested */
int khdays_mod_installed(int mod)
{
    (void)mod;
    return 1;
}
/* the story menu's cursor put back (camp.c story_cursor): the game's functions it calls */
static int hovered = -1;
int Ov025_GetContext(void)
{
    return 0;
}
unsigned Ov025_FindEntryById(int c, int id)
{
    (void)c;
    return 0x02080000u + (unsigned)id;
}
void Ov025_SwapParamOverrides(int c, unsigned e)
{
    (void)c;
    (void)e;
}
void Ov025_Hub_OnEntryHover(unsigned e, unsigned k)
{
    (void)k;
    hovered = (int)(e - 0x02080000u);
    *(int *)KHDAYS_W(0x02100000u + 0x88) = hovered;
}
static int group_opened = -1;
/* the Enemy Profiles' model viewer (enemy_viewer.c): what the camp hands it */
static int viewer_on = -1, viewer_enemies = -1, viewer_cursor = -2, viewer_rows = -1;
void khdays_enemy_viewer_update(int on, int enemies, int cursor, int rows, const int16_t *ids)
{
    (void)ids;
    viewer_on = on;
    viewer_enemies = enemies;
    viewer_cursor = cursor;
    viewer_rows = rows;
}
uint16_t khdays_enemy_viewer_buttons(uint16_t b)
{
    return b;
}
int khdays_savelist_camp_save_screen(int camp_overlay)
{
    (void)camp_overlay;
    return -1;
}
void Ov025_Hub_SelectMenuGroup(unsigned r, int g)
{
    group_opened = g;
    *(int *)KHDAYS_W(r + 0x88) = g == 2 ? 12 : g == 1 ? 10 : 1;
    *(int *)KHDAYS_W(r + 0x94) = g != 0;
}
/* the Files pages' tables in the ROM (UI/cm/str/rpt_&.z, enm_&.z): the records' first halfwords */
typedef struct KhdaysRomReader KhdaysRomReader;
KhdaysRomReader *khdays_rom_reader_new(void)
{
    return (KhdaysRomReader *)&failures;
}
int khdays_rom_file_data(KhdaysRomReader *r, const char *path, int entry, unsigned char **data, unsigned *size)
{
    static const uint16_t diary[] = {1, 2, 5, 9, 2}, enemies[] = {3, 7};
    const int enemy = strstr(path, "enm_") != NULL;
    const uint16_t *ids = enemy ? enemies : diary;
    const unsigned n = enemy ? 2u : 5u;
    (void)r;
    (void)entry;
    if (strstr(path, "_en.z") == NULL)
        return 0;
    *size = 4 + n * 0x14;
    *data = calloc(1, *size);
    (*data)[0] = (unsigned char)n;
    for (unsigned k = 0; k < n; ++k) {
        (*data)[4 + k * 0x14] = (unsigned char)ids[k];
        (*data)[5 + k * 0x14] = (unsigned char)(ids[k] >> 8);
    }
    return 1;
}

static unsigned setup(int story)
{
    const unsigned context = 0x02010000u + (story ? 0 : 0x20000u);
    const unsigned first = 0x02070000u + (story ? 0 : 0x2000u);
    const unsigned global = story ? 0x020b5744u : 0x02090f04u;
    *(unsigned *)KHDAYS_W(global + 4) = context;
    *(unsigned *)KHDAYS_W(context + 0x4a38) = first;
    *(uint16_t *)KHDAYS_W(context + 0x4a38 + 8) = 7;
    *(uint16_t *)KHDAYS_W(context + 0x4a38 + 10) = 0;
    for (unsigned n = 0; n < 7; ++n) {
        const unsigned node = first + n * 0x100;
        *(unsigned *)KHDAYS_W(node + 4) = n == 6 ? 0 : node + 0x100;
        *(int *)KHDAYS_W(node + 0xc) = story ? (n == 6 ? 9 : (int)n + 1) : (int)n + 101;
    }
    return first;
}

int main(void)
{
    khdays_window = calloc(1, 0x02400000);
    if (!khdays_window)
        return 2;
    const unsigned story = setup(1);
    setup(0);
    CHECK(khdays_camp_restore_entry(1, 1) == 1);
    khdays_camp_remember_entry(1, 5);
    CHECK(khdays_camp_restore_entry(1, 1) == 5);
    khdays_camp_remember_entry(1, 7);
    CHECK(khdays_camp_restore_entry(1, 1) == 5);
    *(unsigned char *)KHDAYS_W(story + 4 * 0x100 + 0x84) = 1;
    CHECK(khdays_camp_restore_entry(1, 1) == 1); /* a now-locked option is not restored */
    *(unsigned char *)KHDAYS_W(story + 4 * 0x100 + 0x84) = 0;
    CHECK(khdays_camp_restore_entry(1, 1) == 5);
    khdays_camp_remember_entry(1, 10);
    CHECK(khdays_camp_restore_entry(1, 1) == 2);
    khdays_camp_remember_entry(1, 13);
    CHECK(khdays_camp_restore_entry(1, 1) == 3); /* Files is the story's entry 3 */
    khdays_camp_remember_entry(0, 105);
    CHECK(khdays_camp_restore_entry(0, 101) == 105);
    CHECK(khdays_camp_restore_entry(1, 1) == 3); /* the two modes keep separate memories */
    /* Two hidden DS tabs share one visible section. A single DOWN crosses tabs:
     * one cursor click is heard, and no synthetic page/row sounds. */
    overlay = 25;
    const unsigned context = 0x02010000u, state = 0x02100000u;
    *(unsigned *)KHDAYS_W(context + 0x959c) = state;
    *(unsigned *)KHDAYS_W(0x0204be18) = 0x02110000u;
    *(int *)KHDAYS_W(0x020b574c) = 5;
    *(int *)KHDAYS_W(0x020b5750) = 0;
    *(unsigned char *)KHDAYS_W(0x020b4ee8 + 2) = 2;
    *(unsigned char *)KHDAYS_W(0x020b4ee8 + 3) = 0;
    *(unsigned char *)KHDAYS_W(0x020b4ee8 + 4) = 1;
    for (unsigned n = 0; n < 2; ++n) {
        *(uint16_t *)KHDAYS_W(0x020b4ed0 + 4 * n) = (uint16_t)n;
        *(uint16_t *)KHDAYS_W(0x020b4ed0 + 4 * n + 2) = 1;
        *(unsigned char *)KHDAYS_W(0x020b4f64 + 20 * n + 6) = 1;
        *(unsigned char *)KHDAYS_W(0x020b4f64 + 20 * n + 7) = 2;
    }
    khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
    khdays_camp_set_page_drawn(1);
    khdays_camp_set_files_drawn(1);
    played_sounds = 0;
    const uint16_t keys = khdays_camp_buttons(KHDAYS_KEY_DOWN);
    CHECK(keys == KHDAYS_KEY_R && played_sounds == 1);
    CHECK(!khdays_camp_navigation_sound(0, 0) && !khdays_camp_navigation_sound(0, 2));
    CHECK(khdays_camp_navigation_sound(0, 4)); /* genuine error cues still work */
    *(uint16_t *)KHDAYS_W(state) = 1;
    *(uint16_t *)KHDAYS_W(state + 0x4c) = 1;
    khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
    CHECK(khdays_camp_buttons(0) == 0);
    CHECK(khdays_camp_navigation_sound(0, 0));
    {
        /* B in Files' two options: the game puts its cursor on the progress's default (entry 1), the
         * port puts it back on Files; opening Files again, on the option chosen there last */
        *(int *)KHDAYS_W(0x020b574c) = 0;
        *(int *)KHDAYS_W(0x020b5750) = 0;
        *(int *)KHDAYS_W(state + 0x5c0) = 0;
        *(int *)KHDAYS_W(state + 0x88) = 13;
        khdays_camp_remember_entry(1, 13);
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        *(int *)KHDAYS_W(state + 0x88) = 1;
        hovered = -1;
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        CHECK(hovered == 3);
        *(int *)KHDAYS_W(state + 0x88) = 12;
        hovered = -1;
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        CHECK(hovered == 13);
    }
    {
        /* the Files page (screen 7): Roxas's Diary's rows, the chosen one, the NEW badges */
        static const uint16_t entry1[] = {'E', 'n', 't', 'r', 'y', ' ', '1'}, other1[] = {'E', 'x', 't', 'r', 'a'};
        const unsigned entries = 0x02120000u, texts = 0x02130000u;
        KhdaysCampView v;
        KhdaysCampLists lists = {0}; /* the rows and titles, the reader's own copy */
        *(int *)KHDAYS_W(0x020b574c) = 7;
        *(int *)KHDAYS_W(0x020b5750) = 6;
        *(int *)KHDAYS_W(context + 0x9768) = 0;
        memset(KHDAYS_W(state), 0, 0x278);
        *(int16_t *)KHDAYS_W(state + 0) = 0;
        *(int16_t *)KHDAYS_W(state + 2) = 1;
        *(int16_t *)KHDAYS_W(state + 4) = 3;
        *(int *)KHDAYS_W(state + 0xc8) = 1;
        *(unsigned *)KHDAYS_W(state + 0x1e8) = entries;
        memcpy(KHDAYS_W(texts), entry1, sizeof(entry1));
        memcpy(KHDAYS_W(texts + 0x100), other1, sizeof(other1));
        for (unsigned n = 0; n < 3; ++n) {
            *(uint16_t *)KHDAYS_W(entries + 0x40 * n) = (uint16_t)(7 + n);
            *(uint16_t *)KHDAYS_W(entries + 0x40 * n + 2) = 7;
            *(unsigned *)KHDAYS_W(entries + 0x40 * n + 0x10) = texts;
        }
        *(unsigned *)KHDAYS_W(entries + 0x40 + 0xc) = 0x02120400u; /* row 1 has another version */
        *(uint16_t *)KHDAYS_W(0x02120400u + 2) = 5;
        *(unsigned *)KHDAYS_W(0x02120400u + 0x10) = texts + 0x100;
        *(unsigned *)KHDAYS_W(state + 0x238) = 0x80000000u; /* row 0 viewed on this visit */
        *(unsigned *)KHDAYS_W(state + 0x248) = 0x20000000u; /* row 2 seen before */
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view_lists(&v, &lists);
        CHECK(v.shown && v.files && v.file.mode == 0 && v.file.count == 3 && v.file.cursor == 1);
        CHECK(v.file.row[0].number == 7 && v.file.row[2].number == 9);
        CHECK(v.file.row[0].title[0] == 'E' && v.file.row[0].title[6] == '1' && v.file.row[0].title[7] == 0);
        CHECK(v.file.row[0].badge == 1 && v.file.row[1].badge == 0 && v.file.row[2].badge == -1);
        CHECK(v.description == 13);
        CHECK(viewer_on == 1 && viewer_enemies == 0 && viewer_rows == 0 && viewer_cursor == 1);
        *(int *)KHDAYS_W(state + 0xc0) = 1; /* A held: the chosen row shows its other version */
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view_lists(&v, &lists);
        CHECK(v.file.rows == 3 && v.file.row[1].title[0] == 'E' && v.file.row[1].title[1] == 'x' &&
              v.file.row[1].title[5] == 0);
        CHECK(v.file.held && v.file.row[1].alternate && !v.file.row[0].alternate);
        *(int *)KHDAYS_W(state + 0xc8) = 0; /* B: left, the game still showing it (fading to the menu): read on */
        *(int *)KHDAYS_W(context + 0x95c4) = 0;
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view(&v);
        CHECK(v.files);
        *(unsigned *)KHDAYS_W(state + 0x1e8) = 0; /* torn down: not read */
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view(&v);
        CHECK(!v.files);
        *(unsigned *)KHDAYS_W(state + 0x1e8) = entries;
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view(&v);
        CHECK(!v.files); /* not running, and not the page left just now: the menu's words */
        khdays_camp_lists_free(&lists);
    }
    {
        /* B on the page: back on the menu, Files' two open, the cursor on the page's (Enemy Profiles) */
        KhdaysCampView v;
        *(int *)KHDAYS_W(context + 0x9768) = 1;
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler); /* still on the page (mode 1) */
        *(int *)KHDAYS_W(0x020b574c) = -1;
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        memset(KHDAYS_W(state), 0, 0x600);
        *(int *)KHDAYS_W(0x020b574c) = 0;
        *(int *)KHDAYS_W(0x020b5750) = 0;
        *(int *)KHDAYS_W(state + 0x88) = 3; /* the menu made: the cursor on Files */
        group_opened = -1;
        hovered = -1;
        set_field(0, 6);                 /* day 6: the diary's first two entries (days 1, 2; day 5's next is day 9) */
        set_field(0x45b + 0x10, 0x8000); /* the first entry seen */
        *(unsigned char *)KHDAYS_W(0x02110000u + 0xa11 + 3) = 1; /* enemy 3 met, never viewed */
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view(&v);
        CHECK(group_opened == 2 && hovered == 13);
        CHECK(v.shown && v.sub == 2 && v.selected == 1);
        CHECK(v.files_new == 3);
        group_opened = -1;
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        CHECK(group_opened == -1);        /* once */
        set_field(0x45b + 0x10, 0xc000);  /* both seen */
        set_field(0x3c6a + 0x10, 0x8000); /* the enemy too */
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view(&v);
        CHECK(v.files_new == 0);
        /* the last chapter: day 359, the diary's four entries; the day-2 one rewritten (its row had its other
         * version on the page): viewed as entry 0x41 */
        set_field(0x44e, 6);
        set_field(0x45b + 0x10, 0xf000);
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view(&v);
        CHECK(v.files_new == 1);
        set_field(0x45b + 0x50, 0x4000);
        khdays_camp_update_ran((uintptr_t)Ov025_GetIdleHandler);
        khdays_camp_view(&v);
        CHECK(v.files_new == 0);
    }
    free(khdays_window);
    if (!failures)
        puts(
            "camp_memory_test: cursor memory, locks, nested pages, silent synthetic Config navigation, the Files page, the way back from it and its NEW passed");
    return failures != 0;
}
