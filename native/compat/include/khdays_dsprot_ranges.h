/* Native stand-in for the decomp's src/overlays/ov028/dsprot/dsprot_ranges.h
 * (prepare.py points the includes here).
 *
 * On the DS, DS Protect keeps some instruction ranges encrypted in the ROM: the
 * opening marker (ARM assembly handing pc to Encryptor_StartRange) decrypts the
 * range in place before it runs, the closing one encrypts it again. Natively
 * the range is ordinary compiled C that was never encrypted, so both markers
 * are empty. */
#ifndef KHDAYS_DSPROT_RANGES_H
#define KHDAYS_DSPROT_RANGES_H

extern void func_ov028_0208a7e0(unsigned int *pRange);   /* Encryptor_StartRange */
extern void func_ov028_0208a8ac(unsigned int *pRange);   /* Encryptor_EndRange */

#define DSPROT_RANGE_BEGIN(key)
#define DSPROT_RANGE_END(key)

#endif
