/* Diagnostics for the native game: a symbolized stack on a crash, and on
 * demand after a given time (KHDAYS_STALL_SECONDS=N) to see where a stalled
 * game is spinning. Symbols come from the build's PDB through DbgHelp. */
#include "runtime.h"

extern unsigned khdays_cpsr;
static unsigned khdays_cpsr_value(void) { return khdays_cpsr; }

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

void khdays_diag_init(void)
{
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
