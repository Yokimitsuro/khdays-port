#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from typing import Any


SUPPORTED_MAGICS = (b"KAPH", b"D2KP")
SLOT_POINTER_OFFSET = 0x08
SLOT_COUNT = 8
INVALID_POINTER = 0xFFFFFFFF
MAX_ENTRIES_PER_SLOT = 100_000


FORMAT_BY_MAGIC: dict[bytes, tuple[str, str, str]] = {
    b"BMD0": ("NSBMD", ".nsbmd", "model"),
    b"BTX0": ("NSBTX", ".nsbtx", "texture"),
    b"BCA0": ("NSBCA", ".nsbca", "skeletal_animation"),
    b"BTA0": ("NSBTA", ".nsbta", "texture_animation"),
    b"BTP0": ("NSBTP", ".nsbtp", "texture_pattern_animation"),
    b"BMA0": ("NSBMA", ".nsbma", "material_animation"),
    b"BVA0": ("NSBVA", ".nsbva", "visibility_animation"),
    b"RGCN": ("NCGR", ".ncgr", "graphics_2d"),
    b"RLCN": ("NCLR", ".nclr", "palette"),
    b"RCSN": ("NSCR", ".nscr", "tilemap"),
    b"RECN": ("NCER", ".ncer", "sprite_cells"),
    b"RNAN": ("NANR", ".nanr", "sprite_animation"),
    b"NARC": ("NARC", ".narc", "archive"),
    b"SDAT": ("SDAT", ".sdat", "audio_archive"),
}


@dataclass(frozen=True)
class SlotEntry:
    slot: int
    index: int
    offset: int
    size: int
    end: int
    magic_ascii: str
    magic_hex: str
    detected_format: str
    category: str
    output_path: str
    sha256: str
    nitro_declared_size: int | None
    size_matches_nitro_header: bool | None


@dataclass(frozen=True)
class SlotInfo:
    slot: int
    table_offset: int
    entry_count: int
    table_end: int
    entries: list[dict[str, Any]]


@dataclass(frozen=True)
class ContainerInfo:
    source_path: str
    container_magic: str
    container_size: int
    slot_pointers: list[int | None]
    slots: list[dict[str, Any]]
    extracted_entry_count: int


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_u32(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(
            f"{label}: cannot read u32 at 0x{offset:X}; "
            f"container size is 0x{len(data):X}."
        )
    return struct.unpack_from("<I", data, offset)[0]


def printable_magic(data: bytes) -> str:
    return "".join(chr(value) if 32 <= value <= 126 else "." for value in data[:4])


def detect_format(data: bytes) -> tuple[str, str, str]:
    if len(data) < 4:
        return "unknown", ".bin", "unknown"
    return FORMAT_BY_MAGIC.get(data[:4], ("unknown", ".bin", "unknown"))


def read_nitro_declared_size(data: bytes) -> int | None:
    # Standard Nitro files store their total file size at offset 0x08.
    if len(data) < 0x0C or data[:4] not in FORMAT_BY_MAGIC:
        return None
    return struct.unpack_from("<I", data, 0x08)[0]


def parse_slot_table(
    data: bytes,
    slot: int,
    table_offset: int,
) -> tuple[int, list[tuple[int, int]], int]:
    if table_offset % 4 != 0:
        raise ValueError(
            f"Slot {slot}: table offset 0x{table_offset:X} is not 4-byte aligned."
        )
    if table_offset < 0x28 or table_offset + 4 > len(data):
        raise ValueError(
            f"Slot {slot}: table offset 0x{table_offset:X} is outside the container."
        )

    count = read_u32(data, table_offset, f"Slot {slot} count")
    if count > MAX_ENTRIES_PER_SLOT:
        raise ValueError(
            f"Slot {slot}: unreasonable entry count {count} "
            f"at 0x{table_offset:X}."
        )

    offsets_start = table_offset + 4
    sizes_start = offsets_start + count * 4
    table_end = sizes_start + count * 4

    if table_end > len(data):
        raise ValueError(
            f"Slot {slot}: table requires 0x{table_end:X} bytes, "
            f"container size is 0x{len(data):X}."
        )

    entries: list[tuple[int, int]] = []
    for index in range(count):
        item_offset = read_u32(
            data,
            offsets_start + index * 4,
            f"Slot {slot} entry {index} offset",
        )
        item_size = read_u32(
            data,
            sizes_start + index * 4,
            f"Slot {slot} entry {index} size",
        )

        item_end = item_offset + item_size
        if item_offset < table_end:
            raise ValueError(
                f"Slot {slot} entry {index}: data offset 0x{item_offset:X} "
                f"overlaps its table ending at 0x{table_end:X}."
            )
        if item_end < item_offset or item_end > len(data):
            raise ValueError(
                f"Slot {slot} entry {index}: range "
                f"0x{item_offset:X}-0x{item_end:X} exceeds "
                f"container size 0x{len(data):X}."
            )

        entries.append((item_offset, item_size))

    return count, entries, table_end


def slot_pointer_values(data: bytes) -> list[int | None]:
    if len(data) < 0x28:
        raise ValueError("Container is smaller than the 0x28-byte header.")

    values: list[int | None] = []
    for slot in range(SLOT_COUNT):
        value = read_u32(
            data,
            SLOT_POINTER_OFFSET + slot * 4,
            f"Slot {slot} pointer",
        )
        values.append(None if value in (0, INVALID_POINTER) else value)
    return values


def safe_source_directory(relative_path: str) -> PurePosixPath:
    source = PurePosixPath(relative_path)
    safe_parts = [
        part.replace(":", "_").replace("\\", "_")
        for part in source.parts
        if part not in ("", ".", "..")
    ]
    if not safe_parts:
        return PurePosixPath("unnamed_container")
    return PurePosixPath(*safe_parts)


def unpack_container(
    container_path: Path,
    relative_path: str,
    output_root: Path,
) -> ContainerInfo:
    data = container_path.read_bytes()
    if len(data) < 0x28:
        raise ValueError(f"Container is too small: {container_path}")

    magic = data[:4]
    if magic not in SUPPORTED_MAGICS:
        raise ValueError(
            f"Unsupported container magic {printable_magic(magic)!r}: {container_path}"
        )

    pointers = slot_pointer_values(data)
    container_output = output_root.joinpath(
        *safe_source_directory(relative_path).parts
    )
    container_output.mkdir(parents=True, exist_ok=True)

    slot_infos: list[SlotInfo] = []
    extracted_count = 0

    for slot, table_offset in enumerate(pointers):
        if table_offset is None:
            continue

        count, raw_entries, table_end = parse_slot_table(
            data=data,
            slot=slot,
            table_offset=table_offset,
        )

        entry_records: list[SlotEntry] = []
        slot_output = container_output / f"slot_{slot}"

        for index, (item_offset, item_size) in enumerate(raw_entries):
            item_data = data[item_offset:item_offset + item_size]
            detected_format, extension, category = detect_format(item_data)
            output_name = f"{index:04d}{extension}"
            output_path = slot_output / output_name
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_bytes(item_data)

            declared_size = read_nitro_declared_size(item_data)
            size_match = (
                None if declared_size is None else declared_size == item_size
            )

            relative_output = output_path.relative_to(output_root).as_posix()
            entry_records.append(
                SlotEntry(
                    slot=slot,
                    index=index,
                    offset=item_offset,
                    size=item_size,
                    end=item_offset + item_size,
                    magic_ascii=printable_magic(item_data),
                    magic_hex=item_data[:4].hex().upper(),
                    detected_format=detected_format,
                    category=category,
                    output_path=relative_output,
                    sha256=sha256_bytes(item_data),
                    nitro_declared_size=declared_size,
                    size_matches_nitro_header=size_match,
                )
            )
            extracted_count += 1

        slot_info = SlotInfo(
            slot=slot,
            table_offset=table_offset,
            entry_count=count,
            table_end=table_end,
            entries=[asdict(record) for record in entry_records],
        )
        slot_infos.append(slot_info)

    container_info = ContainerInfo(
        source_path=relative_path.replace("\\", "/"),
        container_magic=magic.decode("ascii"),
        container_size=len(data),
        slot_pointers=pointers,
        slots=[asdict(slot) for slot in slot_infos],
        extracted_entry_count=extracted_count,
    )

    (container_output / "container.json").write_text(
        json.dumps(asdict(container_info), indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    return container_info


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


def is_supported_entry(entry: dict[str, Any]) -> bool:
    magic_ascii = str(entry.get("inner_magic_ascii", ""))
    return magic_ascii.startswith("KAPH") or magic_ascii.startswith("D2KP")


def matches_filters(path: str, filters: list[str]) -> bool:
    if not filters:
        return True
    lowered = path.lower()
    return any(token.lower() in lowered for token in filters)


def unpack_all(
    decompressed_root: Path,
    output_root: Path | None,
    filters: list[str],
    force: bool,
) -> tuple[Path, list[ContainerInfo], list[dict[str, str]]]:
    decompressed_root = decompressed_root.resolve()
    entries = load_decompressed_manifest(decompressed_root)

    if output_root is None:
        output_root = decompressed_root / "unpacked"
    output_root = output_root.resolve()

    if output_root.exists():
        if not force:
            raise FileExistsError(
                f"Output directory already exists: {output_root}. "
                "Use --force to replace it."
            )
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)

    containers: list[ContainerInfo] = []
    failures: list[dict[str, str]] = []

    for entry in entries:
        if not is_supported_entry(entry):
            continue

        relative_path = str(entry.get("output_path", ""))
        if not relative_path or not matches_filters(relative_path, filters):
            continue

        source_path = decompressed_root / Path(relative_path)
        try:
            containers.append(
                unpack_container(
                    container_path=source_path,
                    relative_path=relative_path,
                    output_root=output_root,
                )
            )
        except (OSError, ValueError) as exc:
            failures.append({"path": relative_path, "error": str(exc)})

    manifest = {
        "schema_version": 1,
        "source": str(decompressed_root),
        "filters": filters,
        "container_count": len(containers),
        "entry_count": sum(
            container.extracted_entry_count for container in containers
        ),
        "failure_count": len(failures),
        "containers": [asdict(container) for container in containers],
        "failures": failures,
    }

    (output_root / "unpacked_manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    return output_root, containers, failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Extract embedded files from KH Days KAPH and D2KP containers."
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
        help="Output directory. Default: <decompressed>/unpacked",
    )
    parser.add_argument(
        "--path",
        action="append",
        default=[],
        help=(
            "Only unpack output paths containing this text. "
            "May be supplied multiple times."
        ),
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace an existing unpacked output directory.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        output, containers, failures = unpack_all(
            decompressed_root=args.decompressed,
            output_root=args.output,
            filters=args.path,
            force=args.force,
        )
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    entry_count = sum(container.extracted_entry_count for container in containers)

    print(f"Output:     {output}")
    print(f"Containers:{len(containers):>6}")
    print(f"Entries:   {entry_count:>6}")
    print(f"Failures:  {len(failures):>6}")
    print(f"Manifest:  {output / 'unpacked_manifest.json'}")

    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
