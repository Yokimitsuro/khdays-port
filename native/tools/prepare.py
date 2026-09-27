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
        syms = d / "symbols.txt"
        if syms.exists():
            for line in syms.read_text(encoding="utf-8").splitlines():
                s = SYMBOL_RE.match(line)
                if s:
                    m.symbols.append((s[1], s[2], int(s[4], 16)))
        modules.append(m)
    return modules


# --- Mechanical source transforms ---------------------------------------------

# `register int x asm("r4") = 0;` -- an ARM register binding; drop it.
REGISTER_ASM = re.compile(r"\basm\s*\(\s*\"r\d+\"\s*\)")
# `asm { clz x, y }` -- ARM count-leading-zeros; the compat header supplies it.
CLZ_ASM = re.compile(r"\basm\s*\{\s*clz\s+(\w+)\s*,\s*(\w+)\s*\}")
# Struct members whose array size is a constant expression equal to zero.
ZERO_MEMBER = re.compile(
    r"^([ \t]+)[\w \t\*]+?\b(\w+)[ \t]*\[([\w \t\+\-\*\(\)]+)\][ \t]*;[^\n]*$",
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


# Where the runtime joins the game's own flow, each a single, documented edit
# (file -> (text, replacement)); a missing text stops the script.
HOOKS = {
    # An overlay's data: its image has just been loaded and started; where the
    # SDK runs the overlay's static initializers, the native data initializers
    # of that overlay go over the image (runtime/data_init.c).
    "libs/nitro/fs/calls/FS_StartOverlay.c": (
        "FSOverlayInitFunc *p = p_ovi->header.sinit_init;",
        "FSOverlayInitFunc *p = (khdays_data_init((int)p_ovi->header.id), "
        "p_ovi->header.sinit_init);",
    ),
}


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
    text = REGISTER_ASM.sub("", text)
    text = CLZ_ASM.sub(lambda m: f"{m[1]} = khdays_clz({m[2]});", text)
    text = END_LABEL.sub(lambda m: f"{m[1]};{m[2]}", text)
    text = DSPROT_RANGES_INCLUDE.sub(r'\1"khdays_dsprot_ranges.h"', text)
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
            if rel in HOOKS:
                before, after = HOOKS[rel]
                if before not in new:
                    raise SystemExit(f"hook text not found in {rel}: {before}")
                new = "extern void khdays_data_init(int module);\n" + new.replace(before, after)
            if re.search(r"\basm\b|__asm\b", strip_comments(new)):
                hal_files.append((m.name, rel))
                continue
            texts[rel] = new
            module_of[rel] = m.name
            compiled.append((src, rel))

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
    # FS_OVERLAY_ID(n) is the ADDRESS of the linker symbol OVERLAY_n_ID.
    for d in sorted((CONFIG / "overlays").iterdir()):
        mo = re.fullmatch(r"ov(\d+)", d.name)
        if mo:
            absolute[f"OVERLAY_{int(mo[1])}_ID"] = int(mo[1])

    asm = [".386", ".model flat", ""]
    for name, addr in sorted(absolute.items(), key=lambda kv: (kv[1], kv[0])):
        asm.append(f"PUBLIC _{name}")
        asm.append(f"_{name} EQU 0{addr:08x}h")
    asm += ["", "END", ""]
    (out / "absolute.asm").write_text("\n".join(asm), encoding="utf-8")
    (out / "alternatenames.txt").write_text(
        "".join(f"/ALTERNATENAME:_{a}=_{b}\n" for a, b in sorted(alternates.items())),
        encoding="utf-8")

    print(f"{len(compiled)} sources ({patched} patched), "
          f"{len(hal_files)} assembly-only files for the HAL, "
          f"{len(duplicates)} duplicated globals resolved; at DS addresses: "
          f"{len(converted)} C-defined data objects ({len(unconverted)} left in the image, "
          f"see unconverted_data.txt), {bss_count} other BSS symbols, "
          f"{len(PHANTOMS)} split-unit statics, "
          f"{len(absolute) - bss_count - len(converted) - len(PHANTOMS)} other absolute symbols; "
          f"{len(alternates)} aliases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
