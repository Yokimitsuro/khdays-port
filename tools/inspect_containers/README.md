# KAPH/D2KP container inspector

Inspects decompressed KH Days containers without making assumptions about the final archive layout.

It:

- selects files beginning with `KAPH` or `D2KP`;
- scans for embedded Nitro signatures such as `BMD0`, `BTX0`, `BCA0`, `RGCN`, `RLCN`, and `RCSN`;
- dumps the first `0x100` bytes as little-endian 32-bit words;
- marks values that could be offsets;
- records whether a candidate offset points directly to a known signature;
- creates JSON, CSV, and Markdown reports.

It does **not** extract subfiles yet.

## Run against all containers

From the repository root:

```powershell
py .\tools\inspect_containers\inspect_containers.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed"
```

Output:

```text
decompressed/
└── container_analysis/
    ├── container_report.json
    ├── container_inventory.csv
    └── report.md
```

## Inspect only selected path groups

Character/model packages:

```powershell
py .\tools\inspect_containers\inspect_containers.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed" `
  --path "mi/ch/" `
  --path "ba/ch/"
```

2D packages:

```powershell
py .\tools\inspect_containers\inspect_containers.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed" `
  --path "gameover/" `
  --path "pause/" `
  --path "UI/"
```

## Why inspection comes before extraction

Historical reverse-engineering notes describe KAPH as a simple archive with embedded Nitro files, but the exact header interpretation must be verified against several real files.

The report lets us compare:

- `KAPH` headers with `0x28`, `0x34`, or `0xFFFFFFFF`;
- large `mi/ch/*` character packages;
- smaller battle packages under `ba/ch/*`;
- `D2KP` 2D containers;
- actual positions of `BMD0`, `BTX0`, animation, graphics, palette, and tilemap signatures.

After those offsets agree across multiple samples, the next tool can safely extract subfiles.
