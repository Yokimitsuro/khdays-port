"""Synthetic counter texture wrapping, editable clips, and installer integration."""

import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/hd"))
sys.path.insert(0, str(ROOT / "installer"))
import heart_points as hearts  # noqa: E402
import setup_steps as setup  # noqa: E402
import build_installer as installer  # noqa: E402


class HeartPointsTest(unittest.TestCase):
    def test_compaction_keeps_single_frame_layers_positive(self):
        layout = hearts.Layout([(0, 0, 8, 8)])
        sprite = layout.sprite(0, 8, 8)
        layout.clip("still", [layout.animation(sprite, 0, 0)])
        layout.clip("steps", [layout.animation(sprite, f, f, opacity=(f % 2)) for f in range(5)])
        layout.optimize()
        for record in layout.rows[3]:
            start, end = struct.unpack_from("<2i", record, 8)
            self.assertGreater(end, start)

    def test_total_opacity_fades_down_and_final_tick_stays_opaque(self):
        layout = hearts.Layout([])
        layout.total_pulse("counting", 2253)
        layout.total_pulse("final", 2253, final=True)
        first = [hearts.u32(a, 136) >> 24 for a in layout.rows[3][:5]]
        final = [hearts.u32(a, 136) >> 24 for a in layout.rows[3][5:]]
        self.assertEqual(first, [128, 107, 87, 66, 128])
        self.assertEqual(final, [128] * 5)

    def test_floating_rows_keep_native_alpha_steps_and_cyan_tint(self):
        layout = hearts.Layout([])
        layout.gain("gain")
        layout.gain("bonus_gain", bonus=True)
        self.assertEqual(hearts.u32(layout.rows[3][1], 136) >> 24, round(128 * 29 / 31))
        self.assertEqual(hearts.u32(layout.rows[3][31], 136), 0x80807C00)
        self.assertEqual(hearts.u32(layout.rows[3][-1], 136) >> 24, 0)

    def test_compaction_shares_sprites_and_uses_interpolated_keys(self):
        layout = hearts.Layout([(0, 0, 8, 8)])
        first = layout.sprite(0, 8, 8)
        second = layout.sprite(0, 8, 8)
        layout.clip("moving", [layout.animation(first, f, f, x=2 * f, sx=1 + f / 8) for f in range(9)])
        layout.clip("still", [layout.animation(second, 0, 0)])
        originals = list(layout.rows[3][:9])
        layout.optimize()
        self.assertEqual([len(row) for row in layout.rows[:3]], [1, 1, 1])
        start, count = struct.unpack_from("<HH", layout.rows[4][0])
        self.assertEqual(count, 1)
        merged = layout.rows[3][start]
        self.assertEqual(struct.unpack_from("<2i", merged, 8), (0, 8))
        for frame, source in enumerate(originals):
            self.assertEqual(hearts.key_sample(merged, frame), hearts.key_values(source))

    def test_independent_scale_and_alpha_curves_are_not_flattened(self):
        layout = hearts.Layout([(0, 0, 8, 8)])
        sprite = layout.sprite(0, 8, 8)
        layout.clip("different_curves", [layout.animation(sprite, f, f, sx=1 + f * f, opacity=f / 3) for f in range(4)])
        layout.optimize()
        self.assertGreater(struct.unpack_from("<H", layout.rows[4][0], 2)[0], 1)

    def test_native_tween_curves_and_growth_reset(self):
        self.assertAlmostEqual(hearts.tween(0, 1, 25, 100, 1), 0.0625)
        self.assertAlmostEqual(hearts.tween(0, 1, 25, 100, 2), 0.4375)
        self.assertAlmostEqual(hearts.tween(0, 1, 25, 100, 3), 0.375)
        self.assertAlmostEqual(hearts.tween(0, 1, 75, 100, 3), 0.625)
        layout = hearts.Layout([])
        layout.heart_growth()
        scales = [struct.unpack_from("<f", a, 80)[0] for a in layout.rows[3]]
        self.assertAlmostEqual(scales[0], 1)
        self.assertGreater(scales[4], scales[1])
        self.assertAlmostEqual(scales[-1], 1)

    def test_effect_phases_wait_for_completion_and_draw_before_updating(self):
        keys = hearts.effect_keys([(0, 31, 100), (31, 31, 200), (31, 0, 400)], [(0, 100, 100)] * 3)
        self.assertEqual([row[0] for row in keys[:3]], [0, 0, 0])
        self.assertGreater(keys[3][0], 0)
        self.assertEqual(keys[-1][0], 0)
        self.assertGreater(len(keys), 22)  # Phase completion is consumed on the next update.

    def test_mirror_seams_sample_padded_source_edges(self):
        atlas = hearts.Image.new("RGBA", (8, 8))
        image = hearts.Image.new("RGBA", (2, 2))
        image.putdata([(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255)])
        hearts.paste_texture(atlas, image, 2, 2)
        for y in range(1, 5):
            self.assertEqual(atlas.getpixel((1, y)), image.getpixel((0, max(0, min(1, y - 2)))))
            self.assertEqual(atlas.getpixel((4, y)), image.getpixel((1, max(0, min(1, y - 2)))))

    def test_mirrored_half_is_a_separate_flipped_group(self):
        layout = hearts.Layout([(4, 8, 16, 32)])
        layout.sprite(0, 16, 32, (0, 0, 32, 32, (1 << 16) | (1 << 18)))
        first, count = struct.unpack("<HH", layout.rows[2][0])
        self.assertEqual((first, count), (0, 2))
        left, right = (struct.unpack("<4iI", g) for g in layout.rows[1])
        self.assertEqual(left[:4], (-16, -16, 0, 16))
        self.assertEqual(right[:4], (0, -16, 16, 16))
        self.assertEqual(struct.unpack_from("<4i", layout.rows[0][left[4]], 4), (4, 8, 20, 40))
        self.assertEqual(struct.unpack_from("<4i", layout.rows[0][right[4]], 4), (20, 8, 4, 40))

    def test_shifted_mirror_and_fractional_repeated_edges(self):
        spans = hearts.texture_spans(16, 48, 16, True, True)
        self.assertEqual(spans, [(0, 0.5, 16, 0), (0.5, 1, 0, 16)])
        spans = hearts.texture_spans(120, 152, 16, True, True)
        self.assertEqual(spans, [(0, 0.25, 8, 0), (0.25, 0.75, 0, 16), (0.75, 1, 16, 8)])

    def test_clips_interpolate_without_overlapping_frame_boundaries(self):
        layout = hearts.Layout([(0, 0, 8, 8)])
        sprite = layout.sprite(0, 8, 8)
        layout.clip("moving", [layout.animation(sprite, 0, 0, x=0), layout.animation(sprite, 1, 1, x=8)])
        first = layout.rows[3][0]
        self.assertTrue(struct.unpack_from("<I", first)[0] & (1 << 15))
        self.assertEqual(struct.unpack_from("<4i", first, 8), (0, 1, 0, 8))

    def test_pack_schema_and_installer_include_all_clips(self):
        config = json.loads((ROOT / "docs/examples/kh2-gauge.json").read_text())["heart_points"]
        self.assertEqual(list(config["animations"]), hearts.CLIPS)
        self.assertIn("heart_points.py", setup.DATA_TOOLS)
        self.assertIn("heart_points.py", dict(setup.HD_MENU_TOOLS))
        self.assertIn("tools/hd/heart_points.py", installer.TOOLS)


if __name__ == "__main__":
    unittest.main()
