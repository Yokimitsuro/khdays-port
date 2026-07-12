#!/usr/bin/env python3
from __future__ import annotations

import argparse
import binascii
import json
import struct
import sys
import zlib
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


MAX_DIMENSION = 8192
NITRO_HEADER_SIZE = 0x10


@dataclass(frozen=True)
class NitroSection:
    magic: bytes
    offset: int
    size: int
    data: bytes


@dataclass(frozen=True)
class PaletteData:
    colors: list[tuple[int, int, int, int]]
    depth_code: int
    bpp: int
    raw_size: int


@dataclass(frozen=True)
class CharacterData:
    tiles: bytes
    depth_code: int
    bpp: int
    tile_count: int
    raw_size: int
    declared_width: int
    declared_height: int


@dataclass(frozen=True)
class ScreenData:
    width: int
    height: int
    entries: list[int]
    raw_size: int


@dataclass(frozen=True)
class RenderResult:
    output: str
    width: int
    height: int
    bpp: int
    palette_colors: int
    palette_bank: int
    tile_count: int
    map_entries: int
    unique_map_entries: int
    missing_tile_references: int
    transparent_zero: bool
    render_mode: str
    nclr: str
    ncgr: str
    nscr: str


def read_u16(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"{label}: cannot read u16 at 0x{offset:X}.")
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"{label}: cannot read u32 at 0x{offset:X}.")
    return struct.unpack_from("<I", data, offset)[0]


def parse_nitro_sections(data: bytes, expected_magic: bytes) -> list[NitroSection]:
    if len(data) < NITRO_HEADER_SIZE:
        raise ValueError("Nitro file is smaller than its 0x10-byte header.")
    if data[:4] != expected_magic:
        raise ValueError(
            f"Expected {expected_magic.decode('ascii')}, "
            f"found {data[:4]!r}."
        )

    declared_size = read_u32(data, 0x08, "Nitro file size")
    header_size = read_u16(data, 0x0C, "Nitro header size")
    section_count = read_u16(data, 0x0E, "Nitro section count")

    if declared_size not in (0, len(data)) and declared_size > len(data):
        raise ValueError(
            f"Nitro file declares {declared_size} bytes, "
            f"but only {len(data)} are available."
        )
    if header_size < NITRO_HEADER_SIZE or header_size > len(data):
        raise ValueError(f"Invalid Nitro header size 0x{header_size:X}.")
    if section_count == 0:
        raise ValueError("Nitro file contains no sections.")

    sections: list[NitroSection] = []
    cursor = header_size

    for index in range(section_count):
        if cursor + 8 > len(data):
            raise ValueError(f"Section {index} header is truncated.")

        magic = data[cursor:cursor + 4]
        size = read_u32(data, cursor + 4, f"Section {index} size")

        if size < 8:
            raise ValueError(
                f"Section {index} ({magic!r}) has invalid size {size}."
            )
        if cursor + size > len(data):
            raise ValueError(
                f"Section {index} ({magic!r}) exceeds the file: "
                f"0x{cursor:X}+0x{size:X}>0x{len(data):X}."
            )

        sections.append(
            NitroSection(
                magic=magic,
                offset=cursor,
                size=size,
                data=data[cursor:cursor + size],
            )
        )
        cursor += size

    return sections


def find_section(
    data: bytes,
    file_magic: bytes,
    section_magic: bytes,
) -> NitroSection:
    sections = parse_nitro_sections(data, file_magic)
    for section in sections:
        if section.magic == section_magic:
            return section
    names = ", ".join(repr(section.magic) for section in sections)
    raise ValueError(
        f"Section {section_magic!r} was not found. Available: {names}"
    )


def depth_to_bpp(depth_code: int) -> int:
    # Nitro G2D commonly uses 3 for 16-color/4bpp and 4 for 256-color/8bpp.
    if depth_code == 3:
        return 4
    if depth_code == 4:
        return 8
    raise ValueError(
        f"Unsupported Nitro color-depth code {depth_code}; expected 3 or 4."
    )


def resolve_payload(
    section: NitroSection,
    size_offset: int,
    pointer_offset: int,
    fallback_offset: int,
    label: str,
) -> bytes:
    payload_size = read_u32(section.data, size_offset, f"{label} payload size")
    stored_offset = read_u32(
        section.data,
        pointer_offset,
        f"{label} payload offset",
    )

    # Nitro G2D offsets in PLTT/CHAR are relative to the section body,
    # i.e. immediately after the 8-byte section magic/size header.
    candidates = [8 + stored_offset, fallback_offset]
    tried: list[str] = []

    for start in candidates:
        if start in (0, 0xFFFFFFFF):
            continue
        end = start + payload_size
        tried.append(f"0x{start:X}-0x{end:X}")
        if start >= 8 and end <= len(section.data):
            return section.data[start:end]

    raise ValueError(
        f"{label} payload is outside its section. Tried: {', '.join(tried)}"
    )


def bgr555_to_rgba(value: int) -> tuple[int, int, int, int]:
    red5 = value & 0x1F
    green5 = (value >> 5) & 0x1F
    blue5 = (value >> 10) & 0x1F

    red = (red5 << 3) | (red5 >> 2)
    green = (green5 << 3) | (green5 >> 2)
    blue = (blue5 << 3) | (blue5 >> 2)
    return red, green, blue, 255


def parse_nclr(path: Path) -> PaletteData:
    data = path.read_bytes()
    section = find_section(data, b"RLCN", b"TTLP")

    depth_code = read_u32(section.data, 0x08, "NCLR depth")
    bpp = depth_to_bpp(depth_code)
    payload = resolve_payload(
        section=section,
        size_offset=0x10,
        pointer_offset=0x14,
        fallback_offset=0x18,
        label="NCLR",
    )

    if len(payload) % 2 != 0:
        raise ValueError("NCLR palette data has an odd byte length.")

    colors = [
        bgr555_to_rgba(read_u16(payload, offset, "Palette color"))
        for offset in range(0, len(payload), 2)
    ]
    minimum = 16 if bpp == 4 else 256
    if len(colors) < minimum:
        raise ValueError(
            f"NCLR contains {len(colors)} colors; {minimum} are required for {bpp}bpp."
        )

    return PaletteData(
        colors=colors,
        depth_code=depth_code,
        bpp=bpp,
        raw_size=len(payload),
    )


def parse_ncgr(path: Path) -> CharacterData:
    data = path.read_bytes()
    section = find_section(data, b"RGCN", b"RAHC")

    declared_height = read_u16(section.data, 0x08, "NCGR height")
    declared_width = read_u16(section.data, 0x0A, "NCGR width")
    depth_code = read_u32(section.data, 0x0C, "NCGR depth")
    bpp = depth_to_bpp(depth_code)
    payload = resolve_payload(
        section=section,
        size_offset=0x18,
        pointer_offset=0x1C,
        fallback_offset=0x20,
        label="NCGR",
    )

    tile_size = 32 if bpp == 4 else 64
    if len(payload) % tile_size != 0:
        raise ValueError(
            f"NCGR payload size {len(payload)} is not divisible by "
            f"the {tile_size}-byte {bpp}bpp tile size."
        )

    return CharacterData(
        tiles=payload,
        depth_code=depth_code,
        bpp=bpp,
        tile_count=len(payload) // tile_size,
        raw_size=len(payload),
        declared_width=declared_width,
        declared_height=declared_height,
    )


def parse_nscr(path: Path) -> ScreenData:
    data = path.read_bytes()
    section = find_section(data, b"RCSN", b"NRCS")

    width = read_u16(section.data, 0x08, "NSCR width")
    height = read_u16(section.data, 0x0A, "NSCR height")
    # Unlike NCLR and NCGR, the NRCS section does not contain a data
    # pointer at 0x14. Its tile-map payload starts directly at 0x14.
    payload_size = read_u32(section.data, 0x10, "NSCR payload size")
    payload_start = 0x14
    payload_end = payload_start + payload_size

    if payload_end > len(section.data):
        raise ValueError(
            f"NSCR payload exceeds its section: "
            f"0x{payload_start:X}-0x{payload_end:X} > "
            f"0x{len(section.data):X}."
        )

    payload = section.data[payload_start:payload_end]

    if width <= 0 or height <= 0:
        raise ValueError(f"Invalid NSCR dimensions {width}x{height}.")
    if width > MAX_DIMENSION or height > MAX_DIMENSION:
        raise ValueError(
            f"Refusing NSCR dimensions {width}x{height}; "
            f"maximum is {MAX_DIMENSION}."
        )
    if width % 8 != 0 or height % 8 != 0:
        raise ValueError(
            f"NSCR dimensions {width}x{height} are not multiples of 8."
        )
    if len(payload) % 2 != 0:
        raise ValueError("NSCR tile-map payload has an odd byte length.")

    entries = [
        read_u16(payload, offset, "NSCR map entry")
        for offset in range(0, len(payload), 2)
    ]
    expected_entries = (width // 8) * (height // 8)
    if len(entries) < expected_entries:
        raise ValueError(
            f"NSCR contains {len(entries)} map entries, "
            f"but {expected_entries} are needed for {width}x{height}."
        )

    return ScreenData(
        width=width,
        height=height,
        entries=entries[:expected_entries],
        raw_size=len(payload),
    )


def decode_tile_indices(
    characters: CharacterData,
    tile_index: int,
) -> list[int] | None:
    tile_size = 32 if characters.bpp == 4 else 64
    start = tile_index * tile_size
    end = start + tile_size

    if start < 0 or end > len(characters.tiles):
        return None

    tile = characters.tiles[start:end]
    indices: list[int] = []

    if characters.bpp == 4:
        for value in tile:
            indices.append(value & 0x0F)
            indices.append((value >> 4) & 0x0F)
    else:
        indices.extend(tile)

    if len(indices) != 64:
        raise AssertionError("Decoded tile does not contain 64 pixels.")
    return indices


def palette_color(
    palette: PaletteData,
    pixel_index: int,
    palette_bank: int,
    transparent_zero: bool,
) -> tuple[int, int, int, int]:
    if palette.bpp == 4:
        absolute_index = palette_bank * 16 + pixel_index
    else:
        # Most 8bpp screens use bank 0. Preserve a bank only if the NCLR
        # actually contains enough colors for multiple 256-color palettes.
        banked_index = palette_bank * 256 + pixel_index
        absolute_index = (
            banked_index
            if banked_index < len(palette.colors)
            else pixel_index
        )

    if absolute_index >= len(palette.colors):
        return 255, 0, 255, 255

    red, green, blue, alpha = palette.colors[absolute_index]
    if transparent_zero and pixel_index == 0:
        alpha = 0
    return red, green, blue, alpha



def used_pixel_indices(characters: CharacterData) -> set[int]:
    indices: set[int] = set()
    if characters.bpp == 4:
        for value in characters.tiles:
            indices.add(value & 0x0F)
            indices.add((value >> 4) & 0x0F)
    else:
        indices.update(characters.tiles)
    return indices


def choose_palette_bank(
    palette: PaletteData,
    characters: CharacterData,
    requested_bank: int | None,
) -> int:
    if characters.bpp == 8:
        if requested_bank not in (None, 0):
            raise ValueError("8bpp resources normally use palette bank 0.")
        return 0

    bank_count = len(palette.colors) // 16
    if bank_count <= 0:
        raise ValueError("NCLR does not contain a complete 16-color bank.")

    if requested_bank is not None:
        if requested_bank < 0 or requested_bank >= bank_count:
            raise ValueError(
                f"Palette bank {requested_bank} is outside 0-{bank_count - 1}."
            )
        return requested_bank

    used = used_pixel_indices(characters)
    best_bank = 0
    best_score: tuple[int, int, int] = (-1, -1, -1)

    for bank in range(bank_count):
        bank_colors = palette.colors[bank * 16:(bank + 1) * 16]
        selected = [bank_colors[index] for index in sorted(used) if index < 16]
        unique_colors = len(set(selected))
        non_black = sum(color[:3] != (0, 0, 0) for color in selected)
        brightness = sum(sum(color[:3]) for color in selected)
        score = (unique_colors, non_black, brightness)
        if score > best_score:
            best_score = score
            best_bank = bank

    return best_bank


def should_render_as_atlas(
    characters: CharacterData,
    screen: ScreenData,
    requested_mode: str,
) -> bool:
    if requested_mode == "atlas":
        return True
    if requested_mode == "screen":
        return False

    declared_tile_count = (
        characters.declared_width * characters.declared_height
    )
    map_is_trivial = len(set(screen.entries)) <= 1

    return (
        map_is_trivial
        and characters.declared_width > 0
        and characters.declared_height > 0
        and declared_tile_count == characters.tile_count
    )


def render_atlas(
    palette: PaletteData,
    characters: CharacterData,
    palette_bank: int,
    transparent_zero: bool,
) -> tuple[bytearray, int, int]:
    width_tiles = characters.declared_width
    height_tiles = characters.declared_height

    if width_tiles <= 0 or height_tiles <= 0:
        raise ValueError(
            "NCGR does not provide valid atlas dimensions."
        )
    if width_tiles * height_tiles != characters.tile_count:
        raise ValueError(
            f"NCGR declares {width_tiles}x{height_tiles} tiles "
            f"({width_tiles * height_tiles}), but contains "
            f"{characters.tile_count}."
        )

    width = width_tiles * 8
    height = height_tiles * 8

    if width > MAX_DIMENSION or height > MAX_DIMENSION:
        raise ValueError(
            f"Refusing atlas dimensions {width}x{height}; "
            f"maximum is {MAX_DIMENSION}."
        )

    rgba = bytearray(width * height * 4)

    for tile_index in range(characters.tile_count):
        tile_indices = decode_tile_indices(characters, tile_index)
        if tile_indices is None:
            raise AssertionError("Sequential NCGR tile unexpectedly missing.")

        tile_x = (tile_index % width_tiles) * 8
        tile_y = (tile_index // width_tiles) * 8

        for y in range(8):
            for x in range(8):
                pixel_index = tile_indices[y * 8 + x]
                color = palette_color(
                    palette=palette,
                    pixel_index=pixel_index,
                    palette_bank=palette_bank,
                    transparent_zero=transparent_zero,
                )
                output_offset = ((tile_y + y) * width + tile_x + x) * 4
                rgba[output_offset:output_offset + 4] = bytes(color)

    return rgba, width, height


def render_screen(
    palette: PaletteData,
    characters: CharacterData,
    screen: ScreenData,
    transparent_zero: bool,
    forced_palette_bank: int | None = None,
) -> tuple[bytearray, int]:
    if palette.bpp != characters.bpp:
        raise ValueError(
            f"NCLR is {palette.bpp}bpp but NCGR is {characters.bpp}bpp."
        )

    rgba = bytearray(screen.width * screen.height * 4)
    map_width = screen.width // 8
    missing_tiles = 0

    for map_index, entry in enumerate(screen.entries):
        tile_index = entry & 0x03FF
        horizontal_flip = bool(entry & 0x0400)
        vertical_flip = bool(entry & 0x0800)
        palette_bank = (
            forced_palette_bank
            if forced_palette_bank is not None
            else (entry >> 12) & 0x0F
        )

        tile_indices = decode_tile_indices(characters, tile_index)
        if tile_indices is None:
            missing_tiles += 1
            tile_indices = [0] * 64

        tile_x = (map_index % map_width) * 8
        tile_y = (map_index // map_width) * 8

        for destination_y in range(8):
            source_y = 7 - destination_y if vertical_flip else destination_y

            for destination_x in range(8):
                source_x = (
                    7 - destination_x
                    if horizontal_flip
                    else destination_x
                )
                pixel_index = tile_indices[source_y * 8 + source_x]
                color = palette_color(
                    palette=palette,
                    pixel_index=pixel_index,
                    palette_bank=palette_bank,
                    transparent_zero=transparent_zero,
                )

                x = tile_x + destination_x
                y = tile_y + destination_y
                output_offset = (y * screen.width + x) * 4
                rgba[output_offset:output_offset + 4] = bytes(color)

    return rgba, missing_tiles


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    crc = binascii.crc32(chunk_type)
    crc = binascii.crc32(payload, crc) & 0xFFFFFFFF
    return (
        struct.pack(">I", len(payload))
        + chunk_type
        + payload
        + struct.pack(">I", crc)
    )


def write_rgba_png(
    path: Path,
    width: int,
    height: int,
    rgba: bytes,
) -> None:
    expected = width * height * 4
    if len(rgba) != expected:
        raise ValueError(
            f"RGBA buffer contains {len(rgba)} bytes; expected {expected}."
        )

    scanlines = bytearray()
    row_size = width * 4
    for y in range(height):
        scanlines.append(0)  # PNG filter: None
        start = y * row_size
        scanlines.extend(rgba[start:start + row_size])

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(
        png_chunk(
            b"IHDR",
            struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0),
        )
    )
    png.extend(png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9)))
    png.extend(png_chunk(b"IEND", b""))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def find_single(container: Path, extension: str) -> Path:
    matches = sorted(container.rglob(f"*{extension}"))
    if not matches:
        raise FileNotFoundError(
            f"No {extension} file was found below {container}."
        )
    if len(matches) > 1:
        rendered = "\n".join(f"  - {path}" for path in matches[:20])
        raise ValueError(
            f"Multiple {extension} files were found below {container}. "
            f"Pass an explicit path.\n{rendered}"
        )
    return matches[0]


def resolve_inputs(args: argparse.Namespace) -> tuple[Path, Path, Path]:
    if args.container:
        container = args.container.resolve()
        if not container.is_dir():
            raise NotADirectoryError(f"Not a directory: {container}")

        nclr = args.nclr.resolve() if args.nclr else find_single(container, ".nclr")
        ncgr = args.ncgr.resolve() if args.ncgr else find_single(container, ".ncgr")
        nscr = args.nscr.resolve() if args.nscr else find_single(container, ".nscr")
        return nclr, ncgr, nscr

    if not (args.nclr and args.ncgr and args.nscr):
        raise ValueError(
            "Pass --container, or pass --nclr, --ncgr, and --nscr together."
        )

    return args.nclr.resolve(), args.ncgr.resolve(), args.nscr.resolve()


def render(
    nclr_path: Path,
    ncgr_path: Path,
    nscr_path: Path,
    output_path: Path,
    transparent_zero: bool,
    render_mode: str = "auto",
    requested_palette_bank: int | None = None,
) -> RenderResult:
    for path in (nclr_path, ncgr_path, nscr_path):
        if not path.is_file():
            raise FileNotFoundError(f"Missing input file: {path}")

    palette = parse_nclr(nclr_path)
    characters = parse_ncgr(ncgr_path)
    screen = parse_nscr(nscr_path)

    palette_bank = choose_palette_bank(
        palette=palette,
        characters=characters,
        requested_bank=requested_palette_bank,
    )

    atlas_mode = should_render_as_atlas(
        characters=characters,
        screen=screen,
        requested_mode=render_mode,
    )

    if atlas_mode:
        rgba, width, height = render_atlas(
            palette=palette,
            characters=characters,
            palette_bank=palette_bank,
            transparent_zero=transparent_zero,
        )
        missing_tiles = 0
        actual_mode = "atlas"
    else:
        rgba, missing_tiles = render_screen(
            palette=palette,
            characters=characters,
            screen=screen,
            transparent_zero=transparent_zero,
            forced_palette_bank=(
                palette_bank if requested_palette_bank is not None else None
            ),
        )
        width = screen.width
        height = screen.height
        actual_mode = "screen"

    write_rgba_png(
        path=output_path,
        width=width,
        height=height,
        rgba=rgba,
    )

    result = RenderResult(
        output=str(output_path.resolve()),
        width=width,
        height=height,
        bpp=characters.bpp,
        palette_colors=len(palette.colors),
        palette_bank=palette_bank,
        tile_count=characters.tile_count,
        map_entries=len(screen.entries),
        unique_map_entries=len(set(screen.entries)),
        missing_tile_references=missing_tiles,
        transparent_zero=transparent_zero,
        render_mode=actual_mode,
        nclr=str(nclr_path),
        ncgr=str(ncgr_path),
        nscr=str(nscr_path),
    )

    metadata_path = output_path.with_suffix(output_path.suffix + ".json")
    metadata_path.write_text(
        json.dumps(asdict(result), indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return result

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Render a Nintendo DS NCLR + NCGR + NSCR resource set to PNG."
        )
    )
    parser.add_argument(
        "--container",
        type=Path,
        help=(
            "Unpacked container directory. The tool finds one .nclr, "
            "one .ncgr, and one .nscr recursively."
        ),
    )
    parser.add_argument("--nclr", type=Path, help="Explicit .nclr path")
    parser.add_argument("--ncgr", type=Path, help="Explicit .ncgr path")
    parser.add_argument("--nscr", type=Path, help="Explicit .nscr path")
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output PNG path",
    )
    parser.add_argument(
        "--transparent-zero",
        action="store_true",
        help="Write palette index 0 with alpha 0.",
    )
    parser.add_argument(
        "--mode",
        choices=("auto", "screen", "atlas"),
        default="auto",
        help=(
            "Rendering mode. Auto uses the NCGR atlas when the NSCR map is "
            "trivial and NCGR dimensions match its tile count."
        ),
    )
    parser.add_argument(
        "--palette-bank",
        type=int,
        default=None,
        help=(
            "Force a 4bpp palette bank. Auto selects the most informative "
            "bank for atlas rendering."
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        nclr, ncgr, nscr = resolve_inputs(args)
        result = render(
            nclr_path=nclr,
            ncgr_path=ncgr,
            nscr_path=nscr,
            output_path=args.output.resolve(),
            transparent_zero=args.transparent_zero,
            render_mode=args.mode,
            requested_palette_bank=args.palette_bank,
        )
    except (OSError, ValueError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"PNG:       {result.output}")
    print(f"Size:      {result.width}x{result.height}")
    print(f"Mode:      {result.render_mode}")
    print(
        f"Color:     {result.bpp}bpp, {result.palette_colors} colors, "
        f"bank {result.palette_bank}"
    )
    print(f"Tiles:     {result.tile_count}")
    print(
        f"Map cells: {result.map_entries} "
        f"({result.unique_map_entries} unique)"
    )
    print(f"Missing:   {result.missing_tile_references}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
