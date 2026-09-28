/* An ARMv5TE interpreter (ARM946E-S: ARM and Thumb, the DSP multiplies and
 * saturating arithmetic, CLZ) for the routines the game runs as machine code
 * the native build has no C for: assembly-only sources (a symbol there is
 * its DS address, see prepare.py) and code the game copies at run time (the
 * MobiClip frame decoder in ITCM). DS memory sits at its real addresses, so
 * the interpreted code reads and writes it directly; I/O registers go through
 * the same models as native accesses (io_regs.c).
 *
 * A native call into DS memory faults (the pages are not executable); the
 * fault handler runs the callee here with the cdecl arguments in r0-r3 and on
 * the stack, and returns r1:r0 in EDX:EAX. A branch from interpreted code to
 * an address outside DS memory calls that native function the same way.
 *
 * The ARM7's sound driver runs here too (snd_driver.c), on the ARM7's bus:
 * main RAM through the ARM7's mirrors, its WRAM, and its own I/O registers
 * (sound.c). ARMv5 is a superset of the ARM7's ARMv4T for code built for the
 * latter. Its system calls into the OS and the BIOS are native stand-ins. */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "arm.h"
#include "io.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define RETURN_MAGIC 0xffff0000u  /* the return address of an entry: ends the run */
#define STACK_SIZE (512u * 1024u)

typedef struct Cpu {
    u32 r[16];      /* r15 reads as the current instruction + 8 (ARM) / + 4 (Thumb) */
    u32 pc;         /* the next instruction */
    u32 n, z, c, v, q;
    int thumb;
    int done;
} Cpu;

/* For crash reports: the instruction being run. */
volatile u32 khdays_arm_pc;

extern volatile LONG khdays_async_irq_blocked;

static u8 *stack_memory;
static u32 stack_top;  /* free space ends here; nested runs continue below */

/* The code running is the ARM7's (its bus, its hooks and BIOS). */
static int on_arm7;

#define ARM7_WRAM_BASE 0x037f8000u
#define ARM7_WRAM_END  0x03810000u

#define MAX_HOOKS 16
static u32 hook_address[MAX_HOOKS];
static KhdaysArmHook hook_function[MAX_HOOKS];
static int hook_count;
static KhdaysArmSwi arm7_bios;

static void fail(const Cpu *cpu, u32 op, const char *what)
{
    fprintf(stderr, "khdays-native: ARM%s interpreter at 0x%08x (%s, opcode 0x%08x): %s\n",
            on_arm7 ? "7" : "9", cpu->pc, cpu->thumb ? "Thumb" : "ARM", op, what);
    fflush(stderr);
    exit(13);
}

/* --- Memory ------------------------------------------------------------------- */

static int is_io(u32 a)
{
    return (a >> 24) == 0x04;
}

/* An ARM7 address as the process holds it: main RAM repeats every 4 MB (to
 * the ARM7, 0x027e0000 is main RAM's 0x023e0000, not the ARM9's DTCM that
 * the process maps there), the WRAM is where memory.c put it. Nothing else
 * is modelled. */
static u32 arm7_address(u32 a)
{
    if (a >= 0x02000000u && a < 0x03000000u) {
        return 0x02000000u | (a & 0x3fffffu);
    }
    if (a >= ARM7_WRAM_BASE && a < ARM7_WRAM_END) {
        return a;
    }
    fprintf(stderr, "khdays-native: ARM7 interpreter at 0x%08x: access to 0x%08x, outside main "
                    "RAM, its WRAM and its I/O\n", khdays_arm_pc, a);
    fflush(stderr);
    exit(13);
}

static u8 *io_host(u32 a)
{
    if (a >= KHDAYS_IO_BASE && a < KHDAYS_IO_BASE + KHDAYS_IO_SIZE) {
        return khdays_io_host + (a - KHDAYS_IO_BASE);
    }
    if (a >= KHDAYS_IO2_BASE && a < KHDAYS_IO2_BASE + KHDAYS_IO2_SIZE) {
        return khdays_io2_host + (a - KHDAYS_IO2_BASE);
    }
    fprintf(stderr, "khdays-native: ARM interpreter: I/O access at 0x%08x outside the modelled "
                    "registers\n", a);
    exit(13);
}

static u32 io_read(u32 a, int size)
{
    u32 v = 0;
    khdays_io_read(a, size);
    memcpy(&v, io_host(a), (size_t)size);
    return v;
}

static void io_write(u32 a, int size, u32 v)
{
    u8 before[4];
    u8 *h = io_host(a);
    memcpy(before, h, (size_t)size);
    memcpy(h, &v, (size_t)size);
    khdays_io_write(a, size, before);
}

static u32 rd32(u32 a)
{
    a &= ~3u;
    if (is_io(a)) return on_arm7 ? khdays_sound_io_read(a, 4) : io_read(a, 4);
    if (on_arm7) a = arm7_address(a);
    return *(const u32 *)(size_t)a;
}

static u32 rd16(u32 a)
{
    a &= ~1u;
    if (is_io(a)) return on_arm7 ? khdays_sound_io_read(a, 2) : io_read(a, 2);
    if (on_arm7) a = arm7_address(a);
    return *(const u16 *)(size_t)a;
}

static u32 rd8(u32 a)
{
    if (is_io(a)) return on_arm7 ? khdays_sound_io_read(a, 1) : io_read(a, 1);
    if (on_arm7) a = arm7_address(a);
    return *(const u8 *)(size_t)a;
}

static void wr32(u32 a, u32 v)
{
    a &= ~3u;
    if (is_io(a)) {
        if (on_arm7) khdays_sound_io_write(a, 4, v); else io_write(a, 4, v);
        return;
    }
    if (on_arm7) a = arm7_address(a);
    *(u32 *)(size_t)a = v;
}

static void wr16(u32 a, u32 v)
{
    a &= ~1u;
    if (is_io(a)) {
        if (on_arm7) khdays_sound_io_write(a, 2, v); else io_write(a, 2, v);
        return;
    }
    if (on_arm7) a = arm7_address(a);
    *(u16 *)(size_t)a = (u16)v;
}

static void wr8(u32 a, u32 v)
{
    if (is_io(a)) {
        if (on_arm7) khdays_sound_io_write(a, 1, v); else io_write(a, 1, v);
        return;
    }
    if (on_arm7) a = arm7_address(a);
    *(u8 *)(size_t)a = (u8)v;
}

/* LDR from an unaligned address rotates the word (ARMv5). */
static u32 ldr(u32 a)
{
    const u32 v = rd32(a);
    const u32 s = (a & 3) * 8;
    return s ? (v >> s) | (v << (32 - s)) : v;
}

/* --- Control flow ---------------------------------------------------------------- */

static int is_ds_code(u32 a)
{
    return a >= 0x01ff8000u && a < 0x03800000u;
}

typedef u64(__cdecl *NativeFn)(u32, u32, u32, u32, u32, u32, u32, u32);

/* Branch to `target`: bit 0 selects Thumb when `interwork`. Outside DS memory
 * it is native code, called with r0-r3 and four stack words; execution then
 * continues at the link register, as the callee's return would. */
static void branch(Cpu *cpu, u32 target, int interwork)
{
    if (interwork) {
        cpu->thumb = target & 1;
    }
    target &= cpu->thumb ? ~1u : ~3u;
    if ((target & ~3u) == RETURN_MAGIC) {
        cpu->done = 1;
        return;
    }
    if (on_arm7) {
        for (int i = 0; i < hook_count; ++i) {
            if (hook_address[i] == target) {
                /* A native stand-in: it returns (or suspends) as the routine would. */
                int suspend;
                cpu->pc = target;
                suspend = hook_function[i](cpu->r);
                branch(cpu, cpu->r[14], 1);
                if (suspend) {
                    if (cpu->done) fail(cpu, target, "a wait outside an ARM7 thread");
                    cpu->done = 2;
                }
                return;
            }
        }
        if (target < ARM7_WRAM_BASE || target >= ARM7_WRAM_END) {
            fail(cpu, target, "an ARM7 branch outside its WRAM");
        }
        cpu->pc = target;
        return;
    }
    if (!is_ds_code(target)) {
        const u32 sp = cpu->r[13];
        const u64 r = ((NativeFn)(size_t)target)(cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], rd32(sp),
                                                  rd32(sp + 4), rd32(sp + 8), rd32(sp + 12));
        cpu->r[0] = (u32)r;
        cpu->r[1] = (u32)(r >> 32);
        branch(cpu, cpu->r[14], 1);
        return;
    }
    cpu->pc = target;
}

static int condition(const Cpu *cpu, u32 cond)
{
    switch (cond) {
    case 0x0: return cpu->z;
    case 0x1: return !cpu->z;
    case 0x2: return cpu->c;
    case 0x3: return !cpu->c;
    case 0x4: return cpu->n;
    case 0x5: return !cpu->n;
    case 0x6: return cpu->v;
    case 0x7: return !cpu->v;
    case 0x8: return cpu->c && !cpu->z;
    case 0x9: return !cpu->c || cpu->z;
    case 0xa: return cpu->n == cpu->v;
    case 0xb: return cpu->n != cpu->v;
    case 0xc: return !cpu->z && cpu->n == cpu->v;
    case 0xd: return cpu->z || cpu->n != cpu->v;
    default: return 1;
    }
}

static u32 get_cpsr(const Cpu *cpu)
{
    return cpu->n << 31 | cpu->z << 30 | cpu->c << 29 | cpu->v << 28 | cpu->q << 27 |
           (u32)cpu->thumb << 5 | 0x1f;
}

static void set_flags_nz(Cpu *cpu, u32 v)
{
    cpu->n = v >> 31;
    cpu->z = v == 0;
}

static u32 add_flags(Cpu *cpu, u32 a, u32 b, u32 carry_in, int s)
{
    const u64 wide = (u64)a + b + carry_in;
    const u32 r = (u32)wide;
    if (s) {
        set_flags_nz(cpu, r);
        cpu->c = (u32)(wide >> 32);
        cpu->v = ((~(a ^ b) & (a ^ r)) >> 31) & 1;
    }
    return r;
}

/* a - b - !carry: ARM's SUB (carry 1), SBC */
static u32 sub_flags(Cpu *cpu, u32 a, u32 b, u32 carry_in, int s)
{
    return add_flags(cpu, a, ~b, carry_in, s);
}

/* The barrel shifter. `reg` marks a register-specified amount (0 means no
 * shift, 32+ handled), else an immediate amount with its special zeros. */
static u32 shift(Cpu *cpu, u32 v, u32 type, u32 amount, int reg, u32 *carry)
{
    *carry = cpu->c;
    switch (type) {
    case 0:  /* LSL */
        if (amount == 0) return v;
        if (amount < 32) { *carry = (v >> (32 - amount)) & 1; return v << amount; }
        *carry = amount == 32 ? v & 1 : 0;
        return 0;
    case 1:  /* LSR */
        if (amount == 0) {
            if (reg) return v;
            amount = 32;
        }
        if (amount < 32) { *carry = (v >> (amount - 1)) & 1; return v >> amount; }
        *carry = amount == 32 ? v >> 31 : 0;
        return 0;
    case 2:  /* ASR */
        if (amount == 0) {
            if (reg) return v;
            amount = 32;
        }
        if (amount < 32) { *carry = (v >> (amount - 1)) & 1; return (u32)((s32)v >> amount); }
        *carry = v >> 31;
        return v >> 31 ? 0xffffffffu : 0;
    default:  /* ROR, RRX */
        if (amount == 0) {
            if (reg) return v;
            *carry = v & 1;
            return (cpu->c << 31) | (v >> 1);
        }
        amount &= 31;
        if (amount == 0) { *carry = v >> 31; return v; }
        *carry = (v >> (amount - 1)) & 1;
        return (v >> amount) | (v << (32 - amount));
    }
}

static s32 saturate(s64 v, Cpu *cpu)
{
    if (v > 0x7fffffffll) { cpu->q = 1; return 0x7fffffff; }
    if (v < -0x80000000ll) { cpu->q = 1; return (s32)0x80000000u; }
    return (s32)v;
}

static void swi(Cpu *cpu, u32 number, u32 op)
{
    if (on_arm7) {
        if (arm7_bios == NULL || !arm7_bios(number, cpu->r)) {
            fail(cpu, op, "an ARM7 BIOS call the runtime does not model");
        }
        return;
    }
    switch (number) {
    case 0x09: {  /* Div */
        const s32 num = (s32)cpu->r[0], den = (s32)cpu->r[1];
        if (den == 0) fail(cpu, op, "SWI Div by zero");
        cpu->r[0] = (u32)(num / den);
        cpu->r[1] = (u32)(num % den);
        cpu->r[3] = (u32)abs(num / den);
        break;
    }
    case 0x0d: {  /* Sqrt */
        u32 v = cpu->r[0], root = 0, bit = 1u << 30;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= root + bit) { v -= root + bit; root = (root >> 1) + bit; } else root >>= 1;
            bit >>= 2;
        }
        cpu->r[0] = root;
        break;
    }
    default:
        fail(cpu, op, "an SWI (BIOS call) the interpreter does not model");
    }
}

/* --- ARM ------------------------------------------------------------------------ */

static void block_transfer(Cpu *cpu, u32 op)
{
    const u32 rn = (op >> 16) & 15;
    const u32 list = op & 0xffff;
    const int load = (op >> 20) & 1, writeback = (op >> 21) & 1, up = (op >> 23) & 1, pre = (op >> 24) & 1;
    u32 count = 0, base = cpu->r[rn], addr, end;
    for (int i = 0; i < 16; ++i) count += (list >> i) & 1;
    if (up) {
        addr = base + (pre ? 4 : 0);
        end = base + 4 * count;
    } else {
        addr = base - 4 * count + (pre ? 0 : 4);
        end = base - 4 * count;
    }
    if (writeback && !(load && (list & (1u << rn)))) {
        cpu->r[rn] = end;
    }
    for (int i = 0; i < 16; ++i) {
        if (!(list & (1u << i))) continue;
        if (load) {
            const u32 v = rd32(addr);
            if (i == 15) branch(cpu, v, 1);  /* ARMv5: loading pc interworks */
            else cpu->r[i] = v;
        } else {
            wr32(addr, i == 15 ? cpu->r[15] + 4 : cpu->r[i]);
        }
        addr += 4;
    }
}

static void exec_arm(Cpu *cpu, u32 op)
{
    const u32 cond = op >> 28;
    const u32 rn = (op >> 16) & 15, rd = (op >> 12) & 15;

    if (cond == 0xf) {
        if ((op & 0x0e000000) == 0x0a000000) {  /* BLX immediate */
            const u32 target = cpu->r[15] + ((u32)((s32)(op << 8) >> 6)) + ((op >> 23) & 2);
            cpu->r[14] = cpu->pc;
            branch(cpu, target | 1, 1);
            return;
        }
        if ((op & 0x0d70f000) == 0x0550f000) return;  /* PLD */
        fail(cpu, op, "unconditional instruction");
    }
    if (cond != 0xe && !condition(cpu, cond)) return;

    switch ((op >> 25) & 7) {
    case 0:
    case 1: {
        if ((op & 0x0ffffff0) == 0x012fff10 || (op & 0x0ffffff0) == 0x012fff30) {  /* BX, BLX */
            const u32 target = cpu->r[op & 15];
            if (op & 0x20) cpu->r[14] = cpu->pc;
            branch(cpu, target, 1);
            return;
        }
        if (!(op & (1u << 25))) {
            if ((op & 0x0fc000f0) == 0x00000090) {  /* MUL, MLA */
                const u32 rdm = (op >> 16) & 15;
                u32 r = cpu->r[op & 15] * cpu->r[(op >> 8) & 15];
                if (op & (1u << 21)) r += cpu->r[(op >> 12) & 15];
                cpu->r[rdm] = r;
                if (op & (1u << 20)) set_flags_nz(cpu, r);
                return;
            }
            if ((op & 0x0f8000f0) == 0x00800090) {  /* UMULL, UMLAL, SMULL, SMLAL */
                const u32 hi = (op >> 16) & 15, lo = (op >> 12) & 15;
                u64 r;
                if (op & (1u << 22)) r = (u64)((s64)(s32)cpu->r[op & 15] * (s32)cpu->r[(op >> 8) & 15]);
                else r = (u64)cpu->r[op & 15] * cpu->r[(op >> 8) & 15];
                if (op & (1u << 21)) r += (u64)cpu->r[hi] << 32 | cpu->r[lo];
                cpu->r[lo] = (u32)r;
                cpu->r[hi] = (u32)(r >> 32);
                if (op & (1u << 20)) { cpu->n = (u32)(r >> 63); cpu->z = r == 0; }
                return;
            }
            if ((op & 0x0fb00ff0) == 0x01000090) {  /* SWP, SWPB */
                const u32 a = cpu->r[rn];
                if (op & (1u << 22)) { const u32 t = rd8(a); wr8(a, cpu->r[op & 15]); cpu->r[rd] = t; }
                else { const u32 t = ldr(a); wr32(a, cpu->r[op & 15]); cpu->r[rd] = t; }
                return;
            }
            if ((op & 0x0e000090) == 0x00000090 && (op & 0x60)) {  /* halfword, signed, doubleword */
                const int pre = (op >> 24) & 1, up = (op >> 23) & 1, wb = (op >> 21) & 1, load = (op >> 20) & 1;
                const u32 sh = (op >> 5) & 3;
                const u32 offset = (op & (1u << 22)) ? (((op >> 4) & 0xf0) | (op & 15)) : cpu->r[op & 15];
                const u32 base = cpu->r[rn];
                const u32 at = up ? base + offset : base - offset;
                const u32 addr = pre ? at : base;
                if (load) {
                    u32 v;
                    if (sh == 1) v = rd16(addr);
                    else if (sh == 2) v = (u32)(s32)(s8)rd8(addr);
                    else v = (u32)(s32)(s16)rd16(addr);
                    if (!pre || wb) cpu->r[rn] = at;
                    if (rd == 15) branch(cpu, v, 1); else cpu->r[rd] = v;
                } else if (sh == 1) {  /* STRH */
                    wr16(addr, rd == 15 ? cpu->r[15] + 4 : cpu->r[rd]);
                    if (!pre || wb) cpu->r[rn] = at;
                } else if (sh == 2) {  /* LDRD */
                    const u32 lo = rd32(addr), hi = rd32(addr + 4);
                    if (!pre || wb) cpu->r[rn] = at;
                    cpu->r[rd] = lo;
                    cpu->r[rd + 1] = hi;
                } else {  /* STRD */
                    wr32(addr, cpu->r[rd]);
                    wr32(addr + 4, cpu->r[rd + 1]);
                    if (!pre || wb) cpu->r[rn] = at;
                }
                return;
            }
            if ((op & 0x0fff0ff0) == 0x016f0f10) {  /* CLZ */
                u32 v = cpu->r[op & 15], n = 0;
                if (v == 0) n = 32; else while (!(v & 0x80000000u)) { v <<= 1; ++n; }
                cpu->r[rd] = n;
                return;
            }
            if ((op & 0x0f900ff0) == 0x01000050) {  /* QADD, QSUB, QDADD, QDSUB */
                const s32 a = (s32)cpu->r[op & 15];
                s32 b = (s32)cpu->r[rn];
                const u32 kind = (op >> 21) & 3;
                if (kind & 2) b = saturate((s64)b * 2, cpu);
                cpu->r[rd] = (u32)saturate((kind & 1) ? (s64)a - b : (s64)a + b, cpu);
                return;
            }
            if ((op & 0x0f900090) == 0x01000080) {  /* SMLAxy, SMLAWy, SMULWy, SMLALxy, SMULxy */
                const u32 kind = (op >> 21) & 3, rdm = (op >> 16) & 15, ra = (op >> 12) & 15;
                const s32 m = (s32)cpu->r[op & 15], s = (s32)cpu->r[(op >> 8) & 15];
                const s32 x = (op & 0x20) ? m >> 16 : (s32)(s16)m;
                const s32 y = (op & 0x40) ? s >> 16 : (s32)(s16)s;
                if (kind == 0) {
                    const s64 r = (s64)x * y + (s32)cpu->r[ra];
                    if (r > 0x7fffffffll || r < -0x80000000ll) cpu->q = 1;
                    cpu->r[rdm] = (u32)r;
                } else if (kind == 1) {
                    const s64 r = ((s64)m * y) >> 16;
                    if (op & 0x20) {
                        cpu->r[rdm] = (u32)r;  /* SMULWy */
                    } else {
                        const s64 t = r + (s32)cpu->r[ra];
                        if (t > 0x7fffffffll || t < -0x80000000ll) cpu->q = 1;
                        cpu->r[rdm] = (u32)t;  /* SMLAWy */
                    }
                } else if (kind == 2) {
                    u64 acc = (u64)cpu->r[rdm] << 32 | cpu->r[ra];
                    acc += (u64)(s64)((s64)x * y);
                    cpu->r[ra] = (u32)acc;
                    cpu->r[rdm] = (u32)(acc >> 32);
                } else {
                    cpu->r[rdm] = (u32)(x * y);
                }
                return;
            }
            if ((op & 0x0fbf0fff) == 0x010f0000) {  /* MRS */
                cpu->r[rd] = get_cpsr(cpu);
                return;
            }
        }
        if ((op & 0x0db0f000) == 0x0120f000) {  /* MSR: only the flags matter here */
            u32 v = (op & (1u << 25)) ? shift(cpu, op & 0xff, 3, ((op >> 8) & 15) * 2, 1, &(u32){0})
                                      : cpu->r[op & 15];
            if (!(op & (1u << 22)) && (op & (1u << 19))) {
                cpu->n = v >> 31; cpu->z = (v >> 30) & 1; cpu->c = (v >> 29) & 1;
                cpu->v = (v >> 28) & 1; cpu->q = (v >> 27) & 1;
            }
            return;
        }
        {  /* data processing */
            const u32 opcode = (op >> 21) & 15;
            const int s = (op >> 20) & 1;
            u32 operand, carry, a = cpu->r[rn], r;
            if (op & (1u << 25)) {
                const u32 rot = ((op >> 8) & 15) * 2;
                operand = rot ? ((op & 0xff) >> rot) | ((op & 0xff) << (32 - rot)) : (op & 0xff);
                carry = rot ? operand >> 31 : cpu->c;
            } else if (op & 0x10) {
                u32 v = cpu->r[op & 15];
                if ((op & 15) == 15) v += 4;
                if (rn == 15) a += 4;
                operand = shift(cpu, v, (op >> 5) & 3, cpu->r[(op >> 8) & 15] & 0xff, 1, &carry);
            } else {
                operand = shift(cpu, cpu->r[op & 15], (op >> 5) & 3, (op >> 7) & 31, 0, &carry);
            }
            switch (opcode) {
            case 0x0: r = a & operand; break;
            case 0x1: r = a ^ operand; break;
            case 0x2: r = sub_flags(cpu, a, operand, 1, s); break;
            case 0x3: r = sub_flags(cpu, operand, a, 1, s); break;
            case 0x4: r = add_flags(cpu, a, operand, 0, s); break;
            case 0x5: r = add_flags(cpu, a, operand, cpu->c, s); break;
            case 0x6: r = sub_flags(cpu, a, operand, cpu->c, s); break;
            case 0x7: r = sub_flags(cpu, operand, a, cpu->c, s); break;
            case 0x8: r = a & operand; set_flags_nz(cpu, r); cpu->c = carry; return;
            case 0x9: r = a ^ operand; set_flags_nz(cpu, r); cpu->c = carry; return;
            case 0xa: sub_flags(cpu, a, operand, 1, 1); return;
            case 0xb: add_flags(cpu, a, operand, 0, 1); return;
            case 0xc: r = a | operand; break;
            case 0xd: r = operand; break;
            case 0xe: r = a & ~operand; break;
            default: r = ~operand; break;
            }
            if (s && (opcode < 2 || opcode >= 0xc)) {
                set_flags_nz(cpu, r);
                cpu->c = carry;
            }
            if (rd == 15) branch(cpu, r, s ? 1 : 0); else cpu->r[rd] = r;
            return;
        }
    }
    case 2:
    case 3: {  /* LDR, STR, LDRB, STRB */
        const int pre = (op >> 24) & 1, up = (op >> 23) & 1, byte = (op >> 22) & 1;
        const int wb = (op >> 21) & 1, load = (op >> 20) & 1;
        u32 offset, carry, base = cpu->r[rn], at, addr;
        if (op & (1u << 25)) {
            if (op & 0x10) fail(cpu, op, "undefined (media) instruction");
            offset = shift(cpu, cpu->r[op & 15], (op >> 5) & 3, (op >> 7) & 31, 0, &carry);
        } else {
            offset = op & 0xfff;
        }
        at = up ? base + offset : base - offset;
        addr = pre ? at : base;
        if (load) {
            const u32 v = byte ? rd8(addr) : ldr(addr);
            if (!pre || wb) cpu->r[rn] = at;
            if (rd == 15) branch(cpu, v, 1); else cpu->r[rd] = v;
        } else {
            const u32 v = rd == 15 ? cpu->r[15] + 4 : cpu->r[rd];
            if (byte) wr8(addr, v); else wr32(addr, v);
            if (!pre || wb) cpu->r[rn] = at;
        }
        return;
    }
    case 4:
        block_transfer(cpu, op);
        return;
    case 5:  /* B, BL */
        if (op & (1u << 24)) cpu->r[14] = cpu->pc;
        branch(cpu, cpu->r[15] + (u32)((s32)(op << 8) >> 6), 0);
        return;
    case 6:
        fail(cpu, op, "coprocessor load/store");
        return;
    default:
        if ((op & 0x0f000000) == 0x0f000000) {
            swi(cpu, (op >> 16) & 0xff, op);
            return;
        }
        if ((op & 0x0f000010) == 0x0e000010) {  /* MCR, MRC: CP15 cache and TCM control */
            if (op & (1u << 20)) cpu->r[rd] = 0;
            return;
        }
        fail(cpu, op, "coprocessor data operation");
    }
}

/* --- Thumb ------------------------------------------------------------------------ */

static void exec_thumb(Cpu *cpu, u32 op)
{
    const u32 top = op >> 11;
    switch (top) {
    case 0x00: case 0x01: case 0x02: {  /* LSL, LSR, ASR immediate */
        u32 carry;
        const u32 rd = op & 7, v = cpu->r[(op >> 3) & 7];
        const u32 r = shift(cpu, v, top, (op >> 6) & 31, 0, &carry);
        cpu->r[rd] = r;
        set_flags_nz(cpu, r);
        cpu->c = carry;
        return;
    }
    case 0x03: {  /* ADD, SUB register / 3-bit immediate */
        const u32 rd = op & 7, a = cpu->r[(op >> 3) & 7];
        const u32 b = (op & 0x400) ? (op >> 6) & 7 : cpu->r[(op >> 6) & 7];
        cpu->r[rd] = (op & 0x200) ? sub_flags(cpu, a, b, 1, 1) : add_flags(cpu, a, b, 0, 1);
        return;
    }
    case 0x04: cpu->r[(op >> 8) & 7] = op & 0xff; set_flags_nz(cpu, op & 0xff); return;  /* MOV */
    case 0x05: sub_flags(cpu, cpu->r[(op >> 8) & 7], op & 0xff, 1, 1); return;             /* CMP */
    case 0x06: { const u32 r = (op >> 8) & 7; cpu->r[r] = add_flags(cpu, cpu->r[r], op & 0xff, 0, 1); return; }
    case 0x07: { const u32 r = (op >> 8) & 7; cpu->r[r] = sub_flags(cpu, cpu->r[r], op & 0xff, 1, 1); return; }
    case 0x08:
        if (!(op & 0x400)) {  /* ALU operations */
            const u32 rd = op & 7, rs = (op >> 3) & 7;
            const u32 a = cpu->r[rd], b = cpu->r[rs];
            u32 r, carry;
            switch ((op >> 6) & 15) {
            case 0x0: r = a & b; break;
            case 0x1: r = a ^ b; break;
            case 0x2: r = shift(cpu, a, 0, b & 0xff, 1, &carry); cpu->c = carry; break;
            case 0x3: r = shift(cpu, a, 1, b & 0xff, 1, &carry); cpu->c = carry; break;
            case 0x4: r = shift(cpu, a, 2, b & 0xff, 1, &carry); cpu->c = carry; break;
            case 0x5: r = add_flags(cpu, a, b, cpu->c, 1); break;
            case 0x6: r = sub_flags(cpu, a, b, cpu->c, 1); break;
            case 0x7: r = shift(cpu, a, 3, b & 0xff, 1, &carry); cpu->c = carry; break;
            case 0x8: set_flags_nz(cpu, a & b); return;
            case 0x9: r = sub_flags(cpu, 0, b, 1, 1); break;
            case 0xa: sub_flags(cpu, a, b, 1, 1); return;
            case 0xb: add_flags(cpu, a, b, 0, 1); return;
            case 0xc: r = a | b; break;
            case 0xd: r = a * b; break;
            case 0xe: r = a & ~b; break;
            default: r = ~b; break;
            }
            cpu->r[rd] = r;
            set_flags_nz(cpu, r);
            return;
        } else {  /* high registers, BX, BLX */
            const u32 rd = (op & 7) | ((op >> 4) & 8), rs = (op >> 3) & 15;
            switch ((op >> 8) & 3) {
            case 0: {
                const u32 r = cpu->r[rd] + cpu->r[rs];
                if (rd == 15) branch(cpu, r, 0); else cpu->r[rd] = r;
                return;
            }
            case 1: sub_flags(cpu, cpu->r[rd], cpu->r[rs], 1, 1); return;
            case 2:
                if (rd == 15) branch(cpu, cpu->r[rs], 0); else cpu->r[rd] = cpu->r[rs];
                return;
            default: {
                const u32 target = cpu->r[rs];
                if (op & 0x80) cpu->r[14] = cpu->pc | 1;
                branch(cpu, target, 1);
                return;
            }
            }
        }
    case 0x09:  /* LDR pc-relative */
        cpu->r[(op >> 8) & 7] = rd32((cpu->r[15] & ~2u) + (op & 0xff) * 4);
        return;
    case 0x0a: case 0x0b: {  /* load/store with a register offset */
        const u32 rd = op & 7, addr = cpu->r[(op >> 3) & 7] + cpu->r[(op >> 6) & 7];
        switch ((op >> 9) & 7) {
        case 0: wr32(addr, cpu->r[rd]); break;
        case 1: wr16(addr, cpu->r[rd]); break;
        case 2: wr8(addr, cpu->r[rd]); break;
        case 3: cpu->r[rd] = (u32)(s32)(s8)rd8(addr); break;
        case 4: cpu->r[rd] = ldr(addr); break;
        case 5: cpu->r[rd] = rd16(addr); break;
        case 6: cpu->r[rd] = rd8(addr); break;
        default: cpu->r[rd] = (u32)(s32)(s16)rd16(addr); break;
        }
        return;
    }
    case 0x0c: wr32(cpu->r[(op >> 3) & 7] + ((op >> 6) & 31) * 4, cpu->r[op & 7]); return;
    case 0x0d: cpu->r[op & 7] = ldr(cpu->r[(op >> 3) & 7] + ((op >> 6) & 31) * 4); return;
    case 0x0e: wr8(cpu->r[(op >> 3) & 7] + ((op >> 6) & 31), cpu->r[op & 7]); return;
    case 0x0f: cpu->r[op & 7] = rd8(cpu->r[(op >> 3) & 7] + ((op >> 6) & 31)); return;
    case 0x10: wr16(cpu->r[(op >> 3) & 7] + ((op >> 6) & 31) * 2, cpu->r[op & 7]); return;
    case 0x11: cpu->r[op & 7] = rd16(cpu->r[(op >> 3) & 7] + ((op >> 6) & 31) * 2); return;
    case 0x12: wr32(cpu->r[13] + (op & 0xff) * 4, cpu->r[(op >> 8) & 7]); return;
    case 0x13: cpu->r[(op >> 8) & 7] = ldr(cpu->r[13] + (op & 0xff) * 4); return;
    case 0x14: cpu->r[(op >> 8) & 7] = (cpu->r[15] & ~2u) + (op & 0xff) * 4; return;
    case 0x15: cpu->r[(op >> 8) & 7] = cpu->r[13] + (op & 0xff) * 4; return;
    case 0x16: case 0x17:
        if ((op & 0xff00) == 0xb000) {  /* ADD sp, #imm */
            const u32 imm = (op & 0x7f) * 4;
            cpu->r[13] = (op & 0x80) ? cpu->r[13] - imm : cpu->r[13] + imm;
            return;
        }
        if ((op & 0xf600) == 0xb400) {  /* PUSH, POP */
            const u32 list = op & 0xff;
            const int extra = (op >> 8) & 1;
            if (op & 0x800) {  /* POP */
                u32 addr = cpu->r[13];
                for (int i = 0; i < 8; ++i) {
                    if (list & (1u << i)) { cpu->r[i] = rd32(addr); addr += 4; }
                }
                if (extra) {
                    const u32 v = rd32(addr);
                    addr += 4;
                    cpu->r[13] = addr;
                    branch(cpu, v, 1);  /* ARMv5: POP {pc} interworks */
                    return;
                }
                cpu->r[13] = addr;
            } else {
                u32 count = extra;
                u32 addr;
                for (int i = 0; i < 8; ++i) count += (list >> i) & 1;
                addr = cpu->r[13] - count * 4;
                cpu->r[13] = addr;
                for (int i = 0; i < 8; ++i) {
                    if (list & (1u << i)) { wr32(addr, cpu->r[i]); addr += 4; }
                }
                if (extra) wr32(addr, cpu->r[14]);
            }
            return;
        }
        if ((op & 0xff00) == 0xbe00) fail(cpu, op, "BKPT");
        fail(cpu, op, "undefined Thumb instruction");
        return;
    case 0x18: case 0x19: {  /* STMIA, LDMIA */
        const u32 rb = (op >> 8) & 7, list = op & 0xff;
        u32 addr = cpu->r[rb];
        for (int i = 0; i < 8; ++i) {
            if (!(list & (1u << i))) continue;
            if (op & 0x800) cpu->r[i] = rd32(addr); else wr32(addr, cpu->r[i]);
            addr += 4;
        }
        if (!(op & 0x800) || !(list & (1u << rb))) cpu->r[rb] = addr;
        return;
    }
    case 0x1a: case 0x1b: {  /* conditional branch, SWI */
        const u32 cond = (op >> 8) & 15;
        if (cond == 0xf) { swi(cpu, op & 0xff, op); return; }
        if (cond == 0xe) fail(cpu, op, "undefined Thumb instruction");
        if (condition(cpu, cond)) branch(cpu, cpu->r[15] + (u32)((s32)(s8)(op & 0xff) * 2), 0);
        return;
    }
    case 0x1c:  /* B */
        branch(cpu, cpu->r[15] + (u32)(((s32)(op << 21)) >> 20), 0);
        return;
    case 0x1e:  /* BL / BLX prefix */
        cpu->r[14] = cpu->r[15] + (u32)(((s32)(op << 21)) >> 9);
        return;
    case 0x1f: {  /* BL suffix */
        const u32 target = cpu->r[14] + (op & 0x7ff) * 2;
        cpu->r[14] = cpu->pc | 1;
        branch(cpu, target, 0);
        return;
    }
    default: {  /* 0x1d: BLX suffix, to ARM */
        const u32 target = (cpu->r[14] + (op & 0x7ff) * 2) & ~3u;
        cpu->r[14] = cpu->pc | 1;
        cpu->thumb = 0;
        branch(cpu, target, 0);
        return;
    }
    }
}

/* --- Entry ------------------------------------------------------------------------ */

/* KHDAYS_TRACE_ARM: each entry the first times it runs, with the length of
 * the run in instructions. */
static int trace = -1;
static u32 seen[64], seen_count[64];
static u64 seen_steps[64];
static int nseen;

/* KHDAYS_TRACE_ARM: calls and instructions per entry so far (runtime.c, once
 * a second). */
void khdays_arm_report(void)
{
    if (trace <= 0 || nseen == 0) {
        return;
    }
    fprintf(stderr, "arm:");
    for (int i = 0; i < nseen; ++i) {
        fprintf(stderr, " 0x%08x x%u (%llu)", seen[i], seen_count[i], (unsigned long long)seen_steps[i]);
    }
    fprintf(stderr, "\n");
}

static void run(Cpu *cpu)
{
    const u32 entry = cpu->pc | (u32)cpu->thumb;
    u64 steps = 0;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_ARM") != NULL;
    }
    while (!cpu->done) {
        ++steps;
        const u32 at = cpu->pc;
        khdays_arm_pc = at;
        if (cpu->thumb) {
            const u32 op = *(const u16 *)(size_t)at;
            cpu->r[15] = at + 4;
            cpu->pc = at + 2;
            exec_thumb(cpu, op);
        } else {
            const u32 op = *(const u32 *)(size_t)at;
            cpu->r[15] = at + 8;
            cpu->pc = at + 4;
            exec_arm(cpu, op);
        }
    }
    if (trace) {
        int i;
        for (i = 0; i < nseen && seen[i] != entry; ++i) {
        }
        if (i == nseen && nseen < 64) {
            seen[nseen++] = entry;
        }
        if (i < 64) {
            seen_steps[i] += steps;
        }
        if (i < 64 && seen_count[i]++ < 3) {
            fprintf(stderr, "arm: 0x%08x ran %llu instructions, r0 = 0x%08x\n", entry,
                    (unsigned long long)steps, cpu->r[0]);
        }
    }
}

u64 khdays_arm_call(u32 entry, const u32 *args, int count)
{
    Cpu cpu;
    u32 saved_top, sp;
    if (stack_memory == NULL) {
        stack_memory = (u8 *)VirtualAlloc(NULL, STACK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (stack_memory == NULL) {
            fprintf(stderr, "khdays-native: no memory for the ARM interpreter's stack\n");
            exit(13);
        }
        /* short of the end: at the top of the address space base + size is 0 */
        stack_top = (u32)(size_t)stack_memory + STACK_SIZE - 256;
    }
    saved_top = stack_top;
    memset(&cpu, 0, sizeof(cpu));
    sp = (stack_top - 64) & ~7u;
    if (count > 4) {
        sp -= (u32)(count - 4) * 4;
        sp &= ~7u;
        for (int i = 4; i < count; ++i) {
            *(u32 *)(size_t)(sp + (u32)(i - 4) * 4) = args[i];
        }
    }
    for (int i = 0; i < 4 && i < count; ++i) {
        cpu.r[i] = args[i];
    }
    cpu.r[13] = sp;
    cpu.r[14] = RETURN_MAGIC;
    stack_top = sp - 64;  /* nested runs go below this one's frame */
    cpu.thumb = entry & 1;
    cpu.pc = entry & ~1u;
    {
        const int outer = on_arm7;  /* the ARM9's bus, whatever called */
        on_arm7 = 0;
        run(&cpu);
        on_arm7 = outer;
    }
    stack_top = saved_top;
    return (u64)cpu.r[1] << 32 | cpu.r[0];
}

/* --- The ARM7 ---------------------------------------------------------------------- */

void khdays_arm7cpu_hook(u32 address, KhdaysArmHook hook)
{
    if (hook_count == MAX_HOOKS) {
        fprintf(stderr, "khdays-native: too many ARM7 hooks\n");
        exit(13);
    }
    hook_address[hook_count] = address;
    hook_function[hook_count] = hook;
    ++hook_count;
}

void khdays_arm7cpu_bios(KhdaysArmSwi swi)
{
    arm7_bios = swi;
}

struct KhdaysArmThread {
    Cpu cpu;
};

/* Runs `cpu` on the ARM7's bus, then gives the bus back to whatever ran
 * before (an ARM7 run can start inside an ARM9 one: a native call the
 * interpreted ARM9 makes can talk to the ARM7). */
static void run_arm7(Cpu *cpu)
{
    const int outer = on_arm7;
    const u32 outer_pc = khdays_arm_pc;
    on_arm7 = 1;
    run(cpu);
    on_arm7 = outer;
    khdays_arm_pc = outer_pc;
}

u64 khdays_arm7cpu_call(u32 entry, const u32 *args, int count, u32 sp)
{
    Cpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    sp &= ~7u;
    if (count > 4) {
        sp -= (u32)(count - 4) * 4;
        sp &= ~7u;
        for (int i = 4; i < count; ++i) {
            *(u32 *)(size_t)(sp + (u32)(i - 4) * 4) = args[i];
        }
    }
    for (int i = 0; i < 4 && i < count; ++i) {
        cpu.r[i] = args[i];
    }
    cpu.r[13] = sp;
    cpu.r[14] = RETURN_MAGIC;
    cpu.thumb = entry & 1;
    cpu.pc = entry & ~1u;
    run_arm7(&cpu);
    if (cpu.done != 1) {
        fprintf(stderr, "khdays-native: the ARM7 routine 0x%08x waited, outside a thread\n", entry);
        exit(13);
    }
    return (u64)cpu.r[1] << 32 | cpu.r[0];
}

KhdaysArmThread *khdays_arm7cpu_thread(u32 entry, u32 arg, u32 sp)
{
    KhdaysArmThread *thread = (KhdaysArmThread *)calloc(1, sizeof(*thread));
    if (thread == NULL) {
        fprintf(stderr, "khdays-native: no memory for an ARM7 thread\n");
        exit(13);
    }
    thread->cpu.r[0] = arg;
    thread->cpu.r[13] = sp;
    thread->cpu.r[14] = RETURN_MAGIC;
    thread->cpu.thumb = entry & 1;
    thread->cpu.pc = entry & ~1u;
    return thread;
}

int khdays_arm7cpu_resume(KhdaysArmThread *thread)
{
    thread->cpu.done = 0;
    run_arm7(&thread->cpu);
    return thread->cpu.done == 1;
}

/* A native call landed in DS memory: the page is not executable. Run the
 * callee here, then return to the native caller with r1:r0 as EDX:EAX. */
void khdays_async_irq_undo(CONTEXT *context, const void *address);

static LONG CALLBACK on_execute(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    CONTEXT *context = info->ContextRecord;
    u32 target;
    khdays_async_irq_undo(context, record->ExceptionAddress);  /* an interrupt injected in flight */
    target = context->Eip;
    const u32 *stack;
    u64 result;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->ExceptionInformation[0] != 8 ||
        !is_ds_code(target & ~1u)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    stack = (const u32 *)(size_t)context->Esp;  /* return address, then the arguments */
    InterlockedIncrement(&khdays_async_irq_blocked);  /* no interrupt inside this handler */
    result = khdays_arm_call(target, stack + 1, 8);
    InterlockedDecrement(&khdays_async_irq_blocked);
    context->Eax = (DWORD)result;
    context->Edx = (DWORD)(result >> 32);
    context->Eip = stack[0];
    context->Esp += 4;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void khdays_arm_init(void)
{
    AddVectoredExceptionHandler(1, on_execute);
}
