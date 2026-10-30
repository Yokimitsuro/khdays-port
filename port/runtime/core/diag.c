/* Diagnostics (runtime.h): a named stack on a crash, where a stalled game spins
 * (KHDAYS_STALL_SECONDS=N), memory watches and update traces. Names come from the build's PDB through
 * DbgHelp. */
#include "../../hal/hal.h"
#include "runtime.h"
#include "events.h"
#include "../menus/camp.h"
#include "../menus/charselect.h"
#include "../menus/cinelist.h"
#include "../menus/daycard.h"
#include "../menus/shop.h"
#include "../display/field_wipe.h"
#include "../net/roomlist.h"
#include "../../ui/situation.h"
#include "../input/input.h"
#include "session_log.h"
#include "window.h"
#include "io.h"

/* the HAL's, in the rewritten game's data area */
#include "khdays_data_area.h"
static unsigned khdays_cpsr_value(void)
{
    return *(const unsigned *)KHDAYS_W(KHDAYS_DATA_khdays_cpsr);
}

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <dbghelp.h>

static HANDLE game_thread;
static const char *symbol_name(const void *address);

/* The instruction, stack and frame pointers of a thread's context, and the
 * machine type DbgHelp walks it as. */
#define CONTEXT_PC(c) ((c)->Rip)
#define CONTEXT_SP(c) ((c)->Rsp)
#define CONTEXT_FP(c) ((c)->Rbp)
#define MACHINE IMAGE_FILE_MACHINE_AMD64
typedef DWORD64 Word;

static void print_stack(HANDLE thread, CONTEXT *context)
{
    HANDLE process = GetCurrentProcess();
    STACKFRAME64 frame = {0};
    char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
    frame.AddrPC.Offset = CONTEXT_PC(context);
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = CONTEXT_FP(context);
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = CONTEXT_SP(context);
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 48; ++depth) {
        DWORD64 displacement = 0;
        DWORD line_displacement = 0;
        IMAGEHLP_LINE64 line = {sizeof(line)};
        if (!StackWalk64(MACHINE, process, thread, &frame, context, NULL, SymFunctionTableAccess64, SymGetModuleBase64,
                         NULL) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            fprintf(stderr, "  #%-2d %08llx %s+0x%llx", depth, frame.AddrPC.Offset, symbol->Name, displacement);
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
    khdays_log("crash: exception 0x%08lx at %p", info->ExceptionRecord->ExceptionCode,
               info->ExceptionRecord->ExceptionAddress);
    khdays_log_close();
    fprintf(stderr, "khdays-native: exception 0x%08lx at %p", info->ExceptionRecord->ExceptionCode,
            info->ExceptionRecord->ExceptionAddress);
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        fprintf(stderr, " (%s 0x%08lx)", info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "reading",
                (unsigned long)info->ExceptionRecord->ExceptionInformation[1]);
    }
    fprintf(stderr, "\n");
    {
        const CONTEXT *c = info->ContextRecord;
        fprintf(stderr,
                "  rax %016llx rbx %016llx rcx %016llx rdx %016llx\n  rsi %016llx rdi %016llx rbp %016llx "
                "rsp %016llx\n  r8 %016llx r9 %016llx r12 %016llx r13 %016llx\n",
                c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp, c->R8, c->R9, c->R12, c->R13);
        if (CONTEXT_FP(c) > CONTEXT_SP(c) && CONTEXT_FP(c) - CONTEXT_SP(c) < 0x1000 &&
            !IsBadReadPtr((const void *)(size_t)CONTEXT_SP(c), (UINT_PTR)(CONTEXT_FP(c) - CONTEXT_SP(c) + 0x20))) {
            for (Word at = CONTEXT_SP(c) & ~(Word)15; at < CONTEXT_FP(c) + 0x20; at += 16) {
                const DWORD *w = (const DWORD *)(size_t)at;
                fprintf(stderr, "  %p: %08lx %08lx %08lx %08lx\n", (const void *)(size_t)at, w[0], w[1], w[2], w[3]);
            }
        }
    }
    {
        const Word pc = CONTEXT_PC(info->ContextRecord);
        if (pc >= 0x01ff8000 && pc < 0x0b000000) {
            fprintf(stderr,
                    "  the game jumped to DS address 0x%08lx as a host address: a function reference "
                    "called without its native function (KHDAYS_FN)\n",
                    (unsigned long)pc);
        } else if (khdays_window != NULL && pc >= (Word)(size_t)khdays_window + 0x01ff8000 &&
                   pc < (Word)(size_t)khdays_window + 0x0b000000) {
            fprintf(stderr,
                    "  the game jumped into its memory at DS address 0x%08lx: ARM code there (a routine "
                    "it copies at run time) cannot run natively\n",
                    (unsigned long)(pc - (Word)(size_t)khdays_window));
        }
    }
    if (CONTEXT_PC(info->ContextRecord) == 0) {
        /* a call through a null pointer: go on with the walk at its caller */
        CONTEXT_PC(info->ContextRecord) = *(const Word *)(size_t)CONTEXT_SP(info->ContextRecord);
        CONTEXT_SP(info->ContextRecord) += sizeof(Word);
        fprintf(stderr, "  (called through a null pointer from:)\n");
    }
    print_stack(GetCurrentThread(), info->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* KHDAYS_DUMP="ADDRESS:LENGTH ...": with the stall report, prints DS memory as
 * words. `*ADDRESS+OFFSET:LENGTH` dumps from the pointer stored at ADDRESS,
 * plus OFFSET (numbers in C notation). */
static void dump_memory(void)
{
    const char *p = getenv("KHDAYS_DUMP");
    while (p != NULL && *p != '\0') {
        char *end;
        const int indirect = *p == '*';
        unsigned address, offset = 0, length;
        if (*p == ' ') {
            ++p;
            continue;
        }
        address = (unsigned)strtoul(p + indirect, &end, 0);
        if (*end == '+') {
            offset = (unsigned)strtoul(end + 1, &end, 0);
        }
        length = *end == ':' ? (unsigned)strtoul(end + 1, &end, 0) : 4;
        p = end;
        if (indirect) {
            if (IsBadReadPtr(KHDAYS_W(address), 4)) {
                fprintf(stderr, "*%08x: not readable\n", address);
                continue;
            }
            address = *(volatile unsigned *)KHDAYS_W(address);
        }
        address += offset;
        if (IsBadReadPtr(KHDAYS_W(address), length)) {
            fprintf(stderr, "%08x: not readable\n", address);
            continue;
        }
        for (unsigned at = 0; at < length; at += 4) {
            if (at % 32 == 0) {
                fprintf(stderr, "%s%08x:", at ? "\n" : "", address + at);
            }
            fprintf(stderr, " %08x", *(volatile unsigned *)KHDAYS_W(address + at));
        }
        fprintf(stderr, "\n");
    }
}

/* KHDAYS_STALL_SAMPLES=N: before the stall report, N samples of the game thread's stack a millisecond
 * apart, and the functions found on them most often. */
static void sample_stacks(int samples)
{
    enum { MAX_FUNCTIONS = 512 };
    static DWORD64 function[MAX_FUNCTIONS];
    static int hits[MAX_FUNCTIONS];
    int count = 0;
    HANDLE process = GetCurrentProcess();
    for (int s = 0; s < samples; ++s) {
        CONTEXT context;
        STACKFRAME64 frame = {0};
        DWORD64 seen[48];
        int depth = 0;
        Sleep(1);
        SuspendThread(game_thread);
        context.ContextFlags = CONTEXT_FULL;
        GetThreadContext(game_thread, &context);
        frame.AddrPC.Offset = CONTEXT_PC(&context);
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = CONTEXT_FP(&context);
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = CONTEXT_SP(&context);
        frame.AddrStack.Mode = AddrModeFlat;
        while (depth < 48 &&
               StackWalk64(MACHINE, process, game_thread, &frame, &context, NULL, SymFunctionTableAccess64,
                           SymGetModuleBase64, NULL) &&
               frame.AddrPC.Offset != 0) {
            char buffer[sizeof(SYMBOL_INFO) + 64];
            SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
            DWORD64 displacement;
            DWORD64 start = frame.AddrPC.Offset;
            int i;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 64;
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                start = symbol->Address;
            }
            for (i = 0; i < depth && seen[i] != start; ++i) {
            }
            if (i == depth) {
                seen[depth++] = start;
                for (i = 0; i < count && function[i] != start; ++i) {
                }
                if (i == count && count < MAX_FUNCTIONS) {
                    function[count] = start;
                    hits[count++] = 0;
                }
                if (i < count) {
                    ++hits[i];
                }
            }
        }
        ResumeThread(game_thread);
    }
    fprintf(stderr, "khdays-native: functions on the game thread's stack in %d samples:\n", samples);
    for (int shown = 0; shown < 60; ++shown) {
        int best = -1;
        char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
        DWORD64 displacement;
        for (int i = 0; i < count; ++i) {
            if (hits[i] > 0 && (best < 0 || hits[i] > hits[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        fprintf(stderr, "  %5d  %s\n", hits[best],
                SymFromAddr(process, function[best], &displacement, symbol) ? symbol->Name : "?");
        hits[best] = -hits[best];
    }
}

static DWORD WINAPI watchdog(void *parameter)
{
    CONTEXT context;
    const char *samples = getenv("KHDAYS_STALL_SAMPLES");
    Sleep((DWORD)(size_t)parameter * 1000);
    if (samples != NULL && atoi(samples) > 0) {
        sample_stacks(atoi(samples));
    }
    SuspendThread(game_thread);
    context.ContextFlags = CONTEXT_FULL;
    GetThreadContext(game_thread, &context);
    fprintf(stderr, "khdays-native: still running after %u s; the game thread is at:\n", (unsigned)(size_t)parameter);
    print_stack(game_thread, &context);
    dump_memory();
    /* The SDK's threads: OSi_ThreadInfo (0x02044330) current and list, each
     * OSThread with state at +0x64, next at +0x68, priority at +0x70. */
    {
        const unsigned current = *(volatile unsigned *)KHDAYS_W(0x02044334);
        unsigned t = *(volatile unsigned *)KHDAYS_W(0x02044338);
        fprintf(stderr, "SDK threads (current %08x, rescheduling %u, cpsr %02x):\n", current,
                *(volatile unsigned short *)KHDAYS_W(0x02044330), khdays_cpsr_value());
        for (int n = 0; t != 0 && n < 32; ++n) {
            fprintf(stderr, "  %08x state %u priority %u\n", t, *(volatile unsigned *)KHDAYS_W(t + 0x64),
                    *(volatile unsigned *)KHDAYS_W(t + 0x70));
            t = *(volatile unsigned *)KHDAYS_W(t + 0x68);
        }
    }
    ExitProcess(5);
}

#if defined(_DEBUG)
#include <rtcapi.h>

/* The debug build's run-time checks (/RTC1) catch a local read before any write. On the ARM9 that
 * read takes whatever the register held, like the ABI repairs (port/abi). Each call chain is
 * reported once and the game goes on. */
static int __cdecl on_runtime_check(int type, const wchar_t *file, int line, const wchar_t *module,
                                    const wchar_t *format, ...)
{
    static unsigned long *seen_hash; /* the chains already reported (grows) */
    static int seen, seen_room;
    void *frames[8];
    unsigned long hash = 0;
    USHORT count = CaptureStackBackTrace(1, 8, frames, &hash);
    (void)module;
    for (int i = 0; i < seen; ++i) {
        if (seen_hash[i] == hash) {
            return 0;
        }
    }
    if (seen == seen_room) {
        const int room = seen_room ? seen_room * 2 : 64;
        unsigned long *grown = (unsigned long *)realloc(seen_hash, (size_t)room * sizeof(*grown));
        if (grown != NULL) {
            seen_hash = grown;
            seen_room = room;
        }
    }
    if (seen < seen_room) {
        seen_hash[seen++] = hash;
    }
    fwprintf(stderr, L"khdays-native: run-time check %d at %s:%d: ", type, file ? file : L"?", line);
    if (format != NULL) {
        va_list args;
        va_start(args, format);
        vfwprintf(stderr, format, args);
        va_end(args);
    }
    fwprintf(stderr, L"\n");
    for (USHORT i = 0; i < count; ++i) {
        fprintf(stderr, "  #%-2u %p %s\n", i, frames[i], symbol_name(frames[i]));
    }
    fflush(stderr);
    return 0; /* carry on */
}
#endif

static DWORD game_thread_id; /* the thread that runs the game (main's) */

/* Last in the vectored chain: reports an exception no runtime handler took, before the structured
 * handlers see it (a broken chain there would hide it). */
static LONG CALLBACK on_unhandled_first(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *r = info->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION ||
        r->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
        const Word sp = CONTEXT_SP(info->ContextRecord);
        fprintf(stderr, "khdays-native: first-chance exception 0x%08lx at %p (pc %p, sp %p, info %lu %p)\n",
                r->ExceptionCode, r->ExceptionAddress, (const void *)(size_t)CONTEXT_PC(info->ContextRecord),
                (const void *)(size_t)sp, (unsigned long)r->ExceptionInformation[0],
                (const void *)r->ExceptionInformation[1]);
        fprintf(stderr, "  on %s thread %lu, stack %p-%p\n",
                GetCurrentThreadId() == game_thread_id ? "the game's" : "another", GetCurrentThreadId(),
                tib->StackLimit, tib->StackBase);
        if (CONTEXT_PC(info->ContextRecord) == 0 && !IsBadReadPtr((const void *)(size_t)sp, 16)) {
            const DWORD *w = (const DWORD *)(size_t)sp;
            fprintf(stderr, "  [sp] %08lx %08lx %08lx %08lx\n", w[0], w[1], w[2], w[3]);
        }
        print_stack(GetCurrentThread(), info->ContextRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* KHDAYS_WATCH=ADDR[,ADDR...]: reports each function that writes the 4 bytes at a DS address (hex)
 * on the game's thread, once and with its stack, through the processor's debug registers (four
 * addresses at most). An I/O register's address watches the register model's bytes. */
#define WATCH_ARM 0xe0574348u /* the exception that sets the debug registers */
static DWORD64 watched[4];
static uint32_t watched_ds[4];
static int watched_count;
static const void **watch_seen;
static int watch_seen_count, watch_seen_room;

static int watch_new(const void *function)
{
    for (int i = 0; i < watch_seen_count; ++i) {
        if (watch_seen[i] == function) {
            return 0;
        }
    }
    if (watch_seen_count == watch_seen_room) {
        const int room = watch_seen_room ? watch_seen_room * 2 : 16;
        const void **grown = (const void **)realloc((void *)watch_seen, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return 0;
        }
        watch_seen = grown;
        watch_seen_room = room;
    }
    watch_seen[watch_seen_count++] = function;
    return 1;
}

static LONG CALLBACK on_watch(EXCEPTION_POINTERS *info)
{
    CONTEXT *c = info->ContextRecord;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == WATCH_ARM) {
        DWORD64 dr7 = 0;
        for (int i = 0; i < watched_count; ++i) {
            (&c->Dr0)[i] = watched[i];
            dr7 |= 1ull << (2 * i);      /* enabled */
            dr7 |= 1ull << (16 + 4 * i); /* on writes */
            dr7 |= 3ull << (18 + 4 * i); /* 4 bytes */
        }
        c->Dr7 = dr7;
        c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && (c->Dr6 & 0xf) != 0) {
        for (int i = 0; i < watched_count; ++i) {
            if ((c->Dr6 & (1ull << i)) != 0) {
                const void *site = (const void *)CONTEXT_PC(c);
                char name[300];
                DWORD64 displacement = 0;
                char buffer[sizeof(SYMBOL_INFO) + 256];
                SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
                const void *function = site;
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen = 255;
                if (SymFromAddr(GetCurrentProcess(), (DWORD64)(size_t)site, &displacement, symbol)) {
                    function = (const void *)(size_t)symbol->Address;
                    snprintf(name, sizeof(name), "%s", symbol->Name);
                } else {
                    snprintf(name, sizeof(name), "%p", site);
                }
                if (watch_new(function)) {
                    CONTEXT copy = *c;
                    fprintf(stderr, "watch: 0x%08x (now %08x) written by %s (%p), VBlank %u\n", watched_ds[i],
                            *(const uint32_t *)(size_t)watched[i], name, site, khdays_events_vblank_count());
                    khdays_log("watch: 0x%08x written by %s (%p)", watched_ds[i], name, site);
                    print_stack(GetCurrentThread(), &copy);
                }
            }
        }
        c->Dr6 = 0;
        c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void watch_init(void)
{
    const char *list = getenv("KHDAYS_WATCH");
    for (const char *s = list; s != NULL && *s != '\0';) {
        char *end;
        const unsigned long a = strtoul(s, &end, 16);
        if (end == s) {
            break;
        }
        if (watched_count == 4) {
            fprintf(stderr,
                    "watch: four addresses at most (the processor's debug registers); 0x%08lx not "
                    "watched\n",
                    a);
        } else {
            watched_ds[watched_count] = (uint32_t)(a & ~3ul);
            /* an I/O register: the register model's own bytes, which the C writes for a register that only
             * holds a value */
            watched[watched_count++] =
                a >= KHDAYS_IO_BASE && a < KHDAYS_IO_BASE + KHDAYS_IO_SIZE && khdays_io_host != NULL
                    ? (DWORD64)(size_t)(khdays_io_host + ((a & ~3ul) - KHDAYS_IO_BASE))
                    : (DWORD64)(size_t)KHDAYS_W(a & ~3ul);
        }
        s = *end == ',' ? end + 1 : end;
    }
    if (watched_count > 0) {
        AddVectoredExceptionHandler(1, on_watch);
        RaiseException(WATCH_ARM, 0, 0, NULL);
        fprintf(stderr, "watch: %d DS addresses watched for writes\n", watched_count);
    }
}

void khdays_diag_init(void)
{
    game_thread_id = GetCurrentThreadId();
    AddVectoredExceptionHandler(0, on_unhandled_first);
    const char *stall = getenv("KHDAYS_STALL_SECONDS");
#if defined(_DEBUG)
    _RTC_SetErrorFuncW(on_runtime_check);
#endif
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    watch_init();
    if (stall != NULL && atoi(stall) > 0) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &game_thread, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
        CreateThread(NULL, 0, watchdog, (void *)(size_t)atoi(stall), 0, NULL);
    }
}

/* printf to stderr, for a temporary hook in the game's C (prepare.py HOOKS). */
void khdays_debugf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fflush(stderr);
}

void khdays_diag_stack(void)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    print_stack(GetCurrentThread(), &context);
}

unsigned long long khdays_diag_symbol(const char *name)
{
    char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    return SymFromName(GetCurrentProcess(), name, symbol) ? (unsigned long long)symbol->Address : 0;
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
    {
        /* no symbol (a system DLL's): its module and the offset in it */
        static char where[MAX_PATH + 32];
        HMODULE module = NULL;
        char path[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)address, &module) &&
            GetModuleFileNameA(module, path, (DWORD)sizeof(path)) > 0) {
            const char *name = strrchr(path, '\\');
            snprintf(where, sizeof(where), "%s+0x%llx", name != NULL ? name + 1 : path,
                     (unsigned long long)((const char *)address - (const char *)module));
            return where;
        }
    }
    return "?";
}

const char *khdays_diag_function_at(const void *address)
{
    return symbol_name(address);
}

void khdays_diag_callers(char *out, int size, int skip)
{
    void *frames[24];
    const USHORT count = CaptureStackBackTrace((DWORD)skip + 1, 24, frames, NULL);
    out[0] = '\0';
    for (USHORT i = 0; i < count; ++i) {
        if (i > 0) {
            strncat_s(out, (size_t)size, " < ", _TRUNCATE);
        }
        strncat_s(out, (size_t)size, symbol_name(frames[i]), _TRUNCATE);
    }
}

/* KHDAYS_TRACE_SCRIPT: prints each step of the action-script interpreter (func_02020e58) that differs
 * from the last: the command (opcode, sub-op, length), its action and its five callbacks. */
void khdays_trace_script(void *st, void *entry)
{
    static int enabled = -1;
    static u32 last[8];
    const u8 *e = (const u8 *)entry;
    const u32 cmd_reference = *(const u32 *)(e + 0x10); /* the game's pointer: a reference */
    const u8 *cmd = cmd_reference ? (const u8 *)KHDAYS_W(cmd_reference) : NULL;
    u32 now[8];
    {
        extern void khdays_scene_step(void *st); /* runtime/movies/scene_trace.c */
        khdays_scene_step(st);
    }
    if (enabled < 0) {
        enabled = getenv("KHDAYS_TRACE_SCRIPT") != NULL;
    }
    if (!enabled) {
        return;
    }
    now[0] = cmd_reference;
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
    fprintf(stderr, " action %s", symbol_name((const void *)khdays_function_lookup(now[1])));
    for (int i = 0; i < 5; ++i) {
        if (now[2 + i] != 0) {
            fprintf(stderr, " cb%d %s", i, symbol_name((const void *)khdays_function_lookup(now[2 + i])));
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Obj_UpdateAll's check (prepare.py HOOKS) of the next update an update returned: 0 keeps the current
 * one, -1 and -2 are markers, anything else must be native code. */
static void check_update(intptr_t fn, intptr_t next); /* native addresses */

/* `fn` and `next` are function references: looked up, checked and named. */
void khdays_check_update(int fn, int next)
{
    const khdays_function_pointer native = khdays_function_lookup((KhdaysRef)fn);
    khdays_function_pointer native_next;
    if (next == 0 || next == -1 || next == -2) {
        check_update((intptr_t)native, next);
        return;
    }
    native_next = khdays_function_lookup((KhdaysRef)next);
    if (native_next == NULL) {
        fprintf(stderr,
                "khdays-native: the update %s returned 0x%08x as the object's next update, "
                "which is no function's reference\n",
                symbol_name((const void *)native), (u32)next);
        fflush(stderr);
        exit(12);
    }
    check_update((intptr_t)native, (intptr_t)native_next);
}

static void check_update(intptr_t fn, intptr_t next)
{
    extern char __ImageBase;
    static int trace = -1;
    const IMAGE_NT_HEADERS *nt;
    const u8 *base = (const u8 *)&__ImageBase;
    khdays_input_update_ran((khdays_uptr)fn);
    khdays_cinelist_update_ran((khdays_uptr)fn);
    khdays_daycard_update_ran((khdays_uptr)fn);
    khdays_roomlist_update_ran((khdays_uptr)fn);
    khdays_charselect_update_ran((khdays_uptr)fn);
    khdays_camp_update_ran((khdays_uptr)fn);
    khdays_shop_update_ran((khdays_uptr)fn);
    khdays_field_wipe_update_ran((uintptr_t)fn);
    khdays_situation_update_ran((uintptr_t)fn);
    if (trace < 0) {
        const char *setting = getenv("KHDAYS_TRACE_UPDATES");
        trace = setting == NULL ? 0 : strcmp(setting, "all") == 0 ? 2 : 1;
    }
    if (trace == 2) {
        /* KHDAYS_TRACE_UPDATES=all: print the updates each frame runs, when
         * that set changes */
        static intptr_t frame_set[64], last_set[64];
        static int frame_count, last_count;
        static u32 frame = ~0u;
        const u32 now = khdays_events_vblank_count();
        int i;
        if (now != frame) {
            int same = frame_count == last_count;
            for (i = 0; same && i < frame_count; ++i) {
                int j;
                for (j = 0; j < last_count && last_set[j] != frame_set[i]; ++j) {
                }
                same = j < last_count;
            }
            if (!same && frame_count > 0) {
                fprintf(stderr, "updates at frame %u:", frame);
                for (i = 0; i < frame_count; ++i) {
                    fprintf(stderr, " %s", symbol_name((const void *)frame_set[i]));
                }
                fprintf(stderr, "\n");
                memcpy(last_set, frame_set, sizeof(frame_set));
                last_count = frame_count;
            }
            frame_count = 0;
            frame = now;
        }
        for (i = 0; i < frame_count && frame_set[i] != fn; ++i) {
        }
        if (i == frame_count && frame_count < 64) {
            frame_set[frame_count++] = fn;
        }
    }
    if (trace && next != 0 && next != fn) { /* KHDAYS_TRACE_UPDATES: every change of update */
        fprintf(stderr, "update %s -> ", symbol_name((const void *)fn));
        fprintf(stderr, "%s\n", next == -1 ? "idle" : next == -2 ? "destroy" : symbol_name((const void *)next));
    }
    if (next == 0 || next == -1 || next == -2) {
        return;
    }
    nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    if ((const u8 *)next >= base && (const u8 *)next < base + nt->OptionalHeader.SizeOfImage) {
        return;
    }
    fprintf(stderr,
            "khdays-native: the update %s returned 0x%08x as the object's next update, "
            "which is not code\n",
            symbol_name((const void *)fn), (u32)next);
    fflush(stderr);
    exit(12);
}
