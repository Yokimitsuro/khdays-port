"""Make the decompilation's calls say what the ARM code does.

The decomp's C matches the ROM byte for byte, yet some of it leans on the ARM
calling convention: a call passes fewer arguments than its callee reads (the
callee finds whatever r0-r3 held), or a caller uses the "result" of a
function defined void (whatever it left in r0). Natively those are garbage.

native/abi/ghidra_abi.txt records, from the ROM (native/tools/ghidra/
KhdaysAbiScan.java), what those registers really hold at each such call and
return. This module rewrites the C where that source is mechanical:

  returns   r0 is the result of the function's last call, or of a call it
            jumps to at the end, its first argument untouched, or a constant:
            the function returns that.
  arguments the missing register still holds the caller's own argument, the
            previous call's result (r0), a constant, or a data address: the
            call passes it.

Everything else becomes a stop that names the call (KHDAYS_ABI_GAP), resolved
from the ROM when the game reaches it. Struct arguments loaded into r1-r3
(`ldmia`) need nothing: on x86 a struct passed by value occupies the same
stack slots as its words.
"""
from __future__ import annotations

import collections
import re
from dataclasses import dataclass, field
from pathlib import Path

# --- The ROM findings --------------------------------------------------------------


@dataclass
class Source:
    kind: str                   # call, tail, entry, const, pool, struct, stack, other
    target: int | None = None   # call/tail: the callee's address, when direct
    value: int | None = None    # const/pool
    register: int | None = None  # entry: which argument register
    text: str = ""


def parse_source(s: str) -> Source:
    s = s.strip()
    if s.startswith("entry:r"):
        return Source("entry", register=int(s[7:]), text=s)
    if s.startswith("tail "):
        m = re.search(r"=> 0x([0-9a-f]+)", s) or re.search(r"\bb\w*\s+0x([0-9a-f]+)", s)
        return Source("tail", target=int(m[1], 16) if m else None, text=s)
    m = re.search(r" (?:bl|blx)\s+0x([0-9a-f]+)", s)
    if m:
        return Source("call", target=int(m[1], 16), text=s)
    if re.search(r" (?:bl|blx)\s+r\d+", s):
        return Source("call", text=s)  # through a register
    m = re.search(r" (mov|mvn)\s+r\d+,#(-?0x[0-9a-f]+|-?\d+)$", s)
    if m:
        v = int(m[2], 0)
        return Source("const", value=(~v if m[1] == "mvn" else v) & 0xffffffff, text=s)
    m = re.search(r" ldr\s+r\d+,\[0x[0-9a-f]+\] = 0x([0-9a-f]+)", s)
    if m:
        return Source("pool", value=int(m[1], 16), text=s)
    if re.search(r" ldmia\s+r\d+,\{r\d+", s):
        return Source("struct", text=s)
    return Source("other", text=s)


@dataclass
class Findings:
    # function -> alternatives at each return point
    returns: dict[str, list[list[Source]]] = field(default_factory=dict)
    # (caller, callee) -> call address -> argument index -> alternatives
    calls: dict[tuple[str, str], dict[str, dict[int, list[Source]]]] = field(default_factory=dict)


def load_findings(path: Path, names: dict[str, str] | None = None) -> Findings:
    """Functions are recorded by where they are, `module@address` (main, itcm,
    dtcm, ovNNN), which survives the decomp renaming them; `names` gives each
    key its current name."""
    f = Findings()
    for line in path.read_text(encoding="utf-8").splitlines():
        if " <- " not in line:
            continue
        w = line.split(" ")
        for i in range(1, 2 if w[0] == "A" else 3):
            if names is not None and "@" in w[i]:
                w[i] = names.get(w[i], w[i])
        sources = [parse_source(s) for s in line.split(" <- ", 1)[1].split(" | ")]
        if w[0] == "A":
            f.returns.setdefault(w[1], []).append(sources)
        elif w[0] == "B":
            reg = w[4]
            if reg.startswith("arg"):
                k, sources = int(reg[3:]), [Source("stack", text="stack")]
            else:
                k = int(reg[1:])
            f.calls.setdefault((w[1], w[2]), {}).setdefault(w[3], {})[k] = sources
    return f


# --- C text helpers ------------------------------------------------------------------


def code_mask(text: str) -> bytearray:
    """1 for code, 0 for comments, strings and character literals."""
    mask = bytearray(b"\x01") * len(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            mask[i:end] = bytes(end - i)
            i = end
        elif text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            mask[i:end] = bytes(end - i)
            i = end
        elif text[i] in "\"'":
            j = i + 1
            while j < n and text[j] != text[i] and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            end = min(j + 1, n)
            mask[i:end] = bytes(end - i)
            i = end
        else:
            i += 1
    return mask


def strip_comments_code(text: str, mask: bytearray) -> str:
    """text with its comments and literals blanked (mask from code_mask)."""
    return "".join(c if keep else " " for c, keep in zip(text, mask))


def matching(text: str, mask: bytearray, open_index: int) -> int:
    """Index of the bracket closing the one at open_index (code only)."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack = []
    for i in range(open_index, len(text)):
        if not mask[i]:
            continue
        c = text[i]
        if c in pairs:
            stack.append(pairs[c])
        elif c in ")}]":
            if stack:
                stack.pop()
            if not stack:
                return i
    return len(text)


EXTERN_C_OPEN = re.compile(r'\bextern\s*"C"\s*$')


def depth_at(text: str, mask: bytearray, index: int) -> int:
    """Brace depth at index; an `extern "C" {` block does not count (C++
    sources wrap their C definitions in one)."""
    counted: list[bool] = []
    for i in range(index):
        if mask[i]:
            if text[i] == "{":
                counted.append(not EXTERN_C_OPEN.search(text[max(0, i - 16):i]))
            elif text[i] == "}" and counted:
                counted.pop()
    return sum(counted)


@dataclass
class Definition:
    ret_start: int            # start of the line with the return type
    name_start: int
    params: tuple[int, int]   # inside the parentheses
    body: tuple[int, int]     # the braces


def find_definition(text: str, mask: bytearray, name: str) -> Definition | None:
    for m in re.finditer(rf"\b{re.escape(name)}\s*\(", text):
        if not mask[m.start()] or depth_at(text, mask, m.start()) != 0:
            continue
        close = matching(text, mask, m.end() - 1)
        # a function returning a function pointer: `T (*name(params))(args) {`
        tail = re.match(r"(\s*\)\s*\([^(){};]*\))*\s*\{", text[close + 1:])
        if not tail:
            continue
        after = tail
        open_brace = close + 1 + after.end() - 1
        line_start = text.rfind("\n", 0, m.start()) + 1
        return Definition(line_start, m.start(), (m.end(), close),
                          (open_brace, matching(text, mask, open_brace)))
    return None


def param_list(text: str, d: Definition) -> list[str]:
    inner = text[d.params[0]:d.params[1]].strip()
    if inner in ("", "void"):
        return []
    parts, depth, cur = [], 0, ""
    for c in inner:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += c
    parts.append(cur.strip())
    return parts


def split_top_level(inner: str) -> list[str]:
    """Comma-separated items outside any brackets."""
    parts, depth, cur = [], 0, ""
    for c in inner:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
    parts.append(cur)
    return parts


def param_name(param: str) -> str:
    fp = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)", param)
    if fp:
        return fp[1]
    words = re.findall(r"[A-Za-z_]\w*", param.split("[")[0])
    return words[-1] if len(words) > 1 else ""


NOT_TYPES = ("return", "else", "case", "do", "sizeof")
BASIC_TYPES = {"void", "int", "char", "short", "long", "unsigned", "signed", "float", "double",
               "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "fx16", "fx32", "fx64",
               "BOOL", "OSIntrMode", "vu8", "vu16", "vu32"}


def call_sites(text: str, mask: bytearray, name: str, span: tuple[int, int]) -> list[tuple[int, int]]:
    """(start of the name, index of the closing parenthesis) of each call."""
    out = []
    for m in re.finditer(rf"\b{re.escape(name)}\s*\(", text[span[0]:span[1]]):
        start = span[0] + m.start()
        if not mask[start]:
            continue
        before = text[:start].rstrip()
        word = re.search(r"([A-Za-z_]\w*)$", before)
        if word and word[1] not in NOT_TYPES:
            continue  # a declaration, not a call
        if before.endswith("&"):
            continue
        out.append((start, matching(text, mask, span[0] + m.end() - 1)))
    return out


def value_unused(text: str, mask: bytearray, start: int, close: int) -> bool:
    """Whether the call at start..close is a statement of its own (`f(...);`,
    also under `if (...)`, `else` or a `(void)` cast): its value goes nowhere."""
    if not re.match(r"\s*;", text[close + 1:]):
        return False
    before = text[:start].rstrip()
    if not before or before[-1] in ";{}:" or re.search(r"\belse$", before):
        return True
    if before.endswith(")"):
        # the parenthesis opening it: a control statement's, or a (void) cast
        depth, i = 0, len(before) - 1
        while i >= 0:
            if mask[i] and before[i] == ")":
                depth += 1
            elif mask[i] and before[i] == "(":
                depth -= 1
                if depth == 0:
                    break
            i -= 1
        head = before[:i].rstrip()
        return bool(re.search(r"\b(?:if|while|for)$", head)) or before[i + 1:-1].strip() == "void"
    return False


def declarations(text: str, mask: bytearray, name: str) -> list[tuple[int, int, int]]:
    """(start of the line, open paren, close paren) of each prototype of name
    (not the definition)."""
    out = []
    for m in re.finditer(rf"\b{re.escape(name)}\s*\(", text):
        if not mask[m.start()]:
            continue
        before = text[:m.start()].rstrip()
        word = re.search(r"([A-Za-z_]\w*|\*)$", before)
        if not word or word[1] in NOT_TYPES:
            continue
        close = matching(text, mask, m.end() - 1)
        if not re.match(r"\s*;", text[close + 1:]):
            continue
        out.append((text.rfind("\n", 0, m.start()) + 1, m.end() - 1, close))
    return out


VOID_BEFORE_NAME = re.compile(r"\bvoid\b(?=\s*[A-Za-z_]\w*\s*$)")


def apply_edits(text: str, edits: list[tuple[int, int, str]]) -> str:
    edits = sorted(set(edits), key=lambda e: (e[0], e[1]))
    for (s1, e1, _), (s2, e2, _) in zip(edits, edits[1:]):
        if s2 < e1:
            raise ValueError(f"overlapping repairs at {s1}-{e1} and {s2}-{e2}")
    for start, end, new in reversed(edits):
        text = text[:start] + new + text[end:]
    return text


# --- Parameters used as locals --------------------------------------------------------


def written_params(body: str, names: list[str]) -> list[int]:
    """Indexes of the parameters the body assigns to, or whose address it takes."""
    out = []
    for k, name in enumerate(names):
        if not name:
            continue
        n = re.escape(name)
        # `name = x`, `name += x`, `name++` (also `*name++`), `--name`, `&name`
        # -- not `*name =`, `p->name =`, `s.name =` or `name[i] =`
        for m in re.finditer(rf"\b{n}\s*(?:[-+*/%|&^]|<<|>>)?=(?!=)|(?:\+\+|--)\s*{n}\b|\b{n}\s*(?:\+\+|--)"
                             rf"|(?<![&\w])&\s*{n}\b(?!\s*(?:[\[.(]|->))", body):
            before = body[:m.start()]
            if m[0][0] == "&":
                # unary: not after an operand (`a & name` is a bitwise and)
                if re.search(r"(?:[\w)\]]|\breturn)\s*$", before) and not re.search(r"\breturn\s*$", before):
                    continue
                out.append(k)
                break
            member = re.search(r"(?:\.|->)\s*$", before)
            if m[0][0] in "+-" or (m[0].endswith(("++", "--")) and not member) \
                    or not (member or re.search(r"\*\s*$", before)):
                out.append(k)
                break
    return out


def pad_short_calls(texts: dict[str, str]) -> tuple[dict[str, str], list[str]]:
    """Calls passing fewer arguments than the definition takes, where the
    definition assigns to a parameter it was not given (a parameter used as a
    local, as the ARM code uses the register). On the ARM9 that writes a
    register; on x86 the missing argument's slot is the caller's own stack --
    its saved registers or its locals. Such calls pass zeros up to the last
    parameter written: a value written before any read is never seen, and one
    read first is what the ABI repairs pass (native/abi)."""
    def_file: dict[str, str] = {}
    for rel, text in texts.items():
        for m in re.finditer(r"^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", text, re.MULTILINE):
            def_file.setdefault(m[1], rel)
    masks: dict[str, bytearray] = {}

    def mask(rel: str) -> bytearray:
        if rel not in masks:
            masks[rel] = code_mask(texts[rel])
        return masks[rel]

    needs: dict[str, tuple[int, list[str]]] = {}
    for name, rel in def_file.items():
        d = find_definition(texts[rel], mask(rel), name)
        if d is None:
            continue
        params = param_list(texts[rel], d)
        names = [param_name(p) for p in params]
        code = strip_comments_code(texts[rel][d.body[0]:d.body[1]], mask(rel)[d.body[0]:d.body[1]])
        written = written_params(code, names)
        if written:
            needs[name] = (max(written) + 1, [names[k] for k in written])
    edits: dict[str, list[tuple[int, int, str]]] = collections.defaultdict(list)
    report = []
    for name, (count, which) in sorted(needs.items()):
        pattern = re.compile(rf"\b{re.escape(name)}\s*\(")
        for rel, text in texts.items():
            if name not in text or not pattern.search(text):
                continue
            short = []
            # prototypes and the definition are no calls (`extern T *f();`
            # has no word before the name)
            not_calls = {open_paren for _, open_paren, _ in declarations(text, mask(rel), name)}
            own = find_definition(text, mask(rel), name) if rel == def_file[name] else None
            for start, close in call_sites(text, mask(rel), name, (0, len(text))):
                if text.find("(", start) in not_calls or (own and start == own.name_start):
                    continue
                inner = text[text.find("(", start) + 1:close].strip()
                passed = 0 if not inner else len(split_top_level(inner))
                if passed < count:
                    short.append((close, inner, passed))
            if not short:
                continue
            for close, inner, passed in short:
                zeros = ", ".join("0" for _ in range(passed, count))
                edits[rel].append((close, close, (", " if inner else "") + zeros))
            for _, open_paren, close_paren in declarations(text, mask(rel), name):
                inner = text[open_paren + 1:close_paren].strip()
                if inner == "void":
                    edits[rel].append((open_paren + 1, close_paren,
                                       ", ".join(f"int khdays_p{k}" for k in range(count))))
                elif inner:
                    have = len(split_top_level(inner))
                    if have < count:
                        edits[rel].append((close_paren, close_paren, ", " + ", ".join(
                            f"int khdays_p{k}" for k in range(have, count))))
            report.append(f"{name} in {rel}: {len(short)} call(s) passing "
                          f"{min(p for _, _, p in short)} of {count} (the definition writes "
                          f"{', '.join(which)})")
    return {rel: apply_edits(texts[rel], e) for rel, e in edits.items()}, report


NARROW_TYPE = re.compile(r"^(?:u8|s8|u16|s16|(?:unsigned\s+|signed\s+)?char|(?:unsigned\s+|signed\s+)?short(?:\s+int)?)$")
PROTOTYPE = re.compile(r"^[ \t]*(?:extern\s+)?[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\(([^;{}()]*)\)\s*;", re.MULTILINE)


def param_type(param: str) -> str:
    """The type of a parameter without its name and qualifiers ('ptr' for
    pointers, arrays and functions)."""
    p = re.sub(r"\b(?:const|volatile|register)\b", " ", param)
    p = re.sub(r"\s+", " ", p).strip()
    if any(c in p for c in "*[("):
        return "ptr"
    name = param_name(p)
    return p[:-len(name)].strip() if name and p.endswith(name) and p != name else p


def argument_spans(text: str, mask: bytearray, open_paren: int, close: int) -> list[tuple[int, int]]:
    """(start, end) of each argument of the call whose parentheses are at
    open_paren and close, surrounding blanks left out."""
    spans, depth, start = [], 0, open_paren + 1
    for i in range(open_paren + 1, close + 1):
        c = text[i]
        if not mask[i] and i != close:
            continue
        if c in "([{" and i != close:
            depth += 1
        elif c in ")]}" and i != close:
            depth -= 1
        elif (c == "," and depth == 0) or i == close:
            s, e = start, i
            while s < e and text[s].isspace():
                s += 1
            while e > s and text[e - 1].isspace():
                e -= 1
            if e > s:
                spans.append((s, e))
            start = i + 1
    return spans


def widen_narrow_params(texts: dict[str, str]) -> tuple[dict[str, str], list[str]]:
    """Prototypes declaring a parameter narrower (char, short) than the
    definition takes it. The ARM caller converts the argument to the narrow
    type and passes it extended in its register, which the definition reads
    whole; an x86 caller defines only the narrow part of the stack slot (an
    optimizing MSVC leaves the rest as it was). Such a prototype takes an int
    there, and each call in its file converts the argument to the narrow type
    itself: the same value, extended."""
    definitions: dict[str, list[str]] = {}
    for rel, text in texts.items():
        for m in re.finditer(r"^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{", text, re.MULTILINE):
            if m[1] not in definitions and m[1] not in NOT_TYPES + ("if", "while", "for", "switch"):
                definitions[m[1]] = [param_type(p) for p in split_top_level(m[2])
                                     if p.strip() not in ("", "void")]
    out: dict[str, str] = {}
    report = []
    for rel, text in texts.items():
        mask = None
        narrow: dict[str, dict[int, str]] = {}
        for m in PROTOTYPE.finditer(text):
            defined = definitions.get(m[1])
            if defined is None:
                continue
            params = [p for p in split_top_level(m[2]) if p.strip() not in ("", "void")]
            for k, p in enumerate(params):
                t = param_type(p)
                if k < len(defined) and NARROW_TYPE.match(t) and not NARROW_TYPE.match(defined[k]):
                    narrow.setdefault(m[1], {})[k] = t
        if not narrow:
            continue
        mask = code_mask(text)
        edits: list[tuple[int, int, str]] = []
        for name, ks in narrow.items():
            prototypes = declarations(text, mask, name)
            for _, open_paren, close in prototypes:
                for k, (s, e) in enumerate(argument_spans(text, mask, open_paren, close)):
                    if k in ks:
                        pname = param_name(text[s:e])
                        edits.append((s, e, "int" + (f" {pname}" if pname else "")))
            calls = 0
            not_calls = {open_paren for _, open_paren, _ in prototypes}
            for start, close in call_sites(text, mask, name, (0, len(text))):
                open_paren = text.find("(", start)
                if open_paren in not_calls:
                    continue  # `extern T *f(...);` has no word before the name
                for k, (s, e) in enumerate(argument_spans(text, mask, open_paren, close)):
                    if k in ks:
                        edits.append((s, s, f"({ks[k]})("))
                        edits.append((e, e, ")"))
                calls += 1
            report.append(f"{name} in {rel}: " + ", ".join(f"argument {k} {t}" for k, t in sorted(ks.items()))
                          + f" ({calls} call(s))")
        out[rel] = apply_edits(text, edits)
    return out, report


# --- Repairs ---------------------------------------------------------------------------


@dataclass
class Plan:
    edits: dict[str, list[tuple[int, int, str]]] = field(
        default_factory=lambda: collections.defaultdict(list))
    applied: list[str] = field(default_factory=list)
    gaps: list[str] = field(default_factory=list)


class Repairer:
    def __init__(self, texts: dict[str, str], module_of: dict[str, str],
                 functions: dict[str, dict[int, str]], findings: Findings):
        self.texts = texts
        self.module_of = module_of
        self.functions = functions  # module -> address -> function name
        self.findings = findings
        self.masks: dict[str, bytearray] = {}
        self.def_file: dict[str, str] = {}
        for rel, text in texts.items():
            for m in re.finditer(r"^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{",
                                 text, re.MULTILINE):
                self.def_file.setdefault(m[1], rel)
            # returning a function pointer: `T (*name(params))(args) {`
            for m in re.finditer(r"^[A-Za-z_][\w \t\*]*\(\s*\*\s*([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\)"
                                 r"\s*\([^;{}]*\)\s*\{", text, re.MULTILINE):
                self.def_file.setdefault(m[1], rel)
        self.plan = Plan()
        # definitions that must take more parameters: name -> count
        self.extend: dict[str, int] = {}
        # functions a manual fix (prepare.py ABI_FIXES) has already rewritten
        self.fixed: set[str] = set()
        # hardware-layer functions (native/hal) that return a value, as their
        # assembly does in r0
        self.hal_values: set[str] = set()
        hal = Path(__file__).resolve().parents[1] / "hal"
        for source in hal.glob("*.c"):
            for m in re.finditer(r"^(?!static\b)([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{",
                                 source.read_text(encoding="utf-8"), re.MULTILINE):
                if m[1].split()[-1:] != ["void"] or "*" in m[1]:
                    self.hal_values.add(m[2])

    def mask(self, rel: str) -> bytearray:
        if rel not in self.masks:
            self.masks[rel] = code_mask(self.texts[rel])
        return self.masks[rel]

    def name_at(self, rel: str, address: int | None) -> str | None:
        if address is None:
            return None
        module = self.module_of.get(rel, "main")
        for m in (module, "main", "itcm", "dtcm"):
            name = self.functions.get(m, {}).get(address)
            if name:
                return name
        return None

    def returns_value(self, name: str) -> bool:
        """Whether name's definition gives a value (or will, once repaired)."""
        if name in self.findings.returns or name in self.hal_values:
            return True
        rel = self.def_file.get(name)
        if rel is None:
            return False
        d = find_definition(self.texts[rel], self.mask(rel), name)
        return d is not None and not re.search(r"\bvoid\s*$", self.texts[rel][d.ret_start:d.name_start])

    def value_edits(self, rel: str, callee: str) -> list[tuple[int, int, str]] | None:
        """Edits giving callee's prototypes in rel a value, or None if callee
        gives none to read."""
        if not self.returns_value(callee):
            return None
        text, mask = self.texts[rel], self.mask(rel)
        edits = []
        for line_start, open_paren, _ in declarations(text, mask, callee):
            v = VOID_BEFORE_NAME.search(text[line_start:open_paren])
            if v:
                edits.append((line_start + v.start(), line_start + v.end(), "int"))
        return edits

    # A function defined void whose value callers use.
    def plan_return(self, name: str) -> str | None:
        """None when planned, else why not."""
        rel = self.def_file.get(name)
        if rel is None:
            return "no definition"
        alternatives = [s for point in self.findings.returns.get(name, []) for s in point]
        kinds = {s.kind for s in alternatives}
        text, mask = self.texts[rel], self.mask(rel)
        d = find_definition(text, mask, name)
        if d is None:
            return "definition not found"
        void = re.search(r"\bvoid\s*$", text[d.ret_start:d.name_start])
        if not void:
            return "return type is not plain void"
        params = param_list(text, d)
        edits: list[tuple[int, int, str]] = []
        captures: list[tuple[int, int]] = []
        callee = ""
        if kinds == {"entry"} and {s.register for s in alternatives} == {0} and params:
            p0 = param_name(params[0])
            body = text[d.body[0]:d.body[1]]
            if not p0 or re.search(rf"\b{p0}\s*(?:[-+*/|&^]?=(?!=)|\+\+|--)|(?:\+\+|--)\s*{p0}\b", body):
                return "first argument reassigned"
            init = f"(int){p0}"
        elif kinds == {"const"} and len({s.value for s in alternatives}) == 1:
            init = f"(int)0x{alternatives[0].value:x}"
        elif kinds and kinds <= {"call", "tail"} and len({s.target for s in alternatives}) == 1 \
                and alternatives[0].target is not None:
            callee = self.name_at(rel, alternatives[0].target) or ""
            if not callee:
                return f"callee 0x{alternatives[0].target:x} has no symbol"
            captures = call_sites(text, mask, callee, d.body)
            if not captures:
                return f"no call of {callee} in the C"
            more = self.value_edits(rel, callee)
            if more is None:
                return f"{callee} gives no value either"
            edits += more
            init = "0"
        else:
            return "sources: " + "; ".join(sorted({s.text for s in alternatives}))
        edits.append((d.ret_start + void.start(), d.ret_start + void.start() + 4, "int"))
        for line_start, open_paren, _ in declarations(text, mask, name):
            v = VOID_BEFORE_NAME.search(text[line_start:open_paren])
            if v:
                edits.append((line_start + v.start(), line_start + v.end(), "int"))
        edits.append((d.body[0] + 1, d.body[0] + 1,
                      f"\n    int khdays_r0 = {init};  /* r0 as the ROM leaves it (native/abi) */"))
        for start, close in captures:
            edits.append((start, start, "(khdays_r0 = (int)"))
            edits.append((close + 1, close + 1, ")"))
        for m in re.finditer(r"\breturn\s*;", text[d.body[0]:d.body[1]]):
            at = d.body[0] + m.start()
            if mask[at]:
                edits.append((at, at + len(m[0]), "return khdays_r0;"))
        edits.append((d.body[1], d.body[1], "    return khdays_r0;\n"))
        self.plan.edits[rel] += edits
        self.plan.applied.append(f"{name} returns " + (f"the result of {callee}" if captures else init))
        return None

    # A call passing fewer arguments than its callee reads.
    def plan_call(self, caller: str, callee: str, declared: int, defined: int) -> str | None:
        rel = self.def_file.get(caller)
        if rel is None:
            return "caller definition not found"
        text, mask = self.texts[rel], self.mask(rel)
        d = find_definition(text, mask, caller)
        if d is None:
            return "caller definition not found"
        caller_params = param_list(text, d)
        # the callee as a manual fix (ABI_FIXES) defines it now
        callee_rel = self.def_file.get(callee)
        if callee_rel is not None and callee in self.fixed:
            cd = find_definition(self.texts[callee_rel], self.mask(callee_rel), callee)
            if cd is not None:
                defined = min(defined, len(param_list(self.texts[callee_rel], cd)))
        if defined <= declared:
            return "resolved"
        # A 64-bit parameter takes two argument words (r1:r2, or two stack
        # slots on x86 alike): a prototype counting them covers the definition.
        for _, open_paren, close in declarations(text, mask, callee):
            params = [p for p in split_top_level(text[open_paren + 1:close]) if p.strip() not in ("", "void")]
            wide = sum(1 for p in params if re.search(r"\b(?:u64|s64|fx64|double)\b|\blong\s+long\b", p))
            if params and len(params) + wide >= defined:
                return "resolved"
        # every call already passes them (a manual fix, ABI_FIXES)
        present = call_sites(text, mask, callee, d.body)
        if present and all(len(split_top_level(text[text.find("(", s) + 1:c].strip())) >= defined
                           for s, c in present if text[text.find("(", s) + 1:c].strip()):
            if all(text[text.find("(", s) + 1:c].strip() for s, c in present):
                return "resolved"
        # The caller expects a struct by value where the definition takes the
        # ARM ABI's result pointer: the "missing" first argument is that
        # pointer, which x86 passes the same way for a struct over 8 bytes
        # (the 8-byte ones are fixed by hand, prepare.py ABI_FIXES).
        for line_start, open_paren, _ in declarations(text, mask, callee):
            head = re.sub(r"\b(?:extern|static|inline|const|volatile)\b", "", text[line_start:open_paren])
            words = re.findall(r"[A-Za-z_]\w*|\*", head)[:-1]
            if words and "*" not in words and words[-1] not in BASIC_TYPES and defined - declared == 1:
                return "struct-by-value"
        sources_k: dict[int, list[Source]] = collections.defaultdict(list)
        for regs in self.findings.calls.get((caller, callee), {}).values():
            for k, alternatives in regs.items():
                sources_k[k] += alternatives
        exprs: dict[int, str] = {}
        captures: list[tuple[int, list[str]]] = []
        for k in range(declared, defined):
            alts = sources_k.get(k, [])
            kinds = {s.kind for s in alts}
            if not alts:
                return f"no finding for argument {k}"
            if kinds == {"struct"}:
                return "struct-by-value"
            if kinds == {"entry"} and {s.register for s in alts} == {k}:
                if k < len(caller_params) and param_name(caller_params[k]):
                    exprs[k] = param_name(caller_params[k])
                elif k >= len(caller_params):
                    # the caller receives it too: its definition takes it
                    self.extend[caller] = max(self.extend.get(caller, 0), k + 1)
                    exprs[k] = f"khdays_arg{k}"
                else:
                    return f"argument {k} is an unnamed parameter of the caller"
            elif kinds == {"const"} and len({s.value for s in alts}) == 1:
                exprs[k] = f"(int)0x{alts[0].value:x}"
            elif kinds == {"pool"} and len({s.value for s in alts}) == 1 \
                    and self.name_at(rel, alts[0].value) is None:
                exprs[k] = f"(int)0x{alts[0].value:x}"  # data sits at its DS address natively
            elif kinds == {"call"} and k == 0 and all(s.target is not None for s in alts):
                # each call passes the result of the call before it (several
                # sites may follow different calls)
                gs = []
                for target in sorted({s.target for s in alts}):
                    g = self.name_at(rel, target)
                    if g is None:
                        return f"previous callee 0x{target:x} has no symbol"
                    gs.append(g)
                captures.append((k, gs))
                exprs[k] = f"khdays_c{k}"
            else:
                return "argument %d: %s" % (k, "; ".join(sorted({s.text for s in alts})))
        sites = call_sites(text, mask, callee, d.body)
        if not sites:
            return "no call in the C"
        edits: list[tuple[int, int, str]] = []
        for start, close in sites:
            inner = text[text.find("(", start) + 1:close].strip()
            passed = 0 if not inner else len(split_top_level(inner))
            missing = [exprs[k] for k in range(max(passed, declared), defined)]
            if missing:
                edits.append((close, close, (", " if inner else "") + ", ".join(missing)))
            for k, gs in captures:
                # the closest call before this one of those the ROM names
                prior = [(ps, pc, g) for g in gs for ps, pc in call_sites(text, mask, g, (d.body[0], start))]
                if not prior:
                    return f"no call of {' or '.join(gs)} before {callee} in the C"
                ps, pc, g = max(prior)
                more = self.value_edits(rel, g)
                if more is None:
                    return f"{g} gives no value to pass"
                edits += more
                edits.append((ps, ps, f"(khdays_c{k} = (int)"))
                edits.append((pc + 1, pc + 1, ")"))
        for k, _ in captures:
            edits.append((d.body[0] + 1, d.body[0] + 1,
                          f"\n    int khdays_c{k} = 0;  /* r{k} as the ROM passes it (native/abi) */"))
        for line_start, open_paren, close_paren in declarations(text, mask, callee):
            inner = text[open_paren + 1:close_paren].strip()
            if inner in ("", "void"):
                edits.append((open_paren + 1, close_paren,
                              ", ".join(f"int khdays_p{k}" for k in range(defined))))
            else:
                have = len(split_top_level(inner))
                if have < defined:
                    edits.append((close_paren, close_paren, ", " + ", ".join(
                        f"int khdays_p{k}" for k in range(have, defined))))
        self.plan.edits[rel] += edits
        self.plan.applied.append(f"{caller} -> {callee}: passes " +
                                 ", ".join(exprs[k] for k in range(declared, defined)))
        return None

    def gap_call(self, caller: str, callee: str, why: str, needed: int = 0) -> None:
        rel = self.def_file.get(caller)
        message = f"{callee} called from {caller} without what the ROM passes ({why})"
        self.plan.gaps.append(message)
        if rel is None:
            return
        text, mask = self.texts[rel], self.mask(rel)
        d = find_definition(text, mask, caller)
        span = d.body if d else (0, len(text))
        quoted = message.replace("\\", "\\\\").replace('"', "'")
        for start, close in call_sites(text, mask, callee, span):
            inner = text[text.find("(", start) + 1:close].strip()
            if needed and inner and len(split_top_level(inner)) >= needed:
                continue  # this call already passes them
            # stop before the call, keeping its type for the expression around it
            self.plan.edits[rel].append((start, start, f'(khdays_abi_gap("{quoted}"), '))
            self.plan.edits[rel].append((close + 1, close + 1, ")"))

    def plan_extensions(self) -> None:
        """Give each function whose own arguments pass through more parameters
        (after the calls are planned): its definition and prototypes there."""
        for name, count in sorted(self.extend.items()):
            rel = self.def_file[name]
            text, mask = self.texts[rel], self.mask(rel)
            d = find_definition(text, mask, name)
            have = len(param_list(text, d))
            if count <= have:
                continue
            extra = [f"int khdays_arg{k}" for k in range(have, count)]
            spans = [(d.params[0], d.params[1])] + [(o + 1, c) for _, o, c in declarations(text, mask, name)]
            for open_at, close_at in spans:
                inner = text[open_at:close_at].strip()
                if inner in ("", "void"):
                    self.plan.edits[rel].append((open_at, close_at, ", ".join(extra)))
                else:
                    self.plan.edits[rel].append((close_at, close_at, ", " + ", ".join(extra)))
            self.plan.applied.append(f"{name} takes {count} parameters (its own r{have}..r{count - 1} "
                                     "pass through)")

    def next_targets(self, where: dict[str, tuple[str, int]]) -> list[str]:
        """Ghidra targets for the next round: calls of the extended functions
        passing fewer arguments than they now take, and void callees whose
        value a planned return or capture needs."""
        out = []
        for name, count in sorted(self.extend.items()):
            fs, fa = where[name]
            for rel, text in self.texts.items():
                if name not in text or rel == self.def_file.get(name):
                    continue
                mask = self.mask(rel)
                for m in re.finditer(r"^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{",
                                     text, re.MULTILINE):
                    d = find_definition(text, mask, m[1])
                    if not d or m[1] not in where:
                        continue
                    passed = []
                    for start, close in call_sites(text, mask, name, d.body):
                        inner = text[text.find("(", start) + 1:close].strip()
                        passed.append(0 if not inner else len(split_top_level(inner)))
                    if passed and min(passed) < count:
                        cs, ca = where[m[1]]
                        out.append(f"B {m[1]} {cs} {ca:08x} {name} {fs} {fa:08x} {min(passed)} {count}")
        for gap in self.plan.gaps:
            m = re.search(r"'s value: (\w+) gives no value either", gap) or \
                re.search(r": (\w+) gives no value to pass", gap)
            if m and m[1] in where and m[1] not in self.findings.returns:
                s, a = where[m[1]]
                out.append(f"A {m[1]} {s} {a:08x}")
        return sorted(set(out))

    def result(self) -> dict[str, str]:
        self.plan_extensions()
        return {rel: apply_edits(self.texts[rel], edits) for rel, edits in self.plan.edits.items()}
