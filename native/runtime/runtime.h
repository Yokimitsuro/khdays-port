/* The native runtime: what hosts the recompiled game on a PC. */
#ifndef KHDAYS_RUNTIME_H
#define KHDAYS_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Crash and stall reports (diag.c). */
void khdays_diag_init(void);

/* The native address of the function named `name` (from the PDB), or 0. */
unsigned khdays_diag_symbol(const char *name);

/* Maps the DS address space at its real virtual addresses (memory.c). Must
 * run before anything touches DS memory; returns 0 (and says why) when an
 * address range is already taken. */
int khdays_memory_map(void);

/* The main-RAM state the BIOS and firmware leave for the game (boot.c). */
void khdays_boot_environment(void);

/* Copies a module's native data initializers into place: -1 static, -2 ITCM,
 * -3 DTCM, else an overlay id (data_init.c). */
void khdays_data_init(int module);

/* Brings up the host side (window, audio, files, threads). */
int khdays_runtime_init(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
