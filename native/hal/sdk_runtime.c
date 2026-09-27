/* CodeWarrior's ARM runtime helpers, which the decompilation keeps as
 * assembly (libs/msl/runtime/asm_stubs, libs/msl/c/asm_stubs). Semantics
 * follow that assembly. The 32-bit divisions return the quotient in r0 and the
 * remainder in r1; on x86 both come back as one 64-bit value (EAX:EDX), which
 * serves callers that declared an int return and callers that read both. */
#include "hal.h"

/* _s32_div_f: truncating division; a zero divisor returns the dividend as the
 * quotient and 0 as the remainder (the assembly's early exit). */
u64 func_02020400(s32 numerator, s32 denominator)
{
    s32 quotient = numerator;
    s32 remainder = 0;
    if (denominator != 0) {
        quotient = numerator / denominator;
        remainder = numerator % denominator;
    }
    return ((u64)(u32)remainder << 32) | (u32)quotient;
}

/* _u32_div_f: as above, unsigned. */
u64 func_0202060c(u32 numerator, u32 denominator)
{
    u32 quotient = numerator;
    u32 remainder = 0;
    if (denominator != 0) {
        quotient = numerator / denominator;
        remainder = numerator % denominator;
    }
    return ((u64)remainder << 32) | quotient;
}

/* _ll_sdiv / _ll_mod / _ll_udiv / _ull_mod: 64-bit division. A zero divisor
 * is not modelled yet. */
s64 func_020201b8(s64 numerator, s64 denominator)
{
    if (denominator == 0) {
        khdays_hal_unimplemented("_ll_sdiv by zero");
    }
    return numerator / denominator;
}

s64 func_020201a8(s64 numerator, s64 denominator)
{
    if (denominator == 0) {
        khdays_hal_unimplemented("_ll_mod by zero");
    }
    return numerator % denominator;
}

u64 func_02020368(u64 numerator, u64 denominator)
{
    if (denominator == 0) {
        khdays_hal_unimplemented("_ll_udiv by zero");
    }
    return numerator / denominator;
}

u64 func_02020374(u64 numerator, u64 denominator)
{
    if (denominator == 0) {
        khdays_hal_unimplemented("_ull_mod by zero");
    }
    return numerator % denominator;
}

/* _ll_shl: shift left by the count mod 64. */
u64 func_020203d0(u64 value, u32 count)
{
    return value << (count & 63u);
}

/* _ll_mul */
s64 _ll_mul(s64 a, s64 b)
{
    return (s64)((u64)a * (u64)b);
}
