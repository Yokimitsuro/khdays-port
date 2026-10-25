/* Synthetic camp handles: shared NFTR data must never route a dialogue font. */
#include "../runtime/menus/camp.h"
#include "../runtime/core/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *khdays_window;
unsigned char *khdays_io_host;
/* Unrelated camp UI entry points must never run in this scope-only test. */
void khdays_log(const char *format, ...)
{
    (void)format;
    abort();
}
int GetLanguage(void)
{
    abort();
}
int GameState_GetField(int a, int b)
{
    (void)a;
    (void)b;
    abort();
}
int Ov008_GetLocalPlayerIndex(void)
{
    abort();
}
int Ov025_GetLocalPlayerCharacter(int a)
{
    (void)a;
    abort();
}
int GetFrameRateMode(void)
{
    abort();
}
int GameState_IsFlagSet(int a)
{
    (void)a;
    abort();
}
int Ov008_IsSessionReady(void)
{
    abort();
}
int khdays_language_wanted(void)
{
    abort();
}
uint32_t khdays_events_vblank_count(void)
{
    abort();
}
/* the Menu Overhaul's camp (the story's menu, its Files pages, its save screen): not installed, never run here */
typedef struct KhdaysRomReader KhdaysRomReader;
int khdays_mod_installed(int mod)
{
    (void)mod;
    return 0;
}
int PlaySound(int kind, int sound)
{
    (void)kind;
    (void)sound;
    abort();
}
int Ov025_GetContext(void)
{
    abort();
}
unsigned int Ov025_FindEntryById(int context, int id)
{
    (void)context;
    (void)id;
    abort();
}
void Ov025_SwapParamOverrides(int context, unsigned int entry)
{
    (void)context;
    (void)entry;
    abort();
}
void Ov025_Hub_OnEntryHover(unsigned int entry, unsigned int keys)
{
    (void)entry;
    (void)keys;
    abort();
}
void Ov025_Hub_SelectMenuGroup(unsigned int renderer, int group)
{
    (void)renderer;
    (void)group;
    abort();
}
KhdaysRomReader *khdays_rom_reader_new(void)
{
    abort();
}
int khdays_rom_file_data(KhdaysRomReader *reader, const char *path, int entry, unsigned char **data, unsigned *size)
{
    (void)reader;
    (void)path;
    (void)entry;
    (void)data;
    (void)size;
    abort();
}
void khdays_enemy_viewer_update(int on, int enemies, int cursor, int rows, const int16_t *ids)
{
    (void)on;
    (void)enemies;
    (void)cursor;
    (void)rows;
    (void)ids;
    abort();
}
uint16_t khdays_enemy_viewer_buttons(uint16_t buttons)
{
    return buttons;
}
int khdays_savelist_camp_save_screen(int overlay)
{
    (void)overlay;
    abort();
}
static int overlay;
int khdays_overlay_loaded(int id)
{
    return id == overlay;
}
void Ov008_RouteCommittedPageState(void) {}
int Ov025_GetIdleHandler(void)
{
    return 0;
}
int Ov008_SelectTierIfActive(int a, unsigned b, int c)
{
    return a + (int)b + c;
}
int Ov025_SelectTierIfActive(int a, unsigned b, int c)
{
    return a + (int)b + c;
}

int main(void)
{
    khdays_window = calloc(1, 0x02400000u);
    if (!khdays_window)
        return 1;
    const uint32_t context = 0x02100000u;
    const uint32_t globals[] = {0x02090f08u, 0x020b5748u, 0x020563e8u};
    const uint32_t screens[] = {0x02090f0cu, 0x020b574cu};
    const int overlays[] = {8, 25, 9};
    int failures = 0;
    char scope[80], expected[80];
    for (int mode = 0; mode < 3; ++mode) {
        overlay = overlays[mode];
        *(uint32_t *)KHDAYS_W(globals[mode]) = context;
        for (int page = -1; page < 8; ++page) {
            if (mode < 2)
                *(int *)KHDAYS_W(screens[mode]) = page;
            if (mode < 2)
                snprintf(expected, sizeof(expected), "camp.%s.screen.%d", mode == 0 ? "mission" : "story", page);
            else
                snprintf(expected, sizeof(expected), "camp.save");
            if (mode < 2 && page == 7)
                snprintf(expected, sizeof(expected), "camp.%s.screen.7.mode.0", mode == 0 ? "mission" : "story");
            for (int i = 0; i < 4; ++i) {
                khdays_camp_font_scope(context + 0x9680u + i * 12u, scope, sizeof(scope));
                failures += strcmp(scope, expected) != 0;
            }
            /* Same NFTR contents elsewhere and addresses between handles are ineligible. */
            khdays_camp_font_scope(context + 0x100u, scope, sizeof(scope));
            failures += scope[0] != 0;
            khdays_camp_font_scope(context + 0x9681u, scope, sizeof(scope));
            failures += scope[0] != 0;
        }
        if (mode < 2) {
            *(uint32_t *)KHDAYS_W(context + 0x9768u) = 1;
            khdays_camp_font_scope(context + 0x968cu, scope, sizeof(scope));
            snprintf(expected, sizeof(expected), "camp.%s.screen.7.mode.1", mode == 0 ? "mission" : "story");
            failures += strcmp(scope, expected) != 0;
            *(uint32_t *)KHDAYS_W(context + 0x9768u) = 0;
        }
        *(uint32_t *)KHDAYS_W(globals[mode]) = 0;
        khdays_camp_font_scope(context + 0x9680u, scope, sizeof(scope));
        failures += scope[0] != 0;
    }
    overlay = 2;
    khdays_camp_font_scope(context + 0x9680u, scope, sizeof(scope));
    failures += scope[0] != 0;
    const uint32_t shop_globals[] = {0x02090facu, 0x02091368u};
    for (int mode = 0; mode < 2; ++mode) {
        overlay = mode ? 26 : 8;
        *(uint32_t *)KHDAYS_W(shop_globals[mode]) = context;
        for (int i = 0; i < 3; ++i) {
            khdays_camp_font_scope(context + 0xc13cu + i * 12u, scope, sizeof(scope));
            failures += strcmp(scope, "shop") != 0;
        }
        khdays_camp_font_scope(context + 0xc13du, scope, sizeof(scope));
        failures += scope[0] != 0;
        overlay = 2;
        khdays_camp_font_scope(context + 0xc13cu, scope, sizeof(scope));
        failures += scope[0] != 0;
        *(uint32_t *)KHDAYS_W(shop_globals[mode]) = 0;
        overlay = mode ? 26 : 8;
        khdays_camp_font_scope(context + 0xc13cu, scope, sizeof(scope));
        failures += scope[0] != 0;
    }
    free(khdays_window);
    printf("Camp font scopes: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
