/* The game's language changed while it runs (language.h): gLangPath's first halfword (1-5), read at
 * each '&' path; a switch waits for the scene on show to end, so every scene is built in one language.
 * KHDAYS_LANGUAGE_SWITCH=frame:language[,...] (test runs only) requests switches by itself. */
#include "language.h"

#include "../../hal/hal.h"
#include "events.h"
#include "rom.h"
#include "session_log.h"
#include "window.h"

#include "khdays_ds_symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define FIRMWARE_LANGUAGE 0x027ffc80u + 0x64u
#define DATABASES 0x21 /* MsgDb_InitTable's entries */
#define ENTRY_BYTES 0x14
#define SCENE_CURRENT 0x8u
#define SCENE_PENDING 0xcu

static volatile LONG requested; /* 0: none (any thread) */
static int pending;             /* the language waiting for the scene's end (the game's thread) */

/* The test knob's switches. */
static u32 (*switches)[2];
static int switch_count, switches_read;

static u32 rd32(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

void khdays_language_request(int language)
{
    if (language >= 1 && language <= 5) {
        InterlockedExchange(&requested, khdays_rom_language(language));
    }
}

/* KHDAYS_LANGUAGE_SWITCH: "frame:language,frame:language". */
static void read_switches(void)
{
    const char *text = getenv("KHDAYS_LANGUAGE_SWITCH");
    switches_read = 1;
    while (text != NULL && *text != '\0') {
        unsigned frame = 0, language = 0;
        int used = 0;
        if (sscanf_s(text, "%u:%u%n", &frame, &language, &used) != 2) {
            fprintf(stderr, "language: KHDAYS_LANGUAGE_SWITCH: \"%s\" is not frame:language\n", text);
            return;
        }
        {
            u32(*grown)[2] = realloc(switches, (size_t)(switch_count + 1) * sizeof(*switches));
            if (grown == NULL) {
                return;
            }
            switches = grown;
            switches[switch_count][0] = frame;
            switches[switch_count][1] = language;
            ++switch_count;
        }
        text += used;
        if (*text == ',') {
            ++text;
        }
    }
}

void khdays_language_frame(void)
{
    LONG language;
    if (!switches_read) {
        read_switches();
    }
    for (int s = 0; s < switch_count; ++s) {
        if (switches[s][0] != 0 && khdays_events_vblank_count() >= switches[s][0]) {
            khdays_language_request((int)switches[s][1]);
            switches[s][0] = 0; /* done */
        }
    }
    language = InterlockedExchange(&requested, 0);
    if (language != 0 && language != pending) {
        pending = (int)language;
        khdays_log("language: %d asked for in scene %u: the game's text follows when the scene ends", pending,
                   rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT));
    }
}

int khdays_language_wanted(void)
{
    const LONG asked = InterlockedCompareExchange(&requested, 0, 0); /* read, not taken */
    return asked != 0 ? (int)asked : pending;
}

void khdays_language_scene_ending(void)
{
    const u32 path = KHDAYS_DS_gLangPath, table = rd32(KHDAYS_DS_data_0204c238);
    int before, loaded = 0;
    if (pending == 0) {
        return;
    }
    before = *(const volatile u16 *)KHDAYS_W(path);
    *(volatile u16 *)KHDAYS_W(path) = (u16)pending;
    *(volatile u16 *)KHDAYS_W(FIRMWARE_LANGUAGE) = (u16)pending;
    for (int id = 0; table != 0 && id < DATABASES; ++id) {
        loaded += rd32(table + (u32)id * ENTRY_BYTES) != 0;
    }
    khdays_log("language: %d -> %d as the scene ends (the next: %u); %d message database(s) still loaded", before,
               pending, rd32(KHDAYS_DS_gSceneCtl + SCENE_PENDING), loaded);
    pending = 0;
}
