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
# its routine (+ 8, the 4th) and the routines the game runs for its events (+ 0xa + 2 n, the 5th on: n 0 its leave,
# 1 off the ground, 2 landed, 5 its name shouted -- 1:90, + 0x14 --, 6 entered, + 0x16; FUN_1403ca1d0, FUN_1403ca950)
STATE_ROUTINE_ARGS = tuple(range(3, 12))
# calls that start a routine of the script as a thread: (table, index) -> (arguments, the routine's among the pushes,
# the first pushed 0): 1:112 on an actor, its second (FUN_14042ca20: the actor, the routine, a value -- the routine its
# attached thread: Cerberus's on the player), 2:20 a thread of its own, its first (FUN_140432cd0: the routine, a value)
THREAD_CALLS = {(1, 112): (3, 1), (2, 20): (2, 0)}
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


def _pushed_routines(s: Script, code: dict[int, Instruction], call: tuple[int, int], count: int,
                     which: tuple[int, ...]) -> set[int]:
    """The code words a call's arguments name as routines: its `count` arguments pushed just before it, those at
    `which` (counted from the first pushed) integers within the code."""
    order = sorted(code)
    found = set()
    for i, pc in enumerate(order):
        if code[pc].syscall != call or i < count:
            continue
        args = [code[p] for p in order[i - count : i]]
        if any(a.at + a.size != b.at for a, b in zip(args, [*args[1:], code[pc]])):
            continue  # not the pushes right before it
        for k in which:
            entry = args[k].pushed
            if entry is not None and 0 < entry < s.words:
                found.add(entry)
    return found


def state_routines(s: Script, code: dict[int, Instruction]) -> set[int]:
    """The routines the reached code registers as states: call 1:6 takes 12 arguments, pushed just before it, its 4th
    the code word where the state's routine starts, its 5th to 12th those of the routines the game runs on the state's
    events (its leave, the ground left and met, its name shouted, its entry: 0 none). The game enters a state by
    running its routine as a child thread (FUN_1403cad00) and the others as it tells the state of an event, so no jump
    or call of the script leads there."""
    return _pushed_routines(s, code, STATE_CALL, STATE_ARGS, STATE_ROUTINE_ARGS)


def _leading_pops(s: Script, entry: int) -> list[int]:
    """A routine's parameters: the frame offsets its first instructions pop into, in order (the first popped the value
    pushed last)."""
    offsets = []
    pc = entry
    while 0 <= pc and pc + 2 <= s.words:
        w = s.word(pc)
        if w & 0xF != 1 or (w >> 4) & 3 != 0 or (w >> 6) != 0:  # not a pop into the frame
            break
        offsets.append(s.word(pc + 1))
        pc += 2
    return offsets


def _returns(s: Script, entry: int) -> int:
    """Whether a routine gives a value back: the instruction before its first return a push (the scripts' compiler
    leaves the value last on the stack)."""
    pc, before = entry, None
    while 0 <= pc < s.words:
        ins = decode(s, pc)
        if ins.bad:
            return 0
        if ins.text == "ret":
            return 1 if before is not None and before.text.startswith("push") else 0
        before = ins
        pc += ins.size
    return 0


def _stack_before(s: Script, entry: int, site: int, shapes: dict[tuple[int, int], tuple[int, int]]) -> list | None:
    """What a routine's stack holds just before the instruction at `site`, its code read in order from its entry (a
    branch's both ways as one): ("int", n) an integer pushed, ("param", offset) a parameter's value, None anything
    else; None when it cannot tell (a call into the game of an unknown shape, the stack run out)."""
    params = _leading_pops(s, entry)
    stack: list = []
    frame: dict[int, object] = {o: ("param", o) for o in params}
    pc = entry + 2 * len(params)
    try:
        while pc < site:
            if not 0 <= pc < s.words:
                return None
            ins = decode(s, pc)
            if ins.bad:
                return None
            w = s.word(pc)
            op, sub, arg = w & 0xF, (w >> 4) & 3, w >> 6
            if op == 0:  # a push: an integer, a frame's value, anything else
                if ins.pushed is not None:
                    stack.append(("int", ins.pushed))
                elif sub == 3 and arg == 0:
                    stack.append(frame.get(s.word(pc + 1)))
                else:
                    stack.append(None)
            elif op == 1:  # a pop into a place
                value = stack.pop()
                if sub == 0 and arg == 0:
                    frame[s.word(pc + 1)] = value
            elif op == 2 or (op == 7 and arg != 0):  # a copy's address, a conditional jump's value
                stack.pop()
            elif op == 3:  # a load
                stack[-1] = None
            elif op == 4:  # a store: its address and its value
                del stack[-2:]
            elif op == 5:
                stack[-1] = None
            elif op == 6:
                del stack[-2:]
                stack.append(None)
            elif op in (8, 11):  # a call: its parameters taken, its value given back
                callee = ins.calls[0]
                n = len(_leading_pops(s, callee))
                if n > len(stack):
                    return None
                del stack[len(stack) - n :]
                stack.extend([None] * _returns(s, callee))
            elif op == 9:
                if ins.text == "ret":
                    stack = []
                elif ins.text == "drop":
                    stack.pop()
                elif ins.text == "dup":
                    stack.append(stack[-1])
                elif ins.text in ("sin", "cos", "deg2rad", "rad2deg"):
                    stack[-1] = None
            elif op == 10:
                shape = shapes.get(ins.syscall) if ins.syscall else None
                if shape is None:
                    return None
                n, back = shape
                if n > len(stack):
                    return None
                del stack[len(stack) - n :]
                stack.extend([None] * back)
            pc += ins.size
    except IndexError:
        return None
    return stack


def _routine_values(s: Script, code: dict[int, Instruction], entries: list[int], value, depth: int) -> set[int]:
    """The integers a stack value can be: itself, or a parameter's at each place its routine is called from (the
    callers' stacks there), a few calls up."""
    if value is None or depth > 4:
        return set()
    if value[0] == "int":
        return {value[1]}
    _, offset, routine = value
    params = _leading_pops(s, routine)
    if offset not in params:
        return set()
    k = params.index(offset)  # the first popped is the last pushed
    found: set[int] = set()
    for pc, ins in code.items():
        if routine not in ins.calls:
            continue
        caller = max((e for e in entries if e <= pc), default=None)
        stack = _stack_before(s, caller, pc, SHAPES) if caller is not None else None
        if stack is None or len(stack) < len(params):
            continue
        v = stack[len(stack) - 1 - k]
        if v is not None and v[0] == "param":
            v = ("param", v[1], caller)
        found |= _routine_values(s, code, entries, v, depth + 1)
    return found


# the calls into the game's shapes ((table, index) -> (arguments, values back)) when a tool knows them (the mod tool's
# kh2/calls.txt, KH2's executable): with them the walk reads a routine's stack past such calls
SHAPES: dict[tuple[int, int], tuple[int, int]] = {}


def thread_routines(s: Script, code: dict[int, Instruction]) -> set[int]:
    """The routines the reached code starts as threads (THREAD_CALLS): run by the game, not jumped or called to. One
    pushed right before the call is read from there; one given through a routine's parameter (the Soldier's Cyclone:
    its message 34 passes 90 on to a routine that starts it with 2:20) from the callers' stacks."""
    found: set[int] = set()
    for call, (count, at) in THREAD_CALLS.items():
        found |= _pushed_routines(s, code, call, count, (at,))
    entries = sorted({c for ins in code.values() for c in ins.calls} | {e for _, e in s.triggers})
    for pc, ins in code.items():
        if ins.syscall not in THREAD_CALLS:
            continue
        count, at = THREAD_CALLS[ins.syscall]
        routine = max((e for e in entries if e <= pc), default=None)
        stack = _stack_before(s, routine, pc, SHAPES) if routine is not None else None
        if stack is None or len(stack) < count:
            continue
        value = stack[len(stack) - count + at]
        if value is not None and value[0] == "param":
            value = ("param", value[1], routine)
        found |= {v for v in _routine_values(s, code, entries, value, 0) if 0 < v < s.words}
    return found


def walk(s: Script) -> dict[int, Instruction]:
    """Every instruction reached from the triggers and from the routines the reached code registers as states or
    starts as threads, following jumps and calls."""
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
        new = (state_routines(s, seen) | thread_routines(s, seen)) - routines
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
