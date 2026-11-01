/* The enemies' code, each class's apart. On the DS an enemy class's code is an overlay (114-301: 113 + 1 + the byte
 * data_ov107_020cb6a4 gives the class in a column) loaded into six shared columns of RAM, so an area could only hold
 * the classes whose code fitted together; a class with no column left stopped the game (Ov107_SpawnEntityClass:
 * OS_Terminate). Here each of those overlays has its own place in the game's memory (rom.cpp moves it in the
 * overlay table, prepare.py its symbols, both from these lines), and ov107's slot tables have room for every class:
 * the six columns first, then one slot for each class whose columns are full (prepare.py's join).
 * docs/RUNTIME_MAP.md, "The enemy code slots". */
#ifndef KHDAYS_ENEMY_OVERLAYS_H
#define KHDAYS_ENEMY_OVERLAYS_H

/* (prepare.py reads these defines: plain numbers) */
#define KHDAYS_ENEMY_OVERLAY_FIRST 114
#define KHDAYS_ENEMY_OVERLAY_LAST 301
#define KHDAYS_ENEMY_OVERLAYS 0x09000000u      /* overlay n at this + (n - FIRST) x SPAN */
#define KHDAYS_ENEMY_OVERLAY_SPAN 0x10000u     /* the largest is 0xb3a0 bytes with its BSS */
#define KHDAYS_ENEMY_SLOT_COUNT 126u           /* the six columns, then data_ov107_020cbb20's 120 classes */
#define KHDAYS_ENEMY_HANDLERS 0x09c00000u      /* data_ov107_020cbb08: a class's factory, 4 bytes a slot */
#define KHDAYS_ENEMY_OVERLAY_INFOS 0x09c00200u /* data_ov107_020cbb98: its overlay's info, 0x2c bytes a slot */
#define KHDAYS_ENEMY_SPAWN_RECORDS 0x09c01800u /* data_ov107_020cbca0: its spawn record, 0x6a bytes a slot */
#define KHDAYS_ENEMY_SLOTS_END 0x09c10000u

#endif
