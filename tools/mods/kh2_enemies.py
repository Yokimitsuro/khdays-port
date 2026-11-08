"""KINGDOM HEARTS II's enemies for Days's custom missions, from the player's own 1.5+2.5 copy (docs/KH2_ENEMIES.md):
each kind's files as KH2 has them -- its model, script and collision (obj/<name>.mdlx), its motions (.mset), its
effects and sounds (.a.us), the model's HD pictures -- the shapes of KH2's calls read from its executable, and the
list of kinds with which of them the port covers (every call its script makes answered), which the port reads
(port/runtime/kh2_enemies/shell.h).

    python tools/mods/kh2_enemies.py --kh2 <kh2.hed_out> --kh2-exe <KINGDOM HEARTS II FINAL MIX.exe>
                                     [--exe <khdays-native>] [--mod "Days - KH2 Enemies"]

--kh2 is KH2's entries as the installer extracts them: original/obj/<name>.mdlx, .mset and .a.us, the HD pages
remastered/obj/<name>.mdlx/-N.dds (its model's) and remastered/obj/<name>.a.us/-N.dds (its effects'), and KH2's
resident effect pictures original/effect/texcommon.dpd with remastered/effect/texcommon.dpd/-N.dds. --exe gives the
calls the port answers (khdays-native --kh2-ai-calls); without it no kind is covered. Writes mods/<mod>/kh2_enemies.txt,
mods/<mod>/kh2/calls.txt, mods/<mod>/kh2/obj/ (each model's HD pages in kh2/obj/<name>/, where the port's model reader
looks), mods/<mod>/kh2/effect/texcommon.dpd and the effects' HD pages in mods/<mod>/kh2/fx/<name>/ and
kh2/fx/texcommon/; says what it made and what it left out.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kh2_bdx  # noqa: E402

# The kinds the mod holds and the names authors see: KH2's own data has no object's enemy name (docs/KH2_ENEMIES.md,
# "Names"), so each is named here. The Soldier and the Shadow first (US1); more as the port answers their calls.
KINDS = {"M_EX010": "Soldier", "M_EX020": "Shadow"}
FILES = (".mdlx", ".mset", ".a.us")  # each kind's, as obj/ holds them


def run_exe(line: list[str]) -> str:
    """khdays-native's run: its output; RuntimeError with what it said when it fails."""
    done = subprocess.run(line, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if done.returncode != 0:
        said = done.stderr.strip().splitlines()
        raise RuntimeError(said[-1] if said else f"exit {done.returncode}")
    return done.stdout


def calls_in(bar: bytes) -> set[tuple[int, int]]:
    """Every call a BAR's scripts make, their state routines walked."""
    return {i.syscall for _, s in kh2_bdx.scripts_in(bar) for i in kh2_bdx.walk(s).values() if i.syscall is not None}


def kinds_line(name: str, title: str, calls: set[tuple[int, int]], handled: set[tuple[int, int]]) -> str:
    """A kind's line of kh2_enemies.txt: name, title, covered, how many calls, the missing ones (tab-separated)."""
    missing = sorted(calls - handled)
    listed = " ".join(f"{k}:{n}" for k, n in missing) or "-"
    return f"{name}\t{title}\t{'no' if missing or not calls else 'yes'}\t{len(calls)}\t{listed}"


def copy_pages(source: Path, to: Path) -> int:
    """An entry's HD pages (-N.dds) into `to`: how many."""
    if not source.is_dir():
        return 0
    to.mkdir(parents=True, exist_ok=True)
    pages = sorted(source.glob("-*.dds"))
    for page in pages:
        shutil.copy2(page, to / page.name)
    return len(pages)


def make(
    kh2: Path, kh2_exe: Path, mod: Path, exe: Path | None = None, run: Callable[[list[str]], str] = run_exe
) -> list[str]:
    """The calls' shapes, each kind's files and the list into mod/; what was left out, each with why."""
    left_out = []
    shapes = kh2_bdx.exe_calls(kh2_exe.read_bytes())
    calls_file = mod / "kh2" / "calls.txt"
    calls_file.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# KINGDOM HEARTS II's calls: kind:number arguments gives-back (read from its executable, kh2_enemies.py)"]
    lines += [f"{kind}:{number} {count} {int(back)}" for kind, number, count, back in shapes]
    calls_file.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    handled = kh2_bdx.read_handled(run([str(exe), "--kh2-ai-calls"])) if exe is not None else set()
    if exe is None:
        left_out.append("every kind's coverage: no khdays-native given (--exe), none is covered")
    obj = mod / "kh2" / "obj"
    obj.mkdir(parents=True, exist_ok=True)
    common = kh2 / "original" / "effect" / "texcommon.dpd"
    if common.is_file():  # the pictures KH2 keeps resident, which the effects may cut
        (mod / "kh2" / "effect").mkdir(parents=True, exist_ok=True)
        shutil.copy2(common, mod / "kh2" / "effect" / common.name)
        copy_pages(kh2 / "remastered" / "effect" / common.name, mod / "kh2" / "fx" / "texcommon")
    else:
        left_out.append("KH2's resident effect pictures (effect/texcommon.dpd): effects cutting them drawn without")
    listed = ["# name\ttitle\tcovered\tcalls\tmissing (kh2_enemies.py; the port's kh2_enemies.txt)"]
    for name, title in KINDS.items():
        sources = [kh2 / "original" / "obj" / f"{name}{ext}" for ext in FILES]
        lacking = [s.name for s in sources if not s.is_file()]
        if lacking:
            left_out.append(f"{name} ({title}): KH2's {', '.join(lacking)} not there")
            continue
        for s in sources:
            shutil.copy2(s, obj / s.name)
        if not copy_pages(kh2 / "remastered" / "obj" / f"{name}.mdlx", obj / name):
            left_out.append(f"{name} ({title}): its model's HD pictures (its own PS2 pictures used)")
        copy_pages(kh2 / "remastered" / "obj" / f"{name}.a.us", mod / "kh2" / "fx" / name)
        calls = calls_in(sources[0].read_bytes())
        listed.append(kinds_line(name, title, calls, handled))
        print(f"kh2 enemies: made {name} ({title}), {len(calls)} calls", file=sys.stderr)
    (mod / "kh2_enemies.txt").write_text("\n".join(listed) + "\n", encoding="utf-8", newline="\n")
    return left_out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kh2", type=Path, required=True, help="KH2's entries extracted (<kh2.hed_out>)")
    ap.add_argument("--kh2-exe", type=Path, required=True, help="KINGDOM HEARTS II FINAL MIX.exe (its calls' shapes)")
    ap.add_argument("--exe", type=Path, help="khdays-native, for the calls the port answers (--kh2-ai-calls)")
    ap.add_argument("--mod", default="Days - KH2 Enemies", help="the mod folder under mods/")
    args = ap.parse_args(argv)
    try:
        left_out = make(args.kh2, args.kh2_exe, Path("mods") / args.mod, args.exe)
    except (OSError, RuntimeError, kh2_bdx.AiError) as e:
        print(f"kh2 enemies: {e}", file=sys.stderr)
        return 1
    for line in left_out:
        print(f"kh2 enemies: left out {line}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
