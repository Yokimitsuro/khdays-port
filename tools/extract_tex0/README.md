# TEX0 texture extractor

Extracts embedded Nintendo DS `TEX0` textures from:

- `.nsbmd` / `BMD0`;
- `.nsbtx` / `BTX0`.

The output is standard RGBA PNG plus a JSON manifest.

## Supported texture formats

- A3I5;
- 4-color indexed;
- 16-color indexed;
- 256-color indexed;
- A5I3;
- direct BGR555 color.

Nintendo DS 4×4 compressed textures are detected but not decoded yet.

## Extract textures from the larger test model

```powershell
py .\tools\extract_tex0\extract_tex0.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\mi\ch\03\slot_7\0000.nsbmd" `
  --output ".\data\preview\mi_ch_03_textures"
```

Expected texture names include:

```text
ve_00
ve_01
ve_02
ve_hair00
ve_hair01
ve_robe00
ve_robe01
```

## Extract the small model

```powershell
py .\tools\extract_tex0\extract_tex0.py `
  ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\mi\ch\2A\slot_7\0000.nsbmd" `
  --output ".\data\preview\mi_ch_2A_textures"
```

This model contains one texture named `006arm`.

## Important layout detail

The texture pixel stream is linear row-major data. It must not be rearranged
as 8×8 tiles; doing so produces visibly scrambled textures.
