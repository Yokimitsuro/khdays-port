"""KINGDOM HEARTS II's prizes and chests for Days's, from the player's own 1.5+2.5 copy: the effect file and pages
of each prize KH2 has one for (docs/KH2_SEQUENCES.md, "Field prizes"; the reader: port/ui/prize_effects_model.h),
and each Days chest as the KH2 chest that looks like it.

    python tools/mods/kh2_prizes.py --kh2 <kh2.hed_out> [--exe <khdays-native>] [--bbs2 <bbs_second.hed_out>]
                                    [--mod "Days - KH2 Prizes"]

--kh2 is KH2's entries as the installer extracts them: original/obj/PRIZE.mdlx and its HD pages,
remastered/obj/PRIZE.mdlx/-0.dds, -1.dds (one a page, in the order of the pages' places); the chests'
original/obj/<chest>.mdlx and .mset with their remastered/obj/<chest>.mdlx/-N.dds. --exe makes the chests (through
khdays-native --kh2-object), --bbs2 gives Neverland's its Birth by Sleep pictures. Writes
mods/<mod>/graphics/mo/prize.pak.z/fx/, mods/<mod>/graphics/mo/mo#<entry>/mdl/ and the switch
days-mods/kh2-prizes.txt; says what it made and what it left out.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kh2_pax  # noqa: E402

PRIZE = "obj/PRIZE.mdlx"
# Days's prize pictures and KH2's animation for each (its number in PRIZE.mdlx) with the layer that is the prize's
# body (made as wide as Days's picture): KH2's HP orb (its green core, under the halo) and its munny. KH2's MP orb
# (2) and drive orb (3) have no Days prize.
PAIRS = {"prize_01": (1, 1), "prize_03": (0, 0)}
ANIMATIONS = {0: "munny", 1: "HP orb", 2: "MP orb", 3: "drive orb"}

# Days's chests (mo/mo's <world>_box_b: its entry) and the KH2 chest that looks like each, with its motion set (the
# maintainer's pairing, 2026-10-30): KH2's large chests of the same worlds; Wonderland's (not in KH2) KH2's common
# small chest, red as Days's; Neverland's that model wearing Birth by Sleep's Never Land chest pictures, laid out as
# its own.
CHESTS = {
    "tt": (102, "F_EX040_TT", "F_EX040_TT.mset"),
    "al": (94, "F_EX040_AL", "F_EX040.mset"),
    "bb": (96, "F_EX040_BB", "F_EX040.mset"),
    "he": (98, "F_EX040_HE", "F_EX040.mset"),
    "nm": (99, "F_EX040_NM", "F_EX040.mset"),
    "eh": (97, "F_EX040_EH", "F_EX040.mset"),
    "aw": (95, "F_EX030", "F_EX030.mset"),
    "pp": (101, "F_EX030", "F_EX030.mset"),
}
NEVERLAND = "ForEventJP/gimmick/g01pp/00/g01pp00.pmo"  # in bbs_second: its HD picture, -0.dds
CLOSED = "00A"  # a KH2 chest's motion with its lid closed (its rest has it open)
DAYS_CHEST_WIDTH = 0.982  # every Days chest's width (mo/mo's <world>_box_b, the graphics dump's units)
# KH2's chest's joints to Days's nodes: j0 its root, j3 its lid (box1 Days's, turned 96 degrees when open); Days's
# light rays (light01-03) drawn as the game draws them; the chest follows the settings' KH2 prizes switch
CHEST_BONES = "j0 dummy0\nj3 box1\nkeep light01 light02 light03\nswitch kh2_prizes\n"
# Days's item spheres and KH2's prize boxes, the same designs (the blue one with its gold horn)
BOXES = {"prize_05": "PRIZE_BOX_YELLOW", "prize_06": "PRIZE_BOX_RED", "prize_07": "PRIZE_BOX_BLUE"}


def effect_text(animation: kh2_pax.Animation, pages: dict[int, str], body: int) -> str:
    """An animation as the effect format writes it: its pages, its body layer, a frame a game frame."""
    lines = [
        "effect 1",
        f"# KINGDOM HEARTS II's prize effect, animation {animation.number} ({PRIZE}), made by tools/mods/kh2_prizes.py",
    ]
    lines += [f"page {n} {file}" for n, file in sorted(pages.items())]
    lines += [f"body {body}", f"frame_time {frame_time(animation)}"]
    numbers = {tbp: n for n, tbp in enumerate(sorted(pages_tbp(animation)))}
    for frame in animation.frames:
        lines.append("frame")
        for layer in frame.layers:
            corners = " ".join(f"{x} {y}" for x, y in layer.corners)
            rect = " ".join(f"{v:.6g}" for v in layer.rect)
            colour = " ".join(f"{c / 128:.4g}" for c in layer.colour)
            lines.append(
                f"layer page {numbers[frame.tbp]} rect {rect} corners {corners} colour {colour} blend {layer.blend}"
            )
    return "\n".join(lines) + "\n"


def frame_time(animation: kh2_pax.Animation) -> int:
    """The game frames a frame shows (Days's field runs 30 a second) from KH2's waits (its frames, 60 a second, in
    1/256): the first frame's, at least 1."""
    return max(1, round(animation.frames[0].wait / 256 / 2))


def uneven(animation: kh2_pax.Animation) -> bool:
    """Whether its frames' waits differ or are not whole field frames (the effect shows each frame_time)."""
    return any(f.wait != animation.frames[0].wait for f in animation.frames) or animation.frames[0].wait % 512 != 0


def pages_tbp(animation: kh2_pax.Animation) -> set[int]:
    return {frame.tbp for frame in animation.frames}


def opened(path: Path) -> bool:
    """Whether `path` is a file that can be read (read here, as the installer's trace sees what the tools read)."""
    try:
        with open(path, "rb") as f:
            return bool(f.read(4))
    except OSError:
        return False


def run_exe(line: list[str]) -> str:
    """khdays-native's run: its output; RuntimeError with what it said when it fails."""
    done = subprocess.run(line, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if done.returncode != 0:
        said = done.stderr.strip().splitlines()
        raise RuntimeError(said[-1] if said else f"exit {done.returncode}")
    return done.stdout


def make_chests(
    kh2: Path, mod: Path, exe: Path, bbs2: Path | None, run: Callable[[list[str]], str] = run_exe
) -> tuple[list[str], list[str]]:
    """Each Days chest as its KH2 chest, closed, turned to face as Days's and made Days's width: what was made and
    what was left out."""
    made, left_out = [], []
    for world, (entry, name, motions) in CHESTS.items():
        model = kh2 / "original" / "obj" / f"{name}.mdlx"
        mset = kh2 / "original" / "obj" / motions
        hd = kh2 / "remastered" / "obj" / f"{name}.mdlx"
        if not opened(model) or not opened(mset):
            left_out.append(f"{world}'s chest: KH2's {name} is not there")
            continue
        if hd.is_dir():
            for picture in sorted(hd.glob("-*.dds")):
                opened(picture)
        extra: list[str] = []
        if world == "pp":
            picture = bbs2 / "remastered" / NEVERLAND / "-0.dds" if bbs2 else None
            if picture is None or not opened(picture):
                left_out.append("pp's chest: Birth by Sleep's Never Land chest pictures are not there")
                continue
            extra = ["--picture", "0", str(picture.absolute())]
        out = mod / "graphics" / "mo" / f"mo#{entry}" / "mdl" / f"{world}_box_b.gltf"
        # whole paths: khdays-native starts in a working folder of its own choosing
        line = [str(exe), "--kh2-object", str(model.absolute()), str(out.absolute()), "--hd", str(hd.absolute())]
        line += ["--pose", str(mset.absolute()), CLOSED, "--turn", "180", *extra]
        try:
            said = [ln for ln in run(line).splitlines() if ln.startswith("{")]
            info = json.loads(said[-1])
            width = info["max"][0] - info["min"][0]
            if not width > 0:
                raise ValueError("it has no width")
        except (RuntimeError, ValueError, IndexError, KeyError) as e:
            left_out.append(f"{world}'s chest: KH2's {name} could not be made ({e})")
            continue
        bones = CHEST_BONES + f"scale {DAYS_CHEST_WIDTH / width:.6g}\n"
        out.with_suffix(".bones.txt").write_text(bones, encoding="utf-8", newline="\n")
        made.append(f"{world}'s chest: KH2's {name}" + (" in Birth by Sleep's pictures" if extra else ""))
        left_out += [f"{world}'s chest: {note}" for note in info.get("report", [])]
    return made, left_out


def make_boxes(
    kh2: Path, mod: Path, exe: Path, run: Callable[[list[str]], str] = run_exe
) -> tuple[list[str], list[str]]:
    """Days's item spheres as KH2's prize boxes, models drawn in their place (upright, turning: the port's
    prize models, graphics/mo/prize.pak.z/mdl/): what was made and what was left out."""
    made, left_out = [], []
    for picture, name in BOXES.items():
        model = kh2 / "original" / "obj" / f"{name}.mdlx"
        hd = kh2 / "remastered" / "obj" / f"{name}.mdlx"
        if not opened(model):
            left_out.append(f"{picture}: KH2's {name} is not there")
            continue
        if hd.is_dir():
            for page in sorted(hd.glob("-*.dds")):
                opened(page)
        out = mod / "graphics" / "mo" / "prize.pak.z" / "mdl" / f"{picture}.gltf"
        line = [str(exe), "--kh2-object", str(model.absolute()), str(out.absolute()), "--hd", str(hd.absolute())]
        try:
            said = [ln for ln in run(line).splitlines() if ln.startswith("{")]
            info = json.loads(said[-1])
        except (RuntimeError, ValueError, IndexError) as e:
            left_out.append(f"{picture}: KH2's {name} could not be made ({e})")
            continue
        made.append(f"{picture}: KH2's {name}")
        left_out += [f"{picture}: {note}" for note in info.get("report", [])]
    return made, left_out


def make(
    kh2: Path,
    mod: Path,
    exe: Path | None = None,
    bbs2: Path | None = None,
    run: Callable[[list[str]], str] = run_exe,
) -> list[str]:
    """The effects (and with `exe` the chests and prize boxes) into mod/; what was left out, each with why."""
    left_out = []
    pax = kh2_pax.read_pax((kh2 / "original" / PRIZE).read_bytes())
    page_order = sorted({p.tbp for p in pax.pictures})  # the HD pages' order (-0.dds the first)
    fx = mod / "graphics" / "mo" / "prize.pak.z" / "fx"
    fx.mkdir(parents=True, exist_ok=True)
    made = []
    for picture, (number, body) in PAIRS.items():
        animation = next((a for a in pax.animations if a.number == number), None)
        if animation is None:
            left_out.append(f"{picture}: KH2's {ANIMATIONS.get(number, number)} (animation {number}) is not there")
            continue
        pages = {}
        for n, tbp in enumerate(sorted(pages_tbp(animation))):
            name = f"{picture}_page{n}.png"
            hd = kh2 / "remastered" / PRIZE / f"-{page_order.index(tbp)}.dds"
            if hd.exists():
                with Image.open(hd) as page:
                    page.convert("RGBA").save(fx / name)
            else:  # the original's page, coloured with the animation's own palette
                cbp = animation.frames[0].cbp
                Image.frombytes("RGBA", (kh2_pax.PAGE_W, kh2_pax.PAGE_H), kh2_pax.page_rgba(pax, tbp, cbp)).save(
                    fx / name
                )
            pages[n] = name
        (fx / f"{picture}.txt").write_text(effect_text(animation, pages, body), encoding="utf-8", newline="\n")
        made.append(f"{picture}: KH2's {ANIMATIONS[number]}")
        if uneven(animation):
            left_out.append(f"{picture}: its frames' times as {frame_time(animation)} game frames each")
    for number, what in ANIMATIONS.items():
        if number not in {n for n, _ in PAIRS.values()}:
            left_out.append(f"KH2's {what} (animation {number}): Days has no such prize")
    if exe is not None:
        for part in (make_chests(kh2, mod, exe, bbs2, run), make_boxes(kh2, mod, exe, run)):
            made += part[0]
            left_out += part[1]
    if made:
        switch = mod / "days-mods" / "kh2-prizes.txt"
        switch.parent.mkdir(parents=True, exist_ok=True)
        switch.write_text("KINGDOM HEARTS II's prizes (tools/mods/kh2_prizes.py)\n", encoding="utf-8", newline="\n")
    for line in made:
        print(f"kh2 prizes: made {line}", file=sys.stderr)
    return left_out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kh2", type=Path, required=True, help="KH2's entries extracted (<kh2.hed_out>)")
    ap.add_argument("--exe", type=Path, help="khdays-native, to make the chests with (--kh2-object)")
    ap.add_argument("--bbs2", type=Path, help="Birth by Sleep's bbs_second entries extracted (Neverland's chest)")
    ap.add_argument("--mod", default="Days - KH2 Prizes", help="the mod folder under mods/")
    args = ap.parse_args(argv)
    try:
        left_out = make(args.kh2, Path("mods") / args.mod, args.exe, args.bbs2)
    except (OSError, kh2_pax.PaxError) as e:
        print(f"kh2 prizes: {e}", file=sys.stderr)
        return 1
    for line in left_out:
        print(f"kh2 prizes: left out {line}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
