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
from typing import Any, BinaryIO


HEADER_MIN_SIZE = 0x200
ROOT_DIRECTORY_ID = 0xF000


@dataclass(frozen=True)
class RomHeader:
    title: str
    game_code: str
    maker_code: str
    unit_code: int
    rom_version: int
    arm9_offset: int
    arm9_size: int
    arm7_offset: int
    arm7_size: int
    fnt_offset: int
    fnt_size: int
    fat_offset: int
    fat_size: int
    arm9_overlay_offset: int
    arm9_overlay_size: int
    arm7_overlay_offset: int
    arm7_overlay_size: int


@dataclass(frozen=True)
class FatEntry:
    file_id: int
    start: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.start


@dataclass(frozen=True)
class FileRecord:
    file_id: int
    path: str
    start: int
    end: int
    size: int
    sha256: str


def decode_ascii(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace").strip()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_exact(stream: BinaryIO, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0:
        raise ValueError(f"{label}: negative offset or size.")

    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise ValueError(
            f"{label}: expected {size} bytes at 0x{offset:X}, "
            f"but only {len(data)} could be read."
        )
    return data


def unpack_u32(header: bytes, offset: int) -> int:
    return struct.unpack_from("<I", header, offset)[0]


def read_header(stream: BinaryIO, rom_size: int) -> RomHeader:
    header = read_exact(stream, 0, HEADER_MIN_SIZE, "NDS header")

    parsed = RomHeader(
        title=decode_ascii(header[0x00:0x0C]),
        game_code=decode_ascii(header[0x0C:0x10]),
        maker_code=decode_ascii(header[0x10:0x12]),
        unit_code=header[0x12],
        rom_version=header[0x1E],
        arm9_offset=unpack_u32(header, 0x20),
        arm9_size=unpack_u32(header, 0x2C),
        arm7_offset=unpack_u32(header, 0x30),
        arm7_size=unpack_u32(header, 0x3C),
        fnt_offset=unpack_u32(header, 0x40),
        fnt_size=unpack_u32(header, 0x44),
        fat_offset=unpack_u32(header, 0x48),
        fat_size=unpack_u32(header, 0x4C),
        arm9_overlay_offset=unpack_u32(header, 0x50),
        arm9_overlay_size=unpack_u32(header, 0x54),
        arm7_overlay_offset=unpack_u32(header, 0x58),
        arm7_overlay_size=unpack_u32(header, 0x5C),
    )

    ranges = {
        "ARM9": (parsed.arm9_offset, parsed.arm9_size),
        "ARM7": (parsed.arm7_offset, parsed.arm7_size),
        "FNT": (parsed.fnt_offset, parsed.fnt_size),
        "FAT": (parsed.fat_offset, parsed.fat_size),
        "ARM9 overlay table": (
            parsed.arm9_overlay_offset,
            parsed.arm9_overlay_size,
        ),
        "ARM7 overlay table": (
            parsed.arm7_overlay_offset,
            parsed.arm7_overlay_size,
        ),
    }

    for label, (offset, size) in ranges.items():
        if size == 0:
            continue
        if offset + size > rom_size:
            raise ValueError(
                f"{label}: range outside the ROM "
                f"(offset=0x{offset:X}, size=0x{size:X}, rom=0x{rom_size:X})."
            )

    if parsed.fat_size % 8 != 0:
        raise ValueError("The FAT size is not a multiple of 8 bytes.")

    return parsed


def load_supported_database(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []

    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {path}: {exc}") from exc

    roms = payload.get("roms", [])
    if not isinstance(roms, list):
        raise ValueError(f"{path} must contain a list named 'roms'.")
    return roms


def find_supported_rom(
    sha256: str,
    database: list[dict[str, Any]],
) -> dict[str, Any] | None:
    wanted = sha256.lower()
    for entry in database:
        candidate = str(entry.get("sha256", "")).strip().lower()
        if candidate and candidate == wanted:
            return entry
    return None


def parse_fat(fat: bytes, rom_size: int) -> list[FatEntry]:
    entries: list[FatEntry] = []

    for file_id, offset in enumerate(range(0, len(fat), 8)):
        start, end = struct.unpack_from("<II", fat, offset)

        if end < start:
            raise ValueError(
                f"FAT[{file_id}]: end 0x{end:X} precedes start 0x{start:X}."
            )
        if end > rom_size:
            raise ValueError(
                f"FAT[{file_id}]: end 0x{end:X} outside the ROM "
                f"(0x{rom_size:X})."
            )

        entries.append(FatEntry(file_id=file_id, start=start, end=end))

    return entries


def safe_component(name: str, fallback: str) -> str:
    cleaned = name.replace("\\", "_").replace("/", "_").replace("\x00", "")
    cleaned = cleaned.strip().strip(".")
    return cleaned or fallback


def parse_fnt(fnt: bytes, fat_count: int) -> dict[int, PurePosixPath]:
    if len(fnt) < 8:
        raise ValueError("The FNT is too small.")

    root_subtable_offset, root_first_file_id, directory_count = struct.unpack_from(
        "<IHH", fnt, 0
    )

    if directory_count == 0:
        raise ValueError("The FNT declares zero directories.")
    if directory_count * 8 > len(fnt):
        raise ValueError("The main directory table exceeds the FNT.")
    if root_subtable_offset >= len(fnt):
        raise ValueError("The root subdirectory points outside the FNT.")

    main_entries: dict[int, tuple[int, int, int]] = {}
    for index in range(directory_count):
        offset = index * 8
        subtable_offset, first_file_id, parent_id = struct.unpack_from(
            "<IHH", fnt, offset
        )
        directory_id = ROOT_DIRECTORY_ID + index

        if subtable_offset >= len(fnt):
            raise ValueError(
                f"Directory 0x{directory_id:04X}: subtable outside the FNT."
            )

        main_entries[directory_id] = (
            subtable_offset,
            first_file_id,
            parent_id,
        )

    paths: dict[int, PurePosixPath] = {}
    visited: set[int] = set()

    def walk(directory_id: int, parent_path: PurePosixPath) -> None:
        if directory_id in visited:
            raise ValueError(
                f"Cycle detected in directory 0x{directory_id:04X}."
            )
        if directory_id not in main_entries:
            raise ValueError(
                f"Reference to nonexistent directory 0x{directory_id:04X}."
            )

        visited.add(directory_id)
        cursor, file_id, _parent_id = main_entries[directory_id]

        while True:
            if cursor >= len(fnt):
                raise ValueError(
                    f"Directory 0x{directory_id:04X}: subtable without terminator."
                )

            descriptor = fnt[cursor]
            cursor += 1

            if descriptor == 0:
                break

            is_directory = bool(descriptor & 0x80)
            name_length = descriptor & 0x7F

            if name_length == 0:
                raise ValueError(
                    f"Directory 0x{directory_id:04X}: zero-length name."
                )
            if cursor + name_length > len(fnt):
                raise ValueError(
                    f"Directory 0x{directory_id:04X}: name outside the FNT."
                )

            raw_name = fnt[cursor:cursor + name_length]
            cursor += name_length
            decoded_name = raw_name.decode("ascii", errors="replace")

            if is_directory:
                if cursor + 2 > len(fnt):
                    raise ValueError("Truncated subdirectory ID.")

                child_id = struct.unpack_from("<H", fnt, cursor)[0]
                cursor += 2
                child_name = safe_component(
                    decoded_name,
                    f"dir_{child_id:04X}",
                )
                walk(child_id, parent_path / child_name)
            else:
                if file_id >= fat_count:
                    raise ValueError(
                        f"The FNT references file_id {file_id}, "
                        f"but the FAT only contains {fat_count} entries."
                    )

                file_name = safe_component(
                    decoded_name,
                    f"file_{file_id:05d}.bin",
                )
                paths[file_id] = parent_path / file_name
                file_id += 1

        visited.remove(directory_id)

    walk(ROOT_DIRECTORY_ID, PurePosixPath())
    return paths


def copy_range(
    source: BinaryIO,
    destination: Path,
    start: int,
    size: int,
) -> str:
    destination.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()

    source.seek(start)
    remaining = size

    with destination.open("wb") as output:
        while remaining:
            chunk = source.read(min(1024 * 1024, remaining))
            if not chunk:
                raise ValueError(
                    f"Truncated read while extracting {destination}."
                )
            output.write(chunk)
            digest.update(chunk)
            remaining -= len(chunk)

    return digest.hexdigest()


def extract_optional_range(
    source: BinaryIO,
    destination: Path,
    offset: int,
    size: int,
) -> dict[str, Any] | None:
    if size == 0:
        return None

    digest = copy_range(source, destination, offset, size)
    return {
        "path": destination.as_posix(),
        "offset": offset,
        "size": size,
        "sha256": digest,
    }


def ensure_empty_output(path: Path, force: bool) -> None:
    if path.exists():
        if not force:
            raise FileExistsError(
                f"The output folder already exists: {path}\n"
                "Use --force to replace it."
            )
        shutil.rmtree(path)

    path.mkdir(parents=True, exist_ok=True)


def extract_rom(
    rom_path: Path,
    output_root: Path,
    database_path: Path,
    allow_unknown: bool,
    force: bool,
) -> Path:
    if not rom_path.is_file():
        raise FileNotFoundError(f"ROM does not exist: {rom_path}")

    rom_size = rom_path.stat().st_size
    rom_sha256 = sha256_file(rom_path)
    database = load_supported_database(database_path)
    matched = find_supported_rom(rom_sha256, database)

    if database and matched is None and not allow_unknown:
        raise PermissionError(
            "The ROM is not registered in supported_roms.json. "
            "Verify it first, or use --allow-unknown for development only."
        )

    output_dir = output_root / rom_sha256[:16]
    ensure_empty_output(output_dir, force)

    system_dir = output_dir / "system"
    nitrofs_dir = output_dir / "nitrofs"
    system_dir.mkdir(parents=True, exist_ok=True)
    nitrofs_dir.mkdir(parents=True, exist_ok=True)

    with rom_path.open("rb") as stream:
        header = read_header(stream, rom_size)
        fnt = read_exact(stream, header.fnt_offset, header.fnt_size, "FNT")
        fat = read_exact(stream, header.fat_offset, header.fat_size, "FAT")
        fat_entries = parse_fat(fat, rom_size)
        file_paths = parse_fnt(fnt, len(fat_entries))

        system_records: dict[str, Any] = {}

        for name, offset, size in (
            ("arm9.bin", header.arm9_offset, header.arm9_size),
            ("arm7.bin", header.arm7_offset, header.arm7_size),
            (
                "arm9_overlay_table.bin",
                header.arm9_overlay_offset,
                header.arm9_overlay_size,
            ),
            (
                "arm7_overlay_table.bin",
                header.arm7_overlay_offset,
                header.arm7_overlay_size,
            ),
        ):
            record = extract_optional_range(
                stream,
                system_dir / name,
                offset,
                size,
            )
            if record:
                record["path"] = str(Path("system") / name).replace("\\", "/")
                system_records[name] = record

        (system_dir / "fnt.bin").write_bytes(fnt)
        (system_dir / "fat.bin").write_bytes(fat)

        system_records["fnt.bin"] = {
            "path": "system/fnt.bin",
            "offset": header.fnt_offset,
            "size": len(fnt),
            "sha256": sha256_bytes(fnt),
        }
        system_records["fat.bin"] = {
            "path": "system/fat.bin",
            "offset": header.fat_offset,
            "size": len(fat),
            "sha256": sha256_bytes(fat),
        }

        file_records: list[FileRecord] = []

        for entry in fat_entries:
            relative_path = file_paths.get(
                entry.file_id,
                PurePosixPath("_unnamed") / f"file_{entry.file_id:05d}.bin",
            )

            destination = nitrofs_dir.joinpath(*relative_path.parts)
            digest = copy_range(
                stream,
                destination,
                entry.start,
                entry.size,
            )

            file_records.append(
                FileRecord(
                    file_id=entry.file_id,
                    path=(PurePosixPath("nitrofs") / relative_path).as_posix(),
                    start=entry.start,
                    end=entry.end,
                    size=entry.size,
                    sha256=digest,
                )
            )

    metadata = {
        "schema_version": 1,
        "source": {
            "filename": rom_path.name,
            "file_size": rom_size,
            "sha256": rom_sha256,
            "supported": matched is not None,
            "supported_entry": matched,
        },
        "header": asdict(header),
        "extraction": {
            "nitrofs_file_count": len(file_records),
            "named_file_count": len(file_paths),
            "unnamed_file_count": len(file_records) - len(file_paths),
        },
    }

    manifest = {
        "schema_version": 1,
        "system": system_records,
        "files": [asdict(record) for record in file_records],
    }

    (output_dir / "metadata.json").write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    return output_dir


def parse_args() -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[2]
    default_database = (
        repository_root / "tools" / "verify_rom" / "supported_roms.json"
    )
    default_output = repository_root / "data" / "extracted"

    parser = argparse.ArgumentParser(
        description=(
            "Extract NitroFS and selected system data from a user-provided "
            "Nintendo DS ROM."
        )
    )
    parser.add_argument("rom", type=Path, help="Path to the .nds ROM")
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help=f"Output directory (default: {default_output})",
    )
    parser.add_argument(
        "--database",
        type=Path,
        default=default_database,
        help=f"Supported ROM database (default: {default_database})",
    )
    parser.add_argument(
        "--allow-unknown",
        action="store_true",
        help="Allow extracting an unregistered ROM. Development only.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace a previous extraction with the same SHA-256.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        output = extract_rom(
            rom_path=args.rom,
            output_root=args.output,
            database_path=args.database,
            allow_unknown=args.allow_unknown,
            force=args.force,
        )
    except PermissionError as exc:
        print(f"UNSUPPORTED: {exc}", file=sys.stderr)
        return 2
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Extraction complete: {output}")
    print(f"Metadata: {output / 'metadata.json'}")
    print(f"Manifest: {output / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
