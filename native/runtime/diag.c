/* Diagnostics for the native game: a symbolized stack on a crash, and on
 * demand after a given time (KHDAYS_STALL_SECONDS=N) to see where a stalled
 * game is spinning. Symbols come from the build's PDB through DbgHelp. */
#include "../hal/hal.h"
#include "runtime.h"

extern unsigned khdays_cpsr;
static unsigned khdays_cpsr_value(void) { return khdays_cpsr; }

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <dbghelp.h>

static HANDLE game_thread;

static void print_stack(HANDLE thread, CONTEXT *context)
{
    HANDLE process = GetCurrentProcess();
    STACKFRAME64 frame = {0};
    char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
    frame.AddrPC.Offset = context->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 48; ++depth) {
        DWORD64 displacement = 0;
        DWORD line_displacement = 0;
        IMAGEHLP_LINE64 line = {sizeof(line)};
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, context, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            fprintf(stderr, "  #%-2d %08llx %s+0x%llx", depth, frame.AddrPC.Offset, symbol->Name,
                    displacement);
        } else {
            fprintf(stderr, "  #%-2d %08llx ?", depth, frame.AddrPC.Offset);
        }
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
            fprintf(stderr, "  (%s:%lu)", line.FileName, line.LineNumber);
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *info)
{
    fprintf(stderr, "khdays-native: exception 0x%08lx at %p", info->ExceptionRecord->ExceptionCode,
            info->ExceptionRecord->ExceptionAddress);
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        fprintf(stderr, " (%s 0x%08lx)",
                info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "reading",
                (unsigned long)info->ExceptionRecord->ExceptionInformation[1]);
    }
    fprintf(stderr, "\n");
    if (info->ContextRecord->Eip >= 0x01ff8000 && info->ContextRecord->Eip < 0x0b000000) {
        fprintf(stderr, "  the game jumped into DS memory at 0x%08lx: ARM code there (a routine it "
                        "copies at run time) cannot run natively\n",
                (unsigned long)info->ContextRecord->Eip);
    }
    if (info->ContextRecord->Eip == 0) {
        /* A call through a null pointer: resume the walk at its caller. */
        info->ContextRecord->Eip = *(DWORD *)info->ContextRecord->Esp;
        info->ContextRecord->Esp += 4;
        fprintf(stderr, "  (called through a null pointer from:)\n");
    }
    print_stack(GetCurrentThread(), info->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD WINAPI watchdog(void *parameter)
{
    CONTEXT context;
    Sleep((DWORD)(size_t)parameter * 1000);
    SuspendThread(game_thread);
    context.ContextFlags = CONTEXT_FULL;
    GetThreadContext(game_thread, &context);
    fprintf(stderr, "khdays-native: still running after %u s; the game thread is at:\n",
            (unsigned)(size_t)parameter);
    print_stack(game_thread, &context);
    /* The SDK's threads: OSi_ThreadInfo (0x02044330) current and list, each
     * OSThread with state at +0x64, next at +0x68, priority at +0x70. */
    {
        const unsigned current = *(volatile unsigned *)0x02044334;
        unsigned t = *(volatile unsigned *)0x02044338;
        fprintf(stderr, "SDK threads (current %08x, rescheduling %u, cpsr %02x):\n", current,
                *(volatile unsigned short *)0x02044330, khdays_cpsr_value());
        for (int n = 0; t != 0 && n < 32; ++n) {
            fprintf(stderr, "  %08x state %u priority %u\n", t, *(volatile unsigned *)(t + 0x64),
                    *(volatile unsigned *)(t + 0x70));
            t = *(volatile unsigned *)(t + 0x68);
        }
    }
    ExitProcess(5);
}

#if defined(_DEBUG)
#include <rtcapi.h>

/* The debug build's run-time checks (/RTC1) catch a local variable read
 * before any write. On the ARM9 such a read takes whatever the register held
 * -- the same leftover dependency as the ABI repairs (native/abi). Each site
 * is logged once, for review against the ROM, and the game goes on instead
 * of stopping at the CRT's dialog. */
static int __cdecl on_runtime_check(int type, const wchar_t *file, int line, const wchar_t *module,
                                    const wchar_t *format, ...)
{
    static const wchar_t *seen_file[256];
    static int seen_line[256];
    static int seen;
    (void)module;
    for (int i = 0; i < seen; ++i) {
        if (seen_line[i] == line && seen_file[i] == file) {
            return 0;
        }
    }
    if (seen < 256) {
        seen_file[seen] = file;
        seen_line[seen] = line;
        ++seen;
    }
    fwprintf(stderr, L"khdays-native: run-time check %d at %s:%d: ", type, file ? file : L"?", line);
    if (format != NULL) {
        va_list args;
        va_start(args, format);
        vfwprintf(stderr, format, args);
        va_end(args);
    }
    fwprintf(stderr, L"\n");
    fflush(stderr);
    return 0;  /* carry on */
}
#endif

/* Last in the vectored chain: an exception no runtime handler took, reported
 * before the structured handlers see it (a broken chain there hides it). */
static LONG CALLBACK on_unhandled_first(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *r = info->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION ||
        r->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        fprintf(stderr, "khdays-native: first-chance exception 0x%08lx at %p (eip %08lx, esp %08lx, info %lu %08lx)\n",
                r->ExceptionCode, r->ExceptionAddress, info->ContextRecord->Eip, info->ContextRecord->Esp,
                (unsigned long)r->ExceptionInformation[0], (unsigned long)r->ExceptionInformation[1]);
        print_stack(GetCurrentThread(), info->ContextRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void khdays_diag_init(void)
{
    AddVectoredExceptionHandler(0, on_unhandled_first);
    const char *stall = getenv("KHDAYS_STALL_SECONDS");
#if defined(_DEBUG)
    _RTC_SetErrorFuncW(on_runtime_check);
#endif
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    if (stall != NULL && atoi(stall) > 0) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &game_thread,
                        0, FALSE, DUPLICATE_SAME_ACCESS);
        CreateThread(NULL, 0, watchdog, (void *)(size_t)atoi(stall), 0, NULL);
    }
}

/* The name of the native function at `address`, for traces. */
static const char *symbol_name(const void *address)
{
    static char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
    DWORD64 displacement = 0;
    if (address == NULL) {
        return "-";
    }
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    if (SymFromAddr(GetCurrentProcess(), (DWORD64)(size_t)address, &displacement, symbol)) {
        return symbol->Name;
    }
    return "?";
}

/* KHDAYS_TRACE_SCRIPT: each step of the action-script interpreter
 * (src/calls/func_02020e58.c) that differs from the last one it reported:
 * the entry's command (opcode, sub-op, length word), its action slot and its
 * five callback slots. */
void khdays_trace_script(void *st, void *entry)
{
    static int enabled = -1;
    static u32 last[8];
    const u8 *e = (const u8 *)entry;
    const u8 *cmd = *(const u8 *const *)(e + 0x10);
    u32 now[8];
    if (enabled < 0) {
        enabled = getenv("KHDAYS_TRACE_SCRIPT") != NULL;
    }
    if (!enabled) {
        return;
    }
    now[0] = (u32)(size_t)cmd;
    now[1] = *(const u32 *)(e + 0x18);
    for (int i = 0; i < 5; ++i) {
        now[2 + i] = *(const u32 *)(e + 0x20 + 8 * i);
    }
    now[7] = *(const u32 *)((const u8 *)st + 0x124);
    if (memcmp(now, last, sizeof(now)) == 0) {
        return;
    }
    memcpy(last, now, sizeof(now));
    fprintf(stderr, "script %p depth %u: cmd %p", st, now[7], (const void *)cmd);
    if (cmd != NULL) {
        fprintf(stderr, " op %02x.%02x len %04x", cmd[0], cmd[1], *(const u16 *)(cmd + 2));
    }
    fprintf(stderr, " action %s", symbol_name((const void *)(size_t)now[1]));
    for (int i = 0; i < 5; ++i) {
        if (now[2 + i] != 0) {
            fprintf(stderr, " cb%d %s", i, symbol_name((const void *)(size_t)now[2 + i]));
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Obj_UpdateAll (func_02023adc, prepare.py HOOKS): an update returned
 * `next` as the object's next update. 0 keeps it, -1/-2 are markers; any
 * other value must be native code, or the next frame would jump to it. */
void khdays_check_update(int fn, int next)
{
    extern char __ImageBase;
    static int trace = -1;
    const IMAGE_NT_HEADERS *nt;
    const u8 *base = (const u8 *)&__ImageBase;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_UPDATES") != NULL;
    }
    if (trace && next != 0 && next != fn) {  /* KHDAYS_TRACE_UPDATES: every change of update */
        fprintf(stderr, "update %s -> ", symbol_name((const void *)(size_t)(u32)fn));
        fprintf(stderr, "%s\n", next == -1 ? "idle" : next == -2 ? "destroy"
                                                  : symbol_name((const void *)(size_t)(u32)next));
    }
    if (next == 0 || next == -1 || next == -2) {
        return;
    }
    nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    if ((const u8 *)(size_t)(u32)next >= base && (const u8 *)(size_t)(u32)next < base + nt->OptionalHeader.SizeOfImage) {
        return;
    }
    fprintf(stderr, "khdays-native: the update %s returned 0x%08x as the object's next update, "
                    "which is not code\n", symbol_name((const void *)(size_t)(u32)fn), (u32)next);
    fflush(stderr);
    exit(12);
}
