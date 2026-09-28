#!/usr/bin/env python3
"""Find calls the decompilation declares differently from the definition.

On the ARM9 a call passes its first four arguments in r0-r3 and returns in r0,
so a declaration with fewer parameters than the function really takes still
"works": the callee finds whatever the registers held -- often the result of
the previous call, which the ROM's code relies on. Natively (cdecl) the callee
reads stack garbage instead. This lists every function whose declarations in
other files take fewer parameters than its definition, or declare a value its
definition does not return (the ARM caller reads a leftover r0).

Usage: python native/tools/check_prototypes.py [--out build/native/gen]
"""
from __future__ import annotations

import argparse
import collections
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare as P  # noqa: E402

# `ret name(params)` followed by `{` (definition) or `;` (declaration).
FUNC_RE = re.compile(
    r"(?:^|;|\}|\n)\s*((?:extern\s+|static\s+|inline\s+|__inline\s+)*)"
    r"([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*([{;])",
    re.MULTILINE)
NOT_TYPES = {"return", "if", "while", "for", "switch", "sizeof", "else", "do", "case", "goto"}


def param_count(params: str) -> int | None:
    p = params.strip()
    if p in ("", "..."):
        return None  # unprototyped / variadic: says nothing
    if p == "void":
        return 0
    depth = 0
    count = 1
    for c in p:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            count += 1
    if "..." in p:
        return None
    return count


def param_names(params: str) -> list[str]:
    """The last identifier of each parameter (its name), '' when unnamed."""
    names = []
    depth = 0
    current = ""
    for c in params + ",":
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            words = re.findall(r"[A-Za-z_]\w*", current.split("[")[0])
            # a function-pointer parameter names itself inside (*name)
            fp = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)", current)
            names.append(fp[1] if fp else (words[-1] if len(words) > 1 else ""))
            current = ""
        else:
            current += c
    return names


def body_of(text: str, open_brace: int) -> str:
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    return text[open_brace:]


CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def split_arguments(text: str, open_paren: int) -> tuple[list[str], int]:
    """The top-level arguments of the call whose '(' is at open_paren, and the
    index just past its ')'."""
    depth = 0
    args = []
    current = ""
    for i in range(open_paren, len(text)):
        c = text[i]
        if c in "([{":
            depth += 1
            if depth == 1:
                continue
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                if current.strip() or args:
                    args.append(current.strip())
                return args, i + 1
        if c == "," and depth == 1:
            args.append(current.strip())
            current = ""
        else:
            current += c
    return args, len(text)


def is_declaration(text: str, start: int) -> bool:
    """Whether the `name(` at `start` declares rather than calls: a type name
    or `*` stands right before it (`void f(`, `int *f(`)."""
    before = text[:start].rstrip()
    if not before:
        return True
    word = re.search(r"([A-Za-z_]\w*)$", before)
    if word:
        return word[1] not in ("return", "else", "case", "do", "sizeof")
    if before.endswith("*"):
        return re.search(r"[A-Za-z_]\w*\s*\*+$", before) is not None and \
            not re.search(r"[=(,;{}]\s*\*+$", before)
    return False


def calls_in(body: str) -> list[tuple[str, list[str]]]:
    out = []
    for m in CALL_RE.finditer(body):
        if m[1] in NOT_TYPES or is_declaration(body, m.start()):
            continue
        args, _ = split_arguments(body, m.end() - 1)
        out.append((m[1], args))
    return out


def value_uses(name: str, text: str) -> tuple[bool, set[str]]:
    """How calls of `name` in `text` use its value: (consumed directly, the
    functions whose own value it becomes through `return name(...)`)."""
    direct = False
    through: set[str] = set()
    for m in re.finditer(rf"\b{re.escape(name)}\s*\(", text):
        if is_declaration(text, m.start()):
            continue
        before = text[:m.start()].rstrip()
        _, after_index = split_arguments(text, m.end() - 1)
        after = text[after_index:].lstrip()
        standalone = (not before or before[-1] in ";{}:" or before.endswith("else")) and after.startswith(";")
        if standalone:
            continue
        if re.search(r"\breturn\s*(?:\([\w\s\*]+\)\s*)*$", before) and after.startswith(";"):
            # returned as the enclosing function's value
            enclosing = None
            for d in FUNC_RE.finditer(text[:m.start()]):
                if d.group(5) == "{":
                    enclosing = d.group(3)
            if enclosing:
                through.add(enclosing)
                continue
        direct = True
    return direct, through


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(P.ROOT / "build" / "native" / "gen"))
    args = ap.parse_args()

    defs: dict[str, tuple] = {}            # name -> (ret, count, file, names, body, returns)
    decls = collections.defaultdict(list)  # name -> [(ret, count, file)]
    texts: dict[str, str] = {}
    for m in P.load_modules():
        for rel in m.files:
            src = P.DECOMP / rel
            if "asm_stubs" in rel or src.suffix == ".s" or not src.exists():
                continue
            text = P.strip_comments(P.transform(src.read_text(encoding="utf-8", errors="replace")))
            text = re.sub(r"^\s*#.*$", "", text, flags=re.MULTILINE)
            texts[rel] = text
            for f in FUNC_RE.finditer(text):
                storage, ret, name, params, end = f.groups()
                ret_words = ret.split()
                if not ret_words or ret_words[-1] in NOT_TYPES or name in NOT_TYPES:
                    continue
                if "static" in storage or "typedef" in ret_words:
                    continue
                entry = (" ".join(ret_words), param_count(params), rel)
                if end == "{":
                    body = body_of(text, f.end() - 1)
                    returns = re.search(r"\breturn\s+[^;\s]", body) is not None
                    defs[name] = entry + (param_names(params), body, returns)
                else:
                    decls[name].append(entry)

    # Which parameters each definition really reads. A parameter passed on
    # unchanged (possibly cast) as argument j of another defined function is
    # read only if that function reads its parameter j: iterate to the least
    # fixed point.
    forwards: dict[str, list[list[tuple[str, int]]]] = {}
    reads: dict[str, list[bool]] = {}
    for name, (_, _, _, names, body, _) in defs.items():
        direct = [False] * len(names)
        fwd: list[list[tuple[str, int]]] = [[] for _ in names]
        for k, pname in enumerate(names):
            if not pname:
                continue
            total = len(re.findall(rf"\b{re.escape(pname)}\b", body))
            # `(void)name;` only silences a warning
            total -= len(re.findall(rf"\(\s*void\s*\)\s*{re.escape(pname)}\s*;", body))
            forwarded = 0
            for callee, arguments in calls_in(body):
                for j, argument in enumerate(arguments):
                    if re.fullmatch(rf"(?:\(\s*[\w\s\*]+\)\s*)*{re.escape(pname)}", argument):
                        forwarded += 1
                        if callee in defs:
                            fwd[k].append((callee, j))
                        else:
                            direct[k] = True  # to something we cannot see into
            if total > forwarded:
                direct[k] = True
        reads[name] = direct
        forwards[name] = fwd
    changed = True
    while changed:
        changed = False
        for name, fwd in forwards.items():
            r = reads[name]
            for k, targets in enumerate(fwd):
                if not r[k] and any(j < len(reads[c]) and reads[c][j] for c, j in targets):
                    r[k] = True
                    changed = True

    # Whose value is consumed: used directly somewhere, or returned by a function
    # whose value is consumed (least fixed point over all calls).
    uses_direct: dict[str, bool] = collections.defaultdict(bool)
    uses_through: dict[str, set[str]] = collections.defaultdict(set)
    for rel, text in texts.items():
        for name in set(CALL_RE.findall(text)) & defs.keys():
            direct, through = value_uses(name, text)
            uses_direct[name] |= direct
            uses_through[name] |= through
    consumed = {n for n, d in uses_direct.items() if d}
    changed = True
    while changed:
        changed = False
        for name, via in uses_through.items():
            if name not in consumed and via & consumed:
                consumed.add(name)
                changed = True

    fewer = []
    phantom_return = []
    for name, (dret, dcount, dfile, names, _, returns) in sorted(defs.items()):
        for ret, count, rel in decls.get(name, []):
            if rel == dfile:
                continue
            # only parameters the definition actually reads matter, and only
            # if the file calls it
            if (dcount is not None and count is not None and count < dcount
                    and any(reads[name][count:]) and any(c == name for c, _ in calls_in(texts[rel]))):
                fewer.append((name, dfile, dcount, rel, count))
            # a void definition's "result" matters only where a call uses it
            if dret == "void" and not returns and ret != "void" and name in consumed:
                direct, through = value_uses(name, texts[rel])
                if direct or through & consumed:
                    phantom_return.append((name, dfile, rel, ret))

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with (out / "prototype_mismatches.txt").open("w", encoding="utf-8") as f:
        f.write("# declared with fewer parameters than the definition takes\n")
        for name, dfile, dcount, rel, count in fewer:
            f.write(f"{name}\t{dcount} params in {dfile}\t{count} in {rel}\n")
        f.write("# declared returning a value the definition does not return\n")
        for name, dfile, rel, ret in phantom_return:
            f.write(f"{name}\tvoid in {dfile}\t{ret} in {rel}\n")
    # The same, structured, for the ROM analysis (ghidra_abi.py).
    import json
    (out / "prototype_mismatches.json").write_text(json.dumps({
        "fewer": [{"callee": n, "def_file": d, "def_count": dc, "caller_file": r, "decl_count": c,
                   "reads": reads[n]}
                  for n, d, dc, r, c in fewer],
        "void_used": [{"function": n, "def_file": d, "caller_file": r}
                      for n, d, r, _ in phantom_return],
    }, indent=1), encoding="utf-8")
    print(f"{len(fewer)} declarations with fewer parameters than their definition, "
          f"{len(phantom_return)} that read a return value the definition does not give "
          f"(see {out / 'prototype_mismatches.txt'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
