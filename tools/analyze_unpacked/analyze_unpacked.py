#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


MODEL_FORMATS = {"NSBMD"}
TEXTURE_FORMATS = {"NSBTX"}
ANIMATION_FORMATS = {
    "NSBCA",
    "NSBTA",
    "NSBTP",
    "NSBMA",
    "NSBVA",
}
TWO_D_FORMATS = {
    "NCLR",
    "NCGR",
    "NSCR",
    "NCER",
    "NANR",
}


@dataclass(frozen=True)
class ContainerSummary:
    source_path: str
    container_magic: str
    container_size: int
    entry_count: int
    known_entry_count: int
    unknown_entry_count: int
    formats: dict[str, int]
    has_model: bool
    has_texture: bool
    has_animation: bool
    has_2d: bool
    model_texture_pair: bool
    candidate_score: int
    recommendation: str


def load_manifest(unpacked_root: Path) -> dict[str, Any]:
    path = unpacked_root / "unpacked_manifest.json"
    if not path.is_file():
        raise FileNotFoundError(f"Does not exist: {path}")

    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {path}: {exc}") from exc

    containers = payload.get("containers")
    if not isinstance(containers, list):
        raise ValueError(
            "unpacked_manifest.json must contain a list named 'containers'."
        )
    return payload


def flatten_entries(container: dict[str, Any]) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []

    for slot in container.get("slots", []):
        if not isinstance(slot, dict):
            continue
        slot_entries = slot.get("entries", [])
        if isinstance(slot_entries, list):
            entries.extend(
                entry for entry in slot_entries if isinstance(entry, dict)
            )

    return entries


def score_candidate(
    source_path: str,
    formats: Counter[str],
    entry_count: int,
    container_size: int,
) -> tuple[int, str]:
    has_model = formats["NSBMD"] > 0
    has_texture = formats["NSBTX"] > 0
    has_animation = any(formats[name] > 0 for name in ANIMATION_FORMATS)

    score = 0
    reasons: list[str] = []

    if has_model and has_texture:
        score += 100
        reasons.append("model and textures in the same container")
    elif has_model:
        score += 65
        reasons.append("contains a model")
    elif has_texture:
        score += 40
        reasons.append("contains textures")

    if has_animation:
        score += 10
        reasons.append("includes animations")

    lowered = source_path.lower()
    if "/mi/ch/" in f"/{lowered}" or lowered.startswith("mi/ch/"):
        score += 20
        reasons.append("character path mi/ch")
    if "/ba/ch/" in f"/{lowered}" or lowered.startswith("ba/ch/"):
        score += 8
        reasons.append("battle path ba/ch")

    # Prefer manageable packages for a first viewer.
    if 1 <= entry_count <= 8:
        score += 12
        reasons.append("few entries")
    elif entry_count <= 32:
        score += 6

    if 1_024 <= container_size <= 2_000_000:
        score += 5

    recommendation = ", ".join(reasons) if reasons else "no special priority"
    return score, recommendation


def summarize_container(container: dict[str, Any]) -> ContainerSummary:
    entries = flatten_entries(container)
    formats = Counter(
        str(entry.get("detected_format", "unknown"))
        for entry in entries
    )

    known_count = sum(
        count for name, count in formats.items() if name != "unknown"
    )
    unknown_count = formats.get("unknown", 0)

    source_path = str(container.get("source_path", ""))
    container_size = int(container.get("container_size", 0))
    score, recommendation = score_candidate(
        source_path=source_path,
        formats=formats,
        entry_count=len(entries),
        container_size=container_size,
    )

    has_model = any(formats[name] > 0 for name in MODEL_FORMATS)
    has_texture = any(formats[name] > 0 for name in TEXTURE_FORMATS)
    has_animation = any(formats[name] > 0 for name in ANIMATION_FORMATS)
    has_2d = any(formats[name] > 0 for name in TWO_D_FORMATS)

    return ContainerSummary(
        source_path=source_path,
        container_magic=str(container.get("container_magic", "")),
        container_size=container_size,
        entry_count=len(entries),
        known_entry_count=known_count,
        unknown_entry_count=unknown_count,
        formats=dict(sorted(formats.items())),
        has_model=has_model,
        has_texture=has_texture,
        has_animation=has_animation,
        has_2d=has_2d,
        model_texture_pair=has_model and has_texture,
        candidate_score=score,
        recommendation=recommendation,
    )


def write_csv(path: Path, records: list[ContainerSummary]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[
                "source_path",
                "container_magic",
                "container_size",
                "entry_count",
                "known_entry_count",
                "unknown_entry_count",
                "formats",
                "has_model",
                "has_texture",
                "has_animation",
                "has_2d",
                "model_texture_pair",
                "candidate_score",
                "recommendation",
            ],
        )
        writer.writeheader()

        for record in records:
            row = asdict(record)
            row["formats"] = json.dumps(
                record.formats,
                ensure_ascii=False,
                sort_keys=True,
            )
            writer.writerow(row)


def write_markdown(
    path: Path,
    records: list[ContainerSummary],
    totals: dict[str, Any],
) -> None:
    candidates = sorted(
        records,
        key=lambda record: (
            -record.candidate_score,
            record.entry_count,
            record.source_path,
        ),
    )

    lines = [
        "# Unpacked resource analysis",
        "",
        "## Totals",
        "",
        f"- Containers: **{totals['containers']}**",
        f"- Embedded entries: **{totals['entries']}**",
        f"- Known formats: **{totals['known_entries']}**",
        f"- Unknown entries: **{totals['unknown_entries']}**",
        f"- Model containers: **{totals['model_containers']}**",
        f"- Texture containers: **{totals['texture_containers']}**",
        f"- Model + texture pairs: **{totals['model_texture_pairs']}**",
        f"- Animation containers: **{totals['animation_containers']}**",
        "",
        "## Formats",
        "",
        "| Format | Entries |",
        "|---|---:|",
    ]

    for name, count in totals["formats"].items():
        lines.append(f"| `{name}` | {count} |")

    lines.extend(
        [
            "",
            "## Best 3D candidates",
            "",
            "| Score | Path | Entries | Formats | Reason |",
            "|---:|---|---:|---|---|",
        ]
    )

    shown = 0
    for record in candidates:
        if not (record.has_model or record.has_texture):
            continue

        formats_text = ", ".join(
            f"{name}×{count}"
            for name, count in record.formats.items()
        )
        lines.append(
            f"| {record.candidate_score} "
            f"| `{record.source_path}` "
            f"| {record.entry_count} "
            f"| `{formats_text}` "
            f"| {record.recommendation} |"
        )
        shown += 1
        if shown >= 50:
            break

    if shown == 0:
        lines.append("|  | _No model or texture containers found_ |  |  |  |")

    lines.extend(
        [
            "",
            "## Recommended next milestone",
            "",
            "Choose one small container that includes both `NSBMD` and `NSBTX`.",
            "Use it as the first native 3D viewer target before adding animation.",
            "",
            "A good first target should have:",
            "",
            "- one model;",
            "- one texture archive;",
            "- few embedded entries;",
            "- no dependency on external animation packages;",
            "- preferably a path under `mi/ch/` or a simple object package.",
            "",
        ]
    )

    path.write_text("\n".join(lines), encoding="utf-8")


def analyze(unpacked_root: Path, output_dir: Path | None) -> Path:
    unpacked_root = unpacked_root.resolve()
    payload = load_manifest(unpacked_root)

    records = [
        summarize_container(container)
        for container in payload["containers"]
    ]

    format_counts: Counter[str] = Counter()
    for record in records:
        format_counts.update(record.formats)

    totals = {
        "containers": len(records),
        "entries": sum(record.entry_count for record in records),
        "known_entries": sum(record.known_entry_count for record in records),
        "unknown_entries": sum(record.unknown_entry_count for record in records),
        "model_containers": sum(record.has_model for record in records),
        "texture_containers": sum(record.has_texture for record in records),
        "model_texture_pairs": sum(
            record.model_texture_pair for record in records
        ),
        "animation_containers": sum(
            record.has_animation for record in records
        ),
        "formats": dict(format_counts.most_common()),
    }

    if output_dir is None:
        output_dir = unpacked_root / "analysis"
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    summary = {
        "schema_version": 1,
        "source": str(unpacked_root),
        "totals": totals,
        "containers": [asdict(record) for record in records],
        "best_candidates": [
            asdict(record)
            for record in sorted(
                records,
                key=lambda record: (
                    -record.candidate_score,
                    record.entry_count,
                    record.source_path,
                ),
            )
            if record.has_model or record.has_texture
        ][:100],
    }

    (output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    write_csv(output_dir / "containers.csv", records)
    write_markdown(output_dir / "report.md", records, totals)

    return output_dir


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Analyze unpacked KAPH/D2KP resources and rank the best "
            "NSBMD/NSBTX candidates for a first 3D viewer."
        )
    )
    parser.add_argument(
        "unpacked",
        type=Path,
        help="Directory containing unpacked_manifest.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output directory. Default: <unpacked>/analysis",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        output = analyze(args.unpacked, args.output)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Analysis: {output}")
    print(f"Report:   {output / 'report.md'}")
    print(f"Summary:  {output / 'summary.json'}")
    print(f"CSV:      {output / 'containers.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
