# Servidor de control remoto

Backend + web app para ver y controlar el semáforo a distancia: el dispositivo (en la oficina) abre una conexión WebSocket saliente hacia este servidor (en casa, detrás del nginx del repo `infra`), y la web app habla con el mismo servidor. Diseño y protocolo en [Arquitectura.md](../Arquitectura.md#control-remoto).

```
ESP32 ──wss──▶ nginx (woffu.ededona.es:443) ──▶ app.py (127.0.0.1:8095) ◀──wss── navegador
```

El firmware trae por defecto `wss://woffu.ededona.es/ws/device` como servidor (se puede cambiar o vaciar desde el portal del dispositivo o desde la propia web app).

Desde la web app:

- **Panel**: estado en Woffu (dentro/fuera) y **saldo anual de horas** en vivo (hasta ayer + lo trabajado hoy − la jornada de hoy; si estás dentro y en negativo, a qué hora queda a cero), color real del semáforo, datos del dispositivo; **fichar entrada/salida** (se comprueba antes el estado real en Woffu, igual que el fichaje automático); **color fijo** (rojo/ámbar/verde/todos/apagado, fijo o parpadeando) que oculta tu estado, persistente también tras reiniciar, hasta volver a **automático**.
- **Actividad**: fichajes (NFC, horario, remoto), conexiones, cambios, errores.
- **Ajustes**: todo lo que se configura en el portal del dispositivo: password, horario, fichaje automático, actualizaciones (comprobar, instalar, automáticas), WiFi, Woffu, servidor, aprender tarjeta NFC, restablecer de fábrica.
- **Debug**: log en vivo del dispositivo (el mismo que sale por serie), último estado en JSON, reiniciar.

## Acceso

- Usuario: nombre del dispositivo (`Semaforo-XXXXXX`, el SSID de su portal de configuración).
- Password: la del dispositivo. **Por defecto no tiene** (se entra dejándola vacía) y su red WiFi de configuración es abierta. Se pone desde el portal o desde Ajustes, y protege las dos cosas.

## Registro de dispositivos

En el servidor no se configura ningún dispositivo: cada uno se registra solo la primera vez que se conecta. Se autentica con su nombre y un token secreto aleatorio que genera él mismo (y que nunca cambia, ni con un reset de fábrica); el servidor guarda el hash del primer token que ve para cada nombre y desde entonces rechaza cualquier otro. El dispositivo le comunica además su password (el servidor guarda solo un hash PBKDF2), con la que se protege la web app.

El registro vive en `server/data/devices.json`. Si un dispositivo pierde su token (borrado completo de la flash), se borra su entrada de ese fichero y se reinicia el contenedor para que pueda volver a registrarse.

## Configuración

`server/.env` (no versionado, ver [.env.example](.env.example)):

- `SESSION_SECRET` — clave para firmar las cookies de sesión (si falta, las sesiones caducan en cada reinicio).
- `MAX_DEVICES` — máximo de dispositivos registrados (20 por defecto).

## Despliegue (Raspberry)

```sh
cp server/.env.example server/.env   # y poner un SESSION_SECRET
server/deploy.sh                     # docker compose up -d --build
```

Publicación: entrada `semaforo-woffu` en `proxy_sites` del repo `infra` (con `websockets: true`) + `ansible-playbook site.yml`, y CNAME `woffu` → `ededona.es` en IONOS.

Los cambios en `server/` no publican release de firmware: el workflow de release solo se dispara con cambios en `firmware/`, y además los commits con scope `server` no cuentan para semantic-release (usa `feat(server): …`, `fix(server): …`).

## Desarrollo local

```sh
pip install -r server/requirements.txt
SESSION_SECRET=dev DATA_DIR=/tmp/semaforo-data python3 server/app.py
python3 server/tools/fake_device.py   # simulador del dispositivo, mismo protocolo
# http://127.0.0.1:8095  (usuario Semaforo-A1B2C3, password vacía)
```

## Seguridad

- Sin password, cualquiera que conozca el nombre del dispositivo (visible como red WiFi durante la ventana del portal) puede manejarlo: ponle una password desde Ajustes.
- Login con limitación: 5 fallos en 15 min bloquean la IP (también los tokens de dispositivo erróneos). La IP real se toma del último valor de `X-Forwarded-For`, el que añade nginx.
- Cookie de sesión firmada (HMAC; incluye el hash de la password, así que cambiarla cierra las sesiones abiertas), `HttpOnly`, `Secure`, `SameSite=Strict`, 30 días. El WebSocket de la web app comprueba además el `Origin`.
- Solo se reenvían al dispositivo los comandos de una lista cerrada. Las passwords de WiFi y Woffu se pueden cambiar desde la web app pero nunca salen del dispositivo.
- El estado (último estado, actividad, log) vive en memoria: reiniciar el contenedor lo borra (el dispositivo manda su estado de nuevo al reconectar).
