# MetaDisplay Protocol Specification 1.0

**Estado:** contrato técnico de referencia · **Revisión:** 2026-11-08 (firmware 1.2.0) · **Hardware objetivo:** Waveshare ESP32-C6-LCD-1.47 (ST7789, sin touch, botón BOOT)

Archivos de esta carpeta:

- [`CONTRATO.md`](CONTRATO.md): este documento.
- [`asyncapi.yaml`](asyncapi.yaml): mensajes WSS entre dispositivo y servidor.
- [`openapi.yaml`](openapi.yaml): API REST opcional del servidor de ejemplo.

Comandos de la consola serie para diagnosticar el dispositivo: [`../docs/comandos-consola.md`](../docs/comandos-consola.md).

> **Firmware 1.3.0: contrato 1.1.** Este documento describe el contrato 1.0. El firmware 1.3.0 implementa además el contrato 1.1 de atekos: rotación, formas nuevas, grosor, imágenes, `params`/`binds`/`when`/`format`, animación, acciones de botón físico, `display.buttons` en el `hello`, `source`/`button`/`press` en `touch.tap` y mensajes troceados (`frag`/`frag.ack`). Ya no hay tope de 45 elementos ni de 24 acciones. La referencia de esos campos es el contrato publicado: https://atekos.toxolinko.site/contrato.

> **Cambios respecto a la versión para la Touch-LCD-1.47.** El número de protocolo sigue siendo `1` y los mensajes son compatibles. Lo que cambia es el hardware:
> - Las acciones se disparan con el botón BOOT (sección 5), no con la pantalla táctil.
> - `device.hello` envía `display.touch=false`.
> - `touch.tap` añade `"source":"button"`.

## 1. Objetivos y responsabilidades

El cliente ESP32-C6 ejecuta un motor gráfico declarativo de 172×320 píxeles y mantiene una conexión WebSocket autenticada. El servidor determina **la plantilla**, **los datos**, **las acciones disponibles** y **la respuesta de las acciones**. No se descarga ni ejecuta JavaScript/código desde el servidor: se interpretan exclusivamente instrucciones declarativas con tipos permitidos.

| Componente | Responsabilidad |
| --- | --- |
| Dispositivo | Wi-Fi, TLS/WSS, validación JSON, caché, dibujo, botón de acciones, retransmisión y recuperación |
| Servidor del cliente | Autenticación, registro del `deviceId`, plantilla activa, datos, reglas de negocio, resultados de acciones, renovación de su TLS |
| Propietario | Configurar la URL y el token, y la CA privada cuando aplique; registrar el `deviceId` en su servidor |
| Fabricante | Firmar actualizaciones de firmware (opcional) y emitir nuevas versiones de CA públicas mediante OTA |

## 2. Transporte y seguridad

- **Endpoint:** `wss://DOMINIO/ws/device` (HTTPS/443 con TLS validado). Se admite otro puerto con `wss://DOMINIO:PUERTO/...`. No se admite IPv6 literal.
- **Autenticación:** cabecera HTTP `Authorization: Bearer <DEVICE_TOKEN>` durante el *upgrade* RFC6455. **No** se envía el token en la query string. El token tiene como máximo 380 caracteres, sin saltos de línea.
- **Certificados públicos:** almacén de CA públicas integrado en Arduino-ESP32. **No** se deshabilita la verificación de hostname ni de certificados.
- **Certificados privados:** se importa una **CA DER codificada en base64** (no una clave privada) durante la configuración en WiFiManager. El firmware la convierte a PEM y configura TLS. Renovar el certificado hoja firmado por la misma CA no requiere tocar el dispositivo.
- **Rotación a una CA privada distinta:**
  1. Enviar `trust.stage` por el WSS ya autenticado. Queda una etapa con la CA antigua más la nueva.
  2. Cambiar el certificado del servidor.
  3. Cuando la conexión con la CA nueva ya funcione, enviar `trust.activate`. El ESP32 conserva únicamente la CA nueva.
  4. Si no logra otra conexión autenticada tras 90 s, revierte a la antigua. Hay que probar este procedimiento con el proveedor TLS del cliente.
- **Producción:** proteger físicamente los dispositivos y activar Secure Boot y Flash Encryption cuando el flujo de fabricación lo permita. La NVS sin cifrar **no** protege un token contra alguien con acceso físico a la flash ni al puerto USB (la consola serie permite leer la configuración y cambiarla).
- **OTA:** la imagen se descarga por HTTPS y se valida contra SHA-256 y una firma ECDSA P-256 de la clave del fabricante. Si no hay clave pública configurada, el comando falla de forma segura.
- **Hora:** antes de conectar, el cliente sincroniza la fecha mediante NTP. El protocolo no admite conexiones `ws://` en claro.

## 3. Registro, modelo de sesión y sincronización

### Identidad del dispositivo

El `deviceId` tiene la forma `md-` seguida de 12 hexadecimales en mayúscula derivados de la MAC del chip (p. ej. `md-34B7DA01ABCD`). Es fijo: no cambia al reiniciar ni al reconfigurar. **El servidor debe tenerlo registrado y asociado al token**; si no, puede cerrar la conexión (p. ej. `1008 "deviceId no coincide"`).

Dónde consultarlo:

- en la pantalla de arranque, en "Esperando plantilla" y en el portal WiFi: "ID para tu servidor";
- en la página web del portal;
- con el comando de consola `id`;
- en el log serie `hello sent, deviceId=...`.

### Secuencia

```text
ESP32-C6                                     Servidor WSS
    |---- TLS certificado + Authorization ---->|
    |---- device.hello {cache id,version} ----->|
    |<---- template.set [solo si cambió] -------|
    |---- template.ack ------------------------->|
    |<---- data.update (snapshot) --------------|
    |---- data.ack ----------------------------->|
    |<---- data.update (patch) ------------------|
    |---- touch.tap {eventId,actionId,x,y,source:"button"} -->|
    |<---- ui.result {eventId,toast,patch} ------|
    |-------------------------------------------|
```

Las plantillas se almacenan en LittleFS (`/template.json`) y los valores en `/values.json`, con escritura diferida (como mucho cada 30 s) para reducir el desgaste. Los datos pueden llegar de forma independiente después de la sincronización de versión. Un `data.update` que indique otra `templateVersion` se rechaza con `data.ack {ok:false,reason:"version_mismatch"}`; si todavía no hay plantilla, con `reason:"no_template"`.

## 4. Envoltorio de mensajes

Todo mensaje es un JSON UTF-8: un objeto raíz con el campo obligatorio `type` (string). Los mensajes de plantilla y de estado llevan un snapshot completo o una actualización parcial.

**Límite de trama entrante: 8192 bytes**, en un único frame de texto no fragmentado. Si una trama supera el límite o es inválida, se cierra la conexión y se reintenta. El cliente responde a los `ping`, envía su propio `ping` cada 20 s y enmascara todas las tramas que van del ESP32 al servidor.

### `device.hello` (dispositivo → servidor)

Es el primer mensaje tras cada conexión.

```json
{
  "type": "device.hello",
  "protocol": 1,
  "deviceId": "md-34B7DA01ABCD",
  "firmware": "1.2.0",
  "display": {"width":172,"height":320,"touch":false,"color":"RGB565"},
  "template": {"id":"goal.v1","version":1},
  "capabilities": ["text","rect","line","button","progress","touch.tap","ui.result","template.cache"]
}
```

`template` refleja la plantilla en caché. Si no hay ninguna (primer arranque, o tras `factory-reset` o `clear-template`), se envía `{"id":"","version":0}` y el servidor debe enviar `template.set`.

### `template.set` (servidor → dispositivo)

```json
{
  "type":"template.set",
  "template":{
    "id":"goal.v1","version":1,"background":"#090D18",
    "elements":[
      {"type":"text","x":10,"y":14,"size":2,"value":"META","color":"#FFFFFF"},
      {"type":"text","x":10,"y":80,"size":2,"bind":"collectedText","color":"#FFFFFF"},
      {"type":"progress","x":10,"y":170,"w":152,"h":12,"bind":"progress","fillColor":"#9568FF"},
      {"type":"button","x":10,"y":271,"w":152,"h":36,"value":"ACTUALIZAR","fillColor":"#6240B4"}
    ],
    "actions":[
      {"id":"refresh","trigger":"tap","x":10,"y":271,"w":152,"h":36,"mode":"hybrid","event":"goal.refresh",
       "local":{"kind":"feedback","message":"Actualizando..."}}
    ]
  }
}
```

`template.ack`: `{ "type":"template.ack", "ok":true, "templateId":"goal.v1", "version":1 }`. Si se rechaza, lleva `ok:false` y `reason`: `invalid_template` o `storage_failed`.

#### Elementos permitidos

| Tipo | Campos obligatorios | Opcionales | Significado |
| --- | --- | --- | --- |
| `text` | `x,y`, uno de `value`/`bind` | `size` 1–4, `color` | Texto sin word-wrap |
| `rect` | `x,y,w,h` | `filled`, `radius` 0–10, `fillColor`, `color` | Rectángulo |
| `line` | `x,y,x2,y2` | `color` | Línea |
| `progress` | `x,y,w,h`, uno de `value`/`bind` | `color`, `fillColor` | Porcentaje 0–100 |
| `button` | `x,y,w,h`, uno de `value`/`bind` | `size`, `fillColor`, `color` | Botón dibujado; la acción se define aparte en `actions` |

Reglas:

- `bind` resuelve una **clave plana** dentro de `data.update.values`. No admite expresiones, CSS, JS, scripts ni URLs de imágenes.
- Los colores son hexadecimales `#RRGGBB`.
- La coordenada (0,0) es la esquina superior izquierda de la LCD en rotación 0.
- Todo elemento con anchura y altura debe quedar dentro de 172×320.

**Límites v1:**

- hasta 45 elementos y 24 acciones;
- hasta 64 variables, con claves de ≤40 caracteres y valores de texto de ≤140 caracteres;
- sin objetos ni arrays anidados en los valores.

El servidor debe evitar textos que no quepan físicamente en la pantalla. Si una plantilla no cumple estos límites, se rechaza completa.

### `data.update` (servidor → dispositivo)

```json
{
  "type":"data.update",
  "templateVersion":1,
  "strategy":"snapshot",
  "values":{"targetText":"$40.000.000","collectedText":"$2.500.000","progress":6.25,"daysText":"DIA 4 / 120"}
}
```

- `strategy="snapshot"` sustituye todos los valores; `strategy="patch"` solo actualiza las claves recibidas.
- El dispositivo responde con `data.ack`: `{ "type":"data.ack", "ok":true, "templateId":"goal.v1", "version":1 }`.
- Los datos financieros se calculan en el servidor; la LCD solo representa lo que recibe.

## 5. Acciones (botón BOOT)

Las acciones se definen en `template.actions[]`, independientes de los elementos dibujados. Cada una describe:

- `id`: de 1 a 48 caracteres;
- `trigger:"tap"`: el único admitido;
- `mode` y `event`;
- una caja `x,y,w,h` dentro de 172×320.

La placa no tiene pantalla táctil: la caja se usa para marcar la acción en pantalla y como coordenada del evento.

#### Uso del botón BOOT (GPIO 9)

| Gesto | Efecto |
| --- | --- |
| Pulsación corta (≥30 ms, antirrebote) | Ejecuta la acción seleccionada. Al cargar una plantilla, la seleccionada es la primera. |
| Mantener 700 ms | Selecciona la siguiente acción y muestra el aviso `Accion: <id>`. Si hay más de una acción, la seleccionada se marca con un recuadro amarillo. |

Sin plantilla, el botón no hace nada. Si la plantilla no tiene acciones, muestra `Sin acciones`.

#### Modos

| `mode` | Comportamiento |
| --- | --- |
| `local` | Solo aplica una acción declarativa de UI: `feedback` o `set_value`. No se envía al servidor. |
| `remote` | Emite `touch.tap`, espera `ui.result` y opcionalmente aplica cambios. Si el WSS está caído, muestra `Sin conexion` y deja el evento en cola. |
| `hybrid` | Ejecuta la respuesta local inmediata y emite `touch.tap` para el procesamiento remoto. |

`local` permite `{ "kind":"feedback", "message":"Procesando..." }` o `{ "kind":"set_value", "key":"estado", "value":"Cargando" }`. No es un motor de código arbitrario. Se genera **un solo evento por pulsación**.

### Evento `touch.tap`

Se mantiene el mismo nombre que en la versión táctil, así que los servidores existentes no necesitan cambios.

```json
{
  "type":"touch.tap",
  "source":"button",
  "eventId":"md-34B7DA01ABCD-93351-7",
  "deviceId":"md-34B7DA01ABCD",
  "actionId":"refresh",
  "name":"goal.refresh",
  "x":86,"y":289,
  "templateId":"goal.v1","templateVersion":1,
  "uptimeMs":93351
}
```

- `x,y` es el centro de la caja de la acción. `name` es el `event` de la acción, o `tap` si no tiene.
- El servidor **debe** validar el `actionId` contra la plantilla activa del dispositivo y comprobar la versión.
- El `eventId` es idempotente y el servidor debe deduplicar por él.
- El firmware retiene en RAM hasta 12 eventos sin confirmar y los reenvía cada ≈5 s mientras está conectado. Con la cola llena muestra `Cola de eventos llena`.
- Al reiniciar, la cola se pierde. La garantía es de entrega **al menos una vez mientras el dispositivo sigue encendido**; no es transaccional.

### Respuesta desde el servidor

```json
{
  "type":"ui.result",
  "eventId":"md-34B7DA01ABCD-93351-7",
  "status":"ok",
  "toast":"Datos actualizados",
  "patch":{"collectedText":"$2.700.000","progress":6.75}
}
```

`ui.result`:

- confirma el evento (lo saca de la cola);
- muestra `toast`, recortado a 24 caracteres;
- aplica `patch` (fusiona valores) o `replace` (sustituye todos los valores).

Si el procesamiento es asíncrono, el servidor puede confirmar con `event.ack {"type":"event.ack","eventId":"..."}`. También puede enviar un `template.set` para navegar a otra vista o un `data.update` para refrescar el contenido.

**No confundas confirmación con ejecución única:** que los mensajes se repitan es normal, así que el backend debe implementar operaciones idempotentes. Una pulsación nunca debe mover dinero real por simple configuración visual: las reglas pertenecen al backend autenticado.

## 6. Control y administración del dispositivo (WSS)

| Dirección | Tipo | Cuerpo relevante |
| --- | --- | --- |
| servidor→ESP | `display.refresh` | Sin campos adicionales |
| servidor→ESP | `display.brightness` | `percent` entero (30 si falta), limitado por el firmware a 0–50. No se guarda: al reiniciar vuelve al 18 % |
| servidor→ESP | `trust.stage` | `caDerBase64`: certificado CA privado nuevo, recibido por un TLS ya confiable |
| ESP→servidor | `trust.ack` | `operation:"stage"` o `"activate"`, `ok` |
| servidor→ESP | `trust.activate` | Reinicia para probar la CA preparada; rollback a los 90 s sin conexión WSS válida |
| servidor→ESP | `ota.install` | `url` HTTPS, `sha256` hexadecimal, `signature` ECDSA DER codificada en base64 |
| ESP→servidor | `ota.ack` | `ok`, `reason` (si la operación no reinicia) |

La clave de firmware se genera con `tools/make_signing_key.py` (en el proyecto padre, fuera de este repositorio). No se distribuye la clave privada al cliente. El firmware **no ejecuta OTA sin firma válida**. Esta implementación es un punto de partida; antes de producción exige revisar:

- la actualización de los CA bundles mediante releases;
- las pruebas de corte de energía;
- Secure Boot y Flash Encryption;
- un flujo de recuperación verificado.

## 7. API REST para aplicación/administración (opcional)

El servidor de ejemplo implementa una API REST para editar el diseño y los datos desde cualquier panel web. Este contrato es **del servidor del cliente**: el ESP32 no llama a la API REST. La especificación completa está en [`openapi.yaml`](openapi.yaml).

Todos los endpoints de administración usan `Authorization: Bearer ADMIN_TOKEN`. Nunca reutilices el token de dispositivo como token de administrador.

| Método | Ruta | Cuerpo | Resultado |
| --- | --- | --- | --- |
| GET | `/api/v1/health` | — | `{status:"ok",protocol:1}` |
| GET | `/api/v1/devices/{deviceId}` | — | Estado actual, plantilla, valores y conexión |
| PUT | `/api/v1/devices/{deviceId}/template` | Objeto plantilla | Persiste y publica la plantilla y el snapshot actual |
| PUT | `/api/v1/devices/{deviceId}/data` | Objeto de valores | Reemplaza todos los datos y publica un snapshot |
| PATCH | `/api/v1/devices/{deviceId}/data` | Objeto de valores | Aplica y publica un patch |
| GET | `/api/v1/devices/{deviceId}/events` | — | Últimos eventos de acciones (demo: 100) |
| POST | `/api/v1/devices/{deviceId}/commands` | Comando `display.*`, `trust.*` u `ota.install` | Envía el comando al dispositivo activo |

Errores HTTP: 400 JSON inválido, 401 no autorizado, 404 ruta inexistente, 405 método incorrecto, 409 dispositivo desconectado, 413 mensaje excesivo, 422 plantilla, datos o comando inválidos.

La API de ejemplo guarda todo en **memoria RAM**. Para producción hacen falta:

- base de datos y aislamiento entre clientes;
- auditoría y límites de peticiones;
- claves por dispositivo y rotación de tokens;
- reglas de autorización por propietario.

## 8. Reconexión, fallos y recuperación

- **Arranque sin configuración** (sin URL o sin token): se abre el portal WiFiManager `MetaDisplay-Setup`. La clave WPA2 son 8 letras minúsculas aleatorias, persistentes y visibles en la LCD. El portal se cierra a los 300 s; si no queda configurado, el dispositivo reinicia y lo vuelve a abrir.
- **Wi-Fi:** reintenta cada 12 s y abre el portal tras 120 s sin red. Al arrancar con credenciales espera hasta 18 s antes de abrir el portal.
- **WSS:**
  - *backoff* de 2, 4, 8… hasta 60 s, más un jitter de hasta 750 ms;
  - la espera vuelve a 2 s tras cada handshake correcto, así que un servidor que cierra justo después del `hello` provoca un reintento cada ~4–5 s;
  - ping cada 20 s, y la conexión se da por perdida si no llega pong en 70 s;
  - nunca envía el Bearer antes de validar el TLS.
- **Reloj:** sincronización NTP inicial, de hasta 15 s. Si arranca sin hora válida, la negociación TLS falla de forma segura y se reintenta.
- **Desconexión:** mantiene visibles la última plantilla y los datos, con un indicador rojo en la esquina superior derecha (verde cuando hay conexión).
- **JSON o plantilla inválidos:** se rechazan con un ACK negativo; nunca se ejecutan nombres de funciones recibidos por JSON.
- **Corte de alimentación:** se conservan los últimos datos guardados (con hasta 30 s de atraso) y la plantilla.
- **Fallo al rotar la CA privada:** rollback automático si no se logra un WebSocket válido tras activarla. WiFiManager sigue disponible como recuperación local.
- **Reconfiguración local sin el servidor:** con los comandos de consola `set-url`, `set-token`, `wifi-reset`, `portal` y `factory-reset` (ver [`../docs/comandos-consola.md`](../docs/comandos-consola.md)).

### Diagnóstico por el log serie (115200 baudios)

| Log | Significado habitual | Qué hacer |
| --- | --- | --- |
| `connect failed: http: 'HTTP/1.1 401 Unauthorized'` | El servidor rechaza el token en el handshake | Revisar el token (`config`, `set-token`) |
| `connect failed: tls_connect: <error>` | Fallo de TLS o DNS: certificado, hora o dominio | Revisar el dominio, la CA y la hora NTP |
| `connect failed: invalid_url_or_token` | La URL no es `wss://` o falta el token | `set-url` / `set-token` |
| `disconnected: server_close code=1008 reason=deviceId no coincide` | El servidor no tiene registrado ese `deviceId` para ese token | Registrar el ID que muestra `id` |
| `disconnected: pong_timeout` / `socket_closed` / `wifi_lost` | Corte de red o del servidor | Se reintenta solo |

Con `log wss on` se imprime cada JSON que entra (`[WSS <]`) y sale (`[WSS >]`). El token nunca aparece en el log.

## 9. Seguridad, limitaciones y pendientes de homologación

1. Un **CA bundle** no significa mantenimiento cero: el dispositivo necesita nuevas versiones de firmware firmadas para refrescar las CA públicas a lo largo de su vida.
2. El portal WiFiManager usa HTTP local dentro de un AP WPA2, no HTTPS. El propietario debe configurarlo en un entorno confiable, y el fabricante debe verificar cómo se protegen las credenciales en la flash.
3. La consola serie USB permite leer la URL, ver el final del token y cambiar o borrar la configuración sin autenticación. Quien tenga acceso físico al USB también podría reflashear la placa.
4. El firmware se compiló y se probó en la placa física el 2026-11-08.
   - Probado: la pantalla, el portal, la conexión WSS contra un servidor real, la caché de plantilla, los datos y la consola.
   - Sin probar: OTA, la rotación de CA, `factory-reset` y `wifi-reset`, y el botón físico (la acción se probó simulada con `press`).
5. El demo backend no tiene persistencia, ni varios tenants, ni CA privada automática del servidor. Para certificados públicos se recomienda Caddy o Traefik con ACME.
6. Hay un solo botón: no hay gestos más allá de la pulsación corta y la larga. Mientras el cliente reconecta (handshake TLS bloqueante), una pulsación muy corta puede perderse.
7. Siguen pendientes las pruebas de OTA de extremo a extremo, la rotación de CA, la recuperación tras corte de energía, la validación del flujo de provisioning, la revisión del arranque seguro y la actualización del almacén público de CA.

## 10. Ejemplo de integración rápida

```bash
# En el servidor de ejemplo, por ejemplo con Caddy delante:
cd server && npm install
export DEVICE_TOKEN='un-secreto-largo-y-distinto-para-el-dispositivo'
export ADMIN_TOKEN='otro-secreto-largo-para-la-api-admin'
npm start
# Desplegar el reverse proxy Caddy con TLS público y dominio válido.
```

**Provisioning del dispositivo:**

1. Enciende la placa y anota el `deviceId` que aparece en pantalla ("ID para tu servidor").
2. Regístralo en tu servidor junto con su token.
3. Conéctate a la red `MetaDisplay-Setup` con la clave que muestra la pantalla y abre `192.168.4.1`.
4. Elige la WiFi e introduce `wss://display.tudominio.com/ws/device`, el Bearer token del dispositivo y, si hace falta, la CA privada en base64 DER.
5. Alternativa sin móvil: con la consola serie, `set-url`, `set-token` y `reboot`.

En los repositorios de clientes está prohibido subir tokens, contraseñas o llaves de firma.

### Operaciones API de ejemplo

```bash
curl -X PATCH https://display.tudominio.com/api/v1/devices/md-34B7DA01ABCD/data \
  -H 'Authorization: Bearer TOKEN_ADMIN' \
  -H 'Content-Type: application/json' \
  -d '{"collectedText":"$3.100.000","progress":7.75}'
```

## 11. Criterios de aceptación

- [x] Inicialización de la LCD correcta en 172×320, sin colores invertidos.
- [ ] El botón BOOT físico: una pulsación corta genera un solo evento; mantener 700 ms cambia la acción seleccionada.
- [ ] Portal WiFiManager nuevo y reconexión con las credenciales almacenadas.
- [ ] Rechazo de `ws://`, certificado expirado, hostname falso y token equivocado.
- [ ] Arranque sin servidor: la última plantilla y los datos siguen visibles.
- [x] Cache hit: `device.hello` no descarga una plantilla de igual versión.
- [ ] Un cambio de plantilla reorganiza la pantalla sin flashear firmware.
- [ ] Acción `hybrid`: feedback inmediato + `touch.tap` + `ui.result` ligado al evento.
- [ ] `eventId` duplicado: el backend procesa la acción una sola vez.
- [ ] Caída de Wi-Fi: se recupera automáticamente sin perder el estado.
- [ ] CA privada: stage/activate, éxito y rollback probados.
- [ ] OTA: rechazar firmware sin firma válida y probar una imagen válida y el rollback.
- [ ] Evaluación de los secretos en NVS, Secure Boot, Flash Encryption y hardening (incluida la consola serie).
