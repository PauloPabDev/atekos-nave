# Comandos de consola serie

Estado: **aprobado e implementado** desde el firmware 1.2.0; al día con la 1.3.0 (código en [`src/Console.cpp`](../src/Console.cpp)).

## Uso

- Abre el monitor con `pio device monitor --echo` (115200 baudios). Escribe un comando por línea y pulsa Enter.
- Las respuestas empiezan por `[CMD]`. Los argumentos van separados por un espacio.
- Línea máxima: 1024 caracteres. Una línea más larga se ignora entera, para no guardar un token cortado.
- Si sale `Could not exclusively lock port`, es que otro monitor tiene abierto el puerto (por ejemplo el de VS Code). Ciérralo primero.
- La consola se lee cuando la placa termina de arrancar, unos 10 s tras encenderla, y también durante el portal WiFi.

## Comandos

En la columna **Portal**, ✅ significa que el comando también funciona con el punto de acceso `MetaDisplay-Setup` activo. Si lanzas uno sin ✅ durante el portal, responde: `'<cmd>' no es valido con el punto de acceso activo (portal WiFi)`.

| Comando | Portal | Qué hace |
| --- | --- | --- |
| `help` | ✅ | Lista los comandos que valen en el modo actual. |
| `id` | ✅ | Imprime el `deviceId` (p. ej. `md-34B7DA01ABCD`) que hay que registrar en el servidor. |
| `status` | | Firmware, id, tiempo encendida, memoria libre y bloque libre más grande, WiFi (SSID, dBm, IP), estado del WSS con el último error y el siguiente reintento, plantilla, eventos sin confirmar y, si está llegando un mensaje troceado (`frag`), cuántos trozos y bytes lleva. |
| `config` | ✅ | URL, token enmascarado (`****` + últimos 4), si la CA es privada o pública, y la clave del portal. |
| `set-url <wss://...>` | ✅ | Valida y guarda la URL, y reconecta el WSS. Durante el portal solo la guarda: usa `reboot` para salir del portal. |
| `set-token <token>` | ✅ | Igual que `set-url`, pero con el token. El token queda visible en el historial del monitor. |
| `portal` | | Abre el portal sin borrar nada (con los campos rellenos) y reinicia al terminar. |
| `wifi-reset` | ✅ | Borra solo la WiFi guardada y reinicia. La URL y el token se conservan; tras ~18 s sin red abre el portal. |
| `factory-reset` | ✅ | Borra la URL, el token, las CA, la clave del portal, la WiFi y la plantilla (formatea LittleFS), y reinicia en el portal. |
| `reconnect` | | Cierra el WSS y reconecta al momento. |
| `wss-off` / `wss-on` | | Desconecta el WSS y no reconecta hasta `wss-on`. |
| `log wss on\|off` | | Imprime cada JSON que entra (`[WSS <]`) y sale (`[WSS >]`). El token nunca aparece. Se desactiva al reiniciar. |
| `send <json>` | | Envía un objeto JSON tal cual al servidor. Antes valida que sea JSON. |
| `press` / `press-long` | | Simula una pulsación corta o larga del botón 0 (BOOT). Ejecuta de verdad la acción en el servidor. |
| `actions` | | Lista las acciones de la plantilla, con `boton=N corta\|larga` en las de botón físico. Si la plantilla no tiene acciones de botón, la marcada con `*` es la que ejecuta el BOOT. |
| `brightness <0-50>` | | Cambia el brillo. No se guarda: al reiniciar vuelve al 18 %. |
| `template` | | Imprime el JSON de la plantilla guardada, sin los píxeles: el `data` de cada imagen sale como `@offset:longitud`, su posición en `/template.raw`. |
| `values` | | Imprime los valores actuales. |
| `clear-template` | | Borra la plantilla en caché y reconecta para que el servidor la reenvíe. |
| `reboot` | ✅ | Reinicia la placa. |

Ningún comando pide confirmación, tampoco los que borran datos.

## Logs automáticos del WSS

Salen siempre, sin activar nada:

```text
[WSS 8s] connected to tu-servidor.example:443
[WSS 8s] hello sent, deviceId=md-34B7DA01ABCD
[WSS 32s] disconnected: console_wss_off (was up 26s, no retry until wss-on)
[WSS 8s] connect failed: http: 'HTTP/1.1 401 Unauthorized' (retry in 2144ms)
```

Causas posibles:

- de desconexión: `server_close code=N reason=...`, `wifi_lost`, `socket_closed`, `pong_timeout`, `send_failed`, `bad_frame`, `frame_too_large`, `rx_buffer_full`, `reconfigured`, `clear_template`, `console_reconnect` y `console_wss_off`;
- de conexión fallida: `tls_connect: <error>`, `http: '<línea de estado>'`, `invalid_url_or_token` y `bad_handshake_headers`.

## Decisiones tomadas

1. La consola también funciona durante el portal WiFi. Ahí solo se aceptan los comandos marcados con ✅; el resto responde con un error.
2. No se pide confirmación para los comandos destructivos.
3. Los nombres de los comandos van en `kebab-case`, en inglés.

## Probado en la placa (2026-11-08)

- **Probados:** `help`, `id`, `status`, `config`, `actions`, `template`, `values`, `press`, `send` (válido e inválido), `brightness` (válido y fuera de rango), `log wss on/off`, `wss-off`/`wss-on`, `reconnect`, `clear-template` (el servidor reenvió la plantilla), `set-url` (inválida, y válida con la misma URL), `set-token` vacío, línea de más de 1024 caracteres, y `portal` con `press`, `status`, `help`, `id`, `set-url` y `reboot` dentro del portal.
- **Sin probar**, para no borrar la configuración real o porque cambia el token: `wifi-reset`, `factory-reset`, `set-token` con un token válido y `press-long`.
