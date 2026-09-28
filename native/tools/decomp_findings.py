#!/usr/bin/env python3
"""Refresh the generated sections of docs/DECOMP_FINDINGS.md.

  python native/tools/decomp_findings.py

Each generated section sits between `<!-- BEGIN generated:NAME -->` and
`<!-- END generated:NAME -->`; the prose around them is written by hand. Run
native/tools/prepare.py first (the ABI lists come from its output).

  ambiguous  references to an address several overlays' functions share, and
             which of them the decomp's C names
  repairs    calls the native build rewrites from the ROM (abi_repairs.txt)
  gaps       calls it cannot rewrite mechanically (abi_gaps.txt)
"""
from __future__ import annotations

import collections
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare as P  # noqa: E402

DOC = P.ROOT / "docs" / "DECOMP_FINDINGS.md"
GEN = P.ROOT / "build" / "native" / "gen"


def ambiguous_rows() -> list[str]:
    modules = P.load_modules()
    function_at: dict[tuple[int, int], str] = {}
    for m in modules:
        mo = re.fullmatch(r"ov(\d+)", m.name)
        if mo:
            for n, k, a in m.symbols:
                if k == "function":
                    function_at[(int(mo[1]), a)] = n
    targets: dict[tuple[str, int], set[tuple[int, str]]] = collections.defaultdict(set)
    for m in modules:
        relocs = m.config / "relocs.txt"
        if not relocs.exists():
            continue
        for line in relocs.read_text(encoding="utf-8").splitlines():
            r = P.RELOC_OVERLAYS_RE.match(line)
            if not r:
                continue
            target = int(r[1], 16)
            candidates = {(o, function_at[(o, target)]) for o in
                          (int(x) for x in r[2].replace(" ", "").split(",")) if (o, target) in function_at}
            if len(candidates) >= 2:
                targets[(m.name, target)] |= candidates
    by_module = {m.name: m for m in modules}
    rows = []
    for (module, target), candidates in sorted(targets.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        names = {n for _, n in candidates}
        users: dict[str, set[str]] = collections.defaultdict(set)
        for rel in by_module[module].files:
            src = P.DECOMP / rel
            if src.suffix not in (".c", ".cpp") or not src.exists():
                continue
            text = src.read_text(encoding="utf-8", errors="replace")
            for n in names:
                defined = re.search(rf"^[A-Za-z_][\w \t\*]*\b{re.escape(n)}\s*\([^;{{}}]*\)\s*\{{", text, re.M)
                if re.search(rf"\b{re.escape(n)}\b", text) and not defined:
                    users[n].add(src.stem)
        alternatives = ", ".join(f"ov{o:03d} `{n}`" for o, n in sorted(candidates))
        for name, files in sorted(users.items()):
            rows.append(f"| `0x{target:08x}` | {module} | {', '.join(f'`{f}`' for f in sorted(files))} "
                        f"| `{name}` | {alternatives} |")
    return ["| Address | Referring module | Referring functions | The C names | All candidates |",
            "|---|---|---|---|---|"] + rows


def listing(path: Path) -> list[str]:
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    return ["```"] + lines + ["```"]


def main() -> int:
    for f in ("abi_repairs.txt", "abi_gaps.txt"):
        if not (GEN / f).exists():
            raise SystemExit(f"{GEN / f} missing: run native/tools/prepare.py first")
    decomp = subprocess.run(["git", "-C", str(P.DECOMP), "rev-parse", "--short=9", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    sections = {
        "revision": [f"Checked against khdays-decomp `{decomp}`."],
        "ambiguous": ambiguous_rows(),
        "repairs": listing(GEN / "abi_repairs.txt"),
        "gaps": listing(GEN / "abi_gaps.txt"),
    }
    text = DOC.read_text(encoding="utf-8")
    for name, lines in sections.items():
        pattern = re.compile(rf"(<!-- BEGIN generated:{name} -->\n).*?(<!-- END generated:{name} -->)", re.S)
        if not pattern.search(text):
            raise SystemExit(f"{DOC.name}: no generated:{name} section")
        text = pattern.sub(lambda m: m[1] + "\n".join(lines) + "\n" + m[2], text)
    DOC.write_text(text, encoding="utf-8", newline="\n")
    print(f"{DOC}: {len(sections['ambiguous']) - 2} ambiguous references, "
          f"{len(sections['repairs']) - 2} repairs, {len(sections['gaps']) - 2} gaps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
