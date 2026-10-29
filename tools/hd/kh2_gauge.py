"""Make the native KH2 HP gauge from kh2_fifth's extracted original/remastered files.

python tools/hd/kh2_gauge.py KH2_FIFTH [--mod NAME] [--lang en|es|fr|de|it ...]
"""

from __future__ import annotations

import argparse
import json
import sys
from copy import deepcopy
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port" / "tools"))
from kh2_add_limit_gauge import add_limit_gauge  # noqa: E402
from kh2_add_limit_marker import add_limit_marker  # noqa: E402
from kh2_add_party_gauges import add_party_gauges, party_config  # noqa: E402
from kh2_center_gauge import recenter_gauge, recenter_obtained  # noqa: E402

LANGUAGES = {"en": "us", "es": "sp", "fr": "fr", "de": "gr", "it": "it"}
CONFIG = "docs/examples/kh2-gauge.json"


def make_gauge(source: Path, output: Path, languages: list[str], settings: Path | None = None) -> None:
    """Convert original bytes once per language; validate every input before publishing any files."""
    defaults = json.loads((ROOT / CONFIG).read_text(encoding="utf-8"))
    prepared = []

    def merge(base, custom, prefix=""):
        schema = base
        if prefix in ("party.slots.1", "party.slots.2", "party.slots.3"):
            schema = defaults
        elif prefix.endswith("player_icon") or prefix.endswith("limit_text"):
            schema = defaults["player_icon"]
        elif prefix in (f"reticles.{name}" for name in ("soft_lock", "hard_lock", "interact", "chain", "error")):
            schema = {key: value for key, value in defaults["reticles"].items() if not isinstance(value, dict)} | base
        for key, value in custom.items():
            if key not in schema:
                raise ValueError(f"unsupported gauge setting: {prefix}.{key}")
            if isinstance(schema[key], dict):
                if not isinstance(value, dict):
                    raise ValueError(f"expected an object for {key}")
                merge(base.setdefault(key, {}), value, f"{prefix}.{key}".lstrip("."))
            else:
                base[key] = deepcopy(value)

    for language in languages:
        region = LANGUAGES[language]
        entry = f"field2d/{region}/zz0field.2dd"
        texture = f"{region.upper()}_zz0field_2dd1.png"
        reticle_texture = f"{region.upper()}_zz0field_2dd0.png"
        original = (source / "original" / entry).read_bytes()
        image = source / "remastered" / entry / texture
        with Image.open(image) as picture:
            if picture.format != "PNG":
                raise ValueError(f"expected the gauge's remastered PNG: {image}")
            picture.verify()
        reticle_image = source / "remastered" / entry / reticle_texture
        with Image.open(reticle_image) as picture:
            if picture.format != "PNG":
                raise ValueError(f"expected the reticles' remastered PNG: {reticle_image}")
            picture.verify()
        converted, limit = add_limit_gauge(recenter_obtained(recenter_gauge(original)))
        converted, marker = add_limit_marker(converted)
        converted, party_clips = add_party_gauges(converted)
        config = deepcopy(defaults)
        config.update(archive="zz0field.2dd", texture=texture, source_origin_x=0, source_origin_y=0)
        for name in ("player_icon", "limit_text", "enemy", "obtained", "backpack_full", "breakthrough", "locked_on"):
            config[name].update(archive="zz0field.2dd", texture=texture, entry="gaug")
        config["reticles"].update(archive="zz0field.2dd", texture=reticle_texture, entry="fiel")
        config["information"].update(archive="zz0field.2dd", texture=texture, entry="gaug")
        config.update(limit_circle=limit[0], limit_bar=limit[1], limit_marker=marker)
        party = party_config(config, party_clips)
        # The converter resets its origin; pack placement comes from the shared template.
        for key in ("offset_x", "offset_y", "portrait_offset_x", "portrait_offset_y"):
            party[key] = defaults["party"][key]
        party["slots"] = json.loads(json.dumps(defaults["party"]["slots"]))
        previous = settings / language / "gauge.json" if settings else None
        resources = {"archive", "texture", "entry", "source_origin_x", "source_origin_y"}
        # Earlier converters accidentally copied these root-only sections into party settings.
        party_excluded = resources | {
            "enemy",
            "reticles",
            "information",
            "obtained",
            "backpack_full",
            "breakthrough",
            "locked_on",
            "mission_panel",
            "heart_points",
            "presentation",
        }
        if previous and previous.is_file():
            custom = json.loads(previous.read_text(encoding="utf-8"))
            if not isinstance(custom, dict) or set(custom) - set(defaults):
                raise ValueError(f"unsupported gauge settings in {previous}")
            merge(config, {key: value for key, value in custom.items() if key not in resources | {"party"}})
            if "party" in custom:
                merge(
                    party, {key: value for key, value in custom["party"].items() if key not in party_excluded}, "party"
                )
        previous_party = settings / language / "party.json" if settings else None
        if (
            previous_party
            and previous_party.is_file()
            and not (previous and previous.is_file() and "party" in json.loads(previous.read_text(encoding="utf-8")))
        ):
            custom = json.loads(previous_party.read_text(encoding="utf-8"))
            if not isinstance(custom, dict) or set(custom) - set(defaults):
                raise ValueError(f"unsupported party gauge settings in {previous_party}")
            # The old -8 test offset is now the built-in first-party placement.
            if custom.get("offset_y") == -8:
                custom["offset_y"] = 0
            merge(party, {key: value for key, value in custom.items() if key not in party_excluded}, "party")
        # Party members never have a Days limit layer, even with a copied player configuration.
        party.update(limit_circle=-1, limit_bar=-1, limit_marker=-1)
        config["party"] = party
        prepared.append(
            (language, converted, texture, image.read_bytes(), reticle_texture, reticle_image.read_bytes(), config)
        )
    for language, archive, texture, pixels, reticle_texture, reticle_pixels, config in prepared:
        folder = output / language
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "zz0field.2dd").write_bytes(archive)
        (folder / texture).write_bytes(pixels)
        (folder / reticle_texture).write_bytes(reticle_pixels)
        (folder / "gauge.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        (folder / "party.json").unlink(missing_ok=True)
        print(
            f"{language}: KH2 gauge, limit clips {config['limit_circle']}/{config['limit_bar']}, divider {config['limit_marker']}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kh2", type=Path, help="kh2_fifth.hed_out (original/ and remastered/)")
    parser.add_argument("--mod", default="HD PACK")
    parser.add_argument("--settings", type=Path, help="preserve existing per-language gauge settings on rebuild")
    parser.add_argument("--lang", nargs="+", choices=LANGUAGES, default=list(LANGUAGES))
    args = parser.parse_args()
    mod = Path(args.mod)
    if mod.name != args.mod or args.mod in ("", ".", ".."):
        parser.error("--mod must be a folder name")
    make_gauge(args.kh2, ROOT / "mods" / args.mod / "layouts" / "gauge", args.lang, args.settings)


if __name__ == "__main__":
    main()
