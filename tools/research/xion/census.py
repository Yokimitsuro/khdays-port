"""List models in a locally supplied Days ROM, read-only.

Unpacks LZ10/LZ11, P2 and KAPH containers and writes a model census TSV.
Usage: python tools/research/xion/census.py ROM OUT.tsv [--match xio]"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import nitro3d as n3
from nds import NdsRom, lz_decompress


def unpack(blob: bytes) -> bytes:
    if blob[:1] in (b"\x10", b"\x11"):
        try:
            return lz_decompress(blob)
        except Exception:
            return blob
    return blob


def p2_entries(blob: bytes) -> list[tuple[int, bytes]]:
    """(file offset, entry bytes) for a P2 container (offset 0xC base, 0x200 sectors)."""
    if blob[:2] != b"P2":
        return []
    count = n3.u16(blob, 2) & 0x7FFF
    start = n3.u32(blob, 0x0C)
    sectors = [n3.u16(blob, 0x10 + 2 * i) for i in range(count)]
    sizes_off = 0x10 + (count + 1) // 2 * 4
    out = []
    for i in range(count):
        size_word = n3.u32(blob, sizes_off + 4 * i)
        size = size_word & 0x7FFFFFFF
        at = start + sectors[i] * 0x200
        if size == 0:
            continue
        entry = blob[at : at + size]
        if size_word & 0x80000000:
            entry = unpack(entry)
        out.append((at, entry))
    return out


def bmd0s_in(blob: bytes, depth: int = 0) -> list[tuple[int, bytes, str]]:
    """Every BMD0 found in a blob: (offset in the blob, bytes, where it came from)."""
    out: list[tuple[int, bytes, str]] = []
    if depth > 3 or not blob:
        return out
    magic = blob[:4]
    if magic == b"KAPH":
        try:
            sections = n3.kaph_sections(blob)
        except Exception:
            return out
        for i, sec in enumerate(sections):
            if sec is None:
                continue
            off, size = sec
            got = bmd0s_in(blob[off : off + size], depth + 1)
            out += [(off + go, gb, f"KAPH section {i}" + (f" > {gw}" if gw else "")) for go, gb, gw in got]
        return out
    if magic == b"BMD0" or magic == b"BTX0":
        out.append((0, blob, "file"))
        return out
    if blob[:2] == b"P2":
        for at, entry in p2_entries(blob):
            got = bmd0s_in(entry, depth + 1)
            out += [(at + go, gb, f"P2 entry at 0x{at:x}" + (f" > {gw}" if gw else "")) for go, gb, gw in got]
        return out
    return out


def main() -> int:
    rom_path, out_path = sys.argv[1], sys.argv[2]
    rom = NdsRom(rom_path)
    rows = []
    for f in sorted(rom.files, key=lambda x: x.path):
        blob = unpack(rom.read(f))
        for off, bmd, where in bmd0s_in(blob):
            try:
                model_count = len(n3.parse_dict(bmd, n3.bmd0_sections(bmd)["MDL0"][0] + 8).names)
            except Exception:
                model_count = 0
            for mi in range(model_count):
                try:
                    m = n3.decode_model(bmd, mi)
                except Exception as e:
                    rows.append((f.path, hex(off), where, "ERR", str(e), "", "0", "0", "0", ""))
                    continue
                verts = sum(len(x.vertices) for x in m.meshes)
                tris = sum(len(x.indices) // 3 for x in m.meshes)
                tex = ",".join(sorted({mt.texture for mt in m.materials if mt.texture}))
                rows.append(
                    (
                        f.path,
                        hex(off),
                        where,
                        m.name,
                        ",".join(b.name for b in m.bones[:1]),
                        str(len(m.bones)),
                        str(verts),
                        str(tris),
                        str(len(m.meshes)),
                        tex,
                    )
                )
    with open(out_path, "w") as fh:
        fh.write("file\toffset\twhere\tmodel\tfirst_bone\tbones\tverts\ttris\tmeshes\ttextures\n")
        for r in rows:
            fh.write("\t".join(r) + "\n")
    print(f"{len(rows)} models from {len(rom.files)} files -> {out_path}")
    rom.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
