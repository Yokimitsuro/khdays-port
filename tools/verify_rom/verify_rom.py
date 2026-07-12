#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any


HEADER_MIN_SIZE = 0x200

REGION_BY_CODE = {
    "E": "USA",
    "J": "Japan",
    "P": "Europe",
    "K": "Korea",
    "C": "China",
}


@dataclass(frozen=True)
class RomInfo:
    path: str
    title: str
    game_code: str
    maker_code: str
    unit_code: int
    rom_version: int
    header_crc16: str
    file_size: int
    sha256: str
    region: str


def decode_ascii(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace").strip()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_rom_info(path: Path) -> RomInfo:
    if not path.is_file():
        raise FileNotFoundError(f"No existe el archivo: {path}")

    with path.open("rb") as stream:
        header = stream.read(HEADER_MIN_SIZE)

    if len(header) < HEADER_MIN_SIZE:
        raise ValueError(
            f"El archivo es demasiado pequeño para ser una ROM NDS válida "
            f"({len(header)} bytes)."
        )

    title = decode_ascii(header[0x00:0x0C])
    game_code = decode_ascii(header[0x0C:0x10])
    maker_code = decode_ascii(header[0x10:0x12])
    unit_code = header[0x12]
    rom_version = header[0x1E]
    header_crc16 = f"{int.from_bytes(header[0x15E:0x160], 'little'):04X}"
    region = REGION_BY_CODE.get(game_code[-1:] if game_code else "", "Unknown")

    return RomInfo(
        path=str(path.resolve()),
        title=title,
        game_code=game_code,
        maker_code=maker_code,
        unit_code=unit_code,
        rom_version=rom_version,
        header_crc16=header_crc16,
        file_size=path.stat().st_size,
        sha256=sha256_file(path),
        region=region,
    )


def load_database(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        raise FileNotFoundError(f"No existe la base de datos: {path}")

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"JSON inválido en {path}: {exc}") from exc

    roms = data.get("roms")
    if not isinstance(roms, list):
        raise ValueError("supported_roms.json debe contener una lista llamada 'roms'.")

    return roms


def find_supported_rom(info: RomInfo, roms: list[dict[str, Any]]) -> dict[str, Any] | None:
    actual_hash = info.sha256.lower()

    for entry in roms:
        expected_hash = str(entry.get("sha256", "")).lower().strip()
        if expected_hash and expected_hash == actual_hash:
            return entry

    return None


def print_info(info: RomInfo) -> None:
    print(f"Title:         {info.title or '(empty)'}")
    print(f"Game code:     {info.game_code or '(empty)'}")
    print(f"Region:        {info.region}")
    print(f"Maker code:    {info.maker_code or '(empty)'}")
    print(f"Unit code:     {info.unit_code}")
    print(f"ROM version:   {info.rom_version}")
    print(f"Header CRC16:  {info.header_crc16}")
    print(f"File size:     {info.file_size} bytes")
    print(f"SHA-256:       {info.sha256}")


def print_database_entry(info: RomInfo) -> None:
    entry = {
        "name": f"{info.title or 'Nintendo DS ROM'} ({info.region})",
        "game_code": info.game_code,
        "rom_version": info.rom_version,
        "sha256": info.sha256,
    }
    print("\nEntrada candidata para supported_roms.json:")
    print(json.dumps(entry, indent=2, ensure_ascii=False))


def parse_args() -> argparse.Namespace:
    default_db = Path(__file__).with_name("supported_roms.json")

    parser = argparse.ArgumentParser(
        description="Inspect and verify a user-provided Nintendo DS ROM."
    )
    parser.add_argument("rom", type=Path, help="Ruta al archivo .nds")
    parser.add_argument(
        "--database",
        type=Path,
        default=default_db,
        help=f"Base de ROMs soportadas (por defecto: {default_db})",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Imprime la información de la ROM como JSON.",
    )
    parser.add_argument(
        "--print-entry",
        action="store_true",
        help="Imprime una entrada candidata para supported_roms.json.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        info = read_rom_info(args.rom)
        roms = load_database(args.database)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(asdict(info), indent=2, ensure_ascii=False))
    else:
        print_info(info)

    supported = find_supported_rom(info, roms)

    if supported:
        print(f"\nSUPPORTED: {supported.get('name', 'Known ROM')}")
        return 0

    print("\nUNSUPPORTED: el SHA-256 no está registrado.")
    if args.print_entry:
        print_database_entry(info)

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
