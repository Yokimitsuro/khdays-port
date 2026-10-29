/* The mods' log (mods_log.h). */
#include "mods_log.h"

#include "folders.h"
#include "../core/session_log.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *file;
static Uint64 opened;

void khdays_mods_log_open(const char *folder)
{
    static char started[1024] = ""; /* the folder whose log this run started: opened again, it goes on */
    char path[1024], previous[1024], logs[1024];
    khdays_mods_log_close();
    snprintf(logs, sizeof(logs), "%s/logs", folder);
    snprintf(path, sizeof(path), "%s/mods.log", logs);
    snprintf(previous, sizeof(previous), "%s/mods.prev.log", logs);
    if (strcmp(started, folder) == 0) {
        file = fopen(path, "a");
        return;
    }
    snprintf(started, sizeof(started), "%s", folder);
    khdays_folders_remove(previous);
    khdays_folders_rename(path, previous);
    /* the folder made through a first empty write */
    khdays_folders_write(path, "", 0);
    file = fopen(path, "w");
    opened = SDL_GetTicksNS();
}

static void one_line(const char *id, const char *kind, const char *where, const char *text, size_t n)
{
    const double seconds = (double)(SDL_GetTicksNS() - opened) / 1e9;
    if (file != NULL) {
        fprintf(file, "%.3f %s %s %s %.*s\n", seconds, id, kind, where, (int)n, text);
        fflush(file);
    }
    khdays_log("mods: %s %s %s %.*s", id, kind, where, (int)n, text);
}

void khdays_mods_log(const char *id, const char *kind, const char *file_name, int line, const char *text)
{
    char where[512];
    const char *p = text != NULL ? text : "";
    if (file_name != NULL && line > 0) {
        snprintf(where, sizeof(where), "%s:%d", file_name, line);
    } else if (file_name != NULL) {
        snprintf(where, sizeof(where), "%s", file_name);
    } else {
        snprintf(where, sizeof(where), "-");
    }
    for (int first = 1;; first = 0) {
        const char *eol = strchr(p, '\n');
        const size_t n = eol != NULL ? (size_t)(eol - p) : strlen(p);
        if (first || n > 0) {
            one_line(id, kind, first ? where : "-", p, n);
        }
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }
}

void khdays_mods_log_close(void)
{
    if (file != NULL) {
        fclose(file);
        file = NULL;
    }
}
