# Spike: compiling khdays-decomp natively

**Question.** Instead of re-implementing each screen by hand from the
decompilation, can the decompilation's own C be compiled for the PC and run
on a layer that stands in for the DS hardware (the approach of the Super
Mario 64 PC port, Ship of Harkinian and the pokeemerald ports)? That would
make the game's behaviour 1:1 by construction.

**Setup.** First run at decomp `e20a592a7` (99.2% of functions in C); re-run
at `9791f2452`, when the decomp reached ~100% complete functions (99.5% real
C, 99.73% of code bytes; the rest is library code whose original source is
assembly). The numbers below are the re-run's, with the first run's in
brackets. MSVC 19.5x (Visual Studio 18) for
**32-bit x86**, because the decomp stores pointers in `int` (~34k
`*(T *)(int + n)` dereferences), so a 64-bit build is out. C17
(`/std:c17 /TC`), C++17 for the 7 `.cpp` files. mwcc's own flags are
`-lang c99 -char signed -enum int -gccext,on`; MSVC's `char` is signed and its
enums are `int` already. Scripts: `compile_survey.py`, `link_survey.py`.

## 1. Compile: 99.31% [99.21%]

**24,458 of 24,629** source files compile unchanged [24,385 of 24,580]. The
171 that do not [195]:

| cause | game (`src/`) | libraries (`libs/`) | note |
|-------|---------------|---------------------|------|
| inline ARM `asm` | 10 | 98 | NitroSDK OS / CARD / MI / MTX / init internals -- the hardware layer the port replaces anyway |
| zero-size arrays mid-struct (C2229) | 26 | -- | a GNU extension MSVC rejects; mechanical |
| `__attribute__` | 1 | 20 | a `#define` away |
| unnamed parameters in definitions (C2055) | -- | 7 | name them |
| label at end of block (C5299) | -- | 3 | `/std:clatest` fixes these (and only these) |
| other syntax | 6 | -- | |

So only 43 files of the game's own code fail, and 26 of those for one
mechanical reason.

## 2. Link: everything fits in one image

All 24,385 objects -- every overlay side by side -- link together
(`/FORCE`, to list everything at once) with:

- **632 unresolved symbols** [700]
  - **431 BSS** variables: every one is in the decomp's `symbols.txt` with
    its address, so their definitions can be generated.
  - **129 functions** [197]. Every one has source in the decomp: 122 live in
    the files above that failed to compile (67 SDK routines such as
    `MI_CpuFill8`, `MIi_CpuClearFast`, `OS_DisableInterrupts`, `MTX_RotY33_`,
    `DC_FlushRange`, plus 55 game-named ones), and 7 are `.s` files -- the
    CodeWarrior runtime's division helpers (`func_02020400`, `02020368`,
    `0202060c`, ...) and MobiClip's FastAudio decoder, which KH Days never
    runs.
  - **47 initialized data** objects the decomp keeps in assembly.
  - **25 toolchain symbols**: `OVERLAY_n_ID`, `SDK_*_STACKSIZE`,
    `memset` / `memcpy`.
- **57 duplicate symbols**, mostly NitroSDK thread internals.

## 3. Direct hardware access

Pointer casts of literal DS addresses in the C: ~1,180.

| region | uses | modules |
|--------|------|---------|
| 2D / DMA / timers / keys (0x04000000-0x040003ff) | 694 | 26 |
| sub 2D engine (0x04001xxx) | 317 | 20 |
| shared work RAM (0x027fxxxx) | 64 | 9 |
| 3D geometry engine (0x04000400-0x040006ff) | 55 | 4 |
| palette / VRAM / OAM | 42 | ~15 |
| ITCM / DTCM / IPC | 8 | 3 |

Almost all are state registers (display and BG control, blending, master
brightness) that only need to land in memory for a renderer to read each
frame. The geometry engine is touched directly in only 4 modules; the rest
goes through a few SDK chokepoints (the NNS G3D geometry buffer and
`MI_SendGXCommand`), which is where 3D would be intercepted.

## Verdict

Feasible. The game's own code is not the obstacle -- it compiles and links
almost as it stands. The work is the layer underneath, all of it outside the
decomp:

1. **Link cleanly**: generate the BSS definitions from `symbols.txt`,
   extract the 47 data objects, write the ~200 SDK/asm routines in C, settle
   the duplicates, and fix the ~60 syntax cases.
2. **Memory map**: reserve the DS regions at their real virtual addresses in
   the 32-bit process (ITCM, DTCM, shared RAM, I/O, palette, VRAM, OAM) so
   literal accesses just work; give each overlay load its DS semantics
   (`.data` restored, `.bss` cleared).
3. **Runtime**: `main`, OS threads and VBlank on the port's frame loop;
   `FS` on `khdays::vfs` (mods keep working); saves to files.
4. **2D renderer**: both engines (BG modes, OBJ, windows, blending, master
   brightness) from the mapped registers and memory. First target: the boot
   logos drawn by the real ov000.
5. **3D renderer**: the geometry command stream from the SDK chokepoints.
6. **Sound**: the ARM7 side is not in the decomp; drive the port's SSEQ/STRM
   players from the NNS SND calls.

Reusable from the current port: the SDL platform, `vfs` and mods, the audio
synth, the MobiClip decoder, the asset decoders (tools, HD replacements).
