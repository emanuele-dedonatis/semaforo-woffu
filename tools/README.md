# Tools

Scripts auxiliares de desarrollo, no forman parte del firmware.

## `check_status.py`

Reproduce el flujo de login + consulta de estado de fichaje contra la API de Woffu (ver [Arquitectura.md](../Arquitectura.md#cliente-woffu)), para verificar el comportamiento de la API antes/durante la implementación del firmware.

Requiere Python 3, sin dependencias externas.

```
python3 tools/check_status.py --username tu@email.com
```

- Si no se pasa `--password`, se pide de forma interactiva (no queda en el historial de la shell).
- `-v` / `--verbose` muestra por stderr el JSON completo de login y de slots.
- Por stdout imprime únicamente `FICHADO` o `NO_FICHADO`.
