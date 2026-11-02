/* The game's memory and its references (window.h): every pointer the rewritten game keeps is the DS
 * address it had, and a function reference is a DS address or one the build numbered. */
#include "window.h"
#include "../../hal/hal.h"
#include "runtime.h"
#include "session_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

unsigned char *khdays_window;

static void stop(const char *what, unsigned long long value, const void *caller)
{
    const char *function = khdays_diag_function_at(caller);
    fprintf(stderr, "khdays-native: %s 0x%llx (from %s, %p): stopped\n", what, value, function, caller);
    khdays_log("stop: %s 0x%llx (from %s, %p)", what, value, function, caller);
    khdays_log_close();
    fflush(stderr);
    ExitProcess(5);
}

#if defined(_MSC_VER)
#include <intrin.h>
#define CALLER() _ReturnAddress()
#else
#define CALLER() __builtin_return_address(0)
#endif

/* khdays_game_ram for the game's C (prepare.py's join in MIi_CheckDma0SourceAddress). */
int khdays_game_ram_ref(unsigned int reference)
{
    return khdays_game_ram(reference);
}

KhdaysRef khdays_reference(const void *host)
{
    unsigned long long offset;
    if (host == NULL) {
        return 0; /* NULL, the game's 0 */
    }
    offset = (unsigned long long)((const unsigned char *)host - khdays_window);
    if ((const unsigned char *)host < khdays_window) {
        stop("a host address below the game's memory, as a reference:", (unsigned long long)(size_t)host, CALLER());
    }
    if (offset > 0xffffffffull) {
        stop("a host address past the game's memory, as a reference:", (unsigned long long)(size_t)host, CALLER());
    }
    return (KhdaysRef)offset;
}

/* The function references the build numbered: one record each in section .khfr$m
 * (compat/khdays_references.h), sorted by reference the first time one is called. */
typedef struct {
    KhdaysRef reference;
    khdays_function_pointer function;
} FunctionRecord;

#pragma section(".khfr$a", read)
#pragma section(".khfr$z", read)
__declspec(allocate(".khfr$a")) static const FunctionRecord first_function = {0, 0};
__declspec(allocate(".khfr$z")) static const FunctionRecord last_function = {0, 0};

static FunctionRecord *numbered;
static size_t numbered_count;

static int by_reference(const void *a, const void *b)
{
    const KhdaysRef x = ((const FunctionRecord *)a)->reference, y = ((const FunctionRecord *)b)->reference;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void sort_numbered(void)
{
    size_t n = 0;
    for (const FunctionRecord *r = &first_function + 1; r < &last_function; ++r) {
        n += r->function != NULL;
    }
    numbered = (FunctionRecord *)malloc((n ? n : 1) * sizeof(FunctionRecord));
    if (numbered == NULL) {
        khdays_hal_unimplemented("no memory for the function references' table");
    }
    for (const FunctionRecord *r = &first_function + 1; r < &last_function; ++r) {
        if (r->function != NULL) {
            numbered[numbered_count++] = *r;
        }
    }
    qsort(numbered, numbered_count, sizeof(FunctionRecord), by_reference);
}

extern void *khdays_native_at(u32 address);
extern int khdays_overlay_function_at(u32 address, int *module);

/* The native functions last found, by reference. A new overlay load moves the generation on
 * (overlays.c); the ITCM's are never kept (the MobiClip decoder goes over it). The game's threads,
 * the only callers, run one at a time. */
#define CACHED 4096u /* a power of two */
#define ITCM_BASE 0x01ff8000u
#define ITCM_END 0x02000000u
static struct {
    KhdaysRef reference;
    unsigned generation;
    khdays_function_pointer function;
} cached[CACHED];
static unsigned generation = 1;

void khdays_function_references_moved(void)
{
    ++generation;
}

khdays_function_pointer khdays_function_lookup(KhdaysRef reference)
{
    const unsigned slot = (reference >> 1) & (CACHED - 1);
    size_t lo = 0, hi;
    khdays_function_pointer native;
    if (cached[slot].reference == reference && cached[slot].generation == generation && reference != 0) {
        return cached[slot].function;
    }
    if (numbered == NULL) {
        sort_numbered();
    }
    if (numbered == NULL) {
        return NULL; /* sort_numbered stops the game first; said for the analysis */
    }
    hi = numbered_count;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (numbered[mid].reference < reference) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < numbered_count && numbered[lo].reference == reference) {
        native = numbered[lo].function;
    } else {
        native = (khdays_function_pointer)khdays_native_at(reference);
    }
    if (native != NULL && !(reference >= ITCM_BASE && reference < ITCM_END)) {
        cached[slot].reference = reference;
        cached[slot].generation = generation;
        cached[slot].function = native;
    }
    return native;
}

/* What a call into an overlay that is not loaded runs: nothing, its answer 0. */
static int not_loaded(void)
{
    return 0;
}

khdays_function_pointer khdays_function(KhdaysRef reference)
{
    const khdays_function_pointer native = khdays_function_lookup(reference);
    int module;
    if (native == NULL && khdays_overlay_function_at(reference, &module)) {
        /* a handler of an overlay its scene did not load (a mission built by the port in a room, or with an enemy,
         * whose objects want one the template mission did not ask for): left out rather than the game stopped, said
         * once for each */
        static KhdaysRef *said;
        static size_t said_count, said_capacity;
        size_t i = 0;
        while (i < said_count && said[i] != reference) {
            ++i;
        }
        if (i == said_count) {
            if (said_count == said_capacity) {
                const size_t grown = said_capacity == 0 ? 16 : said_capacity * 2;
                KhdaysRef *more = realloc(said, grown * sizeof(*more));
                if (more != NULL) {
                    said = more;
                    said_capacity = grown;
                }
            }
            if (said_count < said_capacity) {
                said[said_count++] = reference;
            }
            khdays_log("overlays: a call into overlay %d's function %#x while it is not loaded: left out (from %p)",
                       module, (unsigned)reference, CALLER());
        }
        return (khdays_function_pointer)not_loaded;
    }
    if (native == NULL) {
        stop("a call through a function reference with no native function:", reference, CALLER());
    }
    return native;
}
