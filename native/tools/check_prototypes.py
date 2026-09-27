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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(P.ROOT / "build" / "native" / "gen"))
    args = ap.parse_args()

    defs: dict[str, tuple[str, int | None, str]] = {}  # name -> (ret, params, file)
    decls = collections.defaultdict(list)             # name -> [(ret, params, file)]
    for m in P.load_modules():
        for rel in m.files:
            src = P.DECOMP / rel
            if "asm_stubs" in rel or src.suffix == ".s" or not src.exists():
                continue
            text = P.strip_comments(P.transform(src.read_text(encoding="utf-8", errors="replace")))
            text = re.sub(r"^\s*#.*$", "", text, flags=re.MULTILINE)
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
                    # which parameters the body reads, by position
                    used = [bool(n) and re.search(rf"\b{re.escape(n)}\b", body) is not None
                            for n in param_names(params)]
                    returns = re.search(r"\breturn\s+[^;\s]", body) is not None
                    defs[name] = entry + (used, returns)
                else:
                    decls[name].append(entry)

    fewer = []
    phantom_return = []
    for name, (dret, dcount, dfile, used, returns) in sorted(defs.items()):
        for ret, count, rel in decls.get(name, []):
            if rel == dfile:
                continue
            # only parameters the definition actually reads matter
            if dcount is not None and count is not None and count < dcount and any(used[count:]):
                fewer.append((name, dfile, dcount, rel, count))
            if dret == "void" and not returns and ret != "void":
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
    print(f"{len(fewer)} declarations with fewer parameters than their definition, "
          f"{len(phantom_return)} that read a return value the definition does not give "
          f"(see {out / 'prototype_mismatches.txt'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
