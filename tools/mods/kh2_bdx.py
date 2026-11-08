"""KINGDOM HEARTS II's AI scripts as KH2FM.exe's interpreter reads them (an object's obj/<name>.mdlx, BAR entries of
type 3; our reading of it, docs/FORMATS.md, "KH2's objects and their AI"): the header, the triggers the game calls it
by, each instruction decoded, the code walked from the triggers and from the routines the script registers as its
states, and the shapes of KH2's calls read from the player's executable. The KH2 enemies' mod tool (kh2_enemies.py)
and the research tool (tools/research/kh2_ai.py) read scripts through it.

A script: a name (16 bytes), three sizes (+0x10 its work area, +0x14 its frame area, +0x18 its value stack, in
bytes), then its triggers {s32 key, s32 entry} until (0, 0). Code is 16-bit words counted from +0x10 (an entry is
such a word's number). A word: its operation in bits 0-3, a sub-kind in bits 4-5, an argument in bits 6-15; some
take one or two more words.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field


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
STATE_CALL = (1, 6)  # registers a state (see state_routines)
STATE_ARGS = 12
STATE_ROUTINE_ARGS = (3, 10)
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
    pushed: int | None = None  # the integer a push puts on the stack
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
            return Instruction(pc, 3, f"push {value}", [pc + 3], pushed=s.long(pc + 1) if sub == 0 else None)
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


def state_routines(s: Script, code: dict[int, Instruction]) -> set[int]:
    """The routines the reached code registers as states: call 1:6 takes 12 arguments, pushed just before it, its 4th
    (and sometimes its 11th) the code word where the state's routine starts. The game enters a state by running that
    routine as a child thread (FUN_1403cad00), so no jump or call of the script leads there."""
    order = sorted(code)
    found = set()
    for i, pc in enumerate(order):
        if code[pc].syscall != STATE_CALL or i < STATE_ARGS:
            continue
        args = [code[p] for p in order[i - STATE_ARGS : i]]
        if any(a.at + a.size != b.at for a, b in zip(args, [*args[1:], code[pc]])):
            continue  # not the pushes right before it
        for k in STATE_ROUTINE_ARGS:
            entry = args[k].pushed
            if entry is not None and 0 < entry < s.words:
                found.add(entry)
    return found


def walk(s: Script) -> dict[int, Instruction]:
    """Every instruction reached from the triggers and from the routines the reached code registers as states,
    following jumps and calls."""
    seen: dict[int, Instruction] = {}
    todo = [entry for _, entry in s.triggers]
    routines: set[int] = set()
    while todo:
        while todo:
            pc = todo.pop()
            if pc in seen or not 0 <= pc < s.words:
                continue
            ins = decode(s, pc)
            seen[pc] = ins
            todo += ins.next + ins.calls
        new = state_routines(s, seen) - routines
        routines |= new
        todo = sorted(new)
    return seen


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


def read_handled(text: str) -> set[tuple[int, int]]:
    """A list of handled calls: `kind:number` a line; any other line skipped (comments, the game's own notes)."""
    out = set()
    for line in text.splitlines():
        kind, colon, number = line.split("#")[0].strip().partition(":")
        if colon and kind.isdigit() and number.isdigit():
            out.add((int(kind), int(number)))
    return out


# KH2's call tables in KH2FM.exe (the Steam release read): eleven table addresses at CALL_TABLES; kind 9's is set by the
# game at its start (FUN_14026ace0) to CALL_TABLE_9; kind 3's too, at run time only. A record is 16 bytes: the
# function, then a word whose low 16 bits are its arguments' count and whose bit 30 says it gives a value back.
CALL_TABLES = 0x140753490
CALL_TABLE_9 = 0x14073D080
CALL_KINDS = 11


def exe_sections(exe: bytes) -> tuple[int, list[tuple[str, int, int, int]]]:
    """A PE's image base and sections (name, address, file offset, size in the file)."""
    if exe[:2] != b"MZ":
        raise AiError("not an executable")
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    count = struct.unpack_from("<H", exe, pe + 6)[0]
    optional = struct.unpack_from("<H", exe, pe + 20)[0]
    base = struct.unpack_from("<Q", exe, pe + 24 + 24)[0]
    sections = []
    for i in range(count):
        name, _, va, size, raw = struct.unpack_from("<8sIIII", exe, pe + 24 + optional + 40 * i)
        sections.append((name.rstrip(b"\0").decode("latin1"), base + va, raw, size))
    return base, sections


def exe_calls(exe: bytes) -> list[tuple[int, int, int, bool]]:
    """Every call KH2's tables hold, read from the player's executable: (kind, number, arguments, gives back). A table
    ends where the next one starts, or at a record that is not one (a function outside the code, a count word with
    more than its arguments and flag)."""
    _, sections = exe_sections(exe)

    def read(va: int, n: int) -> bytes:
        for _, at, raw, size in sections:
            if at <= va and va + n <= at + size:
                return exe[raw + va - at : raw + va - at + n]
        raise AiError(f"{va:#x} is in no section of this executable")

    code = [(at, at + size) for name, at, _, size in sections if name == ".text"]
    if not code:
        raise AiError("this executable has no code section")
    lo, hi = code[0]
    tables = list(struct.unpack("<11Q", read(CALL_TABLES, 8 * CALL_KINDS)))
    tables[9] = CALL_TABLE_9
    starts = sorted(t for t in tables if t)
    out = []
    for kind, table in enumerate(tables):
        if not table:
            continue
        later = [t for t in starts if t > table]
        end = later[0] if later else table + 0x4000
        number = 0
        while table + 16 * number < end:
            fn, word = struct.unpack("<QI", read(table + 16 * number, 12))
            if (fn and not lo <= fn < hi) or word & 0xBFFF0000:
                break
            if fn:
                out.append((kind, number, word & 0xFFFF, bool(word & 0x40000000)))
            number += 1
    if sum(1 for k, *_ in out if k == 0) < 50:
        raise AiError("this executable's call tables are not where KH2 FINAL MIX's (Steam) are")
    return out
