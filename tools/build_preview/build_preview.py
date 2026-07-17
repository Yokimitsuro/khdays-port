#!/usr/bin/env python3
"""Build a browsable HTML preview of every decodable KH 358/2 Days asset.

This is a TOOLING script: it does not decode anything itself. Every asset is
decoded by the project's own CLI (``khdays-port.exe``); this script only walks
the extracted data, drives the CLI in parallel, converts the CLI's BMP output to
PNG, and writes ``data/preview/index.html``.

Output layout (mirrors the source relative paths)::

    data/preview/
      index.html
      viewer.html                            (shared WebGL model viewer)
      3d/<rel>/<texture>.png + model.obj     (BMD0 models / BTX0 texture sets)
      3d/<rel>/model.js                      (viewer payload; --no-viewers omits)
      ui/p2/<rel>/sub<NNN>/...png            (D2KP packs nested in P2 containers)
      ui/d2kp/<rel>/...png                   (standalone D2KP packs, via slots)
      fonts/<rel>.png                        (NFTR sample sheets)
      text/<rel>.txt                         (.s string tables, P2 message DBs)
      audio/{sequences,streams,swav}/*.wav   (SSEQ / STRM / SWAV)
      build_preview_log.txt

Requires Pillow. On this machine PIL lives in ``python`` (3.13), not ``python3``::

    python tools/build_preview.py                # full run
    python tools/build_preview.py --limit 20     # quick subset for testing
    python tools/build_preview.py --only 3d,audio
"""

from __future__ import annotations

import argparse
import base64
import collections
import concurrent.futures as futures
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from mdl0_materials import Mdl0Error, mesh_texture_bindings

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit(
        "ERROR: Pillow is required. On this machine use `python` (3.13), not "
        "`python3`:\n    python tools/build_preview.py"
    )

# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------

REPO = Path(__file__).resolve().parents[2]  # tools/build_preview/ -> repo root
DATA = REPO / "data" / "extracted" / "1ecf5e7a41a2ae48"
NITRO = DATA / "nitrofs"
DEC = DATA / "decompressed"
OUT = REPO / "data" / "preview"
SDAT = NITRO / "snd" / "sound_data.sdat"

CATEGORIES = ["3d", "ui", "fonts", "text", "audio"]

PANGRAM = ("The quick brown fox jumps over the lazy dog "
           "0123456789 !?.,:;'\"()[]-+/&%#@")

# --dump-ui renders EVERY screen x tile-sheet x palette permutation of a D2KP
# pack. Most permutations are one background paired with the wrong tile sheet or
# palette, and the blow-up is multiplicative: UI/cm/cm.p2 sub-file 85 alone
# writes over 1.2 million BMPs, unbounded. The CLI takes no cap and must not be
# modified, so we bound it from the outside and keep BG_PER_SCREEN compositions
# of each distinct screen afterwards.
#
# Both a file cap and a time cap are needed: the CLI only writes *non-blank*
# compositions, so a pathological pack can burn minutes of CPU on combinations
# that never produce a file. The time cap is what actually binds there; the file
# cap catches packs that do write fast. Packs under both caps are unaffected
# (e.g. UI/mlt/res.p2 sub 0 emits its 504 compositions in ~1s).
DUMP_UI_FILE_CAP = 2000
DUMP_UI_TIMEOUT = 150
BG_PER_SCREEN = 4
MAX_BG_PER_SUB = 64
MAX_CELLS_PER_SUB = 256

# Magics we classify by (never by extension - many files are extensionless).
MAGIC_MODEL = b"BMD0"
MAGIC_TEX = b"BTX0"
MAGIC_D2KP = b"D2KP"
MAGIC_ANIMS = {b"BCA0", b"BMA0", b"BVA0", b"BTA0", b"BTP0"}
MAGIC_KAPH = b"KAPH"
MAGIC_NFTR = b"RTFN"

# Write a viewer payload (3d/<rel>/model.js) next to each exported model.obj.
# Measured over the 530 models in a full run: ~13 MB total (8.2 MB of OBJ text
# plus ~4.9 MB of base64 textures), i.e. +5% on a ~254 MB preview, and a few
# seconds. That is cheap enough to leave on by default - a gallery whose models
# only open in Blender is the problem this solves - but --no-viewers turns it
# off for a leaner 3d/ tree.
VIEWERS = True
VIEWER_TEMPLATE = Path(__file__).resolve().parent / "viewer.html"

_print_lock = threading.Lock()
_log_lines: list[str] = []
_stats: collections.Counter = collections.Counter()


def log(msg: str, quiet: bool = False) -> None:
    with _print_lock:
        _log_lines.append(msg)
        if not quiet:
            print(msg, flush=True)


def find_exe() -> Path:
    for rel in ("build/Release/khdays-port.exe", "build/Debug/khdays-port.exe",
                "build/khdays-port", "build/Release/khdays-port"):
        p = REPO / rel
        if p.exists():
            return p
    sys.exit("ERROR: khdays-port CLI not found under build/. Build it first.")


EXE = find_exe()


def run(args: list, timeout: int = 300) -> tuple[bool, str, str]:
    """Invoke the CLI. Never raises - failures are reported, not fatal."""
    try:
        p = subprocess.run(
            [str(a) for a in args], capture_output=True, text=True,
            timeout=timeout, encoding="utf-8", errors="replace",
        )
        return p.returncode == 0, p.stdout or "", p.stderr or ""
    except subprocess.TimeoutExpired:
        return False, "", f"timeout after {timeout}s"
    except Exception as exc:  # noqa: BLE001 - never abort the whole run
        return False, "", repr(exc)


def magic_of(path: Path) -> bytes:
    try:
        with path.open("rb") as f:
            return f.read(4)
    except Exception:  # noqa: BLE001
        return b""


def safe_name(name: str) -> str:
    return re.sub(r'[<>:"/\\|?*]', "_", name).strip() or "unnamed"


# --------------------------------------------------------------------------
# BMP -> PNG (the CLI writes BMP; the gallery shows PNG)
# --------------------------------------------------------------------------

def bmp_to_png(bmp: Path, drop_1x1: bool = True) -> Path | None:
    """Convert one BMP to PNG and delete the BMP. Returns the PNG path."""
    try:
        with Image.open(bmp) as im:
            im.load()
            if drop_1x1 and im.size == (1, 1):
                bmp.unlink(missing_ok=True)
                return None
            png = bmp.with_suffix(".png")
            im.save(png, "PNG")
        bmp.unlink(missing_ok=True)
        return png
    except Exception as exc:  # noqa: BLE001
        log(f"    ! bmp->png failed {bmp}: {exc}", quiet=True)
        _stats["png_errors"] += 1
        return None


def convert_dir(d: Path) -> list[Path]:
    if not d.is_dir():
        return []
    out = []
    for bmp in sorted(d.rglob("*.bmp")):
        png = bmp_to_png(bmp)
        if png:
            out.append(png)
    return out


_STP_RE = re.compile(r"s(\d+)_t(\d+)_p(\d+)")


def convert_bg_dir(d: Path, per_screen: int, max_total: int) -> list[Path]:
    """Convert --dump-ui output, keeping a representative spread per screen.

    --dump-ui writes every non-blank screen x tile-sheet x palette permutation
    (it is an exploration aid). A single pack can emit ~49k images that are the
    same artwork paired with wrong tile sheets/palettes. We keep the first
    `per_screen` combinations of each distinct screen - which preserves every
    background that exists - and drop the rest unconverted.
    """
    if not d.is_dir():
        return []
    bmps = sorted(d.glob("*.bmp"))

    def key(p: Path):
        m = _STP_RE.search(p.name)
        return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (0, 0, 0)

    bmps.sort(key=key)
    keep, seen = [], collections.Counter()
    for b in bmps:
        s = key(b)[0]
        if seen[s] < per_screen and len(keep) < max_total:
            keep.append(b)
            seen[s] += 1
    dropped = len(bmps) - len(keep)
    if dropped:
        _stats["ui_bg_variants_dropped"] += dropped
    keepset = set(keep)
    out = []
    for b in bmps:
        if b in keepset:
            png = bmp_to_png(b)
            if png:
                out.append(png)
        else:
            b.unlink(missing_ok=True)
    return out


def image_size(p: Path) -> str:
    try:
        with Image.open(p) as im:
            return f"{im.size[0]}x{im.size[1]}"
    except Exception:  # noqa: BLE001
        return ""


def prune_empty_dirs(root: Path) -> None:
    if not root.exists():
        return
    for d in sorted((p for p in root.rglob("*") if p.is_dir()),
                    key=lambda p: len(p.parts), reverse=True):
        try:
            if not any(d.iterdir()):
                d.rmdir()
        except OSError:
            pass


# --------------------------------------------------------------------------
# SDAT symbol names (labels only - all audio decoding is done by the CLI)
# --------------------------------------------------------------------------

def sdat_names(sdat: Path) -> dict[str, list[str]]:
    """Read the SDAT SYMB name tables so tracks get real names in the gallery.

    Purely cosmetic: on any error we fall back to index-based names.
    """
    empty = {"sequences": [], "streams": [], "wavearcs": []}
    try:
        d = sdat.read_bytes()
        if d[:4] != b"SDAT":
            return empty
        symb_off = struct.unpack_from("<I", d, 0x10)[0]
        if d[symb_off:symb_off + 4] != b"SYMB":
            return empty
        ptrs = struct.unpack_from("<8I", d, symb_off + 8)

        def record(rel_off: int) -> list[str]:
            if rel_off == 0:
                return []
            base = symb_off + rel_off
            count = struct.unpack_from("<I", d, base)[0]
            names = []
            for i in range(count):
                o = struct.unpack_from("<I", d, base + 4 + i * 4)[0]
                if o == 0:
                    names.append("")
                    continue
                s = symb_off + o
                e = d.index(b"\x00", s)
                names.append(d[s:e].decode("ascii", "replace"))
            return names

        return {
            "sequences": record(ptrs[0]),
            "wavearcs": record(ptrs[3]),
            "streams": record(ptrs[7]),
        }
    except Exception as exc:  # noqa: BLE001
        log(f"  ! SDAT SYMB names unavailable ({exc}); using indices")
        return empty


# --------------------------------------------------------------------------
# Collectors -> task lists
# --------------------------------------------------------------------------

def collect_3d() -> list[tuple]:
    """Every BMD0 / BTX0 under decompressed/ (includes unpacked/ slots)."""
    tasks = []
    for dirpath, _dirs, files in os.walk(DEC):
        for fn in files:
            p = Path(dirpath) / fn
            m = magic_of(p)
            if m == MAGIC_MODEL:
                tasks.append(("model", p))
            elif m == MAGIC_TEX:
                tasks.append(("tex", p))
            elif m in MAGIC_ANIMS:
                _stats["anim_listed"] += 1
            elif m == MAGIC_KAPH:
                # Container: its contents are already expanded under unpacked/.
                _stats["kaph_skipped"] += 1
    tasks.sort(key=lambda t: str(t[1]))
    return tasks


def p2_subfile_count(p: Path) -> int:
    """Sub-file count of a P2 container: u16 'P2' then u16 & 0x1ff."""
    try:
        with p.open("rb") as f:
            h = f.read(4)
        if len(h) < 4 or h[:2] != b"P2":
            return 0
        return struct.unpack_from("<H", h, 2)[0] & 0x1FF
    except Exception:  # noqa: BLE001
        return 0


def collect_ui_p2() -> list[tuple]:
    """(P2 container, sub-file index) for every sub-file of every P2 in nitrofs.

    Sub-files are mostly LZ-compressed, so their real magic is not visible
    without decompressing. Rather than reimplement LZ, we let --dump-ui probe
    each one (it fails fast on non-D2KP) and run the probes in parallel.
    """
    tasks = []
    for dirpath, _dirs, files in os.walk(NITRO):
        for fn in files:
            p = Path(dirpath) / fn
            n = p2_subfile_count(p)
            for i in range(n):
                tasks.append(("p2sub", p, i))
    tasks.sort(key=lambda t: (str(t[1]), t[2]))
    return tasks


def collect_ui_d2kp() -> list[tuple]:
    """Standalone D2KP packs (decompressed from .z) rendered via unpacked slots.

    These are not P2 containers, so --dump-ui/--render-cell cannot read them;
    the extractor's unpacked/<path>/slot_N view gives the NCLR/NCGR/NSCR pieces
    that --render-bg / --render-tiles consume.
    """
    tasks = []
    for dirpath, _dirs, files in os.walk(DEC):
        if (os.sep + "unpacked" + os.sep) in dirpath + os.sep:
            continue
        for fn in files:
            p = Path(dirpath) / fn
            if magic_of(p) == MAGIC_D2KP:
                rel = p.relative_to(DEC)
                slots = DEC / "unpacked" / rel
                if slots.is_dir():
                    tasks.append(("d2kp", p, slots))
    tasks.sort(key=lambda t: str(t[1]))
    return tasks


def collect_fonts() -> list[tuple]:
    tasks = []
    for base in (NITRO, DEC):
        for dirpath, _dirs, files in os.walk(base):
            for fn in files:
                p = Path(dirpath) / fn
                if magic_of(p) == MAGIC_NFTR:
                    tasks.append(("font", p, base))
    tasks.sort(key=lambda t: str(t[1]))
    return tasks


def collect_text() -> list[tuple]:
    tasks = []
    # .s string tables live decompressed (nitrofs has them as .s.z).
    for dirpath, _dirs, files in os.walk(DEC):
        for fn in files:
            if fn.lower().endswith(".s"):
                tasks.append(("strings", Path(dirpath) / fn))
    # P2 message databases.
    db = NITRO / "db"
    if db.is_dir():
        for p in sorted(db.glob("*.p2")):
            tasks.append(("messages", p))
    tasks.sort(key=lambda t: str(t[1]))
    return tasks


# --------------------------------------------------------------------------
# Task handlers - each returns a list of manifest items
# --------------------------------------------------------------------------

def item(cat: str, group: str, name: str, path: Path, kind: str,
         meta: str = "") -> dict:
    return {
        "c": cat, "g": group, "n": name,
        "p": path.relative_to(OUT).as_posix(), "t": kind, "m": meta,
    }


_MODEL_NAME_RE = re.compile(r"^# model:\s*(.+)$", re.MULTILINE)


def write_model_js(src: Path, outdir: Path, obj: Path) -> Path | None:
    """Write the viewer payload for one model, or None if it has no geometry.

    The payload is a *script* (``KH_MODEL({...});``) rather than a data file
    because viewer.html is opened from file://, where fetch()/XHR cannot read a
    sibling file but <script src> loads fine. Textures are inlined as data: URIs
    for the same reason: a file:// <img> taints the WebGL upload in Chrome.
    See viewer.html for the full reasoning.

    The CLI is still the only decoder here: the geometry is --export-obj's own
    output, embedded verbatim. Only the mesh -> texture binding, which the OBJ
    format drops on the floor, comes from mdl0_materials.
    """
    try:
        obj_text = obj.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None

    groups = [ln[2:].strip() for ln in obj_text.splitlines() if ln.startswith("o ")]
    if not groups:
        return None

    try:
        binds = mesh_texture_bindings(src)
    except (Mdl0Error, OSError, struct.error) as exc:
        log(f"    ! bindings {src.name}: {exc}", quiet=True)
        _stats["3d_bind_fail"] += 1
        binds = []

    # The binding walk replays the same render command stream the exporter used,
    # so its mesh order must equal the OBJ's `o` order. If it ever does not, the
    # two have drifted apart: drop the textures rather than paint a model with
    # confidently wrong ones. The viewer still shows it untextured.
    if binds and [m for m, _ in binds] != groups:
        log(f"    ! binding order != OBJ groups for {src.name}", quiet=True)
        _stats["3d_bind_mismatch"] += 1
        binds = []

    tex: dict[str, dict] = {}
    for name in sorted({t for _, t in binds if t}):
        png = outdir / f"{name}.png"
        if not png.exists():
            # Materials can bind a texture that lives in a separate BTX0 file,
            # which --dump-textures on this model never wrote.
            _stats["3d_tex_unresolved"] += 1
            continue
        try:
            blob = base64.b64encode(png.read_bytes()).decode("ascii")
        except OSError:
            continue
        tex[name] = {"src": "data:image/png;base64," + blob}

    name_match = _MODEL_NAME_RE.search(obj_text)
    payload = {
        "name": name_match.group(1).strip() if name_match else src.name,
        "obj": obj_text,
        "binds": [[m, t] for m, t in binds],
        "tex": tex,
    }
    dst = outdir / "model.js"
    dst.write_text(
        "KH_MODEL(" + json.dumps(payload, separators=(",", ":")) + ");\n",
        encoding="utf-8",
    )
    _stats["3d_viewers"] += 1
    if not tex:
        _stats["3d_viewers_untextured"] += 1
    return dst


def do_3d(kind: str, src: Path, force: bool) -> list[dict]:
    rel = src.relative_to(DEC)
    outdir = OUT / "3d" / rel.parent / rel.name.replace(".", "_")
    group = ("3d/" + rel.parent.as_posix()).rstrip("/")
    items: list[dict] = []

    done = outdir.exists() and any(outdir.iterdir())
    if force or not done:
        outdir.mkdir(parents=True, exist_ok=True)
        ok, out, err = run([EXE, "--dump-textures", src, outdir])
        if not ok:
            log(f"    ! dump-textures {rel}: {err.strip()[:90]}", quiet=True)
            _stats["3d_tex_fail"] += 1
        convert_dir(outdir)
        if kind == "model":
            ok, out, err = run([EXE, "--export-obj", src, outdir / "model.obj"])
            if not ok:
                log(f"    ! export-obj {rel}: {err.strip()[:90]}", quiet=True)
                _stats["3d_obj_fail"] += 1

    if outdir.is_dir():
        pngs = sorted(outdir.glob("*.png"))
        for png in pngs:
            items.append(item("3d", group, f"{rel.name} : {png.stem}", png,
                              "img", image_size(png)))
            _stats["3d_textures"] += 1
        obj = outdir / "model.obj"
        if obj.exists():
            js = None
            if VIEWERS:
                js = outdir / "model.js"
                if force or not js.exists():
                    js = write_model_js(src, outdir, obj)
            size = f"{obj.stat().st_size // 1024} KB"
            if js is not None and js.exists():
                # The card opens the in-browser viewer; the OBJ stays on disk
                # for Blender either way.
                # No thumbnail: the card gets the 3D icon instead. Using one of
                # the model's textures here (pngs[0]) is worse than nothing --
                # it is an arbitrary pick, so a character card would show an ear
                # or a patch of skin as if that were the model.
                items.append(item("3d", group, f"{rel.name} : model", js,
                                  "model", size))
            else:
                items.append(item("3d", group, f"{rel.name} : model.obj", obj,
                                  "obj", size))
            _stats["3d_models"] += 1
    return items


def dump_ui_bounded(p2: Path, sub: int, outdir: Path) -> tuple[bool, str, str, bool]:
    """Run --dump-ui, killing it if it exceeds DUMP_UI_FILE_CAP output files.

    Returns (ok, stdout, stderr, truncated). See DUMP_UI_FILE_CAP for why.
    """
    outdir.mkdir(parents=True, exist_ok=True)
    # stdout/stderr go to temp FILES, never pipes: --dump-ui prints one line per
    # composition, which overruns the (~4 KB) Windows pipe buffer and deadlocks
    # the child while we poll. A file handle has no such limit.
    with tempfile.TemporaryFile() as fout, tempfile.TemporaryFile() as ferr:
        try:
            proc = subprocess.Popen(
                [str(EXE), "--dump-ui", str(p2), str(sub), str(outdir)],
                stdout=fout, stderr=ferr,
            )
        except Exception as exc:  # noqa: BLE001
            return False, "", repr(exc), False

        truncated = False
        t0 = time.time()
        while True:
            try:
                proc.wait(timeout=0.15)
                break
            except subprocess.TimeoutExpired:
                pass
            try:
                n = sum(1 for _ in os.scandir(outdir))
            except OSError:
                n = 0
            if n > DUMP_UI_FILE_CAP or (time.time() - t0) > DUMP_UI_TIMEOUT:
                truncated = True
                proc.kill()
                try:
                    proc.wait(timeout=30)
                except Exception:  # noqa: BLE001
                    pass
                break

        def read(f) -> str:
            try:
                f.seek(0)
                return f.read().decode("utf-8", "replace")
            except Exception:  # noqa: BLE001
                return ""

        out, err = read(fout), read(ferr)
    return (proc.returncode == 0 and not truncated), out, err, truncated


_CELLS_RE = re.compile(r"^(\d+) cells")
_D2KP_RE = re.compile(
    r"D2KP: (\d+) NCLR, (\d+) NCGR, (\d+) NSCR, (\d+) NCER, (\d+) NANR")


def do_p2sub(p2: Path, sub: int, force: bool) -> list[dict]:
    rel = p2.relative_to(NITRO)
    outdir = OUT / "ui" / "p2" / rel.parent / rel.name.replace(".", "_") / f"sub{sub:03d}"
    group = "ui/p2/" + rel.as_posix()
    items: list[dict] = []

    if outdir.exists() and any(outdir.iterdir()) and not force:
        for png in sorted(outdir.glob("*.png")):
            items.append(item("ui", group, f"sub{sub} : {png.stem}", png, "img",
                              image_size(png)))
        return items

    if outdir.is_dir():
        # Re-dumping: clear previous output so stale renders cannot survive a
        # changed cap (and so counts stay honest across re-runs).
        for old in outdir.glob("*.png"):
            old.unlink(missing_ok=True)
        for old in outdir.glob("*.bmp"):
            old.unlink(missing_ok=True)

    ok, out, err, truncated = dump_ui_bounded(p2, sub, outdir)
    m = _D2KP_RE.search(out)
    if not m and not truncated:
        # Not a D2KP sub-file - expected for the large majority of sub-files.
        if outdir.is_dir():
            for stray in outdir.glob("*.bmp"):
                stray.unlink(missing_ok=True)
        return []
    if truncated:
        # Killed mid-dump, so the buffered "D2KP:" line may be lost; the cell
        # count is recovered from --render-cell's own output below.
        _stats["ui_dump_truncated"] += 1
        log(f"    ~ dump-ui capped: {rel} sub{sub}", quiet=True)
    nclr, ncgr, nscr, ncer, nanr = (
        (int(g) for g in m.groups()) if m else (0, 0, 0, 0, 0))
    # parse_pk2d returns an empty pack when the blob is not really D2KP, but
    # --dump-ui prints the "D2KP:" line either way. Zero resources => not a pack.
    if m and nclr + ncgr + nscr + ncer + nanr == 0:
        return []
    _stats["ui_d2kp_subfiles"] += 1
    if m and nscr == 0 and ncer == 0 and ncgr > 0:
        # Tile/palette-only pack: --dump-ui composes NSCR backgrounds and
        # --render-cell needs NCER, so the CLI has no way to render these.
        _stats["ui_packs_tiles_only"] += 1

    for png in convert_bg_dir(outdir, BG_PER_SCREEN, MAX_BG_PER_SUB):
        items.append(item("ui", group, f"sub{sub} : {png.stem}", png, "img",
                          image_size(png)))
        _stats["ui_backgrounds"] += 1

    if ncer or truncated:
        outdir.mkdir(parents=True, exist_ok=True)
        probe = outdir / "cell_000.bmp"
        ok, out, err = run([EXE, "--render-cell", p2, sub, 0, probe], timeout=120)
        if ok:
            cm = _CELLS_RE.search(out.strip())
            total = int(cm.group(1)) if cm else 0
            png = bmp_to_png(probe)  # drops 1x1 (empty) cells
            if png:
                items.append(item("ui", group, f"sub{sub} : cell 0", png, "img",
                                  image_size(png)))
                _stats["ui_cells"] += 1
            for c in range(1, min(total, MAX_CELLS_PER_SUB)):
                bmp = outdir / f"cell_{c:03d}.bmp"
                ok, out, err = run([EXE, "--render-cell", p2, sub, c, bmp],
                                   timeout=120)
                if not ok:
                    continue
                png = bmp_to_png(bmp)
                if png:
                    items.append(item("ui", group, f"sub{sub} : cell {c}", png,
                                      "img", image_size(png)))
                    _stats["ui_cells"] += 1
    return items


def do_d2kp(src: Path, slots: Path, force: bool) -> list[dict]:
    rel = src.relative_to(DEC)
    outdir = OUT / "ui" / "d2kp" / rel.parent / rel.name.replace(".", "_")
    group = "ui/d2kp/" + rel.as_posix()
    items: list[dict] = []

    if outdir.exists() and any(outdir.iterdir()) and not force:
        for png in sorted(outdir.glob("*.png")):
            items.append(item("ui", group, png.stem, png, "img", image_size(png)))
        return items

    nclrs = sorted(slots.rglob("*.nclr"))
    ncgrs = sorted(slots.rglob("*.ncgr"))
    nscrs = sorted(slots.rglob("*.nscr"))
    if not nclrs or not ncgrs:
        return []
    outdir.mkdir(parents=True, exist_ok=True)

    for si, nscr in enumerate(nscrs):
        for pi, nclr in enumerate(nclrs):
            bmp = outdir / f"bg_s{si}_p{pi}.bmp"
            ok, _o, _e = run([EXE, "--render-bg", nscr, nclr, bmp, *ncgrs])
            if not ok:
                continue
            png = bmp_to_png(bmp)
            if png:
                items.append(item("ui", group, f"bg s{si} p{pi}", png, "img",
                                  image_size(png)))
                _stats["ui_backgrounds"] += 1

    for ti, ncgr in enumerate(ncgrs):
        for pi, nclr in enumerate(nclrs):
            bmp = outdir / f"tiles_t{ti}_p{pi}.bmp"
            ok, _o, _e = run([EXE, "--render-tiles", ncgr, nclr, bmp])
            if not ok:
                continue
            png = bmp_to_png(bmp)
            if png:
                items.append(item("ui", group, f"tiles t{ti} p{pi}", png, "img",
                                  image_size(png)))
                _stats["ui_tiles"] += 1

    ncer = list(slots.rglob("*.ncer"))
    if ncer:
        # --render-cell needs a P2 container; standalone D2KP cells cannot be
        # rendered by the CLI. Recorded so the count is honest.
        _stats["ui_cells_unrenderable"] += len(ncer)
    return items


def do_font(src: Path, base: Path, force: bool) -> list[dict]:
    rel = src.relative_to(base)
    tag = "nitrofs" if base == NITRO else "decompressed"
    out_png = OUT / "fonts" / tag / rel.parent / (rel.name.replace(".", "_") + ".png")
    group = "fonts/" + tag
    if out_png.exists() and not force:
        return [item("fonts", group, rel.name, out_png, "img", image_size(out_png))]
    out_png.parent.mkdir(parents=True, exist_ok=True)
    bmp = out_png.with_suffix(".bmp")
    ok, out, err = run([EXE, "--render-text", src, PANGRAM, bmp])
    if not ok:
        log(f"    ! render-text {rel}: {err.strip()[:90]}", quiet=True)
        return []
    png = bmp_to_png(bmp)
    if not png:
        return []
    _stats["fonts"] += 1
    meta = out.strip().splitlines()[0][:70] if out.strip() else image_size(png)
    return [item("fonts", group, rel.name, png, "img", meta)]


def do_text(kind: str, src: Path, force: bool) -> list[dict]:
    if kind == "strings":
        rel = src.relative_to(DEC)
        out_txt = OUT / "text" / "strings" / rel.with_suffix(".s.txt")
        group = "text/strings/" + rel.parent.as_posix()
        cmd = "--dump-strings"
    else:
        rel = src.relative_to(NITRO)
        out_txt = OUT / "text" / "messages" / rel.with_suffix(".txt")
        group = "text/messages"
        cmd = "--dump-messages"

    if out_txt.exists() and out_txt.stat().st_size and not force:
        return [item("text", group, rel.name, out_txt, "txt",
                     f"{out_txt.stat().st_size // 1024} KB")]

    ok, out, err = run([EXE, cmd, src], timeout=180)
    if not ok or not out.strip():
        return []
    out_txt.parent.mkdir(parents=True, exist_ok=True)
    out_txt.write_text(out, encoding="utf-8")
    _stats["text_files"] += 1
    _stats["text_lines"] += out.count("\n")
    return [item("text", group, rel.name, out_txt, "txt",
                 f"{out_txt.stat().st_size // 1024} KB")]


def do_audio(force: bool, limit: int | None, jobs: int) -> list[dict]:
    items: list[dict] = []
    if not SDAT.exists():
        log("  ! SDAT not found; skipping audio")
        return items

    (OUT / "audio").mkdir(parents=True, exist_ok=True)
    ok, out, _e = run([EXE, "--audio-info", SDAT])
    if ok:
        info = OUT / "audio" / "audio_info.txt"
        info.write_text(out, encoding="utf-8")
        items.append(item("audio", "audio", "audio_info.txt", info, "txt",
                          "SDAT inventory"))

    names = sdat_names(SDAT)
    seqs = names.get("sequences") or []
    strms = names.get("streams") or []
    n_seq = len(seqs) or 39
    n_strm = len(strms) or 112

    def seq_task(i: int):
        nm = seqs[i] if i < len(seqs) and seqs[i] else f"seq_{i:03d}"
        dst = OUT / "audio" / "sequences" / f"{i:03d}_{safe_name(nm)}.wav"
        if dst.exists() and dst.stat().st_size > 44 and not force:
            return (i, nm, dst, True)
        dst.parent.mkdir(parents=True, exist_ok=True)
        ok, _o, _e = run([EXE, "--render-sequence", SDAT, i, dst, 30], timeout=600)
        if not ok or not dst.exists() or dst.stat().st_size <= 44:
            dst.unlink(missing_ok=True)
            return (i, nm, dst, False)
        return (i, nm, dst, True)

    def strm_task(i: int):
        nm = strms[i] if i < len(strms) and strms[i] else f"strm_{i:03d}"
        dst = OUT / "audio" / "streams" / f"{i:03d}_{safe_name(nm)}.wav"
        if dst.exists() and dst.stat().st_size > 44 and not force:
            return (i, nm, dst, True)
        dst.parent.mkdir(parents=True, exist_ok=True)
        ok, _o, _e = run([EXE, "--extract-stream", SDAT, i, dst], timeout=600)
        if not ok or not dst.exists() or dst.stat().st_size <= 44:
            dst.unlink(missing_ok=True)
            return (i, nm, dst, False)
        return (i, nm, dst, True)

    seq_idx = list(range(n_seq))[: limit or None]
    strm_idx = list(range(n_strm))[: limit or None]

    with futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for i, nm, dst, ok_ in ex.map(seq_task, seq_idx):
            if ok_:
                items.append(item("audio", "audio/sequences (SSEQ, 30s cap)",
                                  f"{i:03d} {nm}", dst, "audio",
                                  f"{dst.stat().st_size // 1024} KB"))
                _stats["audio_sequences"] += 1
            else:
                _stats["audio_seq_empty"] += 1
        for i, nm, dst, ok_ in ex.map(strm_task, strm_idx):
            if ok_:
                items.append(item("audio", "audio/streams (STRM)", f"{i:03d} {nm}",
                                  dst, "audio", f"{dst.stat().st_size // 1024} KB"))
                _stats["audio_streams"] += 1
            else:
                _stats["audio_strm_empty"] += 1

    # SWAV sample: a modest slice of the 879 wave archives (all of them would be
    # tens of thousands of one-shot sfx).
    wa_names = names.get("wavearcs") or []
    n_arch = 12 if limit else 24
    swav_items = []

    def swav_task(args):
        a, s = args
        nm = wa_names[a] if a < len(wa_names) and wa_names[a] else f"wa{a:03d}"
        dst = OUT / "audio" / "swav" / f"{a:03d}_{safe_name(nm)}" / f"swav_{s:03d}.wav"
        if dst.exists() and dst.stat().st_size > 44 and not force:
            return (a, s, nm, dst, True)
        dst.parent.mkdir(parents=True, exist_ok=True)
        ok, _o, _e = run([EXE, "--extract-wav", SDAT, a, s, dst], timeout=120)
        if not ok or not dst.exists() or dst.stat().st_size <= 44:
            dst.unlink(missing_ok=True)
            return (a, s, nm, dst, False)
        return (a, s, nm, dst, True)

    pairs = [(a, s) for a in range(n_arch) for s in range(6)]
    with futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for a, s, nm, dst, ok_ in ex.map(swav_task, pairs):
            if ok_:
                swav_items.append(item("audio", f"audio/swav (sfx sample)",
                                       f"{nm} #{s}", dst, "audio",
                                       f"{dst.stat().st_size // 1024} KB"))
                _stats["audio_swav"] += 1
    items.extend(swav_items)
    prune_empty_dirs(OUT / "audio")
    return items


# --------------------------------------------------------------------------
# index.html
# --------------------------------------------------------------------------

HTML_HEAD = """<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>KH 358/2 Days - asset preview</title>
<style>
:root{color-scheme:light dark;--bg:#f6f7f9;--fg:#14161a;--muted:#5d6470;
--card:#fff;--line:#e2e5ea;--accent:#3b6ea5;--chip:#eef1f5}
@media(prefers-color-scheme:dark){:root{--bg:#0f1115;--fg:#e6e8ec;--muted:#98a0ad;
--card:#171a21;--line:#272c36;--accent:#7aa7d9;--chip:#1e2430}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{position:sticky;top:0;z-index:5;background:var(--card);
border-bottom:1px solid var(--line);padding:14px 20px}
h1{margin:0 0 4px;font-size:17px;letter-spacing:.2px}
.sub{color:var(--muted);font-size:12px}
.bar{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px;align-items:center}
input[type=search]{flex:1;min-width:200px;padding:7px 10px;border:1px solid var(--line);
border-radius:7px;background:var(--bg);color:var(--fg);font-size:13px}
button{padding:6px 11px;border:1px solid var(--line);border-radius:7px;
background:var(--bg);color:var(--fg);cursor:pointer;font-size:12.5px}
button.on{background:var(--accent);color:#fff;border-color:var(--accent)}
button:hover{border-color:var(--accent)}
main{padding:16px 20px 60px;max-width:1500px;margin:0 auto}
details{background:var(--card);border:1px solid var(--line);border-radius:9px;
margin:0 0 8px;overflow:hidden}
summary{cursor:pointer;padding:9px 12px;font-weight:600;font-size:13px;
display:flex;gap:8px;align-items:center;list-style:none}
summary::-webkit-details-marker{display:none}
summary::before{content:"\\25B8";color:var(--muted);transition:transform .15s}
details[open]>summary::before{transform:rotate(90deg)}
.count{margin-left:auto;color:var(--muted);font-weight:400;font-size:11.5px;
background:var(--chip);padding:1px 7px;border-radius:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(132px,1fr));
gap:10px;padding:10px 12px 14px;border-top:1px solid var(--line)}
.cell{background:var(--bg);border:1px solid var(--line);border-radius:7px;
padding:7px;text-align:center;overflow:hidden}
.cell img{max-width:100%;height:88px;object-fit:contain;display:block;margin:0 auto 5px;
image-rendering:pixelated;background:
linear-gradient(45deg,#8883 25%,transparent 25%,transparent 75%,#8883 75%),
linear-gradient(45deg,#8883 25%,transparent 25%,transparent 75%,#8883 75%);
background-size:12px 12px;background-position:0 0,6px 6px;border-radius:4px}
.cell.model{display:block;text-decoration:none;color:inherit;border-color:var(--accent)}
.cell.model:hover{background:var(--chip)}
.cell.model .mt{color:var(--accent)}
.noth{height:88px;display:flex;align-items:center;justify-content:center;
color:var(--muted);font-size:11px;letter-spacing:.5px;border-radius:4px;
background:var(--chip);margin-bottom:5px}
.noth svg{width:44px;height:44px;stroke:var(--accent);stroke-width:1.5;fill:none;
opacity:.75}
.cell.model:hover .noth svg{opacity:1}
.nm{font-size:10.5px;word-break:break-word;line-height:1.35}
.mt{font-size:9.5px;color:var(--muted);margin-top:2px}
.rows{padding:8px 12px 12px;border-top:1px solid var(--line);display:grid;gap:7px}
.row{display:flex;gap:10px;align-items:center;background:var(--bg);
border:1px solid var(--line);border-radius:7px;padding:6px 9px}
.row .nm{flex:1;text-align:left;font-size:12px}
audio{height:32px;max-width:340px}
a{color:var(--accent)}
.empty{padding:30px;text-align:center;color:var(--muted)}
</style>
"""


def build_html(items: list[dict], meta: dict) -> str:
    groups: dict[tuple, list[dict]] = collections.defaultdict(list)
    for it in items:
        groups[(it["c"], it["g"])].append(it)

    payload = []
    for (cat, group), its in sorted(groups.items()):
        its.sort(key=lambda x: x["n"])
        entries = []
        for x in its:
            e = {"n": x["n"], "p": x["p"], "t": x["t"], "m": x["m"]}
            if x.get("th"):
                e["th"] = x["th"]
            entries.append(e)
        payload.append({"c": cat, "g": group, "i": entries})

    counts = collections.Counter(it["c"] for it in items)
    summary = " &middot; ".join(
        f"{counts[c]} {c}" for c in CATEGORIES if counts.get(c))

    body = f"""<header>
<h1>Kingdom Hearts 358/2 Days &mdash; asset preview</h1>
<div class="sub">{len(items)} items &middot; {summary} &middot; generated {meta['when']}
&middot; decoded by <code>{meta['exe']}</code></div>
<div class="bar">
<input type="search" id="q" placeholder="Filter by folder or item name (e.g. axel, pause, ThemeXIII)&hellip;">
<button data-f="all" class="on">All</button>
<button data-f="3d">3D</button><button data-f="ui">UI</button>
<button data-f="fonts">Fonts</button><button data-f="text">Text</button>
<button data-f="audio">Audio</button>
<button id="collapse">Collapse all</button>
</div></header>
<main id="main"></main>
<script>
const DATA={json.dumps(payload, ensure_ascii=False, separators=(',', ':'))};
const main=document.getElementById('main');let filter='all',q='';
function cell(it){{
  if(it.t==='img')return `<div class="cell"><img loading="lazy" src="${{it.p}}" alt="${{it.n}}">`+
    `<div class="nm">${{it.n}}</div><div class="mt">${{it.m||''}}</div></div>`;
  if(it.t==='model'){{
    const u=`viewer.html?m=${{encodeURIComponent(it.p)}}`;
    const th=it.th?`<img loading="lazy" src="${{it.th}}" alt="">`
                  :`<div class="noth" title="3D model"><svg viewBox="0 0 24 24">`+
                   `<path d="M12 2 2 7v10l10 5 10-5V7L12 2z"/>`+
                   `<path d="M2 7l10 5 10-5M12 12v10"/></svg></div>`;
    return `<a class="cell model" href="${{u}}" target="_blank">${{th}}`+
      `<div class="nm">${{it.n}}</div><div class="mt">view in 3D &middot; ${{it.m||''}}</div></a>`;
  }}
  if(it.t==='audio')return `<div class="row"><div class="nm">${{it.n}}</div>`+
    `<audio controls preload="none" src="${{it.p}}"></audio><span class="mt">${{it.m||''}}</span></div>`;
  return `<div class="row"><div class="nm"><a href="${{it.p}}" target="_blank">${{it.n}}</a></div>`+
    `<span class="mt">${{it.t}} ${{it.m||''}}</span></div>`;
}}
function fill(d,g){{
  if(d.dataset.done)return;d.dataset.done=1;
  const img=g.i.some(x=>x.t==='img');
  const box=document.createElement('div');
  box.className=img?'grid':'rows';
  box.innerHTML=g.i.map(cell).join('');
  d.appendChild(box);
}}
function render(){{
  main.innerHTML='';let shown=0;
  for(const g of DATA){{
    if(filter!=='all'&&g.c!==filter)continue;
    let its=g.i;
    if(q){{const m=g.g.toLowerCase().includes(q);
      if(!m){{its=g.i.filter(x=>x.n.toLowerCase().includes(q));if(!its.length)continue;}}}}
    shown+=its.length;
    const d=document.createElement('details');
    d.innerHTML=`<summary>${{g.g}}<span class="count">${{its.length}}</span></summary>`;
    const gg={{...g,i:its}};
    d.addEventListener('toggle',()=>{{if(d.open)fill(d,gg);}});
    if(q)  {{d.open=true;fill(d,gg);}}
    main.appendChild(d);
  }}
  if(!shown)main.innerHTML='<div class="empty">No matches.</div>';
}}
document.querySelectorAll('button[data-f]').forEach(b=>b.onclick=()=>{{
  document.querySelectorAll('button[data-f]').forEach(x=>x.classList.remove('on'));
  b.classList.add('on');filter=b.dataset.f;render();}});
document.getElementById('collapse').onclick=()=>
  document.querySelectorAll('details').forEach(d=>d.open=false);
let t;document.getElementById('q').addEventListener('input',e=>{{
  clearTimeout(t);t=setTimeout(()=>{{q=e.target.value.trim().toLowerCase();render();}},180);}});
render();
</script>
"""
    return HTML_HEAD + body


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main() -> int:
    global BG_PER_SCREEN, MAX_BG_PER_SUB, VIEWERS  # noqa: PLW0603
    ap = argparse.ArgumentParser(description="Build data/preview/ asset gallery.")
    ap.add_argument("--only", default="", help="comma list: " + ",".join(CATEGORIES))
    ap.add_argument("--limit", type=int, default=None,
                    help="max items per category (testing)")
    ap.add_argument("--jobs", type=int, default=min(16, (os.cpu_count() or 4) * 2))
    ap.add_argument("--force", action="store_true", help="redo existing outputs")
    ap.add_argument("--clean", action="store_true", help="wipe data/preview first")
    ap.add_argument("--bg-per-screen", type=int, default=BG_PER_SCREEN,
                    help="--dump-ui compositions kept per screen (default 4)")
    ap.add_argument("--max-bg-per-sub", type=int, default=MAX_BG_PER_SUB,
                    help="max --dump-ui compositions kept per sub-file")
    ap.add_argument("--viewers", dest="viewers", action="store_true",
                    default=True, help="write in-browser 3D viewers (default)")
    ap.add_argument("--no-viewers", dest="viewers", action="store_false",
                    help="skip model.js viewer payloads (~13 MB over 530 models)")
    args = ap.parse_args()
    BG_PER_SCREEN = args.bg_per_screen
    MAX_BG_PER_SUB = args.max_bg_per_sub
    VIEWERS = args.viewers

    cats = [c.strip() for c in args.only.split(",") if c.strip()] or CATEGORIES
    bad = [c for c in cats if c not in CATEGORIES]
    if bad:
        sys.exit(f"unknown category: {bad}")

    if not DATA.is_dir():
        sys.exit(f"ERROR: extracted data not found at {DATA}")
    if args.clean and OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True, exist_ok=True)

    t0 = time.time()
    log(f"CLI   : {EXE}")
    log(f"data  : {DATA}")
    log(f"out   : {OUT}")
    log(f"jobs  : {args.jobs}   categories: {','.join(cats)}"
        + (f"   limit: {args.limit}" if args.limit else ""))

    items: list[dict] = []

    def parallel(label, tasks, fn):
        if args.limit:
            tasks = tasks[: args.limit]
        log(f"\n[{label}] {len(tasks)} tasks")
        done = 0
        got: list[dict] = []
        with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futs = {ex.submit(fn, t): t for t in tasks}
            for f in futures.as_completed(futs):
                done += 1
                try:
                    got.extend(f.result() or [])
                except Exception as exc:  # noqa: BLE001
                    log(f"    ! task {futs[f]}: {exc!r}", quiet=True)
                    _stats[label + "_errors"] += 1
                if done % 250 == 0 or done == len(tasks):
                    log(f"  {label}: {done}/{len(tasks)}  ({len(got)} items)")
        return got

    if "3d" in cats:
        tasks = collect_3d()
        log(f"\n[3d] found {len(tasks)} BMD0/BTX0 "
            f"(skipped {_stats['kaph_skipped']} KAPH containers, "
            f"listed {_stats['anim_listed']} animations)")
        items += parallel("3d", tasks, lambda t: do_3d(t[0], t[1], args.force))

    if "ui" in cats:
        d = collect_ui_d2kp()
        items += parallel("ui-d2kp", d, lambda t: do_d2kp(t[1], t[2], args.force))
        p = collect_ui_p2()
        items += parallel("ui-p2", p, lambda t: do_p2sub(t[1], t[2], args.force))

    if "fonts" in cats:
        items += parallel("fonts", collect_fonts(),
                          lambda t: do_font(t[1], t[2], args.force))

    if "text" in cats:
        items += parallel("text", collect_text(),
                          lambda t: do_text(t[0], t[1], args.force))

    if "audio" in cats:
        log("\n[audio] rendering sequences / streams / swav sample")
        items += do_audio(args.force, args.limit, max(4, args.jobs // 2))

    for sub in ("3d", "ui", "fonts", "text"):
        prune_empty_dirs(OUT / sub)

    if VIEWERS and any(it["t"] == "model" for it in items):
        # One shared viewer for every model; the per-model model.js carries the
        # data. Copied rather than generated so it stays editable as real HTML.
        if VIEWER_TEMPLATE.exists():
            shutil.copyfile(VIEWER_TEMPLATE, OUT / "viewer.html")
        else:
            log(f"  ! viewer template missing at {VIEWER_TEMPLATE}")

    html = build_html(items, {
        "when": time.strftime("%Y-%m-%d %H:%M"),
        "exe": EXE.relative_to(REPO).as_posix(),
    })
    index = OUT / "index.html"
    index.write_text(html, encoding="utf-8")

    total = sum(f.stat().st_size for f in OUT.rglob("*") if f.is_file())
    log("\n" + "=" * 62)
    log(f"items in gallery : {len(items)}")
    for k, v in sorted(_stats.items()):
        log(f"  {k:26s} {v}")
    log(f"preview size     : {total / 1e6:.1f} MB")
    log(f"elapsed          : {time.time() - t0:.0f}s")
    log(f"index            : {index}")
    (OUT / "build_preview_log.txt").write_text("\n".join(_log_lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
