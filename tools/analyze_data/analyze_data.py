#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


READ_PREFIX_SIZE = 64
ENTROPY_SAMPLE_SIZE = 1024 * 1024


@dataclass(frozen=True)
class Signature:
    name: str
    category: str
    magic_hex: str
    offset: int = 0
    description: str = ""


@dataclass(frozen=True)
class AssetRecord:
    file_id: int
    path: str
    extension: str
    size: int
    sha256: str
    magic_ascii: str
    magic_hex: str
    detected_format: str
    category: str
    compressed: bool
    entropy: float
    score: int
    reason: str


SIGNATURES: tuple[Signature, ...] = (
    # Nintendo DS / Nitro 3D
    Signature("NSBMD", "model", "424D4430", 0, "Nitro 3D model (BMD0)"),
    Signature("NSBTX", "texture", "42545830", 0, "Nitro 3D texture archive (BTX0)"),
    Signature("NSBCA", "animation", "42434130", 0, "Nitro skeletal animation (BCA0)"),
    Signature("NSBTA", "animation", "42544130", 0, "Nitro texture animation (BTA0)"),
    Signature("NSBTP", "animation", "42545030", 0, "Nitro texture pattern animation (BTP0)"),
    Signature("NSBMA", "animation", "424D4130", 0, "Nitro material animation (BMA0)"),
    Signature("NSBVA", "animation", "42564130", 0, "Nitro visibility animation (BVA0)"),

    # Nintendo DS / Nitro archives and audio
    Signature("NARC", "archive", "4E415243", 0, "Nitro archive"),
    Signature("SDAT", "audio", "53444154", 0, "Nitro sound archive"),
    Signature("SSEQ", "audio", "53534551", 0, "Nitro sound sequence"),
    Signature("SSAR", "audio", "53534152", 0, "Nitro sequence archive"),
    Signature("SBNK", "audio", "53424E4B", 0, "Nitro sound bank"),
    Signature("SWAR", "audio", "53574152", 0, "Nitro wave archive"),
    Signature("SWAV", "audio", "53574156", 0, "Nitro wave"),

    # Nintendo DS / Nitro 2D
    Signature("NCGR", "texture", "5247434E", 0, "Nitro character graphics (RGCN)"),
    Signature("NCLR", "palette", "524C434E", 0, "Nitro color palette (RLCN)"),
    Signature("NSCR", "tilemap", "5243534E", 0, "Nitro screen/tile map (RCSN)"),
    Signature("NCER", "sprite", "5245434E", 0, "Nitro cell resource (RECN)"),
    Signature("NANR", "animation", "524E414E", 0, "Nitro animation resource (RNAN)"),

    # Common containers and images
    Signature("PNG", "image", "89504E470D0A1A0A", 0, "Portable Network Graphics"),
    Signature("JPEG", "image", "FFD8FF", 0, "JPEG image"),
    Signature("BMP", "image", "424D", 0, "Windows bitmap"),
    Signature("RIFF", "audio_or_container", "52494646", 0, "RIFF container"),
    Signature("Ogg", "audio", "4F676753", 0, "Ogg container"),
    Signature("ZIP", "archive", "504B0304", 0, "ZIP archive"),
    Signature("GZIP", "archive", "1F8B", 0, "Gzip stream"),
)


CATEGORY_BASE_SCORE = {
    "model": 100,
    "texture": 95,
    "animation": 85,
    "palette": 80,
    "tilemap": 80,
    "sprite": 80,
    "image": 75,
    "archive": 65,
    "audio": 55,
    "audio_or_container": 50,
    "compressed": 45,
    "unknown": 0,
}


def shannon_entropy(data: bytes) -> float:
    if not data:
        return 0.0

    counts = Counter(data)
    length = len(data)
    return -sum(
        (count / length) * math.log2(count / length)
        for count in counts.values()
    )


def printable_ascii(data: bytes, limit: int = 16) -> str:
    output = []
    for value in data[:limit]:
        if 32 <= value <= 126:
            output.append(chr(value))
        else:
            output.append(".")
    return "".join(output)


def detect_nitro_compression(prefix: bytes) -> tuple[bool, str]:
    if not prefix:
        return False, ""

    compression_type = prefix[0]
    names = {
        0x10: "LZ77",
        0x20: "Huffman",
        0x30: "RLE",
    }
    if compression_type in names and len(prefix) >= 4:
        decompressed_size = int.from_bytes(prefix[1:4], "little")
        return True, f"{names[compression_type]} (expected {decompressed_size} bytes)"

    return False, ""


def detect_signature(prefix: bytes) -> Signature | None:
    for signature in SIGNATURES:
        magic = bytes.fromhex(signature.magic_hex)
        start = signature.offset
        end = start + len(magic)
        if len(prefix) >= end and prefix[start:end] == magic:
            return signature
    return None


def normalize_extension(path: str) -> str:
    suffix = Path(path).suffix.lower()
    return suffix if suffix else "(none)"


def score_asset(
    category: str,
    size: int,
    detected: bool,
    compressed: bool,
) -> int:
    score = CATEGORY_BASE_SCORE.get(category, 0)

    if detected:
        score += 10

    if size >= 1024:
        score += 2
    if size >= 64 * 1024:
        score += 3
    if size >= 1024 * 1024:
        score += 2

    if compressed and category == "unknown":
        score = CATEGORY_BASE_SCORE["compressed"]

    return score


def classify_file(
    extraction_root: Path,
    manifest_entry: dict[str, Any],
) -> AssetRecord:
    relative_path = str(manifest_entry["path"])
    disk_path = extraction_root / Path(relative_path)

    if not disk_path.is_file():
        raise FileNotFoundError(f"No existe el archivo del manifiesto: {disk_path}")

    with disk_path.open("rb") as stream:
        prefix = stream.read(READ_PREFIX_SIZE)
        stream.seek(0)
        entropy_sample = stream.read(ENTROPY_SAMPLE_SIZE)

    signature = detect_signature(prefix)
    compressed, compression_reason = detect_nitro_compression(prefix)

    if signature:
        detected_format = signature.name
        category = signature.category
        reason = signature.description
    elif compressed:
        detected_format = compression_reason.split(" ", 1)[0]
        category = "compressed"
        reason = compression_reason
    else:
        detected_format = "unknown"
        category = "unknown"
        reason = "No known signature detected"

    size = int(manifest_entry.get("size", disk_path.stat().st_size))
    score = score_asset(
        category=category,
        size=size,
        detected=signature is not None,
        compressed=compressed,
    )

    return AssetRecord(
        file_id=int(manifest_entry.get("file_id", -1)),
        path=relative_path.replace("\\", "/"),
        extension=normalize_extension(relative_path),
        size=size,
        sha256=str(manifest_entry.get("sha256", "")),
        magic_ascii=printable_ascii(prefix),
        magic_hex=prefix[:16].hex().upper(),
        detected_format=detected_format,
        category=category,
        compressed=compressed,
        entropy=round(shannon_entropy(entropy_sample), 4),
        score=score,
        reason=reason,
    )


def load_manifest(extraction_root: Path) -> dict[str, Any]:
    manifest_path = extraction_root / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"No existe: {manifest_path}")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"JSON inválido en {manifest_path}: {exc}") from exc

    files = manifest.get("files")
    if not isinstance(files, list):
        raise ValueError("manifest.json debe contener una lista llamada 'files'.")

    return manifest


def write_csv(path: Path, records: Iterable[AssetRecord]) -> None:
    records = list(records)
    path.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = list(AssetRecord.__dataclass_fields__.keys())
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow(asdict(record))


def build_summary(records: list[AssetRecord]) -> dict[str, Any]:
    by_format = Counter(record.detected_format for record in records)
    by_category = Counter(record.category for record in records)
    by_extension = Counter(record.extension for record in records)

    total_size = sum(record.size for record in records)
    known_count = sum(record.detected_format != "unknown" for record in records)
    compressed_count = sum(record.compressed for record in records)

    top_candidates = sorted(
        records,
        key=lambda record: (-record.score, -record.size, record.path),
    )[:50]

    return {
        "schema_version": 1,
        "totals": {
            "files": len(records),
            "bytes": total_size,
            "recognized_files": known_count,
            "unknown_files": len(records) - known_count,
            "compressed_files": compressed_count,
        },
        "by_format": dict(by_format.most_common()),
        "by_category": dict(by_category.most_common()),
        "by_extension": dict(by_extension.most_common()),
        "top_candidates": [asdict(record) for record in top_candidates],
    }


def markdown_table(rows: list[tuple[str, int]], limit: int = 20) -> str:
    if not rows:
        return "_No entries found._\n"

    lines = [
        "| Value | Files |",
        "|---|---:|",
    ]
    for value, count in rows[:limit]:
        lines.append(f"| `{value}` | {count} |")
    return "\n".join(lines) + "\n"


def write_markdown_report(
    path: Path,
    extraction_root: Path,
    summary: dict[str, Any],
) -> None:
    totals = summary["totals"]
    candidates = summary["top_candidates"][:20]

    lines = [
        "# Extracted asset analysis",
        "",
        f"Source: `{extraction_root}`",
        "",
        "## Totals",
        "",
        f"- Files: **{totals['files']}**",
        f"- Total size: **{totals['bytes']} bytes**",
        f"- Recognized signatures: **{totals['recognized_files']}**",
        f"- Unknown signatures: **{totals['unknown_files']}**",
        f"- Nitro-compressed candidates: **{totals['compressed_files']}**",
        "",
        "## Formats",
        "",
        markdown_table(list(summary["by_format"].items())),
        "## Categories",
        "",
        markdown_table(list(summary["by_category"].items())),
        "## Extensions",
        "",
        markdown_table(list(summary["by_extension"].items())),
        "## Recommended first candidates",
        "",
        "| Score | Category | Format | Size | Path |",
        "|---:|---|---|---:|---|",
    ]

    for candidate in candidates:
        lines.append(
            f"| {candidate['score']} "
            f"| `{candidate['category']}` "
            f"| `{candidate['detected_format']}` "
            f"| {candidate['size']} "
            f"| `{candidate['path']}` |"
        )

    lines.extend(
        [
            "",
            "## Suggested next milestone",
            "",
            "Prefer a small, self-contained recognized resource:",
            "",
            "1. `NSBTX` or `NCGR` for a first texture viewer.",
            "2. `NCLR` together with `NCGR` when a palette is required.",
            "3. `NSBMD` after texture decoding is working.",
            "4. `NARC` only after the contained-file structure is understood.",
            "",
            "Do not commit extracted files or this generated report if it contains "
            "local game paths you do not want to publish.",
            "",
        ]
    )

    path.write_text("\n".join(lines), encoding="utf-8")


def analyze(
    extraction_root: Path,
    output_dir: Path | None,
) -> Path:
    extraction_root = extraction_root.resolve()
    manifest = load_manifest(extraction_root)

    records = [
        classify_file(extraction_root, entry)
        for entry in manifest["files"]
    ]

    summary = build_summary(records)

    if output_dir is None:
        output_dir = extraction_root / "analysis"
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    inventory_path = output_dir / "asset_inventory.csv"
    summary_path = output_dir / "summary.json"
    report_path = output_dir / "report.md"

    write_csv(inventory_path, records)
    summary_path.write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    write_markdown_report(
        report_path,
        extraction_root,
        summary,
    )

    return output_dir


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Classify extracted Nintendo DS files by extension, magic signature, "
            "compression marker, size, and entropy."
        )
    )
    parser.add_argument(
        "extraction",
        type=Path,
        help="Carpeta que contiene manifest.json y nitrofs/",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Directorio de salida. Por defecto: <extraction>/analysis",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        output = analyze(args.extraction, args.output)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Análisis completado: {output}")
    print(f"Inventario CSV: {output / 'asset_inventory.csv'}")
    print(f"Resumen JSON:   {output / 'summary.json'}")
    print(f"Informe:        {output / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
