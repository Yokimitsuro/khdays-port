#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


CONTAINER_MAGICS = (b"KAPH", b"D2KP")

EMBEDDED_SIGNATURES: tuple[tuple[bytes, str, str], ...] = (
    (b"BMD0", "NSBMD", "model"),
    (b"BTX0", "NSBTX", "texture"),
    (b"BCA0", "NSBCA", "skeletal_animation"),
    (b"BTA0", "NSBTA", "texture_animation"),
    (b"BTP0", "NSBTP", "texture_pattern_animation"),
    (b"BMA0", "NSBMA", "material_animation"),
    (b"BVA0", "NSBVA", "visibility_animation"),
    (b"RGCN", "NCGR", "graphics_2d"),
    (b"RLCN", "NCLR", "palette"),
    (b"RCSN", "NSCR", "tilemap"),
    (b"RECN", "NCER", "sprite_cells"),
    (b"RNAN", "NANR", "sprite_animation"),
    (b"NARC", "NARC", "archive"),
    (b"SDAT", "SDAT", "audio_archive"),
)

HEADER_SCAN_SIZE = 0x100
MAX_SIGNATURE_HITS_PER_FILE = 4096


@dataclass(frozen=True)
class SignatureHit:
    offset: int
    offset_hex: str
    magic: str
    detected_format: str
    category: str
    aligned_4: bool


@dataclass(frozen=True)
class HeaderWord:
    offset: int
    offset_hex: str
    value: int
    value_hex: str
    kind: str
    target_magic: str


@dataclass(frozen=True)
class ContainerRecord:
    path: str
    container_magic: str
    size: int
    header_hex: str
    header_words: list[dict[str, Any]]
    signature_hits: list[dict[str, Any]]
    embedded_format_counts: dict[str, int]


def printable_magic(data: bytes) -> str:
    return "".join(chr(value) if 32 <= value <= 126 else "." for value in data)


def scan_signatures(data: bytes) -> list[SignatureHit]:
    hits: list[SignatureHit] = []

    for magic, detected_format, category in EMBEDDED_SIGNATURES:
        start = 0
        while True:
            offset = data.find(magic, start)
            if offset < 0:
                break

            hits.append(
                SignatureHit(
                    offset=offset,
                    offset_hex=f"0x{offset:08X}",
                    magic=magic.decode("ascii"),
                    detected_format=detected_format,
                    category=category,
                    aligned_4=(offset % 4 == 0),
                )
            )
            if len(hits) >= MAX_SIGNATURE_HITS_PER_FILE:
                return sorted(hits, key=lambda hit: hit.offset)

            start = offset + 1

    return sorted(hits, key=lambda hit: hit.offset)


def classify_header_word(value: int, file_size: int, data: bytes) -> tuple[str, str]:
    if value == 0:
        return "zero", ""
    if value == 0xFFFFFFFF:
        return "sentinel", ""
    if 0 <= value < file_size:
        target = data[value:value + 4]
        target_magic = printable_magic(target)
        if any(target == magic for magic, _fmt, _category in EMBEDDED_SIGNATURES):
            return "offset_to_known_signature", target_magic
        if value % 4 == 0:
            return "aligned_offset_candidate", target_magic
        return "offset_candidate", target_magic
    return "scalar_or_out_of_range", ""


def read_header_words(data: bytes) -> list[HeaderWord]:
    limit = min(len(data), HEADER_SCAN_SIZE)
    words: list[HeaderWord] = []

    for offset in range(0, limit - 3, 4):
        value = struct.unpack_from("<I", data, offset)[0]
        kind, target_magic = classify_header_word(value, len(data), data)
        words.append(
            HeaderWord(
                offset=offset,
                offset_hex=f"0x{offset:04X}",
                value=value,
                value_hex=f"0x{value:08X}",
                kind=kind,
                target_magic=target_magic,
            )
        )

    return words


def inspect_container(path: Path, relative_path: str) -> ContainerRecord:
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError(f"Container is too small: {path}")

    magic = data[:4]
    if magic not in CONTAINER_MAGICS:
        raise ValueError(f"Not a KAPH/D2KP container: {path}")

    hits = scan_signatures(data)
    format_counts = Counter(hit.detected_format for hit in hits)

    return ContainerRecord(
        path=relative_path.replace("\\", "/"),
        container_magic=magic.decode("ascii"),
        size=len(data),
        header_hex=data[:64].hex().upper(),
        header_words=[asdict(word) for word in read_header_words(data)],
        signature_hits=[asdict(hit) for hit in hits],
        embedded_format_counts=dict(format_counts),
    )


def load_decompressed_manifest(decompressed_root: Path) -> list[dict[str, Any]]:
    manifest_path = decompressed_root / "decompressed_manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Missing manifest: {manifest_path}")

    try:
        payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {manifest_path}: {exc}") from exc

    files = payload.get("files")
    if not isinstance(files, list):
        raise ValueError("decompressed_manifest.json must contain a 'files' list.")
    return files


def select_entries(
    entries: Iterable[dict[str, Any]],
    path_filters: list[str],
) -> list[dict[str, Any]]:
    selected = []

    for entry in entries:
        output_path = str(entry.get("output_path", ""))
        inner_magic = str(entry.get("inner_magic_ascii", ""))

        if not (inner_magic.startswith("KAPH") or inner_magic.startswith("D2KP")):
            continue

        if path_filters and not any(token.lower() in output_path.lower() for token in path_filters):
            continue

        selected.append(entry)

    return selected


def write_csv(path: Path, records: list[ContainerRecord]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[
                "path",
                "container_magic",
                "size",
                "embedded_hit_count",
                "embedded_formats",
                "first_embedded_offset",
            ],
        )
        writer.writeheader()

        for record in records:
            hits = record.signature_hits
            writer.writerow(
                {
                    "path": record.path,
                    "container_magic": record.container_magic,
                    "size": record.size,
                    "embedded_hit_count": len(hits),
                    "embedded_formats": json.dumps(
                        record.embedded_format_counts,
                        ensure_ascii=False,
                        sort_keys=True,
                    ),
                    "first_embedded_offset": (
                        hits[0]["offset_hex"] if hits else ""
                    ),
                }
            )


def write_markdown(path: Path, records: list[ContainerRecord]) -> None:
    container_counts = Counter(record.container_magic for record in records)
    format_counts: Counter[str] = Counter()
    for record in records:
        format_counts.update(record.embedded_format_counts)

    lines = [
        "# KAPH/D2KP inspection report",
        "",
        "## Totals",
        "",
        f"- Containers inspected: **{len(records)}**",
    ]

    for name, count in container_counts.most_common():
        lines.append(f"- `{name}` containers: **{count}**")

    lines.extend(["", "## Embedded signatures", ""])

    if format_counts:
        lines.extend(
            [
                "| Format | Hits |",
                "|---|---:|",
            ]
        )
        for name, count in format_counts.most_common():
            lines.append(f"| `{name}` | {count} |")
    else:
        lines.append("_No known embedded signatures found._")

    lines.extend(
        [
            "",
            "## Containers with known embedded signatures",
            "",
            "| Container | Size | Embedded signatures | First offset |",
            "|---|---:|---|---:|",
        ]
    )

    candidates = sorted(
        records,
        key=lambda record: (
            -len(record.signature_hits),
            -record.size,
            record.path,
        ),
    )

    shown = 0
    for record in candidates:
        if not record.signature_hits:
            continue

        formats = ", ".join(
            f"{name}×{count}"
            for name, count in sorted(record.embedded_format_counts.items())
        )
        first_offset = record.signature_hits[0]["offset_hex"]
        lines.append(
            f"| `{record.path}` | {record.size} | `{formats}` | `{first_offset}` |"
        )
        shown += 1
        if shown >= 100:
            break

    if shown == 0:
        lines.append("| _None_ |  |  |  |")

    lines.extend(
        [
            "",
            "## Important",
            "",
            "This tool only inspects headers and scans for known embedded signatures.",
            "It does not yet assume a final KAPH/D2KP offset-table layout and does not "
            "extract subfiles.",
            "",
            "The next parser should be based on verified offsets from several real "
            "containers, not on one sample or an unverified historical description.",
            "",
        ]
    )

    path.write_text("\n".join(lines), encoding="utf-8")


def inspect_all(
    decompressed_root: Path,
    output_dir: Path | None,
    path_filters: list[str],
) -> Path:
    decompressed_root = decompressed_root.resolve()
    entries = load_decompressed_manifest(decompressed_root)
    selected = select_entries(entries, path_filters)

    if output_dir is None:
        output_dir = decompressed_root / "container_analysis"
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    records: list[ContainerRecord] = []
    failures: list[dict[str, str]] = []

    for entry in selected:
        relative_path = str(entry["output_path"])
        disk_path = decompressed_root / Path(relative_path)

        try:
            records.append(inspect_container(disk_path, relative_path))
        except (OSError, ValueError) as exc:
            failures.append({"path": relative_path, "error": str(exc)})

    payload = {
        "schema_version": 1,
        "source": str(decompressed_root),
        "filters": path_filters,
        "container_count": len(records),
        "failure_count": len(failures),
        "containers": [asdict(record) for record in records],
        "failures": failures,
    }

    (output_dir / "container_report.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    write_csv(output_dir / "container_inventory.csv", records)
    write_markdown(output_dir / "report.md", records)

    return output_dir


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Inspect decompressed KH Days KAPH/D2KP containers and scan for "
            "embedded Nitro-format signatures."
        )
    )
    parser.add_argument(
        "decompressed",
        type=Path,
        help="Directory containing decompressed_manifest.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output directory. Default: <decompressed>/container_analysis",
    )
    parser.add_argument(
        "--path",
        action="append",
        default=[],
        help=(
            "Only inspect output paths containing this text. "
            "May be supplied multiple times."
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        output = inspect_all(
            decompressed_root=args.decompressed,
            output_dir=args.output,
            path_filters=args.path,
        )
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Analysis complete: {output}")
    print(f"JSON: {output / 'container_report.json'}")
    print(f"CSV:  {output / 'container_inventory.csv'}")
    print(f"MD:   {output / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
