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

  --missed        only the calls native/abi/ghidra_abi.txt has no findings
                  for (marked `no-call-found`, or new), into
                  ghidra_rescan_targets.txt (run the scan with the arguments
                  `ghidra_rescan_targets.txt ghidra_rescan.txt`)
  --next          the next round instead: the calls of functions the last
                  build widened (abi_next_targets.txt)
  --merge         add ghidra_rescan.txt's findings, in place of what the file
                  had for those calls
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare as P  # noqa: E402


ABI = P.ROOT / "native" / "abi" / "ghidra_abi.txt"


def scanned_pairs() -> tuple[set[tuple[str, str]], set[tuple[str, str]]]:
    """The calls scanned, and those of them the scan found no call for."""
    scanned, missed = set(), set()
    for line in ABI.read_text(encoding="utf-8").splitlines():
        w = line.split()
        if len(w) >= 5 and w[0] == "B":
            scanned.add((w[1], w[2]))
            if w[4] == "no-call-found":
                missed.add((w[1], w[2]))
    return scanned, missed


def merge(gen: Path) -> int:
    """Replace what the file says about each rescanned call (a
    `no-call-found` mark, or fewer registers) with the rescan's lines."""
    rescan = (gen / "ghidra_rescan.txt").read_text(encoding="utf-8").splitlines()
    done = {(line.split()[1], line.split()[2]) for line in rescan if line.startswith("B ")}
    kept = [line for line in ABI.read_text(encoding="utf-8").splitlines()
            if not (line.startswith("B ") and (line.split()[1], line.split()[2]) in done)]
    ABI.write_text("\n".join(kept + rescan) + "\n", encoding="utf-8")
    still = sum(1 for line in rescan if line.endswith(" no-call-found"))
    print(f"{len(done)} pairs rescanned, {still} still without a call")
    return 0


def main() -> int:
    gen = P.ROOT / "build" / "native" / "gen"
    if "--merge" in sys.argv:
        return merge(gen)
    data = json.loads((gen / "prototype_mismatches.json").read_text(encoding="utf-8"))
    where: dict[str, tuple[str, int]] = {}
    for m in P.load_modules():
        space = f"arm9_{m.name}" if m.name.startswith("ov") else "-"
        for name, kind, addr in m.symbols:
            if kind == "function":
                where[name] = (space, addr)
    lines = []
    missing = set()
    # functions go by their key (module@address), which the scan's output
    # keeps: the findings survive the decomp renaming them
    keys = P.function_keys(P.load_modules())
    for name in sorted({e["function"] for e in data["void_used"]}):
        if name not in where:
            missing.add(name)
            continue
        space, addr = where[name]
        lines.append(f"A {keys[name]} {space} {addr:08x}")
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
        lines.append(f"B {keys[caller]} {cs} {ca:08x} {keys[e['callee']]} {fs} {fa:08x} "
                     f"{e['decl_count']} {e['def_count']}")
    target = "ghidra_targets.txt"
    if "--next" in sys.argv:
        # abi_repair.next_targets names functions; the file keeps keys
        lines = []
        for line in (gen / "abi_next_targets.txt").read_text(encoding="utf-8").splitlines():
            w = line.split()
            if w and w[0] == "B" and w[1] in keys and w[4] in keys:
                w[1], w[4] = keys[w[1]], keys[w[4]]
                lines.append(" ".join(w))
        lines = sorted(set(lines))
        target = "ghidra_rescan_targets.txt"
    elif "--missed" in sys.argv:
        scanned, missed = scanned_pairs()
        lines = [line for line in lines if line.startswith("B ")
                 and ((line.split()[1], line.split()[4]) in missed
                      or (line.split()[1], line.split()[4]) not in scanned)]
        target = "ghidra_rescan_targets.txt"
    (gen / target).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(lines)} targets ({sum(l[0] == 'A' for l in lines)} returns, "
          f"{sum(l[0] == 'B' for l in lines)} call sites); no address for: {sorted(missing)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
