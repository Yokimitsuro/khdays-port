# Message data: the `db_<lang>.p2` container

The game's story/menu text lives in `/db/db_<lang>.p2` — one file per language
(`db_en`, `db_es`, `db_fr`, `db_de`, `db_it`, `db_ja`). Each is a **"P2"
container**: a 16-byte header, a directory of sub-files, and a data region of
individually-compressed string blobs. The format is fully decoded and
implemented (`khdays::assets::load_p2_archive`), confirmed from the decompiled
loader (`external/khdays-decomp`) and byte-checked against every sub-file of
`db_en`/`db_es`.

## Path building — `Msg_BuildLangPath` (`func_02024e6c`)

The loader expands a template, replacing `&` with the current language's 2-char
code: `/db/db_&.p2` → `/db/db_en.p2`.

## File header — `Msg_OpenContainerAndReadHeader` (`func_02024ee8`)

The first 16 bytes:

| Offset | Type | Meaning |
|---|---|---|
| `0x00` | `u16` | magic `'P2'` (bytes `50 32`) |
| `0x02` | `u16` | `count = word & 0x1ff` (sub-file count, **23** in `db_en`); bit `0x8000` = "wide" flag |
| `0x04` | `u32` | header word |
| `0x08` | `u32` | header word (holds the card FD in memory) |
| `0x0C` | `u32` | `base_offset` of the data region (**`0x200`** in `db_en`) |

## Directory (at `0x10`)

Two tables, `sizes[]` first then `desc[]`:

| Offset | Table | Size | Contents |
|---|---|---|---|
| `0x10` | `u16 sizes[]` | `((count+1)/2)*4` B | **start sector** of each sub-file, in units of `0x200` (512 B) |
| `0x10 + ((count+1)/2)*4` | `u32 desc[]` | `count*4` B | bit `0x80000000` = **LZ11-compressed**; bits `0..30` = sub-file **size in bytes** |

Each sub-file *k* therefore occupies:

```
start = base_offset + sizes[k] * 0x200
size  = desc[k] & 0x7fffffff
end   = start + size          // compressed if desc[k] & 0x80000000
```

The 31 low bits of `desc[k]` are the **size**, not an offset — the offset comes
from the separate `sizes[]` sector table (`add r2, r2, r1, lsl #9` in the ROM).
This is handled by `Archive_LoadFile` (`func_0201ef9c`) /
`Archive_OpenSubfileByHandle` (`func_020250bc`); the compression flag is read by
`Archive_SubfileIsCompressed` (`func_02025074`).

## Sub-file compression

A sub-file whose `desc[k]` has bit 31 set is packed with **standard Nintendo DS
LZ11** (type byte `0x11`, then a 24-bit uncompressed size). In `db_en` **all 23**
sub-files are LZ11. The container itself is stored raw (it has a `.p2`, not `.z`,
extension); only the individual sub-files are compressed.

## Sub-file contents

Once decompressed, a sub-file is an offset-table blob decoded by
`P2_ExtractString` (`func_02034f44`):

```
{ u32 count; u32 offsets[count]; u16 utf16le_data[] }
```

`offsets[k]` is the byte end of string *k*; string 0 starts at 0 and string
*k>0* at `offsets[k-1]`. The data region starts at `(count+1)*4`. Strings are
UTF-16LE with a trailing NUL and embedded control codes. In `db_en` each of the
23 sub-files holds ~100 strings (**3480** total), e.g. sub-file 0 =
`"Ashes"`, `"Your most basic weapon.\nNothing really sets it apart."`,
`"Doldrums"`, … (weapon names and descriptions).

## Using it

```
khdays-port --message-info   db_es.p2        # sub-db count, string totals, samples
khdays-port --dump-messages  db_es.p2 [SUBDB] # decoded UTF-8 text
```

`load_p2_archive()` returns a `MessageArchive` of sub-databases, each a list of
UTF-16 strings; `message_to_utf8()` renders one for display. The LZ11 decoder is
exposed as `lz_decompress()` for other packed assets.
