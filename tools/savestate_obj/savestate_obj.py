#!/usr/bin/env python
"""Render the OBJ (sprite) layer of a DeSmuME savestate to a PNG.

A development/analysis tool for reading a screen's real sprite layout out of the
running game, the way the char-select grid, the save screen and the difficulty
selector were all measured. It does NOT touch the game or the ROM -- it only
reads a .dst savestate the user provides.

Savestate format (DeSmuME): magic "DeSmuME SState\\0\\0", uncompressed size at
0x18, zlib blob from 0x20. The decompressed stream is a list of chunks, each
`tag[4] ver[4] size[4] data[size]`. The chunks this tool uses:
  WRAM  0x400000  main RAM (0x02000000)
  LCDM  0xa4000   all VRAM banks A..I concatenated
                  (A/B/C/D=0x20000, E=0x10000, F/G=0x4000, H=0x8000, I=0x4000)
  VMEM  0x800     palette RAM (BG at +0x000/+0x400, OBJ at +0x200/+0x600)
  OAMS  0x800     OAM (Engine A/top at +0x000, Engine B/bottom at +0x400)

OBJ rendering pipeline confirmed for the Days front-end bottom screen:
  sub-OBJ tiles = VRAM bank D, 8bpp, byte offset = tile*128,
  256-color palette at VMEM+0x600 (palette index 0 = transparent).

NOTE: the VRAMCNT bytes in the register chunk are stale in DeSmuME savestates
(the emulator keeps the bank mapping in a derived structure), so the bank cannot
be read back from registers -- it is a tool option, defaulting to the value that
matches the Days front-end. Use --bank / --bpp / --unit to override for other
screens, and --dump-oam to print the raw sprite table.
"""
import argparse
import struct
import zlib

try:
    from PIL import Image
except ImportError:
    raise SystemExit("This tool needs Pillow (pip install Pillow); run with py/python 3.13.")

# (offset, length) of each VRAM bank inside the LCDM chunk.
VRAM_BANKS = {
    "A": (0x00000, 0x20000), "B": (0x20000, 0x20000), "C": (0x40000, 0x20000),
    "D": (0x60000, 0x20000), "E": (0x80000, 0x10000), "F": (0x90000, 0x04000),
    "G": (0x94000, 0x04000), "H": (0x98000, 0x08000), "I": (0xA0000, 0x04000),
}

# (shape, size) -> (width, height) in pixels.
OBJ_SIZES = {
    (0, 0): (8, 8), (0, 1): (16, 16), (0, 2): (32, 32), (0, 3): (64, 64),
    (1, 0): (16, 8), (1, 1): (32, 8), (1, 2): (32, 16), (1, 3): (64, 32),
    (2, 0): (8, 16), (2, 1): (8, 32), (2, 2): (16, 32), (2, 3): (32, 64),
}


def load_chunks(path):
    data = open(path, "rb").read()
    if data[:14] != b"DeSmuME SState":
        raise SystemExit(f"{path}: not a DeSmuME savestate (bad magic)")
    raw = zlib.decompress(data[0x20:])
    # Chunk data begins at (tag offset + 12); a linear walk trips over a preamble,
    # so locate each tag directly -- the tags used here are unique in the stream.
    def find(tag):
        i = raw.find(tag)
        return i + 12 if i >= 0 else None
    return raw, {t.decode(): find(t) for t in (b"WRAM", b"LCDM", b"VMEM", b"OAMS")}


def bgr555(raw, off):
    v = struct.unpack_from("<H", raw, off)[0]
    r, g, b = (v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3
    return (r | r >> 5, g | g >> 5, b | b >> 5)


def read_sprites(raw, oam_base):
    sprites = []
    for i in range(128):
        a0, a1, a2 = struct.unpack_from("<HHH", raw, oam_base + i * 8)
        rot, disable = (a0 >> 8) & 1, (a0 >> 9) & 1
        if rot == 0 and disable:
            continue
        y = a0 & 0xFF
        color256 = (a0 >> 13) & 1
        shape = (a0 >> 14) & 3
        x = a1 & 0x1FF
        if x & 0x100:
            x -= 0x200
        size = (a1 >> 14) & 3
        tile = a2 & 0x3FF
        pal = (a2 >> 12) & 0xF
        w, h = OBJ_SIZES[(shape, size)]
        sprites.append(dict(i=i, x=x, y=y, w=w, h=h, tile=tile, pal=pal,
                            color256=color256))
    return sprites


def render(raw, chunks, screen, bank, bpp, unit, bg):
    oam_base = chunks["OAMS"] + (0x000 if screen == "top" else 0x400)
    lcdm, vmem = chunks["LCDM"], chunks["VMEM"]
    boff, blen = VRAM_BANKS[bank]
    vram = raw[lcdm + boff:lcdm + boff + blen]
    obj_pal = vmem + (0x200 if screen == "top" else 0x600)

    img = Image.new("RGBA", (256, 192), (*bg, 255))
    px = img.load()
    sprites = read_sprites(raw, oam_base)
    for s in sprites:
        if s["y"] >= 192:
            continue  # parked/off-screen
        tw, th = s["w"] // 8, s["h"] // 8
        if bpp == 8:
            palette = [bgr555(raw, obj_pal + c * 2) for c in range(256)]
        else:
            palette = [bgr555(raw, obj_pal + s["pal"] * 32 + c * 2) for c in range(16)]
        tile_bytes = 64 if bpp == 8 else 32
        for ty in range(th):
            for tx in range(tw):
                base = s["tile"] * unit + (ty * tw + tx) * tile_bytes
                for row in range(8):
                    for col in range(8):
                        if bpp == 8:
                            p = base + row * 8 + col
                            if p >= len(vram):
                                continue
                            idx = vram[p]
                        else:
                            p = base + row * 4 + col // 2
                            if p >= len(vram):
                                continue
                            idx = (vram[p] >> ((col & 1) * 4)) & 0xF
                        if idx == 0:
                            continue
                        xx, yy = s["x"] + tx * 8 + col, s["y"] + ty * 8 + row
                        if 0 <= xx < 256 and 0 <= yy < 192:
                            px[xx, yy] = (*palette[idx], 255)
    return img, sprites


def main():
    ap = argparse.ArgumentParser(description="Render a DeSmuME savestate's OBJ layer to PNG.")
    ap.add_argument("savestate", help="path to a .dst savestate")
    ap.add_argument("out", help="output PNG path")
    ap.add_argument("--screen", choices=["top", "bottom"], default="bottom")
    ap.add_argument("--bank", choices=list(VRAM_BANKS), default="D",
                    help="VRAM bank holding the OBJ tiles (default D = Days sub-OBJ)")
    ap.add_argument("--bpp", type=int, choices=[4, 8], default=8)
    ap.add_argument("--unit", type=int, default=128,
                    help="bytes per attr2 tile-number step (default 128)")
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--bg", default="64,64,64", help="backdrop RGB, e.g. 64,64,64")
    ap.add_argument("--dump-oam", action="store_true", help="also print the sprite table")
    args = ap.parse_args()

    raw, chunks = load_chunks(args.savestate)
    missing = [k for k, v in chunks.items() if v is None]
    if missing:
        raise SystemExit(f"missing chunks {missing} -- unexpected savestate layout")
    bg = tuple(int(c) for c in args.bg.split(","))
    img, sprites = render(raw, chunks, args.screen, args.bank, args.bpp, args.unit, bg)
    if args.dump_oam:
        for s in sorted(sprites, key=lambda s: (s["y"], s["x"])):
            print(f"  #{s['i']:3d} ({s['x']:4d},{s['y']:3d}) {s['w']}x{s['h']} "
                  f"tile={s['tile']:4d} pal={s['pal']} color256={s['color256']}")
    if args.scale != 1:
        img = img.resize((256 * args.scale, 192 * args.scale), Image.NEAREST)
    img.save(args.out)
    print(f"{args.savestate}: {len(sprites)} sprites ({args.screen}) -> {args.out}")


if __name__ == "__main__":
    main()
