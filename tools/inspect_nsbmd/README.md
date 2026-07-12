# NSBMD section inspector — corrected version

This version fixes the parser assumption that caused every real NSBMD to fail.

## The mistake

The previous parser assumed that the section-offset table had to fit inside
the Nitro `header_size` field.

For BMD0 files, `header_size` is commonly `0x10`, while the section-offset
table begins at `0x10` and follows that fixed header. Therefore, a file with
one section can have:

```text
0x00-0x0F  fixed Nitro header
0x10-0x13  section 0 offset
0x14...    first section
```

The offset table is valid even though its end is greater than `header_size`.

## Run again

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

The report should now list parsed `MDL0` and, where present, embedded `TEX0`
sections instead of reporting 533 identical failures.
