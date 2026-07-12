# Nitro LZ decompressor

Decompresses Nintendo DS LZ10 (`0x10`) and LZ11 (`0x11`) files found in an extraction created by `tools/extract_data/`.

The original extracted files are not modified.

## Run

From the repository root:

```powershell
py .\tools\decompress_data\decompress_data.py `
  ".\data\extracted\<EXTRACTION_ID>"
```

Output:

```text
data/extracted/<EXTRACTION_ID>/decompressed/
├── decompressed_manifest.json
└── ...
```

A source path ending in `.z` is written without the final `.z`:

```text
nitrofs/ba/ch/la/li_e2.p.z
→
decompressed/ba/ch/la/li_e2.p
```

Other compressed paths receive `.decompressed`.

## Replace an existing output

```powershell
py .\tools\decompress_data\decompress_data.py `
  ".\data\extracted\<EXTRACTION_ID>" --force
```

## Manifest

`decompressed_manifest.json` records:

- source path;
- output path;
- compression type;
- compressed and decompressed sizes;
- SHA-256;
- first decompressed bytes;
- failures.

The inner magic bytes help identify the actual KH Days resource formats after decompression.

## Next analysis

The existing `analyze_data.py` expects a standard extraction manifest. For the next milestone, either:

1. inspect `decompressed_manifest.json`, or
2. extend the analyzer to accept decompressed manifests.

Do not commit generated decompressed game data.
