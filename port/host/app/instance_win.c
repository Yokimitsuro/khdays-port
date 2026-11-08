/* host/app/instance.h on Windows. Each slot is a named mutex in the session's
 * namespace, its install's own (the system releases it when the process ends).
 * Other copies are found by looking for processes of the same executable. */
#include "instance.h"

#include <windows.h>
#include <tlhelp32.h>

#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>

static int slot;        /* 0 until a slot is taken */
static char folder[64]; /* "saves" or "saves/player<N>" */

/* The install the copy plays from: its working folder (where saves/ is), case folded and hashed (FNV-1a). The
 * slots are each install's own: a copy of another folder -- another install, a developer's build, one left
 * hanging -- does not make this one player 2, with player 2's saves and no gamepad. */
static unsigned int install_key(void)
{
    wchar_t here[MAX_PATH];
    unsigned int digest = 2166136261u;
    const DWORD length = GetCurrentDirectoryW(MAX_PATH, here);
    for (DWORD i = 0; i < length && i < MAX_PATH; ++i) {
        const wchar_t c = here[i] == L'/' ? L'\\' : here[i];
        digest = (digest ^ (unsigned int)towlower(c)) * 16777619u;
    }
    return digest;
}

/* Tries to take slot `number` of this install. Returns 1 if it is ours now
 * (its lock stays open), or 0 if another copy holds it. If the system cannot
 * make the lock at all, the slot counts as ours, so at least one copy can play. */
static int claim(int number)
{
    wchar_t name[64];
    HANDLE lock;
    swprintf(name, 64, L"Local\\khdays-player-%08x-%d", install_key(), number);
    lock = CreateMutexW(NULL, FALSE, name);
    if (lock != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(lock);
        return 0;
    }
    return 1;
}

int khdays_host_player_slot(void)
{
    char value[16];
    if (slot != 0) {
        return slot;
    }
    /* named by the environment: the walks run as player 1 whatever else is running */
    if (GetEnvironmentVariableA("KHDAYS_PLAYER_SLOT", value, sizeof(value)) > 0 && atoi(value) > 0) {
        slot = atoi(value);
        return slot;
    }
    for (int number = 1;; ++number) {
        if (claim(number)) {
            slot = number;
            break;
        }
    }
    if (slot > 1) {
        fprintf(stderr, "khdays-native: another copy runs: this one is player %d (saves/player%d)\n", slot, slot);
    }
    return slot;
}

int khdays_host_player_slot_taken(void)
{
    return slot;
}

const char *khdays_host_user_folder(void)
{
    if (slot == 0) {
        return "saves"; /* no slot taken (the tools and tests): use slot 1's */
    }
    if (folder[0] == '\0') {
        const int number = slot;
        if (number == 1) {
            snprintf(folder, sizeof(folder), "saves");
        } else {
            snprintf(folder, sizeof(folder), "saves/player%d", number);
            CreateDirectoryA("saves", NULL);
            CreateDirectoryA(folder, NULL);
        }
    }
    return folder;
}

int khdays_host_other_copies(void)
{
    /* count the processes of the same executable, other than this one */
    wchar_t own[MAX_PATH];
    const wchar_t *own_name;
    const DWORD self = GetCurrentProcessId();
    PROCESSENTRY32W entry;
    HANDLE snapshot;
    int others = 0;
    if (GetModuleFileNameW(NULL, own, MAX_PATH) == 0) {
        return 0;
    }
    own_name = wcsrchr(own, L'\\') != NULL ? wcsrchr(own, L'\\') + 1 : own;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID != self && _wcsicmp(entry.szExeFile, own_name) == 0) {
                ++others;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return others > 0;
}

/* "Player" and four digits from this machine: a hash of the computer's name
 * and of the user folder, which reveals neither. */
void khdays_host_automatic_name(char *out, size_t size)
{
    wchar_t machine[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
    unsigned int digest = 2166136261u; /* FNV-1a */
    const char *user = khdays_host_user_folder();
    if (GetComputerNameW(machine, &length)) {
        for (DWORD i = 0; i < length; ++i) {
            digest = (digest ^ (unsigned int)machine[i]) * 16777619u;
        }
    }
    for (const char *c = user; *c != '\0'; ++c) {
        digest = (digest ^ (unsigned char)*c) * 16777619u;
    }
    snprintf(out, size, "Player%04u", digest % 10000u);
}

int khdays_host_player_name(const char *nickname, unsigned short *name, int room)
{
    char chosen[256];
    int length = 0;
    const int number = slot > 1 ? slot : 0;
    const DWORD named = GetEnvironmentVariableA("KHDAYS_NAME", chosen, sizeof(chosen));
    if (named == 0 || named >= sizeof(chosen)) { /* none, or too long to have been copied */
        if (nickname != NULL && nickname[0] != '\0') {
            snprintf(chosen, sizeof(chosen), "%s", nickname);
        } else {
            khdays_host_automatic_name(chosen, sizeof(chosen));
        }
    }
    for (const char *c = chosen; *c != '\0' && length < room; ++c) {
        if (*c >= 0x20 && *c < 0x7f) {
            name[length++] = (unsigned short)*c;
        }
    }
    if (number > 0 && room > 0) {
        /* player N's copy on this PC: the number goes last, replacing the
         * name's last letter if the name is full */
        if (length >= room) {
            length = room - 1;
        }
        name[length++] = (unsigned short)('0' + number % 10);
    }
    return length;
}
