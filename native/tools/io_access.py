"""Hardware register accesses in the decomp's C, made calls to the register model.

The DS I/O pages are mapped with no access natively; each game load or store
there faults into runtime/io_trap.c, which costs microseconds. Code that
names a register through a macro defined as a dereference of its address
(`#define REG_DIVCNT (*(vu16 *)0x04000280)`) or through such a dereference
written out (`*(volatile u32 *)0x04000400 = op;`) is rewritten so the access
is a call to the same register model the trap uses (KHDAYS_IO_GET/SET in
compat/decomp_compat.h): same reads, same writes, same order, no fault.

Only forms whose meaning is plain are rewritten: a read in an expression, and
a whole-statement `=`, `|=`, `&=`, `^=`, `+=` or `-=`. Anything else (an
address taken, `++`, an assignment used as a value, a preprocessor line) is
left alone and still reaches the register through the trap.
"""
from __future__ import annotations

import re
from pathlib import Path

# The two trapped regions (runtime/io.h): 0x04000000 and 0x04100000, 64 KiB each.
ADDRESS = r"0x0?4[01]0[0-9a-fA-F]{4}"
SCALAR_WORDS = frozenset({
    "volatile", "const", "unsigned", "signed", "int", "short", "char", "long",
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
    "vu8", "vu16", "vu32", "vu64", "vs8", "vs16", "vs32", "vs64",
    "REGType8", "REGType16", "REGType32", "REGType64",
    "REGType8v", "REGType16v", "REGType32v", "REGType64v",
})
TYPE = r"[A-Za-z_][\w \t]*?"
# `*(T *)0x04000400`, optionally with the address in parentheses or suffixed u.
DEREF = re.compile(rf"\*[ \t]*\([ \t]*({TYPE})[ \t]*\*[ \t]*\)[ \t]*(?:\([ \t]*({ADDRESS})[uU]?[ \t]*\)|({ADDRESS})[uU]?)"
                   r"(?![\w\[])")
MACRO_DEFINE = re.compile(
    rf"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+\([ \t]*\*[ \t]*\([ \t]*({TYPE})[ \t]*\*[ \t]*\)[ \t]*"
    rf"\(?[ \t]*({ADDRESS})[uU]?[ \t]*\)?[ \t]*\)[ \t]*$", re.MULTILINE)
ANY_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)", re.MULTILINE)
PREPROCESSOR_LINE = re.compile(r"^[ \t]*#.*(?:\\\n.*)*$", re.MULTILINE)
COMPOUND = frozenset({"|", "&", "^", "+", "-"})


def is_scalar(type_text: str) -> bool:
    words = type_text.split()
    return bool(words) and all(w in SCALAR_WORDS for w in words)


def macros_in(text: str) -> dict[str, tuple[str, str]]:
    """Register macros a text defines: name -> (type, address)."""
    return {m[1]: (" ".join(m[2].split()), m[3]) for m in MACRO_DEFINE.finditer(text) if is_scalar(m[2])}


def header_macros(include_dir: Path) -> dict[str, tuple[str, str]]:
    """Register macros the decomp's headers define, minus names defined twice differently."""
    found: dict[str, tuple[str, str]] = {}
    clashing: set[str] = set()
    for header in include_dir.rglob("*.h"):
        for name, value in macros_in(header.read_text(encoding="utf-8", errors="replace")).items():
            if found.get(name, value) != value:
                clashing.add(name)
            found[name] = value
    return {n: v for n, v in found.items() if n not in clashing}


def _rhs_end(text: str, start: int) -> int | None:
    """The `;` ending a statement's right-hand side; None if a top-level comma
    (the comma operator) or an unbalanced bracket comes first."""
    depth = 0
    for i in range(start, len(text)):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth < 0:
                return None
        elif c == "," and depth == 0:
            return None
        elif c == ";" and depth == 0:
            return i
    return None


def _statement_start(text: str, at: int) -> bool:
    """Whether a statement can begin at `at`: after `;`, `{`, `}`, `)` (an if or
    loop header), `else`, `:` (a label) or at the start of a line."""
    before = text[:at].rstrip(" \t")
    if before == "" or before.endswith("\n"):
        return True
    return before[-1] in ";{})" or (before[-1] == ":" and not before.endswith("::")) or \
        re.search(r"\belse$", before) is not None


def _address_taken(prefix: str) -> bool:
    """Whether the text before a register ends in a unary `&` (not `a & b`, `&&`)."""
    if not prefix.endswith("&") or prefix.endswith("&&"):
        return False
    before = prefix[:-1].rstrip()
    return before == "" or before[-1] in "(,=;{}!~?:+-*/%<>|^&[" or re.search(r"\breturn$", before) is not None


FUNCTION_MACRO = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(", re.MULTILINE)
ALL_CAPS = re.compile(r"[A-Z][A-Z0-9_]*")


def _macro_argument(prefix: str, macros: set[str]) -> bool:
    """Whether the text before a register leaves it inside the arguments of a
    function-like macro (which may use it as an lvalue: `BG_PRIORITY(reg, 1)`):
    one the file defines, or any all-caps call."""
    depth = 0
    for i in range(len(prefix) - 1, -1, -1):
        c = prefix[i]
        if c in ")]":
            depth += 1
        elif c in "([":
            if depth == 0:
                if c == "[":
                    return False
                name = re.search(r"(\w+)\s*$", prefix[:i])
                return name is not None and (name[1] in macros or ALL_CAPS.fullmatch(name[1]) is not None)
            depth -= 1
        elif c in ";{}" and depth == 0:
            return False
    return False


def _kept_by_trap(prefix: str, after: str, macros: set[str]) -> bool:
    """An access left to the trap: its address taken, sizeof, `++`/`--`, or a
    function-like macro's argument."""
    return _address_taken(prefix) or prefix.endswith(("++", "--")) or \
        re.search(r"\bsizeof\s*\(?$", prefix) is not None or re.match(r"[ \t]*(\+\+|--)", after) is not None or \
        _macro_argument(prefix, macros)


def rewrite(text: str, headers: dict[str, tuple[str, str]]) -> str:
    """The text with plain register reads and stores as register-model calls."""
    local = macros_in(text)
    redefined = {m[1] for m in ANY_DEFINE.finditer(text)} - set(local)
    # A header's name the file declares as something else (a pointer variable
    # of its own) is not the header's macro there.
    redefined |= {n for n in headers if n not in local and re.search(
        rf"(?:\*|\b(?!return\b|case\b|else\b)[A-Za-z_]\w*)\s*(?:const\s+)?\b{n}\s*(?:=(?!=)|;|\[)", text)}
    registers = {n: v for n, v in {**headers, **local}.items() if n not in redefined}
    names = [n for n in registers if re.search(rf"\b{n}\b", text)]
    if not names and not DEREF.search(text):
        return text

    # Preprocessor lines (the macro definitions themselves among them) stay as
    # written: blank them for the scan, put them back after.
    kept: list[str] = []

    def keep(m: re.Match) -> str:
        kept.append(m[0])
        return f"\x00{len(kept) - 1}\x00"

    macros = {m[1] for m in FUNCTION_MACRO.finditer(text)}
    work = PREPROCESSOR_LINE.sub(keep, text)
    token = DEREF.pattern if not names else rf"(?:\b(?:{'|'.join(map(re.escape, names))})\b(?![\w\[])|{DEREF.pattern})"
    token_re = re.compile(token)

    def register_of(m: re.Match) -> tuple[str, str] | None:
        s = m[0]
        if s[0] == "*":
            d = DEREF.fullmatch(s)
            if d is None or not is_scalar(d[1]):
                return None
            return " ".join(d[1].split()), d[2] or d[3]
        return registers[s]

    out: list[str] = []
    pos = 0
    i = 0
    while True:
        m = token_re.search(work, i)
        if m is None:
            break
        reg = register_of(m)
        start, end = m.start(), m.end()
        if reg is None:
            i = end
            continue
        type_text, address = reg
        prefix = work[:start].rstrip()
        after = work[end:]
        op = re.match(r"[ \t]*(\+\+|--|[|&^+\-*/%]?=(?!=)|<<=|>>=)", after)
        if _kept_by_trap(prefix, after, macros):
            i = end
            continue
        if op is not None:
            assign = op[1]
            semi = _rhs_end(work, end + op.end())
            if semi is None or not _statement_start(work, start) or \
                    (assign != "=" and assign[0] not in COMPOUND) or assign in ("<<=", ">>="):
                i = end
                continue
            rhs = rewrite_reads(work[end + op.end():semi].strip(), token_re, register_of, macros)
            if assign == "=":
                value = rhs
            else:
                value = f"KHDAYS_IO_GET({type_text}, {address}) {assign[0]} ({rhs})"
            out.append(work[pos:start])
            out.append(f"KHDAYS_IO_SET({type_text}, {address}, {value})")
            pos = i = semi
            continue
        out.append(work[pos:start])
        out.append(f"KHDAYS_IO_GET({type_text}, {address})")
        pos = i = end
    out.append(work[pos:])
    result = "".join(out)
    return re.sub(r"\x00(\d+)\x00", lambda m: kept[int(m[1])], result)


def rewrite_reads(expr: str, token_re: re.Pattern, register_of, macros: set[str]) -> str:
    """Register reads inside an assignment's right-hand side."""
    def read(m: re.Match) -> str:
        reg = register_of(m)
        if reg is None:
            return m[0]
        if _kept_by_trap(expr[:m.start()].rstrip(), expr[m.end():], macros):
            return m[0]
        return f"KHDAYS_IO_GET({reg[0]}, {reg[1]})"
    return token_re.sub(read, expr)
