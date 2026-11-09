#!/usr/bin/env python3
"""The static analysis of the port's own sources: builds the khdays-analyze target (/analyze, their real
flags, not linked) and lists every finding; exit 0 only with none. A finding that is no defect is
suppressed at its line: `#pragma warning(suppress: 6387) /* why */` (docs/BUILD.md, "The tools").
    python port/tools/analyze.py [--tree build/port/obj-release-x64]
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

FINDING = re.compile(r"^(?P<file>.+?)\((?P<line>\d+)\)\s*: warning (?P<code>C(?:6|26|28|33)\d{3}): (?P<text>.*)$")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", type=Path, default=ROOT / "build" / "port" / "obj-release-x64")
    args = ap.parse_args()
    if not (args.tree / "build.ninja").exists():
        sys.exit(f"{args.tree} is not a configured build tree (port/build.py)")
    env = build.msvc_env()
    # every object of the analysis again (a finding is said only when its file is compiled), and only
    # those: ninja's clean of the khdays-analyze target also deletes everything it depends on (the
    # libraries the port links, SDL's), so the next build compiled and linked those all again
    for objects in sorted((args.tree / "CMakeFiles").glob("khdays-analyze-*.dir")):
        shutil.rmtree(objects)
    run = subprocess.run(
        ["cmake", "--build", str(args.tree), "--target", "khdays-analyze", "--", "-k", "0"],
        env=env,
        capture_output=True,
        text=True,
        encoding="cp850",
        errors="replace",
    )
    out = run.stdout + run.stderr
    findings: dict[tuple[str, int, str], str] = {}
    others = 0
    for line in out.splitlines():
        match = FINDING.match(line.strip())
        if match:
            where = Path(match["file"]).resolve()
            try:
                shown = str(where.relative_to(ROOT))
            except ValueError:
                shown = str(where)
            if not where.is_relative_to(ROOT / "port"):
                others += 1  # the system's headers, a third party's: not the port's to answer
                continue
            findings[(shown, int(match["line"]), match["code"])] = match["text"].split(": Lines:")[0]
    for (file, line, code), text in sorted(findings.items()):
        print(f"{file}:{line}: {code}: {text}")
    errors = [line for line in out.splitlines() if " error C" in line or "FAILED:" in line]
    if run.returncode != 0 and not findings:
        print("\n".join(errors[:20]) or out[-2000:])
        return 2
    print(f"analysis: {len(findings)} finding(s) in the port's own sources ({others} in others' headers, not counted)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
