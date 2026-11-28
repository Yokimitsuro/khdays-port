"""Number the committed Metal shaders' buffers, textures and samplers as SDL's Metal backend binds them
(port/tools/build_shaders.py, metal_slots: SDL's documented order with the counts the renderer gives it)
without the Vulkan SDK: rewrites only the <name>_msl arrays of port/host/render/generated/*.h, each from
the SPIR-V beside it (the names' sets and bindings). Every shader: the compute ones (*_cs.h) and the
native 3D's vertex and fragment ones (*_vs.h, *_ps.h). Running it again changes nothing.
    python3 port/tools/fix_msl_order.py [--check] [--list]
--check changes nothing and fails if a shader is numbered otherwise, if the renderer's counts are no
longer the ones the numbering was made for, or if a shader has none (the khdays-metal-shader-order test).
--list prints each shader's arguments as they are numbered.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port" / "tools"))
from build_shaders import METAL_COUNTS, c_array, check_renderer_counts, metal_order  # noqa: E402

GENERATED = ROOT / "port" / "host" / "render" / "generated"


def array(text: str, name: str, header: str):
    found = re.search(
        rf"static const unsigned char {name}\[\] = \{{(.*?)\}};\nstatic const unsigned int {name}_size = \d+u;\n",
        text,
        re.S,
    )
    if not found:
        sys.exit(f"{header}: no {name} array")
    return found, bytes(int(b, 16) for b in re.findall(r"0x[0-9a-f]{2}", found[1]))


def main() -> int:
    check = "--check" in sys.argv[1:]
    listing = "--list" in sys.argv[1:]
    wrong = 0
    headers = sorted(GENERATED.glob("*.h"))
    for line in check_renderer_counts():
        wrong += 1
        print(line)
    for base in sorted(set(METAL_COUNTS) - {h.stem for h in headers}):
        wrong += 1
        print(f"{base}: counted in build_shaders.py METAL_COUNTS, but port/host/render/generated has no {base}.h")
    for header in headers:
        base = header.stem
        text = header.read_text(encoding="utf-8")
        found, msl = array(text, base + "_msl", header.name)
        _, spirv = array(text, base + "_spirv", header.name)
        if not msl.endswith(b"\0"):
            sys.exit(f"{header.name}: the MSL does not end in a zero")
        try:
            fixed, changed = metal_order(base, msl[:-1], spirv)
        except ValueError as e:
            wrong += 1
            print(f"{header.name}: {e}")
            continue
        slots = re.findall(r"\b(\w+) \[\[(buffer|texture|sampler)\((\d+)\)\]\]", fixed.decode("utf-8"))
        if listing:
            print(
                f"{header.name}: "
                + (
                    ", ".join(f"{kind} {n} {name}" for name, kind, n in sorted(slots, key=lambda s: (s[1], int(s[2]))))
                    or "nothing bound"
                )
            )
        if not changed:
            if not listing:
                print(f"{header.name}: in SDL's order ({len(slots)} buffers, textures and samplers)")
            continue
        wrong += 1
        if check:
            print(f"{header.name}: not in SDL's order: " + "; ".join(changed))
            continue
        fixed += b"\0"
        header.write_text(
            text[: found.start()] + c_array(base + "_msl", fixed) + text[found.end() :], encoding="utf-8", newline="\n"
        )
        print(f"{header.name}: renumbered ({'; '.join(changed)})")
    return 1 if check and wrong else 0


if __name__ == "__main__":
    sys.exit(main())
