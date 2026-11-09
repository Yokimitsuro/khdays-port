"""KINGDOM HEARTS II's AI scripts (an object's obj/<name>.mdlx, BAR entries of type 3), as KH2FM.exe's interpreter
runs them (our reading of it; docs/FORMATS.md, "KH2's objects and their AI"): the header, the triggers the game
calls it by, and the code disassembled from them and from the routines the script registers as its states or
starts as threads (`1:112`, `2:20`).

    python tools/research/kh2_ai.py --kh2 <kh2_*.hed_out> ... obj/M_EX010.mdlx
    python tools/research/kh2_ai.py --kh2 <kh2_*.hed_out> ... --survey
    python tools/research/kh2_ai.py --kh2 <kh2_*.hed_out> ... --coverage handled.txt [M_EX010 ...]

The first lists the code of an object's scripts; --survey counts which calls each kind of object makes; --coverage
says, for each object (or those named), which of the calls its script makes are in a list of handled ones (a
`kind:number` a line, as `khdays-native --kh2-ai-calls` prints it) and which are missing. --kh2 once for each of
KH2's packages extracted: its objects lie in several.

The scripts are read by tools/mods/kh2_bdx.py (the mod tool reads them too). A script: a name (16 bytes), three sizes (+0x10 its work area, +0x14 its frame area, +0x18 its value stack, in
bytes), then its triggers {s32 key, s32 entry} until (0, 0). Code is 16-bit words counted from +0x10 (an entry is
such a word's number). A word: its operation in bits 0-3, a sub-kind in bits 4-5, an argument in bits 6-15; some
take one or two more words.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "mods"))
from kh2_bdx import (  # noqa: E402
    AI_ENTRY,
    AiError,
    BAR,
    BASES,
    BINARY,
    CALL_KINDS,
    CALL_TABLES,
    CALL_TABLE_9,
    CODE,
    HEADER,
    Instruction,
    MISC,
    STATE_ARGS,
    STATE_CALL,
    STATE_ROUTINE_ARGS,
    Script,
    TRIGGERS,
    UNARY,
    by_argument,
    decode,
    exe_calls,
    exe_sections,
    read_handled,
    read_script,
    scripts_in,
    state_routines,
    thread_routines,
    walk,
)

__all__ = [
    "AI_ENTRY",
    "AiError",
    "BAR",
    "BASES",
    "BINARY",
    "CALL_KINDS",
    "CALL_TABLES",
    "CALL_TABLE_9",
    "CODE",
    "HEADER",
    "Instruction",
    "MISC",
    "STATE_ARGS",
    "STATE_CALL",
    "STATE_ROUTINE_ARGS",
    "Script",
    "TRIGGERS",
    "UNARY",
    "by_argument",
    "decode",
    "exe_calls",
    "exe_sections",
    "read_handled",
    "read_script",
    "scripts_in",
    "state_routines",
    "thread_routines",
    "walk",
    "listing",
    "find",
    "survey",
    "objects_in",
    "calls_of",
    "coverage",
    "main",
]


def listing(s: Script) -> list[str]:
    """The script's code as text: its triggers, then each reached instruction, labels where code is entered."""
    code = walk(s)
    routines = state_routines(s, code)
    threads = thread_routines(s, code) - routines
    entered = {to for ins in code.values() for to in ins.next + ins.calls if to != ins.at + ins.size}
    entered |= {entry for _, entry in s.triggers} | routines | threads
    out = [f"{s.name}: work {s.work}, frame {s.frame}, stack {s.stack} bytes"]
    for key, entry in s.triggers:
        out.append(f"  trigger {key} at {entry:#x}" + (f": {TRIGGERS[key]}" if key in TRIGGERS else ""))
    if routines:
        out.append(f"  {len(routines)} state routines")
    if threads:
        out.append(f"  {len(threads)} thread routines")
    last = None
    for pc in sorted(code):
        if last is not None and pc != last:
            out.append("")
        if pc in entered:
            out.append(f"{pc:#06x}:" + ("  state routine" if pc in routines else "  thread routine" if pc in threads else ""))
        out.append(f"    {code[pc].text}")
        last = pc + code[pc].size
    return out


def find(kh2: list[Path], name: str) -> Path:
    """A file in the first of the packages that has it."""
    for root in kh2:
        if (root / name).is_file():
            return root / name
    raise OSError(f"{name} is in none of the packages given")


def survey(kh2: list[Path]) -> tuple[dict[str, int], dict[str, Counter]]:
    """Every object's scripts (obj/*.mdlx in the packages, each name once): how many of each kind (the name's
    prefix: M_ enemies, B_ bosses, N_ people, P_ the party, F_ things, W_ weapons), and how many scripts of each kind
    make each call."""
    scripts: dict[str, int] = Counter()
    calls: dict[str, Counter] = defaultdict(Counter)
    for _, path in sorted(objects_in(kh2).items()):
        kind = path.stem.split("_")[0].upper()
        for _, s in scripts_in(path.read_bytes()):
            scripts[kind] += 1
            for call in {ins.syscall for ins in walk(s).values() if ins.syscall is not None}:
                calls[kind][call] += 1
    return scripts, calls


def objects_in(kh2: list[Path]) -> dict[str, Path]:
    """obj/*.mdlx in the packages, each name once (the first package that has it), by the name's stem in capitals."""
    objects: dict[str, Path] = {}
    for root in kh2:
        for path in (root / "obj").glob("*.mdlx"):
            objects.setdefault(path.stem.upper(), path)
    return objects


def calls_of(path: Path) -> set[tuple[int, int]]:
    """Every call an object's scripts make (state routines walked)."""
    return {i.syscall for _, s in scripts_in(path.read_bytes()) for i in walk(s).values() if i.syscall is not None}


def coverage(objects: dict[str, Path], handled: set[tuple[int, int]], names: list[str]) -> list[str]:
    """For each object (those named, else all with a script): its calls, how many are handled, the missing ones."""
    out = []
    for name in [n.upper() for n in names] or sorted(objects):
        if name not in objects:
            out.append(f"{name}: not found")
            continue
        calls = calls_of(objects[name])
        if not calls:
            continue
        missing = sorted(calls - handled)
        listed = " ".join(f"{k}:{n}" for k, n in missing) or "-"
        out.append(f"{name}: {len(calls)} calls, {len(calls) - len(missing)} handled; missing {listed}")
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--kh2",
        type=Path,
        action="append",
        default=[],
        help="a KH2 package's entries extracted (<kh2_*.hed_out>); once for each",
    )
    ap.add_argument("--exe-calls", type=Path, help="KH2FM.exe: print its calls' shapes (kind:number arguments back)")
    ap.add_argument("--survey", action="store_true", help="which calls each kind of object's scripts make")
    ap.add_argument("--coverage", type=Path, help="a list of handled calls: which each object's script still misses")
    ap.add_argument("files", nargs="*", help="objects' files under --kh2 (obj/M_EX010.mdlx); with --coverage, names")
    args = ap.parse_args(argv)
    try:
        if args.exe_calls:
            for kind, number, count, back in exe_calls(args.exe_calls.read_bytes()):
                print(f"{kind}:{number} {count} {int(back)}")
            return 0
        if not args.kh2:
            ap.error("--kh2 is needed")
        if args.coverage:
            handled = read_handled(args.coverage.read_text(encoding="utf-8"))
            names = [Path(f).stem for f in args.files]
            print("\n".join(coverage(objects_in(args.kh2), handled, names)))
            return 0
        if args.survey:
            scripts, calls = survey(args.kh2)
            for kind in sorted(scripts):
                top = ", ".join(f"{t}:{n} x{c}" for (t, n), c in calls[kind].most_common(12))
                print(f"{kind}: {scripts[kind]} scripts, {len(calls[kind])} calls; most made: {top}")
            return 0
        for name in args.files:
            found = scripts_in(find(args.kh2, name).read_bytes())
            if not found:
                print(f"kh2 ai: {name} has no AI script", file=sys.stderr)
            for entry, s in found:
                print(f"== {name} [{entry}]")
                print("\n".join(listing(s)))
    except (OSError, AiError) as e:
        print(f"kh2 ai: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
