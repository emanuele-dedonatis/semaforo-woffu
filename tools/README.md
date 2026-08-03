# Tools

Scripts auxiliares de desarrollo, no forman parte del firmware.

## `dev_version.py`

Hook de PlatformIO (`extra_scripts` en `platformio.ini`), no se ejecuta a mano. Embebe `FIRMWARE_VERSION` en builds locales a partir de git (`<último tag con patch+1>-dev.<hash corto>`); se salta si el build ya trae la versión fijada por `PLATFORMIO_BUILD_FLAGS` (caso de `tools/build_firmware.sh` en release). Ver `## OTA — detalle de implementación` en [Arquitectura.md](../Arquitectura.md).

## `check_status.py`

Reproduce el flujo de login + consulta de estado de fichaje contra la API de Woffu (ver [Arquitectura.md](../Arquitectura.md#cliente-woffu)), para verificar el comportamiento de la API antes/durante la implementación del firmware.

Requiere Python 3, sin dependencias externas.

```
python3 tools/check_status.py --username tu@email.com
```

- Si no se pasa `--password`, se pide de forma interactiva (no queda en el historial de la shell).
- `-v` / `--verbose` muestra por stderr el JSON completo de login y de slots.
- Por stdout imprime únicamente `FICHADO` o `NO_FICHADO`.

## `toggle_sign.py`

Verifica el flujo de **fichar/defichar** (`POST /api/svc/signs/signs`) contra la API real de Woffu, antes/durante la implementación de `WoffuClient::toggleSign()` en el firmware (fichaje por NFC, ver `Arquitectura.md`).

**AVISO**: a diferencia de `check_status.py` (solo lectura), este script **tiene un efecto real** sobre la cuenta: si estás fichado te desficha, y viceversa (el propio backend de Woffu decide la dirección, no hay parámetro para elegirla). Pide confirmación interactiva antes del POST, salvo que se pase `--yes`.

Requiere Python 3, sin dependencias externas.

```
python3 tools/toggle_sign.py --username tu@email.com
```

- Muestra el estado antes y después del toggle (reutilizando la misma lógica que `check_status.py`).
- `-v` / `--verbose` muestra por stderr el UserId, el cuerpo de la respuesta del POST y los slots completos.
- `--yes` salta la confirmación interactiva (útil para pruebas repetidas).
