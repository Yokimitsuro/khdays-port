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
  narrowparams  parameters declared narrower than defined (abi_narrow_params.txt)

Only `revision` is required; the others are filled where the document has
them.
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


def narrow_params() -> list[str]:
    """The functions some file declares with a parameter narrower than the
    definition takes it (abi_narrow_params.txt), and the files that do."""
    spelled = {"unsigned char": "u8", "signed char": "s8", "char": "s8", "unsigned short": "u16",
               "unsigned short int": "u16", "short": "s16", "signed short": "s16", "short int": "s16"}
    args: dict[str, set[str]] = collections.defaultdict(set)
    files: dict[str, set[str]] = collections.defaultdict(set)
    for line in (GEN / "abi_narrow_params.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"(\w+) in (\S+): (.*) \(\d+ call", line)
        if m:
            for a in re.finditer(r"argument (\d+) ([\w ]+?)(?:,|$)", m[3]):
                args[m[1]].add(f"argument {a[1]} as {spelled.get(a[2].strip(), a[2].strip())}")
            files[m[1]].add(Path(m[2]).stem)
    return [f"- `{name}` ({', '.join(sorted(args[name]))}): {', '.join(f'`{f}`' for f in sorted(fs))}"
            for name, fs in sorted(files.items())]


def entries(path: Path) -> list[str]:
    return [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]


def listing(path: Path) -> list[str]:
    lines = entries(path)
    return ["```"] + lines + ["```"] if lines else ["*None at this revision.*"]


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
        "narrowparams": narrow_params(),
    }
    text = DOC.read_text(encoding="utf-8")
    filled = []
    for name, lines in sections.items():
        # the sections the document has; only the revision is required
        pattern = re.compile(rf"(<!-- BEGIN generated:{name} -->\n).*?(<!-- END generated:{name} -->)", re.S)
        if not pattern.search(text):
            if name == "revision":
                raise SystemExit(f"{DOC.name}: no generated:{name} section")
            continue
        text = pattern.sub(lambda m: m[1] + "\n".join(lines) + "\n" + m[2], text)
        filled.append(name)
    DOC.write_text(text, encoding="utf-8", newline="\n")
    counts = {"ambiguous": "ambiguous references", "repairs": "repairs", "gaps": "gaps",
              "narrowparams": "narrowly declared parameters"}
    size = {n: len(sections[n]) - (2 if n == "ambiguous" else 0) for n in filled}  # its table's head
    size.update(repairs=len(entries(GEN / "abi_repairs.txt")), gaps=len(entries(GEN / "abi_gaps.txt")))
    print(f"{DOC}: " + ", ".join(f"{size[n]} {counts[n]}" for n in filled if n in counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
