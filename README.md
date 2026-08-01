# Semáforo Woffu

Firmware para un dispositivo ESP32 que muestra visualmente el estado de fichaje de [Woffu](https://woffu.com/) mediante un semáforo LED: 🔴 no fichado, 🟢 fichado, 🟡 estado desconocido.

## Documentación

- [Requisitos.md](Requisitos.md) — alcance funcional, hardware, portal de configuración WiFi, OTA.
- [Arquitectura.md](Arquitectura.md) — máquina de estados, tareas, portal WiFi/NVS, cliente Woffu, OTA.
- [tools/README.md](tools/README.md) — scripts auxiliares de desarrollo.

## Stack

- PlatformIO + framework Arduino (`arduino-esp32`).
- Librerías: WebServer y DNSServer (portal de configuración, incluidas en el core de `arduino-esp32`), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.

## Estado

En desarrollo activo. Implementados: `Config` (NVS), `LedController`, `WifiManager`, `TimeSync`, `ProvisioningPortal` (portal WiFi de configuración). Pendientes: `WoffuClient`, `OtaUpdater`.
