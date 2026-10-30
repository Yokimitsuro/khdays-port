"""tools/mods/kh2_pax.py and kh2_prizes.py with no game data and no collection: made-up PAX effects (KH2's particle
format as docs/FORMATS.md reads it), their pictures, palettes and animations read back, and the mod written from them."""

import struct
import sys
import unittest
from pathlib import Path

MODS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MODS))

ALPHA, ADD = 0x44, 0x48  # a layer's blend: mixed by its alpha, its light added


def swap34(i: int) -> int:
    """A palette index's bits 3 and 4 swapped: the order the PS2's palettes are stored in."""
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)


def tex0(tbp: int, cbp: int) -> int:
    """A GS TEX0 for an 8-bit picture in a 128 x 64 page (width 2 x 64, 2^7 x 2^6)."""
    return tbp | (2 << 14) | (0x13 << 20) | (7 << 26) | (6 << 30) | (cbp << 37)


def picture(tbp, cbp, x, y, w, h, indices: bytes, palette) -> bytes:
    """A picture's block: its header, its w x h indices, its 256 colours (RGBA, alpha 0x80 opaque) stored swapped."""
    stored = b"".join(bytes(palette[swap34(i)]) for i in range(256))
    head = struct.pack("<8HQIHH", tbp, cbp, 2, 0x13, x, y, w, h, tex0(tbp, cbp), w * h, 0x20, 4)
    return head + indices + stored


def frame(kind, tbp, cbp, layers) -> bytes:
    """One frame: kind 0 (each layer a square) or 8 (four corners), its layers, then their colours and blends."""
    body = b""
    for layer in layers:
        if kind & 8:
            body += struct.pack("<8h", *[v for corner in layer["corners"] for v in corner])
        else:
            body += struct.pack("<4h", *layer["square"])
        body += struct.pack("<4H", *layer["uv"])
    for layer in layers:
        body += bytes(layer["colour"]) + bytes([layer["blend"], 0x80 if layer["blend"] == ALPHA else 0, 0x43, 0])
    return struct.pack("<HHI8xQ8x", kind, len(layers), 0x10 + len(body), tex0(tbp, cbp)) + body


def animation(number, frames, waits=None) -> bytes:
    """An animation's block: its header, the frames' list (each offset from the block's +0x10, its wait in KH2's
    frames x 256, 0x80 on the last) and the frames."""
    waits = waits or [0x200] * len(frames)
    head = 0x20 + 8 * len(frames)
    blobs, offsets, at = [], [], head - 0x10
    for f in frames:
        offsets.append(at)
        blobs.append(f)
        at += len(f)
    listing = b"".join(
        struct.pack("<HHI", o, waits[i], 0x80 if i == len(frames) - 1 else 0) for i, o in enumerate(offsets)
    )
    return (
        struct.pack("<IIIIIHHII", number, 0, len(listing), 0, 0x346, len(frames), len(frames), 0, 0)
        + listing
        + b"".join(blobs)
    )


def make_pax(pictures, animations) -> bytes:
    """A BAR file holding one PAX entry (type 18) as KH2's PRIZE.mdlx does: its effect named "prize", its data block
    at 0x180 with the pictures' and the animations' tables. pictures: picture() blocks; animations: animation()
    blocks."""
    tables = 0x10 + 4 * len(pictures) + 4 + 4 * len(animations) + 8
    at = max(tables, 0x60) + 0x80  # the effect's bounds (a box) sit at +0x60, the blocks after them
    pic_offsets, blobs = [], b""
    for p in pictures:
        pic_offsets.append(at + len(blobs))
        blobs += p
    anim_offsets = []
    for a in animations:
        anim_offsets.append(at + len(blobs))
        blobs += a
    block = struct.pack("<IIII", 0x96, 1, 0x60, len(pictures)) + b"".join(struct.pack("<I", o) for o in pic_offsets)
    block += struct.pack("<I", len(animations)) + b"".join(struct.pack("<I", o) for o in anim_offsets) + bytes(8)
    block = block.ljust(at, b"\0") + blobs
    pax = b"PAX_" + struct.pack("<III", 0x60, 1, 0xE0)
    pax = pax.ljust(0x44, b"\0") + struct.pack("<3f", 1.0, 1.0, 1.0)
    pax = pax.ljust(0x60, b"\0") + b"prize".ljust(0x80, b"\0")
    pax += struct.pack("<IIII", 0x82, 0, 0, 1) + struct.pack("<I", 0xA0)
    pax = pax.ljust(0x110, b"\0") + struct.pack("<II", 1, 0xA0)
    pax = pax.ljust(0x180, b"\0") + block
    return b"BAR\x01" + struct.pack("<III", 1, 0, 0) + struct.pack("<HH4sII", 18, 0, b"priz", 32, len(pax)) + pax


def solid(colour_index: int, w: int, h: int) -> bytes:
    return bytes([colour_index]) * (w * h)


def two_pictures():
    """A made-up page: a 32 x 32 picture of index 1 at (0, 32) and one of index 2 at (32, 32), one palette."""
    palette = [(0, 0, 0, 0)] * 256
    palette[1] = (0, 255, 0, 0x80)
    palette[2] = (255, 255, 255, 0x40)
    return [
        picture(0x3000, 0x3E0C, 0, 32, 32, 32, solid(1, 32, 32), palette),
        picture(0x3000, 0x3E0C, 32, 32, 32, 32, solid(2, 32, 32), palette),
    ]


class MadeUpPax(unittest.TestCase):
    def test_the_file_is_a_bar_with_a_pax(self):
        layer = {
            "square": (-100, -100, 100, 100),
            "uv": (0x40, 0x880, 0x380, 0x700),
            "colour": (128, 128, 128, 128),
            "blend": ALPHA,
        }
        data = make_pax(two_pictures(), [animation(0, [frame(0, 0x3000, 0x3E0C, [layer])])])
        self.assertEqual(data[:4], b"BAR\x01")
        self.assertEqual(data[32:36], b"PAX_")


GLOW = {"square": (-336, -336, 336, 336), "uv": (0x440, 0x880, 0x380, 0x700), "colour": (0x90, 0x20, 0x1D, 0x80),
        "blend": ADD}  # fmt: skip
CORE = {"square": (-224, -224, 224, 224), "uv": (0x40, 0x880, 0x380, 0x700), "colour": (0x86, 0x86, 0x86, 0x80),
        "blend": ALPHA}  # fmt: skip
TURNED = {"corners": [(0, -356), (356, 0), (-356, 0), (0, 356)], "uv": (0x420, 0x40, 0x3C0, 0x780),
          "colour": (0xE1, 0x95, 0x4B, 0x80), "blend": ADD}  # fmt: skip


def four_animations(numbers=(0, 1, 2, 3)):
    """Made-up animations numbered as KH2's (0 munny, 1 HP orb, 2 MP orb, 3 drive orb), each of two frames."""
    return [animation(n, [frame(0, 0x3000, 0x3E0C, [GLOW, CORE]), frame(8, 0x3000, 0x3E0C, [TURNED])]) for n in numbers]


class ReadPax(unittest.TestCase):
    def test_pictures_and_their_palette(self):
        import kh2_pax

        palette = [(0, 0, 0, 0)] * 256
        palette[8] = (255, 0, 0, 0x80)  # stored where index 16 would be: the swap read back
        palette[16] = (0, 0, 255, 0x80)
        pax = kh2_pax.read_pax(
            make_pax([picture(0x3000, 0x3E00, 64, 0, 8, 8, solid(8, 8, 8), palette)], four_animations())
        )
        p = pax.pictures[0]
        self.assertEqual((p.tbp, p.cbp, p.x, p.y, p.w, p.h), (0x3000, 0x3E00, 64, 0, 8, 8))
        self.assertEqual(p.palette[8], (255, 0, 0, 0x80))
        self.assertEqual(p.palette[16], (0, 0, 255, 0x80))

    def test_square_and_corner_frames(self):
        import kh2_pax

        pax = kh2_pax.read_pax(make_pax(two_pictures(), four_animations()))
        self.assertEqual([a.number for a in pax.animations], [0, 1, 2, 3])
        squares, turned = pax.animations[1].frames
        self.assertEqual((squares.tbp, squares.cbp), (0x3000, 0x3E0C))
        glow, core = squares.layers
        self.assertEqual(glow.corners, [(-336, -336), (336, -336), (-336, 336), (336, 336)])
        self.assertEqual(glow.rect, (0x440 / 4096, 0x880 / 4096, 0x380 / 4096, 0x700 / 4096))
        self.assertEqual((glow.colour, glow.blend), ((0x90, 0x20, 0x1D, 0x80), "add"))
        self.assertEqual(core.blend, "alpha")
        self.assertEqual(turned.layers[0].corners, [(0, -356), (356, 0), (-356, 0), (0, 356)])
        self.assertEqual((squares.wait, turned.wait), (0x200, 0x200))

    def test_frames_as_the_header_counts_them_with_their_waits(self):
        import kh2_pax

        frames = [
            frame(0, 0x3000, 0x3E0C, [CORE]),
            frame(0, 0x3000, 0x3E0C, [GLOW]),
            frame(8, 0x3000, 0x3E0C, [TURNED]),
        ]
        pax = kh2_pax.read_pax(make_pax(two_pictures(), [animation(5, frames, [0x400, 0x300, 0x3C00])]))
        self.assertEqual([f.wait for f in pax.animations[0].frames], [0x400, 0x300, 0x3C00])
        self.assertEqual(pax.animations[0].frames[1].layers[0].blend, "add")

    def test_a_page_coloured(self):
        import kh2_pax

        pax = kh2_pax.read_pax(make_pax(two_pictures(), four_animations()))
        rgba = kh2_pax.page_rgba(pax, 0x3000, 0x3E0C)
        at = lambda x, y: tuple(rgba[(y * kh2_pax.PAGE_W + x) * 4 : (y * kh2_pax.PAGE_W + x) * 4 + 4])  # noqa: E731
        self.assertEqual(at(0, 32), (0, 255, 0, 255))  # alpha 0x80 is opaque
        self.assertEqual(at(40, 40), (255, 255, 255, 128))
        self.assertEqual(at(0, 0), (0, 0, 0, 0))  # no picture there

    def test_refused(self):
        import kh2_pax

        data = make_pax(two_pictures(), four_animations())
        with self.assertRaises(kh2_pax.PaxError):
            kh2_pax.read_pax(data[:0x300])
        with self.assertRaises(kh2_pax.PaxError):
            kh2_pax.read_pax(b"BAR\x01" + bytes(12))
        with self.assertRaises(kh2_pax.PaxError):
            kh2_pax.read_pax(b"not a pax at all" * 40)


class MakeTheMod(unittest.TestCase):
    def setUp(self):
        import tempfile

        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.kh2 = self.root / "kh2.hed_out"
        self.mod = self.root / "mods" / "Days - KH2 Prizes"

    def tearDown(self):
        self.temp.cleanup()

    def prize(self, numbers=(0, 1, 2, 3), hd=True):
        from PIL import Image

        original = self.kh2 / "original" / "obj" / "PRIZE.mdlx"
        original.parent.mkdir(parents=True, exist_ok=True)
        original.write_bytes(make_pax(two_pictures(), four_animations(numbers)))
        if hd:  # a made-up HD page, twice the page's size (opened by its content, whatever its name)
            page = self.kh2 / "remastered" / "obj" / "PRIZE.mdlx" / "-0.dds"
            page.parent.mkdir(parents=True, exist_ok=True)
            Image.new("RGBA", (256, 128), (10, 200, 30, 255)).save(page, format="PNG")

    def test_hp_and_munny_made(self):
        import kh2_prizes
        from PIL import Image

        self.prize()
        left_out = kh2_prizes.make(self.kh2, self.mod)
        fx = self.mod / "graphics" / "mo" / "prize.pak.z" / "fx"
        hp = (fx / "prize_01.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual(hp[0], "effect 1")
        self.assertIn("page 0 prize_01_page0.png", hp)
        self.assertIn("body 1", hp)  # the HP orb's core
        self.assertIn("frame_time 1", hp)  # KH2's 2 frames at 60 a second: one of the field's 30
        self.assertIn("body 0", (fx / "prize_03.txt").read_text(encoding="utf-8").splitlines())
        self.assertEqual(hp.count("frame"), 2)
        layers = [line for line in hp if line.startswith("layer ")]
        self.assertEqual(
            layers[0],
            "layer page 0 rect 0.265625 0.53125 0.21875 0.4375 corners -336 -336 336 -336 -336 336 336 336 "
            "colour 1.125 0.25 0.2266 1 blend add",
        )
        self.assertIn("corners 0 -356 356 0 -356 0 0 356", layers[2])
        with Image.open(fx / "prize_01_page0.png") as page:
            self.assertEqual(page.size, (256, 128))  # the HD page
        self.assertTrue((self.mod / "days-mods" / "kh2-prizes.txt").exists())
        # the MP orb and the drive orb: no Days prize; the Coliseum's coin: its file not there; the crystals: no Re:CoM
        self.assertEqual(len(left_out), 5)
        self.assertTrue(any("MP orb" in line for line in left_out) and any("drive orb" in line for line in left_out))
        self.assertTrue(any(line.startswith("he_prize: KH2's obj/PRIZE_HE.mdlx is not there") for line in left_out))
        self.assertIn("ef_pp: Re:CoM's prize sheet is not there", left_out)
        self.assertIn("prize_08: Re:CoM's prize sheet is not there", left_out)

    def test_the_original_pages_without_the_remaster(self):
        import kh2_prizes
        from PIL import Image

        self.prize(hd=False)
        kh2_prizes.make(self.kh2, self.mod)
        with Image.open(self.mod / "graphics" / "mo" / "prize.pak.z" / "fx" / "prize_03_page0.png") as page:
            self.assertEqual(page.size, (128, 64))
            self.assertEqual(page.getpixel((0, 32)), (0, 255, 0, 255))

    def test_the_coliseum_coin(self):
        import kh2_prizes

        self.prize()
        coin = self.kh2 / "original" / "obj" / "PRIZE_HE.mdlx"
        coin.write_bytes(make_pax(two_pictures(), [animation(0, [frame(0, 0x3000, 0x3E0C, [CORE, GLOW])] * 3)]))
        left_out = kh2_prizes.make(self.kh2, self.mod)
        text = (self.mod / "graphics" / "mo" / "prize.pak.z" / "fx" / "he_prize.txt").read_text(encoding="utf-8")
        self.assertIn("(obj/PRIZE_HE.mdlx)", text)  # its own file named
        self.assertIn("body 0", text.splitlines())  # the coin, under its bolt's glow
        self.assertEqual(text.splitlines().count("frame"), 3)
        self.assertFalse(any(line.startswith("he_prize") for line in left_out))

    def test_the_crystals_from_recom(self):
        import kh2_prizes
        from PIL import Image, ImageDraw

        self.prize()
        recom = self.root / "Recom.hed_out"
        sheet = recom / "remastered" / kh2_prizes.RECOM_PRIZES / kh2_prizes.RECOM_SHEET
        sheet.parent.mkdir(parents=True)
        made_up = Image.new("RGBA", (256, 128), (0, 0, 0, 0))  # a red bar lying as Re:CoM's crystal lies
        ImageDraw.Draw(made_up).line((70, 6, 122, 58), fill=(200, 30, 30, 255), width=12)
        made_up.save(sheet, format="PNG")
        left_out = kh2_prizes.make(self.kh2, self.mod, recom=recom)
        fx = self.mod / "graphics" / "mo" / "prize.pak.z" / "fx"
        text = (fx / "ef_pp.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual(text[0], "effect 1")
        self.assertIn("page 0 ef_pp_page0.png", text)
        self.assertIn("body 0", text)
        # one still frame, Days's crystal's 16 x 32: the port makes it as wide as the picture
        self.assertEqual(text.count("frame"), 1)
        self.assertIn("layer page 0 rect 0 0 1 1 corners -8 -16 8 -16 -8 16 8 16 colour 1 1 1 1 blend alpha", text)
        with Image.open(fx / "ef_pp_page0.png") as grey, Image.open(fx / "prize_08_page0.png") as blue:
            self.assertGreater(grey.height, 60)  # stood up: as tall as the bar is long
            self.assertEqual(grey.width, grey.height // 2)
            r, g, b, a = grey.getpixel((grey.width // 2, grey.height // 2))
            self.assertTrue(a == 255 and r == g == b)
            r, g, b, a = blue.getpixel((blue.width // 2, blue.height // 2))
            self.assertTrue(a == 255 and b > r and b > g)
        self.assertFalse(any(line.startswith(("ef_pp", "prize_08")) for line in left_out))

    def test_a_missing_animation_said(self):
        import kh2_prizes

        self.prize(numbers=(0, 2, 3))
        left_out = kh2_prizes.make(self.kh2, self.mod)
        fx = self.mod / "graphics" / "mo" / "prize.pak.z" / "fx"
        self.assertTrue((fx / "prize_03.txt").exists())
        self.assertFalse((fx / "prize_01.txt").exists())
        self.assertTrue(any(line.startswith("prize_01") for line in left_out))


class MakeTheChests(unittest.TestCase):
    """The chests' part with khdays-native's --kh2-object stood in for: what it is asked and what is written."""

    def setUp(self):
        import tempfile

        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.kh2 = self.root / "kh2.hed_out"
        self.mod = self.root / "mods" / "Days - KH2 Prizes"
        self.asked = []

    def tearDown(self):
        self.temp.cleanup()

    def objects(self, names):
        obj = self.kh2 / "original" / "obj"
        obj.mkdir(parents=True, exist_ok=True)
        for name in names:
            (obj / name).write_bytes(b"BAR made up")

    def run_exe(self, line):  # as khdays-native --kh2-object answers: a model 96 wide
        self.asked.append(line)
        out = Path(line[3])
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("{}")
        return "\n".join(
            ["khdays-native: working folder", '{"bones":4,"min":[-48,0,-45],"max":[48,87,44],"report":[]}']
        )

    def test_each_world_its_chest(self):
        import kh2_prizes

        self.objects(["F_EX040_TT.mdlx", "F_EX040_TT.mset", "F_EX040_EH.mdlx", "F_EX040.mset", "F_EX030.mdlx"])
        self.objects(["F_EX030.mset"])
        made, left_out = kh2_prizes.make_chests(self.kh2, self.mod, Path("khdays-native"), None, self.run_exe)
        self.assertEqual(
            sorted(made), ["aw's chest: KH2's F_EX030", "eh's chest: KH2's F_EX040_EH", "tt's chest: KH2's F_EX040_TT"]
        )
        tt = next(line for line in self.asked if line[2].endswith("F_EX040_TT.mdlx"))
        self.assertEqual(Path(tt[3]), self.mod / "graphics" / "mo" / "mo#102" / "mdl" / "tt_box_b.gltf")
        self.assertEqual(tt[tt.index("--pose") + 2], "00A")  # made closed
        self.assertEqual(tt[tt.index("--turn") + 1], "180")
        self.assertTrue(tt[tt.index("--pose") + 1].endswith("F_EX040_TT.mset"))  # Twilight Town's own motions
        eh = next(line for line in self.asked if line[2].endswith("F_EX040_EH.mdlx"))
        self.assertTrue(eh[eh.index("--pose") + 1].endswith("F_EX040.mset"))
        bones = (self.mod / "graphics" / "mo" / "mo#102" / "mdl" / "tt_box_b.bones.txt").read_text(encoding="utf-8")
        self.assertIn("j3 box1", bones.splitlines())  # the lids paired
        self.assertIn("keep light01 light02 light03", bones.splitlines())
        self.assertIn("switch kh2_prizes", bones.splitlines())
        self.assertIn(f"scale {0.982 / 96:.6g}", bones.splitlines())  # Days's width
        # the others not there, and Neverland's without Birth by Sleep's pictures
        self.assertTrue(any(line.startswith("al's chest: KH2's F_EX040_AL is not there") for line in left_out))
        self.assertTrue(any(line.startswith("pp's chest: Birth by Sleep") for line in left_out))

    def test_neverland_in_birth_by_sleep_pictures(self):
        import kh2_prizes

        self.objects(["F_EX030.mdlx", "F_EX030.mset"])
        picture = self.root / "bbs_second.hed_out" / "remastered" / kh2_prizes.NEVERLAND / "-0.dds"
        picture.parent.mkdir(parents=True)
        picture.write_bytes(b"DDS made up")
        made, _ = kh2_prizes.make_chests(
            self.kh2, self.mod, Path("khdays-native"), self.root / "bbs_second.hed_out", self.run_exe
        )
        self.assertIn("pp's chest: KH2's F_EX030 in Birth by Sleep's pictures", made)
        pp = next(line for line in self.asked if line[3].endswith("pp_box_b.gltf"))
        self.assertEqual(pp[pp.index("--picture") + 1 : pp.index("--picture") + 3], ["0", str(picture)])

    def test_the_prize_boxes(self):
        import kh2_prizes

        self.objects(["PRIZE_BOX_YELLOW.mdlx", "PRIZE_BOX_BLUE.mdlx"])
        made, left_out = kh2_prizes.make_boxes(self.kh2, self.mod, Path("khdays-native"), self.run_exe)
        self.assertEqual(made, ["prize_05: KH2's PRIZE_BOX_YELLOW", "prize_07: KH2's PRIZE_BOX_BLUE"])
        box = next(line for line in self.asked if line[2].endswith("PRIZE_BOX_BLUE.mdlx"))
        self.assertEqual(Path(box[3]), self.mod / "graphics" / "mo" / "prize.pak.z" / "mdl" / "prize_07.gltf")
        self.assertNotIn("--pose", box)  # at rest, upright
        self.assertTrue(any(line.startswith("prize_06: KH2's PRIZE_BOX_RED is not there") for line in left_out))

    def test_a_chest_that_cannot_be_made_said(self):
        import kh2_prizes

        self.objects(["F_EX030.mdlx", "F_EX030.mset"])

        def fails(line):
            raise RuntimeError("khdays-native --kh2-object: KH2 object: no bones")

        made, left_out = kh2_prizes.make_chests(self.kh2, self.mod, Path("khdays-native"), None, fails)
        self.assertEqual(made, [])
        self.assertTrue(any("aw's chest" in line and "no bones" in line for line in left_out))


if __name__ == "__main__":
    unittest.main()
