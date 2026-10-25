/* The session log (session_log.h). */
#include "session_log.h"
#include "../../host/app/instance.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static FILE *file;
static SRWLOCK lock = SRWLOCK_INIT;
static LARGE_INTEGER started, frequency;
static volatile LONG frame;
static u32 flushed_frame;
static u32 marks;
static volatile LONG wanted;   /* the settings ask for it */
static int opened_for_setting; /* this log was opened because they did */

/* The port's own messages (stderr) when nothing takes them -- the game opened from its shortcut has no
 * console, and a stop of the port's (an update returning no function, a register it does not model, the
 * sound driver's) says why only there, the session log just ending -- go to stderr-<the log's time>.log
 * beside it (buffered: exit(), which those stops call, writes the buffer out). Run from a console or with
 * stderr redirected, they go where they went. 1 when they were sent to `path`. */
static int keep_stderr(const char *path)
{
    static int done;
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (done) {
        return 0;
    }
    done = 1;
    if (h != NULL && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) {
        return 0;
    }
    if (freopen(path, "w", stderr) == NULL) {
        return 0;
    }
    setvbuf(stderr, NULL, _IOFBF, 16384);
    return 1;
}

static void open_now(void)
{
    char path[160], err_path[160];
    SYSTEMTIME now;
    /* saves/logs -- a further player's copy on the PC: saves/player<N>/logs */
    const char *folder = khdays_host_user_folder();
    CreateDirectoryA("saves", NULL);
    CreateDirectoryA(folder, NULL);
    snprintf(path, sizeof(path), "%s/logs", folder);
    CreateDirectoryA(path, NULL);
    GetLocalTime(&now);
    snprintf(path, sizeof(path), "%s/logs/session-%04u%02u%02u-%02u%02u%02u.log", folder, now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond);
    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "log: cannot write %s\n", path);
        return;
    }
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    snprintf(err_path, sizeof(err_path), "%s/logs/stderr-%04u%02u%02u-%02u%02u%02u.log", folder, now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond);
    const int kept = keep_stderr(err_path);
    fprintf(stderr, "log: this session's log is %s (F12 marks it)\n", path);
    fprintf(file, "khdays-native session log, %04u-%02u-%02u %02u:%02u:%02u\n", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond);
    if (kept) {
        fprintf(file, "log: the port's messages (stderr, its stops' reasons among them) go to %s\n", err_path);
    }
    fflush(file);
}

void khdays_log_open(void)
{
    static int registered;
    const char *setting = getenv("KHDAYS_LOG");
    int on = InterlockedCompareExchange(&wanted, 0, 0) != 0;
#if defined(_DEBUG)
    on = on || setting == NULL || strcmp(setting, "0") != 0;
#else
    on = on || (setting != NULL && strcmp(setting, "1") == 0);
#endif
    if (!registered) {
        registered = 1;
        atexit(khdays_log_close);
    }
    AcquireSRWLockExclusive(&lock);
    if (file == NULL && on) {
        open_now();
        opened_for_setting = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_log_want(int on)
{
    InterlockedExchange(&wanted, on != 0);
    AcquireSRWLockExclusive(&lock);
    if (on && file == NULL) {
        open_now();
        opened_for_setting = 1;
    } else if (!on && file != NULL && opened_for_setting) {
        fprintf(file, "log: turned off in the settings\n");
        fclose(file);
        file = NULL;
        opened_for_setting = 0;
        fprintf(stderr, "log: turned off in the settings\n");
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_log_on(void)
{
    return file != NULL;
}

void khdays_log_frame(u32 value)
{
    InterlockedExchange(&frame, (LONG)value);
    if (file != NULL && value - flushed_frame >= 60) {
        flushed_frame = value;
        AcquireSRWLockExclusive(&lock);
        if (file != NULL) {
            fflush(file);
        }
        ReleaseSRWLockExclusive(&lock);
    }
}

void khdays_log(const char *format, ...)
{
    LARGE_INTEGER now;
    va_list args;
    if (file == NULL) {
        return;
    }
    QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&lock);
    if (file != NULL) {
        fprintf(file, "%9.3f f%-6ld ", (double)(now.QuadPart - started.QuadPart) / (double)frequency.QuadPart,
                (long)frame);
        va_start(args, format);
        vfprintf(file, format, args);
        va_end(args);
        fputc('\n', file);
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_log_mark(void)
{
    if (file == NULL) {
        return;
    }
    ++marks;
    khdays_log("mark %u (F12)", marks);
    AcquireSRWLockExclusive(&lock);
    fflush(file);
    ReleaseSRWLockExclusive(&lock);
    fprintf(stderr, "log: mark %u\n", marks);
}

void khdays_log_close(void)
{
    /* a crash may come inside a line, the lock held: then only flushed */
    const BOOLEAN locked = TryAcquireSRWLockExclusive(&lock);
    if (file != NULL) {
        fflush(file);
        if (locked) {
            fclose(file);
            file = NULL;
        }
    }
    if (locked) {
        ReleaseSRWLockExclusive(&lock);
    }
}
