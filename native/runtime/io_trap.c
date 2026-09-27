/* Hardware registers with side effects, reached by ordinary loads and stores.
 *
 * The I/O pages are mapped with no access. A game access faults; the handler
 * works out from the faulting instruction how many bytes it touches and
 * whether it reads, writes or both, brings the register values it will read
 * up to date (io_regs.c), opens the page and single-steps the instruction.
 * On the step trap it closes the page again and applies what the write means
 * (a divide started, an interrupt acknowledged, a DMA triggered...).
 *
 * Only the instruction forms MSVC emits for scalar and SIMD memory accesses
 * are decoded; anything else stops the process naming the bytes, so a new
 * form is added from evidence, not assumed. */
#include "../hal/hal.h"
#include "io.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#define PAGE 0x1000u

typedef struct {
    int size;   /* bytes the memory operand covers */
    int reads;  /* the instruction loads the operand */
    int writes; /* ... and/or stores it */
} Access;

static int is_prefix(u8 b)
{
    return b == 0x66 || b == 0x67 || b == 0xf0 || b == 0xf2 || b == 0xf3 || b == 0x2e ||
           b == 0x36 || b == 0x3e || b == 0x26 || b == 0x64 || b == 0x65;
}

/* 0 on an instruction form this decoder does not know. */
static int decode(const u8 *code, Access *a)
{
    int opsize16 = 0, rep_f3 = 0, rep_f2 = 0;
    u8 op, reg;
    while (is_prefix(*code)) {
        opsize16 |= *code == 0x66;
        rep_f3 |= *code == 0xf3;
        rep_f2 |= *code == 0xf2;
        ++code;
    }
    op = *code++;
    reg = (code[0] >> 3) & 7;  /* ModRM.reg, for the opcodes that have one */
    a->reads = a->writes = 0;
    a->size = opsize16 ? 2 : 4;

    if (op < 0x40 && (op & 7) < 4) {
        /* add/or/adc/sbb/and/sub/xor/cmp in their four ModRM forms */
        const int is_cmp = (op >> 3) == 7;
        if ((op & 1) == 0) a->size = 1;
        a->reads = 1;
        a->writes = (op & 2) == 0 && !is_cmp;
        return 1;
    }
    switch (op) {
    case 0x8a: a->size = 1; /* fall through */
    case 0x8b: a->reads = 1; return 1;
    case 0x88: a->size = 1; /* fall through */
    case 0x89: a->writes = 1; return 1;
    case 0xc6: a->size = 1; /* fall through */
    case 0xc7: a->writes = 1; return 1;
    case 0xa0: a->size = 1; /* fall through */
    case 0xa1: a->reads = 1; return 1;
    case 0xa2: a->size = 1; /* fall through */
    case 0xa3: a->writes = 1; return 1;
    case 0x84: a->size = 1; /* fall through */
    case 0x85: a->reads = 1; return 1;
    case 0x86: a->size = 1; /* fall through */
    case 0x87: a->reads = a->writes = 1; return 1;
    case 0x80: a->size = 1; /* fall through */
    case 0x81:
    case 0x83: a->reads = 1; a->writes = reg != 7; return 1;
    case 0xc0: case 0xd0: case 0xd2: a->size = 1; /* fall through */
    case 0xc1: case 0xd1: case 0xd3: a->reads = a->writes = 1; return 1;
    case 0xf6: a->size = 1; /* fall through */
    case 0xf7:
        a->reads = 1;
        a->writes = reg == 2 || reg == 3;  /* not, neg */
        return 1;
    case 0xfe: a->size = 1; /* fall through */
    case 0xff:
        a->reads = 1;
        a->writes = reg <= 1;  /* inc, dec */
        return 1;
    case 0xa4: a->size = 1; /* fall through */
    case 0xa5: a->reads = a->writes = 1; return 1;  /* movs: either side */
    case 0xaa: a->size = 1; /* fall through */
    case 0xab: a->writes = 1; return 1;
    case 0xac: a->size = 1; /* fall through */
    case 0xad: a->reads = 1; return 1;
    case 0x0f:
        op = *code++;
        reg = (code[0] >> 3) & 7;
        switch (op) {
        case 0xb6: case 0xbe: a->size = 1; a->reads = 1; return 1;
        case 0xb7: case 0xbf: a->size = 2; a->reads = 1; return 1;
        case 0x10: case 0x11:  /* movups/movupd/movss/movsd */
            a->size = rep_f3 ? 4 : rep_f2 ? 8 : 16;
            a->reads = op == 0x10;
            a->writes = op == 0x11;
            return 1;
        case 0x28: case 0x29: a->size = 16; a->reads = op == 0x28; a->writes = op == 0x29; return 1;
        case 0x6f: case 0x7f: a->size = 16; a->reads = op == 0x6f; a->writes = op == 0x7f; return 1;
        case 0x12: case 0x16: a->size = 8; a->reads = 1; return 1;
        case 0x13: case 0x17: a->size = 8; a->writes = 1; return 1;
        case 0xd6: a->size = 8; a->writes = 1; return 1;
        case 0x6e: a->size = 4; a->reads = 1; return 1;
        case 0x7e:
            if (rep_f3) { a->size = 8; a->reads = 1; } else { a->size = 4; a->writes = 1; }
            return 1;
        default: return 0;
        }
    default:
        return 0;
    }
}

static int is_io(u32 address)
{
    return (address >= KHDAYS_IO_BASE && address < KHDAYS_IO_BASE + KHDAYS_IO_SIZE) ||
           (address >= KHDAYS_IO2_BASE && address < KHDAYS_IO2_BASE + KHDAYS_IO2_SIZE);
}

static void set_protection(u32 address, DWORD protection)
{
    DWORD old;
    VirtualProtect((void *)(address & ~(PAGE - 1)), PAGE, protection, &old);
}

/* The access being single-stepped. The game runs on one host thread. */
static struct {
    int active;
    u32 address;
    Access access;
    u8 before[16];
} pending;

static void fatal_instruction(const u8 *code, u32 address)
{
    fprintf(stderr, "io: unknown instruction touching 0x%08x at %p:", address, (void *)code);
    for (int i = 0; i < 12; ++i) {
        fprintf(stderr, " %02x", code[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    ExitProcess(4);
}

static LONG CALLBACK io_handler(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    CONTEXT *context = info->ContextRecord;

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        const u32 address = (u32)record->ExceptionInformation[1];
        if (!is_io(address)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (pending.active) {
            /* One instruction touching two I/O pages (movs between them). */
            fatal_instruction((const u8 *)context->Eip, address);
        }
        if (!decode((const u8 *)context->Eip, &pending.access)) {
            fatal_instruction((const u8 *)context->Eip, address);
        }
        pending.active = 1;
        pending.address = address;
        set_protection(address, PAGE_READWRITE);
        if (pending.access.size > 1) {
            set_protection(address + pending.access.size - 1, PAGE_READWRITE);
        }
        if (pending.access.reads) {
            khdays_io_read(address, pending.access.size);
        }
        if (pending.access.writes) {
            memcpy(pending.before, (const void *)address, pending.access.size);
        }
        context->EFlags |= 0x100;  /* trap after this instruction */
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (record->ExceptionCode == EXCEPTION_SINGLE_STEP && pending.active) {
        const u32 address = pending.address;
        pending.active = 0;
        set_protection(address, PAGE_NOACCESS);
        if (pending.access.size > 1) {
            set_protection(address + pending.access.size - 1, PAGE_NOACCESS);
        }
        if (pending.access.writes) {
            khdays_io_write(address, pending.access.size, pending.before);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void khdays_io_trap_init(void)
{
    DWORD old;
    AddVectoredExceptionHandler(1, io_handler);
    VirtualProtect((void *)KHDAYS_IO_BASE, KHDAYS_IO_SIZE, PAGE_NOACCESS, &old);
    VirtualProtect((void *)KHDAYS_IO2_BASE, KHDAYS_IO2_SIZE, PAGE_NOACCESS, &old);
}
