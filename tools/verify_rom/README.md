# ROM verifier

Herramienta local para inspeccionar una ROM de Nintendo DS proporcionada por el usuario y comprobarla mediante SHA-256.

No modifica, copia ni distribuye la ROM.

## Uso

Desde la raíz de `khdays-port`:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\ruta\Kingdom Hearts 358-2 Days.nds"
```

Para obtener una entrada candidata para la base de datos:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\ruta\game.nds" --print-entry
```

Para obtener la salida completa en JSON:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\ruta\game.nds" --json
```

## Registrar una ROM compatible

1. Ejecuta el verificador sobre una copia conocida y limpia.
2. Confirma manualmente la región y revisión.
3. Ejecuta con `--print-entry`.
4. Copia la entrada mostrada dentro de la lista `roms` de `supported_roms.json`.
5. No registres dumps modificados, parcheados o de procedencia desconocida.

Ejemplo de estructura:

```json
{
  "schema_version": 1,
  "roms": [
    {
      "name": "Kingdom Hearts 358/2 Days (Europe)",
      "game_code": "YKGP",
      "rom_version": 0,
      "sha256": "HASH_REAL_DE_LA_ROM_LIMPIA"
    }
  ]
}
```

El SHA-256 debe ser real. No inventes ni reutilices hashes de otra revisión.

## Códigos de salida

- `0`: ROM reconocida y soportada.
- `1`: error de lectura, ruta o JSON.
- `2`: ROM no registrada.
