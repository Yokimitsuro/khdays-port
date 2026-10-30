"""KINGDOM HEARTS II's prizes for Days's prize pictures, from the player's own 1.5+2.5 copy: the effect file and pages
of each prize KH2 has one for (docs/KH2_SEQUENCES.md, "Field prizes"; the reader: port/ui/prize_effects_model.h).

    python tools/mods/kh2_prizes.py --kh2 <kh2.hed_out> [--mod "Days - KH2 Prizes"]

--kh2 is KH2's entries as the installer extracts them: original/obj/PRIZE.mdlx and its HD pages,
remastered/obj/PRIZE.mdlx/-0.dds, -1.dds (one a page, in the order of the pages' places). Writes
mods/<mod>/graphics/mo/prize.pak.z/fx/ and the switch days-mods/kh2-prizes.txt; says what it made and what it left out.
"""

from __future__ import annotations

import argparse
import sys
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


def effect_text(animation: kh2_pax.Animation, pages: dict[int, str], body: int) -> str:
    """An animation as the effect format writes it: its pages, its body layer, a frame a game frame."""
    lines = [
        "effect 1",
        f"# KINGDOM HEARTS II's prize effect, animation {animation.number} ({PRIZE}), made by tools/mods/kh2_prizes.py",
    ]
    lines += [f"page {n} {file}" for n, file in sorted(pages.items())]
    lines += [f"body {body}", "frame_time 1"]
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


def pages_tbp(animation: kh2_pax.Animation) -> set[int]:
    return {frame.tbp for frame in animation.frames}


def make(kh2: Path, mod: Path) -> list[str]:
    """The effects into mod/; what was left out, each with why."""
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
    for number, what in ANIMATIONS.items():
        if number not in {n for n, _ in PAIRS.values()}:
            left_out.append(f"KH2's {what} (animation {number}): Days has no such prize")
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
    ap.add_argument("--mod", default="Days - KH2 Prizes", help="the mod folder under mods/")
    args = ap.parse_args(argv)
    try:
        left_out = make(args.kh2, Path("mods") / args.mod)
    except (OSError, kh2_pax.PaxError) as e:
        print(f"kh2 prizes: {e}", file=sys.stderr)
        return 1
    for line in left_out:
        print(f"kh2 prizes: left out {line}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
