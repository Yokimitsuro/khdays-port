/* The game's two heaps: made by the game's Ov001_CreateMainAndSubHeaps through khdays_game_heap
 * (prepare.py), over the port's region of the game's memory (window.h). */
#ifndef KHDAYS_GAME_HEAP_H
#define KHDAYS_GAME_HEAP_H

/* The game's heap `which` (0 main, 1 sub) made, its host address returned: over its span of the
 * heaps' region, or with KHDAYS_DS_MEMORY (testing only) from the DS's main arena, `ds_size` bytes.
 * Called from the game's C. */
void *khdays_game_heap(int which, unsigned int ds_size);

/* Whether the heaps are made as on the DS (KHDAYS_DS_MEMORY set). */
int khdays_game_heap_ds_memory(void);

/* A heap's figures (the game's NitroSystem expanded heap at `heap`, a
 * reference): its start and end, its free bytes in how many blocks, and the
 * largest; 0 when `heap` is not one in the game's RAM. */
typedef struct {
    unsigned start, end, free, blocks, largest;
} KhdaysHeapFigures;
int khdays_game_heap_figures(unsigned heap, KhdaysHeapFigures *out);

/* The heap's name for the log: "main heap", "sub heap", or "heap at <ref>". */
const char *khdays_game_heap_name(unsigned heap, char *buffer, unsigned size);

/* NNS_FndAllocFromExpHeapEx gave no block (port/tools/prepare.py): said in
 * the log, once per heap and size. Called from the game's C. */
void khdays_game_heap_failed(void *heap, unsigned int size);

/* The headers' heaps (window.h), all called from the game's C: a packed archive's header allocated
 * from the current heap's headers' heap; the heap a block is freed to; a game heap remade remakes
 * its headers' heap; and a file handle's header checked to lie where headers are. */
void *khdays_header_alloc(unsigned int size);
void *khdays_header_heap_of(void *heap, void *block);
void khdays_game_heap_recreated(void *heap);
void khdays_handle_check(unsigned int handle, unsigned int base);

/* The game's resource cache (ResCache_FindSlot) past its 128 entries, called from the game's C: its table `n` (0 the
 * game's, then the port's, NULL past them) and, when every entry is in use, a new table of 128 from the main heap
 * (its first entry; NULL when the heap has no room). */
void *khdays_res_cache_table(int n);
void *khdays_res_cache_grow(void);

#endif
