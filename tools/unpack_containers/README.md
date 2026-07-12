# KAPH/D2KP container unpacker

Extracts embedded resources from the `KAPH` and `D2KP` containers used by Kingdom Hearts 358/2 Days.

The format observed in the analyzed files is:

```text
0x00  char[4]  magic: KAPH or D2KP
0x04  u32      unknown/reserved
0x08  u32[8]   pointers to up to eight slot tables
```

A slot pointer is either:

- `0xFFFFFFFF` or `0`: unused;
- an offset to a table with this structure:

```text
u32 count
u32 offsets[count]
u32 sizes[count]
```

The extracted Nitro files retain their original bytes.

## Why this structure is considered verified

Examples from the generated report show that:

- a D2KP slot table points to `RLCN`, `RGCN`, and `RCSN`;
- the values following each offset equal the complete Nitro file sizes;
- a KAPH file with one model contains count `1`, offset `0x40`, and size `0x480`;
- a KAPH UI texture package contains count `213` and 213 `BTX0` offsets.

The tool still performs strict bounds checks and refuses malformed tables.

## Run against every KAPH/D2KP container

From the repository root:

```powershell
py .\tools\unpack_containers\unpack_containers.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed"
```

Output:

```text
decompressed/
└── unpacked/
    ├── unpacked_manifest.json
    ├── gameover/
    │   └── gameoverbg_es.pbg/
    │       ├── container.json
    │       ├── slot_0/
    │       │   └── 0000.nclr
    │       ├── slot_1/
    │       │   └── 0000.ncgr
    │       └── slot_6/
    │           └── 0000.nscr
    └── UI/
        └── ...
```

## Start with the 2D files

```powershell
py .\tools\unpack_containers\unpack_containers.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed" `
  --path "gameover/" `
  --path "pause/" `
  --path "UI/"
```

## Replace a previous extraction

```powershell
py .\tools\unpack_containers\unpack_containers.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed" `
  --force
```

## Generated metadata

Each container receives `container.json` with:

- source path and container size;
- slot pointers;
- table positions;
- entry offsets and sizes;
- detected format;
- output path;
- SHA-256;
- Nitro header-declared size;
- whether the Nitro size matches the table size.

The global `unpacked_manifest.json` aggregates every extracted resource and every failure.

Do not commit generated game data.
