"""The staged HD importer builds native gauge resources from synthetic collection files."""

import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer"))
sys.path.insert(0, str(ROOT / "tools" / "hd"))
import setup_steps as setup  # noqa: E402
import build_installer as installer  # noqa: E402
import kh2_gauge as gauge  # noqa: E402
from kh2_add_limit_gauge import replace_sequence, tables  # noqa: E402


def synthetic_gauge() -> bytes:
    parts = [struct.pack("<I4i2f4I", 0, 99, 60, 144, 105, 0, 0, *([0x80808080] * 4))]
    parts *= 129
    for index, uv in (
        (101, (1, 109, 24, 132)),
        (102, (223, 72, 237, 99)),
        (103, (27, 109, 50, 132)),
        (105, (79, 109, 102, 132)),
    ):
        parts[index] = struct.pack("<I4i2f4I", 0, *uv, 0, 0, *([0x80808080] * 4))
    parts[108] = struct.pack("<I4i2f4I", 0, 99, 60, 144, 105, 0, 0, *([0x80180880] * 4))
    groups = [struct.pack("<4iI", -45, -45, 0, 0, 0)]
    sprites = [struct.pack("<HH", 0, 1)] * 118
    animations = []
    clips = [bytes(36)] * 139
    for index, clip in enumerate((*range(121, 130), *range(132, 139))):
        animation = bytearray(144)
        source = {121: 91, 122: 91, 123: 97, 124: 92, 125: 92, 126: 95, 127: 96, 128: 93, 129: 94}.get(clip, 117)
        struct.pack_into(
            "<II2i4i",
            animation,
            0,
            1,
            source,
            0,
            20,
            455 if clip >= 132 else 0,
            462 if clip >= 132 else 0,
            355 if clip >= 132 else 0,
            349 if clip >= 132 else 0,
        )
        animations.append(bytes(animation))
        clips[clip] = struct.pack("<HH", index, 1) + bytes(32)
    popup = bytearray(144)
    struct.pack_into("<II2i4i", popup, 0, 3, 0, 0, 106, 199, 205, 340, 348)
    clips[76] = struct.pack("<HH", len(animations), 1) + bytes(32)
    animations.append(bytes(popup))
    data = bytearray(80)
    struct.pack_into("<4sI", data, 0, b"BAR\1", 1)
    struct.pack_into("<I4sII", data, 16, 25, b"gaug", 32, 48)
    data[32:40] = b"SEQD\x60\1\0\0"
    return replace_sequence(bytes(data), 16, [parts, groups, sprites, animations, clips])


def sources(folder: Path) -> bytes:
    from PIL import Image

    original = synthetic_gauge()
    for region in gauge.LANGUAGES.values():
        entry = f"field2d/{region}/zz0field.2dd"
        archive = folder / "original" / entry
        archive.parent.mkdir(parents=True, exist_ok=True)
        archive.write_bytes(original)
        texture = folder / "remastered" / entry / f"{region.upper()}_zz0field_2dd1.png"
        texture.parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGBA", (4, 2), (128, 128, 128, 255)).save(texture)
        Image.new("RGBA", (4, 4), (255, 255, 255, 255)).save(texture.with_name(f"{region.upper()}_zz0field_2dd0.png"))
    return original


class GaugeImport(unittest.TestCase):
    def test_obtained_recenters_all_tracks_without_changing_motion(self):
        original = synthetic_gauge()
        _, before = tables(original)
        _, after = tables(gauge.recenter_obtained(original))
        first, count = struct.unpack_from("<HH", before[4][76])
        for index in range(len(before[3])):
            if first <= index < first + count:
                self.assertEqual(struct.unpack_from("<4i", after[3][index], 16), (0, 6, 0, 8))
                self.assertEqual(before[3][index][:16], after[3][index][:16])
                self.assertEqual(before[3][index][32:], after[3][index][32:])
            else:
                self.assertEqual(before[3][index], after[3][index])
        for t in (0, 1, 2, 4):
            self.assertEqual(before[t], after[t])
        entry, regional = tables(original)
        track = bytearray(regional[3][first])
        struct.pack_into("<2i", track, 16, 256, 262)
        regional[3][first] = bytes(track)
        _, centered = tables(gauge.recenter_obtained(replace_sequence(original, entry, regional)))
        self.assertEqual(struct.unpack_from("<4i", centered[3][first], 16), (0, 6, 0, 8))

    def test_party_conversion_preserves_timing_and_centers_the_mirrored_backing(self):
        centered = gauge.recenter_gauge(synthetic_gauge())
        entry, old = tables(centered)
        first = struct.unpack_from("<H", old[4][133])[0]
        animation = bytearray(old[3][first])
        struct.pack_into("<4f", animation, 112, 3, 1, -10, 0)
        old[3][first] = bytes(animation)
        centered = replace_sequence(centered, entry, old)
        converted, mapping = gauge.add_party_gauges(centered)
        _, new = tables(converted)
        for table in range(5):
            self.assertEqual(new[table][: len(old[table])], old[table])
        for original, target in mapping.items():
            a, n = struct.unpack_from("<HH", old[4][int(original)])
            b, m = struct.unpack_from("<HH", new[4][target])
            self.assertEqual(n, m)
            self.assertEqual(old[4][int(original)][4:], new[4][target][4:])
            for source, copy in zip(old[3][a : a + n], new[3][b : b + m]):
                self.assertEqual(source[:4], copy[:4])
                self.assertEqual(source[8:16], copy[8:16])
                self.assertEqual(source[48:112], copy[48:112])
                self.assertEqual(source[128:], copy[128:])
                for offset in range(16, 48, 4):
                    self.assertEqual(
                        struct.unpack_from("<i", copy, offset)[0],
                        round(struct.unpack_from("<i", source, offset)[0] * 23 / 45),
                    )
                for offset in range(112, 128, 4):
                    self.assertAlmostEqual(
                        struct.unpack_from("<f", copy, offset)[0],
                        struct.unpack_from("<f", source, offset)[0] * 23 / 45,
                        places=5,
                    )
        center = new[4][mapping["137"]]
        first = struct.unpack_from("<H", center)[0]
        sprite = struct.unpack_from("<I", new[3][first], 4)[0]
        group, count = struct.unpack("<HH", new[2][sprite])
        self.assertEqual(count, 2)
        self.assertEqual(
            [struct.unpack_from("<4i", g) for g in new[1][group : group + 2]], [(-14, -14, 0, 14), (0, -14, 14, 14)]
        )
        left, right = [struct.unpack_from("<I", g, 16)[0] for g in new[1][group : group + 2]]
        self.assertEqual(struct.unpack_from("<4i", new[0][left], 4), (223, 72, 237, 99))
        self.assertEqual(struct.unpack_from("<4i", new[0][right], 4), (237, 72, 223, 99))
        for g in new[1][len(old[1]) :]:
            x0, y0, x1, y1 = struct.unpack_from("<4i", g)
            self.assertLessEqual(x0, x1)
            self.assertLessEqual(y0, y1)
        cap = struct.unpack_from("<H", new[4][mapping["123"]])[0]
        sprite = struct.unpack_from("<I", new[3][cap], 4)[0]
        group, count = struct.unpack("<HH", new[2][sprite])
        part = struct.unpack_from("<I", new[1][group], 16)[0]
        self.assertEqual(new[0][part][28:], bytes.fromhex("00000080") * 4)
        u0, v0, u1, v1 = struct.unpack_from("<4i", new[0][part], 4)
        self.assertEqual((abs(u1 - u0), abs(v1 - v0)), (23, 1))

    def test_installer_carries_the_converter_and_default_settings(self):
        self.assertTrue({"tools/hd/kh2_gauge.py", *setup.HD_MENU_SUPPORT}.issubset(installer.TOOLS))

    def test_staged_import_converts_all_languages_without_touching_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            original = sources(root / "source")
            for rel in ["tools/hd/kh2_gauge.py", *setup.HD_MENU_SUPPORT]:
                target = root / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / rel, target)
            result = subprocess.run(
                [sys.executable, str(root / "tools/hd/kh2_gauge.py"), str(root / "source"), "--mod", "test"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            for language, region in gauge.LANGUAGES.items():
                folder = root / "mods/test/layouts/gauge" / language
                config = json.loads((folder / "gauge.json").read_text())
                self.assertEqual((config["limit_circle"], config["limit_bar"], config["limit_marker"]), (139, 140, 141))
                self.assertEqual((config["source_origin_x"], config["source_origin_y"]), (0, 0))
                self.assertEqual(config["center_normal"], 137)
                _, old = tables(original)
                _, changed = tables((folder / "zz0field.2dd").read_bytes())
                self.assertEqual(len(changed[4]), 158)
                for source, moved in zip(old[3][9:], changed[3][9:16]):
                    self.assertEqual(struct.unpack_from("<4i", moved, 16), (0, 7, 0, -6))
                    self.assertEqual(moved[:16], source[:16])
                    self.assertEqual(moved[32:], source[32:])
                self.assertEqual(changed[0][131][28:], bytes.fromhex("FFFFFF80") * 4)
                self.assertEqual(changed[0][130][28:], bytes.fromhex("FFE60080") * 4)
                party = config["party"]
                self.assertFalse({"enemy", "reticles", "information", "obtained"} & party.keys())
                self.assertEqual(config["obtained"]["animation"], 76)
                self.assertEqual(config["obtained"]["scale"], 0.65)
                self.assertEqual((config["offset_x"], config["offset_y"]), (-4, 0))
                self.assertEqual((party["offset_x"], party["offset_y"]), (-4, 0))
                self.assertEqual(config["obtained"]["text_offset_y"], 2)
                self.assertEqual(config["obtained"]["text_scale"], 1.25)
                self.assertEqual(config["reticles"]["scale"], 0.35)
                self.assertTrue(all(config["presentation"].values()))
                popup = struct.unpack_from("<H", old[4][76])[0]
                self.assertEqual(struct.unpack_from("<4i", changed[3][popup], 16), (0, 6, 0, 8))
                self.assertEqual(party["scale"], 0.65)
                self.assertAlmostEqual(party["geometry_scale"], 23 / 45)
                self.assertEqual((party["limit_circle"], party["limit_bar"], party["limit_marker"]), (-1, -1, -1))
                self.assertEqual(party["center_normal"], 156)
                self.assertEqual(party["cap"], 144)
                enemy = config["enemy"]
                self.assertTrue(enemy["enabled"])
                self.assertEqual(enemy["scale"], 0.65)
                self.assertEqual(enemy["texture"], f"{region.upper()}_zz0field_2dd1.png")
                self.assertEqual((enemy["source_origin_x"], enemy["source_origin_y"]), (498, 31))
                self.assertEqual(
                    [
                        enemy[key]
                        for key in (
                            "background",
                            "bar_background",
                            "cap",
                            "damage",
                            "preview",
                            "fill",
                            "tank_full",
                            "tank_empty",
                            "tank_damage",
                        )
                    ],
                    list(range(38, 47)),
                )
                self.assertEqual(changed[4][38:47], old[4][38:47])
                source = root / "source/original" / f"field2d/{region}/zz0field.2dd"
                self.assertEqual(source.read_bytes(), original)
                texture = root / "source/remastered" / f"field2d/{region}/zz0field.2dd" / config["texture"]
                self.assertEqual((folder / config["texture"]).read_bytes(), texture.read_bytes())
                self.assertEqual(len(list(folder.iterdir())), 4)
                self.assertEqual(config["reticles"]["entry"], "fiel")
                self.assertEqual(config["reticles"]["texture"], f"{region.upper()}_zz0field_2dd0.png")
                self.assertEqual(config["reticles"]["soft_lock"]["animation"], 5)
                self.assertEqual(config["reticles"]["interact"]["end_animation"], 49)
                self.assertFalse(config["reticles"]["chain"]["enabled"])
                self.assertFalse(config["reticles"]["error"]["enabled"])
                self.assertTrue(config["information"]["enabled"])
                self.assertEqual(config["information"]["texture"], texture.name)
                self.assertEqual([config["information"][k] for k in ("intro", "loop", "reveal", "outro")], [0, 1, 2, 3])
                self.assertEqual(config["information"]["scroll_wait_frames"], 180)
                self.assertEqual(config["information"]["scroll_speed"], 1)
                self.assertEqual(config["information"]["clip_width"], 0)
                self.assertEqual(config["information"]["scale"], 0.65)

    def test_missing_input_does_not_publish_half_a_gauge(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources(root / "source")
            (root / "source/original/field2d/it/zz0field.2dd").unlink()
            with self.assertRaises(FileNotFoundError):
                gauge.make_gauge(root / "source", root / "out", list(gauge.LANGUAGES))
            self.assertFalse((root / "out").exists())

    def test_rebuild_repairs_root_sections_in_old_party_settings(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources(root / "source")
            folder = root / "settings/en"
            folder.mkdir(parents=True)
            old = {"party": {"offset_y": 7, "enemy": {"enabled": True}, "reticles": {}, "information": {}}}
            settings = folder / "gauge.json"
            settings.write_text(json.dumps(old))
            gauge.make_gauge(root / "source", root / "out", ["en"], root / "settings")
            repaired = json.loads((root / "out/en/gauge.json").read_text())
            self.assertEqual(repaired["party"]["offset_y"], 7)
            self.assertFalse({"enemy", "reticles", "information"} & repaired["party"].keys())
            self.assertEqual(json.loads(settings.read_text()), old)

    def test_rebuild_preserves_custom_animations_and_positions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources(root / "source")
            previous = root / "settings/en"
            previous.mkdir(parents=True)
            custom = {
                "portrait_offset_y": -2,
                "offset_x": 17,
                "center_override": 136,
                "limit_marker": -1,
                "texture": "old.png",
                "source_origin_x": 455,
                "enemy": {"offset_x": -4, "scale": 0.75, "fill": 43, "enabled": False},
                "reticles": {"soft_lock": {"scale": 0.8, "offset_y": 6, "end_animation": -1}},
                "information": {"clip_width": 280, "text_offset_x": 3, "scale": 0.65},
                "presentation": {
                    "align_hud_to_edges": True,
                    "pause_mission_target": True,
                    "keep_hud_during_information": False,
                },
            }
            (previous / "gauge.json").write_text(json.dumps(custom))
            (previous / "party.json").write_text(json.dumps({"offset_y": 9, "center_override": 156, "limit_bar": 140}))
            gauge.make_gauge(root / "source", root / "out", ["en"], root / "settings")
            config = json.loads((root / "out/en/gauge.json").read_text())
            for key in ("portrait_offset_y", "offset_x", "center_override", "limit_marker"):
                self.assertEqual(config[key], custom[key])
            self.assertEqual(config["texture"], "US_zz0field_2dd1.png")
            self.assertEqual(config["source_origin_x"], 0)
            for key, value in custom["enemy"].items():
                self.assertEqual(config["enemy"][key], value)
            for key, value in custom["reticles"]["soft_lock"].items():
                self.assertEqual(config["reticles"]["soft_lock"][key], value)
            for key, value in custom["information"].items():
                self.assertEqual(config["information"][key], value)
            party = config["party"]
            self.assertTrue(config["presentation"]["align_hud_to_edges"])
            self.assertTrue(config["presentation"]["pause_mission_target"])
            self.assertFalse(config["presentation"]["keep_hud_during_information"])
            self.assertNotIn("presentation", party)
            self.assertEqual((party["offset_y"], party["center_override"], party["limit_bar"]), (9, 156, -1))
            self.assertEqual(json.loads((previous / "gauge.json").read_text()), custom)

    def test_support_changes_invalidate_hd_install_cache(self):
        before = setup.made_by("hd-menus")
        template = dict(setup.HD_MENU_TOOLS)["kh2_gauge.py"]
        gauge_before = setup.tool_made_by("kh2_gauge.py", template)
        font_before = setup.tool_made_by("kh2_fonts.py", ["{kh2}"])
        read = Path.read_bytes

        def changed(path):
            return read(path) + (b"changed" if path == ROOT / setup.HD_MENU_SUPPORT[0] else b"")

        with patch.object(Path, "read_bytes", changed):
            self.assertNotEqual(before, setup.made_by("hd-menus"))
            self.assertNotEqual(gauge_before, setup.tool_made_by("kh2_gauge.py", template))
            self.assertEqual(font_before, setup.tool_made_by("kh2_fonts.py", ["{kh2}"]))

    def test_authored_mission_panel_survives_regeneration(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources(root / "source")
            folder = root / "pack/en"
            folder.mkdir(parents=True)
            panel = {
                "enabled": True,
                "archive": "mine.2dd",
                "texture": "mine.dds",
                "description_x": 33,
                "tally_x": 249,
                "counter_gap": 9,
                "anchor": "top-left",
                "pause_anchor": "bottom",
            }
            (folder / "gauge.json").write_text(json.dumps({"mission_panel": panel}))
            (folder / "mine.2dd").write_bytes(b"custom authored sequence")
            (folder / "mine.dds").write_bytes(b"custom atlas")
            gauge.make_gauge(root / "source", root / "pack", ["en", "fr"], root / "pack")
            en = json.loads((folder / "gauge.json").read_text())
            fr = json.loads((root / "pack/fr/gauge.json").read_text())
            self.assertTrue(en["mission_panel"]["enabled"])
            for key, value in panel.items():
                self.assertEqual(en["mission_panel"][key], value)
            self.assertNotIn("mission_panel", en["party"])
            self.assertFalse(fr["mission_panel"]["enabled"])
            self.assertEqual((folder / "mine.2dd").read_bytes(), b"custom authored sequence")
            self.assertEqual((folder / "mine.dds").read_bytes(), b"custom atlas")

    def test_legacy_party_file_is_merged_and_test_lift_becomes_default(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources(root / "source")
            settings = root / "pack/en"
            settings.mkdir(parents=True)
            (settings / "gauge.json").write_text(json.dumps({"portrait_offset_y": -2}))
            (settings / "party.json").write_text(json.dumps({"offset_y": -8, "offset_x": 3}))
            gauge.make_gauge(root / "source", root / "pack", ["en"], root / "pack")
            config = json.loads((settings / "gauge.json").read_text())
            self.assertFalse((settings / "party.json").exists())
            self.assertEqual(config["portrait_offset_y"], -2)
            self.assertEqual((config["party"]["offset_x"], config["party"]["offset_y"]), (3, 0))
            self.assertFalse(config["player_icon"]["enabled"])
            self.assertFalse(config["limit_text"]["enabled"])

    def test_nested_slot_settings_do_not_leak_into_other_slots_or_languages(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources(root / "source")
            settings = root / "settings/en"
            settings.mkdir(parents=True)
            custom = {
                "party": {
                    "offset_x": 2,
                    "slots": {
                        "2": {
                            "offset_x": 11,
                            "player_icon": {
                                "enabled": True,
                                "animation": 1,
                                "archive": "icons.any",
                                "texture": "icons.dds",
                            },
                        }
                    },
                }
            }
            (settings / "gauge.json").write_text(json.dumps(custom))
            gauge.make_gauge(root / "source", root / "out", ["en", "fr"], root / "settings")
            en = json.loads((root / "out/en/gauge.json").read_text())
            fr = json.loads((root / "out/fr/gauge.json").read_text())
            self.assertEqual(en["party"]["slots"]["2"]["player_icon"]["archive"], "icons.any")
            self.assertFalse(en["player_icon"]["enabled"])
            self.assertFalse(en["party"]["player_icon"]["enabled"])
            self.assertEqual(en["party"]["slots"]["1"]["offset_x"], 0)
            self.assertEqual(fr["party"]["slots"]["2"]["offset_x"], 0)
            self.assertEqual(fr["player_icon"]["texture"], "FR_zz0field_2dd1.png")


if __name__ == "__main__":
    unittest.main()
