#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


NITRO_HEADER_SIZE = 0x10
MAX_SECTION_COUNT = 64


@dataclass(frozen=True)
class SectionInfo:
    magic: str
    offset: int
    size: int


@dataclass(frozen=True)
class ModelFileInfo:
    path: str
    size: int
    declared_size: int
    header_size: int
    section_count: int
    sections: list[dict]
    has_mdl0: bool
    has_tex0: bool
    mdl0_size: int
    tex0_size: int
    candidate_score: int
    recommendation: str


def read_u16(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"{label}: cannot read u16 at 0x{offset:X}")
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"{label}: cannot read u32 at 0x{offset:X}")
    return struct.unpack_from("<I", data, offset)[0]


def parse_sections(path: Path, root: Path) -> ModelFileInfo:
    data = path.read_bytes()

    if len(data) < NITRO_HEADER_SIZE:
        raise ValueError("file is smaller than the Nitro header")
    if data[:4] != b"BMD0":
        raise ValueError(f"expected BMD0, found {data[:4]!r}")

    declared_size = read_u32(data, 0x08, "declared size")
    header_size = read_u16(data, 0x0C, "header size")
    section_count = read_u16(data, 0x0E, "section count")

    if header_size < NITRO_HEADER_SIZE or header_size > len(data):
        raise ValueError(f"invalid header size 0x{header_size:X}")
    if section_count == 0 or section_count > MAX_SECTION_COUNT:
        raise ValueError(f"invalid section count {section_count}")
    if declared_size not in (0, len(data)) and declared_size > len(data):
        raise ValueError(
            f"declared size {declared_size} exceeds actual size {len(data)}"
        )

    # Nitro files use an offset table after the fixed header.
    # In Nitro container files such as BMD0, header_size is commonly 0x10.
    # The section-offset table begins at 0x10 and is not included in that
    # fixed header-size value.
    table_end = NITRO_HEADER_SIZE + section_count * 4
    if table_end > len(data):
        raise ValueError("section-offset table exceeds the file")

    offsets = [
        read_u32(data, NITRO_HEADER_SIZE + index * 4, f"section {index} offset")
        for index in range(section_count)
    ]

    sections: list[SectionInfo] = []
    for index, offset in enumerate(offsets):
        if offset < header_size or offset + 8 > len(data):
            raise ValueError(
                f"section {index} offset 0x{offset:X} is outside the file"
            )

        magic_bytes = data[offset:offset + 4]
        magic = "".join(
            chr(value) if 32 <= value <= 126 else "."
            for value in magic_bytes
        )
        size = read_u32(data, offset + 4, f"section {index} size")

        if size < 8 or offset + size > len(data):
            raise ValueError(
                f"section {index} {magic!r} has invalid range "
                f"0x{offset:X}-0x{offset + size:X}"
            )

        sections.append(
            SectionInfo(
                magic=magic,
                offset=offset,
                size=size,
            )
        )

    mdl0_size = sum(section.size for section in sections if section.magic == "MDL0")
    tex0_size = sum(section.size for section in sections if section.magic == "TEX0")
    has_mdl0 = mdl0_size > 0
    has_tex0 = tex0_size > 0

    score = 0
    reasons: list[str] = []

    if has_mdl0:
        score += 50
        reasons.append("contains MDL0")
    if has_tex0:
        score += 100
        reasons.append("contains embedded TEX0")

    size = len(data)
    if 1_024 <= size <= 256_000:
        score += 20
        reasons.append("manageable size")
    elif size <= 1_000_000:
        score += 10

    relative = path.relative_to(root).as_posix()
    if "/mi/ch/" in f"/{relative.lower()}" or relative.lower().startswith("mi/ch/"):
        score += 15
        reasons.append("character path")

    if section_count <= 2:
        score += 10
        reasons.append("simple section layout")

    return ModelFileInfo(
        path=relative,
        size=size,
        declared_size=declared_size,
        header_size=header_size,
        section_count=section_count,
        sections=[asdict(section) for section in sections],
        has_mdl0=has_mdl0,
        has_tex0=has_tex0,
        mdl0_size=mdl0_size,
        tex0_size=tex0_size,
        candidate_score=score,
        recommendation=", ".join(reasons) if reasons else "no special priority",
    )


def find_nsbmd_files(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.rglob("*.nsbmd")
        if path.is_file()
    )


def write_csv(path: Path, records: Iterable[ModelFileInfo]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[
                "path",
                "size",
                "declared_size",
                "header_size",
                "section_count",
                "sections",
                "has_mdl0",
                "has_tex0",
                "mdl0_size",
                "tex0_size",
                "candidate_score",
                "recommendation",
            ],
        )
        writer.writeheader()

        for record in records:
            row = asdict(record)
            row["sections"] = json.dumps(
                record.sections,
                ensure_ascii=False,
                sort_keys=True,
            )
            writer.writerow(row)


def write_report(
    path: Path,
    records: list[ModelFileInfo],
    failures: list[dict[str, str]],
) -> None:
    ranked = sorted(
        records,
        key=lambda record: (
            -record.has_tex0,
            -record.candidate_score,
            record.size,
            record.path,
        ),
    )

    lines = [
        "# NSBMD section analysis",
        "",
        "## Totals",
        "",
        f"- NSBMD files parsed: **{len(records)}**",
        f"- Files with `MDL0`: **{sum(record.has_mdl0 for record in records)}**",
        f"- Files with embedded `TEX0`: **{sum(record.has_tex0 for record in records)}**",
        f"- Parse failures: **{len(failures)}**",
        "",
        "## Best first-viewer candidates",
        "",
        "| Score | Path | Size | Sections | TEX0 | Reason |",
        "|---:|---|---:|---|:---:|---|",
    ]

    for record in ranked[:100]:
        section_text = ", ".join(
            f"{section['magic']}:{section['size']}"
            for section in record.sections
        )
        lines.append(
            f"| {record.candidate_score} "
            f"| `{record.path}` "
            f"| {record.size} "
            f"| `{section_text}` "
            f"| {'yes' if record.has_tex0 else 'no'} "
            f"| {record.recommendation} |"
        )

    if failures:
        lines.extend(
            [
                "",
                "## Parse failures",
                "",
                "| Path | Error |",
                "|---|---|",
            ]
        )
        for failure in failures[:100]:
            lines.append(
                f"| `{failure['path']}` | {failure['error']} |"
            )

    lines.extend(
        [
            "",
            "## Selection rule",
            "",
            "Prefer a small `BMD0` containing both `MDL0` and `TEX0`.",
            "That gives the first viewer a self-contained model and texture set.",
            "Only look for a separate `.nsbtx` when the chosen NSBMD has no `TEX0`.",
            "",
        ]
    )

    path.write_text("\n".join(lines), encoding="utf-8")


def analyze(root: Path, output: Path | None) -> Path:
    root = root.resolve()
    if not root.is_dir():
        raise NotADirectoryError(root)

    files = find_nsbmd_files(root)
    if not files:
        raise FileNotFoundError(f"No .nsbmd files found below {root}")

    records: list[ModelFileInfo] = []
    failures: list[dict[str, str]] = []

    for path in files:
        try:
            records.append(parse_sections(path, root))
        except (OSError, ValueError, struct.error) as exc:
            failures.append(
                {
                    "path": path.relative_to(root).as_posix(),
                    "error": str(exc),
                }
            )

    if output is None:
        output = root / "nsbmd_analysis"
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    ranked = sorted(
        records,
        key=lambda record: (
            -record.has_tex0,
            -record.candidate_score,
            record.size,
            record.path,
        ),
    )

    payload = {
        "schema_version": 1,
        "source": str(root),
        "totals": {
            "files_found": len(files),
            "files_parsed": len(records),
            "failures": len(failures),
            "with_mdl0": sum(record.has_mdl0 for record in records),
            "with_embedded_tex0": sum(record.has_tex0 for record in records),
        },
        "best_candidates": [asdict(record) for record in ranked[:200]],
        "files": [asdict(record) for record in records],
        "failures": failures,
    }

    (output / "summary.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    write_csv(output / "models.csv", records)
    write_report(output / "report.md", records, failures)

    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Inspect NSBMD/BMD0 section tables and identify models with "
            "embedded TEX0 texture data."
        )
    )
    parser.add_argument(
        "root",
        type=Path,
        help="Unpacked root directory containing .nsbmd files",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output directory. Default: <root>/nsbmd_analysis",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        output = analyze(args.root, args.output)
    except (OSError, ValueError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Analysis: {output}")
    print(f"Report:   {output / 'report.md'}")
    print(f"Summary:  {output / 'summary.json'}")
    print(f"CSV:      {output / 'models.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
