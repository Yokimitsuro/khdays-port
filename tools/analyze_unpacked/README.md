# Unpacked resource analyzer

Analyzes `unpacked_manifest.json` and ranks the best candidates for the first 3D viewer.

It reports:

- total embedded resources;
- counts by Nitro format;
- containers with models;
- containers with textures;
- containers containing both `NSBMD` and `NSBTX`;
- animation packages;
- unknown embedded entries;
- ranked 3D candidates.

## First unpack every container

```powershell
py .\tools\unpack_containers\unpack_containers.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed" `
  --force
```

## Analyze the result

```powershell
py .\tools\analyze_unpacked\analyze_unpacked.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked"
```

Generated files:

```text
unpacked/
└── analysis/
    ├── report.md
    ├── summary.json
    └── containers.csv
```

Upload `report.md` or `summary.json` to choose the first model/texture package.

Do not commit generated game data or generated reports containing local paths.
