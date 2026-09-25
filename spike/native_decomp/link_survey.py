#!/usr/bin/env python3
"""Spike step 2: link every object compile_survey.py produced into one image.

On the DS the overlays share address ranges and are loaded on demand; a
native build links them all side by side. This forces a link of all the
objects (as a DLL with no entry point, so nothing is discarded) and
classifies what the linker reports: symbols nothing defines, and symbols
defined more than once.

Usage: python spike/native_decomp/link_survey.py
Writes spike/native_decomp/link_survey.json and prints a summary.
"""
from __future__ import annotations

import collections
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compile_survey import ROOT, msvc_env  # noqa: E402

OUT = Path(__file__).resolve().parent
OBJ_DIR = ROOT / "build" / "spike_objs"
LINK_DIR = ROOT / "build" / "spike_link"

# Language-independent: the linker localizes its messages, so match the
# code and take the decorated symbol (x86 C names start with '_', C++ with '?').
UNRESOLVED_RE = re.compile(r"LNK20(?:19|01):.*?\s(?P<sym>[_?][^\s]+)")
DUPLICATE_RE = re.compile(
    r"(?P<obj>\S+\.obj) : (?:error|warning) LNK(?:2005|4006):.*?\s(?P<sym>[_?][^\s]+)"
    r".*?\s(?P<first>\S+\.obj)")


def classify(sym: str) -> str:
    name = sym.lstrip("_")
    if re.match(r"func_ov\d+_", name):
        return "overlay function"
    if re.match(r"func_0", name):
        return "main/itcm function"
    if re.match(r"data_ov\d+_", name):
        return "overlay data"
    if re.match(r"data_0", name):
        return "main data"
    if re.match(r"OVERLAY_\d+_ID", name) or "SDK_" in name or name.startswith("__"):
        return "linker/toolchain symbol"
    return "named (SDK/library/game)"


def main() -> int:
    env = msvc_env()
    link = shutil.which("link.exe", path=env.get("Path") or env.get("PATH"))
    if link is None:
        raise SystemExit("link.exe not found")
    objs = sorted(OBJ_DIR.glob("*.obj"))
    LINK_DIR.mkdir(parents=True, exist_ok=True)
    rsp = LINK_DIR / "objs.rsp"
    rsp.write_text("\n".join(f'"{o}"' for o in objs), encoding="utf-8")
    cmd = [link, "/nologo", "/DLL", "/NOENTRY", "/MACHINE:X86", "/SAFESEH:NO",
           "/FORCE:UNRESOLVED", "/FORCE:MULTIPLE", "/OPT:NOREF",
           f"/OUT:{LINK_DIR / 'decomp.dll'}", f"@{rsp}"]
    res = subprocess.run(cmd, capture_output=True, text=True, env=env,
                         encoding="mbcs", errors="replace")
    lines = res.stdout.splitlines()
    (LINK_DIR / "link.log").write_text(res.stdout, encoding="utf-8")

    unresolved = collections.Counter()
    for line in lines:
        m = UNRESOLVED_RE.search(line)
        if m:
            unresolved[m["sym"]] += 1
    duplicates = collections.defaultdict(set)
    for line in lines:
        m = DUPLICATE_RE.search(line)
        if m:
            duplicates[m["sym"]].update({m["obj"], m["first"]})

    print(f"{len(objs)} objects linked (exit {res.returncode})")
    print(f"{len(unresolved)} distinct unresolved symbols")
    for kind, n in collections.Counter(classify(s) for s in unresolved).most_common():
        print(f"  {kind}: {n}")
    print("  most referenced:", ", ".join(s for s, _ in unresolved.most_common(12)))
    print(f"{len(duplicates)} symbols defined more than once")
    for kind, n in collections.Counter(classify(s) for s in duplicates).most_common():
        print(f"  {kind}: {n}")
    print("  e.g.:", ", ".join(list(duplicates)[:10]))
    (OUT / "link_survey.json").write_text(json.dumps({
        "objects": len(objs),
        "unresolved": dict(unresolved.most_common()),
        "duplicates": {k: sorted(v) for k, v in duplicates.items()},
    }, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
