#!/usr/bin/env python3
"""Prepare the pinned khdays-decomp for the native (x64) build: choose the files the DS build links,
patch the few MSVC cannot take, generate what the DS linker provided, and rewrite the game's C with
every pointer a 4-byte reference (docs/BUILD.md).
Usage: python port/tools/prepare.py [--out build/port/gen-x64]
"""

from __future__ import annotations

import argparse
import bisect
import collections
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import abi_repair  # noqa: E402
import registers  # noqa: E402
import font_layout  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DECOMP = ROOT / "external" / "khdays-decomp"
CONFIG = DECOMP / "config" / "arm9"

SECTION_RE = re.compile(r"^\s+(\.\w+)\s+start:0x([0-9a-fA-F]+)\s+end:0x([0-9a-fA-F]+)")
SYMBOL_RE = re.compile(r"^(\S+)\s+kind:(\w+)(?:\(([^)]*)\))?\s+addr:0x([0-9a-fA-F]+)")


@dataclass
class Module:
    name: str
    config: Path
    sections: dict[str, tuple[int, int]] = field(default_factory=dict)
    files: list[str] = field(default_factory=list)
    symbols: list[tuple[str, str, int]] = field(default_factory=list)  # name, kind, addr
    ranges: dict[str, list[tuple[int, int]]] = field(default_factory=dict)  # file -> sections
    thumb: set[str] = field(default_factory=set)  # Thumb functions
    sizes: dict[str, int] = field(default_factory=dict)  # function -> bytes


def load_modules() -> list[Module]:
    dirs = [CONFIG, CONFIG / "itcm", CONFIG / "dtcm"]
    dirs += sorted(p for p in (CONFIG / "overlays").iterdir() if p.is_dir())
    modules = []
    for d in dirs:
        delinks = d / "delinks.txt"
        if not delinks.exists():
            continue
        m = Module("main" if d == CONFIG else d.name, d)
        in_header = True
        for line in delinks.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                in_header = False
                continue
            if in_header:
                s = SECTION_RE.match(line)
                if s:
                    m.sections[s[1]] = (int(s[2], 16), int(s[3], 16))
                continue
            if line.endswith(":") and not line.startswith(" "):
                m.files.append(line[:-1])
                continue
            s = SECTION_RE.match(line)
            if s and m.files:
                m.ranges.setdefault(m.files[-1], []).append((int(s[2], 16), int(s[3], 16)))
        syms = d / "symbols.txt"
        if syms.exists():
            for line in syms.read_text(encoding="utf-8").splitlines():
                s = SYMBOL_RE.match(line)
                if s:
                    m.symbols.append((s[1], s[2], int(s[4], 16)))
                    if s[2] == "function" and (s[3] or "").startswith("thumb"):
                        m.thumb.add(s[1])
                    size = re.search(r"size=0x([0-9a-fA-F]+)", s[3] or "")
                    if s[2] == "function" and size:
                        m.sizes[s[1]] = int(size[1], 16)
        modules.append(m)
    return modules


# --- Mechanical source transforms ---------------------------------------------

# `register int x asm("r4") = 0;` -- an ARM register binding; drop it.
REGISTER_ASM = re.compile(r"\basm\s*\(\s*\"r\d+\"\s*\)")
# `asm { clz x, y }` -- ARM count-leading-zeros; the compat header supplies it.
CLZ_ASM = re.compile(r"\basm\s*\{\s*clz\s+(\w+)\s*,\s*(\w+)\s*\}")
# Struct members whose array size is a constant expression equal to zero. A
# statement of the same shape (`return p[0];`) is no declaration: the line
# may not start with a statement keyword.
ZERO_MEMBER = re.compile(
    r"^([ \t]+)(?![ \t]|(?:return|else|case|goto|do)\b)[\w \t\*]+?\b(\w+)[ \t]*"
    r"\[([\w \t\+\-\*\(\)]+)\][ \t]*;[^\n]*$",
    re.MULTILINE,
)
# An object-like macro with a plain number as its value.
NUMERIC_DEFINE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+\(?(0[xX][0-9a-fA-F]+|\d+)[uU]?\)?[ \t]*$", re.MULTILINE
)
# `*(T (*)[N])dst = *(T (*)[N])src;` -- assigning a whole array, which mwcc
# accepts and C does not; copied element by element instead.
ARRAY_ASSIGN = re.compile(
    r"^([ \t]*)\*\((\w[\w \t]*?)[ \t]*\(\*\)\[([^\]\n]+)\]\)([^=\n]+?)[ \t]*=[ \t]*"
    r"\*\(\2[ \t]*\(\*\)\[\3\]\)([^;\n]+);",
    re.MULTILINE,
)
# DS Protect's encrypted-range markers are ARM assembly; see the native header.
DSPROT_RANGES_INCLUDE = re.compile(r'^([ \t]*#[ \t]*include[ \t]*)"dsprot_ranges\.h"', re.MULTILINE)
# A label closing a block (`out: }`), which C17 does not allow; `out: ; }` is
# the same program.
END_LABEL = re.compile(r"(^[ \t]*[A-Za-z_]\w*:)([ \t]*\n?[ \t]*\})", re.MULTILINE)
# A function definition header with an unnamed parameter such as `u32)`.
DEF_HEADER = re.compile(r"^(\w[\w \t\*]*\b\w+[ \t]*\()([^()\n]*)\)[ \t]*$", re.MULTILINE)
# A virtual member function (the decomp's C++ declares classes only to call
# through vtables whose entries are C functions taking `this` first). On the
# ARM `this` is simply r0; MSVC's default for members, __thiscall, passes it
# in ECX instead, so the C function would read its arguments one off.
# __cdecl members take `this` as the first stack argument, as the C does.
VIRTUAL_METHOD = re.compile(r"(\bvirtual\s+(?:[\w\*&]+\s+)*?)(~?[A-Za-z_]\w*\s*\()")
# mwcc gives a virtual destructor two vtable slots, the complete-object
# destructor and then the deleting one (`delete p` loads slot 1:
# ov024 0x02084ec4 `ldr r1,[r1,#4]`; data_ov024_020939c4 holds just those
# two), where MSVC gives it one. A placeholder declared first takes slot 0,
# so the destructor and every later method land on mwcc's slots.
VIRTUAL_DTOR = re.compile(r"^([ \t]*)(virtual __cdecl ~\w+\s*\(\s*\)\s*;)", re.MULTILINE)
# `delete p` of such a class: mwcc checks p and calls vtable slot 1 with p in
# r0 (ov024 0x02084ec4: `ldr r1,[r0]; ldr r1,[r1,#4]; blx r1`). MSVC keeps
# __thiscall for destructors whatever the declaration says, so the C function
# in that slot would get the delete flag as its argument; the call is spelled
# out instead.
DELETE_STATEMENT = re.compile(r"^([ \t]*)delete[ \t]+([^;\n]+);", re.MULTILINE)
# A definition with its return type on a line of its own (`void`, then
# `name(void)`): one line, as the definitions everything here matches are
# written. Only whitespace changes.
SPLIT_HEADER = re.compile(
    r"^((?:static[ \t]+|inline[ \t]+)*[A-Za-z_][\w \t\*]*?)[ \t]*\n"
    r"(?=[A-Za-z_]\w*[ \t]*\([^;{}]*\)[ \t]*\n?[ \t]*\{)",
    re.MULTILINE,
)
# An empty function (`bx lr` in the ROM) leaves r0 as its caller set it, and
# callers that read the result get their own first argument back -- the
# object dispatcher relies on it (an empty update keeps itself). Natively it
# returns its first argument, which it now takes.
EMPTY_FUNCTION = re.compile(r"^void[ \t]+(\w+)[ \t]*\([ \t]*(?:void)?[ \t]*\)[ \t]*\{[ \t]*\}[ \t]*$", re.MULTILINE)


# Whole modules the port replaces, none of their files built: the HAL defines what the game calls
# in them, and a call to anything else stops the game, naming it (runtime/ds_calls.c). ov105 is
# NitroSDK's wireless library (WM, its ARM7 protocol, wh.c): port/hal/sdk_wireless.c is it.
REPLACED_MODULES = {"ov105"}


# Where the runtime joins the game's own flow: for each function, a list of
# documented edits (text, replacement), applied in order; a text that is not
# there stops the script. A function is named by its key, module@address
# (function_keys), which stays put when the decomp renames it; the texts must
# match its current source.
HOOKS = {
    # The wireless layer's teardown loops: they spin on wh.c's
    # system state, which only the wireless interrupt advances; while it is
    # BUSY the loop halts at a wait point, where the port takes interrupts
    # and the wireless layer's deliveries (hal/sdk_irq.c), as the CPU would
    # take them between the loop's instructions.
    "main@020303d8": [
        (  # Game_RunSceneLoop
            "            case 3:\n                break;\n",
            "            case 3:\n                { extern void khdays_wait_point(void); khdays_wait_point(); }\n"
            "                break;\n",
        )
    ],
    # Mission Mode's Retry in the pause (runtime/menus/retry.c): yes on the "Are
    # you sure?" page of Give Up tells the port; after a Retry the game over
    # leaves at once, dark (its mode 8, the one its A gives in Mission Mode),
    # and the session menu asks for the field again instead of the results.
    "main@02022eb0": [
        (  # PauseMenu_ConfirmFrame
            "            case 2:\n                Ov002_PauseMissionScene(0);\n",
            "            case 2:\n                { extern void khdays_retry_confirmed(void); khdays_retry_confirmed(); }\n"
            "                Ov002_PauseMissionScene(0);\n",
        )
    ],
    "ov027@02082dac": [
        (  # Ov027_GameOverFadeIn
            "    pScene = NNSi_FndGetCurrentRootHeap();\n    pNext = 0;\n",
            "    pScene = NNSi_FndGetCurrentRootHeap();\n"
            "    { extern int khdays_retry_pending(void); if (khdays_retry_pending()) { pScene->nMode = 8; } }\n"
            "    pNext = 0;\n",
        )
    ],
    "ov002@02068720": [
        (  # Ov002_StepSessionMenu
            "        case 0:\n            Ov002_FillMissionResult();\n",
            "        case 0:\n"
            "            { extern int khdays_retry_take(void);\n"
            "              if (khdays_retry_take()) { Scene_RequestPending(SCENE_FIELD, 0); break; } }\n"
            "            Ov002_FillMissionResult();\n",
        )
    ],
    # The save lists in pages (runtime/menus/savelist.c): the game's questions (load, delete,
    # overwrite, create) name the file as its row + 1; with the page view the row's file is the
    # page's first + the row, so the number is the port's (khdays_savelist_file_number; the game's
    # own row + 1 without the pages).
    "ov000@020508a0": [  # Ov000_DrawLoadPageText
        (
            "void Ov000_DrawLoadPageText(void)\n",
            "extern int khdays_savelist_file_number(int slot);\nvoid Ov000_DrawLoadPageText(void)\n",
        ),
        ("data_ov000_0205ac24->pageIndex + 1", "khdays_savelist_file_number(data_ov000_0205ac24->pageIndex)"),
    ],
    "ov025@0209b7a4": [  # Ov025_SaveMenuDrawPrompt
        (
            "void Ov025_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n",
            "extern int khdays_savelist_file_number(int slot);\n"
            "void Ov025_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n",
        ),
        ("pMenu->nSlot + 1", "khdays_savelist_file_number(pMenu->nSlot)"),
    ],
    "ov008@020693c4": [  # Ov008_SaveMenuDrawPrompt
        (
            "void Ov008_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n",
            "extern int khdays_savelist_file_number(int slot);\n"
            "void Ov008_SaveMenuDrawPrompt(Ov008SaveMenu *pMenu, int nPhase)\n",
        ),
        ("pMenu->nSlot + 1", "khdays_savelist_file_number(pMenu->nSlot)"),
    ],
    "ov006@02051884": [
        (  # Ov006_MissionInitVideoScene
            "            case 3:\n                break;\n",
            "            case 3:\n                { extern void khdays_wait_point(void); khdays_wait_point(); }\n"
            "                break;\n",
        )
    ],
    "ov008@0207d5d8": [
        (  # Ov008_MissionInitVideoScene
            "            case 3:\n                break;\n",
            "            case 3:\n                { extern void khdays_wait_point(void); khdays_wait_point(); }\n"
            "                break;\n",
        )
    ],
    # The save written whole once a save action of the game's ends: every backup write, program or
    # erase ends in this stream command; a changed chip is committed to saves/khdays.sav atomically,
    # and a commit that fails is the game's failed save.
    "main@0200e4e8": [  # CARDi_RequestStreamCommandCore
        (
            "\tCARDi_EndTask(p, TRUE);\n",
            "\t{\n"
            "\t\textern int khdays_backup_commit(void);\n"
            "\t\tif (!khdays_backup_commit() && p->cmd->result == CARD_RESULT_SUCCESS) {\n"
            "\t\t\tp->cmd->result = CARD_RESULT_FAILURE;\n"
            "\t\t}\n"
            "\t}\n"
            "\tCARDi_EndTask(p, TRUE);\n",
        ),
    ],
    # NitroSDK's PXI without the IPC registers: the three functions that touch them hand the words
    # to the port's transport (runtime/core/pxi.h); the callbacks, the interrupt and the dispatch
    # stay the SDK's.
    "main@02009278": [  # PXI_InitFifo
        (
            "        *(volatile unsigned short *)0x04000184 = 0xc408;\n",
            "        { extern void khdays_pxi_init(void); khdays_pxi_init(); } /* IPCFIFOCNT's enable */\n",
        ),
        (
            "        count = 0;\n"
            "        for (;;) {\n"
            "            mark = *sync & 0xf;\n"
            "            *sync = (unsigned short)(mark << 8);\n"
            "            if (mark == 0 && count > 4) {\n"
            "                break;\n"
            "            }\n"
            "            timeout = 1000;\n"
            "            while ((*sync & 0xf) == mark) {\n"
            "                if (timeout <= 0) {\n"
            "                    count = 0;\n"
            "                    break;\n"
            "                }\n"
            "                timeout--;\n"
            "            }\n"
            "            count++;\n"
            "            }\n",
            "        /* the IPCSYNC handshake with the ARM7: none, natively */\n",
        ),
    ],
    "main@020093e8": [  # PXI_SendWordByFifo
        (
            "    if (pFifo->wCnt & 0x4000) {\n"
            "        pFifo->wCnt |= 0xc000;\n"
            "        return -1;\n"
            "    }\n"
            "    state = OS_DisableInterrupts();\n"
            "    if (pFifo->wCnt & 2) {\n"
            "        OS_RestoreInterrupts(state);\n"
            "        return -2;\n"
            "    }\n"
            "    pFifo->dwSend = *(u32 *)&packet;\n",
            "    (void)pFifo; /* the FIFO's registers: the port's transport */\n"
            "    state = OS_DisableInterrupts();\n"
            "    { extern int khdays_pxi_send(unsigned int tag, unsigned int data, unsigned int err);\n"
            "      khdays_pxi_send(packet.nTag, packet.nData, packet.bErr); }\n",
        ),
    ],
    "main@02009470": [  # PXIi_HandlerRecvFifoNotEmpty, its PXIi_SetToFifo and PXIi_GetFromFifo
        (
            "#define FifoRecvCallbackTable data_02046288\n",
            "#define FifoRecvCallbackTable data_02046288\n"
            "extern int khdays_pxi_send(unsigned int tag, unsigned int data, unsigned int err);\n"
            "extern int khdays_pxi_take(unsigned int *word);\n",
        ),
        (
            "    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_ERR_MASK) {\n"
            "        reg_PXI_FIFO_CNT |= (REG_PXI_FIFO_CNT_E_MASK | REG_PXI_FIFO_CNT_ERR_MASK);\n"
            "        return PXI_FIFO_FAIL_SEND_ERR;\n"
            "    }\n",
            "",
        ),
        (
            "    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_ERR_MASK) {\n"
            "        reg_PXI_FIFO_CNT |= (REG_PXI_FIFO_CNT_E_MASK | REG_PXI_FIFO_CNT_ERR_MASK);\n"
            "        return PXI_FIFO_FAIL_RECV_ERR;\n"
            "    }\n",
            "",
        ),
        (
            "    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_SEND_FULL_MASK) {\n"
            "        (void)OS_RestoreInterrupts(enabled);\n"
            "        return PXI_FIFO_FAIL_SEND_FULL;\n"
            "    }\n"
            "\n"
            "    reg_PXI_SEND_FIFO = data;\n",
            "    khdays_pxi_send(data & 0x1fu, data >> 6, (data >> 5) & 1u);\n",
        ),
        (
            "    if (reg_PXI_FIFO_CNT & REG_PXI_FIFO_CNT_RECV_EMP_MASK) {\n"
            "        (void)OS_RestoreInterrupts(enabled);\n"
            "        return PXI_FIFO_FAIL_RECV_EMPTY;\n"
            "    }\n"
            "\n"
            "    *data_buf = reg_PXI_RECV_FIFO;\n",
            "    if (!khdays_pxi_take(data_buf)) {\n"
            "        (void)OS_RestoreInterrupts(enabled);\n"
            "        return PXI_FIFO_FAIL_RECV_EMPTY;\n"
            "    }\n",
        ),
    ],
    # The SDK's accesses to the DMA and geometry registers through pointers,
    # thousands a second in the field (KHDAYS_PROFILE's `io:` lines): each through the register model (runtime/core/io.h) instead of
    # the trap's fault -- the same reads, the same stores, in the same order.
    "itcm@01ff8590": [  # MIi_DmaSetParams
        (
            "    *p = (volatile unsigned int)src;\n"
            "    *(p + 1) = (volatile unsigned int)dest;\n"
            "    *(p + 2) = (volatile unsigned int)ctrl;\n",
            "    khdays_io_store((unsigned int)p, 4, src);\n"
            "    khdays_io_store((unsigned int)(p + 1), 4, dest);\n"
            "    khdays_io_store((unsigned int)(p + 2), 4, ctrl);\n",
        ),
    ],
    "itcm@01ff85d0": [  # func_01ff85d0, MIi_DmaSetParams_wait
        (
            "    *p = (volatile unsigned int)src;\n"
            "    *(p + 1) = (volatile unsigned int)dest;\n"
            "    *(p + 2) = (volatile unsigned int)ctrl;\n"
            "    (void)*(volatile unsigned int *)0x040000B0;\n"
            "    (void)*(volatile unsigned int *)0x040000B0;\n",
            "    khdays_io_store((unsigned int)p, 4, src);\n"
            "    khdays_io_store((unsigned int)(p + 1), 4, dest);\n"
            "    khdays_io_store((unsigned int)(p + 2), 4, ctrl);\n"
            "    (void)khdays_io_load(0x040000B0u, 4);\n"
            "    (void)khdays_io_load(0x040000B0u, 4);\n",
        ),
        (
            "        *p = 0;\n        *(p + 1) = 0;\n        *(p + 2) = 0x81400001;\n",
            "        khdays_io_store((unsigned int)p, 4, 0);\n"
            "        khdays_io_store((unsigned int)(p + 1), 4, 0);\n"
            "        khdays_io_store((unsigned int)(p + 2), 4, 0x81400001u);\n",
        ),
    ],
    "main@02004000": [  # MIi_CheckAnotherAutoDMA
        ("        cnt = *cntp;\n", "        cnt = khdays_io_load((u32)cntp, 4);\n"),
    ],
    # NitroSDK panics on a DMA 0 source at or above 0x08000000 (the cartridge bus), where the game's
    # heaps now are. The port's DMA has no such bus: a source in the game's RAM passes, the DS's own
    # ranges keep the panic (runtime/core/io_regs.c).
    "main@02004084": [  # MIi_CheckDma0SourceAddress
        (
            "/* MIi_CheckDma0SourceAddress -- NitroSDK mi_dma.c. */\n",
            "/* MIi_CheckDma0SourceAddress -- NitroSDK mi_dma.c. */\n"
            "extern int khdays_game_ram_ref(unsigned int reference);  /* runtime/core/window.c */\n",
        ),
        (
            "        if (addStart == 0x04000000 || addStart >= 0x08000000 ||\n"
            "            addEnd == 0x04000000 || addEnd >= 0x08000000) {\n",
            "        if ((addStart == 0x04000000 || addStart >= 0x08000000 ||\n"
            "             addEnd == 0x04000000 || addEnd >= 0x08000000) &&\n"
            "            !(khdays_game_ram_ref(src) &&\n"
            "              khdays_game_ram_ref(dir == MI_DMA_SRC_DEC ? src - size : src + size - 1))) {\n",
        ),
    ],
    # A heap that gives no block: said in the port's log with the heap's figures
    # (runtime/core/game_heap.c); what the game does with the 0 stays its own.
    "main@020107fc": [  # NNS_FndAllocFromExpHeapEx
        (
            "void *NNS_FndAllocFromExpHeapEx(void *heap, unsigned int size, int align) {\n",
            "extern void khdays_game_heap_failed(void *heap, unsigned int size);  /* runtime/core/game_heap.c */\n"
            "void *NNS_FndAllocFromExpHeapEx(void *heap, unsigned int size, int align) {\n"
            "    void *khdays_block;\n",
        ),
        (
            "    if (align >= 0) {\n"
            "        return AllocFromHead(heap, size, align);\n"
            "    }\n"
            "    return AllocFromTail(heap, size, -align);\n",
            "    khdays_block = align >= 0 ? AllocFromHead(heap, size, align) : AllocFromTail(heap, size, -align);\n"
            "    if (khdays_block == 0) {\n"
            "        khdays_game_heap_failed(heap, size);\n"
            "    }\n"
            "    return khdays_block;\n",
        ),
    ],
    # The headers' heaps: a file handle names a packed archive's header by 24 bits from 0x01ff8000,
    # so headers must lie below 0x02ff8000. The ones Msg_OpenContainerAndReadHeader reads come from
    # heaps of their own there, and the handle decoders check it (runtime/core/game_heap.c;
    # docs/RUNTIME_MAP.md).
    "main@02024ee8": [  # Msg_OpenContainerAndReadHeader
        (
            "    buf = (u32 *)NNS_FndAllocFromDefaultExpHeapEx((u16)(size + 0x10), 4);\n",
            "    { extern void *khdays_header_alloc(unsigned int size);  /* runtime/core/game_heap.c */\n"
            "      buf = (u32 *)khdays_header_alloc((u16)(size + 0x10)); }\n",
        ),
    ],
    "main@0201098c": [  # NNS_FndFreeToExpHeap
        (
            "        NNSiFndHeapHead * pHeapHd = heap;\n",
            "        extern void *khdays_header_heap_of(void *heap, void *block);  /* runtime/core/game_heap.c */\n"
            "        NNSiFndHeapHead * pHeapHd = khdays_header_heap_of(heap, memBlock);\n",
        ),
    ],
    "main@02023604": [  # HeapState_Recreate
        (
            "    NNS_FndInitAllocatorForExpHeap(&s->allocator, s->heap, 4);\n}",
            "    NNS_FndInitAllocatorForExpHeap(&s->allocator, s->heap, 4);\n"
            "    { extern void khdays_game_heap_recreated(void *heap);  /* runtime/core/game_heap.c */\n"
            "      khdays_game_heap_recreated(s->heap); }\n}",
        ),
    ],
    "main@020250bc": [  # Archive_OpenSubfileByHandle
        (
            "    u8 *p = (u8 *)base;\n",
            "    u8 *p = (u8 *)base;\n"
            "    { extern void khdays_handle_check(unsigned int handle, unsigned int base);  /* runtime/core/game_heap.c */\n"
            "      khdays_handle_check(id, base); }\n",
        ),
    ],
    "main@02025074": [  # Archive_SubfileIsCompressed
        (
            "    unsigned short lo = param_1 & (mask >> 15);\n",
            "    unsigned short lo = param_1 & (mask >> 15);\n"
            "    { extern void khdays_handle_check(unsigned int handle, unsigned int base);  /* runtime/core/game_heap.c */\n"
            "      khdays_handle_check(param_1, base); }\n",
        ),
    ],
    # The game's main heap (0xc8000 bytes) and sub heap (the rest of the arena), made by the port
    # over its region of the game's memory (runtime/core/game_heap.c).
    "ov001@0204ce40": [  # Ov001_CreateMainAndSubHeaps
        (
            "#define HEAP_ALIGN 0x10\n",
            "#define HEAP_ALIGN 0x10\n"
            "extern void *khdays_game_heap(int which, u32 ds_size);  /* runtime/core/game_heap.c */\n",
        ),
        (
            "        mainHeap = NNS_FndCreateExpHeapEx(\n"
            "            OS_AllocFromArenaLo(OS_ARENA_MAIN, MAIN_HEAP_SIZE, HEAP_ALIGN),\n"
            "            MAIN_HEAP_SIZE, 0);\n",
            "        mainHeap = khdays_game_heap(0, MAIN_HEAP_SIZE);\n",
        ),
        (
            "        subHeap = NNS_FndCreateExpHeapEx(\n"
            "            OS_AllocFromArenaLo(OS_ARENA_MAIN, size, HEAP_ALIGN), size, 0);\n",
            "        subHeap = khdays_game_heap(1, size);\n",
        ),
    ],
    "main@02003f1c": [  # MI_WaitDma
        (
            "    while ((*ctrl & 0x80000000) != 0) {\n",
            "    while ((khdays_io_load((unsigned int)ctrl, 4) & 0x80000000) != 0) {\n",
        ),
        (
            "        reg[0] = 0;\n        reg[1] = 0;\n        reg[2] = 0x81400001;\n",
            "        khdays_io_store((unsigned int)&reg[0], 4, 0);\n"
            "        khdays_io_store((unsigned int)&reg[1], 4, 0);\n"
            "        khdays_io_store((unsigned int)&reg[2], 4, 0x81400001u);\n",
        ),
    ],
    "main@02006f28": [  # G3X_GetClipMtx
        ("    if (*gxstat & 0x8000000) {\n", "    if (khdays_io_load((unsigned int)gxstat, 4) & 0x8000000) {\n"),
    ],
    "main@02006f58": [  # G3X_GetVectorMtx
        ("    if (*gxstat & 0x8000000) {\n", "    if (khdays_io_load((unsigned int)gxstat, 4) & 0x8000000) {\n"),
    ],
    "itcm@01ffc450": [  # NNSi_G3dFuncSbc_NODEMIX: the matrix commands, through the model
        (
            "    registers->mode = 0;\n"
            "    registers->store = 1;\n"
            "    registers->identity = 0;\n"
            "    registers->mode = 2;\n",
            "    khdays_io_store(0x04000440u, 4, 0);\n"
            "    khdays_io_store(0x0400044cu, 4, 1);\n"
            "    khdays_io_store(0x04000454u, 4, 0);\n"
            "    khdays_io_store(0x04000440u, 4, 2);\n",
        ),
        (
            "            *restoreRegister = entry->matrixIndex;\n            registers->mode = 1;\n",
            "            khdays_io_store((unsigned int)restoreRegister, 4, entry->matrixIndex);\n"
            "            khdays_io_store(0x04000440u, 4, 1);\n",
        ),
        (
            "            registers->mode = 2;\n            G3_MultMtx33(",
            "            khdays_io_store(0x04000440u, 4, 2);\n            G3_MultMtx33(",
        ),
        (
            "    registers->mode = 1;\n"
            "    G3_LoadMtx43(&accum.position);\n"
            "    registers->mode = 0;\n"
            "    registers->restore = 1;\n"
            "    registers->mode = 2;\n"
            "    registers->store = state->pSbc[1];\n",
            "    khdays_io_store(0x04000440u, 4, 1);\n"
            "    G3_LoadMtx43(&accum.position);\n"
            "    khdays_io_store(0x04000440u, 4, 0);\n"
            "    khdays_io_store(0x04000450u, 4, 1);\n"
            "    khdays_io_store(0x04000440u, 4, 2);\n"
            "    khdays_io_store(0x0400044cu, 4, state->pSbc[1]);\n",
        ),
    ],
    # Replace only the heart panel's own node draws; its bookkeeping and other widgets still run.
    "main@0202aa9c": [  # Scene_DrawNode
        (
            "void Scene_DrawNode(SceneNode *node)\n{\n    SceneNode *child;\n",
            "void Scene_DrawNode(SceneNode *node)\n{\n    SceneNode *child;\n"
            "    { extern int khdays_heart_node(const void *node);\n"
            "      if (khdays_heart_node(node)) return; }\n",
        ),
    ],
    "ov002@0205f980": [  # Ov002_Hud_RewindWidget: repeated pickups restart the pulse
        (
            "    Ov002_RewindWidget",
            "    { extern void khdays_heart_event(int kind, int row); khdays_heart_event(0, 0); }\n"
            "    Ov002_RewindWidget",
        ),
    ],
    "ov002@0205fc5c": [  # Ov002_SceneSpawnPanelRowCount: only after the row is actually allocated
        (
            "    s->aRowBusy[nRow] = 1;\n",
            "    s->aRowBusy[nRow] = 1;\n"
            "    { extern void khdays_heart_event(int kind, int row); khdays_heart_event(1, nRow); }\n",
        ),
    ],
    "ov002@02060358": [  # Ov002_SceneStartPanelBlink
        (
            "    s = *(Ov002BlinkScene **)&data_ov002_0207f628;\n",
            "    s = *(Ov002BlinkScene **)&data_ov002_0207f628;\n"
            "    { extern void khdays_heart_event(int kind, int row); khdays_heart_event(2, 0); }\n",
        ),
    ],
    # The field's HUD panel draws its 3D pieces (the hearts counter, the countdown) with a camera of
    # its own: the port marks those polygons as the HUD's, so widescreen keeps them to the sides
    # (runtime/display/display.c khdays_hud_3d). Nothing the game computes changes.
    "ov002@020616a0": [  # Ov002_SceneStepPanel
        (
            "    Ov002_SceneStepPanelBlink();\n",
            "    Ov002_SceneStepPanelBlink();\n    { extern void khdays_heart_end(void); khdays_heart_end(); }\n",
        ),
        (
            "    Camera_CommitMatricesEx(s->aCamera, 0x3b33, -0x3b33, -0x4d9a, 0x4d9a);\n",
            "    Camera_CommitMatricesEx(s->aCamera, 0x3b33, -0x3b33, -0x4d9a, 0x4d9a);\n"
            "    { extern void khdays_hud_3d(int on); khdays_hud_3d(1); }\n"
            "    { extern void khdays_heart_begin(const void *context); khdays_heart_begin(s); }\n",
        ),
        (
            "    Ov002_DrawMarkerQueue(pCam);\n",
            "    { extern void khdays_hud_3d(int on); khdays_hud_3d(0); }\n"
            "    Ov002_DrawMarkerQueue(pCam);\n"
            "    { extern void khdays_hud_3d(int on); khdays_hud_3d(1); }\n",
        ),
        (
            "    Ov002_SweepParkedValues(pCam);\n",
            "    { extern void khdays_hud_3d(int on); khdays_hud_3d(0); }\n    Ov002_SweepParkedValues(pCam);\n",
        ),
    ],
    # The game over's floating orb, which the DS shows over all the 2D: marked as the HUD's shown
    # over the 2D, so the one-screen layout places it in front of the lines and widescreen leaves
    # it in place.
    "ov027@0208344c": [  # Ov027_UpdateFlushForward
        (
            "    Ov002_ResourceEntryCallback(*(int *)(data_ov027_02084360 + 4) + 0x588);\n",
            "    { extern void khdays_hud_3d(int on); khdays_hud_3d(2); }\n"
            "    Ov002_ResourceEntryCallback(*(int *)(data_ov027_02084360 + 4) + 0x588);\n"
            "    { extern void khdays_hud_3d(int on); khdays_hud_3d(0); }\n",
        ),
    ],
    # An overlay's data: its image has just been loaded and started; where the
    # SDK runs the overlay's static initializers, the native data initializers
    # of that overlay go over the image (runtime/core/data_init.c).
    "main@0200b4b8": [
        (  # FS_StartOverlay
            "FSOverlayInitFunc *p = p_ovi->header.sinit_init;",
            "FSOverlayInitFunc *p = (khdays_data_init((int)p_ovi->header.id), p_ovi->header.sinit_init);",
        )
    ],
    # An overlay the game unloads is unloaded for the port too (runtime/core/overlays.c); the info's
    # first word is its id.
    "main@0200b69c": [
        (  # FS_UnloadOverlayImage
            "    FS_EndOverlay(p);\n",
            "    FS_EndOverlay(p);\n"
            "    { extern void khdays_overlay_ended(int id); khdays_overlay_ended(*(int *)p); }\n",
        )
    ],
    # An object's update returns its next update; the ROM calls it with its own address in r0, so
    # one that leaves r0 alone keeps itself (EMPTY_FUNCTION). Anything returned that is not native
    # code (or -1/-2) is caught here, naming the update (runtime/core/diag.c).
    "main@02023adc": [
        (  # Obj_UpdateAll
            "                int cb = ((int (*)(void))((int *)gObjSystem[1])[5])();\n",
            "                int cb = ((int (*)(int))((int *)gObjSystem[1])[5])(((int *)gObjSystem[1])[5]);\n"
            "                { extern void khdays_check_update(int fn, int next);\n"
            "                  khdays_check_update(((int *)gObjSystem[1])[5], cb); }\n",
        )
    ],
    # The camp menus on one screen (runtime/menus/camp.c): the status panel's numbers as the panel draws
    # them, each field with its values and the change the Panels page previews with a panel held; the change
    # left off the DS's upper screen while the port draws the page (its status card shows it).
    "ov008@0206d46c": [
        (  # Ov008_DrawStatusField
            "    nCtx = Ov008_GetCtxBlock4a80();\n",
            "    { extern unsigned int khdays_camp_field(void *panel, int field, unsigned int delta, int style,\n"
            "                                            void *values, int animate);\n"
            "      nDelta = khdays_camp_field(pPanel, nField, nDelta, nStyle, pValues, bAnimate); }\n"
            "    nCtx = Ov008_GetCtxBlock4a80();\n",
        )
    ],
    # The story's camp menu the same (ov025, the same panel under its own name).
    "ov025@020a2b00": [
        (  # Ov025_DrawStatusField
            "    nCtx = Ov025_GetBlock4a80();\n",
            "    { extern unsigned int khdays_camp_field(void *panel, int field, unsigned int delta, int style,\n"
            "                                            void *values, int animate);\n"
            "      nDelta = khdays_camp_field(pPanel, nField, nDelta, nStyle, pValues, bAnimate); }\n"
            "    nCtx = Ov025_GetBlock4a80();\n",
        )
    ],
    # KHDAYS_TRACE_SCRIPT: the action-script interpreter reports each step it
    # stands on (runtime/core/diag.c), to see what a scene waits for.
    "main@02020e58": [
        (  # Game_RunActionScript
            "    cur = st + 4 + *(int *)(st + 0x124) * 0x48;\n",
            "    cur = st + 4 + *(int *)(st + 0x124) * 0x48;\n"
            "    { extern void khdays_trace_script(void *st, void *entry); khdays_trace_script(st, cur); }\n",
        )
    ],
    # An event script's skippable stretch begins or ends (runtime/movies/scene_trace.c): the script
    # machine's skip target is set (an operand) or cleared (-1).
    "main@020221f8": [
        (  # Game_ActionSetBranchTarget
            "    *(int *)(param_1 + 0x588) = 0;\n    return 1;\n}\n",
            "    *(int *)(param_1 + 0x588) = 0;\n"
            "    { extern void khdays_scene_target(unsigned int state, int operand);\n"
            "      khdays_scene_target((unsigned int)param_1, operand); }\n"
            "    return 1;\n}\n",
        )
    ],
    # The day's monologue (ov007, scene 10) begins with its record picked by the story's day, and
    # ends asking for the calendar: the theater's voice for it may be heard over its page
    # (runtime/movies/scene_trace.c).
    "ov007@0204cb80": [
        (  # Ov007_SceneInit
            "    heap[0x1d] = (int)Ov007_GetVarRecordByIndex(heap + 2, idx);\n",
            "    heap[0x1d] = (int)Ov007_GetVarRecordByIndex(heap + 2, idx);\n"
            "    { extern void khdays_scene_monologue(int record); khdays_scene_monologue(idx); }\n",
        )
    ],
    "ov007@0204d31c": [
        (  # Ov007_RequestSceneChange
            "    Scene_RequestPending(SCENE_CALENDAR, r1);\n",
            "    Scene_RequestPending(SCENE_CALENDAR, r1);\n"
            "    { extern void khdays_scene_monologue_ends(void); khdays_scene_monologue_ends(); }\n",
        )
    ],
    # A scene has ended and the next is not built yet: a language the
    # settings menu asked for is set there, so that every scene is built whole
    # in one language (runtime/core/language.c).
    "main@0202099c": [
        (  # Scene_AdvanceToPending
            "            HeapState_Recreate(data_0204c02c);\n",
            "            { extern void khdays_language_scene_ending(void); khdays_language_scene_ending(); }\n"
            "            HeapState_Recreate(data_0204c02c);\n",
        )
    ],
    # The game reads its buttons once per game frame, here (KEYINPUT and the
    # ARM7's X/Y word). The runtime takes that frame's input just before --
    # the script's keys and the player's (runtime/input/input.c) -- so every source
    # of input reaches the game the one way the DS's buttons do.
    "main@02023f78": [
        (  # Pad_Sample
            "    gPadHeld.prev = gPadHeld.cont;\n",
            "    { extern void khdays_input_pad_sample(void); khdays_input_pad_sample(); }\n"
            "    gPadHeld.prev = gPadHeld.cont;\n",
        )
    ],
    # The game's ROM reads answered by the port (runtime/core/files.h): every read reaches
    # CARDi_ReadRom, which then takes CARDi_ReadRomSyncCore without the card DMA, and there the
    # copy is one copy from the ROM image. The SDK's checks, flags and callbacks run as they are.
    # KHDAYS_FILES=card keeps the card.
    "main@0200eea4": [
        (  # CARDi_ReadRom
            "    if (CARDi_TryReadCardDma(state)) {\n",
            "    extern int khdays_files_mode(void);\n"
            "    if (khdays_files_mode() != 0 && CARDi_TryReadCardDma(state)) {\n",
        )
    ],
    # The movies (runtime/movies/video.h): the frame is decoded by the decomp's portable
    # reconstruction of the payload ov024 copies to the ITCM (libs/mobiclip/video/portable), called
    # where the C calls the payload; the payload is still copied, and nothing runs it. The blit is
    # port/hal/sdk_mobiclip.c.
    "ov024@020859d4": [
        (  # Ov024_MobiClip_DecodeFrame
            "    **(int **)(ctx + 0x34) += (*(int (**)(int))(ctx + 0x38))(*(int *)(ctx + 0x34));\n",
            "    {\n"
            "        extern void khdays_video_decode_begin(void);\n"
            "        extern void khdays_video_decode_end(void);\n"
            "        extern int khdays_video_decode(void *pState);\n"
            "        int consumed;\n"
            "        khdays_video_decode_begin();\n"
            "        consumed = khdays_video_decode((void *)*(int *)(ctx + 0x34));\n"
            "        khdays_video_decode_end();\n"
            "        **(int **)(ctx + 0x34) += consumed;\n"
            "    }\n",
        )
    ],
    "ov024@02085ab8": [
        (  # Ov024_MobiClip_BlitFrame (C++, its externs inside extern "C")
            "extern void Ov024_MobiClip_BlitRows(MobiClipBlitRequest *pRequest);\n",
            "extern void Ov024_MobiClip_BlitRows(MobiClipBlitRequest *pRequest);\n"
            "extern void khdays_video_blit_begin(u32 destination, u32 stride, u32 height);\n"
            "extern void khdays_video_blit_end(void);\n"
            "extern void khdays_video_frame_blitted(u32 request);\n",
        ),
        (
            "    Ov024_MobiClip_BlitRows(&pDecoder->blit);\n",
            "    khdays_video_blit_begin((u32)pDecoder->blit.pDest, (u32)pDecoder->blit.nStride, pDecoder->nHeight);\n"
            "    Ov024_MobiClip_BlitRows(&pDecoder->blit);\n"
            "    khdays_video_blit_end();\n"
            "    khdays_video_frame_blitted((u32)&pDecoder->blit); /* the port's picture of it */\n",
        ),
    ],
    # A movie's sound: the channels Ov024_MobiClip_StartAudio locks (mask 3,
    # channels 0 and 1) until Ov024_MobiClip_ReleaseAudioChannel unlocks
    # them -- their category is the movies' (sound_bridge.h).
    "ov024@020840a4": [
        (  # Ov024_MobiClip_StartAudio
            "    SND_LockChannel(3, 0);\n",
            "    SND_LockChannel(3, 0);\n"
            "    { extern void khdays_sound_movie_channels(u32 mask); khdays_sound_movie_channels(3); }\n",
        )
    ],
    "ov024@020841c4": [
        (  # Ov024_MobiClip_ReleaseAudioChannel
            "    SND_UnlockChannel(3, 0);\n",
            "    SND_UnlockChannel(3, 0);\n"
            "    { extern void khdays_sound_movie_channels(unsigned int mask); khdays_sound_movie_channels(0); }\n",
        )
    ],
    # A movie a mod replaces (runtime/movies/video_mod.h):
    # the path the game opens a stream at, and the movie's stop.
    "ov024@02084650": [
        (  # Ov024_MobiClip_OpenStreamSlot
            "    if (FS_OpenFile(data_ov024_02093a48 + idx * 0x48, Msg_BuildLangPath(path)) == 0) {\n",
            "    const int built = Msg_BuildLangPath(path);\n"
            "    { extern void khdays_video_movie_opened(int slot, const char *path);\n"
            "      khdays_video_movie_opened(idx, (const char *)built); }\n"
            "    if (FS_OpenFile(data_ov024_02093a48 + idx * 0x48, built) == 0) {\n",
        )
    ],
    "ov024@02084b14": [
        (  # Ov024_MobiClip_StopPlayback
            "    pAudio = data_ov024_02093a2c.pAudio;\n",
            "    { extern void khdays_video_movie_stopped(void); khdays_video_movie_stopped(); }\n"
            "    pAudio = data_ov024_02093a2c.pAudio;\n",
        )
    ],
    # KHDAYS_TRACE_TIMING (runtime/core/events.h): the timer 1
    # interrupts (the alarms), timer 0's overflows (the tick's count-up) and
    # the movie's flips, counted.
    "main@0200316c": [
        (  # OSi_CountUpTick
            "    OSi_TickCounter++;\n",
            "    { extern void khdays_timing_countup(void); khdays_timing_countup(); }\n    OSi_TickCounter++;\n",
        )
    ],
    "ov024@02084438": [
        (  # Ov024_MobiClip_PresentOnVBlank
            "    if (nScanline < 0xa0 || nScanline >= 0x104) {\n        return;\n    }\n",
            "    if (nScanline < 0xa0 || nScanline >= 0x104) {\n"
            "        { extern void khdays_timing_flip(int outside); khdays_timing_flip(1); }\n"
            "        return;\n    }\n",
        ),
        (
            "        pTimer->bPresented = 1;\n",
            "        pTimer->bPresented = 1;\n"
            "        { extern void khdays_timing_flip(int outside); khdays_timing_flip(0); }\n",
        ),
        (
            # The display line: this runs in the VBlank interrupt's
            # task list (OSi_VBlankInterruptHandler), where the DS reads line 192
            # -- inside the window it checks. No scan: 192 while the VBlank
            # handler runs, 0 anywhere else.
            "    nScanline = *reg_vcount;\n",
            "    { extern int khdays_events_in_vblank(void); nScanline = khdays_events_in_vblank() ? 192 : 0; }\n",
        ),
    ],
    # The SDK's tick and alarm timer on the PC's clock (hal/sdk_time.c;):
    # no timer register is touched.
    "main@020030e4": [
        (  # OS_InitTick
            "    TM0CNT_H = 0;\n    TM0CNT_L = 0;\n    TM0CNT_H = 0xC1;\n",
            "    { extern void khdays_tick_set(unsigned long long tick); khdays_tick_set(0); }\n",
        )
    ],
    "main@020031d4": [
        (  # OS_GetTick
            "OSTick OS_GetTick (void)\n{\n",
            "OSTick OS_GetTick (void)\n{\n"
            "    { extern unsigned long long khdays_tick_now(void); return khdays_tick_now(); }\n",
        )
    ],
    "main@02003274": [
        (  # OS_GetTickLo
            "    return *(volatile unsigned short *)0x04000100;\n",
            "    { extern unsigned long long khdays_tick_now(void); return (unsigned short)khdays_tick_now(); }\n",
        )
    ],
    "main@02003284": [
        (  # OS_SetTick
            "void OS_SetTick(u64 nTick)\n{\n",
            "void OS_SetTick(u64 nTick)\n{\n"
            "    { extern void khdays_tick_set(unsigned long long tick); khdays_tick_set(nTick); return; }\n",
        )
    ],
    "main@020032f4": [
        (  # OSi_SetTimer
            "void OSi_SetTimer (OSAlarm * alarm)\n{\n",
            "void OSi_SetTimer (OSAlarm * alarm)\n{\n"
            "    {\n"
            "        extern void khdays_alarm_timer_for(unsigned long long fire);\n"
            "        OSi_EnterTimerCallback(OSi_ALARM_TIMER, OSi_AlarmHandler, NULL);\n"
            "        khdays_alarm_timer_for(alarm->fire);\n"
            "        (void)OS_EnableIrqMask(OSi_ALARM_IE_TIMER);\n"
            "        return;\n"
            "    }\n",
        )
    ],
    "main@020036b0": [
        (  # OSi_ArrangeTimer (and KHDAYS_TRACE_TIMING's count)
            "    OS_SetTimerControl(OSi_ALARM_TIMER, 0);\n    (void)OS_DisableIrqMask(OSi_ALARM_IE_TIMER);\n",
            "    { extern void khdays_timing_alarm(void); khdays_timing_alarm(); }\n"
            "    { extern void khdays_alarm_clear(void); khdays_alarm_clear(); }\n"
            "    (void)OS_DisableIrqMask(OSi_ALARM_IE_TIMER);\n",
        )
    ],
    "main@02003b68": [
        (  # OS_GetLowEntropyData: entropy, not a line
            "static inline u16 GX_GetVCount(void) { return reg_GX_VCOUNT; }",
            "static inline u16 GX_GetVCount(void) { extern unsigned long long khdays_clock_cycles(void); "
            "return (u16)khdays_clock_cycles(); }",
        )
    ],
    # The game's loops that wait on an interrupt taken inside them:
    # each halts at a wait point until an interrupt is due and
    # takes it (runtime/core/events.h khdays_wait_point), as the CPU would take it
    # between two of the loop's instructions; the loop then checks again.
    "ov012@0205b618": [
        (  # Ov012_RunOpeningScene: the opening's movie loop
            "                } while (Ov024_TickStreamSlots() == 0);\n",
            "                    { extern void khdays_wait_point(void); khdays_wait_point(); }\n                } while (Ov024_TickStreamSlots() == 0);\n",
        ),
        (
            # An HD video in the opening's place (runtime/movies/movie_hd.h):
            # its flags each turn -- bit 2 set by the START skip
            # just below; bit 3 given, as the opening reads START with no
            # condition, unlike the other movies (Ov024_MobiClip_UpdatePlayback).
            "                do {\n                    if ((*(u16 *)(context + 2) & 4) != 0) {\n",
            "                do {\n                    { extern void khdays_movie_hd_flags(unsigned flags);\n"
            "                      khdays_movie_hd_flags(*(u16 *)(context + 2) | 8u); }\n"
            "                    if ((*(u16 *)(context + 2) & 4) != 0) {\n",
        ),
    ],
    # The credits scene, ov011 (runtime/movies/movie_hd.h): its movie is followed by the staff roll,
    # which the HD video carries too, so the scene's begin, the roll's end and the scene's end are
    # told.
    "ov011@0205ac40": [
        (  # Ov011_CreateScene
            "    Ov024_MobiClip_InstallStreamSourceVtbl(pStream);\n",
            "    { extern void khdays_movie_hd_credits(int phase); khdays_movie_hd_credits(1); }\n"
            "    Ov024_MobiClip_InstallStreamSourceVtbl(pStream);\n",
        )
    ],
    "ov011@0205c2fc": [
        (  # Ov011_StartTitleExit
            "    data_ov011_0205e960.pScene->nMode = MODE_EXIT;\n",
            "    data_ov011_0205e960.pScene->nMode = MODE_EXIT;\n"
            "    { extern void khdays_movie_hd_credits(int phase); khdays_movie_hd_credits(2); }\n",
        )
    ],
    "ov011@0205b034": [
        (  # Ov011_DestroyScene
            "void Ov011_DestroyScene(void)\n{\n",
            "void Ov011_DestroyScene(void)\n{\n"
            "    { extern void khdays_movie_hd_credits(int phase); khdays_movie_hd_credits(0); }\n",
        )
    ],
    "ov024@02082f04": [
        (  # Ov024_MobiClip_UpdatePlayback: the movies' loop
            "            }\n        }\n\n    drain:\n",
            "            }\n            { extern void khdays_wait_point(void); khdays_wait_point(); }\n        }\n\n    drain:\n",
        ),
        (
            # An HD video in the movie's place (runtime/movies/movie_hd.h):
            # the player's flags each turn -- bit 3 the DS lets
            # START skip the movie (Ov024_MobiClip_OpenPlayer's argument), bit 2
            # the skip taken.
            "        while (Ov024_TickStreamSlots() == 0) {\n            if (player->wFlags & 4) {\n",
            "        while (Ov024_TickStreamSlots() == 0) {\n"
            "            { extern void khdays_movie_hd_flags(unsigned flags); khdays_movie_hd_flags(player->wFlags); }\n"
            "            if (player->wFlags & 4) {\n",
        ),
    ],
    "main@0200ca50": [
        (  # TP_WaitBusy: the touch panel's reply (PXI)
            "    while (*(volatile unsigned short *)(&data_02046390 + 0x3a) & mask) {\n    }\n",
            "    while (*(volatile unsigned short *)(&data_02046390 + 0x3a) & mask) {\n        { extern void khdays_wait_point(void); khdays_wait_point(); }\n    }\n",
        )
    ],
    "main@0200cab8": [
        (  # PMi_WaitBusy: the power manager's reply (PXI)
            "        if (OS_GetCpsrIrq() == 0x80) {\n            PXIi_HandlerRecvFifoNotEmpty();\n        }\n",
            "        if (OS_GetCpsrIrq() == 0x80) {\n            PXIi_HandlerRecvFifoNotEmpty();\n        } else { extern void khdays_wait_point(void); khdays_wait_point(); }\n",
        )
    ],
    # The camera of each 3D list (gfx/g3d.h khdays_g3d_frame_camera):
    # what NitroSystem's flush sends, as it sends it.
    # NitroSystem's NNS_G3dGlbFlush, which the decomp calls
    # Gfx_SubmitCachedCommandBlock: the whole NNSG3dGlb block at once, the
    # projection at +0x08 and the camera at +0x4c.
    "main@0201571c": [
        (  # NNS_G3dGlbFlush
            "    unsigned int *p = (unsigned int *)&NNS_G3dGlb;\n",
            "    unsigned int *p = (unsigned int *)&NNS_G3dGlb;\n"
            "    { extern void khdays_g3d_frame_camera(const int *projection, const int *camera);\n"
            "      khdays_g3d_frame_camera((const int *)((const char *)&NNS_G3dGlb + 0x08),\n"
            "                              (const int *)((const char *)&NNS_G3dGlb + 0x4c)); }\n",
        )
    ],
    "main@020157c4": [
        (  # NNS_G3dGlbFlushP
            "void NNS_G3dGlbFlushP(void)\n{\n",
            "void NNS_G3dGlbFlushP(void)\n{\n"
            "    { extern void khdays_g3d_frame_camera(const int *projection, const int *camera);\n"
            "      khdays_g3d_frame_camera((const int *)&NNS_G3dGlb.projMtx, (const int *)&NNS_G3dGlb.cameraMtx); }\n",
        )
    ],
    # The card's chip ID, read after every ROM read to see the card is still in: with the port's
    # reads, the value the card answers, without its command.
    "main@0200edb0": [
        (  # CARDi_ReadRomIDCore
            "    CARDi_SetRomOp(MROMOP_G_READ_ID, 0);\n",
            "    {\n"
            "        extern int khdays_files_mode(void);\n"
            "        extern u32 khdays_files_chip_id(void);\n"
            "        if (khdays_files_mode() == 0) {\n"
            "            return khdays_files_chip_id();\n"
            "        }\n"
            "    }\n"
            "    CARDi_SetRomOp(MROMOP_G_READ_ID, 0);\n",
        )
    ],
    "main@0200ee0c": [
        (  # CARDi_ReadRomSyncCore
            "    if (CARDi_ReadFromCache(state)) {\n        (*state->read_func)(state);\n    }\n",
            "    {\n"
            "        extern int khdays_files_mode(void);\n"
            "        extern void khdays_files_read(unsigned int src, void *dst, unsigned int len);\n"
            "        struct CARDiCommon *c = &data_020464e0;\n"
            "        if (khdays_files_mode() == 0) {\n"
            "            khdays_files_read(c->src, (void *)c->dst, c->len);\n"
            "            c->src += c->len;\n"
            "            c->dst += c->len;\n"
            "            c->len = 0;\n"
            "        } else if (CARDi_ReadFromCache(state)) {\n"
            "            (*state->read_func)(state);\n"
            "        }\n"
            "    }\n",
        )
    ],
    # The game asking for a music stream, for the delay until it is heard
    # (runtime/sound/sound_bridge.c, KHDAYS_TRACE_SOUND_COMMANDS).
    "main@0201cac0": [
        (  # NNS_SndArcStrmPrepare
            "    strmInfo = NNS_SndArcGetStrmInfo(strmNo);\n",
            "    { extern void khdays_sound_stream_requested(int stream_number); khdays_sound_stream_requested(strmNo); }\n"
            "    strmInfo = NNS_SndArcGetStrmInfo(strmNo);\n",
        )
    ],
    # A music stream prepared: the port reads the stream's file itself into the player's rings
    # (runtime/sound/sound_bridge.c, sound/stream_reader.c), so the channels play whether or not the
    # stream thread has had its turn; the first sample is the one computed at the top (`tmpU64`).
    # docs/RUNTIME_MAP.md section 6, "The music streams' ring".
    "main@0201ce18": [
        (  # PrepareStrm
            "        NNS_SndStrmSetChannelPan(&player->stream, 1, 127);\n    }\n\n    return TRUE;\n",
            "        NNS_SndStrmSetChannelPan(&player->stream, 1, 127);\n    }\n\n"
            "    { extern void khdays_sound_stream_prepared(int player, int stream_number, int channels,\n"
            "          const u8 *channel, u32 ring, u32 ring_bytes, const void *header, u32 file_at, int in_memory,\n"
            "          u32 top, u32 bottom, u32 start_sample, int mono);\n"
            "      extern void *NNS_SndArcGetFileAddress(u32 fileId);\n"
            "      khdays_sound_stream_prepared(player->playerNo, strmNo, player->stream.numChannels,\n"
            "          player->stream.channelNo,\n"
            "          (u32)player->buffer, player->stream.chBufLen, &player->info, player->fileOffset,\n"
            "          NNS_SndArcGetFileAddress(strmInfo->fileId) != NULL, player->file.prop.file.top,\n"
            "          player->file.prop.file.bottom, (u32)tmpU64, player->monoFlag); }\n"
            "    return TRUE;\n",
        )
    ],
    # Which sequence, or which entry of a sequence archive, an ARM7 player plays, with the tag of
    # the command list that prepares it: a mod's file may replace it (runtime/sound/sound_mods.c).
    "main@0201c734": [
        (  # StartSeq
            # its entry, for the time its loading takes
            "    player = NNSi_SndPlayerAllocSeqPlayer(handle, playerNo, playerPrio);\n",
            "    { extern void khdays_sound_sequence_requested(void); khdays_sound_sequence_requested(); }\n"
            "    player = NNSi_SndPlayerAllocSeqPlayer(handle, playerNo, playerPrio);\n",
        ),
        (
            "    NNS_SndPlayerSetSeqNo(handle, seqNo);\n",
            "    NNS_SndPlayerSetSeqNo(handle, seqNo);\n"
            "    { extern void khdays_sound_sequence_started(int arm7_player, int kind, int number, int entry,\n"
            "          u32 command_tag);\n"
            "      khdays_sound_sequence_started(player->playerNo, 0, seqNo, -1, player->commandTag); }\n",
        ),
    ],
    "main@0201c830": [
        (  # StartSeqArc
            "    NNS_SndPlayerSetSeqArcNo(handle, seqArcNo, index);\n",
            "    NNS_SndPlayerSetSeqArcNo(handle, seqArcNo, index);\n"
            "    { extern void khdays_sound_sequence_started(int arm7_player, int kind, int number, int entry,\n"
            "          u32 command_tag);\n"
            "      khdays_sound_sequence_started(player->playerNo, 1, seqArcNo, index, player->commandTag); }\n",
        )
    ],
    # Where each wave archive is once a bank's loading has it (sndarc_loader.c):
    # the channels that start on a wave a mod replaces play its file
    # (runtime/sound/sound_mods.c). A wave archive loaded wave by wave has its
    # waves' places filled in after (LoadSingleWaves); they are read when a
    # channel starts.
    "main@0201be70": [
        (  # NNSi_SndArcLoadWaveArc
            "    if (pData != NULL) *pData = waveArc;\n",
            "    { extern void khdays_sound_wave_archive_loaded(int wave_archive, const void *address);\n"
            "      khdays_sound_wave_archive_loaded(waveArcNo, waveArc); }\n"
            "    if (pData != NULL) *pData = waveArc;\n",
        )
    ],
    # Which sound archive player each ARM7 sequence player serves, so the volumes can tell music,
    # effects and voices apart (sound/categories.c; docs/RUNTIME_MAP.md section 6).
    "main@0201a568": [
        (  # NNSi_SndPlayerStartSeq
            "    player = seqPlayer->player;\n",
            "    player = seqPlayer->player;\n"
            "    { extern void khdays_sound_player_assigned(int arm7_player, int archive_player);\n"
            "      extern NNSSndPlayer data_0204a760[32];\n"
            "      khdays_sound_player_assigned(seqPlayer->playerNo, (int)(player - data_0204a760)); }\n",
        )
    ],
    # The controls' enhancements meet the game in the functions below (runtime/input/
    # controls_hooks.c): each call hands back what the game had unless an enhancement the player
    # switched on changes it; KHDAYS_TRACE_CONTROLS reports what the game does with its controls.
    # The field's pace with three or four players: the game drops to frame mode 1 (a step every
    # three frames, 20 a second); the port keeps mode 0's. KHDAYS_MP_DS_PACE=1 keeps the game's.
    "ov002@020679d4": [
        (  # Ov002_ConstructGameplayScene: the record the field is built from (its mission, its event
            # script), seen by the port and set by a test (runtime/movies/scene_trace.c)
            "    src = param_1 ? (char *)param_1 : (char *)&data_0204c240;\n",
            "    src = param_1 ? (char *)param_1 : (char *)&data_0204c240;\n"
            "    { extern void khdays_scene_field_record(void *record, int from_argument);\n"
            "      khdays_scene_field_record(src, param_1 != 0); }\n",
        ),
        (  # Ov002_ConstructGameplayScene
            "    if (Session_IsActive() != 0 && *(int *)(Session_GetSetup() + 4) >= 3) {\n        i = 1;\n",
            "    if (Session_IsActive() != 0 && *(int *)(Session_GetSetup() + 4) >= 3) {\n"
            "        { extern int khdays_mp_field_mode(int players); i = khdays_mp_field_mode("
            "*(int *)(Session_GetSetup() + 4)); }\n",
        ),
        (  # the field begins (a mission, or the same one again after Retry): the mods' mission start
            # waits for its first run (runtime/scripts/scripts.h)
            "    if (Session_IsActive() != 0) {\n        return (void *)Ov002_UpdatePendingRequest;\n    }\n"
            "    return (void *)Ov002_TickGameplayState;\n}\n",
            "    { extern void khdays_scripts_field_begins(void); khdays_scripts_field_begins(); }\n"
            "    if (Session_IsActive() != 0) {\n        return (void *)Ov002_UpdatePendingRequest;\n    }\n"
            "    return (void *)Ov002_TickGameplayState;\n}\n",
        ),
    ],
    # A guest's prediction of its own character (runtime/net/mp_predict.c): the battle is
    # host-simulated, and a guest's own character runs the host's branch from its own last control,
    # the host's state only correcting it. Inside that update "am I the host" answers as a host's
    # (ROLE_QUERY); "is this my character" stays the guest's (docs/BUILD.md).
    "ov022@020a0008": [
        (  # Ov022_ActorUpdate
            "        Ov022_UpdateCommandInput(pActor);\n",
            "        { extern void khdays_mp_predict_pause(int paused); khdays_mp_predict_pause(1);\n"
            "          Ov022_UpdateCommandInput(pActor);\n"
            "          khdays_mp_predict_pause(0); }\n",
        )
    ],
    "ov022@02087ac0": [
        (  # Ov022_Party_SyncAndUpdate
            "                Ov022_ActorUpdate(node);\n                Ov022_TickSubObjectChain(node);\n",
            "                { extern void khdays_mp_predict_open(int node, int local);\n"
            "                  extern void khdays_mp_predict_close(void);\n"
            "                  extern int Session_GetLocalPlayerIndex(void);\n"
            "                  extern int khdays_mp_predict_is_open(void);\n"
            "                  extern void func_ov022_020ad474(unsigned int *arg0);\n"
            "                  khdays_mp_predict_open(node, Session_GetLocalPlayerIndex());\n"
            "                  /* the host's tick for its characters (Ov022_RefreshSlotRows): the\n"
            "                   * event word reset and the pending one merged in */\n"
            "                  if (khdays_mp_predict_is_open() && (root->flags & 4) == 0) {\n"
            "                      func_ov022_020ad474((unsigned int *)node);\n"
            "                  }\n"
            "                  Ov022_ActorUpdate(node);\n"
            "                  Ov022_TickSubObjectChain(node);\n"
            "                  khdays_mp_predict_close(); }\n",
        )
    ],
    "ov022@02087b7c": [
        (  # Ov022_Party_ApplyNetState
            "            changed = 0;\n\n            actor->flags01a = changed;\n",
            "            changed = 0;\n"
            "            { extern int khdays_mp_predict_keep(int own, int actor, int record);\n"
            "              extern void khdays_mp_predict_shift(int *x, int *y, int *z);\n"
            "              const int khdays_keep = khdays_mp_predict_keep(index == (unsigned int)QueryActiveStateOrDelegate(),"
            " (int)actor, (int)record);\n"
            "              if (khdays_keep != 0) {\n"
            "                  Ov022_ActorSetHp(actor, record->field20);\n"
            "                  /* visible as apply_entry has it: not while the field moves it to another\n"
            "                   * area (Ov002_StepRosterSlotRelease leaves its group -1 until\n"
            "                   * Ov002_StepRosterSlotPhase shows it in the new one) */\n"
            "                  func_ov022_020a0678(actor, localField66 != -1 && localField66 == actor->field066);\n"
            "                  /* what a host takes from its own character (Ov022_RefreshSlotRows):\n"
            "                   * the action lock and the camera's request */\n"
            "                  root->stateBits3c.bit4 = (*((unsigned char *)actor + 0x694) >> 3) & 1;\n"
            "                  if (*((signed char *)actor + 0x47a) > 0 && func_ov022_020886f8(index) == 0) {\n"
            "                      Ov002_Camera_SetMode(func_ov022_02083f0c(), *((signed char *)actor + 0x47a),\n"
            "                                           *((signed char *)actor + 0x47b));\n"
            "                  }\n"
            "                  if (khdays_keep == 2) {\n"
            "                      /* the host's run of it off the predicted path: moved by the difference */\n"
            "                      VecFx32 khdays_at = actor->position48c;\n"
            "                      khdays_mp_predict_shift(&khdays_at.x, &khdays_at.y, &khdays_at.z);\n"
            "                      Actor_SetVecAndSyncChild((void *)actor->child20, &khdays_at);\n"
            "                      actor->position48c = khdays_at;\n"
            "                  }\n"
            "                  continue;\n"
            "              } }\n\n"
            "            actor->flags01a = changed;\n",
        )
    ],
    "main@02031258": [
        (  # MsgQueue_SendGate
            "    ret = 0;\n    doSend = 1;\n    switch (Session_GetLinkMode()) {\n",
            "    ret = 0;\n    doSend = 1;\n"
            "    { extern int khdays_mp_predict_drops(int gate, const void *data, int size);\n"
            "      const int khdays_drop = khdays_mp_predict_drops(param_1, param_2, param_3);\n"
            "      if (khdays_drop == 2) {\n"
            "          /* a guest's own shot: applied at once as a host applies its own (mode 2's\n"
            "           * dispatch below, AsyncMessage_Flush), the host's copy left out on arrival */\n"
            "          ((MsgHdr *)buf)->f0 = 1; /* as MsgQueue_Enqueue marks what the host sends */\n"
            "          ((MsgHdr *)buf)->type = param_1;\n"
            "          ((MsgHdr *)buf)->size = param_3;\n"
            "          MI_CpuCopy8(param_2, (unsigned char *)buf + 2, param_3);\n"
            "          dispatchByObjTypeBits((int)buf, param_3 + 2);\n"
            "          return 1;\n"
            "      }\n"
            "      if (khdays_drop != 0) {\n"
            "          return 1;\n"
            "      } }\n"
            "    switch (Session_GetLinkMode()) {\n",
        )
    ],
    # The field camera: where the game applies its turn and tilt (the D-pad's look view, a drag on
    # the touch screen, L/R), the analog camera adds its own and marks the frame as a camera frame
    # (0x10), so the game does not pull the camera back behind the player meanwhile.
    "ov002@0204d170": [  # Ov002_Camera_UpdateFollow
        (
            "  if ((piVar7[0xe] & 0x200U) == 0) {\n    if ((piVar7[0xe] & 2U) != 0) {\n",
            "  if ((piVar7[0xe] & 0x200U) == 0) {\n    if ((piVar7[0xe] & 2U) != 0) {\n"
            "      { extern int khdays_controls_camera_turn(int rate, int inverted, int bits, int flags, int flags2,"
            " int yaw, int target, int pitch);\n"
            "        int turn = (uVar23 & 0x400) != 0 ? 0 : khdays_controls_camera_turn(piVar7[0x2a],"
            " (piVar7[0xf] & 0x10U) != 0, uVar20, piVar7[0xe], piVar7[0xf], piVar7[0x16], piVar7[0x20],"
            " piVar7[0x21]);\n"
            "        if (turn != 0) {\n"
            "          piVar7[0x20] = (piVar7[0x20] + turn) & 0xffff;\n"
            "          if ((piVar7[0xe] & 0x10000U) != 0) func_ov022_02088280(idx, piVar7[0x20]);\n"
            "          uVar20 |= 0x10;\n"
            "        } }\n",
        ),
        (
            "        piVar7[0x21] = piVar7[0x21] - (piVar7[0x2b] + iVar12);\n      }\n",
            "        piVar7[0x21] = piVar7[0x21] - (piVar7[0x2b] + iVar12);\n      }\n"
            "      { extern int khdays_controls_camera_pitch(int rate, int inverted);\n"
            "        int tilt = (uVar23 & 0x400) != 0 ? 0 : khdays_controls_camera_pitch(piVar7[0x2b] + iVar12,"
            " (piVar7[0xf] & 8U) != 0);\n"
            "        if (tilt > 0 && FX_Mul(fxRatio, 0x3400) > piVar7[0x21]) piVar7[0x21] += tilt;\n"
            "        if (tilt < 0 && FX_Mul(fxRatio, -0x2000) < piVar7[0x21]) piVar7[0x21] += tilt;\n"
            "        if (tilt != 0) uVar20 |= 0x10; }\n",
        ),
        (
            "    puVar13 = Ov002_ResolveRequestUnlessKind2a(&request.x);\n",
            "    puVar13 = Ov002_ResolveRequestUnlessKind2a(&request.x);\n"
            "    { extern void khdays_trace_camera_request(int found, int x, int y, int kind, int state);\n"
            "      khdays_trace_camera_request(puVar13 != 0, request.x, request.y, request.kind, request.state); }\n",
        ),
        # Chase Camera set to Free (the port's third value of the Config's Chase Camera, after Auto and
        # Manual: runtime/menus/camp.h khdays_camp_chase_free): the game's Manual, except that walking no
        # longer pulls the camera's tilt back to the preset's (its distance still is); the events' cameras
        # and the lock-on keep the game's own. On Auto and Manual the game's as it is.
        (
            "        piVar7[0x21] = iVar11;\n        iVar11 = Ov002_GetCameraDistance(piVar7[0x11]);",
            "        { extern int khdays_camp_chase_free(void);\n"
            "          if (GameState_GetField(CONFIG_CHASE_CAM, 1) == 0 || !khdays_camp_chase_free())\n"
            "              piVar7[0x21] = iVar11; }\n"
            "        iVar11 = Ov002_GetCameraDistance(piVar7[0x11]);",
        ),
    ],
    # Lock-on: whether a target is locked, the lock-on scheme (Config option 0), and whether the
    # lock-on logic runs this frame, so separate lock-on makes R presses only when the game will
    # take them; traced, the logic's state around each frame.
    "ov022@02085380": [  # Ov022_ReadSelectionInput
        (
            '#include "nitro/types.h"\n',
            '#include "nitro/types.h"\n'
            "extern void khdays_trace_call(const char *what);\n"
            "extern void khdays_trace_call_arg(const char *what, int arg);\n"
            "extern void *khdays_trace_pointer(const char *what, void *result);\n"
            "extern int khdays_controls_selection_pressed(int pressed, int other_screen);\n",
        ),
        (
            # the player's battle only: with the 3D on both screens Sora's memory's runs on the other frames
            # (data_0204be04 1), and the L and R pressed then are kept for the player's next frame
            "    held = gPadHeld;\n",
            "    { extern void khdays_controls_selection(int locked, int scheme, int held, int pressed, int flags,"
            " int active, int selected, int repeat100, int repeat200, int repeat_any);\n"
            "      if (runtimeMode == 0)\n"
            "      khdays_controls_selection(context->flags0 & 4, GameState_GetField(0x37c4, 1), gPadHeld,"
            " gPadPressed, context->flags0, context->activationState, context->selectedIndex,"
            " context->repeat100, context->repeat200, context->repeatAny); }\n"
            "    held = gPadHeld;\n",
        ),
        (
            "    pressed = gPadPressed;\n",
            "    pressed = (s16)khdays_controls_selection_pressed(gPadPressed, runtimeMode != 0);\n",
        ),
        (
            "    if (suppressInput != 0) {\n        held = 0;\n        pressed = 0;\n    }\n",
            "    if (suppressInput != 0) {\n        held = 0;\n        pressed = 0;\n    }\n"
            "    { extern void khdays_controls_selection_live(int live);\n"
            "      if (runtimeMode == 0) khdays_controls_selection_live(!suppressInput); }\n",
        ),
        (
            "Ov022_SetSelectionEnabled(0);",
            '(khdays_trace_call_arg("SetSelectionEnabled", 0), Ov022_SetSelectionEnabled(0));',
        ),
        (
            "Ov022_SetSelectionEnabled(1);",
            '(khdays_trace_call_arg("SetSelectionEnabled", 1), Ov022_SetSelectionEnabled(1));',
        ),
        ("Ov022_TryLockOn();", '(khdays_trace_call("Ov022_TryLockOn"), Ov022_TryLockOn());'),
        (
            "Ov022_PickSelectorTarget(0x100)",
            'khdays_trace_pointer("PickSelectorTarget(0x100)", Ov022_PickSelectorTarget(0x100))',
        ),
        (
            "Ov022_PickSelectorTarget(0x200)",
            'khdays_trace_pointer("PickSelectorTarget(0x200)", Ov022_PickSelectorTarget(0x200))',
        ),
        (
            "    Ov022_UpdateSelectionMarker();\n    Ov002_ReaimActor(object, 0);\n",
            "    { extern void khdays_trace_selection_after(int flags, int active, int selected, int value);\n"
            "      khdays_trace_selection_after(context->flags0, context->activationState, context->selectedIndex,"
            " context->selectedValue); }\n"
            "    Ov022_UpdateSelectionMarker();\n    Ov002_ReaimActor(object, 0);\n",
        ),
    ],
    # The command menu: that the player has field control this frame (past
    # the suppression tests, with or without a HUD panel open) and the
    # game's Config option 6, for the command menu on the D-pad; and what
    # each press makes the menu do.
    "ov022@020a2978": [  # Ov022_UpdateCommandInput
        (
            '#include "nitro/types.h"\n',
            '#include "nitro/types.h"\n'
            "extern void khdays_trace_call(const char *what);\n"
            "extern void khdays_trace_call_arg(const char *what, int arg);\n",
        ),
        (
            "    if (pActor->nMenuState == 2 || pActor->nMenuState == 3) {\n",
            "    { extern void khdays_controls_command_input(int scheme, int panel_open, int menu_state, int held,"
            " int pressed, int menu0, int menu1, int deck_word);\n"
            "      khdays_controls_command_input(GameState_GetField(0x37c6, 1), Ov002_Hud_IsPanelOpen(),"
            " pActor->nMenuState, gPadHeld, gPadPressed, pMenu[0], pMenu[1], Ov022_GetRepeatKeys()); }\n"
            "    if (pActor->nMenuState == 2 || pActor->nMenuState == 3) {\n",
        ),
        (  # Retained INFORMATION leaves command navigation, shortcuts and command kinds intact.
            "Ov002_Hud_IsPanelOpen",
            "khdays_ui_command_panel_open",
        ),
        (
            "Ov002_SelectShortcut(nCmd);",
            '(khdays_trace_call_arg("SetPanelModeForRequest", nCmd), Ov002_SelectShortcut(nCmd));',
        ),
        (
            "Ov002_Hud_ActivatePanelSlot();",
            '(khdays_trace_call("Hud_ActivatePanelSlot"), Ov002_Hud_ActivatePanelSlot());',
        ),
        (
            "Ov002_AcceptRequestAndNotify(1)",
            '(khdays_trace_call("AcceptRequestAndNotify"), Ov002_AcceptRequestAndNotify(1))',
        ),
        ("Ov002_RequestCrawlSkip();", '(khdays_trace_call("RequestCrawlSkip"), Ov002_RequestCrawlSkip());'),
        ("Ov002_PanelCursorNext();", '(khdays_trace_call("PlayPanelConfirm"), Ov002_PanelCursorNext());'),
        ("Ov002_PanelCursorPrev();", '(khdays_trace_call("PlayPanelCancel"), Ov002_PanelCursorPrev());'),
        ("Ov002_PanelCursorStepLeft();", '(khdays_trace_call("PanelCursorStepLeft"), Ov002_PanelCursorStepLeft());'),
        ("Ov002_PanelCursorStepRight();", '(khdays_trace_call("Panel_HandleConfirm"), Ov002_PanelCursorStepRight());'),
    ],
    # The movement heading the game makes from the D-pad (Up 0, Left 0x4000,
    # Down 0x8000, Right 0xc000): analog movement gives the stick's instead,
    # for the local player's actor only.
    "ov022@020a0814": [
        (  # Ov022_HeadingFromHeldDpad
            "    if (cnt > 1) r >>= 1;\n    return r;\n",
            "    if (cnt > 1) r >>= 1;\n"
            "    { extern int khdays_controls_move_heading(int actor, int heading);\n"
            "      return khdays_controls_move_heading(actor, r); }\n",
        )
    ],
    # The speed an idle actor moves toward and the animation speed it moves
    # with: analog movement's walk slows both; traced with the heading's
    # reference (the bias the game adds to it).
    "ov022@020a3ccc": [  # Ov022_SteerIdleByAim
        (
            "            nRate = pActor->nWalkSpeed;\n",
            "            { extern int khdays_controls_move_rate(int actor, int rate);\n"
            "              nRate = khdays_controls_move_rate((int)pActor, pActor->nWalkSpeed); }\n",
        ),
        (
            "            vecMove.x = FxMul(vecMove.x, pActor->nStepRate);\n",
            "            { extern void khdays_trace_steer(int actor, int aim, int bias, int angle, int step, int rate);\n"
            "              khdays_trace_steer((int)pActor, nAimAngle, pActor->nAngleBias, nAngle, pActor->nStepRate,"
            " nRate); }\n"
            "            vecMove.x = FxMul(vecMove.x, pActor->nStepRate);\n",
        ),
        (
            "                Ov022_SetAnimSpeed(pActor, (short)nRate);\n",
            "                { extern int khdays_controls_move_anim(int actor, int scale);\n"
            "                  Ov022_SetAnimSpeed(pActor, (short)khdays_controls_move_anim((int)pActor, nRate)); }\n",
        ),
    ],
    # The states an actor enters (jump, guard, ...), traced for the local
    # player's actor: what each button makes Roxas do.
    "ov022@02098834": [
        (  # Ov022_EnterActorState
            "int Ov022_EnterActorState(struct Actor *pActor, int nState, u32 nPrevTimer)\n{\n",
            "int Ov022_EnterActorState(struct Actor *pActor, int nState, u32 nPrevTimer)\n{\n"
            "    { extern void khdays_trace_actor_state(int actor, int state);"
            " khdays_trace_actor_state((int)pActor, nState); }\n",
        )
    ],
    # The frame-rate mode field play runs at, traced.
    "ov002@02067f78": [
        (  # Ov002_TickGameplayState
            "    SetGameMode((u8)*(int *)(base + 0x8b54));\n",
            "    { extern void khdays_trace_frame_mode(int value); khdays_trace_frame_mode(*(int *)(base + 0x8b54)); }\n"
            "    SetGameMode((u8)*(int *)(base + 0x8b54));\n",
        )
    ],
    # The engine's three loads: which game file is
    # read into which buffer -- by path or by packed id, raw or decompressed --
    # for the port's names of the graphics the game uploads
    # (runtime/core/origin.c). Nothing the game does changes, but for the
    # mods' new missions: Archive_LoadFile hands their rows in Mission Mode's
    # list and the missions' table after the game's (runtime/missions/).
    "main@0201ebdc": [
        (  # Loader_RequestFile
            "    OS_SendMessage(data_0204bc1c, self, 1);\n    return (void *)self[10];\n",
            "    { extern void khdays_origin_loaded(unsigned top, unsigned bottom, const void *buffer, unsigned size,\n"
            "                                       int compressed);\n"
            "      khdays_origin_loaded((unsigned)self[3], (unsigned)self[4], (const void *)self[10], (unsigned)self[11],\n"
            "                           self[1]); }\n"
            "    OS_SendMessage(data_0204bc1c, self, 1);\n    return (void *)self[10];\n",
        )
    ],
    "main@0201ed3c": [
        (  # FileLoader_LoadInto
            "    OS_SendMessage(data_0204bc1c, self, 1);\n    return self[11];\n",
            "    { extern void khdays_origin_loaded(unsigned top, unsigned bottom, const void *buffer, unsigned size,\n"
            "                                       int compressed);\n"
            "      khdays_origin_loaded((unsigned)self[3], (unsigned)self[4], (const void *)self[10], (unsigned)self[11],\n"
            "                           self[1]); }\n"
            "    OS_SendMessage(data_0204bc1c, self, 1);\n    return self[11];\n",
        )
    ],
    "main@0201ef9c": [
        (  # Archive_LoadFile
            "    gFileLoader[5] = 0;\n    FS_CloseFile(&file);\n    return buf;\n",
            "    { extern void khdays_origin_loaded(unsigned top, unsigned bottom, const void *buffer, unsigned size,\n"
            "                                       int compressed);\n"
            "      khdays_origin_loaded(file.top, file.bottom, buf, buf != 0 ? size : 0, compressed != 0); }\n"
            "    { extern void khdays_stop_file_loaded(unsigned top, void *buffer, unsigned size);\n"
            "      if (buf != 0) khdays_stop_file_loaded(file.top, buf, size); }\n"
            "    { extern void *khdays_missions_file_loaded(unsigned top, void *buffer, unsigned size, void *heap);\n"
            "      if (buf != 0) buf = khdays_missions_file_loaded(file.top, buf, size, (void *)gFileLoader[5]); }\n"
            "    gFileLoader[5] = 0;\n    FS_CloseFile(&file);\n    return buf;\n",
        )
    ],
    # NitroSystem's geometry sends (RUNTIME_MAP §7 "The direct
    # geometry"): the command words it stores to GXFIFO one by one go to the
    # port's geometry (gfx/g3d.h) as they are, not through the I/O trap; its
    # blocks reach it through MIi_CpuSend32 (hal/sdk_mi.c). What the commands
    # do is unchanged.
    "itcm@01ff9f00": [
        (  # GX_SendFifoWords (NNS_G3dGeBufferOP_N)
            "    *(volatile u32 *)0x04000400 = op;\n",
            "    { extern void khdays_g3d_words(const unsigned int *words, unsigned int count);\n"
            "      khdays_g3d_words((const unsigned int *)&op, 1); }\n",
        )
    ],
    "itcm@01ffa764": [
        (  # NNSi_G3dGeBufferCommand1
            "    *(volatile u32 *)0x04000400 = command;\n    *(volatile u32 *)0x04000400 = argument;\n",
            "    { extern void khdays_g3d_words(const unsigned int *words, unsigned int count);\n"
            "      const unsigned int words[2] = {command, argument};\n"
            "      khdays_g3d_words(words, 2); }\n",
        )
    ],
    # A mod's models drawn instead of the game's (runtime/mods/models.h): each node's matrix as
    # NODEDESC leaves it, and the shapes' display lists unsent while a model is replaced, but for
    # their matrix commands. Without a mod nothing changes.
    "itcm@01ffb8fc": [
        (  # NNSi_G3dDrawInternal
            "    do {\n        u32 command;\n",
            # the render object's joint animation first (its anmJnt, +0x10): a mod character's own skeleton
            # plays its clip for it
            "    { extern void khdays_models_draw_animation(unsigned animation);\n"
            "      khdays_models_draw_animation(*(unsigned *)(renderObj->pad08_20 + 8)); }\n"
            "    { extern void khdays_models_draw_begin(const void *model);\n"
            "      khdays_models_draw_begin(renderObj->pResMdl); }\n"
            "    do {\n        u32 command;\n",
        ),
        (
            "    renderObj->flags &= ~1;\n",
            "    { extern void khdays_models_draw_end(void); khdays_models_draw_end(); }\n"
            "    renderObj->flags &= ~1;\n",
        ),
    ],
    "itcm@01ffc164": [
        (  # NNSi_G3dFuncSbc_NODEDESC
            "    if (state->callbackTimings[6] == 3) {\n        state->sbcCallbacks[6](state);\n    }\n",
            # a mod's character's weapon node first put on its own skeleton's weapon bone, where the node's
            # callback reads it (the game's weapon follows it)
            "    { extern void khdays_models_node_placed(unsigned node, int sent);\n"
            "      khdays_models_node_placed(nodeId, !(state->flags & 0x100)); }\n"
            "    if (state->callbackTimings[6] == 3) {\n        state->sbcCallbacks[6](state);\n    }\n"
            "    { extern void khdays_models_node(unsigned node, int sent);\n"
            "      khdays_models_node(nodeId, !(state->flags & 0x100)); }\n",
        )
    ],
    "itcm@01ffc0d0": [
        (  # NNSi_G3dFuncSbcSHP
            "        NNS_G3dGeSendDL((u8 *)data + data->payloadOffset08, data->payloadSize0c);\n",
            "        { extern int khdays_models_shape(const void *list, unsigned int size);\n"
            "          if (!khdays_models_shape((u8 *)data + data->payloadOffset08, data->payloadSize0c)) {\n"
            "              NNS_G3dGeSendDL((u8 *)data + data->payloadOffset08, data->payloadSize0c);\n"
            "          } }\n",
        )
    ],
    # Gameplay mods' scripts (runtime/scripts/scripts.h): the frame point, once the game's logic of the
    # frame is done and before its pacing; and the game's own moments the mods hear.
    "main@02000bcc": [
        (  # main's frame loop
            "        SoundMgr_Update();\n\n        /* frame-rate pacing",
            "        SoundMgr_Update();\n"
            "        { extern void khdays_scripts_frame(void); khdays_scripts_frame(); }\n\n"
            "        /* frame-rate pacing",
        )
    ],
    "ov002@02068090": [
        (  # Ov002_BeginMissionRun: the session request went through, a run starts (the first after the
            # field begins is the mission's start)
            "        pfnStep = Ov002_SessionTick;\n",
            "        pfnStep = Ov002_SessionTick;\n"
            "        { extern void khdays_scripts_mission_start(void); khdays_scripts_mission_start(); }\n",
        )
    ],
    "ov005@020550e4": [
        (  # Ov005_CommitMissionResults: complete is the routine's own rule for status 3, story missions only
            "    int rank,reward;\n",
            "    int rank,reward;\n"
            "    { extern void khdays_scripts_mission_end(int mission, int rank, int complete);\n"
            "      khdays_scripts_mission_end(config->missionIndex, config->resultRank,\n"
            "                                 config->rewardMode == 255 && config->nOption64 == 0\n"
            "                                     ? config->missionResultValue == config->missionTargetValue : -1); }\n",
        )
    ],
    "ov022@020acc94": [
        (  # Ov022_ActorSetHp: the HP before, then the change told once it is made
            "    struct Ent *pSelf = (struct Ent *)pEnt;\n",
            "    struct Ent *pSelf = (struct Ent *)pEnt;\n    const int khdays_before = pSelf->nHp;\n",
        ),
        (  # a defeat of the local player a mod holds: 1 instead, as the routine does for a host's peer
            "    if (nValue != pSelf->nHp) {\n",
            "    { extern int khdays_scripts_lethal(void *entity, int value); nValue = khdays_scripts_lethal(pEnt, nValue); }\n"
            "    if (nValue != pSelf->nHp) {\n",
        ),
        (
            "    Ov002_GetPanelWord0220(pSelf->nId, pSelf->nHp);\n}\n",
            "    Ov002_GetPanelWord0220(pSelf->nId, pSelf->nHp);\n"
            "    { extern void khdays_scripts_hp(void *entity, int before); khdays_scripts_hp(pEnt, khdays_before); }\n"
            "}\n",
        ),
    ],
    "ov022@020a08a8": [
        (  # Ov022_ReceiveHit: the hit's source while its damage is dealt (a defeat's attacker, api_run.c)
            "        Ov022_ActorSetHp(pActor, pActor->nHp - nAmount);\n",
            "        { extern void khdays_link_hit_from(void *source); khdays_link_hit_from(pReq->pSrc); }\n"
            "        Ov022_ActorSetHp(pActor, pActor->nHp - nAmount);\n"
            "        { extern void khdays_link_hit_from(void *source); khdays_link_hit_from(0); }\n",
        )
    ],
    # The music a mod plays (runtime/scripts/game_link.h): while it plays, the game's own requests to
    # the sound manager's queue (its four ways in) are kept back, the last of them made as it ends.
    "main@020335ec": [
        (  # SetSelectionIfChanged: request 2
            "    int base = gSoundMgr;\n",
            "    int base = gSoundMgr;\n"
            "    { extern int khdays_link_music_request(int kind, int arg, int value);\n"
            "      if (khdays_link_music_request(2, param_1, 0)) return 1; }\n",
        )
    ],
    "main@02033660": [
        (  # SoundMgr_QueueKind1: request 1
            "int SoundMgr_QueueKind1(int arg) {\n",
            "int SoundMgr_QueueKind1(int arg) {\n"
            "    { extern int khdays_link_music_request(int kind, int arg, int value);\n"
            "      if (khdays_link_music_request(1, arg, 0)) return 1; }\n",
        )
    ],
    "main@020336a4": [
        (  # RequestQueue_SetOrPushKind3: request 3, the music stopped
            "void RequestQueue_SetOrPushKind3(int arg) {\n",
            "void RequestQueue_SetOrPushKind3(int arg) {\n"
            "    { extern int khdays_link_music_request(int kind, int arg, int value);\n"
            "      if (khdays_link_music_request(3, 0, arg)) return; }\n",
        )
    ],
    "main@020336e8": [
        (  # SoundMgr_QueueRequest: requests 4 and 5 (SoundMgr_SwitchBgm, SoundMgr_SwitchBgmResume)
            "    unsigned char *ptr;\n",
            "    unsigned char *ptr;\n"
            "    { extern int khdays_link_music_request(int kind, int arg, int value);\n"
            "      if (khdays_link_music_request(kind, arg1, arg2)) return; }\n",
        )
    ],
    # The field's enemies (runtime/scripts/enemies_link.h): the actor a marker row made (a mod's enemy
    # is made that way), every defeat, the area's enemies let go; the class asked for, the spawners and
    # their slots given back only for KHDAYS_TRACE_ENEMIES.
    "ov002@02073d0c": [
        (  # Ov002_CreateActorFromMarker: the actor made from a marker row
            "    if(pRow->bId==0x55){\n",
            "    { extern void khdays_enemies_marker_made(int source, int row, void *actor, int id);\n"
            "      khdays_enemies_marker_made(nSourceIndex, nRowIndex, pActor, pRow->bId); }\n"
            "    if(pRow->bId==0x55){\n",
        )
    ],
    "ov107@020c2ea8": [
        (  # Ov107_SpawnEntityClass
            "    unsigned char state = data_ov107_020cbb20[id];\n",
            "    unsigned char state = data_ov107_020cbb20[id];\n"
            "    { extern void khdays_enemies_class(int id); khdays_enemies_class(id); }\n",
        )
    ],
    "ov107@020c1870": [
        (  # Ov107_Spawner_Run: op 0 (an idle enemy set going) skipped while a mod holds the mission's own
            "    table = 0;\n",
            "    { extern int khdays_enemies_spawner_run(void *spawner, int op);\n"
            "      if (khdays_enemies_spawner_run(obj, op)) return; }\n"
            "    table = 0;\n",
        )
    ],
    "ov107@020c1e80": [
        (  # Ov107_Spawner_OnActorRetired
            "    Ov107Object *obj = actor->pSpawner;\n",
            "    Ov107Object *obj = actor->pSpawner;\n"
            "    { extern void khdays_enemies_retired(void *actor); khdays_enemies_retired(actor); }\n",
        )
    ],
    "ov107@020c7da4": [
        (  # Ov107_AiState_OnDefeat: any enemy defeated, before its reward and its spawner's slot
            "    context = self->field_04;\n",
            "    { extern void khdays_enemies_defeated(void *actor); khdays_enemies_defeated(self); }\n"
            "    context = self->field_04;\n",
        )
    ],
    "ov002@020736c0": [
        (  # Ov002_ReleaseResources: the area's enemies let go
            "    Ov002Res *r = data_ov002_0207fa14;\n",
            "    Ov002Res *r = data_ov002_0207fa14;\n"
            "    { extern void khdays_enemies_area_left(void); khdays_enemies_area_left(); }\n",
        )
    ],
}


def _rooms2d_hooks(ov, draw, menu, empty, list_wrap, member_tail):
    """The port's list of rooms on the game's own screens (ui/rooms2d.c): the game's "Select a group
    to join" (Ovxxx_UpdateMissionMenuOptionScreen) draws each plate's label and colour, a room's
    players on its right, its note (0x3d) and its button's word (0x3e) from the port's view, its hand
    at the port's cursor; its members' screen (..._SelectionScreen) each member's ping and the room's
    name. The two overlays run the same code, their text call and data named apart."""
    # The port's strings go into buffers on the game's own stack (the game reads only its memory),
    # alive until the screen's text is flushed at the function's end.
    externs = (
        "extern void *khdays_rooms2d_label(int plate, void *game, void *empty, void *out, int units);\n"
        "extern int khdays_rooms2d_colour(int plate, int game);\n"
        "extern void *khdays_rooms2d_detail(int plate, void *out, int units);\n"
        "extern int khdays_rooms2d_hand(int game);\n"
        "extern void *khdays_rooms2d_text(int id, void *game, void *out, int units);\n"
        f"extern u16 {empty}[];\n"
    )
    member_externs = (
        "extern void *khdays_rooms2d_title(void *out, int units);\n"
        "extern void *khdays_rooms2d_member_detail(int row, void *out, int units);\n"
    )
    record = f"Ov{ov}_GetVarRecordByIndex(&{menu}->resource, "
    return [
        (
            f"MissionState Ov{ov}_UpdateMissionMenuOptionScreen(void)\n{{\n",
            externs + f"MissionState Ov{ov}_UpdateMissionMenuOptionScreen(void)\n{{\n"
            "    u16 khdays_labels[4][64], khdays_details[4][16], khdays_note[256], khdays_button[64];\n",
        ),
        (record + "0x3d)", f"khdays_rooms2d_text(0x3d, {record}0x3d), khdays_note, 256)"),
        (record + "0x3e)", f"khdays_rooms2d_text(0x3e, {record}0x3e), khdays_button, 64)"),
        (
            f"{draw}(optionLabels[optionIndex], 0x57,",
            f"{draw}(khdays_rooms2d_label(optionIndex, optionLabels[optionIndex], {empty}, "
            "khdays_labels[optionIndex], 64), 0x57,",
        ),
        (
            "(u8)(optionIndex < visibleOptionCount ? 1 : 3), 1, 0, 0);\n",
            "(u8)khdays_rooms2d_colour(optionIndex, optionIndex < visibleOptionCount ? 1 : 3), 1, 0, 0);\n"
            "        if (khdays_rooms2d_detail(optionIndex, khdays_details[optionIndex], 16) != 0) {\n"
            f"            {draw}(khdays_details[optionIndex], 0xb8, optionIndex * 0x18 + 0x23, (u8)1, 1, 1, 0);\n"
            "        }\n",
        ),
        (
            f"Ov{ov}_MissionScene_SetByte95AC((u8){menu}->selection.cursorIndex);",
            f"Ov{ov}_MissionScene_SetByte95AC((u8)khdays_rooms2d_hand((int){menu}->selection.cursorIndex));",
        ),
    ], [
        (
            f"MissionState Ov{ov}_UpdateMissionMenuSelectionScreen(void)\n{{\n",
            member_externs + f"MissionState Ov{ov}_UpdateMissionMenuSelectionScreen(void)\n{{\n"
            "    u16 khdays_pings[4][16], khdays_title[64];\n",
        ),
        (
            list_wrap + member_tail,
            list_wrap + member_tail + "    { int row;\n"
            "      for (row = 0; row < 4; row++) {\n"
            "          if (khdays_rooms2d_member_detail(row, khdays_pings[row], 16) != 0) {\n"
            f"              {draw}(khdays_pings[row], 0xd0, row * 0x18 + 0x23, 1, 1, 1, 0);\n"
            "          }\n"
            "      }\n"
            "      if (khdays_rooms2d_title(khdays_title, 64) != 0) {\n"
            f"          {draw}(khdays_title, 0x80, 0x18, 1, 1, 3, 0);\n"
            "      } }\n",
        ),
    ]


_ov006_list, _ov006_members = _rooms2d_hooks(
    "006",
    "Ov006_MissionDrawTextRunFwd",
    "data_ov006_02056660",
    "data_ov006_020563d4",
    "        Ov006_MissionDrawTextRunFwd(data_ov006_0205651c, 99, visibleRowIndex * 0x18 + 0x23, 1, 1, 0,\n"
    "                                    0);\n",
    "        visibleRowIndex = (visibleRowIndex + 1) & 0xff;\n    }\n",
)
_ov008_list, _ov008_members = _rooms2d_hooks(
    "008",
    "Ov008_ForwardSevenArgs",
    "data_ov008_02090fa0",
    "data_ov008_02090bc4",
    "        Ov008_ForwardSevenArgs(data_ov008_02090d0c, 99, visibleRowIndex * 0x18 + 0x23, 1, 1, 0, 0);\n",
    "        visibleRowIndex = (visibleRowIndex + 1) & 0xff;\n    }\n",
)
# Mission Mode's character select on the game's own screens: "Y Random" added to its help line
# (runtime/menus/charselect.c), written into a buffer on the function's stack.
_SELECT_HELP = "extern void *khdays_charselect_help(int message, void *game, void *out, int units);\n"
HOOKS["ov006@02050fc8"] = [  # Ov006_UpdateMissionMemberMenuScreen
    (
        "void *Ov006_UpdateMissionMemberMenuScreen(void)\n{\n",
        _SELECT_HELP + "void *Ov006_UpdateMissionMemberMenuScreen(void)\n{\n    u16 khdays_help[128];\n",
    ),
    (
        "Ov006_MissionDrawTextRunFwd(record, 0xa, 0xb4, (u8)1, 1, 0, 0);",
        "Ov006_MissionDrawTextRunFwd(khdays_charselect_help((int)messageId, record, khdays_help, 128), 0xa, 0xb4,"
        " (u8)1, 1, 0, 0);",
    ),
]
HOOKS["ov008@0207ce84"] = [  # Ov008_LobbyStep
    (
        "Ov008LobbyStep Ov008_LobbyStep(void)\n{\n",
        _SELECT_HELP + "Ov008LobbyStep Ov008_LobbyStep(void)\n{\n    u16 khdays_help[128];\n",
    ),
    (
        "Ov008_ForwardSevenArgs(Ov008_GetVarRecordByIndex(data_ov008_02090fa0->records, bHost != 0 ? 0x3b : 0x38),"
        " 10, 0xb4, 1, 1, 0, 0);",
        "Ov008_ForwardSevenArgs(khdays_charselect_help(bHost != 0 ? 0x3b : 0x38,"
        " Ov008_GetVarRecordByIndex(data_ov008_02090fa0->records, bHost != 0 ? 0x3b : 0x38), khdays_help, 128),"
        " 10, 0xb4, 1, 1, 0, 0);",
    ),
]
HOOKS["ov006@02050360"] = _ov006_list  # Ov006_UpdateMissionMenuOptionScreen
HOOKS["ov008@0207c248"] = _ov008_list  # Ov008_UpdateMissionMenuOptionScreen
HOOKS["ov006@020508e0"] = _ov006_members  # Ov006_UpdateMissionMenuSelectionScreen
HOOKS["ov008@0207c7c8"] = _ov008_members  # Ov008_UpdateMissionMenuSelectionScreen

# The Stop magic (runtime/battle/stop_magic.c): Days's unfinished sixth magic made whole on what the game
# left. Its three panels are written over three blank records as the panels' table loads, the camp lets
# them in, and the battle's list of magic names gets the names the game left empty.
HOOKS["main@02034150"] = [
    (  # MsgDb_LoadDb: a database loaded whole
        "            ResSlot_Release_2(id);\n            return 0;\n        }\n    }\n    return 1;\n}\n",
        "            ResSlot_Release_2(id);\n            return 0;\n        }\n    }\n"
        "    { extern void khdays_stop_db_loaded(int id); khdays_stop_db_loaded(id); }\n"
        "    return 1;\n}\n",
    )
]
for _eligible in ("ov008@0205ccd8", "ov025@0208f26c"):  # Ov008_IsGridItemEligible, Ov025_IsGridItemEligible
    HOOKS[_eligible] = [
        (
            "    if (513 <= itemId && itemId <= 630) {\n",
            "    { extern int khdays_stop_panel(int id); if (khdays_stop_panel(itemId)) { return 1; } }\n"
            "    if (513 <= itemId && itemId <= 630) {\n",
        )
    ]
HOOKS["ov002@0205cf54"] = [
    (  # Ov002_CreatePanelSession: Stop's levels take the magic submenu's free cells (15-17 by name)
        "    } while (k < 0xf);\n",
        "    } while (k < 0xf);\n"
        "    { extern int khdays_stop_cells(void *session, int count); nCount = khdays_stop_cells(s, nCount); }\n",
    ),
    (
        "    *(int *)((char *)s + 0x4a8) = pReq->nCellMask;\n",
        "    { extern int khdays_stop_cell_mask(void);\n"
        "      *(int *)((char *)s + 0x4a8) = pReq->nCellMask | khdays_stop_cell_mask(); }\n",
    ),
    (  # the battle's magic names loaded
        "    Ov002_InitResourceRecord((char *)s + 0x5e8, gOv002UiBtlMagicTextPath);\n",
        "    Ov002_InitResourceRecord((char *)s + 0x5e8, gOv002UiBtlMagicTextPath);\n"
        "    { extern void khdays_stop_magic_names(void *record); khdays_stop_magic_names((char *)s + 0x5e8); }\n",
    ),
]
# The Stop magic's battle (runtime/battle/stop_magic.c). An actor's pool of magic slots has 15 places from +0x18
# and kept the cast's level animation (its grid entry: Ov022_BuildEntryLabelText) in the word after them, +0x54:
# that word is the port's now (khdays_magic_track), so the place after the 15 is Stop's, 15. Stop's slot is Cure's
# constructor over Stop's model and takes Cure's handlers (its effect stands round its caster, as Cure's does);
# its levels are the magic submenu's cells 15-17. docs/RUNTIME_MAP.md "The magic".
HOOKS["ov022@0209016c"] = [
    (  # Ov022_InitEntryState: the pool cleared, Stop's place with the 15, and the level animation's entry
        "    *(int *)(obj + 0x54) = 0;\n",
        "    *(int *)(obj + 0x54) = 0;\n"
        "    { extern void khdays_magic_track_set(unsigned int pool, int entry);\n"
        "      khdays_magic_track_set((unsigned)obj, 0); }\n",
    )
]
HOOKS["ov022@0208a830"] = [
    (  # Ov022_BuildEntryLabelText: Stop's level's animation
        "    OS_SPrintf(f.tmp, (const char *)&gOv022BaMaMoPackPathFmt, f.buf.w[arg1]);\n",
        "    OS_SPrintf(f.tmp, (const char *)&gOv022BaMaMoPackPathFmt, f.buf.w[arg1]);\n"
        "    { extern void khdays_stop_animation(unsigned int pool, char *path);\n"
        "      khdays_stop_animation((unsigned)arg0, f.tmp); }\n",
    ),
    (  # the level animation's entry to the port
        "    *(int *)(arg0 + 0x54) = Ov022_AcquireGridSlot(\n"
        "        f.tmp, *(unsigned char *)(*(int *)(arg0 + 0x58) + 9), 1,\n"
        "        *(int *)(arg0 + arg1 * 4 + 0x18) + 8);\n",
        "    { extern void khdays_magic_track_set(unsigned int pool, int entry);\n"
        "      khdays_magic_track_set((unsigned)arg0, Ov022_AcquireGridSlot(\n"
        "          f.tmp, *(unsigned char *)(*(int *)(arg0 + 0x58) + 9), 1,\n"
        "          *(int *)(arg0 + arg1 * 4 + 0x18) + 8)); }\n",
    ),
]


def _magic_track_reads(signature: str, field: str, as_type: str = "") -> list[tuple[str, str]]:
    """A handler of the magics reading the pool's level animation entry (its `pCtx->nTrack`, `->pOwner` or
    `->pTrack`, of type `as_type`): the port's."""
    read = "khdays_magic_track((unsigned)pCtx)"
    return [
        (signature, "extern int khdays_magic_track(unsigned int pool);\n" + signature),
        (f"pCtx->{field}", f"(({as_type}){read})" if as_type else read),
    ]


for _key, _signature, _field, _type in (
    ("ov022@0208e404", "void Ov022_ArmSlotAtActor(struct ReactionCtx *pCtx)\n", "nTrack", ""),
    ("ov022@0208f888", "void Ov022_EnterBlockReaction(struct ReactionCtx *pCtx)\n", "nTrack", ""),
    ("ov022@0208de14", "void Ov022_EnterReactionAtActor(struct ReactionCtx *pCtx)\n", "nTrack", ""),
    ("ov022@0208c9b4", "void Ov022_EnterReactionAtOffset(struct ReactionCtx *pCtx)\n", "nTrack", ""),
    ("ov022@0208fe40", "int Ov022_StepReaction(struct ReactionCtx *pCtx, int nDelta)\n", "nTrack", ""),
    ("ov022@0208eee8", "void Ov022_EnterChainReaction(struct ReactionCtx *pCtx)\n", "pOwner", "void *"),
    ("ov022@0208f2d4", "void Ov022_EnterGroundReaction(struct ReactionCtx *pCtx)\n", "pOwner", "void *"),
    ("ov022@0208b848", "void Ov022_LaunchSlotPart(struct ReactionCtx *pCtx)\n", "pOwner", "void *"),
    ("ov022@0208be38", "void Ov022_LaunchSlotPartArc(struct ReactionCtx *pCtx)\n", "pOwner", "void *"),
    ("ov022@0208d764", "void Ov022_LaunchSlotPartFlat(struct ReactionCtx *pCtx)\n", "pOwner", "void *"),
    ("ov022@0208cfbc", "void Ov022_LaunchSlotPartTracked(struct ReactionCtx *pCtx)\n", "pOwner", "void *"),
):
    HOOKS[_key] = _magic_track_reads(_signature, _field, _type)
# The panels' pictures loaded (the camp's two menus, the results): the clock Stop's panels show, in the magics' purple.
_STOP_PICTURES = (
    "    {{ extern void khdays_stop_pictures(unsigned int archive); khdays_stop_pictures((unsigned){0}); }}\n"
)
HOOKS["ov008@0205f98c"] = [  # Ov008_InitGridMenuTextures
    (
        "    Obj_RelocateSections(pCtx->pIconArchive, 0);\n",
        "    Obj_RelocateSections(pCtx->pIconArchive, 0);\n" + _STOP_PICTURES.format("pCtx->pIconArchive"),
    )
]
HOOKS["ov025@02091edc"] = [  # Ov025_InitGridMenuTextures
    (
        "    Obj_RelocateSections(pCtx->pIconArchive, 0);\n",
        "    Obj_RelocateSections(pCtx->pIconArchive, 0);\n" + _STOP_PICTURES.format("pCtx->pIconArchive"),
    )
]
HOOKS["ov005@02052a7c"] = [  # Ov005_AllocateResultTextures
    (
        "    Obj_RelocateSections(context->textureSet.archive,0);\n",
        "    Obj_RelocateSections(context->textureSet.archive,0);\n"
        + _STOP_PICTURES.format("context->textureSet.archive"),
    )
]
# The cast's message (kind 0xc, its slot in five bits) carries the cell, Stop's level with it: a copy that receives
# it takes Stop's place and that level, as the caster's does (Ov022_SetRecordValue).
HOOKS["ov022@0208a2b4"] = _magic_track_reads("void Ov022_OnEndMessage(struct EndMessage *pMsg)\n", "pTrack", "u8 *") + [
    (  # Ov022_OnEndMessage, a cast begun on another copy: its slot
        "        pCtx->nSlot = pMsg->nSlot;\n",
        "        { extern int khdays_stop_record(unsigned int pool, int value);\n"
        "          pCtx->nSlot = khdays_stop_record((unsigned)pCtx, pMsg->nSlot); }\n",
    )
]
HOOKS["ov022@0208a464"] = [
    (  # Ov022_MarshalStateHalf12: the cast's message with the cell (Stop's level) for its slot
        "    if (param_2 == 0) buf.f5 = *param_3;\n",
        "    if (param_2 == 0) {\n"
        "        extern int khdays_stop_message_slot(unsigned int pool, int slot);\n"
        "        buf.f5 = khdays_stop_message_slot((unsigned)obj, *param_3);\n"
        "    }\n",
    )
]
# A player another's Stop holds (friendly fire on) stands still: on the host its input, state and motion (the
# guests' copies follow the host's), on every copy its animation.
HOOKS["ov022@020a0008"] = [
    (  # Ov022_ActorUpdate
        "    void *pNext;\n\n    VEC_COPY(&pActor->vecPrevPos, &pActor->vecPos);\n",
        "    void *pNext;\n"
        "    int khdays_held;\n\n"
        "    { extern int khdays_stop_player_holding(void *actor); khdays_held = khdays_stop_player_holding(pActor); }\n"
        "    VEC_COPY(&pActor->vecPrevPos, &pActor->vecPos);\n",
    ),
    (
        "    Ov022_StepScaledAnimTracks(pActor);\n",
        "    if (!khdays_held) {\n        Ov022_StepScaledAnimTracks(pActor);\n    }\n",
    ),
    (
        "    if (Session_GetLocalPlayerIndex() == 0) {\n        pActor->nAnimHandle = Anim_GetFrame(&pActor->pNode->anim, 0);\n",
        "    if (Session_GetLocalPlayerIndex() == 0 && khdays_held) {\n"
        "        /* held by Stop */\n"
        "    } else if (Session_GetLocalPlayerIndex() == 0) {\n"
        "        pActor->nAnimHandle = Anim_GetFrame(&pActor->pNode->anim, 0);\n",
    ),
]
HOOKS["ov022@0208cbf8"] = [
    (  # func_ov022_0208cbf8: the level animation's entry from the port
        "int func_ov022_0208cbf8(int arg0, int arg1) {\n",
        "extern int khdays_magic_track(unsigned int pool);\nint func_ov022_0208cbf8(int arg0, int arg1) {\n",
    ),
    ("*(int *)(arg0 + 0x54)", "khdays_magic_track((unsigned)arg0)"),
]
HOOKS["ov022@020901d4"] = [
    (  # Ov022_BuildSlotPool: Stop's slot, Cure's constructor over Stop's model, moved to Stop's place with a
        # kind of its own (not Cure's heal: Ov022_ApplyReactionHit), Cure's place given back
        "    NNSi_FndFreeFromDefaultHeap(pTemplates);\n",
        "    {\n"
        "        extern int khdays_stop_slot_wanted(int actor, char *path, int *parts, int *template_index);\n"
        "        int khdays_parts, khdays_template;\n"
        "        if (khdays_stop_slot_wanted(pActor->nId, szModel, &khdays_parts, &khdays_template)) {\n"
        "            struct Slot *khdays_cure = pPool->apSlots[12];\n"
        "            data_ov022_020b29e4[12](pPool, szModel, khdays_parts, &pTemplates[khdays_template + khdays_parts - 1]);\n"
        "            pPool->apSlots[15] = pPool->apSlots[12];\n"
        "            pPool->apSlots[12] = khdays_cure;\n"
        "            *(u8 *)pPool->apSlots[15] = 15;\n"
        "            pPool->apSlots[15]->nCount = 0;\n"
        "            pPool->apSlots[15]->nMark = -1;\n"
        "            pPool->nKindMask |= 1 << 15;\n"
        "        }\n"
        "    }\n"
        "    NNSi_FndFreeFromDefaultHeap(pTemplates);\n",
    ),
]
HOOKS["ov022@02090198"] = [
    (  # func_ov022_02090198, the pool's teardown: Stop's slot freed with the 15 (Cure's, whose handlers it takes,
        # has no destructor either); the level animation's entry forgotten
        "    if ((*slot & 1) != 0) {\n",
        "    { extern void khdays_magic_track_set(unsigned int pool, int entry); khdays_magic_track_set((unsigned)slot, 0); }\n"
        "    if ((*slot & 1) != 0) {\n"
        "        int khdays_slots = (*(unsigned *)((char *)slot + 8) & (1 << 15)) != 0 ? 0x10 : 0xf;\n",
    ),
    ("} while (i < 0xf);", "} while (i < khdays_slots);"),
]
# The magics' handler tables (begin, step, update) hold 15, laid where the DS has them: Stop's slot takes Cure's.
HOOKS["ov022@0209093c"] = [
    (  # func_ov022_0209093c: the cast slot's update
        "((void (**)(short *))&data_ov022_020b2a98)[idx](arg0);",
        "((void (**)(short *))&data_ov022_020b2a98)[idx == 15 ? 12 : idx](arg0);",
    )
]
HOOKS["ov022@020902d4"] = [
    (  # Ov022_SetRecordValue: a cast's slot -- each of Stop's levels (cells 15-17) casts Stop's slot
        "void Ov022_SetRecordValue(unsigned short *record, int value) {\n",
        "void Ov022_SetRecordValue(unsigned short *record, int value) {\n"
        "    { extern int khdays_stop_record(unsigned int pool, int value); value = khdays_stop_record((unsigned)record, value); }\n",
    )
]
HOOKS["ov022@02090978"] = [
    (  # Ov022_GetSlotMoveMode: Stop's levels stand still to cast, as Cure's do
        "    case 14:\n        nMode = MODE_STILL;\n",
        "    case 14:\n    case 15:\n    case 16:\n    case 17:\n        nMode = MODE_STILL;\n",
    )
]
HOOKS["ov022@0209038c"] = [
    (  # Ov022_IsSlotActionAllowed: Stop's levels are its slot and the port's casts (the party record's 16th pair is
        # Dodge Roll's)
        "    if (func_ov022_0209029c(obj, idx) == 0) return 0;\n"
        "    if (Load2DArrayU8(*(unsigned char *)(*(int *)(obj + 0x58) + 9), idx) <= 0) return 0;\n",
        "    if (idx >= 15 && idx <= 17) {\n"
        "        extern int khdays_stop_casts_left(unsigned int pool, int level);\n"
        "        if (func_ov022_0209029c(obj, 15) == 0 || khdays_stop_casts_left((unsigned)obj, idx - 15) <= 0) return 0;\n"
        "    } else {\n"
        "        if (func_ov022_0209029c(obj, idx) == 0) return 0;\n"
        "        if (Load2DArrayU8(*(unsigned char *)(*(int *)(obj + 0x58) + 9), idx) <= 0) return 0;\n"
        "    }\n",
    )
]
HOOKS["ov022@0208a6d0"] = [
    (  # Ov022_BeginSubStateDispatch: the cast slot's begin, Stop's Cure's
        "data_ov022_020b2a20[*(int *)(obj + 0xc)]",
        "data_ov022_020b2a20[*(int *)(obj + 0xc) == 15 ? 12 : *(int *)(obj + 0xc)]",
    ),
    (  # Stop's cast spends the port's count and marks its zone
        "    r = Load2DArrayU8(*(unsigned char *)(e + 9), *(int *)(obj + 0xc));\n",
        "    { extern int khdays_stop_cast(unsigned int pool); if (khdays_stop_cast((unsigned)obj)) return; }\n"
        "    r = Load2DArrayU8(*(unsigned char *)(e + 9), *(int *)(obj + 0xc));\n",
    ),
]
HOOKS["ov022@020a2978"] += [
    (  # Ov022_UpdateCommandInput: Stop's cells, their casts and whether each may be cast
        "    if (GameState_IsFlagSet(0x20e0) != 0) {\n",
        "    { extern void khdays_stop_hud(int actor, int *mask); khdays_stop_hud(pActor->nId, &uMask); }\n"
        "    if (GameState_IsFlagSet(0x20e0) != 0) {\n",
    )
]
HOOKS["ov022@0209045c"] = [
    (  # Ov022_StepReactionState: the level animation's entry from the port
        "void Ov022_StepReactionState(struct ReactionCtx *pCtx, int nDelta)\n",
        "extern int khdays_magic_track(unsigned int pool);\n"
        "void Ov022_StepReactionState(struct ReactionCtx *pCtx, int nDelta)\n",
    ),
    ("pCtx->pOwner", "((void *)khdays_magic_track((unsigned)pCtx))"),
    (  # the cast slot's step, Stop's Cure's
        "data_ov022_020b2a5c[pCtx->nSlot]",
        "data_ov022_020b2a5c[pCtx->nSlot == 15 ? 12 : pCtx->nSlot]",
    ),
]
HOOKS["ov107@020c5cfc"] = [
    (  # Ov107_AiState_ApplyHit: an enemy's hit, its damage worked out
        "    amount = Ov107_CalcHitDamage(obj, packet);\n",
        "    { extern void khdays_stop_measure(int on); khdays_stop_measure(1); }\n"
        "    amount = Ov107_CalcHitDamage(obj, packet);\n"
        "    { extern void khdays_stop_measure(int on); khdays_stop_measure(0); }\n"
        "    { extern int khdays_stop_store(void *enemy, int amount); amount = khdays_stop_store(obj, amount); }\n",
    ),
]
HOOKS["ov107@020c6980"] = [
    (  # Ov107_ProcessObjectTick: a held enemy updated as a frozen one is (its tasks with no time, no motion)
        "void Ov107_ProcessObjectTick(struct AiState *self, int delta)\n{\n",
        "int khdays_stop_holding(void *enemy);\nint khdays_stop_held(void *enemy);\n"
        "void Ov107_ProcessObjectTick(struct AiState *self, int delta)\n{\n",
    ),
    (
        "        if (((self->field_1c4 & 2) != 0 || (self->field_1c4 & 8) != 0) &&\n",
        "        if (((self->field_1c4 & 2) != 0 || (self->field_1c4 & 8) != 0 || khdays_stop_holding(self)) &&\n",
    ),
    (
        "            } else if ((self->field_1c4 & 2) == 0) {\n",
        "            } else if ((self->field_1c4 & 2) == 0 && !khdays_stop_held(self)) {\n",
    ),
    (  # a held enemy moving again takes the damage it kept, as one timed hit (a burn's, above)
        "    if (self->field_1c8 != 1 && self->currentAction == 1) {\n",
        "    {\n"
        "        extern int khdays_stop_release(void *enemy, int *amount);\n"
        "        extern void khdays_stop_released(void *enemy);\n"
        "        int khdays_amount;\n"
        "        if (khdays_stop_release(self, &khdays_amount)) {\n"
        "            struct HitPacket khdays_hit = {0};\n"
        "            khdays_hit.flags00 = 0x88;\n"
        "            khdays_hit.flags02 = 0x80;\n"
        "            khdays_hit.amount = khdays_amount;\n"
        "            Ov107_AiState_ApplyHit(self, self->field_25c, &khdays_hit);\n"
        "            khdays_stop_released(self);\n"
        "        }\n"
        "    }\n"
        "    if (self->field_1c8 != 1 && self->currentAction == 1) {\n",
    ),
]
HOOKS["ov107@020c89e8"] = [
    (  # Ov107_CalcHitDamage: none on a held enemy (its own hit handler asks too), the damage being kept
        "int Ov107_CalcHitDamage(Actor *self, struct HitEvent *hit)\n{\n",
        "static int khdays_calc_hit_damage(Actor *self, struct HitEvent *hit);\n"
        "int Ov107_CalcHitDamage(Actor *self, struct HitEvent *hit)\n{\n"
        "    extern int khdays_stop_damage(void *enemy, int amount);\n"
        "    return khdays_stop_damage(self, khdays_calc_hit_damage(self, hit));\n"
        "}\n"
        "static int khdays_calc_hit_damage(Actor *self, struct HitEvent *hit)\n{\n",
    )
]

# The mods' new missions (runtime/missions/): their progress lies where the game keeps a mission's, by its
# number, past the flag array's 0x4000 bits; a field or flag there of a new mission is the port's, any other
# the game's own call as before.
HOOKS["main@020235d0"] = [
    (  # GameState_GetField
        "    return BitArray_GetField(gGameState + 0x10, arg0, arg1);\n",
        "    if ((unsigned int)arg0 >= 0x4000u) {\n"
        "        extern long long khdays_missions_field_get(unsigned int field, unsigned int bits);\n"
        "        long long khdays_value = khdays_missions_field_get((unsigned int)arg0, (unsigned int)arg1);\n"
        "        if (khdays_value >= 0) return (int)khdays_value;\n"
        "    }\n"
        "    return BitArray_GetField(gGameState + 0x10, arg0, arg1);\n",
    )
]
HOOKS["main@020235e8"] = [
    (  # GameState_SetField
        "    BitArray_SetField(gGameState + 0x10, arg0, arg1, arg2);\n",
        "    { extern int khdays_missions_field_set(unsigned int field, unsigned int bits, unsigned int value);\n"
        "      if (arg0 >= 0x4000u && khdays_missions_field_set(arg0, arg1, arg2)) return; }\n"
        "    BitArray_SetField(gGameState + 0x10, arg0, arg1, arg2);\n",
    )
]
HOOKS["main@02023588"] = [
    (  # GameState_IsFlagSet
        "    if (BitArray_TestBit(gGameState + 0x10, arg) != 0) {\n",
        "    if (arg >= 0x4000u) {\n"
        "        extern int khdays_missions_flag_get(unsigned int flag);\n"
        "        int khdays_value = khdays_missions_flag_get(arg);\n"
        "        if (khdays_value >= 0) return khdays_value;\n"
        "    }\n"
        "    if (BitArray_TestBit(gGameState + 0x10, arg) != 0) {\n",
    )
]
HOOKS["main@020235a8"] = [
    (  # GameState_SetFlag
        "    return BitArray_SetBit(*(int *)&gGameState + 0x10, arg0);\n",
        "    { extern int khdays_missions_flag_set(unsigned int flag, int on);\n"
        "      if ((unsigned int)arg0 >= 0x4000u && khdays_missions_flag_set((unsigned int)arg0, 1)) return 0; }\n"
        "    return BitArray_SetBit(*(int *)&gGameState + 0x10, arg0);\n",
    )
]
HOOKS["main@020235bc"] = [
    (  # GameState_ClearFlag
        "    BitArray_ClearBit(gGameState + 0x10, flag);\n",
        "    { extern int khdays_missions_flag_set(unsigned int flag, int on);\n"
        "      if ((unsigned int)flag >= 0x4000u && khdays_missions_flag_set((unsigned int)flag, 0)) return; }\n"
        "    BitArray_ClearBit(gGameState + 0x10, flag);\n",
    )
]
# ... the best result the results keep, gGameState's missionResultRecords[200]: a new mission's in the
# port's store
HOOKS["ov005@020550e4"] += [
    (  # Ov005_CommitMissionResults
        "    if(config->updateMissionRecord)gGameState->missionResultRecords[config->missionIndex]="
        "config->missionResultValue;\n",
        "    if(config->updateMissionRecord){ extern void khdays_missions_set_best(int number, int value);\n"
        "        if(config->missionIndex<200)gGameState->missionResultRecords[config->missionIndex]="
        "config->missionResultValue;\n"
        "        else khdays_missions_set_best(config->missionIndex,config->missionResultValue); }\n",
    )
]
HOOKS["ov005@02057c48"] = [
    (  # Ov005_InitializeResultConfiguration
        "void Ov005_InitializeResultConfiguration(void) {\n",
        "extern int khdays_missions_best(int number);\nvoid Ov005_InitializeResultConfiguration(void) {\n",
    ),
    (
        "gGameState->missionResultRecords[config->missionIndex]",
        "(config->missionIndex<200?gGameState->missionResultRecords[config->missionIndex]:"
        "(u32)khdays_missions_best(config->missionIndex))",
    ),
]
for _key in ("ov008@02074c40", "ov025@020aa298"):  # Ov008_MissionMenu_DrawInfo, Ov025_MissionMenu_DrawInfo
    HOOKS[_key] = [
        (
            "            nRecord = gGameState->missionResultRecords[nMissionId];\n",
            "            { extern int khdays_missions_best(int number);\n"
            "              nRecord = nMissionId < 200 ? gGameState->missionResultRecords[nMissionId]\n"
            "                                         : khdays_missions_best(nMissionId); }\n",
        )
    ]
# ... their rows in Mission Mode's list: shown by the port's own test (their numbers past the game's 900)
HOOKS["ov008@02056048"] = [
    (  # Ov008_IsEncounterEligible
        "    unsigned int bit;\n    unsigned int m;\n",
        "    unsigned int bit;\n    unsigned int m;\n"
        "    { extern int khdays_missions_eligible(const void *record, unsigned int flags);\n"
        "      int khdays_shown = khdays_missions_eligible((const void *)param_1, param_2);\n"
        "      if (khdays_shown >= 0) return khdays_shown; }\n",
    )
]
# ... a later wave's spawners, made after their region was enabled: running as the first wave's (the port's
# missions only)
HOOKS["ov002@02073a10"] = [
    (  # Ov002_CreateSlotObjectAndStart
        "        pCtx->pSlots[nIndex].nActiveEvent=-1;\n",
        "        pCtx->pSlots[nIndex].nActiveEvent=-1;\n"
        "        { extern void khdays_missions_spawner_made(void *node);\n"
        "          khdays_missions_spawner_made(pCtx->pSlots[nIndex].pObject); }\n",
    )
]
# ... their chests and emblems, which Mission Mode's own missions leave out: put out in a new one's
HOOKS["ov002@0206e1a4"] = [
    (  # Ov002_ScriptSpawnTimedSet
        "    if (data_0204c240 & 2)\n        return;\n",
        "    { extern int khdays_missions_spots_shown(void);\n"
        "      if ((data_0204c240 & 2) && !khdays_missions_spots_shown())\n        return; }\n",
    )
]
HOOKS["ov002@0206e524"] = [
    (  # Ov002_SpawnStateSpots
        "    if ((data_0204c240 & 2) == 0 && Session_GetLocalPlayerIndex() == 0) {\n",
        "    extern int khdays_missions_spots_shown(void);\n"
        "    if (((data_0204c240 & 2) == 0 || khdays_missions_spots_shown()) && Session_GetLocalPlayerIndex() == 0) {\n",
    )
]
# ... their chests opened noted (the marks the end raises the flags of) and their chests' module asked, as the story's
HOOKS["ov002@0206dc70"] = [
    (  # Ov002_NoteSeatAnswer
        "        if ((data_0204c240 & 2) != 0) {\n            return;\n        }\n",
        "        extern int khdays_missions_spots_shown(void);\n"
        "        if ((data_0204c240 & 2) != 0 && !khdays_missions_spots_shown()) {\n            return;\n        }\n",
    )
]
HOOKS["ov002@0206dc10"] = [
    (  # Ov002_ModuleHasKey
        "    if ((data_0204c240 & 2) != 0) {\n        return 0;\n    }\n",
        "    extern int khdays_missions_spots_shown(void);\n"
        "    if ((data_0204c240 & 2) != 0 && !khdays_missions_spots_shown()) {\n        return 0;\n    }\n",
    )
]
# ... and their opened chests' flags raised at the end, as the story's
HOOKS["ov002@0206a304"] = [
    (  # Ov002_RaiseSeatFlagsForKey
        "    if ((data_0204c240 & 2) == 0) {\n",
        "    extern int khdays_missions_spots_shown(void);\n"
        "    if ((data_0204c240 & 2) == 0 || khdays_missions_spots_shown()) {\n",
    )
]
# ... their files ("mi/mi/<file>"), which the ROM's file table does not name: opened where the ROM holds them
HOOKS["main@0200acb0"] = [
    (  # FS_OpenFile
        "int FS_OpenFile(void *file, const char *path) {\n    struct FileID id;\n",
        "int FS_OpenFile(void *file, const char *path) {\n    struct FileID id;\n"
        "    {\n"
        "        extern int khdays_rom_added_file(const char *path, unsigned int *bounds);\n"
        "        extern int FS_OpenFileDirect(void *file, void *arc, unsigned int top, unsigned int bottom,\n"
        "                                     unsigned int index);\n"
        "        extern char data_02046334[]; /* fsi_arc_rom */\n"
        "        unsigned int khdays_bounds[3];\n"
        "        if (khdays_rom_added_file(path, khdays_bounds))\n"
        "            return FS_OpenFileDirect(file, data_02046334, khdays_bounds[0], khdays_bounds[1],\n"
        "                                     khdays_bounds[2]);\n"
        "    }\n",
    )
]


# Characters of mods (port/runtime/characters, docs/RUNTIME_MAP.md "Characters of mods"): a new character is a
# number past the game's (0x16 on) in the select's places only; the seats (data_020429c8) and the members
# (gPartyMembers) get its base's number, so that nothing of the game but the select ever sees the new one, and the
# port keeps which seat is a new character (khdays_characters_seat). Where the game opens a character's data for a
# seat's actor, the character's own folder ba/ch/<code>/, which the port serves (its files, else copies of its
# base's).
CHARACTER_DECLARATIONS = (
    "extern int khdays_characters_known(int number);\n"
    "extern int khdays_characters_of(int slot, int kind);\n"
    "extern int khdays_characters_base(int number);\n"
    "extern const char *Port_CharacterCode(int number);\n"
)
# the own folders' codes, by number ("c16" ...): in the game's memory, for its paths (a game-side function:
# prepare.py makes a new global's definition extern, as a split unit's static)
CHARACTER_CODES = (
    "static const char sPortCharacterCodes[0x100][4] = {"
    + ", ".join('""' if n < 0x16 else f'"c{n:02x}"' for n in range(0x100))
    + "};\n"
    "const char *Port_CharacterCode(int number)\n{\n    return sPortCharacterCodes[number & 0xff];\n}\n"
)


def character_folder(number: str) -> str:
    """The data folder of character `number`: its own for a new one."""
    return f"(khdays_characters_known({number}) ? Port_CharacterCode({number}) : data_02042a70[{number}])"


def actor_folder(slot: str, kind: str) -> str:
    """The data folder of seat `slot`'s character, which plays as `kind`: its own for a new one."""
    n = f"khdays_characters_of({slot}, {kind})"
    return f"({n} != ({kind}) ? Port_CharacterCode({n}) : data_02042a70[{kind}])"


HOOKS["main@02035730"] = [
    (  # LevelTable_ReadEntry: the levels of a new character from its own folder
        "extern char *data_02042a70[];\n",
        "extern char *data_02042a70[];\n" + CHARACTER_DECLARATIONS + CHARACTER_CODES,
    ),
    ("gBaChPath, data_02042a70[kind]);", f"gBaChPath, {character_folder('kind')});"),
]
HOOKS["main@02035328"] = [
    (  # PartyMember_Reset: a seat's new character's levels
        "void PartyMember_Reset(int slot, int kind, int level)\n",
        "extern int khdays_characters_of(int slot, int kind);\nvoid PartyMember_Reset(int slot, int kind, int level)\n",
    ),
    (
        "    LevelTable_ReadEntry(kind, level, &e);\n",
        "    LevelTable_ReadEntry(khdays_characters_of(slot, kind), level, &e);\n",
    ),
]
HOOKS["ov022@0209c5ec"] = [
    (  # Ov022_BuildActorSlot: its weapons' texts
        "extern char *data_02042a70[];\n",
        "extern char *data_02042a70[];\n" + CHARACTER_DECLARATIONS,
    ),
    ("data_02042a70[pActor->nKind]", actor_folder("pActor->nId", "pActor->nKind")),
]
HOOKS["ov022@020b0720"] = [
    (  # Ov022_BuildActionTable: its weapon, combos and links
        "extern char *data_02042a70[];\n",
        "extern char *data_02042a70[];\n" + CHARACTER_DECLARATIONS,
    ),
    (
        "    Ov002_LoadCharacterWeapon(&pTable->blkMove, nKind, nArg);\n",
        "    Ov002_LoadCharacterWeapon(&pTable->blkMove, khdays_characters_of(pActor->nId, nKind), nArg);\n",
    ),
    ("data_02042a70[nKind]", actor_folder("pActor->nId", "nKind")),
]
HOOKS["ov022@020b02d8"] = [
    (  # Ov022_LoadActionRows: its actions' files
        "extern char *data_02042a70[];\n",
        "extern char *data_02042a70[];\n" + CHARACTER_DECLARATIONS,
    ),
    ("data_02042a70[nKind]", actor_folder("pTable->nSlotBase", "nKind")),  # the actor's slot (BuildActionTable)
]
HOOKS["ov022@0209cda8"] = [
    (  # Ov022_LoadActorEffects
        "extern const char *data_02042a70[];\n",
        "extern const char *data_02042a70[];\n" + CHARACTER_DECLARATIONS,
    ),
    ("data_02042a70[pActor->nModelId]", actor_folder("pActor->nId", "pActor->nModelId")),
]
HOOKS["ov022@020b1730"] = [
    (  # Ov022_StartAnimRequest: its animations
        "extern const char *const data_02042a70[];\n",
        "extern const char *const data_02042a70[];\n" + CHARACTER_DECLARATIONS,
    ),
    ("data_02042a70[pOwner->nModel]", actor_folder("pOwner->nId", "pOwner->nModel")),
]
HOOKS["ov022@02096e94"] = [
    (  # Ov022_ApplyMissionLevelStats: its own levels
        "extern void LevelTable_ReadEntry(int nCharacterId, int nLevel, struct StatRow *pRow);\n",
        "extern void LevelTable_ReadEntry(int nCharacterId, int nLevel, struct StatRow *pRow);\n"
        "extern int khdays_characters_of(int slot, int kind);\n",
    ),
    (
        "LevelTable_ReadEntry(pActor->nCharacterId, ",
        "LevelTable_ReadEntry(khdays_characters_of(pActor->nId, pActor->nCharacterId), ",
    ),
]
HOOKS["ov023@02085b00"] = [
    (  # Ov023_CmdSetupPartyActors: a seat's weapons' texts
        "extern const char *data_02042a70[];                                 /* member names by kind */\n",
        "extern const char *data_02042a70[];                                 /* member names by kind */\n"
        + CHARACTER_DECLARATIONS,
    ),
    ("data_02042a70[nKind]", actor_folder("i", "nKind")),
]
for ov, address in (("002", "020522c8"), ("008", "0208b890")):
    HOOKS[f"ov{ov}@{address}"] = [
        (  # Ov00N_LoadCharacterWeapon: its weapon's move block, read as its base's
            "extern char *data_02042a70[];\n",
            "extern char *data_02042a70[];\n" + CHARACTER_DECLARATIONS,
        ),
        ("data_02042a70[slot]", character_folder("slot")),
        (
            f"Ov{ov}_PackTableRow(dst, file, slot, arg);",
            f"Ov{ov}_PackTableRow(dst, file, khdays_characters_base(slot), arg);",
        ),
    ]
for ov, address in (("006", "0204d610"), ("008", "0207928c")):
    HOOKS[f"ov{ov}@{address}"] = [
        (  # Ov00N_MenuEntryToSlot: a new character's place is its base's character
            f"int Ov{ov}_MenuEntryToSlot(int id) {{\n    int slot = 0;\n",
            "extern int khdays_characters_known(int number);\n"
            "extern int khdays_characters_base(int number);\n"
            f"int Ov{ov}_MenuEntryToSlot(int id) {{\n    int slot = 0;\n"
            "    if (khdays_characters_known(id & 0xff)) {\n        return khdays_characters_base(id & 0xff);\n    }\n",
        )
    ]
# ... the seats written from the select's places: the port keeps which is a new character's
_SEAT = "extern void khdays_characters_seat(int seat, int place);\n"
HOOKS["ov006@0204e8b0"] = [
    (  # Ov006_UpdateMissionEntrySynchronization
        "MissionCallback Ov006_UpdateMissionEntrySynchronization(void) {\n",
        _SEAT + "MissionCallback Ov006_UpdateMissionEntrySynchronization(void) {\n",
    ),
    (
        "CopyToSlotTable8(&slot, entryIndex);",
        "CopyToSlotTable8(&slot, entryIndex);\n khdays_characters_seat(entryIndex, -1);",
    ),
    (
        "CopyToSlotTable8(&slot, outputIndex);",
        "CopyToSlotTable8(&slot, outputIndex);\n khdays_characters_seat(outputIndex, entries[entryIndex].characterId);",
    ),
]
HOOKS["ov008@0207a758"] = [
    (  # Ov008_MissionLobbyPoll (its Ov008_ForwardJoinedSlots)
        "static inline void Ov008_ForwardJoinedSlots(MissionContext *pCtx)\n",
        _SEAT + "static inline void Ov008_ForwardJoinedSlots(MissionContext *pCtx)\n",
    ),
    ("CopyToSlotTable8(&info, i);", "CopyToSlotTable8(&info, i);\n khdays_characters_seat(i, -1);"),
    (
        "CopyToSlotTable8(&info, nSlot);",
        "CopyToSlotTable8(&info, nSlot);\n khdays_characters_seat(nSlot, pPacket[i].characterId);",
    ),
]
# ... the camp's places, made from the seats: a new character's seat's place on its own number
HOOKS["ov008@0207a424"] = [
    (  # Ov008_MissionLobbyStartTransfer (its Ov008_BuildGroupMessage)
        "static inline void Ov008_BuildGroupMessage(MissionContext *pCtx)\n",
        "extern int khdays_characters_place(int seat, int entry);\n"
        "static inline void Ov008_BuildGroupMessage(MissionContext *pCtx)\n",
    ),
    (
        "pPacket[i].characterId = Ov008_MenuSlotToEntry(pInfo->nMemberKind);",
        "pPacket[i].characterId = khdays_characters_place(i, Ov008_MenuSlotToEntry(pInfo->nMemberKind));",
    ),
]
# ... Mission Mode's camp: the chosen character's model (member 0's) and weapon texts from its own folder
HOOKS["ov008@0205a3dc"] = [
    (  # Ov008_Menu_InitSceneObject
        "extern char gOv008BaChDefHPackPathFmt[];\n",
        "extern char gOv008BaChDefHPackPathFmt[];\n" + CHARACTER_DECLARATIONS,
    ),
    (
        "    val = *(int *)(val + sceneId * 0x34);\n",
        "    val = khdays_characters_of(0, sceneId) != sceneId ? (int)Port_CharacterCode(khdays_characters_of(0, sceneId))\n"
        "                                                  : *(int *)(val + sceneId * 0x34);\n",
    ),
]
HOOKS["ov008@0205a138"] = [
    (  # Ov008_Menu_LoadSceneText
        "extern SceneParam data_ov008_0208e9c4[];\n",
        "extern SceneParam data_ov008_0208e9c4[];\n" + CHARACTER_DECLARATIONS,
    ),
    (
        "    v = data_ov008_0208e9c4[sceneId].f0;\n",
        "    v = khdays_characters_of(0, sceneId) != sceneId ? (int)Port_CharacterCode(khdays_characters_of(0, sceneId))\n"
        "                                                : data_ov008_0208e9c4[sceneId].f0;\n",
    ),
]
# ... the game's own select screens (scene 7's, the camp's): what they draw of a place on a new character -- the
# model's pose, the gauges, the names, the slots -- and whether it may be chosen are its base's place's
_MENU_ENTRY = "extern int khdays_characters_menu_entry(int id);\n"


def _menu(expression: str) -> str:
    return f"khdays_characters_menu_entry({expression})"


HOOKS["ov006@02050fc8"] += [  # Ov006_UpdateMissionMemberMenuScreen
    (
        "void *Ov006_UpdateMissionMemberMenuScreen(void)\n{\n",
        _MENU_ENTRY + "void *Ov006_UpdateMissionMemberMenuScreen(void)\n{\n",
    ),
    (
        "Ov006_ResolveMissionSelection(rows[cursorEntry].memberId)",
        "Ov006_ResolveMissionSelection(" + _menu("rows[cursorEntry].memberId") + ")",
    ),
    (
        "Ov006_MissionSetModelPose(rows[cursorEntry].memberId)",
        "Ov006_MissionSetModelPose(" + _menu("rows[cursorEntry].memberId") + ")",
    ),
    ("paramSel = rows[cursorEntry].memberId;", "paramSel = " + _menu("rows[cursorEntry].memberId") + ";"),
    (
        "Ov006_SetMissionRowSlotValue(visibleSlot, (u16)rowPtr->memberId,",
        "Ov006_SetMissionRowSlotValue(visibleSlot, (u16)" + _menu("rowPtr->memberId") + ",",
    ),
    (
        "rows[Ov006_MissionGetCursorEntry()].memberId + 0xb",
        _menu("rows[Ov006_MissionGetCursorEntry()].memberId") + " + 0xb",
    ),
    (
        "rows[Ov006_MissionGetCursorEntry()].memberId + 0x1f",
        _menu("rows[Ov006_MissionGetCursorEntry()].memberId") + " + 0x1f",
    ),
    (
        "Ov006_ResolveMissionSelection(rows[i].memberId)",
        "Ov006_ResolveMissionSelection(" + _menu("rows[i].memberId") + ")",
    ),
    ("rows[i].memberId + 0xb", _menu("rows[i].memberId") + " + 0xb"),
]
HOOKS["ov008@0207ce84"] += [  # Ov008_LobbyStep
    ("Ov008LobbyStep Ov008_LobbyStep(void)\n{\n", _MENU_ENTRY + "Ov008LobbyStep Ov008_LobbyStep(void)\n{\n"),
    (
        "Ov008_ResolveMissionSelection(aRow[nLocal].icon)",
        "Ov008_ResolveMissionSelection(" + _menu("aRow[nLocal].icon") + ")",
    ),
    ("Ov008_MissionSetModelPose(aRow[nLocal].icon)", "Ov008_MissionSetModelPose(" + _menu("aRow[nLocal].icon") + ")"),
    (
        "Ov008_MissionApplyParameterRow(nCursor != 0 ? aRow[nLocal].icon : ICON_NONE)",
        "Ov008_MissionApplyParameterRow(nCursor != 0 ? " + _menu("aRow[nLocal].icon") + " : ICON_NONE)",
    ),
    (
        "Ov008_SetMissionRowSlotValue(nSlot, (u16)aRow[i].icon,",
        "Ov008_SetMissionRowSlotValue(nSlot, (u16)" + _menu("aRow[i].icon") + ",",
    ),
    (
        "aRow[Ov008_GetLocalPlayerIndex()].icon + TEXT_ICON_BASE",
        _menu("aRow[Ov008_GetLocalPlayerIndex()].icon") + " + TEXT_ICON_BASE",
    ),
    (
        "aRow[Ov008_GetLocalPlayerIndex()].icon + TEXT_NAME_BASE",
        _menu("aRow[Ov008_GetLocalPlayerIndex()].icon") + " + TEXT_NAME_BASE",
    ),
    ("Ov008_ResolveMissionSelection(aRow[i].icon)", "Ov008_ResolveMissionSelection(" + _menu("aRow[i].icon") + ")"),
    ("aRow[i].icon + TEXT_ICON_BASE", _menu("aRow[i].icon") + " + TEXT_ICON_BASE"),
]
# ... and their input: a place on a new character is looked for on the DS's grid at its base's place, and keeps
# its own number unless a direction moves it on (A and B would take the grid's, and the screen asks for the
# place every frame: Ov006_MissionArmCursorRequest)
for ov, address, cursor, grid in (
    ("006", "02051b34", "    cursorEntry = Ov006_MissionGetCursorEntry();\n", "data_ov006_02056410"),
    ("008", "0207d888", "    cursorEntry = Ov008_GetLocalPlayerIndex();\n", "data_ov008_02090c00"),
):
    HOOKS[f"ov{ov}@{address}"] = [  # Ov00N_UpdateMissionMemberSelectionInput
        (
            cursor,
            cursor + "    extern int khdays_characters_known(int number);\n"
            "    extern int khdays_characters_menu_entry(int id);\n"
            "    int khdays_kept;\n",
        ),
        (
            "    if (members[selectionIndex].icon < 0) {\n",
            "    khdays_kept = khdays_characters_known(members[selectionIndex].icon & 0xff) ? members[selectionIndex].icon\n"
            "                                                                                : -1;\n"
            "    if (khdays_kept >= 0) {\n"
            "        members[selectionIndex].icon = (s8)khdays_characters_menu_entry(khdays_kept);\n"
            "    }\n"
            "    if (members[selectionIndex].icon < 0) {\n",
        ),
        (
            f"    members[selectionIndex].icon =\n        (s8){grid}[gridRow][gridColumn];\n",
            f"    members[selectionIndex].icon =\n        (s8){grid}[gridRow][gridColumn];\n"
            "    if (khdays_kept >= 0 && (action == 4 || action == 5)) {\n"
            "        members[selectionIndex].icon = (s8)khdays_kept;\n"
            "    }\n",
        ),
        (
            f"Ov{ov}_ResolveMissionSelection((s8)members[selectionIndex].icon)",
            f"Ov{ov}_ResolveMissionSelection(khdays_characters_menu_entry((s8)members[selectionIndex].icon))",
        ),
        (  # no input: the place as it was (its caller asks for it every frame)
            "    if (action == -1) {\n        return 0;\n    }\n",
            "    if (action == -1) {\n"
            "        if (khdays_kept >= 0) {\n"
            "            members[selectionIndex].icon = (s8)khdays_kept;\n"
            "        }\n"
            "        return 0;\n"
            "    }\n",
        ),
    ]
HOOKS["main@0202b914"] = [
    (  # TailForwardTrackEntry: a seat's model packs from its new character's own folder (a path under its base's;
        # never a packed handle, bit 31)
        "void TailForwardTrackEntry(int param_1, void *pArg2, int param_3, int param_4) {\n"
        "    int param_2 = (int)pArg2;\n",
        "extern int khdays_characters_of(int slot, int kind);\n"
        "extern int khdays_characters_base(int number);\n"
        "extern const char *const data_02042a70[];\n"
        "extern const char *Port_CharacterCode(int number);\n"
        "static char sPortCharacterPath[0x80];\n"
        "void TailForwardTrackEntry(int param_1, void *pArg2, int param_3, int param_4) {\n"
        "    int param_2 = (int)pArg2;\n"
        "    if (param_2 > 0 && param_1 >= 0 && param_1 < 4) {\n"
        "        int n = khdays_characters_of(param_1, -1);\n"
        "        if (n >= 0) {\n"
        "            const char *path = (const char *)pArg2;\n"
        "            const char *base = data_02042a70[khdays_characters_base(n)];\n"
        "            const char *code = Port_CharacterCode(n);\n"
        "            int b = 0, i, k = 0;\n"
        "            if (path[0] == 'b' && path[1] == 'a' && path[2] == '/' && path[3] == 'c' && path[4] == 'h' &&\n"
        "                path[5] == '/') {\n"
        "                while (base[b] != 0 && path[6 + b] == base[b]) b++;\n"
        "                if (base[b] == 0 && path[6 + b] == '/') {\n"
        "                    const char *rest = path + 6 + b;\n"
        "                    for (k = 0; k < 6; k++) sPortCharacterPath[k] = path[k];\n"
        "                    for (i = 0; code[i] != 0; i++) sPortCharacterPath[k++] = code[i];\n"
        "                    for (i = 0; rest[i] != 0 && k < (int)sizeof(sPortCharacterPath) - 1; i++)\n"
        "                        sPortCharacterPath[k++] = rest[i];\n"
        "                    sPortCharacterPath[k] = 0;\n"
        "                    param_2 = (int)sPortCharacterPath;\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "    }\n",
    )
]
# ... its voice: the sounds an actor starts are its seat's (the actor's +9, its character at +0xc), so that a new
# character's [sounds] replace the base's only for its own actor (runtime/sound/sound_mods.c)
_SPEAKER = "extern void khdays_characters_speaker(int seat, int kind);\n"
HOOKS["ov022@020a4490"] = [
    (  # Ov022_PlayEntityVoice: an actor's cues and voices
        "        nRet = Slot_Spawn(nFirst, nSecond, &pActor->vecPos, nFlags);\n",
        "        { " + _SPEAKER + "        khdays_characters_speaker(((u8 *)pActor)[9], pActor->nState);\n"
        "        nRet = Slot_Spawn(nFirst, nSecond, &pActor->vecPos, nFlags);\n"
        "        khdays_characters_speaker(-1, -1); }\n",
    )
]
HOOKS["ov022@020a23a4"] = [
    (  # Ov022_ScaleRowValues: the damage of a move a player overlay's own code makes (a Limit, a special)
        "    kind = *(unsigned char *)(obj + 9);\n",
        "    { extern int khdays_characters_own_moves(int seat, int kind, int factor);\n"
        "      factor = khdays_characters_own_moves(*(unsigned char *)(obj + 9), *(int *)(obj + 0xc), factor); }\n"
        "    kind = *(unsigned char *)(obj + 9);\n",
    )
]
HOOKS["ov022@020a5570"] = [
    (  # Ov022_RunComboContacts: the attack an actor's hits come from, for the plugins and KHDAYS_TRACE_CHARACTERS
        "    pRecord = pActor->pComboLock->pRecord;\n",
        "    pRecord = pActor->pComboLock->pRecord;\n"
        "    { extern void khdays_characters_attack(const void *actor, int seat, int kind, int attack);\n"
        "      khdays_characters_attack(pActor, pActor->nId, ((int *)pActor)[3], *(u16 *)pRecord); }\n",
    )
]
HOOKS["ov022@020a48c8"] = [
    (  # Ov022_SpawnVoiceCue: the cue of an actor's action
        "        Slot_Spawn(nHandlerId, nCue, pPos, nFlags);\n",
        "        { " + _SPEAKER + "        khdays_characters_speaker(pActor->nId, pActor->nModelId);\n"
        "        Slot_Spawn(nHandlerId, nCue, pPos, nFlags);\n"
        "        khdays_characters_speaker(-1, -1); }\n",
    )
]


# Hook points for the mods' native plugins (kh.hooks: sdk/include/kh/kh_hooks.h, port/runtime/plugins/interfaces.c).
# Each routine is renamed khdays_hooked_<name> behind a wrapper of its own name: while no plugin hooks the point,
# the wrapper calls it at once; while one does, it gives the routine's arguments to the plugins' before-functions
# (which may change them, or skip the routine giving its result), calls it, then gives its result to their
# after-functions. Arguments and results as 64-bit slots: integers widened ("int" sign-extended, "uint" not),
# the game's pointers as references ("ref"). A point's number is its place in this list; the port's catalogue is
# generated from it (khdays_hook_points.h). Points are added as plugins need them, across the game's subsystems.
HOOK_POINTS = [
    # (its file's key, its header as the decompilation writes it, its result's kind, its arguments' kinds)
    # combat and entities: an actor's HP set (a hit, a cure, a defeat)
    ("ov022@020acc94", "void Ov022_ActorSetHp(u32 *pEnt, int nValue)\n{", "void", ("ref", "int")),
    # combat: the damage of a player's attack (its strength times the hit's factor)
    ("ov022@020a1f84", "int Ov022_ComputeStrengthDamage(struct Actor *pActor, int nScale)\n{", "int", ("ref", "int")),
    # resources: a file of the game's loaded whole (its name, or a packed handle with bit 31)
    ("main@0201ef9c", "void *Archive_LoadFile(char *name, u32 kind)\n{", "ref", ("ref", "uint")),
    # audio: a sound effect started from the sound archive (its sequence archive, its entry)
    ("main@02033b78", "int PlaySound(int arg0, int arg1) {", "int", ("int", "int")),
    # combat and entities: an enemy takes a hit (its target, the hit's source, the hit; its damage worked out inside)
    (
        "ov107@020c5cfc",
        "int Ov107_AiState_ApplyHit(struct Obj *obj, struct HitSource *source, struct HitPacket *packet)\n{",
        "int",
        ("ref", "ref", "ref"),
    ),
    # audio: a positioned sound started (the field's: an actor's cues and voices; its handler, its entry, its place)
    (
        "main@02033d0c",
        "unsigned int Slot_Spawn(unsigned int param_1, unsigned int param_2, unsigned int *param_3, "
        "unsigned int param_4)\n{",
        "uint",
        ("uint", "uint", "ref", "uint"),
    ),
    # combat and entities: an actor enters a state by its number (an attack, a jump, a stagger, a Limit ...)
    ("ov022@020a35f4", "int Ov022_ActorSetState(u32 *pActor, int nState)\n{", "int", ("ref", "int")),
    # camera: the field's camera, once a frame
    ("ov002@0204f0a8", "void *Ov002_TickCamera(void)\n{", "ref", ()),
]


def hook_point_pair(index: int, header: str, result: str, kinds: tuple[str, ...]) -> tuple[str, str]:
    """The text replacing a hook point's header: the routine renamed behind its wrapper."""
    m = re.fullmatch(r"(?s)(.*?)\b([A-Za-z_]\w*)\((.*)\)(\s*)\{", header)
    if m is None:
        raise SystemExit(f"hook point: not a function's header: {header!r}")
    ret, name, params, gap = m[1], m[2], m[3], m[4]
    names = (
        [re.search(r"([A-Za-z_]\w*)\s*$", one)[1] for one in params.split(",")]
        if params.strip() not in ("", "void")
        else []
    )
    types = [one.strip()[: -len(n)].strip() for one, n in zip(params.split(","), names)]
    if len(names) != len(kinds):
        raise SystemExit(f"hook point {name}: {len(names)} arguments, {len(kinds)} kinds")
    inner = f"khdays_hooked_{name}"

    def slot(n: str, kind: str) -> str:
        return {"ref": f"(unsigned long long)(unsigned)({n})", "uint": f"(unsigned long long)({n})"}.get(
            kind, f"(unsigned long long)(long long)({n})"
        )

    def back(i: int, ctype: str, kind: str) -> str:
        return f"({ctype})(unsigned)khdays_args[{i}]" if kind == "ref" else f"({ctype})khdays_args[{i}]"

    plain_args = ", ".join(names)
    slot_args = ", ".join(back(i, ctype, kind) for i, (ctype, kind) in enumerate(zip(types, kinds)))
    fill = "".join(f"    khdays_args[{i}] = {slot(n, kind)};\n" for i, (n, kind) in enumerate(zip(names, kinds)))
    if result == "void":
        plain = f"        {inner}({plain_args});\n        return;\n"
        call = f"        {inner}({slot_args});\n"
        out = ""
    else:
        widen = "(unsigned long long)(unsigned)" if result == "ref" else "(unsigned long long)(long long)"
        plain = f"        return {inner}({plain_args});\n"
        call = f"        khdays_result = {widen}{inner}({slot_args});\n"
        narrow = f"({ret.strip()})(unsigned)khdays_result" if result == "ref" else f"({ret.strip()})khdays_result"
        out = f"    return {narrow};\n"
    return header, (
        f"static {ret}{inner}({params});\n"
        f"{ret}{name}({params}){gap}{{\n"
        "    extern int khdays_hook_on(int point);\n"
        "    extern int khdays_hook_before(int point, unsigned long long *args, unsigned long long *result);\n"
        "    extern void khdays_hook_after(int point, unsigned long long *args, unsigned long long *result);\n"
        f"    unsigned long long khdays_args[{max(1, len(names))}], khdays_result = 0;\n"
        f"    if (!khdays_hook_on({index})) {{\n{plain}    }}\n"
        f"{fill}"
        f"    if (!khdays_hook_before({index}, khdays_args, &khdays_result)) {{\n{call}    }}\n"
        f"    khdays_hook_after({index}, khdays_args, &khdays_result);\n"
        f"{out}"
        "}\n"
        f"static {ret}{inner}({params}){gap}{{"
    )


def hook_points_header() -> str:
    """The port's catalogue of the hook points (khdays_hook_points.h)."""
    lines = [
        "/* Generated by port/tools/prepare.py from HOOK_POINTS: the mods' native plugins' hook points, by number",
        " * (port/runtime/plugins/interfaces.c). */",
        "static const struct {",
        "    const char *name, *signature;",
        "    int args, returns;",
        "} khdays_hook_points[] = {",
    ]
    for _, header, result, kinds in HOOK_POINTS:
        m = re.fullmatch(r"(?s)(.*?)\b([A-Za-z_]\w*)\((.*)\)(\s*)\{", header)
        signature = f"{m[1]}{m[2]}({m[3]})".replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'    {{"{m[2]}", "{signature}", {len(kinds)}, {0 if result == "void" else 1}}},')
    lines += ["};", f"#define KHDAYS_HOOK_POINT_COUNT {len(HOOK_POINTS)}", ""]
    return "\n".join(lines)


for _index, (_key, _header, _result, _kinds) in enumerate(HOOK_POINTS):
    HOOKS.setdefault(_key, []).append(hook_point_pair(_index, _header, _result, _kinds))


# The routines whose drawing the port takes over: (function, part, the port's function, kind).
# "replace" runs the game's body (off), the port's (on) or both (check); "follow" runs the game's,
# then the port's with what it returned; "around" also observes the input. Parts: port/ui/parts.h; docs/BUILD.md.
UI_PARTS = {"text": 0, "hud": 1, "pause": 2, "camp": 3, "save": 4, "mission": 5, "shop": 6}
UI_HOOKS: dict[str, list[tuple[str, str, str, str]]] = {
    "src/overlays/scenes/ov002_field/Ov002_LinkPageRefresh.c": [
        ("Ov002_LinkPageRefresh", "text", "khdays_ui_text_field_tutorial", "around")
    ],
    # NitroSystem BG text operations. These are followed, never replaced:
    # original DS text logic and canvases remain authoritative.
    "libs/nns/g2d/calls/DrawGlyphLine.c": [("DrawGlyphLine", "text", "khdays_ui_text_glyph", "around")],
    "libs/nns/g2d/calls/ClearContinuous.c": [("ClearContinuous", "text", "khdays_ui_text_clear", "follow")],
    "libs/nns/g2d/calls/ClearAreaLine.c": [("ClearAreaLine", "text", "khdays_ui_text_clear_area", "follow")],
    "src/engine/TextWindow_ScrollUp.c": [("TextWindow_ScrollUp", "text", "khdays_ui_text_scroll", "around")],
    "libs/nns/g2d/calls/NNS_G2dFontInitUTF16.c": [
        ("NNS_G2dFontInitUTF16", "text", "khdays_ui_text_font_init", "follow")
    ],
    "src/engine/Font_LoadUTF16.c": [("Font_LoadUTF16", "text", "khdays_ui_text_font_loaded", "follow")],
    "src/engine/Resource_BindByName.c": [("Resource_BindByName", "text", "khdays_ui_text_font_bound", "follow")],
    # The text: the two text surfaces that make
    # NitroSystem character canvases, the frees that end them, and the SDK's
    # loads of characters into VRAM that show them (the transfer queue's,
    # data_02041934, and the routines' own: Ov008_DrawPageBRows,
    # Ov002_BuildTileRow, ...).
    "src/engine/TileTextRenderer_Init.c": [("TileTextRenderer_Init", "text", "khdays_ui_text_renderer", "follow")],
    "src/engine/TileTextRenderer_Destroy.c": [
        ("TileTextRenderer_Destroy", "text", "khdays_ui_text_renderer_gone", "follow")
    ],
    "src/engine/TileSurface_AddCanvas.c": [
        ("TileSurface_AddCanvas", "text", "khdays_ui_text_surface_canvas", "follow")
    ],
    # a canvas's buffer freed: the canvas is gone (ui/text.c khdays_ui_text_freed)
    "libs/nns/fnd/calls/NNS_FndFreeToExpHeap.c": [("NNS_FndFreeToExpHeap", "text", "khdays_ui_text_freed", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG0Char.c": [("GX_LoadBG0Char", "text", "khdays_ui_text_chars_a0", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG1Char.c": [("GX_LoadBG1Char", "text", "khdays_ui_text_chars_a1", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG2Char.c": [("GX_LoadBG2Char", "text", "khdays_ui_text_chars_a2", "follow")],
    "libs/nitro/gx/calls/GX_LoadBG3Char.c": [("GX_LoadBG3Char", "text", "khdays_ui_text_chars_a3", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG0Char.c": [("GXS_LoadBG0Char", "text", "khdays_ui_text_chars_b0", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG1Char.c": [("GXS_LoadBG1Char", "text", "khdays_ui_text_chars_b1", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG2Char.c": [("GXS_LoadBG2Char", "text", "khdays_ui_text_chars_b2", "follow")],
    "libs/nitro/gx/calls/GXS_LoadBG3Char.c": [("GXS_LoadBG3Char", "text", "khdays_ui_text_chars_b3", "follow")],
    "libs/nitro/gx/calls/GX_LoadOBJ.c": [("GX_LoadOBJ", "text", "khdays_ui_text_obj_a", "follow")],
    "libs/nitro/gx/calls/GXS_LoadOBJ.c": [("GXS_LoadOBJ", "text", "khdays_ui_text_obj_b", "follow")],
}

for _module, _prefix in (
    ("scenes/ov008_camp_menu", "Ov008"),
    ("screens/ov025_camp_menu_2", "Ov025"),
):
    for _function, _role in (
        ("DrawMissionSummaryHeading", "instruction"),
        ("MainMenu_UpdateSelectionText", "world"),
        ("DrawMissionDetail", "header"),
        ("MissionMenu_DrawMissionText", "summary"),
        ("MissionMenu_DrawInfo", "objectives"),
        ("DrawMissionInfoText", "rewards"),
    ):
        _name = f"{_prefix}_{_function}"
        UI_HOOKS[f"src/overlays/{_module}/{_name}.c"] = [(_name, "text", f"mission_select.{_role}", "scope")]


# Field HUD text shares fonts with dialogue; scope only layout/drawing operations.
for _function, _scope in (
    ("BeginTextCrawl", "field.hud.information"),
    ("StepTextCrawl", "field.hud.information"),
    ("HoldPanelScreen", "field.hud.information"),
    ("SetCaptionText", "field.hud.information"),
    ("BuildRunSummary", "field.hud.information"),
    ("DrawLoadedCaption", "field.hud.area"),
    ("DrawLoadedPortrait", "field.hud.status"),
    ("DrawLoadedTally", "field.hud.status"),
    ("DrawMessageCaption", "field.hud.status"),
    ("ShowNoticeText", "field.hud.status"),
    ("DrawPanelRowText", "field.command"),
    ("BuildTextItem", "field.breakthrough"),
):
    _name = f"Ov002_{_function}"
    UI_HOOKS[f"src/overlays/scenes/ov002_field/{_name}.c"] = [(_name, "text", _scope, "scope")]

for _function, _role in (("UpdateResultLabels", "summary"), ("RenderTextSurface", "rewards")):
    _name = f"Ov005_{_function}"
    UI_HOOKS[f"src/overlays/scenes/ov005_mission_result/{_name}.c"] = [
        (_name, "text", f"mission.results.{_role}", "scope")
    ]


for _function in ("DrawStringShadowed", "DrawNumber3Shadowed", "DrawDescriptionText", "OpenShopDetailPanel"):
    _name = f"Ov026_{_function}"
    UI_HOOKS[f"src/overlays/screens/ov026_shop/{_name}.c"] = [(_name, "text", "shop", "scope")]


def ui_hook(text: str, rel: str, name: str, part: str, port: str, kind: str = "replace") -> str:
    """`name`'s definition in `text` split in two: the game's body under a
    second name, static, and `name` asking `part`'s mode (UI_HOOKS)."""
    mask = abi_repair.code_mask(text)
    d = abi_repair.find_definition(text, mask, name)
    if d is None:
        raise SystemExit(f"UI hook: no definition of {name} in {rel}")
    head = text[d.ret_start : d.name_start]
    params = abi_repair.param_list("".join(ch if code else " " for ch, code in zip(text, mask)), d)
    names = [abi_repair.param_name(p) for p in params]
    if any(not n for n in names):
        raise SystemExit(f"UI hook: {name} in {rel} has an unnamed parameter")
    void = re.sub(r"\b(?:static|inline|extern)\b", "", head).strip() == "void"
    signature = text[d.name_start : d.params[1] + 1]
    args = ", ".join(names)
    result_type = re.sub(r"\b(?:static|inline|extern)\b", "", head).strip()
    param_text = text[d.params[0] : d.params[1]].strip()
    if kind == "scope":
        if not void:
            raise SystemExit("UI scope hooks require a void function")
        port_decl = (
            "extern void khdays_ui_text_scope_push(const char *scope);\nextern void khdays_ui_text_scope_pop(void);"
        )
    elif kind in ("follow", "around"):
        told = ([param_text] if param_text not in ("", "void") else []) + (
            [] if void else [f"{result_type} khdays_result"]
        )
        port_decl = f"extern void {port}({', '.join(told) or 'void'});"
    else:
        port_decl = f"extern {head.strip()} {port}({text[d.params[0] : d.params[1]]});"
    if kind == "around":
        if not void:
            raise SystemExit("UI around hooks require a void function")
        port_decl += f"\nextern void {port}_before({param_text});"
    game = (
        text[: d.ret_start]
        + port_decl
        + "\nextern int khdays_ui_mode(int part);\n"
        + "static "
        + re.sub(r"\b(?:static|inline)\b\s*", "", head)
        + name
        + "__khdays_game"
        + text[d.name_start + len(name) : d.body[1] + 1]
    )
    mode = f"    const int khdays_mode = khdays_ui_mode({UI_PARTS[part]});  /* the port's {part} (prepare.py UI_HOOKS) */\n"
    if kind == "scope":
        body = (
            f'\n{{\n{mode}    if (khdays_mode != 0) khdays_ui_text_scope_push("{port}");\n'
            f"    {name}__khdays_game({args});\n"
            "    if (khdays_mode != 0) khdays_ui_text_scope_pop();\n}"
        )
    elif kind == "around":
        body = (
            f"\n{{\n{mode}    if (khdays_mode != 0) {{\n        {port}_before({args});\n    }}\n"
            f"    {name}__khdays_game({args});\n"
            f"    if (khdays_mode != 0) {{\n        {port}({args});\n    }}\n}}"
        )
    elif kind == "follow" and void:
        body = (
            f"\n{{\n{mode}    {name}__khdays_game({args});\n"
            f"    if (khdays_mode != 0) {{\n        {port}({args});\n    }}\n}}"
        )
    elif kind == "follow":
        body = (
            f"\n{{\n{mode}    {result_type} khdays_result = {name}__khdays_game({args});\n"
            f"    if (khdays_mode != 0) {{\n        {port}({args + ', ' if args else ''}khdays_result);\n    }}\n"
            f"    return khdays_result;\n}}"
        )
    elif void:
        body = (
            f"\n{{\n    const int khdays_mode = khdays_ui_mode({UI_PARTS[part]});  /* the port's {part} (prepare.py UI_HOOKS) */\n"
            f"    if (khdays_mode != 0) {{\n        {port}({args});\n    }}\n"
            f"    if (khdays_mode != 2) {{\n        {name}__khdays_game({args});\n    }}\n}}"
        )
    else:
        body = (
            f"\n{{\n    const int khdays_mode = khdays_ui_mode({UI_PARTS[part]});  /* the port's {part} (prepare.py UI_HOOKS) */\n"
            f"    if (khdays_mode == 2) {{\n        return {port}({args});\n    }}\n"
            f"    if (khdays_mode != 0) {{\n        {port}({args});\n    }}\n"
            f"    return {name}__khdays_game({args});\n}}"
        )
    return game + "\n\n" + head + signature + body + text[d.body[1] + 1 :]


# The game's "am I the host" checks (the local player's index against 0) answer through the port
# (runtime/net/mp_predict.c khdays_mp_role): as a host's while a guest's own character is
# predicted, as the game's otherwise. "Is this my character" stays the game's call.
ROLE_QUERY = re.compile(
    r"Session_GetLocalPlayerIndex\(\)(?=\s*[!=]=\s*0\b)"
    r"|(?<=!)Session_GetLocalPlayerIndex\(\)"
    r"|(?<=if \()Session_GetLocalPlayerIndex\(\)(?=\))"
    r"|(?<=if\()Session_GetLocalPlayerIndex\(\)(?=\))"
)
# ... and the same check through a variable, as Zexion's parts make it
# (Ov045_StepParts and its three twins: `player = ...; if (player == 0 ...`).
ROLE_QUERY_VAR = re.compile(r"(\b\w+ = )Session_GetLocalPlayerIndex\(\)(;\s*if \(\w+ == 0\b)")
# Only a character's own run asks through the port: ov022's actors and the characters' overlays.
# What it does to the others and the world (hits, HP, items, tallies, the network) stays the
# game's answer, so a guest never makes what the host decides.
ROLE_DIRS = ("src/overlays/field/ov022_battle/", "src/overlays/players/")
ROLE_KEPT = {
    # hits delivered to others
    "Ov022_DeliverHit",
    "Ov022_SweepCapsuleOverEntries",
    "Ov022_SweepCapsuleOverGroup",
    "Ov022_SweepCapsuleOverParts",
    "Ov022_SweepFanOverEntries",
    "Ov022_SweepFanOverGroup",
    "Ov022_SweepFanOverParts",
    "Ov022_SendSweepHit",
    "Ov022_NotifyPeerHit",
    "Ov022_ReportHit",
    "Ov022_BroadcastCue",
    "func_ov022_02088398",
    # hits taken, HP, items, abilities
    "Ov022_ReceiveHit",
    "Ov022_ApplyDamageAndFlagHit",
    "Ov022_ApplyScaledDamage",
    "Ov022_ActorSetHp",
    "Ov022_FireReaction",
    "Ov022_ChargeGaugeForHit",
    "Ov022_SpendGuardOnHit",
    "Ov022_TryBlockHit",
    "Ov022_TryTriggerFinisher",
    "Ov022_Member_ShowDamage",
    "Ov022_Member_ShowSpotMessage",
    "Ov022_ApplyItemEffect",
    "func_ov022_02093900",
    "func_ov022_020888b8",
    "func_ov022_020888ec",
    # the mission's tallies, the shared tables, the members' events, the network, the battle's own loop
    "func_ov022_02088984",
    "func_ov022_0208a0b4",
    "func_ov022_0208a134",
    "Ov022_PublishRecord",
    "Ov022_PublishTableSlot",
    "Ov022_SendSyncRequest",
    "Ov022_StepSyncRequest",
    "Ov022_RefreshSlotRows",
    "Ov022_Party_ApplyNetState",
    "Ov022_TickSceneDispatch",
    "func_ov022_020897dc",
    "func_ov022_02089704",
    "Ov022_DispatchEntryByKind",
    "Ov022_SetupPauseMenu",
    "Ov022_InitActor",
    "Ov022_UpdateCommandInput",
}


def role_scoped(rel: str) -> bool:
    """Whether a file's "am I the host" checks answer through the port (ROLE_QUERY)."""
    path = rel.replace("\\", "/")
    return any(path.startswith(d) for d in ROLE_DIRS) and Path(rel).stem not in ROLE_KEPT


# The Menu Overhaul's (runtime/mods/installed.h): each port function the game calls here answers as the
# game would without that mod. The camps' menus keep their cursor between visits (runtime/menus/camp.c
# khdays_camp_remember_entry, khdays_camp_restore_entry), the story's menu hovers its entry as it starts.
HOOKS["ov025@0208b7d0"] = [  # Ov025_Hub_InitEntries
    (
        "    pScene->nHoverId = Ov025_UpdateMenuOptionLocksAndDrawCategory(pScene);",
        "    { extern int khdays_camp_restore_entry(int story, int fallback);\n"
        "      pScene->nHoverId = khdays_camp_restore_entry(1, Ov025_UpdateMenuOptionLocksAndDrawCategory(pScene)); }",
    ),
    (
        "    Ov025_ReleaseTwoSlotsEx(nCtx, pCursor, &pos);",
        "    Ov025_ReleaseTwoSlotsEx(nCtx, pCursor, &pos);\n"
        "    { extern void Ov025_Hub_OnEntryHover(Ov008MenuEntry *entry, u32 keys);\n"
        "      extern int khdays_camp_hover_at_start(void);\n"
        "      if (khdays_camp_hover_at_start()) { pScene->nHoverId = -1; Ov025_Hub_OnEntryHover(pEntry, 0xf0); } }",
    ),
]
HOOKS["ov025@0208bf68"] = [  # Ov025_Hub_OnEntryHover
    (
        "    pScene->nHoverId = pEntry->nId;",
        "    pScene->nHoverId = pEntry->nId;\n"
        "    { extern void khdays_camp_remember_entry(int story, int entry);\n"
        "      khdays_camp_remember_entry(1, pEntry->nId); }",
    )
]
HOOKS["ov008@02059698"] = [  # Ov008_HandleMenuEntrySelection
    (
        "    *(int *)(renderer + 0x88) = selectedEntry->value0c;",
        "    *(int *)(renderer + 0x88) = selectedEntry->value0c;\n"
        "    { extern void khdays_camp_remember_entry(int story, int entry);\n"
        "      khdays_camp_remember_entry(0, selectedEntry->value0c); }",
    )
]
HOOKS["ov008@02057f58"] = [  # Ov008_Menu_RefreshSubitemGrid
    (
        "    if (((Flags *)(data_ov008_02090f1c + 0x5c6))->b5 == 0) {\n        e = Ov008_FindEntryById(ui, 9);",
        "    { extern int khdays_camp_restore_entry(int story, int fallback);\n"
        "      unsigned int saved = khdays_camp_restore_entry(0, (int)uVar13);\n"
        "      for (i = 0; i < 7; ++i) if (values[i] == saved && flags[i] == 0) uVar13 = saved; }\n"
        "    if (((Flags *)(data_ov008_02090f1c + 0x5c6))->b5 == 0) {\n        e = Ov008_FindEntryById(ui, 9);",
    )
]
# The story's Panels page's line (runtime/menus/camp.c khdays_camp_text_row): the text the lower screen's box
# shows, for the port to show it in its own band.
HOOKS["ov025@02091070"] = [  # Ov025_RepaintTextRow
    (
        "    EnqueueObjGfxCommand(self->animator);\n}",
        "    EnqueueObjGfxCommand(self->animator);\n"
        "    { extern void khdays_camp_text_row(unsigned int self, int row, unsigned int text, int style);\n"
        "      khdays_camp_text_row((unsigned int)self, row, (unsigned int)text, style); }\n}",
    )
]
# Mission Mode's Panels page the same (ov008: the same grid menu under its own names).
HOOKS["ov008@0205eafc"] = [  # Ov008_RepaintTextRow
    (
        "    EnqueueObjGfxCommand(self->animator);\n}",
        "    EnqueueObjGfxCommand(self->animator);\n"
        "    { extern void khdays_camp_text_row(unsigned int self, int row, unsigned int text, int style);\n"
        "      khdays_camp_text_row((unsigned int)self, row, (unsigned int)text, style); }\n}",
    )
]
# The Panels page's preview of a panel held (Ov025/Ov008_PreviewDropSummary): the DS's up and down arrows beside
# the upper screen's numbers left out while the port draws the page (its status card shows the changes; camp.c
# khdays_camp_preview_arrows, with khdays_camp_field the changes' words).
HOOKS["ov025@020a42b8"] = [  # Ov025_DrawPageBWidget
    (
        "        nSelection = Ov025_SetPairSelection(pPage, &pairs.aPair[nPair], nArg);\n",
        "        { extern int khdays_camp_preview_arrows(int delta);\n"
        "          nSelection = Ov025_SetPairSelection(pPage, &pairs.aPair[nPair], khdays_camp_preview_arrows(nArg)); }\n",
    )
]
HOOKS["ov008@0206ec2c"] = [  # Ov008_DrawPageBWidget
    (
        "        nSelection = Ov008_SetPairSelection(pPage, &pairs.aPair[nPair], nArg);\n",
        "        { extern int khdays_camp_preview_arrows(int delta);\n"
        "          nSelection = Ov008_SetPairSelection(pPage, &pairs.aPair[nPair], khdays_camp_preview_arrows(nArg)); }\n",
    )
]
# Preferences' steering (runtime/menus/camp.c khdays_camp_navigation_sound): the port's presses on the way to a
# setting make no sound of their own.
HOOKS["main@02033b78"] = [  # PlaySound
    (
        "int PlaySound(int arg0, int arg1) {",
        "int PlaySound(int arg0, int arg1) {\n"
        "    extern int khdays_camp_navigation_sound(int kind, int sound);\n"
        "    if (!khdays_camp_navigation_sound(arg0, arg1)) return 0;",
    )
]
# The day's monologue in the theater's voice with its subtitles (runtime/movies/scene_trace.c): the DS's words
# and the page's arrow left out (Roxas's picture, BG1, stays), A not turning the page while the voice speaks,
# START taken by the window's pause, and the monologue left once the voice has said it all.
HOOKS["ov007@0204cac0"] = [  # Ov007_SetupSubDisplay
    (
        "        reg_dispcnt[0] = reg_dispcnt[0] & ~0x1f00 | 0x1600;",
        "        { extern int khdays_monologue_words_hidden(void);\n"
        "          reg_dispcnt[0] = reg_dispcnt[0] & ~0x1f00 | (khdays_monologue_words_hidden() ? 0x200 : 0x1600); }",
    )
]
_MONOLOGUE_EXTERNS = (
    "extern unsigned short gPadPressed;",
    "extern unsigned short gPadPressed;\n"
    "extern int khdays_monologue_next(int pressed);\n"
    "extern int khdays_monologue_leave(int pressed);",
)
_MONOLOGUE_NEXT = ("    if ((gPadPressed & 1) != 0) {", "    if (khdays_monologue_next((gPadPressed & 1) != 0)) {")
_MONOLOGUE_LEAVE = ("    if ((gPadPressed & 8) != 0) {", "    if (khdays_monologue_leave((gPadPressed & 8) != 0)) {")
HOOKS["ov007@0204cf7c"] = [_MONOLOGUE_EXTERNS, _MONOLOGUE_NEXT, _MONOLOGUE_LEAVE]  # Ov007_TextWindowFadeIn
HOOKS["ov007@0204d170"] = [_MONOLOGUE_EXTERNS, _MONOLOGUE_NEXT, _MONOLOGUE_LEAVE]  # Ov007_ScrollTextWindow
HOOKS["ov007@0204d23c"] = [_MONOLOGUE_EXTERNS, _MONOLOGUE_LEAVE]  # Ov007_FadeBackdropStep
HOOKS["ov007@0204d0ec"] = [_MONOLOGUE_EXTERNS, _MONOLOGUE_LEAVE]  # Ov007_AdvanceTextLine
# The game over's own firefly left out while the window draws its menu as KH2's pause, with KH2's firefly
# (runtime/menus/retry.c khdays_game_over_hides_orb).
HOOKS["ov027@0208344c"].append(  # Ov027_UpdateFlushForward
    (
        "    Ov002_ResetViewToDefault();\n",
        "    Ov002_ResetViewToDefault();\n"
        "    { extern int khdays_game_over_hides_orb(void); if (khdays_game_over_hides_orb()) return; }\n",
    )
)

# The Walk mod's (runtime/input/walk.h): the local player's model drawn with its walk, each joint the model sends
# taken from the walk's clip over the game's own result and given back after (the result is the motion's cache).
# Without the mod no clip is read and both answer at once.
HOOKS["main@0202aa9c"] = [  # Scene_DrawNode
    (
        "    NNS_G3dDraw(node->object);",
        "    { extern void khdays_walk_draw_begin(int node);\n"
        "      extern void khdays_walk_draw_end(void);\n"
        "      khdays_walk_draw_begin((int)node);\n"
        "      NNS_G3dDraw(node->object);\n"
        "      khdays_walk_draw_end(); }",
    )
]
HOOKS["itcm@01ffc164"].append(  # NNSi_G3dFuncSbc_NODEDESC
    (
        "        state->sendJnt(result);",
        "        { extern void khdays_walk_send_begin(unsigned int joint, void *result);\n"
        "          extern void khdays_walk_send_end(void *result);\n"
        "          khdays_walk_send_begin(nodeId, result);\n"
        "          state->sendJnt(result);\n"
        "          khdays_walk_send_end(result); }",
    )
)


# Hooks by the decomp file, for functions HOOKS cannot name: DS Protect's (ov028) are data in the
# symbols, their bodies encrypted in the ROM, and several share a file. Applied like HOOKS.
FILE_HOOKS = {
    # DS Protect's own read of the card (ROMUtil_Read, for its flashcart check): each page comes
    # from the port as a cartridge answers it, the card's registers left alone.
    "src/overlays/system/ov028_dsprotect/dsprot/dsprot_rom_util.c": [
        (
            "    DSPROT_RANGE_BEGIN(KEY_ROM_UTIL_READ)\n\n    registerBase = 1;\n",
            "    DSPROT_RANGE_BEGIN(KEY_ROM_UTIL_READ)\n"
            "\n"
            "    {\n"
            "        extern int khdays_files_mode(void);\n"
            "        extern void khdays_files_card_page(u32 address, u8 *page);\n"
            "        if (khdays_files_mode() == 0) {\n"
            "            u8 page[CARD_ROM_PAGE_SIZE];\n"
            "            offset = 0 - (addr & (CARD_ROM_PAGE_SIZE - 1));\n"
            "            addr += offset;\n"
            "            while (offset < size) {\n"
            "                khdays_files_card_page(addr, page);\n"
            "                for (i = 0; i < CARD_ROM_PAGE_SIZE; i += 4) {\n"
            "                    data = page[i] | page[i + 1] << 8 | page[i + 2] << 16 | (u32)page[i + 3] << 24;\n"
            "                    if (offset >= 0 && offset < size) {\n"
            "                        *(u32 *)((u32)dest + offset) = data;\n"
            "                    }\n"
            "                    offset += 4;\n"
            "                }\n"
            "                addr += CARD_ROM_PAGE_SIZE;\n"
            "            }\n"
            "            goto khdays_read_done;\n"
            "        }\n"
            "    }\n"
            "    registerBase = 1;\n",
        ),
        (
            "    *(vu16 *)(registerBase + REG_EXMEMCNT_OFFSET) = exmemOriginal;\n"
            "\n"
            "    DSPROT_RANGE_END(KEY_ROM_UTIL_READ)\n",
            "    *(vu16 *)(registerBase + REG_EXMEMCNT_OFFSET) = exmemOriginal;\n"
            "\n"
            "khdays_read_done:\n"
            "    DSPROT_RANGE_END(KEY_ROM_UTIL_READ)\n",
        ),
    ],
    # A DMA channel's three registers, in the SDK's order (source, then
    # destination and control through a pointer into the channel): through
    # the register model, whose control write runs the transfer (the
    # struct's element and field forms are not registers.py's).
    "libs/nitro/mi/auto/MIi_DmaSetChannelRegs.c": [
        (
            "    struct DmaChannel *p = (struct DmaChannel *)0x40000b0;\n"
            "    volatile unsigned int *q = &p[ch].dst;\n"
            "    p[ch].src = src;\n"
            "    q[0] = dst;\n"
            "    q[1] = ctrl;\n",
            "    khdays_io_store(0x040000b0u + (unsigned int)ch * sizeof(struct DmaChannel), 4, src);\n"
            "    khdays_io_store(0x040000b4u + (unsigned int)ch * sizeof(struct DmaChannel), 4, dst);\n"
            "    khdays_io_store(0x040000b8u + (unsigned int)ch * sizeof(struct DmaChannel), 4, ctrl);\n",
        )
    ],
    # The card's data port (reg_CARD_DATA, 0x0200ecc0 `ldr` through its
    # address), read through the register model. Only KHDAYS_FILES=card
    # reads the card this way.
    "libs/nitro/card/calls/CARDi_ReadCard.c": [
        (
            "                    u32 data = reg_CARD_DATA;\n",
            "                    u32 data = khdays_io_load(0x04100010u, 4); /* the card's data port */\n",
        )
    ],
    # An 8-bit character's two words' masks: shifts by a register (0x020139a0
    # `lsr r1`, 0x020139a8 `lsl r1`, 0x020139c4 `lsl r3`, 0x020139cc `lsr r3`)
    # whose count is 32 for an area starting, or ending, at the character's
    # fifth pixel. The ARM gives 0 for a shift by 32 or more; x64 takes the
    # count modulo 32, which would clear the other four pixels too.
    "libs/nns/g2d/calls/ClearChar.c": [
        (
            "                mask_0 = (u32)(~0) >> x8;\n",
            "                mask_0 = x8 < 32 ? (u32)(~0) >> x8 : 0;  /* the ARM's shift by 32 (prepare.py) */\n",
        ),
        (
            "                    mask_0 <<= x8;\n",
            "                    mask_0 = x8 < 32 ? mask_0 << x8 : 0;\n",
        ),
        (
            "                mask_1 = (u32)(~0) << rw8;\n",
            "                mask_1 = rw8 < 32 ? (u32)(~0) << rw8 : 0;\n",
        ),
        (
            "                    mask_1 >>= rw8;\n",
            "                    mask_1 = rw8 < 32 ? mask_1 >> rw8 : 0;\n",
        ),
    ],
}

FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_SetCaptionText.c"] = [
    (
        "void Ov002_SetCaptionText(const u16 *pText)\n{",
        "void Ov002_SetCaptionText(const u16 *pText)\n{\n"
        "    extern void khdays_ui_information_message(const unsigned short *text);\n"
        "    khdays_ui_information_message(pText);",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_RequestPanelScreen.c"] = [
    (
        "    Ov002_StartBlendFade(-1, 0, 300);",
        "    { extern int khdays_ui_information_keep_hud(void);\n"
        "      if (!khdays_ui_information_keep_hud()) Ov002_StartBlendFade(-1, 0, 300); }",
    ),
    (
        "    StrCopy16(ctx->pCrawlText, pText);",
        "    StrCopy16(ctx->pCrawlText, pText);\n"
        "    { extern void khdays_ui_information_panel(const unsigned short *text);\n"
        "      khdays_ui_information_panel(pText); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_FinishPanelFadeIn.c"] = [
    (
        "        Ov002_PopMapSnapshot();",
        "        { extern void khdays_ui_information_pop_maps(void);\n          khdays_ui_information_pop_maps(); }",
    ),
    (
        "        Ov002_StartBlendFade(0, 0x10, 300);",
        "        { extern int khdays_ui_information_keep_hud(void);\n"
        "          if (!khdays_ui_information_keep_hud()) Ov002_StartBlendFade(0, 0x10, 300); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_ResetFaders.c"] = [
    (
        "    Ov002_PushMapSnapshot(0);",
        "    { extern void khdays_ui_information_push_maps(void);\n      khdays_ui_information_push_maps(); }",
    ),
    (
        "    Ov002_Ctx_InvokeTagTrackerCallback(Ov002_ForwardToSubDc(0x50));",
        "    { extern int khdays_ui_information_keep_hud(void);\n"
        "      if (!khdays_ui_information_keep_hud())\n"
        "          Ov002_Ctx_InvokeTagTrackerCallback(Ov002_ForwardToSubDc(0x50)); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_PushMapSnapshot.c"] = [
    (
        "    for (i = 0; i <= 2; i++) {",
        "    for (i = 0; i <= 2; i++) {\n"
        "        extern int khdays_ui_information_preserving_maps(void);\n"
        "        if (khdays_ui_information_preserving_maps()) continue;",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_SetPanelMode.c"] = [
    (
        "        pOfs[0] = 0x180000;",
        "        { extern int khdays_ui_information_keep_hud(void);\n"
        "          if (nMode != 0 || !khdays_ui_information_keep_hud()) pOfs[0] = 0x180000; }",
    ),
    (
        "        pOfs[2] = 0x180000;",
        "        { extern int khdays_ui_information_keep_hud(void);\n"
        "          if (!khdays_ui_information_keep_hud()) pOfs[2] = 0x180000; }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_SetBgOfs_main001c.c"] = [
    (
        "void Ov002_SetBgOfs_main001c(unsigned int param_1, unsigned int param_2) {",
        "void Ov002_SetBgOfs_main001c(unsigned int param_1, unsigned int param_2) {\n"
        "    { extern int khdays_ui_information_keep_hud(void);\n"
        "      if (khdays_ui_information_keep_hud()) return; }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_SetBgOfs_main0014.c"] = [
    (
        "void Ov002_SetBgOfs_main0014(unsigned int param_1, unsigned int param_2) {",
        "void Ov002_SetBgOfs_main0014(unsigned int param_1, unsigned int param_2) {\n"
        "    { extern int khdays_ui_information_capture_scroll(unsigned int x, unsigned int y);\n"
        "      if (khdays_ui_information_capture_scroll(param_1, param_2)) return; }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_BeginTextCrawl.c"] = [
    (
        "    ctx->nCrawlPenX = 8;",
        "    ctx->nCrawlPenX = 8;\n"
        "    { extern void khdays_ui_information_crawl(const unsigned short *text);\n"
        "      khdays_ui_information_crawl((const unsigned short *)ctx->pCrawlText); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_OpenPanelScreen.c"] = [
    (
        "    MI_CpuFill8(ctx, 0, 0x2c4);",
        "    MI_CpuFill8(ctx, 0, 0x2c4);\n"
        "    { extern void khdays_ui_information_reset(void);\n"
        "      khdays_ui_information_reset(); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_ClosePanel.c"] = [
    (
        "    ctx->nPanelState = 0;",
        "    ctx->nPanelState = 0;\n"
        "    { extern void khdays_ui_information_panel(const unsigned short *text);\n"
        "      khdays_ui_information_panel(0); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_StartPanelFadeIn.c"] = [
    (
        "    char *ctx = data_ov002_0207f614;",
        "    char *ctx = data_ov002_0207f614;\n"
        "    { extern void khdays_ui_information_panel(const unsigned short *text);\n"
        "      khdays_ui_information_panel(0); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_ClosePanelScreen.c"] = [
    (
        "    data_ov002_0207f614 = 0;",
        "    data_ov002_0207f614 = 0;\n"
        "    { extern void khdays_ui_information_reset(void);\n"
        "      khdays_ui_information_reset(); }",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_HoldPanelScreen.c"] = [
    (
        "    ctx->nHeld = nHold;",
        "    ctx->nHeld = nHold;\n"
        "    { extern void khdays_ui_information_hold(int held, int leaving);\n"
        "      khdays_ui_information_hold(nHold, bLeaving); }",
    ),
]

# Capture the minimap's icon stamps before they are flattened into its 4bpp layer.
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_RedrawMapPanel.c"] = [
    (
        "    pScene = data_ov002_0207f638;",
        "    extern void khdays_minimap_begin(void);\n    extern void khdays_minimap_end(void);\n    khdays_minimap_begin();\n    pScene = data_ov002_0207f638;",
    ),
    (
        "    GFXi_EnqueueCommand(0x16, 0x2720, pScene->pTiles, 0x12c0);",
        "    khdays_minimap_end();\n    GFXi_EnqueueCommand(0x16, 0x2720, pScene->pTiles, 0x12c0);",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_PlotMapIcon.c"] = [
    (
        "    pCtx = data_ov002_0207f638;",
        "    extern void khdays_minimap_icon(int kind, int x, int y);\n    khdays_minimap_icon(nKind, pCell[0], pCell[1]);\n    pCtx = data_ov002_0207f638;",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_PlotPageGlyph.c"] = [
    (
        "    nBand = ((nHeight + 0x1000) / 0x2000) % 8;",
        "    extern void khdays_minimap_player(int player, int x, int y, int heading);\n    khdays_minimap_player(nSelf, pCell->x, pCell->y, nHeight);\n    nBand = ((nHeight + 0x1000) / 0x2000) % 8;",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_PlotPageEmblem.c"] = [
    (
        "    bMask = data_ov002_0207e400[nSelf];",
        "    extern void khdays_minimap_emblem(int player);\n    khdays_minimap_emblem(nSelf);\n    bMask = data_ov002_0207e400[nSelf];",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_StampCellBlock.c"] = [
    (
        "    for (i = 0; i < 8; i++) {",
        "    { extern void khdays_minimap_stamp(unsigned source, int x, int y);\n      khdays_minimap_stamp((unsigned)pCells, x, y); }\n    for (i = 0; i < 8; i++) {",
    ),
]
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_TeardownHudNode.c"] = [
    (
        "    int node = (int)data_ov002_0207f638;",
        "    extern void khdays_minimap_reset(void);\n    int node = (int)data_ov002_0207f638;\n    khdays_minimap_reset();",
    ),
]

# The portrait label is measured and drawn here, before dialogue window layout.
# Selection art only: keep projection, target choice, tween state and all native fallback draws.
HOOKS["ov022@02085380"] += [
    (
        "    u8 runtimeMode = data_0204be04;\n",
        "    u8 runtimeMode = data_0204be04;\n"
        "    extern void khdays_reticles_begin(void);\n"
        "    extern void khdays_reticles_end(int cancel);\n"
        "    if (runtimeMode == 0) khdays_reticles_begin();\n",
    ),
    (
        "        return suppressInput;",
        "        if (runtimeMode == 0) khdays_reticles_end(1);\n        return suppressInput;",
    ),
    ("return 0;", "if (runtimeMode == 0) khdays_reticles_end(1);\n    return 0;"),
    ("    Ov022_UpdateSelectionMarker();\n", "    Ov022_UpdateSelectionMarker();\n    khdays_reticles_end(0);\n"),
]
FILE_HOOKS["src/overlays/field/ov022_battle/Ov022_UpdateSelectionMarker.c"] = [
    (
        "    void *target = 0;\n",
        "    void *target = 0;\n"
        "    extern int khdays_reticles_saved_world(int kind, int *world);\n"
        "    extern void khdays_reticles_projected(int kind, int visible, int x, int y);\n"
        "    extern void khdays_reticles_target(const int *world);\n"
        "    /* Use the world camera before native marker drawing switches to orthographic. */\n"
        "    for (int kind = 0; kind < 5; ++kind) {\n"
        "        int world[3];\n"
        "        if (khdays_reticles_saved_world(kind, world)) {\n"
        "            Ov022SelectionPoint projected;\n"
        "            int visible = Ov022_ProjectToScreen(world, &projected) != -1;\n"
        "            khdays_reticles_projected(kind, visible, projected.x, projected.y);\n"
        "        }\n"
        "    }\n",
    ),
    (
        "    if (Ov022_ProjectToScreen(target, &point) == -1)",
        "    khdays_reticles_target((const int *)target);\n    if (Ov022_ProjectToScreen(target, &point) == -1)",
    ),
]
FILE_HOOKS["src/overlays/field/ov022_battle/Ov022_DrawSelectionMarkers.c"] = [
    (
        "    Ov002_ResetViewToDefault();\n    Ov002_ResourceNodeCallback(&group->records[0]);",
        "    Ov002_ResetViewToDefault();\n"
        "    extern int khdays_reticles_mark(int kind, unsigned identity, unsigned sprite, int x, int y);\n"
        "    int khdays_replaced = khdays_reticles_mark(enabled ? 1 : 0, (unsigned)context->focus,\n"
        "        (unsigned)&group->records[0], point->x, point->y);\n"
        "    if (!khdays_replaced) Ov002_ResourceNodeCallback(&group->records[0]);",
    ),
    (
        "    Ov002_ResourceNodeCallback(&group->records[1]);",
        "    if (!khdays_replaced) Ov002_ResourceNodeCallback(&group->records[1]);",
    ),
    (
        "            Ov002_ResourceNodeCallback(&group->records[2]);",
        "            if (!khdays_reticles_mark(3, (unsigned)context->focus, (unsigned)&group->records[2],\n"
        "                    point->x, point->y)) Ov002_ResourceNodeCallback(&group->records[2]);",
    ),
]
FILE_HOOKS["src/overlays/field/ov022_battle/Ov022_UpdateUiSelectionMarker.c"] = [
    (
        "    Ov002_ResetViewToDefault();\n    switch (subsystem->state)",
        "    Ov002_ResetViewToDefault();\n"
        "    extern int khdays_reticles_mark(int kind, unsigned identity, unsigned sprite, int x, int y);\n"
        "    int khdays_replaced = 0;\n"
        "    if (subsystem->state == 1 || subsystem->state == 2)\n"
        "        khdays_replaced = khdays_reticles_mark(\n"
        "            (unsigned)subsystem->displayObject == (unsigned)subsystem + 0xf0 ? 4 : 2,\n"
        "            *(unsigned *)((char *)subsystem + 0x128), (unsigned)subsystem->displayObject, point->x, point->y);\n"
        "    switch (subsystem->state)",
    ),
    (
        "        Ov022_PlaceCornerMarkers(subsystem, value, point);",
        "        if (!khdays_replaced) Ov022_PlaceCornerMarkers(subsystem, value, point);",
    ),
    (
        "        Ov002_ResourceNodeCallback(subsystem->displayObject);",
        "        if (!khdays_replaced) Ov002_ResourceNodeCallback(subsystem->displayObject);",
    ),
]

FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_SceneApplyPanelRequest.c"] = [
    (
        "void Ov002_SceneApplyPanelRequest(int *pReq)",
        "extern void khdays_ui_text_scope_push(const char *scope);\n"
        "extern void khdays_ui_text_scope_pop(void);\n"
        "void Ov002_SceneApplyPanelRequest(int *pReq)",
    ),
    (
        "        Resource_BindByName(f.aBind, gOv002TextFontEu10SPath);",
        '        khdays_ui_text_scope_push("field.dialogue.speaker");\n'
        "        Resource_BindByName(f.aBind, gOv002TextFontEu10SPath);",
    ),
    (
        "        FreeFieldAt8(f.aBind);",
        "        FreeFieldAt8(f.aBind);\n        khdays_ui_text_scope_pop();",
    ),
]

# The pickup banner aliases the Items list's character tiles. Draw its routed
# name into a private, non-uploaded surface; text.c retains that capture only.
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_PanelDrawSlotCursor.c"] = [
    (
        '#include "nitro/types.h"',
        '#include "nitro/types.h"\n'
        "extern int khdays_ui_text_obtained_wants(unsigned int slot, int obtained);\n"
        "extern void khdays_ui_text_obtained_capture(const void *surface);\n"
        "extern void khdays_ui_text_scope_push(const char *scope);\n"
        "extern void khdays_ui_text_scope_pop(void);\n"
        "extern int khdays_ui_mode(int part);\n"
        "extern char *data_ov002_0207f620;\n"
        "extern char gOv002TextFontEu08SPath_2[];\n"
        "extern int data_ov002_0207eb08[];\n"
        "extern int Ov002_Hud_GetBlock24(void);\n"
        "extern void *Ov002_FindNamedEntry(int key);\n"
        "extern void Resource_BindByName(void *handle, void *name);\n"
        "extern void FreeFieldAt8(char *handle);\n"
        "extern int TileSurface_Init4bpp(int surface, int config);\n"
        "extern void Ov002_PickTextFitting64(int surface, int font, int narrow, int text);\n"
        "extern void Text_DrawWithShadow(void *surface, int x, int y, int style, void *text, int shadow);\n"
        "extern void HD_FontFitShadow(void *surface, int x, int y, int style, void *text, int shadow, int layout);\n"
        "extern void FreeAllListNodeSubBuffers(void *surface);",
    ),
    (
        "    Ov002_SelectEntry(9);\n}",
        "    Ov002_SelectEntry(9);\n"
        "    if (khdays_ui_mode(0) != 0 && khdays_ui_text_obtained_wants(nSlot, nShift == -2)) {\n"
        "        int surface[15];\n"
        "        int narrow[3];\n"
        "        int font = Ov002_Hud_GetBlock24();\n"
        "        int config[10] = {0, 0, 8, 2, 0, 15, 0, 0, font, 0};\n"
        "        void *text = Ov002_FindNamedEntry(*(u16 *)(data_ov002_0207f620 + 0x4b8 + nSlot * 12));\n"
        "        if (text == 0) text = data_ov002_0207eb08;\n"
        '        khdays_ui_text_scope_push("field.obtained");\n'
        "        Resource_BindByName(narrow, gOv002TextFontEu08SPath_2);\n"
        "        TileSurface_Init4bpp((int)surface, (int)config);\n"
        "        Ov002_PickTextFitting64((int)surface, font, (int)narrow, (int)text);\n"
        "        HD_FontFitShadow(surface, 0, 3, 2, text, 1, 1);\n"
        "        khdays_ui_text_obtained_capture(surface);\n"
        "        FreeAllListNodeSubBuffers(surface);\n"
        "        FreeFieldAt8((char *)narrow);\n"
        "        khdays_ui_text_scope_pop();\n"
        "    }\n}",
    ),
]

# Observe layout placement without changing the native tilemap or gameplay callbacks.
FILE_HOOKS["src/overlays/scenes/ov002_field/Ov002_BlitTileRegion.c"] = [
    (
        "void Ov002_BlitTileRegion(",
        "extern void khdays_ui_notification_layout(const void *request, int shown);\nvoid Ov002_BlitTileRegion(",
    ),
    (
        "    Ov002_GetItemFlag(pReq->nDest);",
        "    khdays_ui_notification_layout(pReq, nFlag);\n    Ov002_GetItemFlag(pReq->nDest);",
    ),
]

# New games start with Config's "Command list" (option 6) on X with the D-pad, not X alone, so the
# command menu moves in all four directions (runtime/input/sequencers.c). Every place that makes a
# fresh game state sets it, Mission Mode's guest game too (a whole state copied from /ttl/ttl.p2).
# Saved games keep their own.
_X_ALONE = "GameState_SetField(CONFIG_COMMAND_LIST, 1, 1);"
_X_WITH_DPAD = "GameState_SetField(CONFIG_COMMAND_LIST, 1, 0); /* the port's: X with the D-pad */"
_COMMAND_LIST = [
    ("src/overlays/scenes/ov000_title/Ov000_SetupWorkArea.c", _X_ALONE, _X_WITH_DPAD),
    ("src/overlays/scenes/ov008_camp_menu/Ov008_SetupWorkArea.c", _X_ALONE, _X_WITH_DPAD),
    ("src/overlays/scenes/ov009_camp_save/Ov009_SetupWorkArea.c", _X_ALONE, _X_WITH_DPAD),
    ("src/overlays/screens/ov025_camp_menu_2/Ov025_SetupWorkArea.c", _X_ALONE, _X_WITH_DPAD),
    (
        "src/overlays/scenes/ov000_title/Ov000_HandleLoadConfirm.c",
        "GameState_SetFlag(0x200b);",
        "GameState_SetFlag(0x200b);"
        " GameState_SetField(0x37c6, 1, 0); /* the port's: the guest's Command list, X with the D-pad */",
    ),
]
for _rel, _old, _new in _COMMAND_LIST:
    FILE_HOOKS.setdefault(_rel, []).append((_old, _new))


for _module, _prefix in (
    ("scenes/ov008_camp_menu", "Ov008"),
    ("screens/ov025_camp_menu_2", "Ov025"),
):
    _root = f"src/overlays/{_module}/{_prefix}_"
    FILE_HOOKS[_root + "MainMenu_SetupTextSurfaces.c"] = [
        (
            f"void {_prefix}_MainMenu_SetupTextSurfaces(int obj)",
            "extern void khdays_ui_text_scope_push(const char *scope);\n"
            "extern void khdays_ui_text_scope_pop(void);\n"
            f"void {_prefix}_MainMenu_SetupTextSurfaces(int obj)",
        ),
        (
            "    if (*(int *)(obj + 0x14e0) == 0 || Session_IsReady() != 0) {",
            '    khdays_ui_text_scope_push("mission_select.instruction");\n'
            "    if (*(int *)(obj + 0x14e0) == 0 || Session_IsReady() != 0) {",
        ),
        (
            "    EnqueueObjGfxCommand(obj + 0x1420);",
            "    EnqueueObjGfxCommand(obj + 0x1420);\n    khdays_ui_text_scope_pop();\n"
            '    khdays_ui_text_scope_push("mission_select.world");',
        ),
        (
            "    EnqueueObjGfxCommand(obj + 0x145c);",
            "    EnqueueObjGfxCommand(obj + 0x145c);\n    khdays_ui_text_scope_pop();",
        ),
    ]
    FILE_HOOKS[_root + "DrawMissionDetail.c"] = [
        (
            "        Obj_InvokeInnerVtable4(pMenu->hintSurface);",
            '        khdays_ui_text_scope_push("mission_select.buttons");\n'
            "        Obj_InvokeInnerVtable4(pMenu->hintSurface);",
        ),
        (
            f"        {_prefix}_MarkSlotUsed(SLOT_A);",
            "        khdays_ui_text_scope_pop();\n" + f"        {_prefix}_MarkSlotUsed(SLOT_A);",
        ),
    ]


# Calls whose native form the ROM shows but no mechanical rule gives (see
# abi_repair.py for the rules): file -> [(text, replacement)], each read from
# the function's disassembly. Applied before the mechanical repairs. Keyed
# like HOOKS.
ABI_FIXES = {
    # Ov025_MoveGridCursor passes nDx and nDy to Ov025_DrawCardCountAndIcons
    # set only when the context has a list node (0x02094ac8-0x02094ae0); on
    # the other path they hold what was there, on the DS too, and the callee
    # reads them only with a shape, which that path never has (pShape stays 0
    # from 0x02094990). 0 says so, without the leftover.
    "ov025@02094988": [  # Ov025_MoveGridCursor
        ("    int nDx;\n", "    int nDx = 0;  /* unread without a list node (port/abi) */\n"),
        ("    int nDy;\n", "    int nDy = 0;\n"),
    ],
    "ov008@0206245c": [  # Ov008_MoveGridCursor: the same, Mission Mode's Panels page's
        ("    int nDx;\n", "    int nDx = 0;  /* unread without a list node (port/abi) */\n"),
        ("    int nDy;\n", "    int nDy = 0;\n"),
    ],
    # Ov006_MissionBuildOptionRows hands Ov006_SetTitleMode an unset `input` in single-player mode:
    # the ROM's r4 there is an address, which it ignores like 0. Natively the garbage could fall in
    # 1-4 (docs/DECOMP_FINDINGS.md section 2).
    "ov006@02050e1c": [  # Ov006_MissionBuildOptionRows
        ("    int input;\n", "    int input = 0;  /* the ROM's r4 here: a heap address, ignored (port/abi) */\n"),
    ],
    # The same in Ov008_MissionMenuWaitReady: the ROM hands Ov008_MissionScene_SetMode a heap
    # address, which it ignores (it stores only 1-4).
    "ov008@0207cd04": [  # Ov008_MissionMenuWaitReady
        ("    int nValue;\n", "    int nValue = 0;  /* the ROM's r4 here: a heap address, ignored (port/abi) */\n"),
    ],
    # Ov002_MapTouchToCell clamps an unset r4 into 0x16..0x1d; its one caller has r4 the cue
    # context's address, which clamps to 0x1d. Natively the garbage could be any cell.
    "ov002@020660b0": [  # Ov002_MapTouchToCell
        (
            "    int nCell;\n",
            "    int nCell = 0x1d;  /* the ROM's r4 here: an address, clamped to 0x1d (port/abi) */\n",
        ),
    ],
    # Ov005's direction and confirmation handlers keep r4 when no key is set: an address, taken as
    # no direction and no action. 0 does the same; natively the garbage could be a key.
    "ov005@020536ac": [  # Ov005_HandleDirectionalInput
        (
            "    int direction;\n",
            "    int direction = 0;  /* the ROM's r4 here: an address, no direction (port/abi) */\n",
        ),
    ],
    "ov005@020547e4": [  # Ov005_UpdateConfirmation
        ("    int action;\n", "    int action = 0;  /* the ROM's r4 here: an address, no action (port/abi) */\n"),
    ],
    # Ov001_SeedMathRandContexts mixes the entropy into accumulators it never sets: main's r4 (1)
    # and r6:r5 as crt0's autoload left them (docs/DECOMP_FINDINGS.md section 2).
    "ov001@0204cccc": [  # Ov001_SeedMathRandContexts
        (
            "    unsigned r;\n    unsigned long long x;\n",
            "    unsigned r = 1;  /* the ROM's r4 here: main's (port/abi) */\n"
            "    unsigned long long x = 0x027e0000027e0e60ULL;  /* r5:r6, crt0's autoload's (port/abi) */\n",
        ),
    ],
    # ModelAnimSet_Bind leaves texSrc in r0: it stores its fourth argument at
    # [sp,#4] on entry and reloads it to store the half at +0x0a, last
    # (`ldr r0,[sp,#4]; strh r0,[fp,#0xa]`, 0x0202a378). Resource_BindFileToSlot
    # returns that.
    "main@0202a208": [  # ModelAnimSet_Bind
        (
            "void ModelAnimSet_Bind(ModelAnimSet *set, ModelInst *inst, void *file, int texSrc)",
            "int ModelAnimSet_Bind(ModelAnimSet *set, ModelInst *inst, void *file, int texSrc)",
        ),
        ("    set->texSrc = texSrc;\n}", "    set->texSrc = texSrc;\n    return texSrc;\n}"),
    ],
    # Ov002_IsPanelModeSet returns its r0 untouched when no panel is installed; without a panel
    # both ways do the same, so 0 skips the branch (reached in mission 61).
    "ov002@02069d40": [  # Ov002_CreateAndRestoreHud
        ("extern int Ov002_IsPanelModeSet(void);", "extern int Ov002_IsPanelModeSet(int fallback);"),
        (
            "    if (Ov002_IsPanelModeSet()) {",
            "    if (Ov002_IsPanelModeSet(0)) {  /* no panel: either way nothing (port/abi) */",
        ),
    ],
}

# Ov022_StartPauseMenu, an object's update, leaves r0 as it came while the lock byte is set: its
# own address, so the object tries again next frame. The C's bare `return;` gave the native
# caller garbage (the port stopped in mission 61).
ABI_FIXES["ov022@02082c90"] = [  # Ov022_StartPauseMenu
    (
        "         * object keeps this update and tries again next frame. */\n        return;\n",
        "         * object keeps this update and tries again next frame. */\n"
        "        return (void *)&Ov022_StartPauseMenu;  /* r0 as it came: the update itself (port/abi) */\n",
    ),
]

# Bare `return;`s in functions with a value whose callers read it: the ROM's r0 on those paths,
# read from each one's code (docs/DECOMP_FINDINGS.md section 2). Ov022_TestShotHit's early exit
# leaves the last call's r0, which Ov022_ResolveShotHit reads as the hit.
ABI_FIXES["ov022@02090f5c"] = [  # Ov022_TestShotHit
    (
        "    if (pShot->nField134 != 0 && pShot->nAge < FIRST_HIT_AGE) {\n        return;\n    }\n",
        "    if (pShot->nField134 != 0 && pShot->nAge < FIRST_HIT_AGE) {\n"
        "        return (pDesc->nFlags & DESC_ROW_VALUES) == 0 ? params.nValue : 0;  /* the ROM's r0 (port/abi) */\n"
        "    }\n",
    ),
]
# The menus' clamp-and-notify (Ov025 and Ov008, four with one body: the same
# bytes) leave r0 as it came -- the first argument -- on the early exit
# (`ldmiaeq`, ov025 0x020a4ed8); their RunTwoPassUpdate callers read it.
for _key in ("ov025@020a4eb8", "ov025@020a9750", "ov008@02074150", "ov008@0206f914"):
    ABI_FIXES[_key] = [
        (
            "    if (param_1 == 0 || param_2 == 0 || param_4 == 0) {\n        return;\n    }\n",
            "    if (param_1 == 0 || param_2 == 0 || param_4 == 0) {\n"
            "        return param_1;  /* the ROM's r0: the first argument, as it came (port/abi) */\n    }\n",
        ),
    ]

# Ov107_Actor_SetAttachSlot takes a fifth, stack argument. Five effect actors
# store it in the outgoing-argument slot themselves and call with four: the
# C's `frame.outgoing = 0x1000` is that store (Ov117: `mov r0,#0x1000;
# str r0,[sp]` at 0x020cc09c/0x020cc0a4, then `bl` at 0x020cc0c4). Natively
# the fifth argument is passed.
for _key, _proto in (
    ("ov117@020cbfc4", "struct Vec4 *"),
    ("ov118@020cfc04", "struct Vec4 *"),
    ("ov185@020cfa2c", "int"),
    ("ov186@020d184c", "int"),
    ("ov187@020d548c", "int"),
):
    ABI_FIXES[_key] = [
        (
            f"extern void Ov107_Actor_SetAttachSlot(struct Obj *, int, unsigned int, {_proto});",
            f"extern void Ov107_Actor_SetAttachSlot(struct Obj *, int, unsigned int, {_proto}, unsigned int);",
        ),
        (
            "    Ov107_Actor_SetAttachSlot(self, 2, 2, 0);",
            "    Ov107_Actor_SetAttachSlot(self, 2, 2, 0, frame.outgoing);",
        ),
    ]

# The tailbunker and blade families' joint callbacks (two locals that were the
# DS's frame of one 4x3 matrix: natively the matrix overran them) and three
# script ops calling ScriptVm_ReadOperandInt without the interpreter's r0/r1
# had fixes here; the decomp's C now has the whole matrix and both
# arguments.

# r9 across the object updates (runtime.c khdays_rom_r9): Obj_UpdateAll
# starts with main's (unknown) and leaves each update's result in it
# (`blx r0; mov sb, r0`, 0x02023b64/68).
ABI_FIXES["main@02023adc"] = [  # Obj_UpdateAll
    (
        "void Obj_UpdateAll(int paused)\n{",
        "extern void khdays_rom_r9_unknown(void), khdays_rom_r9_set(int value);\nvoid Obj_UpdateAll(int paused)\n{",
    ),
    (
        "    gObjSystem[1] = gObjSystem[3];\n",
        "    khdays_rom_r9_unknown();  /* main's r9 (port/abi) */\n    gObjSystem[1] = gObjSystem[3];\n",
    ),
    (
        "                  khdays_check_update(((int *)gObjSystem[1])[5], cb); }\n",
        "                  khdays_check_update(((int *)gObjSystem[1])[5], cb); }\n"
        "                khdays_rom_r9_set(cb);  /* `mov sb, r0` */\n",
    ),
]
# The tag trackers' `swap` is r9, which the ROM never sets when the table has no selection getter:
# it reads what its caller left (the previous update's result), which the port keeps
# (khdays_rom_r9). With a nonzero r9 and no setter the ROM would call address 0: reported.
for _key in (
    "ov000@020561b4",
    "ov002@020540f0",
    "ov005@0204cdd0",
    "ov006@0204cdd0",
    "ov008@020551a4",
    "ov009@02051f68",
    "ov025@02088e4c",
    "ov026@02082ecc",
):
    ABI_FIXES[_key] = [
        (
            "    int swap;\n",
            "    extern int khdays_rom_r9(const char *reader);\n"
            "    extern void khdays_abi_gap(const char *what);\n"
            "    int swap = *(int *)(param_1 + 0x48) != 0 ? 0 : khdays_rom_r9(\n"
            "        \"a tag tracker's swap without a getter: r9 before the frame's first update\");\n",
        ),
        (
            "        if (swap != 0) {\n            (*(void (**)(int))(param_1 + 0x44))(*(int *)(e + 0x28));",
            "        if (swap != 0) {\n"
            "            if (*(int *)(param_1 + 0x44) == 0) {\n"
            '                khdays_abi_gap("a tag tracker\'s setter at address 0 (the ROM runs the ITCM there)");\n'
            "            }\n"
            "            (*(void (**)(int))(param_1 + 0x44))(*(int *)(e + 0x28));",
        ),
    ]

# Calls to an address several overlays share, where the function the ROM
# reaches is known: caller -> address -> the overlay loaded there. What the
# call needs is what that function takes, not the one the C names.
SHARED_CALL_TARGETS = {
    # The opening movie: the call at 0x0205b1f0 reaches ov024's
    # Ov024_MobiClip_OpenStreams(request), not ov008's Ov008_RebuildShopList
    # (confirmed at run time; docs/DECOMP_FINDINGS.md section 1).
    "ov012@0205b0cc": {0x020846C0: "ov024"},
}

# Argument registers the Ghidra scan counts as read but the ROM shows the
# function never reads (it sets them first), by function: the calls into it
# need nothing more than the C passes.
ABI_NOT_READ = {
    # Ov008_QueryFieldBySelector switches on r1 to eight helpers; seven load
    # their own r3 before the tail call (`ldr r12,=0x02056268; ldr r3,=...;
    # bx r12`, 0x020562f0, 0x020563a0-0x02056404), the eighth (0x02056304)
    # writes r3 before reading it.
    "ov008@02056478": (3,),
    # Its copy in ov005 is the same: seven helpers `ldr r3,[pc,#4]` before
    # `bx ip` (0x0204ea58, 0x0204eb08-0x0204eb6c); the eighth, 0x0204ea68,
    # reads r3 only after `ldrhls r3,[r1,#8]` under the same condition.
    "ov005@0204ebdc": (3,),
    # Ov002_LoadPanelSlots (Thumb) loads r2 from its literal pool at
    # 0x020550dc before anything reads it, and r3 is only saved by the push
    # that keeps the stack aligned until 0x0205510c sets it.
    "ov002@020550d0": (2, 3),
    # Ov011_TickLayoutAnimator sets r0 (`add r0,r4,#2`, 0x0205b34c) and then
    # branches to its loop test, which sets r1 (`ldr r1,[r8]`, 0x0205b7d8),
    # before anything reads either.
    "ov011@0205b340": (0, 1),
    # Ov011_BlitTileRow reads one stack argument, its fifth (`ldr r8,[sp,#0x20]`
    # after pushing eight registers, 0x0205b828); nothing reads the sixth.
    "ov011@0205b814": (5,),
}

# Values the C returns from functions reached only through pointers, where
# the ROM's pointer calls never read r0 (the rest the need filter finds
# mechanically: no C reads them). By the function whose value it is.
VALUE_NOT_READ = {
    # Returned by the enemies' tick hooks at +0xc: every `blx` of such a hook sets r0 next or
    # returns it unread, so no C reads it.
    "ov107@020c6980": "Ov107_ProcessObjectTick",
    # Returned by Ov285/286_ForwardAnimEvent, the animation hook at +0x1dc.
    # Its callers: Ov107_AiState_OnMessage branches to `mov r0,sl`
    # (0x020c7a10), Ov144/145_Construct `mov r0,r4`; Ov107_PostTagUpdate
    # returns it to Ov245_ResetMode and Ov245_SetNodeMode3, whose values no
    # C reads.
    "main@0203b9fc": "SetSubitemState",
}
# (The C of these uses the parameters as locals: the calls pass zeros for
# them, abi_repair.pad_short_calls, so the writes stay in the call's frame.)


# Globals some split files define that symbols.txt does not name: statics of one original
# translation unit, fields of a block its module's symbol anchors. These are the ones code uses,
# at the addresses the ROM reaches them by; a used one missing here fails the link.
PHANTOMS = {
    # os_thread.c, block data_0204430c: OSi_RescheduleThread (+0x04), the
    # scheduler's current-thread pointer (func_02001f10: ldr [base,#8]),
    # OS_ExitThread's destructor stack (func_02001e0c: ldr [base,#0x1c]),
    # the thread id counter (func_020018ec: ldr/str [base,#0x20]).
    "OSi_RescheduleCount": 0x02044310,
    "OSi_CurrentThreadPtr": 0x02044314,
    "OSi_IsThreadInitialized": 0x02044318,
    "OSi_StackForDestructor": 0x02044328,
    "OSi_ThreadIdCount": 0x0204432C,
    # snd_command.c, block data_02044748 (func_020085f0: [base,#4] finished
    # tag, #0x10 free-list end, #0x14 queue read index, #0x1c waiting count;
    # func_0200851c: #0x8/#0xc reserve list and its end, #0x18 queue write
    # index, #0x20 current tag = 1).
    "sFinishedTag": 0x0204474C,
    "sReserveList": 0x02044750,
    "sReserveListEnd": 0x02044754,
    "sFreeListEnd": 0x02044758,
    "sWaitingCommandListQueueRead": 0x0204475C,
    "sWaitingCommandListQueueWrite": 0x02044760,
    "sWaitingCommandListCount": 0x02044764,
    "sCurrentTag": 0x02044768,
    # nns snd_resource.c, block data_0204a2fc (func_02019d4c: [base,#4] alarm
    # lock; func_02019cb8: [base,#8] channel lock).
    "sAlarmLock": 0x0204A300,
    "sChannelLock": 0x0204A304,
    # nns snd_stream.c, block data_0204ad8c (func_0201d0e0: [base,#4] prepare
    # thread; func_0201c8f4: str sDecodeBufferArea -> [base,#8]).
    "sPrepareThread": 0x0204AD90,
    "sDecodeBuffer": 0x0204AD94,
    # ov105 wh.c, block data_ov105_020c04c0 (func_ov105_020bf7f4: #0x28 receive
    # size, #0x2c send size, #0x14 receive buffer, #0x1c send buffer, #0x34
    # connect mode, #0x40 child WEP key generator; func_ov105_020bf5a8: #0x4c
    # WM buffer, #0x20 receiver, #0x30 error code, strh #0xc disconnect reason,
    # #0x38 accept judge; func_ov105_020bf704: strh #0x4 MP frequency).
    "sWh_nMpFreq": 0x020C04C4,
    "sWh_nDisconnectReason": 0x020C04CC,
    "sWh_pRecvBuffer": 0x020C04D4,
    "sWh_pSendBuffer": 0x020C04DC,
    "sWh_pReceiver": 0x020C04E0,
    "sWh_nRecvBufferSize": 0x020C04E8,
    "sWh_nSendBufferSize": 0x020C04EC,
    "sWh_nErrCode": 0x020C04F0,
    "sWh_nConnectMode": 0x020C04F4,
    "sWh_pJudgeAccept": 0x020C04F8,
    "sWh_pChildWEPKeyGenerator": 0x020C0500,
    "sWh_pWmBuffer": 0x020C050C,
}

# The game's objects the port's side reads by name, by their DS address in the window
# (khdays_ds_symbols.h): the sound archive's path, and what the one-screen picture tells the
# game's situation from (ui/situation.c).
PORT_DS_SYMBOLS = (
    "gSndSoundDataPath",
    "gSceneCtl",
    "gPauseMode",
    "data_0204bda4",
    "data_02044660",
    "data_020446d6",
    "data_020446d8",
    "data_020446e2",
    "data_020446e6",
    "gGfdFrmTexRegions",
    "data_ov000_0205ac24",
    "data_ov002_0207f9fc",
    "data_ov025_020b574c",
    "data_0204c2d0",
    "data_0204c3d8",
    "data_ov022_020b2e78",
    # the pause's context and Mission Mode's flags (runtime/menus/retry.c)
    "data_0204be08",
    "data_0204c240",
    # the game's language and its message databases' table (runtime/core/language.c)
    "gLangPath",
    "data_0204c238",
    # the mods' interface (runtime/scripts/game_link.c): the game's state, the object list's frame
    # count, the current mission, the sound manager (its track playing)
    "gGameState",
    "gObjSystem",
    "data_0204c23c",
    "gSoundMgr",
    # the room's rules, friendly fire first (runtime/battle/stop_magic.c)
    "data_0204c248",
)


def forward_tail_call(text: str, name: str) -> tuple[str, str]:
    """`T name() { return g(); }` for a ROM tail call: take and pass on four
    argument words (cdecl: a caller that pushed fewer leaves its own frame in
    the extra ones, which g does not read). Returns the text and what was
    done."""
    body = re.search(
        rf"(\b{re.escape(name)}\s*\()\s*(?:void)?\s*(\)\s*\{{\s*(?:return\s+)?)"
        rf"(\w+)\s*\(\s*\)(\s*;\s*\}})",
        text,
    )
    if body is None:
        return text, "left (the C passes its arguments or is not a lone call)"
    callee = body[3]
    before = text[: body.start()]
    decl = None
    for decl in re.finditer(rf"\b{re.escape(callee)}\s*\(([^()]*)\)\s*;", before):
        pass
    if decl is not None and decl[1].strip() not in ("", "void"):
        return text, f"left ({callee} is declared with parameters)"
    words = ", ".join(f"khdays_r{i}" for i in range(4))
    params = ", ".join(f"int khdays_r{i}" for i in range(4))
    new = f"{body[1]}{params}{body[2]}{callee}({words}){body[4]}"
    text = before + new + text[body.end() :]
    # declarations of either that say `(void)` would contradict the call and
    # the definition
    for fn in (callee, name):
        text = re.sub(rf"(\b{re.escape(fn)}\s*\()\s*void\s*(\)\s*;)", r"\1\2", text)
    return text, f"forwards four words to {callee}"


def zero_size(expr: str, defines: dict[str, str]) -> bool:
    expr = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: defines.get(m[0], m[0]), expr)
    if not re.fullmatch(r"[0-9a-fA-FxX\s\+\-\*\(\)]+", expr):
        return False
    try:
        return eval(expr, {"__builtins__": {}}) == 0  # numbers, + - * and ( ) only
    except Exception:
        return False


def name_unnamed_params(match: re.Match) -> str:
    params = [p.strip() for p in match[2].split(",")]
    if params == ["void"] or params == [""]:
        return match[0]
    fixed = []
    for i, p in enumerate(params):
        if p == "...":
            fixed.append(p)
            continue
        tokens = re.findall(r"\w+|\*", p)
        unnamed = (
            p.endswith("*")
            or len(tokens) <= 1
            or tokens[-1]
            in (
                "int",
                "char",
                "short",
                "long",
                "u8",
                "u16",
                "u32",
                "s8",
                "s16",
                "s32",
                "fx32",
                "fx16",
                "BOOL",
                "void",
                "unsigned",
                "signed",
            )
        )
        fixed.append(f"{p} _unnamed{i}" if unnamed else p)
    return f"{match[1]}{', '.join(fixed)})"


# A cast of the math unit's or IME's / IE's address, `(T *)0x04000280`: the
# register reached without the trap's fault (runtime/core/io.h, khdays_io_cp and
# khdays_io_plain) -- the same register, the same values. The size of a read
# there is the pointed-to type's, for the math unit's results.
UNTRAPPED_REGISTER = re.compile(r"\(([^()]*?)\*\s*\)\s*0x0?40002([0-9a-fA-F]{2})\b")

# `*(T *)0x04000400 = value;` -- a word stored to a geometry command port
# (GXFIFO 0x400-0x43f, the command ports 0x440-0x5ff): given to the register
# model as the trap would give it (runtime/core/io.h, khdays_io_store), without
# the fault. A statement of its own, the value without a `;`.
GEOMETRY_STORE = re.compile(
    r"\*\s*\(\s*(?:volatile\s+)?(?:unsigned\s+int|unsigned|u32|vu32|s32|int|REGType32v)\s*\*\s*\)\s*"
    r"0x0?4000([45][0-9a-fA-F]{2})\s*=(?!=)\s*([^;]+);"
)


def untrapped_register(m: re.Match) -> str:
    offset = int(m[2], 16)
    pointee = m[1]
    # A static object's initializer must be a constant: such a pointer keeps
    # the address (and the trap). Its statement starts after the last ; { }.
    statement = m.string[max(m.string.rfind(c, 0, m.start()) for c in ";{}") + 1 : m.start()]
    if re.match(r"\s*(?:#[^\n]*\n\s*)*static\b", statement):
        return m[0]
    if 0x80 <= offset < 0xC0:
        if re.search(r"64|long\s+long", pointee):
            size = 8
        elif re.search(r"\b(?:u16|s16|vu16|vs16|short)\b", pointee):
            size = 2
        elif re.search(r"\b(?:u8|s8|vu8|char)\b", pointee):
            size = 1
        else:
            size = 4
        return f"({pointee}*)khdays_io_cp(0x2{m[2]}u, {size})"
    if 0x08 <= offset < 0x0C or 0x10 <= offset < 0x14:
        return f"({pointee}*)khdays_io_plain(0x2{m[2]}u)"
    return m[0]


def transform(text: str) -> str:
    text = UNTRAPPED_REGISTER.sub(untrapped_register, text)
    text = SPLIT_HEADER.sub(r"\1 ", text)
    text = REGISTER_ASM.sub("", text)
    text = CLZ_ASM.sub(lambda m: f"{m[1]} = khdays_clz({m[2]});", text)
    text = END_LABEL.sub(lambda m: f"{m[1]};{m[2]}", text)
    text = DSPROT_RANGES_INCLUDE.sub(r'\1"khdays_dsprot_ranges.h"', text)
    text = VIRTUAL_METHOD.sub(r"\1__cdecl \2", text)
    text = EMPTY_FUNCTION.sub(r"void *\1(void *khdays_r0) { return khdays_r0; }  /* bx lr: r0 unchanged */", text)
    text = VIRTUAL_DTOR.sub(r"\1virtual void __cdecl khdays_mwcc_complete_dtor();  /* mwcc's slot 0 */\n\1\2", text)
    text = DELETE_STATEMENT.sub(
        r"\1{ void *khdays_p = (void *)(\2); if (khdays_p) "
        r"((void (__cdecl *)(void *))(*(void ***)khdays_p)[1])(khdays_p); }",
        text,
    )
    text = ARRAY_ASSIGN.sub(
        lambda m: f"{m[1]}KHDAYS_ARRAY_ASSIGN({m[2].strip()}, {m[3]}, {m[4].strip()}, {m[5].strip()});", text
    )
    defines = {m[1]: m[2] for m in NUMERIC_DEFINE.finditer(text)}

    def drop_zero(m: re.Match) -> str:
        name = m[2]
        # Only a member nothing accesses (`.name` / `->name`) can go.
        if zero_size(m[3], defines) and not re.search(rf"(\.|->)\s*{re.escape(name)}\b", text):
            return f"{m[1]}/* zero-size member {name} removed for MSVC */"
        return m[0]

    text = ZERO_MEMBER.sub(drop_zero, text)
    text = DEF_HEADER.sub(name_unnamed_params, text)
    text = widen_narrow_returns(text)
    return text


# A function returning a char or short: the ARM callee leaves the value
# extended to 32 bits in r0, and the decomp's callers often declare it `int`;
# MSVC returns it in AL/AX and leaves the rest of the register as it was. So the
# definition returns an int holding the narrowed value, as r0 does.
NARROW_TYPE = r"(?:u8|s8|u16|s16|(?:unsigned\s+|signed\s+)?char|(?:unsigned\s+|signed\s+)?short(?:\s+int)?)"
NARROW_DEFINITION = re.compile(rf"^((?:static\s+)?)({NARROW_TYPE})(\s+)(\w+)(\s*\([^;{{}}]*\)\s*\{{)", re.MULTILINE)
RETURN_VALUE = re.compile(r"\breturn\b(\s*)([^;]+?)\s*;")


# The decomp's C++ (ov024's MobiClip stream layer) as C: a class becomes a struct with its
# vtable's reference first, and a virtual call a call through the vtable's slot with the object
# first, as the ROM makes it (docs/BUILD.md).
CLASS_DECL = re.compile(r"\bclass\s+(\w+)\s*\{(.*?)\n\};", re.S)
VIRTUAL_DECL = re.compile(r"virtual\s+(?:(.*?)\s+)?__cdecl\s+(~?\w+)\s*\(([^()]*)\)\s*$", re.S)
STRUCT_DEF = re.compile(r"^struct\s+(\w+)\s*\{", re.M)
EXTERN_C_BLOCK = re.compile(r'\bextern\s+"C"\s*\{')


def matching_brace(text: str, open_brace: int) -> int:
    """The `}` closing the `{` at `open_brace`, past comments and literals."""
    depth, i = 0, open_brace
    while i < len(text):
        c = text[i]
        if text.startswith("/*", i):
            i = text.index("*/", i) + 2
            continue
        if text.startswith("//", i):
            i = text.index("\n", i)
            continue
        if c in "\"'":
            j = i + 1
            while text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise SystemExit("prepare.py: an unclosed brace")


def cpp_to_c(text: str, rel: str) -> str:
    methods: dict[str, tuple[int, str, str]] = {}  # name -> (slot, result, parameters)

    def class_struct(m: re.Match) -> str:
        name, body = m[1], re.sub(r"\b(?:public|private|protected)\s*:", "", m[2])
        members, slot = [], 0
        for decl in body.split(";"):
            d = re.sub(r"/\*.*?\*/|//[^\n]*", "", decl, flags=re.S).strip()
            if not d:
                continue
            v = VIRTUAL_DECL.match(d)
            if v is None:
                if "(" in d:
                    raise SystemExit(f"prepare.py: {rel}: a member function that is not virtual: {d}")
                members.append(d + ";")
                continue
            if not v[2].startswith("~") and v[2] != "khdays_mwcc_complete_dtor":
                if v[2] in methods:
                    raise SystemExit(f"prepare.py: {rel}: two virtual {v[2]}s")
                methods[v[2]] = (slot, v[1] or "void", v[3].strip())
            slot += 1
        return (
            f"typedef struct {name} {{\n    unsigned int khdays_vtable;  /* the game's vtable (cpp_to_c) */\n"
            + "".join(f"    {d}\n" for d in members)
            + f"}} {name};"
        )

    text = CLASS_DECL.sub(class_struct, text)
    if re.search(r"\bclass\b", re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)):
        raise SystemExit(f"prepare.py: {rel}: a class cpp_to_c does not take")
    # every struct its own typedef name, as C++ has it
    names = STRUCT_DEF.findall(text)
    if names:
        first = STRUCT_DEF.search(text).start()
        text = text[:first] + "".join(f"typedef struct {n} {n};\n" for n in names) + text[first:]
    for name, (slot, result, params) in methods.items():
        params = "" if params in ("", "void") else ", " + params
        call = re.compile(r"([A-Za-z_]\w*(?:\s*(?:->|\.)\s*[A-Za-z_]\w*)*)\s*->\s*" + name + r"\s*\(\s*(\))?")
        text = call.sub(
            lambda m: (
                f"(({result} (__cdecl *)(void *{params}))(*(void ***)({m[1]}))[{slot}])"
                f"((void *)({m[1]})" + (")" if m[2] else ", ")
            ),
            text,
        )
    while (m := EXTERN_C_BLOCK.search(text)) is not None:
        close = matching_brace(text, m.end() - 1)
        text = text[: m.start()] + text[m.end() : close] + text[close + 1 :]
    return text.replace('extern "C" ', "extern ")


def body_end(text: str, open_brace: int) -> int:
    """The index just past the brace that closes the one at `open_brace`
    (strings, characters and comments skipped)."""
    depth, i, n = 0, open_brace, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if text.startswith("/*", i):
            i = text.find("*/", i + 2) + 2 if text.find("*/", i + 2) >= 0 else n
            continue
        if text.startswith("//", i):
            i = text.find("\n", i) if text.find("\n", i) >= 0 else n
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def widen_narrow_returns(text: str) -> str:
    out, pos, widened = [], 0, []
    for m in NARROW_DEFINITION.finditer(text):
        if m.start() < pos:
            continue
        name, narrow = m[4], re.sub(r"\s+", " ", m[2])
        end = body_end(text, m.end() - 1)
        body = RETURN_VALUE.sub(lambda r: f"return{r[1]}({narrow})({r[2]});", text[m.end() : end])
        out.append(text[pos : m.start()])
        out.append(f"{m[1]}int{m[3]}{name}{m[5]}{body}")
        pos = end
        widened.append(name)
    out.append(text[pos:])
    text = "".join(out)
    # the same functions' prototypes in this file say so too
    for name in widened:
        text = re.sub(
            rf"^(\s*(?:extern\s+|static\s+)?){NARROW_TYPE}(\s+{re.escape(name)}\s*\([^;{{}}]*\)\s*;)",
            r"\1int\2",
            text,
            flags=re.MULTILINE,
        )
    return text


# --- File-scope definitions (for duplicates and BSS) --------------------------

# A file-scope object definition header, on one line: `T name[..] =` or `T name;`.
DEFINITION_RE = re.compile(
    r"^(?!extern\b|static\b|typedef\b|return\b|union\b|enum\b|#)"
    r"([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)((?:[ \t]*\[[^\]\n]*\])*)"
    r"((?:[ \t]*__attribute__[ \t]*\(\([^\n]*?\)\))?)[ \t]*(=|;)",
    re.MULTILINE,
)
KEYWORDS = {"else", "goto", "case", "do", "break", "continue", "return", "struct"}


def brace_depths(text: str) -> list[tuple[int, int]]:
    """(index, depth from there on) at every change of brace depth, outside
    comments, strings and character literals."""
    changes = [(0, 0)]
    depth = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if c == "{" or c == "}":
            depth += 1 if c == "{" else -1
            changes.append((i + 1, depth))
        i += 1
    return changes


def definitions(text: str) -> list[re.Match]:
    """File-scope object definitions (a declaration at column 0 inside a
    function body is a local, not one of these)."""
    changes = brace_depths(text)
    starts = [i for i, _ in changes]
    out = []
    for m in DEFINITION_RE.finditer(text):
        head = m[1].split()
        if not head or head[-1] in KEYWORDS or m[2] in KEYWORDS:
            continue
        if changes[bisect.bisect_right(starts, m.start()) - 1][1] != 0:
            continue
        out.append(m)
    return out


def statement_end(text: str, start: int) -> int:
    """Index just past the `;` that ends the declaration starting at `start`,
    skipping braces, strings and comments inside an initializer."""
    depth = 0
    i = start
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if text.startswith("/*", i):
            i = text.find("*/", i + 2) + 2
            continue
        if text.startswith("//", i):
            i = text.find("\n", i)
            continue
        if c in "{(":
            depth += 1
        elif c in "})":
            depth -= 1
        elif c == ";" and depth == 0:
            return i + 1
        i += 1
    return n


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def hal_definitions() -> set[str]:
    """What port/hal defines: functions, data and KHDAYS_HAL_TODO stubs."""
    names: set[str] = set()
    for source in (ROOT / "port" / "hal").glob("*.c"):
        text = strip_comments(source.read_text(encoding="utf-8"))
        names.update(re.findall(r"KHDAYS_HAL_TODO\((\w+)\)", text))
        names.update(
            re.findall(
                r"^(?!extern\b|static\b|typedef\b|#)[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)"
                r"[ \t]*[\(\[=;]",
                text,
                re.MULTILINE,
            )
        )
    return names


def function_keys(modules: list[Module]) -> dict[str, str]:
    """Each function's key, `module@address`: what port/abi and the manual
    fixes name functions by, as the decomp renames them."""
    return {n: f"{m.name}@{a:08x}" for m in modules for n, k, a in m.symbols if k == "function"}


def module_id(name: str) -> int:
    """The module number KHDAYS_DATA_INIT records: an overlay's id, or -1/-2/-3
    for the static module, ITCM and DTCM (their data is placed at start-up)."""
    if name.startswith("ov"):
        return int(name[2:])
    return {"main": -1, "itcm": -2, "dtcm": -3}[name]


def decomp_absolute_symbols() -> dict[str, int]:
    """The ABSOLUTE_SYMBOLS table of the decomp's tools/configure.py."""
    text = (DECOMP / "tools" / "configure.py").read_text(encoding="utf-8")
    block = text[text.index("ABSOLUTE_SYMBOLS = {") :]
    block = block[: block.index("}")]
    return {n: int(v, 16) for n, v in re.findall(r'"(\w+)":\s*0x([0-9A-Fa-f]+)', block)}


RELOC_OVERLAYS_RE = re.compile(r"^from:0x[0-9a-fA-F]+\s+kind:\w+\s+to:0x([0-9a-fA-F]+)\s+module:overlays\(([\d,\s]+)\)")


def overlay_dispatch(
    modules: list[Module], texts: dict[str, str], module_of: dict[str, str]
) -> tuple[dict[int, list[tuple[int, str]]], int]:
    """Routes references to an address several overlays' functions share
    through a thunk per address (see main). Returns the thunks (address ->
    [(overlay, function)]) and how many files were rerouted."""
    function_at: dict[tuple[int, int], str] = {}
    for m in modules:
        mo = re.fullmatch(r"ov(\d+)", m.name)
        if mo:
            for n, k, a in m.symbols:
                if k == "function":
                    function_at[(int(mo[1]), a)] = n
    thunks: dict[int, list[tuple[int, str]]] = {}
    reroute: dict[str, dict[str, str]] = collections.defaultdict(dict)  # module -> name -> thunk
    for m in modules:
        relocs = m.config / "relocs.txt"
        if not relocs.exists():
            continue
        for line in relocs.read_text(encoding="utf-8").splitlines():
            r = RELOC_OVERLAYS_RE.match(line)
            if not r:
                continue
            target = int(r[1], 16)
            candidates = [
                (o, function_at[(o, target)])
                for o in (int(x) for x in r[2].replace(" ", "").split(","))
                if (o, target) in function_at
            ]
            if len(candidates) < 2:
                continue  # data, or a function in only one of them
            thunks[target] = candidates
            for _, name in candidates:
                reroute[m.name][name] = f"khdays_ovcall_{target:08x}"
    files = 0
    for rel, text in texts.items():
        names = reroute.get(module_of[rel])
        if not names:
            continue
        used = [
            n
            for n in names
            if re.search(rf"\b{re.escape(n)}\b", text)
            and
            # not the file defining it (a definition starts at column 0)
            not re.search(rf"^[A-Za-z_][\w \t\*]*\b{re.escape(n)}\s*\([^;{{}}]*\)\s*\{{", text, re.MULTILINE)
        ]
        if used:
            texts[rel] = (
                "".join(f"#define {n} {names[n]}  /* the overlay loaded there (prepare.py) */\n" for n in sorted(used))
                + text
            )
            files += 1
    return thunks, files


# Two punctuators that one token would be, written together (C11 6.4.6).
PASTED_PAIRS = {
    "--",
    "++",
    "-=",
    "+=",
    "->",
    "&&",
    "||",
    "<<",
    ">>",
    "==",
    "!=",
    "<=",
    ">=",
    "*=",
    "/=",
    "%=",
    "&=",
    "|=",
    "^=",
    "##",
    "/*",
    "//",
    "..",
}
PUNCTUATORS = "+-*/%&|^!<>=.#"
MACRO_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)(\()?((?:[^\n\\]|\\[\s\S])*)$", re.M)
CODE_SKIP = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*', re.S)


def punctuator_macros(texts) -> tuple[dict[str, str], dict[str, str]]:
    """The macros whose expansion starts with a punctuator (any macro) and
    those whose expansion ends with one (object-like): name -> that
    punctuator."""
    starts: dict[str, str] = {}
    ends: dict[str, str] = {}
    for text in texts:
        for m in MACRO_DEFINE.finditer(text):
            rest = m[3]
            if m[2]:  # function-like: the body follows the parameters
                close = rest.find(")")
                if close < 0:
                    continue
                rest = rest[close + 1 :]
            body = re.sub(r"/\*.*?\*/|//.*$", "", rest.replace("\\\n", " ")).strip()
            if body and body[0] in PUNCTUATORS:
                starts.setdefault(m[1], body[0])
            if body and body[-1] in PUNCTUATORS and not m[2]:
                ends.setdefault(m[1], body[-1])
    return starts, ends


def pasted_sites(text: str, starts: dict[str, str], ends: dict[str, str]) -> list[int]:
    """Where MSVC's preprocessed output (/E) would write two punctuators
    together that the compiler keeps apart -- a punctuator right before a
    macro whose expansion starts with one (`-EDGE_TILT`, EDGE_TILT -0xb33,
    comes out `--0xb33`), or after one ending with one: the offsets to put a
    space at, outside comments and literals."""
    skip = bytearray(len(text))
    for m in CODE_SKIP.finditer(text):
        skip[m.start() : m.end()] = b"\1" * (m.end() - m.start())
    sites = []
    for m in re.finditer(r"([" + re.escape(PUNCTUATORS) + r"])([A-Za-z_]\w*)", text):
        if not skip[m.start()] and m[1] + starts.get(m[2], " ") in PASTED_PAIRS:
            sites.append(m.start(2))
    for m in re.finditer(r"\b([A-Za-z_]\w*)([" + re.escape(PUNCTUATORS) + r"])", text):
        if not skip[m.start(2)] and ends.get(m[1], " ") + m[2] in PASTED_PAIRS:
            sites.append(m.start(2))
    return sorted(set(sites))


def separate_pasted(texts: dict[str, str]) -> int:
    """The sites pasted_sites finds in the compiled sources, separated; one in
    a header (which this script does not patch) stops it, naming it."""
    headers = [
        p
        for d in (
            DECOMP / "include",
            DECOMP / "src",
            DECOMP / "libs",
            ROOT / "port" / "compat",
            ROOT / "port" / "hal",
            ROOT / "port" / "runtime",
        )
        for p in d.rglob("*.h")
    ]
    header_texts = {p: p.read_text(encoding="utf-8", errors="replace") for p in headers}
    starts, ends = punctuator_macros(list(header_texts.values()) + list(texts.values()))
    in_headers = [
        f"{p.relative_to(ROOT)}: offset {at}" for p, t in header_texts.items() for at in pasted_sites(t, starts, ends)
    ]
    if in_headers:
        raise SystemExit(
            "references: tokens MSVC's preprocessed output would paste, in headers:\n" + "\n".join(in_headers[:20])
        )
    count = 0
    for rel, text in texts.items():
        sites = pasted_sites(text, starts, ends)
        for at in reversed(sites):
            text = text[:at] + " " + text[at:]
        if sites:
            texts[rel] = text
            count += len(sites)
    return count


def rewrite_game(
    out: Path,
    sources: list[tuple[str, str]],
    absolute: dict[str, int],
    functions: dict[str, int],
    dispatched: dict[str, int],
) -> tuple[list[str], int]:
    """The game's C (`sources`: (path compiled, original relative path)) and
    the HAL rewritten into out/rewritten (port/tools/references)."""
    from references.build import Program, rewrite_all

    jobs = [(path, str(out / "rewritten" / rel).replace("\\", "/")) for path, rel in sources]
    jobs += [
        (str(p).replace("\\", "/"), str(out / "rewritten" / "native_hal" / p.name).replace("\\", "/"))
        for p in sorted((ROOT / "port" / "hal").glob("*.c"))
    ]
    thunks = {n for n in functions if n.startswith("khdays_ovcall_")}
    outputs, size = rewrite_all(
        jobs, Program(absolute=dict(absolute), functions=functions, game_functions=thunks, dispatched=dispatched), out
    )
    return outputs, size


def cmake_list(name: str, paths: list[str]) -> str:
    return f"set({name}\n" + "".join(f'    "{p}"\n' for p in paths) + ")\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build" / "port" / "gen-x64"))
    args = ap.parse_args()
    out = Path(args.out).resolve()  # sources.cmake names its files absolutely
    patched_dir = out / "patched"
    patched_dir.mkdir(parents=True, exist_ok=True)

    modules = load_modules()
    compiled: list[tuple[Path, str]] = []  # (source, original relative path)
    hal_files: list[tuple[str, str]] = []  # (module, relative path)
    texts: dict[str, str] = {}
    module_of: dict[str, str] = {}
    # The manual edits by the file of the function they name (one function per
    # file, named after it).
    key_name = {k: n for n, k in function_keys(modules).items()}
    for key in list(HOOKS) + list(ABI_FIXES):
        if key not in key_name:
            raise SystemExit(f"no function at {key} (prepare.py HOOKS / ABI_FIXES)")
    for rel in FILE_HOOKS:
        if not (DECOMP / rel).exists():
            raise SystemExit(f"no file {rel} (prepare.py FILE_HOOKS)")
    hooks = {key_name[k]: v for k, v in HOOKS.items()}
    abi_fixes = {key_name[k]: v for k, v in ABI_FIXES.items()}
    register_tables = registers.header_tables(
        (DECOMP / "include" / "nitro" / "hw.h").read_text(encoding="utf-8"),
        (DECOMP / "include" / "nitro" / "gx.h").read_text(encoding="utf-8"),
    )
    registers_left: list[str] = []
    as_c: set[str] = set()  # the C++ sources turned into C
    ui_hooked: set[str] = set()  # the files UI_HOOKS changed
    for m in modules:
        for rel in m.files:
            src = DECOMP / rel
            if "asm_stubs" in rel or src.suffix == ".s" or not src.exists() or m.name in REPLACED_MODULES:
                hal_files.append((m.name, rel))
                continue
            text = src.read_text(encoding="utf-8", errors="replace")
            # Anything still carrying ARM assembly after the transforms is
            # hardware-level code the HAL replaces.
            new = transform(text)
            new = font_layout.patch(new, rel)
            file_hooks = hooks.get(Path(rel).stem, []) + FILE_HOOKS.get(rel, [])
            for ui_name, ui_part, ui_port, ui_kind in UI_HOOKS.get(rel, []):
                new = ui_hook(new, rel, ui_name, ui_part, ui_port, ui_kind)
                ui_hooked.add(rel)
            if file_hooks:
                for before, after in file_hooks:
                    if before not in new:
                        raise SystemExit(f"hook text not found in {rel} ({Path(rel).stem}): {before}")
                    new = new.replace(before, after)
                new = "extern void khdays_data_init(int module);\n" + new
            # the "am I the host" checks through the port (ROLE_QUERY)
            roles = 0
            if role_scoped(rel):
                new, roles = ROLE_QUERY.subn("khdays_mp_role()", new)
                new, more = ROLE_QUERY_VAR.subn(r"\1khdays_mp_role()\2", new)
                roles += more
            if roles:
                new = "extern int khdays_mp_role(void);\n" + new
            # after the hooks, which give some of these stores to the geometry themselves
            new = GEOMETRY_STORE.sub(
                lambda m: f"khdays_io_store(0x04000{m[1]}u, 4, (unsigned int)({m[2].strip()}));", new
            )
            # every other register access reaches the port without the trap's
            # fault (port/tools/registers.py)
            new = registers.rewrite(new, rel, register_tables, registers_left)
            if re.search(r"\basm\b|__asm\b", strip_comments(new)):
                hal_files.append((m.name, rel))
                continue
            if rel.endswith(".cpp"):
                new = cpp_to_c(new, rel)  # rewritten as C (T005a)
                as_c.add(rel)
            texts[rel] = new
            module_of[rel] = m.name
            compiled.append((src, rel))

    missing_ui = sorted(set(UI_HOOKS) - ui_hooked)
    if missing_ui:
        raise SystemExit("UI hooks on files the build does not compile: " + ", ".join(missing_ui))
    # Data sources no delinks.txt lists although they are the only definition of
    # a DS data symbol (the decomp's own build then takes those bytes from the
    # ROM, which natively would leave ARM addresses in function-pointer tables).
    # Each joins the module that owns its symbol.
    listed = {rel for m in modules for rel in m.files}
    provided = {d[2] for text in texts.values() for d in definitions(text)}
    owner_module = {n: m.name for m in modules for n, k, _ in m.symbols if k in ("data", "bss")}
    orphans = []
    for path in sorted((DECOMP / "src").rglob("data/*.c")):
        rel = path.relative_to(DECOMP).as_posix()
        if rel in listed:
            continue
        text = transform(path.read_text(encoding="utf-8", errors="replace"))
        names = [
            n
            for n in re.findall(r"(?:^|\})[ \t\w\*]*?\b(data_\w+)\s*(?:\[[^\]\n]*\])*\s*=", text, re.MULTILINE)
            if n in owner_module and n not in provided
        ]
        if names:
            texts[rel] = text
            module_of[rel] = owner_module[names[0]]
            compiled.append((path, rel))
            orphans.append(f"{rel}\t{' '.join(names)}")
    (out / "orphan_data.txt").write_text("".join(o + "\n" for o in orphans), encoding="utf-8")
    (out / "registers_left.txt").write_text("".join(f"{line}\n" for line in registers_left), encoding="utf-8")

    # Globals defined (with an initializer) in several files: the decomp split
    # one original translation unit into a file per function, repeating its
    # file-scope variables. Keep one definition -- in a data/ file when there
    # is one -- and make the rest `extern`.
    owners = collections.defaultdict(list)
    for rel, text in texts.items():
        for d in definitions(text):
            owners[d[2]].append(rel)
    duplicates = {n: sorted(set(f)) for n, f in owners.items() if len(set(f)) > 1}
    for name, files in duplicates.items():
        keep = next((f for f in files if "/data/" in f), files[0])
        for f in files:
            if f == keep:
                continue
            text = texts[f]
            for d in definitions(text):
                if d[2] != name:
                    continue
                end = statement_end(text, d.start())
                texts[f] = text[: d.start()] + f"extern {d[1]}{d[2]}{d[3]};" + text[end:]
                break
    (out / "duplicates.txt").write_text(
        "".join(f"{n}\t{' '.join(f)}\n" for n, f in sorted(duplicates.items())), encoding="utf-8"
    )

    # Calls that lean on the ARM registers (port/tools/abi_repair.py): pass
    # and return what the ROM's code does, from port/abi/ghidra_abi.txt.
    fix_files = {Path(rel).stem: rel for rel in texts if Path(rel).stem in abi_fixes}
    for name, fixes in abi_fixes.items():
        rel = fix_files.get(name)
        if rel is None:
            raise SystemExit(f"no source defines {name} (prepare.py ABI_FIXES)")
        for before, after in fixes:
            if before not in texts[rel]:
                raise SystemExit(f"ABI fix text not found in {rel}: {before[:60]}")
            texts[rel] = texts[rel].replace(before, after)
    # Tail calls (port/abi/tail_calls.txt): the ROM's `ldr r12,=g; bx r12`
    # hands g the argument registers as the caller set them; a C body
    # `return g();` passes none. Such a body forwards four.
    tail_report = []
    tail_targets: dict[str, str] = {}
    for line in (ROOT / "port" / "abi" / "tail_calls.txt").read_text(encoding="utf-8").splitlines():
        key = line.strip()
        if not key or key.startswith("#"):
            continue
        name = key_name.get(key)
        rel = next((r for r in texts if Path(r).stem == name), None) if name else None
        if rel is None:
            tail_report.append(f"{key} {name}: no source")
            continue
        texts[rel], note = forward_tail_call(texts[rel], name)
        tail_report.append(f"{key} {name}: {note}")
        if note.startswith("forwards four words to "):
            tail_targets[name] = note.rsplit(" ", 1)[1]
    (out / "tail_calls.txt").write_text("\n".join(tail_report) + "\n", encoding="utf-8")

    names = {k: n for n, k in function_keys(modules).items()}
    findings = abi_repair.load_findings(ROOT / "port" / "abi" / "ghidra_abi.txt", names)
    # What the ROM's code inside a replaced module needs is moot: none of it
    # is built (REPLACED_MODULES).
    replaced = {n for m in modules if m.name in REPLACED_MODULES for n, _, _ in m.symbols}
    findings.calls = {k: v for k, v in findings.calls.items() if k[0] not in replaced}
    findings.returns = {k: v for k, v in findings.returns.items() if k not in replaced}
    functions = {m.name: {a: n for n, k, a in m.symbols if k == "function"} for m in modules}
    repairer = abi_repair.Repairer(texts, module_of, functions, findings)
    repairer.fixed = set(abi_fixes)

    # What each call needs is what its callee takes: its definition's argument words, more where
    # its registers pass on to a call that needs them, and a tail call's target's. Registers the
    # ROM shows a callee never reads are dropped (ABI_NOT_READ).
    def words(name: str) -> int:
        rel = repairer.def_file.get(name)
        d = abi_repair.find_definition(texts[rel], repairer.mask(rel), name) if rel else None
        if d is None:
            return 8  # no C definition to go by: keep every finding
        params = abi_repair.param_list(texts[rel], d)
        if any(p.strip() == "..." for p in params):
            return 8
        return sum(abi_repair.param_words(p) for p in params)

    involved = {f for pair in findings.calls for f in pair} | set(tail_targets) | set(tail_targets.values())
    need = {f: 0 if f in tail_targets else words(f) for f in involved}
    changed = True
    while changed:
        changed = False
        for wrapper, target in tail_targets.items():
            if need[target] > need[wrapper]:
                need[wrapper] = need[target]
                changed = True
        for (caller, callee), sites in findings.calls.items():
            for regs in sites.values():
                for k, alternatives in regs.items():
                    if k >= need[callee]:
                        continue
                    for s in alternatives:
                        if s.kind == "entry" and s.register + 1 > need[caller]:
                            need[caller] = s.register + 1
                            changed = True
    not_read = {names.get(key, key): set(registers) for key, registers in ABI_NOT_READ.items()}
    # the function a call to a shared address really reaches (SHARED_CALL_TARGETS)
    reaches: dict[tuple[str, str], str] = {}
    for caller_key, targets in SHARED_CALL_TARGETS.items():
        for address, module in targets.items():
            reached = functions.get(module, {}).get(address)
            for m in modules:
                named = functions[m.name].get(address)
                if named and reached and names.get(caller_key) and m.name != module:
                    reaches[(names[caller_key], named)] = reached

    # An empty function (`bx lr`, written to return its first argument, as
    # the decomp does and EMPTY_FUNCTION did) takes r0 only to give it back: a
    # call whose value goes nowhere needs nothing.
    def returns_first_argument(name: str) -> bool:
        rel = repairer.def_file.get(name)
        d = abi_repair.find_definition(texts[rel], repairer.mask(rel), name) if rel else None
        if d is None:
            return False
        params = abi_repair.param_list(texts[rel], d)
        first = abi_repair.param_name(params[0]) if len(params) == 1 else ""
        body = abi_repair.strip_comments_code(
            texts[rel][d.body[0] + 1 : d.body[1]], repairer.mask(rel)[d.body[0] + 1 : d.body[1]]
        )
        return (
            bool(first) and re.fullmatch(rf"\s*return\s+(?:\([^()]*\)\s*)?{re.escape(first)}\s*;\s*", body) is not None
        )

    for caller, callee in list(findings.calls):
        if not returns_first_argument(callee):
            continue
        crel = repairer.def_file.get(caller)
        d = abi_repair.find_definition(texts[crel], repairer.mask(crel), caller) if crel else None
        sites = abi_repair.call_sites(texts[crel], repairer.mask(crel), callee, d.body) if d else []
        if sites and all(abi_repair.value_unused(texts[crel], repairer.mask(crel), s, c) for s, c in sites):
            del findings.calls[(caller, callee)]
    for pair in list(findings.calls):
        sites = findings.calls[pair]
        limit = words(reaches[pair]) if pair in reaches else need[pair[1]]
        for site in list(sites):
            for k in list(sites[site]):
                if k >= limit or k in not_read.get(pair[1], ()):
                    del sites[site][k]
            if not sites[site]:
                del sites[site]
        if not sites:
            del findings.calls[pair]
    # A value no C reads needs nothing: each call a statement of its own, the argument of a callee
    # that takes fewer words, or returned where nothing reads it.
    defined_in: dict[str, list[str]] = collections.defaultdict(list)
    for fname, rel in repairer.def_file.items():
        defined_in[rel].append(fname)

    def address_taken(name: str) -> bool:
        for rel, text in texts.items():
            if name not in text:
                continue
            mask = repairer.mask(rel)
            for m in re.finditer(rf"\b{re.escape(name)}\b(?!\s*\()", text):
                if mask[m.start()]:
                    return True
        return False

    def returned_by(rel: str, text: str, mask: bytearray, start: int, close: int) -> str | None:
        """The function returning the call's value: `return f(...);`, or
        `v = f(...);` into a local it only returns."""
        if not re.match(r"\s*;", text[close + 1 :]):
            return None
        before = text[:start]
        for fname in defined_in[rel]:
            d = abi_repair.find_definition(text, mask, fname)
            if not d or not d.body[0] < start < d.body[1]:
                continue
            if re.search(r"\breturn\s*(?:\([\w\s\*]+\)\s*)*$", before):
                return fname
            v = re.search(r"\b([A-Za-z_]\w*)\s*=\s*$", before)
            if not v or not mask[v.start()]:
                return None
            body = text[d.body[0] : d.body[1]]
            for use in re.finditer(rf"\b{v[1]}\b", body):
                at = d.body[0] + use.start()
                if not mask[at] or at == v.start(1):
                    continue
                if re.search(r"\breturn\s*$", body[: use.start()]) and re.match(r"\s*;", body[use.end() :]):
                    continue
                if re.search(r"\b(?:int|u32|s32|void\s*\*)\s*$", body[: use.start()]) and re.match(
                    r"\s*;", body[use.end() :]
                ):
                    continue  # its declaration
                return None
            return fname
        return None

    def value_read(name: str, seen: tuple[str, ...] = ()) -> bool:
        for rel, text in texts.items():
            if name not in text or rel == repairer.def_file.get(name):
                continue
            mask = repairer.mask(rel)
            decls = abi_repair.declarations(text, mask, name)
            if decls and all(abi_repair.VOID_BEFORE_NAME.search(text[s:o]) for s, o, _ in decls):
                continue
            prototypes = {o for _, o, _ in decls}
            for s, c in abi_repair.call_sites(text, mask, name, (0, len(text))):
                if text.find("(", s) in prototypes or abi_repair.value_unused(text, mask, s, c):
                    continue
                argument = abi_repair.argument_of(text, mask, s, c)
                if argument and argument[0] in repairer.def_file and words(argument[0]) <= argument[1]:
                    continue
                through = returned_by(rel, text, mask, s, c)
                if (
                    through
                    and through not in seen
                    and through != name
                    and not address_taken(through)
                    and not value_read(through, seen + (name,))
                ):
                    continue
                return True
        return False

    values_unread = 0
    not_read_values = {names.get(key, key) for key in VALUE_NOT_READ}
    for name in sorted(findings.returns):
        if name in not_read_values or not value_read(name):
            values_unread += 1
            continue
        why = repairer.plan_return(name)
        if why == "return type is not plain void":
            continue  # a manual fix (ABI_FIXES) gave it its value
        if why:
            repairer.plan.gaps.append(f"{name}'s value: {why}")
    equivalent = 0
    for (caller, callee), sites in sorted(findings.calls.items()):
        ks = sorted({k for regs in sites.values() for k in regs})
        why = repairer.plan_call(caller, callee, ks[0], ks[-1] + 1)
        if why in ("struct-by-value", "resolved"):
            equivalent += 1
        elif why:
            repairer.gap_call(caller, callee, why, ks[-1] + 1)
    where = {}
    for m in modules:
        space = f"arm9_{m.name}" if m.name.startswith("ov") else "-"
        for n, k, a in m.symbols:
            if k == "function":
                where[n] = (space, a)
    new_texts = repairer.result()
    (out / "abi_next_targets.txt").write_text("".join(t + "\n" for t in repairer.next_targets(where)), encoding="utf-8")
    texts.update(new_texts)
    # Locals read unset only to add `(x - x)` start at 0.
    initialized_texts, initialized = abi_repair.init_self_subtracted(texts)
    texts.update(initialized_texts)
    # Parameters used as locals that a call does not pass: natively the
    # definition would write over its caller's frame.
    padded_texts, padded = abi_repair.pad_short_calls(texts)
    texts.update(padded_texts)
    (out / "abi_padded.txt").write_text("\n".join(padded) + "\n", encoding="utf-8")
    # Parameters declared narrower than defined: the calls pass them extended.
    widened_texts, widened = abi_repair.widen_narrow_params(texts)
    texts.update(widened_texts)
    (out / "abi_narrow_params.txt").write_text("\n".join(widened) + "\n", encoding="utf-8")
    # Results declared wider than defined: the calls extend them themselves.
    narrowed_texts, narrowed = abi_repair.narrow_return_declarations(texts)
    texts.update(narrowed_texts)
    (out / "abi_narrow_returns.txt").write_text("\n".join(narrowed) + "\n", encoding="utf-8")
    # Functions reached through pointers, whose short calls nothing pads,
    # keep their parameters' slots to themselves: compiled unoptimized.
    unoptimized_texts, callbacks = abi_repair.unoptimize_callbacks(texts)
    texts.update(unoptimized_texts)
    (out / "abi_unoptimized.txt").write_text("\n".join(callbacks) + "\n", encoding="utf-8")
    (out / "abi_repairs.txt").write_text("\n".join(repairer.plan.applied) + "\n", encoding="utf-8")
    (out / "abi_gaps.txt").write_text("\n".join(repairer.plan.gaps) + "\n", encoding="utf-8")
    abi_summary = (
        f"ABI: {len(repairer.plan.applied)} repaired, {equivalent} equivalent as the C passes them, "
        f"{values_unread} values no C reads, "
        f"{len(repairer.plan.gaps)} gaps (abi_gaps.txt), {len(padded)} short calls padded "
        f"(abi_padded.txt), {len(widened)} narrow parameters widened (abi_narrow_params.txt), "
        f"{initialized} files' `(x - x)` locals set to 0, {len(callbacks)} functions reached "
        f"through pointers in {len(unoptimized_texts)} unoptimized files (abi_unoptimized.txt)"
    )

    # References to an address several overlays share: the C names one of them, not always the
    # one loaded, so natively such a reference goes through a thunk to the loaded overlay's
    # function (runtime/core/overlays.c).
    thunks, dispatched = overlay_dispatch(modules, texts, module_of)

    # Game data at its DS address: each object the DS linker placed becomes an absolute symbol
    # there; an initialised one keeps its initializer under a second name (KHDAYS_DATA_INIT), copied
    # into place when its module loads (docs/BUILD.md).
    ds_data: dict[str, int] = {}
    for m in modules:
        for n, k, a in m.symbols:
            if k in ("data", "bss"):
                ds_data[n] = a
    converted: dict[str, int] = {}
    # C-defined globals the DS names nowhere: statics of a split unit (PHANTOMS).
    phantom_names = {
        n for n in owners if n not in ds_data and not any(n in {s for s, _, _ in m.symbols} for m in modules)
    }
    for rel in list(texts):
        text = texts[rel]
        module = module_id(module_of[rel])
        extern = 'extern "C"' if rel.endswith(".cpp") and rel not in as_c else "extern"
        for d in reversed(definitions(text)):
            name = d[2]
            if name not in ds_data:
                if name in phantom_names:
                    text = text[: d.start()] + f"{extern} {d[1]}{name}{d[3]};" + text[statement_end(text, d.start()) :]
                continue
            decl = f"{extern} {d[1]}{name}{d[3]};"
            if d[5] == "=":
                end = statement_end(text, d.start())
                text = (
                    text[: d.start()]
                    + decl
                    + "\n"
                    + f"{d[1]}{name}__khdays_init{d[3]}{d[4]} ="
                    + text[d.end() : end]
                    + f"\nKHDAYS_DATA_INIT({name}, {module})"
                    + text[end:]
                )
            else:
                text = text[: d.start()] + decl + text[d.end() :]
            converted[name] = ds_data[name]
        texts[rel] = text
    unconverted = sorted(n for n in owners if n in ds_data and n not in converted)
    (out / "unconverted_data.txt").write_text(
        "".join(f"{n}\t{' '.join(owners[n])}\n" for n in unconverted), encoding="utf-8"
    )
    pasted = separate_pasted(texts)

    lines = []
    compiled_path: dict[str, str] = {}
    patched = 0
    for src, rel in compiled:
        text = texts[rel]
        original = src.read_text(encoding="utf-8", errors="replace")
        path = src
        if text != original:
            patched += 1
            path = patched_dir / (rel[:-4] + ".c" if rel in as_c else rel)
            path.parent.mkdir(parents=True, exist_ok=True)
            # Rewrite only on change, so the build does not recompile it.
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                path.write_text(text, encoding="utf-8")
        lines.append(str(path).replace("\\", "/"))
        compiled_path[rel] = lines[-1]

    # The functions at their DS addresses, for a call through a DS address the ROM's data holds
    # (runtime/core/window.c, overlays.c). The ITCM's are listed with their sizes: the MobiClip
    # decoder is copied over the ITCM while a movie plays.
    # the names compat/decomp_defines.txt gives the game's functions natively
    # (main, the C runtime's, the system's): each listed under its native name
    renames = {
        m[1]: m[2]
        for m in re.finditer(
            r"^(\w+)=(\w+)\s*$", (ROOT / "port" / "compat" / "decomp_defines.txt").read_text(encoding="utf-8"), re.M
        )
    }
    hal_names_all = hal_definitions()
    ds_functions: list[tuple[int, int, str]] = []
    for m in modules:
        if m.name == "dtcm":
            continue
        ds_module = -1 if m.name == "main" else -2 if m.name == "itcm" else int(m.name[2:])
        native: set[str] = set()
        for rel in m.files:
            text = texts.get(rel)
            if text is None:
                continue
            text = strip_comments(text)
            # a definition at a line's start or right after the previous
            # one's brace (`}int Ov022_StepGlideState(...)`), and one returning
            # a function pointer (`void (*Ov002_CaptureRootHeap(void))(void)`)
            for d in re.finditer(
                r"(?:^|\})[ \t]*(?!static\b|extern\b|typedef\b)[A-Za-z_][\w \t\*]*?"
                r"(?:\(\s*\*\s*([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\)\s*\([^;{}]*\)|"
                r"\b([A-Za-z_]\w*)\s*\([^;{}]*\))\s*\{",
                text,
                re.MULTILINE,
            ):
                if rel.endswith(".cpp") and rel not in as_c and not re.search(r'extern\s*"C"\s*\{', text[: d.start()]):
                    continue  # C++ linkage
                native.add(d[1] or d[2])
        for n, k, a in m.symbols:
            # the HAL's too, of any module (the ITCM's OS_IrqHandler, which the
            # BIOS vector names by its DS address)
            if k == "function" and (n in native or n in hal_names_all):
                ds_functions.append((a, ds_module, n))
    ds_functions.sort()
    sizes = {n: sz for m in modules for n, sz in m.sizes.items()}
    ds_c = [
        "/* Generated by port/tools/prepare.py: each native function of the main module,",
        " * the ITCM and the overlays at its DS address, and its size (runtime/core/overlays.c). */",
        "typedef struct { unsigned address; int module; unsigned size; void (*native)(void); } KhdaysDsFunction;",
    ]
    ds_c += [f"extern void {renames.get(n, n)}(void);" for n in sorted({n for _, _, n in ds_functions})]
    ds_c.append("const KhdaysDsFunction khdays_ds_functions[] = {")
    ds_c += [f"    {{0x{a:08x}, {mod}, 0x{sizes.get(n, 0):x}, {renames.get(n, n)}}}," for a, mod, n in ds_functions]
    ds_c += ["};", f"const unsigned khdays_ds_function_count = {len(ds_functions)};", ""]
    ds_path = out / "ds_functions.c"
    ds_text = "\n".join(ds_c)
    if not ds_path.exists() or ds_path.read_text(encoding="utf-8") != ds_text:
        ds_path.write_text(ds_text, encoding="utf-8", newline="\n")

    # Functions the HAL must provide: the symbols inside the assembly-only files.
    hal_syms = []
    for mod_name, rel in hal_files:
        stem = Path(rel).stem
        hal_syms.append(f"{mod_name}\t{stem}\t{rel}")
    (out / "hal_symbols.txt").write_text("\n".join(hal_syms) + "\n", encoding="utf-8")

    # Symbols the DS linker placed, emitted as ABSOLUTE symbols at their DS
    # addresses: the native process maps the DS memory at the same virtual
    # addresses, so BSS keeps its exact layout (overlays sharing an address
    # range alias each other, as on the DS), and literal addresses in the C
    # meet the same objects the symbols name.
    defined = set(owners) - set(converted) - phantom_names
    absolute: dict[str, int] = dict(converted)
    absolute.update(PHANTOMS)
    for m in modules:
        if ".bss" not in m.sections:
            continue
        start, end = m.sections[".bss"]
        for n, k, a in m.symbols:
            if start <= a < end and n not in defined:
                absolute[n] = a
    bss_count = len(absolute) - len(converted) - len(PHANTOMS)
    # The decomp's own linker-absolute symbols (tools/configure.py). A data_
    # entry that another, C-defined symbol shares the address of is an alias
    # of that table: point it there instead (the table's bytes live wherever
    # MSVC put the C definition, not at the DS address).
    by_addr = collections.defaultdict(list)
    for m in modules:
        for n, k, a in m.symbols:
            by_addr[a].append(n)
    alternates: dict[str, str] = {}
    for name, addr in decomp_absolute_symbols().items():
        target = next((n for n in by_addr.get(addr, []) if n != name and n in defined), None)
        if target is not None:
            alternates[name] = target
        else:
            absolute[name] = addr
    # What only an assembly file holds and port/hal does not implement is the ROM's bytes at its DS
    # address: data is read there; a call into code there stops the game, naming it. rom_code.txt
    # lists what may be code (docs/BUILD.md).
    # A Thumb function's address carries bit 0, as an ARM pointer to it does.
    hal_names = hal_definitions()
    by_name = {m.name: m for m in modules}
    rom_run: list[str] = []
    for mod_name, rel in hal_files:
        m = by_name[mod_name]
        for start, end in m.ranges.get(rel, []):
            for n, k, a in m.symbols:
                if start <= a < end and n not in hal_names and n not in absolute and n not in defined:
                    absolute[n] = a | (1 if n in m.thumb else 0)
                    if k == "function" or not rel.endswith(".s") or "_unk" in n:
                        rom_run.append(n)
    (out / "rom_code.txt").write_text("".join(f"{n}\n" for n in sorted(rom_run)), encoding="utf-8")
    # FS_OVERLAY_ID(n) is the ADDRESS of the linker symbol OVERLAY_n_ID.
    for d in sorted((CONFIG / "overlays").iterdir()):
        mo = re.fullmatch(r"ov(\d+)", d.name)
        if mo:
            absolute[f"OVERLAY_{int(mo[1])}_ID"] = int(mo[1])

    if alternates:
        raise SystemExit(
            "references: second names of C-defined tables have no reference yet: " + ", ".join(sorted(alternates)[:20])
        )
    # A function reference is its DS address where the runtime finds the
    # native function by it (ds_functions.c; an overlay's while it is
    # loaded there, an address overlays share through its thunk); one
    # with no native form there, the ROM's code, stops the game when
    # called. Every other function the build numbers.
    functions = {n: a for a, _, n in ds_functions}
    # the rewritten C names a renamed function by its native name
    functions.update({renames[n]: a for a, _, n in ds_functions if n in renames})
    functions.update({n: absolute[n] for n in rom_run if n in absolute})
    functions.update({f"khdays_ovcall_{target:08x}": target for target in thunks})
    # The game's routines and data by their decompilation names, for the mods' native plugins (kh.engine:
    # port/runtime/plugins/engine.c), each table sorted by name: a routine's native function and module, a datum's
    # reference (where the game keeps it). The overlays' OVERLAY_n_ID are numbers, not data.
    routine_names: dict[str, tuple[str, int]] = {}
    for _, mod, n in ds_functions:
        routine_names.setdefault(n, (renames.get(n, n), mod))
    datum_names = {n: a for n, a in absolute.items() if not re.fullmatch(r"OVERLAY_\d+_ID", n) and n not in functions}
    names_c = [
        "/* Generated by port/tools/prepare.py: the game's routines and data by name, sorted by name, for the mods'",
        " * native plugins (port/runtime/plugins/engine.c). */",
        "typedef struct { const char *name; void (*native)(void); int module; } KhdaysRoutineName;",
        "typedef struct { const char *name; unsigned reference; } KhdaysDatumName;",
    ]
    names_c += [f"extern void {native}(void);" for native in sorted({v[0] for v in routine_names.values()})]
    names_c.append("const KhdaysRoutineName khdays_routine_names[] = {")
    names_c += [f'    {{"{n}", {native}, {mod}}},' for n, (native, mod) in sorted(routine_names.items())]
    names_c += ["};", f"const unsigned khdays_routine_name_count = {len(routine_names)};"]
    names_c.append("const KhdaysDatumName khdays_datum_names[] = {")
    names_c += [f'    {{"{n}", 0x{a & 0xFFFFFFFF:08x}u}},' for n, a in sorted(datum_names.items())]
    names_c += ["};", f"const unsigned khdays_datum_name_count = {len(datum_names)};", ""]
    points_path = out / "khdays_hook_points.h"
    points_text = hook_points_header()
    if not points_path.exists() or points_path.read_text(encoding="utf-8") != points_text:
        points_path.write_text(points_text, encoding="utf-8", newline="\n")
    names_path = out / "ds_names.c"
    names_text = "\n".join(names_c)
    if not names_path.exists() or names_path.read_text(encoding="utf-8") != names_text:
        names_path.write_text(names_text, encoding="utf-8", newline="\n")
    c_sources = [
        (compiled_path[rel], rel[:-4] + ".c" if rel in as_c else rel)
        for _, rel in compiled
        if not rel.endswith(".cpp") or rel in as_c
    ]
    # The static initializers' table is the DS linker's .ctor, read where
    # the static module's image has it (runtime/core/start.c defines one only
    # for the build without references).
    placed = dict(absolute, ARM9_CTOR_START=by_name["main"].sections[".ctor"][0])
    # direct calls to an address several overlays share (their thunks) and
    # into the ROM's code reach it as a call through its DS address does
    dispatched_calls = {n: a for n, a in functions.items() if n.startswith("khdays_ovcall_")}
    dispatched_calls.update({n: absolute[n] for n in rom_run if n in absolute})
    rewritten, area = rewrite_game(out, c_sources, placed, functions, dispatched_calls)
    # the port's side calls the game's functions the host's way: none of
    # those taking their arguments in a block (references/emit.py)
    block = set((out / "references_block_abi.txt").read_text(encoding="utf-8").split())
    port_calls = []
    for source in sorted(p for d in ("runtime", "host", "gfx", "sound") for p in (ROOT / "port" / d).rglob("*.c*")):
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))
        port_calls += [f"{source.name}: {n}" for n in sorted(block) if re.search(rf"\b{re.escape(n)}\s*\(", text)]
    if port_calls:
        raise SystemExit(
            "references: the port calls game functions that take their arguments in a block: " + ", ".join(port_calls)
        )
    missing = [n for n in PORT_DS_SYMBOLS if n not in ds_data]
    if missing:
        raise SystemExit(f"references: the port reads {', '.join(missing)}, which the decomp places nowhere")
    symbols_h = (
        "/* Generated by port/tools/prepare.py: the DS addresses of the\n"
        " * game's objects the port's side reads (PORT_DS_SYMBOLS). */\n"
        "#ifndef KHDAYS_DS_SYMBOLS_H\n#define KHDAYS_DS_SYMBOLS_H\n"
        + "".join(f"#define KHDAYS_DS_{n} 0x{ds_data[n]:08x}u\n" for n in PORT_DS_SYMBOLS)
        + "#endif\n"
    )
    target = out / "khdays_ds_symbols.h"
    if not target.exists() or target.read_text(encoding="utf-8") != symbols_h:
        target.write_text(symbols_h, encoding="utf-8")
    cmake = cmake_list("KHDAYS_REWRITTEN", rewritten)
    rewritten_summary = (
        f"; rewritten with references: {len(rewritten)} files ({pasted} pasted tokens "
        f"separated), a data area of 0x{area:x} bytes"
    )
    target = out / "sources.cmake"
    if not target.exists() or target.read_text(encoding="utf-8") != cmake:
        target.write_text(cmake, encoding="utf-8")

    print(
        f"{len(compiled)} sources ({patched} patched), "
        f"{len(hal_files)} assembly-only files for the HAL, "
        f"{len(orphans)} unlisted data sources taken in (orphan_data.txt), "
        f"{len(duplicates)} duplicated globals resolved; at DS addresses: "
        f"{len(converted)} C-defined data objects ({len(unconverted)} left in the image, "
        f"see unconverted_data.txt), {bss_count} other BSS symbols, "
        f"{len(PHANTOMS)} split-unit statics, "
        f"{len(absolute) - bss_count - len(converted) - len(PHANTOMS)} other absolute symbols; "
        f"{len(alternates)} aliases; {abi_summary}; "
        f"{len(thunks)} addresses shared by overlays' functions, reached through thunks from "
        f"{dispatched} files{rewritten_summary}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
