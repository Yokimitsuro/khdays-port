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

/* --- Plain loads and stores, done in the handler ----------------------------
 * The single step costs a second exception and four page-protection changes
 * per access, which a loop polling a register (the card's data port: two
 * accesses a word) pays thousands of times a frame. The forms that only move
 * a value between a register (or an immediate) and the register file are
 * carried out here instead, and the thread continues after the instruction. */

static DWORD *gpr(CONTEXT *c, int n)
{
    switch (n) {
    case 0: return &c->Eax;
    case 1: return &c->Ecx;
    case 2: return &c->Edx;
    case 3: return &c->Ebx;
    case 4: return &c->Esp;
    case 5: return &c->Ebp;
    case 6: return &c->Esi;
    default: return &c->Edi;
    }
}

/* AL, CL, DL, BL, AH, CH, DH, BH */
static u8 *gpr8(CONTEXT *c, int n)
{
    return (u8 *)gpr(c, n & 3) + (n >> 2);
}

/* The bytes of a ModRM memory operand (ModRM, SIB, displacement); 0 for a
 * register operand. */
static int modrm_bytes(const u8 *p)
{
    const int mod = p[0] >> 6, rm = p[0] & 7;
    int n = 1;
    if (mod == 3) return 0;
    if (rm == 4) {
        ++n;
        if (mod == 0 && (p[1] & 7) == 5) n += 4;
    } else if (mod == 0 && rm == 5) {
        n += 4;
    }
    if (mod == 1) n += 1;
    if (mod == 2) n += 4;
    return n;
}

static u8 *host_view(u32 address)
{
    return address >= KHDAYS_IO2_BASE ? khdays_io2_host + (address - KHDAYS_IO2_BASE)
                                      : khdays_io_host + (address - KHDAYS_IO_BASE);
}

static u32 io_load(u32 address, int size)
{
    u32 value = 0;
    khdays_io_read(address, size);
    memcpy(&value, host_view(address), (size_t)size);
    return value;
}

static void io_store(u32 address, int size, u32 value)
{
    u8 before[4];
    u8 *host = host_view(address);
    memcpy(before, host, (size_t)size);
    memcpy(host, &value, (size_t)size);
    khdays_io_write(address, size, before);
}

/* 1 when the instruction at Eip was carried out (Eip is then past it). */
static int emulate(CONTEXT *c, u32 address)
{
    const u8 *code = (const u8 *)c->Eip;
    int size = 4, n;
    u8 op, reg;
    if (*code == 0x66) {
        size = 2;
        ++code;
    }
    if (is_prefix(*code)) return 0;
    op = *code++;
    if (op == 0x0f) {  /* movzx, movsx into a 32-bit register */
        const u8 op2 = *code++;
        u32 value;
        if (size != 4 || (op2 != 0xb6 && op2 != 0xb7 && op2 != 0xbe && op2 != 0xbf)) return 0;
        if ((n = modrm_bytes(code)) == 0) return 0;
        reg = (code[0] >> 3) & 7;
        size = (op2 & 1) ? 2 : 1;
        value = io_load(address, size);
        if (op2 == 0xbe) value = (u32)(s32)(s8)value;
        if (op2 == 0xbf) value = (u32)(s32)(s16)value;
        *gpr(c, reg) = value;
        c->Eip = (DWORD)(size_t)(code + n);
        return 1;
    }
    switch (op) {
    case 0x8a: case 0x8b: case 0x88: case 0x89: case 0xc6: case 0xc7:
        if ((n = modrm_bytes(code)) == 0) return 0;
        reg = (code[0] >> 3) & 7;
        if ((op & 1) == 0) size = 1;
        if (op == 0x8a) {
            *gpr8(c, reg) = (u8)io_load(address, 1);
        } else if (op == 0x8b) {
            const u32 value = io_load(address, size);
            if (size == 2) *(u16 *)gpr(c, reg) = (u16)value; else *gpr(c, reg) = value;
        } else if (op == 0x88) {
            io_store(address, 1, *gpr8(c, reg));
        } else if (op == 0x89) {
            io_store(address, size, *gpr(c, reg));
        } else {
            u32 imm = 0;
            if (reg != 0) return 0;
            memcpy(&imm, code + n, (size_t)size);
            io_store(address, size, imm);
            n += size;
        }
        c->Eip = (DWORD)(size_t)(code + n);
        return 1;
    case 0xa0: case 0xa1: case 0xa2: case 0xa3:  /* al/ax/eax <-> [moffs32] */
        if ((op & 1) == 0) size = 1;
        if (op < 0xa2) {
            const u32 value = io_load(address, size);
            if (size == 1) *(u8 *)&c->Eax = (u8)value;
            else if (size == 2) *(u16 *)&c->Eax = (u16)value;
            else c->Eax = value;
        } else {
            io_store(address, size, c->Eax);
        }
        c->Eip = (DWORD)(size_t)(code + 4);
        return 1;
    default:
        return 0;
    }
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

/* Whether an access is half done (the page open, a single step to come):
 * the interrupt watcher must not divert the thread then (async_irq.c). */
int khdays_io_trap_busy(void)
{
    return pending.active;
}

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

void khdays_async_irq_undo(CONTEXT *context, const void *address);

static LONG CALLBACK io_handler(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    CONTEXT *context = info->ContextRecord;

    khdays_async_irq_undo(context, record->ExceptionAddress);  /* an interrupt injected in flight */

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        const u32 address = (u32)record->ExceptionInformation[1];
        if (!is_io(address)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (pending.active) {
            /* One instruction touching two I/O pages (movs between them). */
            fatal_instruction((const u8 *)context->Eip, address);
        }
        if (emulate(context, address)) {
            return EXCEPTION_CONTINUE_EXECUTION;
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
