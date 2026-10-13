"""Register accesses through a pointer variable, made calls to the register model.

The second form io_access.py's rewrite does not see: a variable initialised
to a register's address, then dereferenced --

    volatile unsigned short *ime = (volatile unsigned short *)0x04000208;
    unsigned old = *ime;      ->  KHDAYS_IO_GET(volatile unsigned short, (unsigned int)(ime))
    *ime = 0;                 ->  KHDAYS_IO_SET(volatile unsigned short, (unsigned int)(ime), 0)

(the SDK's inline OS_DisableInterrupts, copied into many files, the DMA
helpers' `p[1]`, the G3D node mixer's `registers->mode = 2`). The address is
the variable's value when the access runs, so a pointer that steps through
registers (`cntp += 3`) stays right; one that ends up outside the I/O
region is still a plain memory access (khdays_io_get/set fall back to it).

The same plain forms only, as io_access.py: a read of a scalar in an
expression, a whole-statement assignment; for a pointer to a register block
(a struct), only a whole-statement `=` to a member, through
KHDAYS_IO_SET_LV. A variable is followed only within the block that
declares it (or a whole file for a file-scope `const` pointer), and not at
all where a declaration of the same name might shadow it.
"""
from __future__ import annotations

import re

import io_access
from io_access import ADDRESS, COMPOUND, TYPE, is_scalar

DECLARATION = re.compile(
    rf"(?<![\w.>])((?:(?:static|const|register)\s+)*)({TYPE})\s*\*\s*(const\s+)?([A-Za-z_]\w*)\s*=\s*"
    rf"(&?\s*\(*\s*\(\s*{TYPE}\s*\*\s*\)\s*\(?\s*{ADDRESS}[^;]*);")
# A register block named by a macro (`#define SQRT_REGISTERS ((SqrtRegisters
# *)0x040002b0)`): followed like a file-scope const pointer.
POINTER_MACRO = re.compile(
    rf"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+\([ \t]*\([ \t]*({TYPE})[ \t]*\*[ \t]*\)[ \t]*"
    rf"\(?[ \t]*{ADDRESS}[uU]?[ \t]*\)?[ \t]*\)[ \t]*$", re.MULTILINE)
CAST_BEFORE = re.compile(rf"\*\s*\(\s*({TYPE})\s*\*\s*\)\s*$")
UNARY_BEFORE = "(,=;{}!~?:+-*/%<>|^&["
ASSIGNMENT = re.compile(r"[ \t]*([|&^+\-*/%]?=(?!=)|<<=|>>=|\+\+|--)")


def _unary_star(work: str, star: int) -> bool:
    """Whether the `*` at `star` dereferences (rather than multiplies)."""
    before = work[:star].rstrip()
    cast = re.search(r"\(\s*([A-Za-z_][\w \t]*?)\s*(\*?)\s*\)$", before)
    return before == "" or before[-1] in UNARY_BEFORE or before.endswith("\x00") or \
        re.search(r"\b(?:return|case|else)$", before) is not None or \
        (cast is not None and (cast[1] == "void" or is_scalar(cast[1])))


def _closing(text: str, open_at: int) -> int | None:
    """The index of the bracket closing the one at `open_at`."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    stack = []
    for i in range(open_at, len(text)):
        c = text[i]
        if c in pairs:
            stack.append(pairs[c])
        elif c in ")]}":
            if not stack or stack.pop() != c:
                return None
            if not stack:
                return i
    return None


def _scope_end(work: str, at: int) -> int | None:
    """The end of the block enclosing `at`; None at file scope."""
    depth = 0
    for i in range(at - 1, -1, -1):
        c = work[i]
        if c == "}":
            depth += 1
        elif c == "{":
            if depth == 0:
                return _closing(work, i)
            depth -= 1
    return None


def _accesses(work: str, name: str, start: int, end: int):
    """(access start, access end, address expression, member, type) for each
    dereference of `name` in [start, end): `*name`, `name[e]`, `*(name + e)`,
    `name->member`, and `*(T *)name` (type T; otherwise None, the pointee)."""
    for m in re.finditer(rf"(?<![\w.>]){re.escape(name)}\b", work[:end]):
        at = m.start()
        if at < start:
            continue
        after = work[m.end():]
        index = re.match(r"\s*\[", after)
        member = re.match(r"\s*->\s*([A-Za-z_]\w*)", after)
        star = work[:at].rstrip()
        if index:
            close = _closing(work, m.end() + index.end() - 1)
            if close is not None:
                yield at, close + 1, f"({name} + ({work[m.end() + index.end():close]}))", None, None
        elif member:
            yield at, m.end() + member.end(), None, member[1], None
        elif star.endswith("*") and _unary_star(work, len(star) - 1) and \
                not re.match(r"\s*[\(\[]|\s*->", after):
            yield len(star) - 1, m.end(), name, None, None
        elif star.endswith(")") and (cast := CAST_BEFORE.search(star)) and is_scalar(cast[1]) and \
                _unary_star(work, cast.start()) and not re.match(r"\s*[\(\[]|\s*->", after):
            yield cast.start(), m.end(), name, None, " ".join(cast[1].split())
        elif star.endswith("(") and re.match(r"\s*[+-]", after):
            open_at = len(star) - 1
            outer = work[:open_at].rstrip()
            close = _closing(work, open_at)
            if close is not None and outer.endswith("*") and _unary_star(work, len(outer) - 1):
                yield len(outer) - 1, close + 1, work[open_at:close + 1], None, None


def rewrite(text: str) -> str:
    """The text with plain dereferences of register pointer variables as
    register-model calls."""
    if not DECLARATION.search(text) and not POINTER_MACRO.search(text):
        return text
    kept: list[str] = []

    def keep(m: re.Match) -> str:
        kept.append(m[0])
        return f"\x00{len(kept) - 1}\x00"

    work = io_access.PREPROCESSOR_LINE.sub(keep, text)
    macros = set(io_access.FUNCTION_MACRO.findall(text))
    edits: list[tuple[int, int, str]] = []
    # (name, pointee, where its accesses start, where they end)
    pointers: list[tuple[str, str, int, int]] = []
    for d in DECLARATION.finditer(work):
        scope_end = _scope_end(work, d.start())
        if scope_end is None:
            if not d[3]:
                continue  # a file-scope pointer that could be reassigned anywhere
            scope_end = len(work)
        pointers.append((d[4], d[2], d.end(), scope_end))
    for d in POINTER_MACRO.finditer(text):
        pointers.append((d[1], d[2], 0, len(work)))
    for name, pointee, scope_start, scope_end in pointers:
        pointee = " ".join(w for w in pointee.split() if w not in ("static", "register"))
        body = work[scope_start:scope_end]
        # Another declaration of the name (`T name`, `T *name`) might shadow
        # this one: leave it.
        if re.search(rf"\b(?!return\b|case\b|else\b|goto\b)[A-Za-z_]\w*(?:\s+|\s*\*+\s*)(?:const\s+)?"
                     rf"{re.escape(name)}\s*[=;,\[)]", body):
            continue
        scalar = is_scalar(pointee)
        for start, end, address, member, cast in _accesses(work, name, scope_start, scope_end):
            prefix = work[:start].rstrip()
            if io_access._kept_by_trap(prefix, work[end:], macros):
                continue
            op = ASSIGNMENT.match(work, end)
            if op is not None and op[1] in ("++", "--"):
                continue
            if member is not None:
                # A register block's member: a plain store statement only.
                if op is None or op[1] != "=" or not io_access._statement_start(work, start):
                    continue
                semi = io_access._rhs_end(work, op.end())
                if semi is None:
                    continue
                value = work[op.end():semi].strip()
                edits.append((start, semi, f"KHDAYS_IO_SET_LV({work[start:end]}, {value})"))
                continue
            if not scalar and cast is None:
                continue
            target = f"{cast or pointee}, (unsigned int)({address})"
            if op is not None:
                assign = op[1]
                semi = io_access._rhs_end(work, op.end())
                if semi is None or not io_access._statement_start(work, start) or \
                        (assign != "=" and assign[0] not in COMPOUND) or assign in ("<<=", ">>="):
                    continue
                value = work[op.end():semi].strip()
                if assign != "=":
                    value = f"KHDAYS_IO_GET({target}) {assign[0]} ({value})"
                edits.append((start, semi, f"KHDAYS_IO_SET({target}, {value})"))
            else:
                edits.append((start, end, f"KHDAYS_IO_GET({target})"))
    # Apply from the end. A read inside a store's value (`p[0] = p[1]`) is
    # substituted into the store's replacement, which quotes the value as
    # written.
    edits.sort()
    merged: list[tuple[int, int, str]] = []
    for e in edits:
        if merged and e[0] < merged[-1][1]:
            outer = merged[-1]
            inner = work[e[0]:e[1]]
            head = outer[2].rfind(inner)
            if e[1] <= outer[1] and head >= 0:
                merged[-1] = (outer[0], outer[1], outer[2][:head] + e[2] + outer[2][head + len(inner):])
            continue
        merged.append(e)
    for start, end, replacement in reversed(merged):
        work = work[:start] + replacement + work[end:]
    return re.sub(r"\x00(\d+)\x00", lambda m: kept[int(m[1])], work)
