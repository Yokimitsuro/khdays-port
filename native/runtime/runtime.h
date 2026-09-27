/* The native runtime: what hosts the recompiled game on a PC. */
#ifndef KHDAYS_RUNTIME_H
#define KHDAYS_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Maps the DS address space at its real virtual addresses (memory.c). Must
 * run before anything touches DS memory; returns 0 (and says why) when an
 * address range is already taken. */
int khdays_memory_map(void);

/* Brings up the host side (window, audio, files, threads). */
int khdays_runtime_init(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
