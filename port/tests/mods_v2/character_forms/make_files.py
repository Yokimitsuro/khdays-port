"""Writes the forms walks' two mods (character_forms/, character_forms_two/): the same made-up character, Twin, with
the character walk's figure (../character/, make_files.py there) as its default form's model and the figure in other
colours as its `dual` form's, chosen by the Zero Gear with three Ability Units, with two made-up weapons (a thin bar
on each of the dual-wield Roxas's weapons' first node); each mod's script sets the deck at the select, three units
here and two in character_forms_two/. Nothing of the game: run it again to remake them."""

import json
import shutil
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCK = HERE.parent / "character" / "characters" / "block"
sys.path.insert(0, str(HERE.parent / "character"))
from make_files import box, png  # noqa: E402  (the character walk's box and PNG writer)

DUAL_COLOURS = [(30, 160, 60), (200, 40, 160), (240, 240, 240), (20, 20, 20)]  # the dual form's four texels

CHARACTER = """# A made-up character with two forms for the port's tests (make_files.py): nothing of the game.
[character]
format = 2
base = roxas
name = Twin
face = face.png

[model]
file = model/twin.gltf
skeleton = paired

[stats]
1 = hp 40 strength 9
100 = hp 400 strength 90

[form dual]
base = roxas_dual
priority = 10
when = gear 230 and links 241 >= 3   # Zero Gear, Ability Unit
model = model/twin_dual.gltf
skeleton = paired

[form dual weapons]
ro_w13600 = weapons/right.gltf
ro_w14600 = weapons/left.gltf
"""
# (file, the joint it follows -- the game's weapon model's first node --, its colour)
WEAPONS = [("right", "ro_w13600_a", (250, 220, 40)), ("left", "ro_w14600_a", (40, 220, 250))]


def weapon(name: str, joint: str, colour) -> dict:
    """A bar along z, where the game's weapon models have their blades, on one joint."""
    positions, normals, indices = box((-0.02, -0.02, 0.45), (0.02, 0.02, 0.95))
    blob = b"".join(struct.pack("<3f", *p) for p in positions)
    blob += b"".join(struct.pack("<3f", *n) for n in normals)
    blob += b"".join(struct.pack("<4B", 0, 0, 0, 0) for _ in positions)
    blob += b"".join(struct.pack("<4f", 1, 0, 0, 0) for _ in positions)
    blob += struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    blob += b"".join(struct.pack("<H", i) for i in indices)
    v = len(positions)
    sizes = [12 * v, 12 * v, 4 * v, 16 * v, 64, 2 * len(indices)]
    offsets = [sum(sizes[:k]) for k in range(len(sizes))]
    views = [{"buffer": 0, "byteOffset": o, "byteLength": n} for o, n in zip(offsets, sizes)]
    lows = [min(p[k] for p in positions) for k in range(3)]
    highs = [max(p[k] for p in positions) for k in range(3)]
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": v, "type": "VEC3", "min": lows, "max": highs},
        {"bufferView": 1, "componentType": 5126, "count": v, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5121, "count": v, "type": "VEC4"},
        {"bufferView": 3, "componentType": 5126, "count": v, "type": "VEC4"},
        {"bufferView": 4, "componentType": 5126, "count": 1, "type": "MAT4"},
        {"bufferView": 5, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
    ]
    gltf = {
        "asset": {"version": "2.0", "generator": "khdays-port tests: character_forms/make_files.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": name, "mesh": 0, "skin": 0}, {"name": joint}],
        "skins": [{"joints": [1], "inverseBindMatrices": 4}],
        "meshes": [
            {
                "name": name,
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
                        "indices": 5,
                        "material": 0,
                    }
                ],
            }
        ],
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorFactor": [c / 255 for c in colour] + [1]}}],
        "buffers": [{"uri": f"{name}.bin", "byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    return gltf, blob


MOD = """# The forms walk{which}: Twin (characters/twin/) plays its `dual` form (the game's dual-wield Roxas, its own model)
# with the Zero Gear and three Ability Units, its default form otherwise; its script sets the deck at the select
# with {units} units. Its files: character_forms/make_files.py.
id = test.forms
name = Two forms
version = 1
kind = gameplay
api = 2
"""

SCRIPT = """-- The forms walk's deck (port/tests/mods_v2/character_forms): at the select, the Zero Gear (230) alone with UNITS
-- Ability Units (241) linked on its cells; the camp then chooses Twin's form from it.
local ZERO_GEAR, UNIT = 230, 241
local UNITS = {units}
local set = false

local function index(page, row, col)
  return (page - 1) * 40 + (row - 1) * 5 + col
end

mod.on("scene", function(e)
  if e.to ~= 7 or set then return end
  set = true
  local info = deck.info(ZERO_GEAR)
  local minr, minc = 0, 0
  for _, c in ipairs(info.cells) do
    if c.row < minr then minr = c.row end
    if c.col < minc then minc = c.col end
  end
  local row0, col0 = 1 - minr, 1 - minc
  local saved = deck.save()
  local grid = {{}}
  for i = 1, #saved.grid do grid[i] = 0 end
  grid[index(1, row0, col0)] = ZERO_GEAR
  local linked = 0
  for _, c in ipairs(info.cells) do
    if linked < UNITS and (c.row ~= 0 or c.col ~= 0) then
      grid[index(1, row0 + c.row, col0 + c.col)] = UNIT
      linked = linked + 1
    end
  end
  local owned = {{}}
  for id, n in pairs(saved.owned) do owned[id] = n end
  owned[ZERO_GEAR] = math.max(owned[ZERO_GEAR] or 0, 1)
  owned[UNIT] = math.max(owned[UNIT] or 0, UNITS)
  print("forms deck", deck.restore({{grid = grid, owned = owned}}), "units", linked)
end)
"""


def write(folder: Path, units: int, which: str) -> None:
    twin = folder / "characters" / "twin"
    (twin / "model").mkdir(parents=True, exist_ok=True)
    (folder / "scripts").mkdir(exist_ok=True)
    (folder / "mod.ini").write_text(MOD.format(which=which, units=units), encoding="utf-8", newline="\n")
    (folder / "scripts" / "main.lua").write_text(SCRIPT.format(units=units), encoding="utf-8", newline="\n")
    (twin / "character.ini").write_text(CHARACTER, encoding="utf-8", newline="\n")
    shutil.copyfile(BLOCK / "face.png", twin / "face.png")
    shutil.copyfile(BLOCK / "model" / "block.bin", twin / "model" / "twin.bin")
    shutil.copyfile(BLOCK / "model" / "block.png", twin / "model" / "twin.png")
    (twin / "model" / "twin_dual.png").write_bytes(png(4, 1, DUAL_COLOURS))
    (twin / "weapons").mkdir(exist_ok=True)
    for name, joint, colour in WEAPONS:
        made, blob = weapon(name, joint, colour)
        text = json.dumps(made, indent=1) + "\n"
        (twin / "weapons" / f"{name}.gltf").write_text(text, encoding="utf-8", newline="\n")
        (twin / "weapons" / f"{name}.bin").write_bytes(blob)
    gltf = json.loads((BLOCK / "model" / "block.gltf").read_text(encoding="utf-8"))
    gltf["buffers"][0]["uri"] = "twin.bin"
    for name, picture in (("twin", "twin.png"), ("twin_dual", "twin_dual.png")):
        gltf["images"][0]["uri"] = picture
        (twin / "model" / f"{name}.gltf").write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8", newline="\n")
        shutil.copyfile(BLOCK / "model" / "block.bones.txt", twin / "model" / f"{name}.bones.txt")


def main() -> None:
    write(HERE, 3, "")
    write(HERE.parent / "character_forms_two", 2, " with two units")


if __name__ == "__main__":
    main()
