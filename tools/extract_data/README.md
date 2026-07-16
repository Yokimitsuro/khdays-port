# Game data extractor

Extracts NitroFS and several system blocks locally from a Nintendo DS ROM provided by the user.

The tool neither downloads nor distributes game content. The output is written under `data/`, which must stay ignored by Git.

## Prerequisite

Run the verifier first:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\path\game.nds"
```

Add to `supported_roms.json` only the SHA-256 of a clean, confirmed ROM.

## Extract

From the repository root:

```powershell
py .\tools\extract_data\extract_data.py "E:\path\game.nds"
```

The output is created in:

```text
data/
└── extracted/
    └── <first 16 characters of the SHA-256>/
        ├── metadata.json
        ├── manifest.json
        ├── system/
        │   ├── arm9.bin
        │   ├── arm7.bin
        │   ├── arm9_overlay_table.bin
        │   ├── arm7_overlay_table.bin
        │   ├── fnt.bin
        │   └── fat.bin
        └── nitrofs/
            └── ...
```

Some blocks may not exist in certain ROMs and are skipped when their size is zero.

## Unregistered ROM

When `supported_roms.json` contains ROMs but the hash does not match, extraction stops.

To investigate an unknown ROM temporarily:

```powershell
py .\tools\extract_data\extract_data.py "E:\path\game.nds" --allow-unknown
```

Do not register or publish results from a modified ROM or one of unknown provenance.

## Repeating an extraction

The tool does not silently overwrite a previous extraction:

```powershell
py .\tools\extract_data\extract_data.py "E:\path\game.nds" --force
```

## Generated files

### `metadata.json`

Contains:

- SHA-256 and size of the ROM;
- support status;
- the main header data;
- NitroFS file count.

### `manifest.json`

Contains:

- path, position, size and SHA-256 of each extracted file;
- equivalent information for ARM9, ARM7 and the overlay tables.

The manifest will let the following tools locate resources without relying on manually hard-coded paths.

## Exit codes

- `0`: extraction successful.
- `1`: read, structure or write error.
- `2`: ROM not registered.
