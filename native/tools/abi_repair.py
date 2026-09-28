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


def load_findings(path: Path) -> Findings:
    f = Findings()
    for line in path.read_text(encoding="utf-8").splitlines():
        if " <- " not in line:
            continue
        w = line.split(" ")
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


def depth_at(text: str, mask: bytearray, index: int) -> int:
    depth = 0
    for i in range(index):
        if mask[i]:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
    return depth


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
        captures: list[tuple[int, str]] = []
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
            elif kinds == {"call"} and k == 0 and len({s.target for s in alts}) == 1 \
                    and alts[0].target is not None:
                g = self.name_at(rel, alts[0].target)
                if g is None:
                    return f"previous callee 0x{alts[0].target:x} has no symbol"
                captures.append((k, g))
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
            for k, g in captures:
                prior = call_sites(text, mask, g, (d.body[0], start))
                if not prior:
                    return f"no call of {g} before {callee} in the C"
                more = self.value_edits(rel, g)
                if more is None:
                    return f"{g} gives no value to pass"
                edits += more
                ps, pc = prior[-1]
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
