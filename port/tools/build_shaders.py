"""Compile the renderer's shaders for every graphics backend SDL3 supports: each stage of
port/shaders/<name>.hlsl to DXIL, SPIR-V and MSL, as C arrays in port/host/render/generated/ (committed:
the build never runs this). Needs the Vulkan SDK; the resource layout: docs/BUILD.md, "The tools".
    python port/tools/build_shaders.py [name ...]
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "port" / "shaders"
OUT = ROOT / "port" / "host" / "render" / "generated"

STAGES = {"vs_main": "vs", "ps_main": "ps", "cs_main": "cs"}


def tool(name: str) -> str:
    sdk = os.environ.get("VULKAN_SDK")
    candidates = []
    if sdk:
        candidates.append(Path(sdk) / "Bin" / (name + ".exe"))
        candidates.append(Path(sdk) / "bin" / name)
    found = next((str(c) for c in candidates if c.exists()), None) or shutil.which(name)
    if not found:
        sys.exit(f"build_shaders: {name} not found (install the Vulkan SDK and set VULKAN_SDK)")
    return found


def run(args: list[str]) -> None:
    result = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        sys.exit(f"build_shaders: {' '.join(args)}\n{result.stdout}{result.stderr}")


def version(exe: str) -> str:
    result = subprocess.run([exe, "--version"], capture_output=True, text=True, encoding="utf-8", errors="replace")
    line = (result.stdout or result.stderr).strip().splitlines()
    return line[0] if line else "unknown"


def c_array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i : i + 16]) for i in range(0, len(data), 16)]
    body = ",\n    ".join(rows)
    return (
        f"static const unsigned char {name}[] = {{\n    {body}\n}};\n"
        f"static const unsigned int {name}_size = {len(data)}u;\n"
    )


# How the renderer makes each shader: the resource counts it gives SDL (SDL_GPUComputePipelineCreateInfo,
# SDL_GPUShaderCreateInfo), which SDL's Metal backend numbers the shader's arguments by (metal_slots), and
# where in port/host/render they are written, as text to find there (check_renderer_counts: a count changed
# in the renderer and not here fails port/tools/fix_msl_order.py --check, the khdays-metal-shader-order test).
# Compute: samplers, read-only storage textures and buffers, read-write storage textures and buffers,
# uniform buffers. Vertex and fragment: samplers, storage textures, storage buffers, uniform buffers.
_OF = (
    "gfx_render.c",
    "info.num_readonly_storage_buffers = (Uint32)buffers; "
    "info.num_readwrite_storage_textures = (Uint32)textures; info.num_uniform_buffers = 1;",
)
_RASTER = (
    "gpu_raster3d.c",
    "info.num_readonly_storage_textures = ro_textures; "
    "info.num_readonly_storage_buffers = ro_buffers; info.num_readwrite_storage_textures = rw_textures; "
    "info.num_uniform_buffers = 1; info.num_readwrite_storage_buffers = rw_buffers;",
)
_SHADER = ("gpu_native3d.c", "info.num_storage_buffers = buffers; info.num_uniform_buffers = uniforms;")


def _cs(samplers=0, ro_textures=0, ro_buffers=0, rw_textures=0, rw_buffers=0, uniforms=0, where=()):
    return {
        "stage": "cs",
        "samplers": samplers,
        "ro_textures": ro_textures,
        "ro_buffers": ro_buffers,
        "rw_textures": rw_textures,
        "rw_buffers": rw_buffers,
        "uniforms": uniforms,
        "where": where,
    }


def _gfx(stage, buffers, uniforms, entry):
    kind = "VERTEX" if stage == "vs" else "FRAGMENT"
    return {
        "stage": stage,
        "samplers": 0,
        "textures": 0,
        "buffers": buffers,
        "uniforms": uniforms,
        "where": (_SHADER, ("gpu_native3d.c", f"{entry}, SDL_GPU_SHADERSTAGE_{kind}, {buffers}, {uniforms})")),
    }


METAL_COUNTS = {
    "capture_cs": _cs(rw_textures=3, uniforms=1, where=(_OF, ("gfx_render.c", "CAPTURE_CS_ENTRY, 0, 3)"))),
    "shrink_cs": _cs(rw_textures=2, uniforms=1, where=(_OF, ("gfx_render.c", "SHRINK_CS_ENTRY, 0, 2)"))),
    "onescreen_cs": _cs(
        ro_buffers=4, rw_textures=8, uniforms=1, where=(_OF, ("gfx_render.c", "ONESCREEN_CS_ENTRY, 4, 8)"))
    ),
    "compose2d_cs": _cs(
        ro_buffers=8, rw_textures=6, uniforms=1, where=(_OF, ("gfx_render.c", "COMPOSE2D_CS_ENTRY, 8, 6)"))
    ),
    "raster3d_cs": _cs(
        ro_buffers=7, rw_textures=2, uniforms=1, where=(_RASTER, ("gpu_raster3d.c", "RASTER3D_CS_ENTRY, 0, 7, 2, 0)"))
    ),
    "compose_cs": _cs(
        ro_buffers=1, rw_textures=3, uniforms=1, where=(_RASTER, ("gpu_raster3d.c", "COMPOSE_CS_ENTRY, 0, 1, 3, 0)"))
    ),
    "shadow_mask_cs": _cs(
        ro_buffers=5, rw_buffers=1, uniforms=1, where=(_RASTER, ("gpu_raster3d.c", "SHADOW_MASK_CS_ENTRY, 0, 5, 0, 1)"))
    ),
    "shadow_blur_cs": _cs(
        ro_buffers=1, rw_buffers=1, uniforms=1, where=(_RASTER, ("gpu_raster3d.c", "SHADOW_BLUR_CS_ENTRY, 0, 1, 0, 1)"))
    ),
    "fxaa3d_cs": _cs(rw_textures=2, uniforms=1, where=(_RASTER, ("gpu_raster3d.c", "FXAA3D_CS_ENTRY, 0, 0, 2, 0)"))),
    "trail3d_cs": _cs(rw_textures=3, uniforms=1, where=(_RASTER, ("gpu_raster3d.c", "TRAIL3D_CS_ENTRY, 0, 0, 3, 0)"))),
    "native3d_pack_cs": _cs(
        ro_buffers=1,
        rw_textures=8,
        uniforms=1,
        where=(
            ("gpu_native3d.c", "#define TARGETS 6"),
            (
                "gpu_native3d.c",
                "info.entrypoint = NATIVE3D_PACK_CS_ENTRY; "
                "info.num_readonly_storage_buffers = 1; "
                "info.num_readwrite_storage_textures = TARGETS + 2; "
                "info.num_uniform_buffers = 1;",
            ),
        ),
    ),
    "kh2_sequence_cs": _cs(
        ro_buffers=1,
        rw_buffers=1,
        where=(
            (
                "kh2_sequence_gpu.c",
                "info.entrypoint = KH2_SEQUENCE_CS_ENTRY; "
                "info.num_readonly_storage_buffers = 1; info.num_readwrite_storage_buffers = 1; "
                "info.threadcount_x",
            ),
        ),
    ),
    "native3d_vs": _gfx("vs", 4, 1, "NATIVE3D_VS_ENTRY"),
    "native3d_ps": _gfx("ps", 4, 1, "NATIVE3D_PS_ENTRY"),
    "native3d_z_ps": _gfx("ps", 4, 1, "NATIVE3D_Z_PS_ENTRY"),
    "native3d_rear_vs": _gfx("vs", 0, 0, "NATIVE3D_REAR_VS_ENTRY"),
    "native3d_rear_ps": _gfx("ps", 4, 1, "NATIVE3D_REAR_PS_ENTRY"),
}


def check_renderer_counts() -> list[str]:
    """METAL_COUNTS against the renderer's sources: what is not found there as written here."""
    missing = []
    texts = {}
    for base, counts in METAL_COUNTS.items():
        for name, wanted in counts["where"]:
            if name not in texts:
                texts[name] = " ".join((ROOT / "port" / "host" / "render" / name).read_text(encoding="utf-8").split())
            if " ".join(wanted.split()) not in texts[name]:
                missing.append(f"{base}: port/host/render/{name} no longer has `{wanted}`")
    return missing


def spirv_resources(spirv: bytes) -> dict[str, tuple[str, int, int]]:
    """A SPIR-V module's resources by name: ("buffer", "texture" or "sampler", descriptor set, binding).
    dxc gives the set and binding of each HLSL register (tN/uN/bN, spaceM: set M, binding N) and keeps
    the HLSL name, which spirv-cross gives the MSL argument."""
    words = [int.from_bytes(spirv[i : i + 4], "little") for i in range(0, len(spirv) - 3, 4)]
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError("not a SPIR-V module")
    names, sets, bindings, pointers, kinds, variables = {}, {}, {}, {}, {}, {}
    at = 5
    while at < len(words):
        op, count = words[at] & 0xFFFF, words[at] >> 16
        if count == 0 or at + count > len(words):
            raise ValueError("a broken SPIR-V instruction")
        w = words[at + 1 : at + count]
        if op == 5:  # OpName
            text = b"".join(x.to_bytes(4, "little") for x in w[1:])
            names[w[0]] = text.split(b"\0")[0].decode("utf-8")
        elif op == 71 and w[1] == 34:  # OpDecorate DescriptorSet
            sets[w[0]] = w[2]
        elif op == 71 and w[1] == 33:  # OpDecorate Binding
            bindings[w[0]] = w[2]
        elif op == 32:  # OpTypePointer
            pointers[w[0]] = w[2]
        elif op in (25, 27):  # OpTypeImage, OpTypeSampledImage
            kinds[w[0]] = "texture"
        elif op == 26:  # OpTypeSampler
            kinds[w[0]] = "sampler"
        elif op == 30:  # OpTypeStruct
            kinds[w[0]] = "buffer"
        elif op == 59:  # OpVariable
            variables[w[1]] = w[0]
        at += count
    found = {}
    for var, pointer in variables.items():
        if var not in sets or var not in bindings:
            continue
        kind = kinds.get(pointers.get(pointer))
        if kind is None or var not in names or names[var] in found:
            raise ValueError(f"a resource of the SPIR-V has no usable type or name (id {var})")
        found[names[var]] = (kind, sets[var], bindings[var])
    return found


def metal_slots(base: str, spirv: bytes) -> dict[str, tuple[str, int]]:
    """Where SDL's Metal backend binds each of shader `base`'s resources (SDL_gpu.h, "For MSL/metallib, use
    the following order"; SDL_gpu_metal.m), by name: ("buffer", "texture" or "sampler", index).
    A compute pipeline (SDL_CreateGPUComputePipeline): [[buffer]] the uniform buffers, then the read-only
    storage buffers, then the read-write ones; [[texture]] the sampled textures, then the read-only storage
    textures, then the read-write ones. A vertex or fragment shader (SDL_CreateGPUShader): [[buffer]] the
    uniform buffers, then the storage buffers (the vertex buffers from [[buffer(14)]] on, taken through
    [[stage_in]]); [[texture]] the sampled textures, then the storage textures; [[sampler]] as the sampled
    textures. The place of each within its kind is its place in SDL's SPIR-V sets, which the renderer binds
    by: compute set 0 the sampled textures, read-only storage textures and read-only storage buffers, set 1
    the read-write storage textures and buffers, set 2 the uniform buffers; a vertex shader set 0 its
    textures and storage buffers and set 1 its uniform buffers, a fragment shader sets 2 and 3."""
    if base not in METAL_COUNTS:
        raise ValueError(
            f"no counts for {base} in METAL_COUNTS (port/tools/build_shaders.py): add how the renderer makes it"
        )
    c = METAL_COUNTS[base]
    slots = {}
    for name, (kind, group, binding) in sorted(spirv_resources(spirv).items()):
        where = f"{base}: {name} (set {group}, binding {binding})"
        if c["stage"] == "cs":
            if group == 2 and kind == "buffer":
                limit, index = c["uniforms"], binding
            elif group == 0 and kind == "buffer":
                index = binding - c["samplers"] - c["ro_textures"]
                limit = c["uniforms"] + c["ro_buffers"]
                index = c["uniforms"] + index if index >= 0 else -1
            elif group == 1 and kind == "buffer":
                index = binding - c["rw_textures"]
                limit = c["uniforms"] + c["ro_buffers"] + c["rw_buffers"]
                index = c["uniforms"] + c["ro_buffers"] + index if index >= 0 else -1
            elif group == 0 and kind == "texture":
                limit, index = c["samplers"] + c["ro_textures"], binding
            elif group == 1 and kind == "texture":
                limit = c["samplers"] + c["ro_textures"] + c["rw_textures"]
                index = c["samplers"] + c["ro_textures"] + binding
            elif group == 0 and kind == "sampler":
                limit, index = c["samplers"], binding
            else:
                raise ValueError(f"{where} is in no set SDL binds a compute shader's {kind}s from")
        else:
            resources, uniforms = (0, 1) if c["stage"] == "vs" else (2, 3)
            if group == uniforms and kind == "buffer":
                limit, index = c["uniforms"], binding
            elif group == resources and kind == "buffer":
                index = binding - c["samplers"] - c["textures"]
                limit = c["uniforms"] + c["buffers"]
                index = c["uniforms"] + index if index >= 0 else -1
            elif group == resources and kind == "texture":
                limit, index = c["samplers"] + c["textures"], binding
            elif group == resources and kind == "sampler":
                limit, index = c["samplers"], binding
            else:
                raise ValueError(f"{where} is in no set SDL binds a {c['stage']} shader's {kind}s from")
        if index < 0 or index >= limit:
            raise ValueError(f"{where} is outside what the renderer says it binds (METAL_COUNTS)")
        slots[name] = (kind, index)
    return slots


_MSL_SLOT = re.compile(r"\b(\w+) \[\[(buffer|texture|sampler)\((\d+)\)\]\]")


def metal_order(base: str, msl: bytes, spirv: bytes) -> tuple[bytes, list[str]]:
    """The MSL of shader `base` with every buffer, texture and sampler argument numbered as SDL's Metal
    backend binds it (metal_slots), and what was numbered otherwise ("name: buffer 4 -> 0"). spirv-cross
    numbers the arguments of each kind as it meets them, leaving out the ones the shader does not use: the
    uniform block after the storage buffers (every shader then read its sizes from another buffer, as 0,
    and wrote no pixel: a black picture on Metal), and a buffer after an unused one a place too low."""
    slots = metal_slots(base, spirv)
    changed, taken, seen = [], {}, set()

    def number(m: re.Match) -> str:
        name, kind, old = m[1], m[2], int(m[3])
        if name in seen:
            raise ValueError(f"{base}: duplicate MSL resource `{name}`")
        seen.add(name)
        if name not in slots or slots[name][0] != kind:
            raise ValueError(f"{base}: the MSL's {kind} `{name}` is no {kind} of the SPIR-V")
        new = slots[name][1]
        if taken.setdefault((kind, new), name) != name:
            raise ValueError(f"{base}: `{name}` and `{taken[(kind, new)]}` both at {kind} {new}")
        if new != old:
            changed.append(f"{name}: {kind} {old} -> {new}")
        return f"{name} [[{kind}({new})]]"

    fixed = _MSL_SLOT.sub(number, msl.decode("utf-8")).encode("utf-8")
    if missing := slots.keys() - seen:
        raise ValueError(f"{base}: missing MSL resources: {', '.join(sorted(missing))}")
    return fixed, changed


def sdl_metal_order(base: str, msl: bytes, spirv: bytes) -> bytes:
    try:
        return metal_order(base, msl, spirv)[0]
    except ValueError as e:
        sys.exit(f"build_shaders: {e}")


def build(source: Path, dxc: str, cross: str, versions: str, work: Path) -> None:
    text = source.read_text(encoding="utf-8")
    stages = [(entry, stage) for entry, stage in STAGES.items() if re.search(rf"\b{entry}\s*\(", text)]
    if not stages:
        sys.exit(f"build_shaders: {source.name} has none of {', '.join(STAGES)}")
    for entry, stage in stages:
        profile = f"{stage}_6_0"
        base = f"{source.stem}_{stage}"
        dxil = work / f"{base}.dxil"
        spirv = work / f"{base}.spv"
        msl = work / f"{base}.metal"
        # STAGE_VS, STAGE_PS or STAGE_CS: a file with several stages declares each one's resources
        define = ["-D", f"STAGE_{stage.upper()}"]
        run([dxc, "-nologo", "-T", profile, "-E", entry, *define, "-O3", "-Fo", str(dxil), str(source)])
        run(
            [
                dxc,
                "-nologo",
                "-spirv",
                "-fspv-target-env=vulkan1.1",
                "-T",
                profile,
                "-E",
                entry,
                *define,
                "-O3",
                "-Fo",
                str(spirv),
                str(source),
            ]
        )
        run([cross, str(spirv), "--msl", "--msl-version", "20100", "--output", str(msl)])
        header = OUT / f"{base}.h"
        header.write_text(
            f"/* Generated by port/tools/build_shaders.py from port/shaders/{source.name}\n"
            f" * ({entry}); do not edit. Tools: {versions}. */\n"
            + c_array(f"{base}_dxil", dxil.read_bytes())
            + c_array(f"{base}_spirv", spirv.read_bytes())
            + c_array(f"{base}_msl", sdl_metal_order(base, msl.read_bytes(), spirv.read_bytes()) + b"\0")
            + f'#define {base.upper()}_ENTRY "{entry}"\n',
            encoding="utf-8",
            newline="\n",
        )
        print(
            f"{header.relative_to(ROOT)}: DXIL {dxil.stat().st_size} B, SPIR-V {spirv.stat().st_size} B, "
            f"MSL {msl.stat().st_size} B"
        )


def main() -> int:
    dxc, cross = tool("dxc"), tool("spirv-cross")
    versions = f"{version(dxc)}; spirv-cross"
    names = sys.argv[1:]
    sources = [SHADERS / f"{n}.hlsl" for n in names] if names else sorted(SHADERS.glob("*.hlsl"))
    if not sources:
        sys.exit("build_shaders: no shaders in port/shaders")
    OUT.mkdir(parents=True, exist_ok=True)
    work = ROOT / "build" / "port" / "shaders"
    work.mkdir(parents=True, exist_ok=True)
    for source in sources:
        if not source.exists():
            sys.exit(f"build_shaders: {source} does not exist")
        build(source, dxc, cross, versions, work)
    return 0


if __name__ == "__main__":
    sys.exit(main())
