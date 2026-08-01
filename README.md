# Semáforo Woffu

Firmware para un dispositivo ESP32 que muestra visualmente el estado de fichaje de [Woffu](https://woffu.com/) mediante un semáforo LED: 🔴 no fichado, 🟢 fichado, 🟡 estado desconocido.

## Hardware

| ESP32 (ELEGOO Type-C, DevKit genérico) | Módulo semáforo LED (R/Y/G) |
|---|---|
| ![Placa ESP32 DevKit con pinout serigrafiado](docs/img/esp32.jpg) | ![Módulo de semáforo LED de 3 colores](docs/img/semaforo.jpg) |

## Documentación

- [Requisitos.md](Requisitos.md) — alcance funcional, hardware, portal de configuración WiFi, OTA.
- [Arquitectura.md](Arquitectura.md) — máquina de estados, tareas, portal WiFi/NVS, cliente Woffu, OTA.
- [Plan.md](Plan.md) — plan de implementación: pasos hechos y pendientes.
- [tools/README.md](tools/README.md) — scripts auxiliares de desarrollo.

## Stack

- PlatformIO + framework Arduino (`arduino-esp32`).
- Librerías: WebServer y DNSServer (portal de configuración, incluidas en el core de `arduino-esp32`), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.
- CI/CD: GitHub Actions + `semantic-release` (versionado y publicación de releases a partir de Conventional Commits).

## Estado

MVP completo: `Config` (NVS), `LedController`, `WifiManager`, `TimeSync`, `ProvisioningPortal` (portal WiFi de configuración), `Scheduler`, `WoffuClient`, `AppStateMachine` (incluyendo el bucle de polling en `RUNNING`), `OtaUpdater` y el pipeline de release en CI. Pendiente de validar en hardware real; ver [Plan.md](Plan.md).
