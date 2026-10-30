"""Writes the select pages' test characters (characters/page01 .. page30): thirty made-up characters on Roxas's code,
each the character walk's figure of three boxes (port/tests/mods_v2/character, copied) with a face of its own colour,
so that the one-screen select needs three pages. Nothing of the game: run it again to remake them."""

import colorsys
import shutil
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIGURE = HERE.parent / "character" / "characters" / "block" / "model"
COUNT = 30


def png(width: int, height: int, rgb: list[tuple[int, int, int]]) -> bytes:
    rows = b"".join(b"\0" + bytes(c for p in rgb[y * width : (y + 1) * width] for c in p) for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def face(n: int) -> bytes:
    """An 8 x 8 face: its colour round the hues, a dark band at the bottom for the even ones."""
    r, g, b = (round(c * 255) for c in colorsys.hsv_to_rgb(n / COUNT, 0.8, 0.95))
    pixels = [(r, g, b)] * 64
    if n % 2 == 0:
        pixels = pixels[:48] + [(20, 20, 20)] * 16
    return png(8, 8, pixels)


def main() -> None:
    for n in range(1, COUNT + 1):
        key = f"page{n:02d}"
        folder = HERE / "characters" / key
        (folder / "model").mkdir(parents=True, exist_ok=True)
        for f in FIGURE.iterdir():
            target = folder / "model" / f.name.replace("block", key)
            if f.suffix == ".gltf":
                target.write_text(f.read_text(encoding="utf-8").replace("block", key), encoding="utf-8")
            else:
                shutil.copyfile(f, target)
        (folder / "face.png").write_bytes(face(n))
        (folder / "character.ini").write_text(
            "# A made-up character for the port's tests: nothing of the game, nothing of any other work.\n"
            "[character]\nformat = 1\nbase = roxas\n"
            f"name = Page {n}\nname es = Página {n}\n"
            "weapon = Stick\nface = face.png\n"
            f"gauges = {1 + n % 9} 2 2 2 4 2 2 2 2\n\n"
            f"[model]\nfile = model/{key}.gltf\nskeleton = paired\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
