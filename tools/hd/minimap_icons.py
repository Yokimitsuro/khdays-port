"""Export Days' minimap-only icons as an editable RGBA atlas and logical-coordinate JSON.

Run with --data <extracted Days root> --output <mod>/layouts/minimap.
No game assets are stored in the repository.
"""

import argparse
import json
import struct
import shutil
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import save_screen as ds  # noqa: E402


def make(data: Path, output: Path):
    root = data / "nitrofs/UI/btl"
    raw = ds.dd.decompress_nitro((root / "bm_lo_bg002.NCGR.z").read_bytes())[1]
    size = struct.unpack_from("<I", raw, 0x28)[0]
    offset = 0x18 + struct.unpack_from("<I", raw, 0x2C)[0]
    raw = raw[offset : offset + size]
    colors, _ = ds.palette(ds.dd.decompress_nitro((root / "bm_lo_bg000.NCLR.z").read_bytes())[1])
    colors = colors[13 * 16 : 14 * 16]
    sheet = Image.new("RGBA", (256, 128))
    sprites = {}

    def add(name, offset, w=8, h=8, mask=255, tiled=False):
        index = len(sprites)
        x, y = (index % 16) * 16, (index // 16) * 16
        if y + h > sheet.height:
            raise ValueError("minimap atlas overflow")
        for py in range(h):
            for px in range(w):
                at = (
                    offset + (py // 8 * 2 + px // 8) * 32 + (py % 8) * 4 + (px % 8) // 2
                    if tiled
                    else offset + py * 4 + px // 2
                )
                value = (raw[at] & mask) >> (4 * (px & 1)) & 15
                sheet.putpixel((x + px, y + py), (*colors[value], 255 if value else 0))
        sprites[name] = dict(x=x, y=y, width=w, height=h)

    for player in range(4):
        mask = (player + 2) * 17
        for direction in range(8):
            add(f"player{player + 1}_{direction}", direction * 32, mask=mask)
        add(f"emblem{player + 1}", 0x2E0, 16, 16, mask, True)
    for kind, offset in {0: 0x102, 1: 0x240, 2: 0x260, 4: 0x2A0, 5: 0x2A0, 6: 0x2C0}.items():
        add(f"entity_{kind}", offset, 4 if kind == 0 else 8, 4 if kind == 0 else 8)
    for offset in (0x120, 0x280, 0x360, 0x380, 0x3A0):
        add(f"marker_{offset:03x}", offset)
    for tile in range(32):
        add(f"source_{tile:02d}", tile * 32)
    names = [
        "Xemnas",
        "Xigbar",
        "Xaldin",
        "Vexen",
        "Lexaeus",
        "Zexion",
        "Saix",
        "Axel",
        "Demyx",
        "Luxord",
        "Marluxia",
        "Larxene",
        "Roxas",
        "Xion",
        "Mickey",
        "Sora",
        "Riku",
        "Donald",
        "Goofy",
        "Xion_Hood",
        "Roxas_Hood",
    ]
    config = dict(
        version=1,
        enabled=True,
        kh2map=True,
        map_texture="radar.dds",
        map_scale=0.65,
        map_offset_x=0,
        map_offset_y=0,
        map_opacity=0.75,
        map_icon_scale=1,
        texture="minimap.png",
        logical_width=256,
        logical_height=128,
        sprites=sprites,
        players={str(i): dict(sprite="", smooth_rotation=False, rotation_offset=0, scale=1) for i in range(1, 5)},
        characters={
            n: dict(enabled=False, sprite="", smooth_rotation=False, rotation_offset=0, scale=1) for n in names
        },
    )
    output.mkdir(parents=True, exist_ok=True)
    # Preserve a modder's atlas and settings when regenerating an installed pack.
    if not (output / "minimap.png").exists():
        sheet.save(output / "minimap.png")
    if not (output / "minimap.json").exists():
        (output / "minimap.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    return config


def preserve(settings: Path, output: Path):
    config = json.loads((settings / "minimap.json").read_text(encoding="utf-8"))
    files = {"minimap.json", config.get("map_texture", "radar.dds"), config.get("texture", "minimap.png")}
    for group in ("players", "characters"):
        for policy in config.get(group, {}).values():
            name = policy.get("sprite", "")
            if name not in config.get("sprites", {}) and Path(name).suffix.lower() in (".png", ".dds"):
                files.add(name)
    for name in files:
        if not isinstance(name, str) or not name or any(c in name for c in "/\\:") or name in (".", ".."):
            raise ValueError("existing minimap textures must be local filenames")
    output.mkdir(parents=True, exist_ok=True)
    for name in files:
        source = settings / name
        if source.is_file() and source.resolve() != (output / name).resolve():
            shutil.copy2(source, output / name)


def make_radar(source: Path, output: Path):
    """Keep the KH2 radar mask's remastered texels and alpha without conversion."""
    file = source / "remastered/radar.tm2/-0.dds"
    with Image.open(file) as picture:
        if picture.format != "DDS" or "A" not in picture.getbands():
            raise ValueError(f"expected the radar's remastered DDS with alpha: {file}")
        w, h = picture.size
        if w % 64 or h % 16 or w // 64 != h // 16:
            raise ValueError(f"expected radar texture dimensions proportional to 64x16: {file}")
        picture.load()
    output.mkdir(parents=True, exist_ok=True)
    if not (output / "radar.dds").exists():
        shutil.copy2(file, output / "radar.dds")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data", type=Path)
    p.add_argument("--output", type=Path)
    p.add_argument("--mod", default="HD PACK")
    p.add_argument("--settings", type=Path, help="preserve an existing mod's minimap settings and atlas")
    p.add_argument("--kh2", type=Path, help="extracted kh2_first archive, including the remastered radar.tm2")
    a = p.parse_args()
    output = a.output or Path(__file__).resolve().parents[2] / "mods" / a.mod / "layouts/minimap"
    if a.settings and (a.settings / "minimap.json").is_file():
        preserve(a.settings, output)
    data = a.data
    if data is None:
        found = sorted((Path(__file__).resolve().parents[2] / "data/extracted").glob("*/nitrofs"))
        if not found:
            p.error("no extracted Days data: give --data")
        data = found[0].parent
    make(data, output)
    if a.kh2:
        make_radar(a.kh2, output)


if __name__ == "__main__":
    main()
