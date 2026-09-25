#!/usr/bin/env python3
"""Spike: how much of the khdays-decomp C compiles natively?

Compiles every C/C++ source of the pinned decomp (external/khdays-decomp,
src/ and libs/) with MSVC for 32-bit x86 -- the decomp stores pointers in
`int`, so a 64-bit build is out -- and classifies the failures. Nothing is
linked; this measures the compile step only.

Usage: python spike/native_decomp/compile_survey.py [--jobs N] [--limit N]
Writes spike/native_decomp/survey.json and prints a summary.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DECOMP = ROOT / "external" / "khdays-decomp"
OUT = Path(__file__).resolve().parent
VCVARS = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community"
              r"\VC\Auxiliary\Build\vcvarsall.bat")

# mwcc's own flags are -lang c99 -char signed -enum int -gccext,on. MSVC's
# char is signed and enums are int already; C17 is the closest C mode.
C_FLAGS = ["/nologo", "/c", "/std:c17", "/W0", "/TC", "/D_CRT_SECURE_NO_WARNINGS"]
CPP_FLAGS = ["/nologo", "/c", "/std:c++17", "/W0", "/TP"]

ERROR_RE = re.compile(r"^(?P<file>.+?)\((?P<line>\d+)\): (?:fatal )?error (?P<code>C\d+): (?P<msg>.*)$")


def msvc_env() -> dict[str, str]:
    out = subprocess.run(
        f'"{VCVARS}" x86 >nul && set', shell=True, capture_output=True, text=True,
        check=True)
    env = {}
    for line in out.stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k] = v
    env["VSLANG"] = "1033"  # English diagnostics
    return env


def find_cl(env: dict[str, str]) -> str:
    path = env.get("Path") or env.get("PATH") or ""
    cl = shutil.which("cl.exe", path=path)
    if cl is None:
        raise SystemExit("cl.exe not found in the vcvarsall environment")
    return cl


def module_of(path: Path) -> str:
    rel = path.relative_to(DECOMP).parts
    if rel[0] == "libs":
        return "/".join(rel[:3]) if len(rel) > 3 else "/".join(rel[:2])
    if rel[1] == "overlays":
        return rel[2]
    return "main"


def obj_name(p: Path) -> str:
    # One flat directory: encode the source path so same-named files in
    # different modules do not overwrite each other.
    rel = str(p.relative_to(DECOMP)).replace("\\", "/")
    return rel.replace("/", "__").rsplit(".", 1)[0] + ".obj"


def compile_batch(env, cl, batch: list[Path], cpp: bool, obj_dir: Path) -> list[dict]:
    results = []
    for p in batch:
        cmd = [cl] + (CPP_FLAGS if cpp else C_FLAGS) + [f"/Fo{obj_dir / obj_name(p)}", str(p)]
        res = subprocess.run(cmd, capture_output=True, text=True, env=env,
                             encoding="mbcs", errors="replace")
        errs = []
        for line in res.stdout.splitlines():
            m = ERROR_RE.match(line.strip())
            if m:
                errs.append({"line": int(m["line"]), "code": m["code"], "msg": m["msg"]})
        if res.returncode != 0 and not errs:
            errs.append({"line": 0, "code": "?", "msg": res.stdout.strip()[-200:]})
        results.append({"file": str(p.relative_to(DECOMP)).replace("\\", "/"),
                        "module": module_of(p), "ok": not errs, "errors": errs,
                        "obj": obj_name(p) if not errs else None})
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--obj-dir", default=str(ROOT / "build" / "spike_objs"))
    args = ap.parse_args()

    sources = sorted(p for d in ("src", "libs") for p in (DECOMP / d).rglob("*")
                     if p.suffix in (".c", ".cpp") and p.is_file())
    if args.limit:
        sources = sources[: args.limit]
    env = msvc_env()
    cl = find_cl(env)
    obj_dir = Path(args.obj_dir)
    obj_dir.mkdir(parents=True, exist_ok=True)
    c_files = [p for p in sources if p.suffix == ".c"]
    cpp_files = [p for p in sources if p.suffix == ".cpp"]
    batches = [(c_files[i:i + args.batch], False) for i in range(0, len(c_files), args.batch)]
    batches += [(cpp_files, True)] if cpp_files else []

    results = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_batch, env, cl, b, cpp, obj_dir) for b, cpp in batches]
        for i, f in enumerate(cf.as_completed(futures), 1):
            results.extend(f.result())
            if i % 25 == 0:
                print(f"  {i}/{len(batches)} batches", file=sys.stderr)

    ok = sum(r["ok"] for r in results)
    print(f"{ok}/{len(results)} files compile ({100.0 * ok / max(1, len(results)):.2f}%)")
    codes = collections.Counter(e["code"] for r in results for e in r["errors"][:1])
    print("first-error codes over failing files:")
    for code, n in codes.most_common(15):
        sample = next(e["msg"] for r in results for e in r["errors"][:1] if e["code"] == code)
        print(f"  {code}: {n:5d}  e.g. {sample[:100]}")
    per_module = collections.Counter(r["module"] for r in results if not r["ok"])
    print("failing files per module (top 15):")
    for mod, n in per_module.most_common(15):
        print(f"  {mod}: {n}")
    (OUT / "survey.json").write_text(json.dumps(results, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
