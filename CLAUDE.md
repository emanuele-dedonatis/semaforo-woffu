# CLAUDE.md

Guía para Claude Code al trabajar en este repositorio.

## Qué es este proyecto

Firmware para un dispositivo ESP32 que muestra el estado de fichaje de [Woffu](https://woffu.com/) mediante un semáforo LED (rojo/verde/ámbar). Ver [Requisitos.md](Requisitos.md) para el detalle funcional completo (MVP, hardware, OTA, portal de configuración WiFi, seguridad).

## Stack

- PlatformIO + framework Arduino (`arduino-esp32`).
- Librerías: WebServer y DNSServer (portal de configuración, incluidas en el core de `arduino-esp32`), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.
- CI/CD: GitHub Actions + `semantic-release` (Node), disparado en push a `main` — ver `## CI/CD` en Arquitectura.md.

## Mantenimiento de la documentación

`README.md` y este `CLAUDE.md` deben mantenerse siempre actualizados: cualquier cambio relevante en stack, estructura del proyecto o alcance debe reflejarse en ambos como parte del mismo trabajo, no como tarea aparte.

## Portal de configuración: limitaciones del navegador cautivo

El popup automático "Iniciar sesión en red" (Captive Network Assistant en iOS, pantalla equivalente en Android) es el flujo de acceso preferido al portal — no se debe diseñar asumiendo que el usuario abrirá un navegador normal. Ese WebView es muy limitado: en iOS normalmente **no ejecuta JavaScript** y el soporte de `<datalist>` es pobre o nulo en varios sistemas. Cualquier funcionalidad del portal (como la sugerencia de redes WiFi) debe funcionar sin depender de JS y usando controles HTML nativos con soporte universal (p. ej. `<select>` en vez de `<datalist>`).

## Convenciones de commits

Este repo usa **Conventional Commits** de forma estricta, para poder automatizar versionado (semantic-release o similar) a partir del historial. Formato:

```
<tipo>(<scope opcional>): <descripción>
```

Tipos habituales: `feat`, `fix`, `docs`, `refactor`, `chore`, `test`, `build`, `ci`. Usa `!` o un footer `BREAKING CHANGE:` para cambios incompatibles.

## Git: commit sí, push no

Puedes hacer commit de los cambios de forma automática (sin pedir confirmación), pero **nunca hagas push a menos que el usuario lo pida explícitamente**: cada push a `main` dispara una release automática (ver CI/CD arriba), y el usuario controla cuándo quiere publicarla.
