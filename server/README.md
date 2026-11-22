# Servidor de control remoto

Backend + web app para ver y controlar el semáforo a distancia: el dispositivo (en la oficina) abre una conexión WebSocket saliente hacia este servidor (en casa, detrás del nginx del repo `infra`), y la web app habla con el mismo servidor. Diseño y protocolo en [Arquitectura.md](../Arquitectura.md#control-remoto).

```
ESP32 ──wss──▶ nginx (woffu.ededona.es:443) ──▶ app.py (127.0.0.1:8095) ◀──wss── navegador
```

Desde la web app:

- ver el estado en Woffu (dentro/fuera), el color real del semáforo y los datos del dispositivo (firmware, WiFi, memoria, horario…);
- **fichar entrada/salida** (se comprueba antes el estado real en Woffu, igual que el fichaje automático);
- **poner un color fijo** (rojo/ámbar/verde/todos/apagado, fijo o parpadeando) que oculta tu estado, persistente también tras reiniciar, hasta volver a **automático**;
- ver la actividad reciente (fichajes por NFC/horario/remoto, conexiones, errores);
- pestaña **Debug**: log en vivo del dispositivo (el mismo que sale por serie), último estado en JSON, reiniciar y actualizar firmware (OTA).

## Acceso

Usuario: nombre del dispositivo (`Semaforo-XXXXXX`, el SSID de su portal de configuración). Password: la de esa red WiFi (8 dígitos, sale en el log serie al abrir el portal). El dispositivo usa la misma pareja para autenticarse al conectar.

## Configuración

`server/.env` (no versionado, ver [.env.example](.env.example)):

- `DEVICES` — dispositivos autorizados, `Nombre:password` separados por comas.
- `SESSION_SECRET` — clave para firmar las cookies de sesión (si falta, las sesiones caducan en cada reinicio).

En el dispositivo: en el portal de configuración, campo **Servidor de control remoto** = `wss://woffu.ededona.es/ws/device`, y guardar.

## Despliegue (Raspberry)

```sh
cp server/.env.example server/.env   # y rellenarlo
server/deploy.sh                     # docker compose up -d --build
```

Publicación: entrada `semaforo-woffu` en `proxy_sites` del repo `infra` (con `websockets: true`) + `ansible-playbook site.yml`, y CNAME `woffu` → `ededona.es` en IONOS.

Los cambios en `server/` no publican release de firmware: el workflow de release solo se dispara con cambios en `firmware/`, y además los commits con scope `server` no cuentan para semantic-release (usa `feat(server): …`, `fix(server): …`).

## Desarrollo local

```sh
pip install -r server/requirements.txt
DEVICES=Semaforo-A1B2C3:12345678 SESSION_SECRET=dev python3 server/app.py
python3 server/tools/fake_device.py   # simulador del dispositivo, mismo protocolo
# http://127.0.0.1:8095  (usuario Semaforo-A1B2C3, password 12345678)
```

## Seguridad

- Login con limitación: 5 fallos en 15 min bloquean la IP (también para la autenticación del dispositivo). La IP real se toma del último valor de `X-Forwarded-For`, el que añade nginx.
- Cookie de sesión firmada (HMAC, incluye la password, así que cambiarla invalida las sesiones), `HttpOnly`, `Secure`, `SameSite=Strict`, 30 días. El WebSocket de la web app comprueba además el `Origin`.
- Solo se reenvían al dispositivo los comandos de una lista cerrada.
- **Ojo**: la password del dispositivo se deriva de su MAC con un algoritmo público (está en este repo) y el nombre lleva la mitad de la MAC, así que quien vea el SSID del portal y conozca el código podría calcularla. Ver Arquitectura.md, `## Control remoto`.
- Todo el estado vive en memoria: reiniciar el contenedor borra el historial de actividad/log (el dispositivo manda su estado de nuevo al reconectar).
