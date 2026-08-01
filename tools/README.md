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
