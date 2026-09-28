#!/usr/bin/env python3
"""Prepare the pinned khdays-decomp for a native 32-bit build.

The decomp is consumed read-only. This script decides which sources build
the game, writes patched copies of the few that need a mechanical change
for MSVC, and generates what the DS linker used to provide:

  * sources.cmake      -- every compiled source (original path or patched copy)
  * hal_symbols.txt    -- files whose only source is ARM assembly; native/hal
                          implements what they define
  * duplicates.txt     -- globals several split files defined; one keeps the
                          definition, the rest become extern
  * absolute.asm       -- symbols at fixed DS addresses (MASM, x86): the BSS no
                          C file defines, OVERLAY_n_ID and the decomp's own
                          linker-absolute symbols
  * alternatenames.txt -- /ALTERNATENAME link options for second names of a
                          C-defined table

The source list comes from each module's delinks.txt: it names exactly the
files the DS build links, so every function has one provider.

Usage: python native/tools/prepare.py --out build/native/gen
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

ROOT = Path(__file__).resolve().parents[2]
DECOMP = ROOT / "external" / "khdays-decomp"
CONFIG = DECOMP / "config" / "arm9"

SECTION_RE = re.compile(
    r"^\s+(\.\w+)\s+start:0x([0-9a-fA-F]+)\s+end:0x([0-9a-fA-F]+)")
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
    re.MULTILINE)
# An object-like macro with a plain number as its value.
NUMERIC_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+\(?(0[xX][0-9a-fA-F]+|\d+)[uU]?\)?[ \t]*$",
                            re.MULTILINE)
# `*(T (*)[N])dst = *(T (*)[N])src;` -- assigning a whole array, which mwcc
# accepts and C does not; copied element by element instead.
ARRAY_ASSIGN = re.compile(
    r"^([ \t]*)\*\((\w[\w \t]*?)[ \t]*\(\*\)\[([^\]\n]+)\]\)([^=\n]+?)[ \t]*=[ \t]*"
    r"\*\(\2[ \t]*\(\*\)\[\3\]\)([^;\n]+);", re.MULTILINE)
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
    r"(?=[A-Za-z_]\w*[ \t]*\([^;{}]*\)[ \t]*\n?[ \t]*\{)", re.MULTILINE)
# An empty function (`bx lr` in the ROM) leaves r0 as its caller set it, and
# callers that read the result get their own first argument back -- the
# object dispatcher relies on it (an empty update keeps itself). Natively it
# returns its first argument, which it now takes.
EMPTY_FUNCTION = re.compile(r"^void[ \t]+(\w+)[ \t]*\([ \t]*(?:void)?[ \t]*\)[ \t]*\{[ \t]*\}[ \t]*$", re.MULTILINE)


# Where the runtime joins the game's own flow, each a single, documented edit
# (function -> (text, replacement)); a missing text stops the script. A
# function is named by its key, module@address (function_keys), which stays
# put when the decomp renames it; the text must match its current source.
HOOKS = {
    # An overlay's data: its image has just been loaded and started; where the
    # SDK runs the overlay's static initializers, the native data initializers
    # of that overlay go over the image (runtime/data_init.c).
    "main@0200b4b8": (  # FS_StartOverlay
        "FSOverlayInitFunc *p = p_ovi->header.sinit_init;",
        "FSOverlayInitFunc *p = (khdays_data_init((int)p_ovi->header.id), "
        "p_ovi->header.sinit_init);",
    ),
    # An object's update returns its next update. The ROM calls it with the
    # update's own address in r0 (0x02023b60 `ldr r0,[r1,#0x14]`, `blx r0`),
    # so one that leaves r0 alone -- an empty one, see EMPTY_FUNCTION -- keeps
    # itself; natively it gets that address as its argument. Whatever else it
    # returns must be native code (or -1/-2): anything not is caught here,
    # naming the update that returned it (runtime/diag.c).
    "main@02023adc": (  # Obj_UpdateAll
        "                int cb = ((int (*)(void))((int *)data_0204c058[1])[5])();\n",
        "                int cb = ((int (*)(int))((int *)data_0204c058[1])[5])(((int *)data_0204c058[1])[5]);\n"
        "                { extern void khdays_check_update(int fn, int next);\n"
        "                  khdays_check_update(((int *)data_0204c058[1])[5], cb); }\n",
    ),
    # KHDAYS_TRACE_SCRIPT: the action-script interpreter reports each step it
    # stands on (runtime/diag.c), to see what a scene waits for.
    "main@02020e58": (  # Game_RunActionScript
        "    cur = st + 4 + *(int *)(st + 0x124) * 0x48;\n",
        "    cur = st + 4 + *(int *)(st + 0x124) * 0x48;\n"
        "    { extern void khdays_trace_script(void *st, void *entry); khdays_trace_script(st, cur); }\n",
    ),
}


# Calls whose native form the ROM shows but no mechanical rule gives (see
# abi_repair.py for the rules): file -> [(text, replacement)], each read from
# the function's disassembly. Applied before the mechanical repairs. Keyed
# like HOOKS.
ABI_FIXES = {
    # The ARM ABI returns this two-word struct through a pointer in r0, which
    # the definition spells out; its callers declare the NitroSDK form
    # `FSFileID FS_GetOverlayFileID(const FSOverlayInfo *)`. The ROM
    # (0x0200b178) stores {&rom archive, overlay->file_id (+0x18)} through r0.
    "main@0200b178": [(  # FS_GetOverlayFileID
        "void FS_GetOverlayFileID(FsOverlayInfo *dst, int *overlay) {\n"
        "    FsOverlayInfo info;\n"
        "    info.a = (int)&data_02046334;\n"
        "    info.b = overlay[6];\n"
        "    *dst = info;\n"
        "}",
        "FsOverlayInfo FS_GetOverlayFileID(int *overlay) {\n"
        "    FsOverlayInfo info;\n"
        "    info.a = (int)&data_02046334;\n"
        "    info.b = overlay[6];\n"
        "    return info;\n"
        "}",
    )],
    # NNSi_G2dFontGetTextRect returns its two-word rect by value; both callers
    # (Text_AlignAnchor, Ov002_SceneLayoutPanelWindow) pass the ARM ABI's
    # result pointer themselves. MSVC returns an 8-byte struct in EDX:EAX
    # instead, so the definition takes the pointer as they do.
    "main@0201386c": [  # NNSi_G2dFontGetTextRect
        ("NNSG2dTextRect NNSi_G2dFontGetTextRect (const NNSG2dFont * pFont, int hSpace, int vSpace, "
         "const void * txt)",
         "void NNSi_G2dFontGetTextRect (NNSG2dTextRect * khdays_result, const NNSG2dFont * pFont, "
         "int hSpace, int vSpace, const void * txt)"),
        ("    return rect;\n}", "    *khdays_result = rect;\n}"),
    ],
    # Ov002_IsPanelModeSet(fallback) returns its r0 untouched when no panel is
    # installed (`ldr r1,=0x0207f628; ldr r1,[r1]; cmp r1,#0; bxeq lr`,
    # 0x02061b80); here that r0 is whatever Ov002_RepublishHud left, several
    # calls deep. With a panel it is not read; without one, stop there.
    "ov002@02069d40": [  # Ov002_CreateAndRestoreHud
        ("extern int Ov002_IsPanelModeSet(void);",
         "extern int Ov002_IsPanelModeSet(int fallback);\n"
         "extern void *data_ov002_0207f628;\n"
         "extern void khdays_abi_gap(const char *what);"),
        ("    if (Ov002_IsPanelModeSet()) {",
         "    if (Ov002_IsPanelModeSet(data_ov002_0207f628 != 0 ? 0 :\n"
         "            (khdays_abi_gap(\"Ov002_IsPanelModeSet's fallback with no panel: what \"\n"
         "                            \"Ov002_RepublishHud left in r0\"), 0))) {"),
    ],
}

# A node's +0x80 hook gets the node: `ldr r1,[r0,#0x80]; ...; blx r1` with r0
# still the node (0x0203c8e8-0x0203c8f4). ProcessListChildren, a hook, reads
# it. (A call through a pointer: the ROM scan does not see these.)
ABI_FIXES["main@0203c8e4"] = [  # Node_CallHook80
    ("    return f();\n", "    return f(p);\n"),
]

# r9 across the object updates (runtime.c khdays_rom_r9): Obj_UpdateAll
# starts with main's (unknown) and leaves each update's result in it
# (`blx r0; mov sb, r0`, 0x02023b64/68).
ABI_FIXES["main@02023adc"] = [  # Obj_UpdateAll
    ("void Obj_UpdateAll(int paused)\n{",
     "extern void khdays_rom_r9_unknown(void), khdays_rom_r9_set(int value);\n"
     "void Obj_UpdateAll(int paused)\n{"),
    ("    data_0204c058[1] = data_0204c058[3];\n",
     "    khdays_rom_r9_unknown();  /* main's r9 (native/abi) */\n"
     "    data_0204c058[1] = data_0204c058[3];\n"),
    ("                  khdays_check_update(((int *)data_0204c058[1])[5], cb); }\n",
     "                  khdays_check_update(((int *)data_0204c058[1])[5], cb); }\n"
     "                khdays_rom_r9_set(cb);  /* `mov sb, r0` */\n"),
]
# The tag trackers' `swap` is r9 (sb), which the ROM never sets when the table
# has no selection getter (+0x48): it reads what its caller left there (the
# decomp's header calls it a ROM bug, kept). The eight copies are the same
# 0x1a0 bytes; in Ov025's, reached from the camp menu's update through
# Ov025_GetIdleHandler, Ov025_CommitPage and Ov025_TickSelectionWidget, none
# of which touches r9, that is the previous update's result (0 in the
# Holomisiones list). With a nonzero r9 and no setter (+0x44) the ROM calls
# address 0 -- the ITCM mirror, OSi_VBlankInterruptHandler: the port stops.
for _key in ("ov000@020561b4", "ov002@020540f0", "ov005@0204cdd0", "ov006@0204cdd0",
             "ov008@020551a4", "ov009@02051f68", "ov025@02088e4c", "ov026@02082ecc"):
    ABI_FIXES[_key] = [
        ("    int swap;\n",
         "    extern int khdays_rom_r9(const char *reader);\n"
         "    extern void khdays_abi_gap(const char *what);\n"
         "    int swap = *(int *)(param_1 + 0x48) != 0 ? 0 : khdays_rom_r9(\n"
         "        \"a tag tracker's swap without a getter: r9 before the frame's first update\");\n"),
        ("        if (swap != 0) {\n            (*(void (**)(int))(param_1 + 0x44))(*(int *)(e + 0x28));",
         "        if (swap != 0) {\n"
         "            if (*(int *)(param_1 + 0x44) == 0) {\n"
         "                khdays_abi_gap(\"a tag tracker's setter at address 0 (the ROM runs the ITCM there)\");\n"
         "            }\n"
         "            (*(void (**)(int))(param_1 + 0x44))(*(int *)(e + 0x28));"),
    ]

# Calls to an address several overlays share, where the function the ROM
# reaches is known: caller -> address -> the overlay loaded there. What the
# call needs is what that function takes, not the one the C names.
SHARED_CALL_TARGETS = {
    # The opening movie: the call at 0x0205b1f0 reaches ov024's
    # Ov024_MobiClip_OpenStreams(request), not ov008's Ov008_RebuildShopList
    # (confirmed at run time; docs/DECOMP_FINDINGS.md section 1).
    "ov012@0205b0cc": {0x020846c0: "ov024"},
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
# (The C of these uses the parameters as locals: the calls pass zeros for
# them, abi_repair.pad_short_calls, so the writes stay in the call's frame.)


# Globals some split files define that symbols.txt does not name: statics of
# one original translation unit, repeated in each file the decomp split it
# into. On the DS they are fields of a block the module's own symbol anchors
# (tools/share_bss.py of the decomp). Their definitions become references;
# these are the ones code uses, at the addresses the ROM's own code reaches
# them by (base + offset read from its disassembly). A used one missing here
# fails the link.
PHANTOMS = {
    # os_thread.c, block data_0204430c: OSi_RescheduleThread (+0x04), the
    # scheduler's current-thread pointer (func_02001f10: ldr [base,#8]),
    # OS_ExitThread's destructor stack (func_02001e0c: ldr [base,#0x1c]),
    # the thread id counter (func_020018ec: ldr/str [base,#0x20]).
    "OSi_RescheduleCount": 0x02044310,
    "OSi_CurrentThreadPtr": 0x02044314,
    "OSi_IsThreadInitialized": 0x02044318,
    "OSi_StackForDestructor": 0x02044328,
    "OSi_ThreadIdCount": 0x0204432c,
    # snd_command.c, block data_02044748 (func_020085f0: [base,#4] finished
    # tag, #0x10 free-list end, #0x14 queue read index, #0x1c waiting count;
    # func_0200851c: #0x8/#0xc reserve list and its end, #0x18 queue write
    # index, #0x20 current tag = 1).
    "sFinishedTag": 0x0204474c,
    "sReserveList": 0x02044750,
    "sReserveListEnd": 0x02044754,
    "sFreeListEnd": 0x02044758,
    "sWaitingCommandListQueueRead": 0x0204475c,
    "sWaitingCommandListQueueWrite": 0x02044760,
    "sWaitingCommandListCount": 0x02044764,
    "sCurrentTag": 0x02044768,
    # nns snd_resource.c, block data_0204a2fc (func_02019d4c: [base,#4] alarm
    # lock; func_02019cb8: [base,#8] channel lock).
    "sAlarmLock": 0x0204a300,
    "sChannelLock": 0x0204a304,
    # nns snd_stream.c, block data_0204ad8c (func_0201d0e0: [base,#4] prepare
    # thread; func_0201c8f4: str sDecodeBufferArea -> [base,#8]).
    "sPrepareThread": 0x0204ad90,
    "sDecodeBuffer": 0x0204ad94,
    # ov105 wh.c, block data_ov105_020c04c0 (func_ov105_020bf7f4: #0x28 receive
    # size, #0x2c send size, #0x14 receive buffer, #0x1c send buffer, #0x34
    # connect mode, #0x40 child WEP key generator; func_ov105_020bf5a8: #0x4c
    # WM buffer, #0x20 receiver, #0x30 error code, strh #0xc disconnect reason,
    # #0x38 accept judge; func_ov105_020bf704: strh #0x4 MP frequency).
    "sWh_nMpFreq": 0x020c04c4,
    "sWh_nDisconnectReason": 0x020c04cc,
    "sWh_pRecvBuffer": 0x020c04d4,
    "sWh_pSendBuffer": 0x020c04dc,
    "sWh_pReceiver": 0x020c04e0,
    "sWh_nRecvBufferSize": 0x020c04e8,
    "sWh_nSendBufferSize": 0x020c04ec,
    "sWh_nErrCode": 0x020c04f0,
    "sWh_nConnectMode": 0x020c04f4,
    "sWh_pJudgeAccept": 0x020c04f8,
    "sWh_pChildWEPKeyGenerator": 0x020c0500,
    "sWh_pWmBuffer": 0x020c050c,
}


def forward_tail_call(text: str, name: str) -> tuple[str, str]:
    """`T name() { return g(); }` for a ROM tail call: take and pass on four
    argument words (cdecl: a caller that pushed fewer leaves its own frame in
    the extra ones, which g does not read). Returns the text and what was
    done."""
    body = re.search(rf"(\b{re.escape(name)}\s*\()\s*(?:void)?\s*(\)\s*\{{\s*(?:return\s+)?)"
                     rf"(\w+)\s*\(\s*\)(\s*;\s*\}})", text)
    if body is None:
        return text, "left (the C passes its arguments or is not a lone call)"
    callee = body[3]
    before = text[:body.start()]
    decl = None
    for decl in re.finditer(rf"\b{re.escape(callee)}\s*\(([^()]*)\)\s*;", before):
        pass
    if decl is not None and decl[1].strip() not in ("", "void"):
        return text, f"left ({callee} is declared with parameters)"
    words = ", ".join(f"khdays_r{i}" for i in range(4))
    params = ", ".join(f"int khdays_r{i}" for i in range(4))
    new = f"{body[1]}{params}{body[2]}{callee}({words}){body[4]}"
    text = before + new + text[body.end():]
    # declarations of either that say `(void)` would contradict the call and
    # the definition
    for fn in (callee, name):
        text = re.sub(rf"(\b{re.escape(fn)}\s*\()\s*void\s*(\)\s*;)", r"\1\2", text)
    return text, f"forwards four words to {callee}"


def zero_size(expr: str, defines: dict[str, str]) -> bool:
    expr = re.sub(r"\b[A-Za-z_]\w*\b",
                  lambda m: defines.get(m[0], m[0]), expr)
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
        unnamed = p.endswith("*") or len(tokens) <= 1 or tokens[-1] in (
            "int", "char", "short", "long", "u8", "u16", "u32", "s8", "s16", "s32",
            "fx32", "fx16", "BOOL", "void", "unsigned", "signed")
        fixed.append(f"{p} _unnamed{i}" if unnamed else p)
    return f"{match[1]}{', '.join(fixed)})"


def transform(text: str) -> str:
    text = SPLIT_HEADER.sub(r"\1 ", text)
    text = REGISTER_ASM.sub("", text)
    text = CLZ_ASM.sub(lambda m: f"{m[1]} = khdays_clz({m[2]});", text)
    text = END_LABEL.sub(lambda m: f"{m[1]};{m[2]}", text)
    text = DSPROT_RANGES_INCLUDE.sub(r'\1"khdays_dsprot_ranges.h"', text)
    text = VIRTUAL_METHOD.sub(r"\1__cdecl \2", text)
    text = EMPTY_FUNCTION.sub(r"void *\1(void *khdays_r0) { return khdays_r0; }  /* bx lr: r0 unchanged */", text)
    text = VIRTUAL_DTOR.sub(r"\1virtual void __cdecl khdays_mwcc_complete_dtor();  /* mwcc's slot 0 */\n\1\2",
                            text)
    text = DELETE_STATEMENT.sub(
        r"\1{ void *khdays_p = (void *)(\2); if (khdays_p) "
        r"((void (__cdecl *)(void *))(*(void ***)khdays_p)[1])(khdays_p); }", text)
    text = ARRAY_ASSIGN.sub(
        lambda m: f"{m[1]}KHDAYS_ARRAY_ASSIGN({m[2].strip()}, {m[3]}, {m[4].strip()}, {m[5].strip()});",
        text)
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
# MSVC returns it in AL/AX and leaves the rest of EAX as it was. So the
# definition returns an int holding the narrowed value, as r0 does.
NARROW_TYPE = r"(?:u8|s8|u16|s16|(?:unsigned\s+|signed\s+)?char|(?:unsigned\s+|signed\s+)?short(?:\s+int)?)"
NARROW_DEFINITION = re.compile(
    rf"^((?:static\s+)?)({NARROW_TYPE})(\s+)(\w+)(\s*\([^;{{}}]*\)\s*\{{)", re.MULTILINE)
RETURN_VALUE = re.compile(r"\breturn\b(\s*)([^;]+?)\s*;")


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
        body = RETURN_VALUE.sub(lambda r: f"return{r[1]}({narrow})({r[2]});", text[m.end():end])
        out.append(text[pos:m.start()])
        out.append(f"{m[1]}int{m[3]}{name}{m[5]}{body}")
        pos = end
        widened.append(name)
    out.append(text[pos:])
    text = "".join(out)
    # the same functions' prototypes in this file say so too
    for name in widened:
        text = re.sub(rf"^(\s*(?:extern\s+|static\s+)?){NARROW_TYPE}(\s+{re.escape(name)}\s*\([^;{{}}]*\)\s*;)",
                      r"\1int\2", text, flags=re.MULTILINE)
    return text


# --- File-scope definitions (for duplicates and BSS) --------------------------

# A file-scope object definition header, on one line: `T name[..] =` or `T name;`.
DEFINITION_RE = re.compile(
    r"^(?!extern\b|static\b|typedef\b|return\b|union\b|enum\b|#)"
    r"([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)((?:[ \t]*\[[^\]\n]*\])*)"
    r"((?:[ \t]*__attribute__[ \t]*\(\([^\n]*?\)\))?)[ \t]*(=|;)",
    re.MULTILINE)
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
    """What native/hal defines: functions, data and KHDAYS_HAL_TODO stubs."""
    names: set[str] = set()
    for source in (ROOT / "native" / "hal").glob("*.c"):
        text = strip_comments(source.read_text(encoding="utf-8"))
        names.update(re.findall(r"KHDAYS_HAL_TODO\((\w+)\)", text))
        names.update(re.findall(r"^(?!extern\b|static\b|typedef\b|#)[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)"
                                r"[ \t]*[\(\[=;]", text, re.MULTILINE))
    return names


def function_keys(modules: list[Module]) -> dict[str, str]:
    """Each function's key, `module@address`: what native/abi and the manual
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
    block = text[text.index("ABSOLUTE_SYMBOLS = {"):]
    block = block[:block.index("}")]
    return {n: int(v, 16) for n, v in re.findall(r'"(\w+)":\s*0x([0-9A-Fa-f]+)', block)}


RELOC_OVERLAYS_RE = re.compile(
    r"^from:0x[0-9a-fA-F]+\s+kind:\w+\s+to:0x([0-9a-fA-F]+)\s+module:overlays\(([\d,\s]+)\)")


def overlay_dispatch(modules: list[Module], texts: dict[str, str], module_of: dict[str, str]
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
            candidates = [(o, function_at[(o, target)]) for o in
                          (int(x) for x in r[2].replace(" ", "").split(",")) if (o, target) in function_at]
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
        used = [n for n in names if re.search(rf"\b{re.escape(n)}\b", text) and
                # not the file defining it (a definition starts at column 0)
                not re.search(rf"^[A-Za-z_][\w \t\*]*\b{re.escape(n)}\s*\([^;{{}}]*\)\s*\{{", text,
                              re.MULTILINE)]
        if used:
            texts[rel] = "".join(f"#define {n} {names[n]}  /* the overlay loaded there (prepare.py) */\n"
                                 for n in sorted(used)) + text
            files += 1
    return thunks, files


def thunk_asm(thunks: dict[int, list[tuple[int, str]]]) -> list[str]:
    """x86 thunks: ask which overlay is loaded at the address, jump to its
    function with the caller's arguments and return address untouched."""
    if not thunks:
        return []
    asm = ["EXTERN _khdays_overlay_at:PROC", "EXTERN _khdays_overlay_call_missing:PROC"]
    for name in sorted({n for c in thunks.values() for _, n in c}):
        asm.append(f"EXTERN _{name}:PROC")
    asm += ["", ".code"]
    for target, candidates in sorted(thunks.items()):
        thunk = f"_khdays_ovcall_{target:08x}"
        asm += [f"PUBLIC {thunk}", f"{thunk} PROC",
                f"    push 0{target:08x}h", "    call _khdays_overlay_at", "    add esp, 4"]
        for overlay, name in candidates:
            asm += [f"    cmp eax, {overlay}", f"    jne @F", f"    jmp _{name}", "@@:"]
        asm += [f"    push 0{target:08x}h", "    call _khdays_overlay_call_missing", f"{thunk} ENDP", ""]
    return asm


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build" / "native" / "gen"))
    args = ap.parse_args()
    out = Path(args.out)
    patched_dir = out / "patched"
    patched_dir.mkdir(parents=True, exist_ok=True)

    modules = load_modules()
    compiled: list[tuple[Path, str]] = []   # (source, original relative path)
    hal_files: list[tuple[str, str]] = []  # (module, relative path)
    texts: dict[str, str] = {}
    module_of: dict[str, str] = {}
    # The manual edits by the file of the function they name (one function per
    # file, named after it).
    key_name = {k: n for n, k in function_keys(modules).items()}
    for key in list(HOOKS) + list(ABI_FIXES):
        if key not in key_name:
            raise SystemExit(f"no function at {key} (prepare.py HOOKS / ABI_FIXES)")
    hooks = {key_name[k]: v for k, v in HOOKS.items()}
    abi_fixes = {key_name[k]: v for k, v in ABI_FIXES.items()}
    for m in modules:
        for rel in m.files:
            src = DECOMP / rel
            if "asm_stubs" in rel or src.suffix == ".s" or not src.exists():
                hal_files.append((m.name, rel))
                continue
            text = src.read_text(encoding="utf-8", errors="replace")
            # Anything still carrying ARM assembly after the transforms is
            # hardware-level code the HAL replaces.
            new = transform(text)
            if Path(rel).stem in hooks:
                before, after = hooks[Path(rel).stem]
                if before not in new:
                    raise SystemExit(f"hook text not found in {rel}: {before}")
                new = "extern void khdays_data_init(int module);\n" + new.replace(before, after)
            if re.search(r"\basm\b|__asm\b", strip_comments(new)):
                hal_files.append((m.name, rel))
                continue
            texts[rel] = new
            module_of[rel] = m.name
            compiled.append((src, rel))

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
        names = [n for n in re.findall(r"(?:^|\})[ \t\w\*]*?\b(data_\w+)\s*(?:\[[^\]\n]*\])*\s*=",
                                       text, re.MULTILINE)
                 if n in owner_module and n not in provided]
        if names:
            texts[rel] = text
            module_of[rel] = owner_module[names[0]]
            compiled.append((path, rel))
            orphans.append(f"{rel}\t{' '.join(names)}")
    (out / "orphan_data.txt").write_text("".join(o + "\n" for o in orphans), encoding="utf-8")

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
                texts[f] = text[:d.start()] + f"extern {d[1]}{d[2]}{d[3]};" + text[end:]
                break
    (out / "duplicates.txt").write_text(
        "".join(f"{n}\t{' '.join(f)}\n" for n, f in sorted(duplicates.items())),
        encoding="utf-8")

    # Calls that lean on the ARM registers (native/tools/abi_repair.py): pass
    # and return what the ROM's code does, from native/abi/ghidra_abi.txt.
    fix_files = {Path(rel).stem: rel for rel in texts if Path(rel).stem in abi_fixes}
    for name, fixes in abi_fixes.items():
        rel = fix_files.get(name)
        if rel is None:
            raise SystemExit(f"no source defines {name} (prepare.py ABI_FIXES)")
        for before, after in fixes:
            if before not in texts[rel]:
                raise SystemExit(f"ABI fix text not found in {rel}: {before[:60]}")
            texts[rel] = texts[rel].replace(before, after)
    # Tail calls (native/abi/tail_calls.txt): the ROM's `ldr r12,=g; bx r12`
    # hands g the argument registers as the caller set them; a C body
    # `return g();` passes none. Such a body forwards four.
    tail_report = []
    tail_targets: dict[str, str] = {}
    for line in (ROOT / "native" / "abi" / "tail_calls.txt").read_text(encoding="utf-8").splitlines():
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
    findings = abi_repair.load_findings(ROOT / "native" / "abi" / "ghidra_abi.txt", names)
    functions = {m.name: {a: n for n, k, a in m.symbols if k == "function"} for m in modules}
    repairer = abi_repair.Repairer(texts, module_of, functions, findings)
    repairer.fixed = set(abi_fixes)
    # What each call needs is what its callee takes: the argument words of its
    # definition, more where the callee's own registers pass on to a call that
    # needs them (abi_repair's extensions), and for a tail call (which hands
    # on four words) what its target needs. A finding past that -- a register
    # the scan was asked about when the C said otherwise -- is dropped, as are
    # the registers the ROM shows a callee never reads (ABI_NOT_READ).
    def words(name: str) -> int:
        rel = repairer.def_file.get(name)
        d = abi_repair.find_definition(texts[rel], repairer.mask(rel), name) if rel else None
        if d is None:
            return 8  # no C definition to go by: keep every finding
        params = abi_repair.param_list(texts[rel], d)
        if any(p.strip() == "..." for p in params):
            return 8
        return sum(2 if re.search(r"\b(?:u64|s64|fx64|double)\b|\blong\s+long\b", p) else 1 for p in params)
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
        body = abi_repair.strip_comments_code(texts[rel][d.body[0] + 1:d.body[1]], repairer.mask(rel)[d.body[0] + 1:d.body[1]])
        return bool(first) and re.fullmatch(rf"\s*return\s+(?:\([^()]*\)\s*)?{re.escape(first)}\s*;\s*", body) is not None
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
    for name in sorted(findings.returns):
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
    (out / "abi_next_targets.txt").write_text(
        "".join(t + "\n" for t in repairer.next_targets(where)), encoding="utf-8")
    texts.update(new_texts)
    # Parameters used as locals that a call does not pass: on x86 the
    # definition would write over its caller's frame.
    padded_texts, padded = abi_repair.pad_short_calls(texts)
    texts.update(padded_texts)
    (out / "abi_padded.txt").write_text("\n".join(padded) + "\n", encoding="utf-8")
    # Parameters declared narrower than defined: the calls pass them extended.
    widened_texts, widened = abi_repair.widen_narrow_params(texts)
    texts.update(widened_texts)
    (out / "abi_narrow_params.txt").write_text("\n".join(widened) + "\n", encoding="utf-8")
    (out / "abi_repairs.txt").write_text("\n".join(repairer.plan.applied) + "\n", encoding="utf-8")
    (out / "abi_gaps.txt").write_text("\n".join(repairer.plan.gaps) + "\n", encoding="utf-8")
    abi_summary = (f"ABI: {len(repairer.plan.applied)} repaired, {equivalent} equivalent on x86, "
                   f"{len(repairer.plan.gaps)} gaps (abi_gaps.txt), {len(padded)} short calls padded "
                   f"(abi_padded.txt), {len(widened)} narrow parameters widened (abi_narrow_params.txt)")

    # References to an address several overlays share (the decomp's relocs
    # name them all: module:overlays(a,b)). On the DS the one loaded there at
    # the time runs; the decomp's C names one of them, not always that one
    # (ov012 opens its movie through "func_ov008_020846c0", ov008's shop list,
    # where the ROM reaches ov024's MobiClip open at the same address). Natively
    # every such function reference in the referring module goes through a
    # thunk that jumps to the loaded overlay's function (runtime/overlays.c).
    thunks, dispatched = overlay_dispatch(modules, texts, module_of)

    # Game data at its DS address. Every object a C file defines that the DS
    # linker placed (symbols.txt) becomes an absolute symbol at that address,
    # so all code meets it where the DS had it -- including code that reaches
    # other memory relative to it. An initialised definition keeps its
    # initializer under a second name and registers it (KHDAYS_DATA_INIT); the
    # runtime copies it into place when its module is loaded, over the image
    # the DS itself loaded there, so pointers to functions in it are native.
    ds_data: dict[str, int] = {}
    for m in modules:
        for n, k, a in m.symbols:
            if k in ("data", "bss"):
                ds_data[n] = a
    converted: dict[str, int] = {}
    # C-defined globals the DS names nowhere: statics of a split unit (PHANTOMS).
    phantom_names = {n for n in owners if n not in ds_data and
                     not any(n in {s for s, _, _ in m.symbols} for m in modules)}
    for rel in list(texts):
        text = texts[rel]
        module = module_id(module_of[rel])
        extern = 'extern "C"' if rel.endswith(".cpp") else "extern"
        for d in reversed(definitions(text)):
            name = d[2]
            if name not in ds_data:
                if name in phantom_names:
                    text = text[:d.start()] + f"{extern} {d[1]}{name}{d[3]};" + \
                        text[statement_end(text, d.start()):]
                continue
            decl = f"{extern} {d[1]}{name}{d[3]};"
            if d[5] == "=":
                end = statement_end(text, d.start())
                text = (text[:d.start()] + decl + "\n" +
                        f"{d[1]}{name}__khdays_init{d[3]}{d[4]} =" + text[d.end():end] +
                        f"\nKHDAYS_DATA_INIT({name}, {module})" + text[end:])
            else:
                text = text[:d.start()] + decl + text[d.end():]
            converted[name] = ds_data[name]
        texts[rel] = text
    unconverted = sorted(n for n in owners if n in ds_data and n not in converted)
    (out / "unconverted_data.txt").write_text(
        "".join(f"{n}\t{' '.join(owners[n])}\n" for n in unconverted), encoding="utf-8")

    lines = []
    patched = 0
    for src, rel in compiled:
        text = texts[rel]
        original = src.read_text(encoding="utf-8", errors="replace")
        path = src
        if text != original:
            patched += 1
            path = patched_dir / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            # Rewrite only on change, so the build does not recompile it.
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                path.write_text(text, encoding="utf-8")
        lines.append(str(path).replace("\\", "/"))
    (out / "sources.cmake").write_text(
        "set(DECOMP_SOURCES\n" + "".join(f'    "{l}"\n' for l in lines) + ")\n",
        encoding="utf-8")

    # The functions of the main module and the overlays at their DS addresses,
    # for a call through a DS address -- a pointer the ROM's own data holds,
    # such as an overlay's static initializers -- to run the native function
    # instead of interpreting the ROM's bytes (runtime/overlays.c). The ITCM
    # and DTCM are left out: the MobiClip decoder is copied over the ITCM.
    renamed = {"main", "strcmp", "strcpy", "strlen", "strncmp", "strncpy", "strtol", "toupper", "abs"}
    hal_names_all = hal_definitions()
    ds_functions: list[tuple[int, int, str]] = []
    for m in modules:
        if m.name in ("itcm", "dtcm"):
            continue
        ds_module = -1 if m.name == "main" else int(m.name[2:])
        native: set[str] = set()
        for rel in m.files:
            text = texts.get(rel)
            if text is None:
                continue
            text = strip_comments(text)
            for d in re.finditer(r"^(?!static\b|extern\b|typedef\b)[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*"
                                 r"\([^;{}]*\)\s*\{", text, re.MULTILINE):
                if rel.endswith(".cpp") and not re.search(r'extern\s*"C"\s*\{', text[:d.start()]):
                    continue  # C++ linkage
                native.add(d[1])
        for n, k, a in m.symbols:
            if k == "function" and n not in renamed and (n in native or (ds_module < 0 and n in hal_names_all)):
                ds_functions.append((a, ds_module, n))
    ds_functions.sort()
    ds_c = ["/* Generated by native/tools/prepare.py: each native function of the main module",
            " * and the overlays at its DS address (runtime/overlays.c). */",
            "typedef struct { unsigned address; int module; void (*native)(void); } KhdaysDsFunction;"]
    ds_c += [f"extern void {n}(void);" for n in sorted({n for _, _, n in ds_functions})]
    ds_c.append("const KhdaysDsFunction khdays_ds_functions[] = {")
    ds_c += [f"    {{0x{a:08x}, {mod}, {n}}}," for a, mod, n in ds_functions]
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
    # What only an assembly file holds and native/hal does not implement is
    # the ROM's bytes at its DS address -- the module image the game loads
    # holds them: data is read there, and code is run there by the ARM
    # interpreter (runtime/arm.c), which a native call into DS memory reaches.
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

    asm = [".386", ".model flat", ""]
    for name, addr in sorted(absolute.items(), key=lambda kv: (kv[1], kv[0])):
        asm.append(f"PUBLIC _{name}")
        asm.append(f"_{name} EQU 0{addr:08x}h")
    asm += [""] + thunk_asm(thunks)
    asm += ["", "END", ""]
    (out / "absolute.asm").write_text("\n".join(asm), encoding="utf-8")
    (out / "alternatenames.txt").write_text(
        "".join(f"/ALTERNATENAME:_{a}=_{b}\n" for a, b in sorted(alternates.items())),
        encoding="utf-8")

    print(f"{len(compiled)} sources ({patched} patched), "
          f"{len(hal_files)} assembly-only files for the HAL, "
          f"{len(orphans)} unlisted data sources taken in (orphan_data.txt), "
          f"{len(duplicates)} duplicated globals resolved; at DS addresses: "
          f"{len(converted)} C-defined data objects ({len(unconverted)} left in the image, "
          f"see unconverted_data.txt), {bss_count} other BSS symbols, "
          f"{len(PHANTOMS)} split-unit statics, "
          f"{len(absolute) - bss_count - len(converted) - len(PHANTOMS)} other absolute symbols; "
          f"{len(alternates)} aliases; {abi_summary}; "
          f"{len(thunks)} addresses shared by overlays' functions, reached through thunks from "
          f"{dispatched} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
