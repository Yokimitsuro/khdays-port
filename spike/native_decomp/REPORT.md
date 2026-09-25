# Spike: compiling khdays-decomp natively

**Question.** Instead of re-implementing each screen by hand from the
decompilation, can the decompilation's own C be compiled for the PC and run
on a layer that stands in for the DS hardware (the approach of the Super
Mario 64 PC port, Ship of Harkinian and the pokeemerald ports)? That would
make the game's behaviour 1:1 by construction.

**Setup.** Pinned decomp `e20a592a7`. MSVC 19.5x (Visual Studio 18) for
**32-bit x86**, because the decomp stores pointers in `int` (~34k
`*(T *)(int + n)` dereferences), so a 64-bit build is out. C17
(`/std:c17 /TC`), C++17 for the 7 `.cpp` files. mwcc's own flags are
`-lang c99 -char signed -enum int -gccext,on`; MSVC's `char` is signed and its
enums are `int` already. Scripts: `compile_survey.py`, `link_survey.py`.

## 1. Compile: 99.21%

**24,385 of 24,580** source files compile unchanged (58 s on 28 threads).
The 195 that do not:

| cause | files | note |
|-------|-------|------|
| ARM `asm` (C2054 / C2065 / C2400) | 132 | NitroSDK OS / CARD / MI / MTX / init internals -- the hardware layer the port replaces anyway |
| zero-size arrays mid-struct (C2229) | 26 | `/std:clatest` or a mechanical fix |
| `__attribute__` (C2061 / C2146) | 22 | a `#define` away |
| other syntax (C2055, C2143, C5299) | 15 | label at end of block etc.; `/std:clatest` covers C5299 |

## 2. Link: everything fits in one image

All 24,385 objects -- every overlay side by side -- link together
(`/FORCE`, to list everything at once) with:

- **700 unresolved symbols**
  - **431 BSS** variables: every one is in the decomp's `symbols.txt` with
    its address, so their definitions can be generated.
  - **197 functions**: the not-yet-C remainder plus the SDK's assembly-only
    routines (`MI_CpuFill8`, `MIi_CpuClearFast`, `OS_DisableInterrupts`,
    `MTX_RotY33_`, ...).
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
