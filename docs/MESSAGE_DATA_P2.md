# Message data: the `db_<lang>.p2` container

The game's story/menu text lives in `/db/db_<lang>.p2` — one file per language
(`db_en`, `db_es`, `db_fr`, `db_de`, `db_it`, `db_ja`). Each is a **"P2"
container**: a 16-byte header, a directory of sub-files, and a packed data
region of the actual strings. This note records the on-disk layout confirmed
from the decompiled loader (`external/khdays-decomp`) and cross-checked against
the real `db_en.p2`.

## Path building — `Msg_BuildLangPath` (`func_02024e6c`)

The loader expands a template, replacing `&` with the current language's 2-char
code: `/db/db_&.p2` → `/db/db_en.p2`. The code comes from a per-language table
indexed by the current language id.

## File header — `Msg_OpenContainerAndReadHeader` (`func_02024ee8`)

The first 16 bytes:

| Offset | Type | Meaning |
|---|---|---|
| `0x00` | `u16` | magic `'P2'` (bytes `50 32`) |
| `0x02` | `u16` | `count = word & 0x1ff` (record count, **23** in `db_en`); bit `0x8000` = "wide" format flag |
| `0x04` | `u32` | header word (copied through; `0` in `db_en`) |
| `0x08` | `u32` | header word (overwritten in memory with the FS offset) |
| `0x0C` | `u32` | data-region base (`0x200` in `db_en`) |

The loader reads the header, then reads a **directory block** of
`count*4 + ((count+1)/2)*4` bytes (plus `count*8` if the wide flag is set)
immediately after it, at file offset `0x10`.

## Directory (at `0x10`)

Two tables, in this order (confirmed byte-exact against `db_en`):

| Offset | Table | Size | Contents |
|---|---|---|---|
| `0x10` | `u16 sizes[]` | `((count+1)/2)*4` = 48 B | per-record prefix/offset table — `db_en`: `0,4,8,10,12,16,20,24,28,30,32,37,41,43,47,51,55,59,64,68,77,78,103` (23 sub-DBs, 103 strings total) |
| `0x40` | `u32 desc[]` | `count*4` = 92 B | one sub-file descriptor each; bit `0x80000000` set = present, low 31 bits = the sub-file's handle/offset |

For `count = 23` the directory ends at `0x9C`; the file then pads with zeros up
to the data-region base `0x200`.

## Data region (from `0x200`)

The packed sub-files begin at the data-region base. Once a sub-file is located,
its inner blob is decoded by `P2_ExtractString` (`func_02034f44`):

```
{ u32 count; u32 offsets[count]; u16 utf16le_data[] }
```

string *k* = `data[offsets[k] .. offsets[k+1]]`, UTF-16LE with embedded control
codes. (This inner offset-table format was confirmed earlier; the per-db decoder
family `func_020347xx…020349xx` wraps it per record type.)

## Still open

Turning a `desc[k]` into the sub-file's byte range is done by `func_0201ef9c`
(the FS/packed-file reader), which is **not yet decompiled**. Dumping `db_en`'s
data region shows UTF-16 text (e.g. `"Basic Model!"` around `0xB6D`)
interleaved with control/marker bytes rather than a clean run of inner blobs, so
the exact sub-file addressing — and whether the region is additionally packed —
needs that reader before a from-scratch extractor can be written. The header and
directory above are confirmed; the sub-file locator is the remaining piece.
