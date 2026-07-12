#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from typing import Any


MAX_OUTPUT_SIZE = 512 * 1024 * 1024


@dataclass(frozen=True)
class DecompressedRecord:
    source_path: str
    output_path: str
    compression: str
    compressed_size: int
    decompressed_size: int
    sha256: str
    inner_magic_ascii: str
    inner_magic_hex: str


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def printable_ascii(data: bytes, limit: int = 16) -> str:
    return "".join(chr(value) if 32 <= value <= 126 else "." for value in data[:limit])


def expected_size(data: bytes) -> int:
    if len(data) < 4:
        raise ValueError("Compressed stream is smaller than the 4-byte Nitro header.")

    size = int.from_bytes(data[1:4], "little")
    if size == 0:
        if len(data) < 8:
            raise ValueError("Extended-size Nitro stream is truncated.")
        size = int.from_bytes(data[4:8], "little")

    if size <= 0:
        raise ValueError("Decompressed size is zero.")
    if size > MAX_OUTPUT_SIZE:
        raise ValueError(
            f"Refusing to allocate {size} bytes; maximum is {MAX_OUTPUT_SIZE}."
        )
    return size


def decompress_lz10(data: bytes) -> bytes:
    if not data or data[0] != 0x10:
        raise ValueError("Not a Nintendo DS LZ10 stream.")

    output_size = expected_size(data)
    cursor = 4
    output = bytearray()

    while len(output) < output_size:
        if cursor >= len(data):
            raise ValueError("LZ10 stream ended before the output was complete.")

        flags = data[cursor]
        cursor += 1

        for bit in range(7, -1, -1):
            if len(output) >= output_size:
                break

            compressed = (flags >> bit) & 1
            if not compressed:
                if cursor >= len(data):
                    raise ValueError("LZ10 literal byte is truncated.")
                output.append(data[cursor])
                cursor += 1
                continue

            if cursor + 1 >= len(data):
                raise ValueError("LZ10 back-reference is truncated.")

            first = data[cursor]
            second = data[cursor + 1]
            cursor += 2

            length = (first >> 4) + 3
            displacement = (((first & 0x0F) << 8) | second) + 1

            if displacement > len(output):
                raise ValueError(
                    f"Invalid LZ10 displacement {displacement} at output offset "
                    f"{len(output)}."
                )

            for _ in range(length):
                if len(output) >= output_size:
                    break
                output.append(output[-displacement])

    return bytes(output)


def decompress_lz11(data: bytes) -> bytes:
    if not data or data[0] != 0x11:
        raise ValueError("Not a Nintendo DS LZ11 stream.")

    output_size = expected_size(data)
    cursor = 4 if int.from_bytes(data[1:4], "little") else 8
    output = bytearray()

    while len(output) < output_size:
        if cursor >= len(data):
            raise ValueError("LZ11 stream ended before the output was complete.")

        flags = data[cursor]
        cursor += 1

        for bit in range(7, -1, -1):
            if len(output) >= output_size:
                break

            compressed = (flags >> bit) & 1
            if not compressed:
                if cursor >= len(data):
                    raise ValueError("LZ11 literal byte is truncated.")
                output.append(data[cursor])
                cursor += 1
                continue

            if cursor >= len(data):
                raise ValueError("LZ11 back-reference is truncated.")

            first = data[cursor]
            high_nibble = first >> 4

            if high_nibble == 0:
                if cursor + 2 >= len(data):
                    raise ValueError("LZ11 long back-reference is truncated.")
                second = data[cursor + 1]
                third = data[cursor + 2]
                length = (((first & 0x0F) << 4) | (second >> 4)) + 0x11
                displacement = (((second & 0x0F) << 8) | third) + 1
                cursor += 3
            elif high_nibble == 1:
                if cursor + 3 >= len(data):
                    raise ValueError("LZ11 very-long back-reference is truncated.")
                second = data[cursor + 1]
                third = data[cursor + 2]
                fourth = data[cursor + 3]
                length = (
                    ((first & 0x0F) << 12)
                    | (second << 4)
                    | (third >> 4)
                ) + 0x111
                displacement = (((third & 0x0F) << 8) | fourth) + 1
                cursor += 4
            else:
                if cursor + 1 >= len(data):
                    raise ValueError("LZ11 short back-reference is truncated.")
                second = data[cursor + 1]
                length = high_nibble + 1
                displacement = (((first & 0x0F) << 8) | second) + 1
                cursor += 2

            if displacement > len(output):
                raise ValueError(
                    f"Invalid LZ11 displacement {displacement} at output offset "
                    f"{len(output)}."
                )

            for _ in range(length):
                if len(output) >= output_size:
                    break
                output.append(output[-displacement])

    return bytes(output)


def decompress_nitro(data: bytes) -> tuple[str, bytes]:
    if not data:
        raise ValueError("Empty input.")

    if data[0] == 0x10:
        return "LZ10", decompress_lz10(data)
    if data[0] == 0x11:
        return "LZ11", decompress_lz11(data)

    raise ValueError(f"Unsupported compression type 0x{data[0]:02X}.")


def load_manifest(extraction_root: Path) -> list[dict[str, Any]]:
    manifest_path = extraction_root / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Missing manifest: {manifest_path}")

    try:
        payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {manifest_path}: {exc}") from exc

    files = payload.get("files")
    if not isinstance(files, list):
        raise ValueError("manifest.json must contain a 'files' list.")
    return files


def output_relative_path(source_path: str) -> PurePosixPath:
    path = PurePosixPath(source_path)
    if path.parts and path.parts[0] == "nitrofs":
        path = PurePosixPath(*path.parts[1:])

    if path.suffix.lower() == ".z":
        path = path.with_suffix("")
    else:
        path = PurePosixPath(f"{path.as_posix()}.decompressed")

    return path


def decompress_extraction(
    extraction_root: Path,
    output_root: Path | None = None,
    force: bool = False,
) -> tuple[Path, list[DecompressedRecord], list[dict[str, str]]]:
    extraction_root = extraction_root.resolve()
    files = load_manifest(extraction_root)

    if output_root is None:
        output_root = extraction_root / "decompressed"
    output_root = output_root.resolve()

    if output_root.exists() and not force:
        raise FileExistsError(
            f"Output directory already exists: {output_root}. Use --force to replace it."
        )
    if output_root.exists():
        import shutil
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)

    records: list[DecompressedRecord] = []
    failures: list[dict[str, str]] = []

    for entry in files:
        source_path = str(entry.get("path", ""))
        if not source_path:
            continue

        source_file = extraction_root / Path(source_path)
        if not source_file.is_file():
            failures.append({"path": source_path, "error": "Source file is missing"})
            continue

        prefix = source_file.read_bytes()[:1]
        if prefix not in (b"\x10", b"\x11"):
            continue

        try:
            compressed_data = source_file.read_bytes()
            compression, decompressed_data = decompress_nitro(compressed_data)

            relative_output = output_relative_path(source_path)
            destination = output_root.joinpath(*relative_output.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(decompressed_data)

            records.append(
                DecompressedRecord(
                    source_path=source_path.replace("\\", "/"),
                    output_path=relative_output.as_posix(),
                    compression=compression,
                    compressed_size=len(compressed_data),
                    decompressed_size=len(decompressed_data),
                    sha256=sha256_bytes(decompressed_data),
                    inner_magic_ascii=printable_ascii(decompressed_data),
                    inner_magic_hex=decompressed_data[:16].hex().upper(),
                )
            )
        except (OSError, ValueError) as exc:
            failures.append({"path": source_path, "error": str(exc)})

    manifest = {
        "schema_version": 1,
        "source_extraction": str(extraction_root),
        "decompressed_count": len(records),
        "failure_count": len(failures),
        "files": [asdict(record) for record in records],
        "failures": failures,
    }
    (output_root / "decompressed_manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    return output_root, records, failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Decompress Nintendo DS LZ10/LZ11 files from an extracted ROM."
    )
    parser.add_argument(
        "extraction",
        type=Path,
        help="Extraction directory containing manifest.json and nitrofs/",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output directory. Default: <extraction>/decompressed",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace an existing decompressed output directory.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        output, records, failures = decompress_extraction(
            extraction_root=args.extraction,
            output_root=args.output,
            force=args.force,
        )
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Output:       {output}")
    print(f"Decompressed: {len(records)}")
    print(f"Failures:     {len(failures)}")
    print(f"Manifest:     {output / 'decompressed_manifest.json'}")

    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
