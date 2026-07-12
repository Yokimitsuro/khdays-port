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
from typing import Any


FORMAT_NAMES = {
    1: "A3I5",
    2: "PLTT4",
    3: "PLTT16",
    4: "PLTT256",
    5: "COMP4X4",
    6: "A5I3",
    7: "DIRECT",
}


@dataclass(frozen=True)
class TextureRecord:
    name: str
    palette_name: str | None
    width: int
    height: int
    format_code: int
    format_name: str
    data_offset: int
    data_size: int
    palette_offset: int | None
    palette_size: int | None
    color0_transparent: bool
    output_path: str


def read_u16(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"{label}: cannot read u16 at 0x{offset:X}")
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"{label}: cannot read u32 at 0x{offset:X}")
    return struct.unpack_from("<I", data, offset)[0]


def parse_nitro_sections(data: bytes) -> dict[bytes, bytes]:
    if len(data) < 0x10:
        raise ValueError("File is smaller than the Nitro header.")

    file_magic = data[:4]
    if file_magic not in (b"BMD0", b"BTX0"):
        raise ValueError(
            f"Expected BMD0 or BTX0, found {file_magic!r}."
        )

    section_count = read_u16(data, 0x0E, "section count")
    if section_count <= 0 or section_count > 64:
        raise ValueError(f"Invalid section count {section_count}.")

    table_end = 0x10 + section_count * 4
    if table_end > len(data):
        raise ValueError("Section-offset table exceeds the file.")

    sections: dict[bytes, bytes] = {}
    for index in range(section_count):
        section_offset = read_u32(
            data,
            0x10 + index * 4,
            f"section {index} offset",
        )
        if section_offset + 8 > len(data):
            raise ValueError(
                f"Section {index} offset 0x{section_offset:X} is outside the file."
            )

        magic = data[section_offset:section_offset + 4]
        size = read_u32(data, section_offset + 4, f"section {index} size")
        if size < 8 or section_offset + size > len(data):
            raise ValueError(
                f"Section {index} {magic!r} has invalid size {size}."
            )

        sections[magic] = data[section_offset:section_offset + size]

    return sections


def parse_resource_dictionary(
    data: bytes,
    offset: int,
    label: str,
) -> tuple[list[str], list[bytes]]:
    if offset < 0 or offset + 8 > len(data):
        raise ValueError(f"{label}: dictionary offset 0x{offset:X} is invalid.")

    entry_count = data[offset + 1]
    dictionary_size = read_u16(data, offset + 2, f"{label} dictionary size")
    entry_header_offset = read_u16(
        data,
        offset + 6,
        f"{label} entry-header offset",
    )

    if entry_count == 0:
        return [], []
    if offset + dictionary_size > len(data):
        raise ValueError(f"{label}: dictionary exceeds TEX0.")

    entry_header = offset + entry_header_offset
    if entry_header + 4 > offset + dictionary_size:
        raise ValueError(f"{label}: entry header exceeds dictionary.")

    unit_size = read_u16(data, entry_header, f"{label} entry size")
    names_offset = read_u16(
        data,
        entry_header + 2,
        f"{label} names offset",
    )

    entries_start = entry_header + 4
    entries_end = entries_start + entry_count * unit_size
    names_start = entry_header + names_offset
    names_end = names_start + entry_count * 16

    dictionary_end = offset + dictionary_size
    if entries_end > dictionary_end or names_end > dictionary_end:
        raise ValueError(f"{label}: entries or names exceed dictionary.")

    entries = [
        data[
            entries_start + index * unit_size:
            entries_start + (index + 1) * unit_size
        ]
        for index in range(entry_count)
    ]

    names = []
    for index in range(entry_count):
        raw = data[names_start + index * 16:names_start + (index + 1) * 16]
        names.append(raw.split(b"\0", 1)[0].decode("ascii", "replace"))

    return names, entries


def decode_texture_parameter(value: int) -> dict[str, int | bool]:
    return {
        "offset_units": value & 0xFFFF,
        "size_s_exp": (value >> 20) & 0x07,
        "size_t_exp": (value >> 23) & 0x07,
        "format_code": (value >> 26) & 0x07,
        "color0_transparent": bool((value >> 29) & 0x01),
    }


def texture_data_size(format_code: int, width: int, height: int) -> int:
    pixels = width * height

    if format_code in (1, 4, 6):
        return pixels
    if format_code == 2:
        return (pixels + 3) // 4
    if format_code == 3:
        return (pixels + 1) // 2
    if format_code == 5:
        raise NotImplementedError(
            "Nintendo DS 4x4 compressed textures are not supported yet."
        )
    if format_code == 7:
        return pixels * 2

    raise ValueError(f"Unsupported texture format code {format_code}.")


def bgr555_to_rgba(value: int, alpha: int = 255) -> tuple[int, int, int, int]:
    red5 = value & 0x1F
    green5 = (value >> 5) & 0x1F
    blue5 = (value >> 10) & 0x1F

    red = (red5 << 3) | (red5 >> 2)
    green = (green5 << 3) | (green5 >> 2)
    blue = (blue5 << 3) | (blue5 >> 2)
    return red, green, blue, alpha


def decode_palette(data: bytes) -> list[tuple[int, int, int, int]]:
    if len(data) % 2 != 0:
        raise ValueError("Palette data has an odd byte length.")

    return [
        bgr555_to_rgba(read_u16(data, offset, "palette color"))
        for offset in range(0, len(data), 2)
    ]


def decode_indexed_pixels(
    raw: bytes,
    format_code: int,
    pixel_count: int,
) -> tuple[list[int], list[int]]:
    indices: list[int] = []
    alphas: list[int] = []

    if format_code == 1:  # A3I5
        for value in raw:
            indices.append(value & 0x1F)
            alphas.append(((value >> 5) & 0x07) * 255 // 7)

    elif format_code == 2:  # 4 colors
        for value in raw:
            for shift in (0, 2, 4, 6):
                indices.append((value >> shift) & 0x03)
                alphas.append(255)

    elif format_code == 3:  # 16 colors
        for value in raw:
            indices.append(value & 0x0F)
            indices.append((value >> 4) & 0x0F)
            alphas.extend((255, 255))

    elif format_code == 4:  # 256 colors
        indices.extend(raw)
        alphas.extend([255] * len(raw))

    elif format_code == 6:  # A5I3
        for value in raw:
            indices.append(value & 0x07)
            alphas.append(((value >> 3) & 0x1F) * 255 // 31)

    else:
        raise ValueError(
            f"Format {format_code} is not an indexed texture format."
        )

    return indices[:pixel_count], alphas[:pixel_count]


def decode_texture_rgba(
    raw: bytes,
    width: int,
    height: int,
    format_code: int,
    palette: list[tuple[int, int, int, int]] | None,
    color0_transparent: bool,
) -> bytes:
    pixel_count = width * height
    rgba = bytearray()

    if format_code == 7:
        if len(raw) < pixel_count * 2:
            raise ValueError("Direct-color texture data is truncated.")

        for offset in range(0, pixel_count * 2, 2):
            value = read_u16(raw, offset, "direct-color pixel")
            alpha = 255 if value & 0x8000 else 0
            rgba.extend(bgr555_to_rgba(value, alpha))
        return bytes(rgba)

    if palette is None:
        raise ValueError("Indexed texture has no palette.")

    indices, alphas = decode_indexed_pixels(
        raw=raw,
        format_code=format_code,
        pixel_count=pixel_count,
    )
    if len(indices) != pixel_count:
        raise ValueError(
            f"Decoded {len(indices)} pixels, expected {pixel_count}."
        )

    for index, alpha in zip(indices, alphas):
        if index >= len(palette):
            red, green, blue, _ = (255, 0, 255, 255)
        else:
            red, green, blue, _ = palette[index]

        if color0_transparent and index == 0:
            alpha = 0

        rgba.extend((red, green, blue, alpha))

    return bytes(rgba)


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
        scanlines.append(0)
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


def safe_filename(name: str) -> str:
    safe = "".join(
        character if character.isalnum() or character in "._-" else "_"
        for character in name
    )
    return safe or "texture"


def choose_palette_index(
    texture_name: str,
    texture_index: int,
    palette_names: list[str],
) -> int | None:
    if not palette_names:
        return None

    expected = texture_name + "_pl"
    if expected in palette_names:
        return palette_names.index(expected)

    if texture_name in palette_names:
        return palette_names.index(texture_name)

    if texture_index < len(palette_names):
        return texture_index

    return 0


def extract_tex0(
    input_path: Path,
    output_dir: Path,
) -> dict[str, Any]:
    data = input_path.read_bytes()
    sections = parse_nitro_sections(data)

    tex0 = sections.get(b"TEX0")
    if tex0 is None:
        raise ValueError("The file contains no TEX0 section.")

    texture_data_size_bytes = read_u16(
        tex0,
        0x0C,
        "texture data size",
    ) * 8
    texture_dictionary_offset = read_u16(
        tex0,
        0x0E,
        "texture dictionary offset",
    )
    texture_data_offset = read_u32(
        tex0,
        0x14,
        "texture data offset",
    )

    palette_data_size_bytes = read_u16(
        tex0,
        0x30,
        "palette data size",
    ) * 8
    palette_dictionary_offset = read_u32(
        tex0,
        0x34,
        "palette dictionary offset",
    )
    palette_data_offset = read_u32(
        tex0,
        0x38,
        "palette data offset",
    )

    if texture_data_offset + texture_data_size_bytes > len(tex0):
        raise ValueError("Texture data exceeds TEX0.")
    if palette_data_offset + palette_data_size_bytes > len(tex0):
        raise ValueError("Palette data exceeds TEX0.")

    texture_names, texture_entries = parse_resource_dictionary(
        tex0,
        texture_dictionary_offset,
        "texture",
    )
    palette_names, palette_entries = parse_resource_dictionary(
        tex0,
        palette_dictionary_offset,
        "palette",
    )

    palette_offsets = [
        read_u16(entry, 0, f"palette {index} offset") * 8
        for index, entry in enumerate(palette_entries)
    ]

    records: list[TextureRecord] = []
    failures: list[dict[str, str]] = []
    output_dir.mkdir(parents=True, exist_ok=True)

    for index, (name, entry) in enumerate(zip(texture_names, texture_entries)):
        try:
            if len(entry) < 4:
                raise ValueError("Texture dictionary entry is too small.")

            parameter = read_u32(entry, 0, f"texture {name} parameter")
            decoded = decode_texture_parameter(parameter)

            format_code = int(decoded["format_code"])
            width = 8 << int(decoded["size_s_exp"])
            height = 8 << int(decoded["size_t_exp"])
            data_offset = int(decoded["offset_units"]) * 8
            data_size = texture_data_size(format_code, width, height)

            absolute_texture_start = texture_data_offset + data_offset
            absolute_texture_end = absolute_texture_start + data_size
            texture_region_end = texture_data_offset + texture_data_size_bytes

            if absolute_texture_end > texture_region_end:
                raise ValueError(
                    f"Texture data 0x{absolute_texture_start:X}-"
                    f"0x{absolute_texture_end:X} exceeds texture region."
                )

            raw = tex0[absolute_texture_start:absolute_texture_end]

            palette_index = choose_palette_index(
                texture_name=name,
                texture_index=index,
                palette_names=palette_names,
            )

            palette_name: str | None = None
            palette_offset: int | None = None
            palette_size: int | None = None
            palette: list[tuple[int, int, int, int]] | None = None

            if format_code != 7:
                if palette_index is None:
                    raise ValueError("No palette is available.")

                palette_name = palette_names[palette_index]
                palette_offset = palette_offsets[palette_index]

                next_offsets = [
                    value
                    for value in palette_offsets
                    if value > palette_offset
                ]
                palette_end = (
                    min(next_offsets)
                    if next_offsets
                    else palette_data_size_bytes
                )
                palette_size = palette_end - palette_offset

                if palette_size <= 0:
                    raise ValueError("Palette has an invalid size.")

                absolute_palette_start = palette_data_offset + palette_offset
                absolute_palette_end = palette_data_offset + palette_end
                palette = decode_palette(
                    tex0[absolute_palette_start:absolute_palette_end]
                )

            rgba = decode_texture_rgba(
                raw=raw,
                width=width,
                height=height,
                format_code=format_code,
                palette=palette,
                color0_transparent=bool(decoded["color0_transparent"]),
            )

            output_name = (
                f"{index:03d}_{safe_filename(name)}_"
                f"{width}x{height}_{FORMAT_NAMES.get(format_code, str(format_code))}.png"
            )
            output_path = output_dir / output_name
            write_rgba_png(output_path, width, height, rgba)

            records.append(
                TextureRecord(
                    name=name,
                    palette_name=palette_name,
                    width=width,
                    height=height,
                    format_code=format_code,
                    format_name=FORMAT_NAMES.get(
                        format_code,
                        f"UNKNOWN_{format_code}",
                    ),
                    data_offset=data_offset,
                    data_size=data_size,
                    palette_offset=palette_offset,
                    palette_size=palette_size,
                    color0_transparent=bool(
                        decoded["color0_transparent"]
                    ),
                    output_path=output_path.name,
                )
            )

        except (OSError, ValueError, NotImplementedError, struct.error) as exc:
            failures.append({"name": name, "error": str(exc)})

    manifest = {
        "schema_version": 1,
        "source": str(input_path.resolve()),
        "texture_count": len(texture_names),
        "exported_count": len(records),
        "failure_count": len(failures),
        "textures": [asdict(record) for record in records],
        "failures": failures,
    }

    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Extract embedded TEX0 textures from Nintendo DS NSBMD/NSBTX "
            "files and export them as RGBA PNG."
        )
    )
    parser.add_argument(
        "input",
        type=Path,
        help="Input .nsbmd or .nsbtx file",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output directory for PNG files and manifest.json",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        manifest = extract_tex0(
            input_path=args.input.resolve(),
            output_dir=args.output.resolve(),
        )
    except (OSError, ValueError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Textures:  {manifest['texture_count']}")
    print(f"Exported:  {manifest['exported_count']}")
    print(f"Failures:  {manifest['failure_count']}")
    print(f"Output:    {args.output.resolve()}")

    return 0 if manifest["failure_count"] == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
