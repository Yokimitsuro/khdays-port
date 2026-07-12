# Extracted data analyzer

Analiza los archivos generados por `tools/extract_data/` y crea un inventario por:

- extensión;
- firma mágica;
- formato conocido;
- categoría;
- tamaño;
- posible compresión Nitro;
- entropía aproximada;
- prioridad para el primer visor de recursos.

No modifica los archivos extraídos.

## Uso

Primero localiza la carpeta creada por el extractor:

```text
data/extracted/<16 caracteres del SHA-256>/
```

Desde la raíz del repositorio:

```powershell
py .\tools\analyze_data\analyze_data.py ".\data\extracted\<ID>"
```

También puedes indicar otra carpeta de salida:

```powershell
py .\tools\analyze_data\analyze_data.py ".\data\extracted\<ID>" `
  --output ".\data\reports\<ID>"
```

## Salida

```text
analysis/
├── asset_inventory.csv
├── summary.json
└── report.md
```

### `asset_inventory.csv`

Una fila por archivo:

- `file_id`
- `path`
- `extension`
- `size`
- `sha256`
- primeros bytes en ASCII y hexadecimal
- formato detectado
- categoría
- indicador de compresión
- entropía
- puntuación y motivo

Puedes abrirlo directamente con Excel o LibreOffice.

### `summary.json`

Resumen procesable por otras herramientas:

- recuento por formato;
- recuento por categoría;
- recuento por extensión;
- archivos comprimidos;
- candidatos principales.

### `report.md`

Informe legible con los candidatos recomendados para el primer visor.

## Firmas inicialmente reconocidas

### Nitro 3D

- `BMD0` — NSBMD, modelo
- `BTX0` — NSBTX, texturas
- `BCA0` — NSBCA, animación esquelética
- `BTA0`, `BTP0`, `BMA0`, `BVA0` — animaciones auxiliares

### Nitro 2D

- `RGCN` — NCGR, gráficos
- `RLCN` — NCLR, paleta
- `RCSN` — NSCR, tilemap
- `RECN` — NCER, sprites/celdas
- `RNAN` — NANR, animación

### Otros

- `NARC`
- `SDAT`, `SSEQ`, `SSAR`, `SBNK`, `SWAR`, `SWAV`
- PNG, JPEG, BMP, RIFF, Ogg, ZIP y Gzip
- cabeceras Nitro LZ77, Huffman y RLE

## Qué candidato elegir

El primer objetivo recomendado es:

1. un `NSBTX` pequeño, si aparece;
2. un par `NCGR` + `NCLR`;
3. un `NSBMD` después de tener texturas;
4. un `NARC` solo cuando necesitemos abrir archivos contenedores.

La herramienta prioriza recursos gráficos reconocidos sobre archivos grandes desconocidos.
