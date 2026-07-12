# Game data extractor

Extrae localmente NitroFS y varios bloques de sistema desde una ROM de Nintendo DS proporcionada por el usuario.

La herramienta no descarga ni distribuye contenido del juego. El resultado se guarda bajo `data/`, que debe permanecer ignorado por Git.

## Requisito previo

Ejecuta primero el verificador:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\ruta\game.nds"
```

Añade a `supported_roms.json` únicamente el SHA-256 de una ROM limpia y confirmada.

## Extraer

Desde la raíz del repositorio:

```powershell
py .\tools\extract_data\extract_data.py "E:\ruta\game.nds"
```

La salida se crea en:

```text
data/
└── extracted/
    └── <16 primeros caracteres del SHA-256>/
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

Algunos bloques pueden no existir en determinadas ROMs y se omitirán cuando su tamaño sea cero.

## ROM no registrada

Cuando `supported_roms.json` contiene ROMs pero el hash no coincide, la extracción se detiene.

Para investigar temporalmente una ROM desconocida:

```powershell
py .\tools\extract_data\extract_data.py "E:\ruta\game.nds" --allow-unknown
```

No registres ni publiques resultados de una ROM modificada o de procedencia desconocida.

## Repetir una extracción

La herramienta no sobrescribe una extracción previa de forma silenciosa:

```powershell
py .\tools\extract_data\extract_data.py "E:\ruta\game.nds" --force
```

## Archivos generados

### `metadata.json`

Contiene:

- SHA-256 y tamaño de la ROM;
- estado de compatibilidad;
- datos principales de la cabecera;
- recuento de archivos NitroFS.

### `manifest.json`

Contiene:

- ruta, posición, tamaño y SHA-256 de cada archivo extraído;
- información equivalente para ARM9, ARM7 y tablas de overlays.

El manifiesto permitirá que las siguientes herramientas localicen recursos sin depender de rutas codificadas manualmente.

## Códigos de salida

- `0`: extracción correcta.
- `1`: error de lectura, estructura o escritura.
- `2`: ROM no registrada.
