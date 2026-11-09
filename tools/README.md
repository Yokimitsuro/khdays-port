# Tools

Python 3.11+, run from the repository's root. Nothing here downloads, copies
or distributes the game: each tool reads your own copy and writes under
`data/`, which is never committed. The installer runs the same tools.

## From your ROM to the game's data

```powershell
py .\tools\verify_rom.py "E:\path\game.nds"
py .\tools\extract_data.py "E:\path\game.nds"
py .\tools\decompress_data.py ".\data\extracted\<hash>"
```

- `verify_rom.py` checks the ROM's SHA-256 against `supported_roms.json`. A trimmed dump (cut
  after the ROM's used size) is hashed with its cartridge's 0xFF padding put back, so it counts as
  the whole dump; `extract_data.py` hashes it the same way. An entry's `sha256_header_free` is the
  hash with the header's area 0x200-0xFFF taken as zeroes: nothing loads it (the port keeps the
  header's first 0x200 bytes) and dumpers fill it differently -- a No-Intro dump of 358/2 Days keeps
  187 bytes of the cartridge there, another copy of the same game zeroes, every other byte the same.
  `--json` prints what it read; `--print-entry` prints an entry for that
  list. Register only clean dumps whose region and revision you confirmed,
  with their real hash. Exit codes: 0 supported, 1 read error, 2 not
  registered.
- `extract_data.py` writes `data/extracted/<first 16 hex digits of the
  SHA-256>/`: `system/` (header, ARM9, ARM7, overlay tables, FNT, FAT),
  `nitrofs/` (the game's files), `metadata.json` and `manifest.json` (each
  file's place, size and hash). It refuses an unregistered ROM
  (`--allow-unknown` for development) and an existing extraction (`--force`
  replaces it).
- `decompress_data.py` writes `decompressed/` beside `nitrofs/`: each LZ10 or
  LZ11 file as the game reads it (`x.p.z` becomes `x.p`), with
  `decompressed_manifest.json`. `--force` replaces it.

`tests/` holds their tests: `python -m unittest discover -s tools/tests`.

## HD mods from the HD collections

`hd/` makes the optional HD look (title, menus, save screen, theater, camp,
pause, videos) from your own copies of the KINGDOM HEARTS HD collections;
nothing of them is in the repository. The README's sections on each mod say
which tool makes it and from which files. The installer runs them when you
choose the HD pack.

## The port's own mods

`mods/` makes the mods that switch on features of the port's own (the walk,
the KH2 prizes, the Menu Overhaul), with the readers they share. The
installer runs them and hashes the folder (`installer/mod_inputs.txt`): a
change there makes every player's copy remake those mods.

## Research

`research/` reads the games' files to study them, for the formats in
`docs/FORMATS.md`; nothing the game or the installer uses. `kh2_ai.py` lists
KINGDOM HEARTS II's AI scripts (`docs/FORMATS.md`, "KH2's objects and their
AI"). Their tests are in `research/tests/`.
