#!/usr/bin/env python3
"""Turn check_prototypes.py's findings into ROM addresses for the Ghidra
analysis (native/tools/ghidra/KhdaysAbiScan.java).

  A <function> <space> <address>                  void function whose value
                                                  callers use: what is in r0
                                                  at each of its returns?
  B <caller> <space> <address> <callee> <callee space> <callee address>
    <declared count> <defined count>              a call passing fewer
                                                  arguments than the callee
                                                  reads: what is in the
                                                  missing registers there?

Space is the Ghidra address space (arm9_ovNNN for an overlay, `-` for the
static module, ITCM and DTCM).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare as P  # noqa: E402


def main() -> int:
    gen = P.ROOT / "build" / "native" / "gen"
    data = json.loads((gen / "prototype_mismatches.json").read_text(encoding="utf-8"))
    where: dict[str, tuple[str, int]] = {}
    for m in P.load_modules():
        space = f"arm9_{m.name}" if m.name.startswith("ov") else "-"
        for name, kind, addr in m.symbols:
            if kind == "function":
                where[name] = (space, addr)
    lines = []
    missing = set()
    for name in sorted({e["function"] for e in data["void_used"]}):
        if name not in where:
            missing.add(name)
            continue
        space, addr = where[name]
        lines.append(f"A {name} {space} {addr:08x}")
    seen = set()
    for e in data["fewer"]:
        caller = Path(e["caller_file"]).stem
        key = (caller, e["callee"])
        if key in seen:
            continue
        seen.add(key)
        if caller not in where or e["callee"] not in where:
            missing.update(n for n in (caller, e["callee"]) if n not in where)
            continue
        cs, ca = where[caller]
        fs, fa = where[e["callee"]]
        lines.append(f"B {caller} {cs} {ca:08x} {e['callee']} {fs} {fa:08x} "
                     f"{e['decl_count']} {e['def_count']}")
    (gen / "ghidra_targets.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(lines)} targets ({sum(l[0] == 'A' for l in lines)} returns, "
          f"{sum(l[0] == 'B' for l in lines)} call sites); no address for: {sorted(missing)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
