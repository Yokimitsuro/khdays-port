#!/usr/bin/env python3
"""Compila los shaders GLSL a arrays C embebidos (SPIR-V).

Reejecuta esto tras editar cualquier archivo de `shaders/`. Los `.inc`
resultantes se commitean para que el build y el CI no necesiten el Vulkan SDK.

Requiere `glslc` (parte del Vulkan SDK). Se busca en el PATH y, si no, en
`$VULKAN_SDK/Bin`.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SHADERS = REPO / "shaders"
OUT = REPO / "platform" / "pc" / "generated"

# (archivo fuente, nombre del .inc de salida)
STAGES = [
    ("model.vert", "model.vert.spv.inc"),
    ("model.frag", "model.frag.spv.inc"),
]


def find_glslc() -> str:
    found = shutil.which("glslc")
    if found:
        return found
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidate = Path(sdk) / "Bin" / ("glslc.exe" if os.name == "nt" else "glslc")
        if candidate.exists():
            return str(candidate)
    print(
        "ERROR: no se encontró 'glslc'. Instala el Vulkan SDK o añade glslc al PATH.",
        file=sys.stderr,
    )
    sys.exit(1)


def main() -> int:
    glslc = find_glslc()
    OUT.mkdir(parents=True, exist_ok=True)
    for source, output in STAGES:
        src = SHADERS / source
        dst = OUT / output
        subprocess.run(
            [glslc, "-O", str(src), "-mfmt=c", "-o", str(dst)],
            check=True,
        )
        print(f"compiled {source} -> {dst.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
