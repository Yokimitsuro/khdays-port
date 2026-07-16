# ROM verifier

Local tool to inspect a Nintendo DS ROM provided by the user and check it via SHA-256.

It does not modify, copy or distribute the ROM.

## Usage

From the `khdays-port` root:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\path\Kingdom Hearts 358-2 Days.nds"
```

To obtain a candidate entry for the database:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\path\game.nds" --print-entry
```

For the full output as JSON:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\path\game.nds" --json
```

## Registering a supported ROM

1. Run the verifier against a known clean copy.
2. Manually confirm the region and revision.
3. Run with `--print-entry`.
4. Copy the printed entry into the `roms` list in `supported_roms.json`.
5. Do not register modified or patched dumps, or dumps of unknown provenance.

Example structure:

```json
{
  "schema_version": 1,
  "roms": [
    {
      "name": "Kingdom Hearts 358/2 Days (Europe)",
      "game_code": "YKGP",
      "rom_version": 0,
      "sha256": "REAL_HASH_OF_THE_CLEAN_ROM"
    }
  ]
}
```

The SHA-256 must be real. Do not invent hashes or reuse them from another revision.

## Exit codes

- `0`: ROM recognized and supported.
- `1`: read, path or JSON error.
- `2`: ROM not registered.
