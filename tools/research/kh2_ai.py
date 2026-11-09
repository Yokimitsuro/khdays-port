"""KINGDOM HEARTS II's AI scripts (an object's obj/<name>.mdlx, BAR entries of type 3), as KH2FM.exe's interpreter
runs them (our reading of it; docs/FORMATS.md, "KH2's objects and their AI"): the header, the triggers the game
calls it by, and the code disassembled from them.

    python tools/research/kh2_ai.py --kh2 <kh2_*.hed_out> ... obj/M_EX010.mdlx
    python tools/research/kh2_ai.py --kh2 <kh2_*.hed_out> ... --survey

The first lists the code of an object's scripts; --survey counts which calls each kind of object makes. --kh2 once
for each of KH2's packages extracted: its objects lie in several.

A script: a name (16 bytes), three sizes (+0x10 its work area, +0x14 its frame area, +0x18 its value stack, in
bytes), then its triggers {s32 key, s32 entry} until (0, 0). Code is 16-bit words counted from +0x10 (an entry is
such a word's number). A word: its operation in bits 0-3, a sub-kind in bits 4-5, an argument in bits 6-15; some
take one or two more words.
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

BAR = b"BAR\x01"
AI_ENTRY = 3  # an AI script's BAR entry type
HEADER = 0x1C  # the name and the three sizes; the triggers follow
CODE = 0x10  # word 0 of the code
BASES = ("fp", "wp", "pp", "code")  # the frame, the work area, the frame's first word as a pointer, the script


def by_argument(names: str) -> dict[int, str]:
    """An operation's names by its argument, in order ("-": none)."""
    return {i: name for i, name in enumerate(names.split()) if name != "-"}


UNARY = {  # by sub-kind: 0 integers, 1 floats
    0: by_argument("itof - neg not eqz abs ltz lez eqz nez gez gtz"),
    1: by_argument("- ftoi fneg - - fabs fltz flez feqz fnez fgez fgtz"),
}
BINARY = {
    0: by_argument("add sub mul div mod and or xor shl sar land lor"),
    1: by_argument("fadd fsub fmul fdiv fmod"),
}
MISC = by_argument("yield exit ret drop - dup sin cos deg2rad rad2deg")
# the triggers the game raises itself (KH2FM.exe's callers of its trigger runner); the others an object's own
TRIGGERS = {0: "start, run once with the object", 1: "main, the object's thread resumed each tick"}


class AiError(ValueError):
    """Not an AI script as read here (cut short, a trigger past its end)."""


@dataclass
class Instruction:
    at: int  # its word number
    size: int  # in words
    text: str
    next: list[int] = field(default_factory=list)  # where the code goes on
    calls: list[int] = field(default_factory=list)  # what it calls
    syscall: tuple[int, int] | None = None  # (table, index)
    bad: bool = False


@dataclass
class Script:
    name: str
    work: int
    frame: int
    stack: int
    triggers: list[tuple[int, int]]
    data: bytes

    @property
    def words(self) -> int:
        return (len(self.data) - CODE) // 2

    def word(self, i: int) -> int:
        return struct.unpack_from("<H", self.data, CODE + 2 * i)[0]

    def signed(self, i: int) -> int:
        return struct.unpack_from("<h", self.data, CODE + 2 * i)[0]

    def long(self, i: int) -> int:
        return struct.unpack_from("<i", self.data, CODE + 2 * i)[0]

    def real(self, i: int) -> float:
        return struct.unpack_from("<f", self.data, CODE + 2 * i)[0]


def read_script(data: bytes) -> Script:
    """A script's header and triggers."""
    if len(data) < HEADER:
        raise AiError("shorter than its header")
    name = data[:16].split(b"\0")[0].decode("latin1")
    work, frame, stack = struct.unpack_from("<3i", data, 16)
    triggers = []
    at = HEADER
    while True:
        if at + 8 > len(data):
            raise AiError("its triggers run past its end")
        key, entry = struct.unpack_from("<2i", data, at)
        at += 8
        if key == 0 and entry == 0:
            break
        triggers.append((key, entry))
    script = Script(name, work, frame, stack, triggers, data)
    for key, entry in triggers:
        if not 0 <= entry < script.words:
            raise AiError(f"trigger {key} starts past its end")
    return script


def decode(s: Script, pc: int) -> Instruction:
    """The instruction at word `pc`."""

    def need(n: int) -> bool:
        return pc + n <= s.words

    w = s.word(pc)
    op, sub, arg = w & 0xF, (w >> 4) & 3, w >> 6
    base = BASES[arg] if arg < 4 else f"base{arg}"
    if op == 0:  # push: an int, a float, an address, a value
        if sub < 2:
            if not need(3):
                return Instruction(pc, 1, f"push? {w:#06x}", bad=True)
            value = f"{s.long(pc + 1)}" if sub == 0 else f"{s.real(pc + 1):g}f"
            return Instruction(pc, 3, f"push {value}", [pc + 3])
        if not need(2):
            return Instruction(pc, 1, f"push? {w:#06x}", bad=True)
        o = s.word(pc + 1)
        if sub == 2:
            return Instruction(pc, 2, f"push &{base}+{o:#x}", [pc + 2])
        if arg == 3:
            return Instruction(pc, 2, f"push code[{o:#x}]", [pc + 2])
        return Instruction(pc, 2, f"push {base}+{o:#x}", [pc + 2])
    if op == 1:  # pop into a place
        if not need(2):
            return Instruction(pc, 1, f"pop? {w:#06x}", bad=True)
        return Instruction(pc, 2, f"pop {base}+{s.word(pc + 1):#x}", [pc + 2])
    if op == 2:  # copy bytes from a popped address
        if not need(3):
            return Instruction(pc, 1, f"copy? {w:#06x}", bad=True)
        return Instruction(pc, 3, f"copy {s.word(pc + 1)} bytes to {base}+{s.word(pc + 2):#x}", [pc + 3])
    if op == 3:  # load from a popped address
        if not need(2):
            return Instruction(pc, 1, f"load? {w:#06x}", bad=True)
        return Instruction(pc, 2, f"load +{s.word(pc + 1):#x}", [pc + 2])
    if op == 4:  # store a popped value at a popped address (an argument: copy that many bytes)
        return Instruction(pc, 1, "store" if arg == 0 else f"store {arg} bytes", [pc + 1])
    if op in (5, 6):
        names = (UNARY if op == 5 else BINARY).get(sub, {})
        text = names.get(arg)
        if text is None:
            return Instruction(pc, 1, f"{'unary' if op == 5 else 'binary'}? {w:#06x}", bad=True)
        return Instruction(pc, 1, text, [pc + 1])
    if op == 7:  # jumps, from the next instruction
        if not need(2) or arg > 2:
            return Instruction(pc, 1, f"jump? {w:#06x}", bad=True)
        to = pc + 2 + s.signed(pc + 1)
        kind = ("jmp", "jz", "jnz")[arg]
        return Instruction(pc, 2, f"{kind} {to:#x}", [to] if arg == 0 else [pc + 2, to])
    if op == 8:  # a call; the argument the frame's words
        if not need(2):
            return Instruction(pc, 1, f"call? {w:#06x}", bad=True)
        to = pc + 2 + s.signed(pc + 1)
        return Instruction(pc, 2, f"call {to:#x} (frame {arg})", [pc + 2], [to])
    if op == 9:
        text = MISC.get(arg)
        if text is None:
            return Instruction(pc, 1, f"misc? {w:#06x}", bad=True)
        return Instruction(pc, 1, text, [] if arg in (1, 2) else [pc + 1])
    if op == 10:  # a call into the game: the argument its table, the next word its number
        if not need(2):
            return Instruction(pc, 1, f"syscall? {w:#06x}", bad=True)
        n = s.word(pc + 1)
        return Instruction(pc, 2, f"syscall {arg}:{n}", [pc + 2], syscall=(arg, n))
    if op == 11:  # a far call (a 32-bit distance)
        if not need(3):
            return Instruction(pc, 1, f"call? {w:#06x}", bad=True)
        to = pc + 3 + s.long(pc + 1)
        return Instruction(pc, 3, f"call {to:#x} (frame {arg}, far)", [pc + 3], [to])
    return Instruction(pc, 1, f"bad {w:#06x}", bad=True)


def walk(s: Script) -> dict[int, Instruction]:
    """Every instruction reached from the triggers, following jumps and calls."""
    seen: dict[int, Instruction] = {}
    todo = [entry for _, entry in s.triggers]
    while todo:
        pc = todo.pop()
        if pc in seen or not 0 <= pc < s.words:
            continue
        ins = decode(s, pc)
        seen[pc] = ins
        todo += ins.next + ins.calls
    return seen


def listing(s: Script) -> list[str]:
    """The script's code as text: its triggers, then each reached instruction, labels where code is entered."""
    code = walk(s)
    entered = {to for ins in code.values() for to in ins.next + ins.calls if to != ins.at + ins.size}
    entered |= {entry for _, entry in s.triggers}
    out = [f"{s.name}: work {s.work}, frame {s.frame}, stack {s.stack} bytes"]
    for key, entry in s.triggers:
        out.append(f"  trigger {key} at {entry:#x}" + (f": {TRIGGERS[key]}" if key in TRIGGERS else ""))
    last = None
    for pc in sorted(code):
        if last is not None and pc != last:
            out.append("")
        if pc in entered:
            out.append(f"{pc:#06x}:")
        out.append(f"    {code[pc].text}")
        last = pc + code[pc].size
    return out


def scripts_in(bar: bytes) -> list[tuple[str, Script]]:
    """A BAR's AI scripts with their entry names (an object's .mdlx has one)."""
    if bar[:4] != BAR:
        return []
    count = struct.unpack_from("<I", bar, 4)[0]
    out = []
    for i in range(count):
        if 16 + 16 * (i + 1) > len(bar):
            break
        kind, _, name, at, size = struct.unpack_from("<HH4sII", bar, 16 + 16 * i)
        if kind != AI_ENTRY or size < HEADER or at + size > len(bar):
            continue
        out.append((name.rstrip(b"\0").decode("latin1"), read_script(bar[at : at + size])))
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
    objects: dict[str, Path] = {}
    for root in kh2:
        for path in (root / "obj").glob("*.mdlx"):
            objects.setdefault(path.name.lower(), path)
    for _, path in sorted(objects.items()):
        kind = path.stem.split("_")[0].upper()
        for _, s in scripts_in(path.read_bytes()):
            scripts[kind] += 1
            for call in {ins.syscall for ins in walk(s).values() if ins.syscall is not None}:
                calls[kind][call] += 1
    return scripts, calls


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--kh2",
        type=Path,
        action="append",
        required=True,
        help="a KH2 package's entries extracted (<kh2_*.hed_out>); once for each",
    )
    ap.add_argument("--survey", action="store_true", help="which calls each kind of object's scripts make")
    ap.add_argument("files", nargs="*", help="objects' files under --kh2 (obj/M_EX010.mdlx)")
    args = ap.parse_args(argv)
    try:
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
