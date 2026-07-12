# NSBMD section inspector

The previous container report counts top-level Nitro files only. An `.nsbmd`
file can contain both:

```text
MDL0 — model data
TEX0 — texture and palette data
```

Therefore, a container does not need a separate `.nsbtx` to be self-contained.

This tool scans every `.nsbmd`, reads its Nitro section-offset table, and ranks
models that already contain an embedded `TEX0`.

## Run

```powershell
py .\tools\inspect_nsbmd\inspect_nsbmd.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked"
```

Generated files:

```text
unpacked/
└── nsbmd_analysis/
    ├── report.md
    ├── summary.json
    └── models.csv
```

Upload `report.md` after running it. The best first-viewer candidate should be a
small file with both `MDL0` and `TEX0`.
