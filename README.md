# Semáforo Woffu

Firmware para un dispositivo ESP32 que muestra visualmente el estado de fichaje de [Woffu](https://woffu.com/) mediante un semáforo LED: 🔴 no fichado, 🟢 fichado, 🟡 estado desconocido.

## Documentación

- [Requisitos.md](Requisitos.md) — alcance funcional, hardware, provisioning BLE, OTA.
- [Arquitectura.md](Arquitectura.md) — máquina de estados, tareas, esquema BLE/NVS, cliente Woffu, OTA.
- [tools/README.md](tools/README.md) — scripts auxiliares de desarrollo.

## Stack

- PlatformIO + framework Arduino (`arduino-esp32`).
- Librerías previstas: NimBLE-Arduino (BLE), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.

## Estado

En fase de diseño, aún sin código de firmware.
