# Pantalla sin imagen: la placa era la versión sin touch

Resuelto el 2026-11-08. Pantalla verificada en el hardware real.

## Síntoma

- La pantalla se quedaba gris o negra: el backlight a veces se encendía, pero no aparecía ninguna imagen.
- El firmware no daba ningún error de pantalla: `gfx->begin()` devolvía `true`.
- El touch no respondía en I2C `0x63`.

## Causa

La placa física es la **Waveshare ESP32-C6-LCD-1.47 (sin touch)**, y el firmware estaba escrito para la **ESP32-C6-Touch-LCD-1.47**. Por fuera casi no se distinguen, pero la pantalla va en otros pines y usa otro controlador:

| Señal            | Firmware anterior (Touch) | Placa real (sin touch) |
|------------------|---------------------------|------------------------|
| SCK (reloj SPI)  | GPIO 1                    | **GPIO 7**             |
| MOSI (datos SPI) | GPIO 2                    | **GPIO 6**             |
| RST              | GPIO 22                   | **GPIO 21**            |
| Backlight        | GPIO 23                   | **GPIO 22**            |
| CS / DC          | 14 / 15                   | 14 / 15 (iguales)      |
| Controlador      | JD9853 + init propia      | ST7789, IPS            |
| Touch            | AXS5106L en I2C 0x63      | no tiene               |

Esto explica cada síntoma:

1. **No había imagen.** El reloj y los datos SPI salían por GPIO 1 y 2, que en esta placa no llegan a la pantalla. El panel nunca recibía comandos y ni siquiera salía del modo sleep.
2. **El backlight se encendía por casualidad.** El firmware usaba GPIO 22 como RST, y la librería lo deja en HIGH al iniciar. En esta placa GPIO 22 es el backlight, así que se encendía la luz pero sin imagen: esa era la "pantalla gris". El brillo se mandaba a GPIO 23, que aquí no está conectado.
3. **El touch no respondía** porque esta placa no tiene touch. Un escaneo I2C no encontró ningún dispositivo.
4. **No había error en el arranque.** Por SPI solo se envían datos a la pantalla y esta nunca contesta, así que `begin()` devuelve `true` aunque los pines estén mal. Por software no se puede detectar; hay que mirar la pantalla.

## Qué no funcionó (y por qué)

Estos intentos no cambiaban nada, porque el problema eran los pines:

- Cambiar `Arduino_ESP32SPI` por `Arduino_HWSPI` o al revés.
- Copiar la secuencia completa de inicialización del JD9853 del ejemplo del fabricante.
- Cambiar `IPS` entre `true` y `false`.

## Cómo lo diagnosticamos

Con un firmware de prueba aparte, `test-placa`, que tiene dos entornos de PlatformIO, uno para cada placa:

- `pio run -e lcd -t upload`: pines del ST7789 sin touch.
- `pio run -e touch -t upload`: pines del JD9853 con touch, más un escaneo I2C.

Los dos llenan la pantalla con colores (rojo, verde, azul…) con el backlight al 100 %. Con el entorno `lcd` los colores se vieron bien; con `touch` la pantalla siguió sin imagen y el I2C estaba vacío. Eso confirmó el modelo de la placa.

El proyecto está en `~/Documents/Temp/testEps32c6/test-placa`, fuera de este repositorio.

## Solución aplicada

- [`include/Board.h`](../include/Board.h): pines de la placa sin touch (SCK=7, MOSI=6, CS=14, DC=15, RST=21, BL=22). Quitados los pines y la dirección del touch.
- [`src/Display.cpp`](../src/Display.cpp): `Arduino_HWSPI` + `Arduino_ST7789(..., IPS=true, 172, 320, 34, 0, 34, 0)`. Quitada la init del JD9853, que este panel no necesita.
- Touch retirado por completo:
  - Borrados `src/Touch.cpp` e `include/Touch.h`, y [`src/main.cpp`](../src/main.cpp) ya no inicia ni lee el touch.
  - En [`src/Runtime.cpp`](../src/Runtime.cpp), quitada la gestión de toques (`touch.tap`), la cola de eventos con reintentos y las respuestas del servidor a esos toques (`ui.result`, `event.ack`).
  - Quitados los avisos emergentes de `Display::feedback`, porque solo salían al tocar.
- Protocolo: el `device.hello` ahora envía `display.touch=false` y versión `1.0.0`, y ya no anuncia las capacidades `touch.tap` ni `ui.result`.
- Plantillas: el campo `actions` se sigue aceptando, porque el servidor lo exige. Al principio se ignoraba; después pasó a dispararse con el botón BOOT (GPIO 9). Una pulsación corta ejecuta la acción seleccionada y mantenerlo 0,7 s pasa a la siguiente, que se marca con un recuadro amarillo. Se envía el mismo `touch.tap` con `"source":"button"`.

## Cómo comprobarlo

1. `pio run -t upload`. La placa suele aparecer como `/dev/ttyACM1`, no siempre `ttyACM0`.
2. Mirar la pantalla: debe salir "MetaDisplay C6 / Iniciando..." y después el portal WiFi o la plantilla.
3. Que el serial no muestre nada con la WiFi ya configurada es normal: el firmware no imprime nada al arrancar.

## Lección

Si la pantalla no muestra nada, lo primero es **confirmar el modelo exacto de la placa** y sus pines con un test de colores mínimo, antes de cambiar drivers o secuencias de inicialización. Que `begin()` devuelva `true` no prueba nada con pantallas SPI.

## Pendiente

- `server/server.js`, en la carpeta del proyecto (fuera de este repositorio), todavía responde a `touch.tap`. Es código sin uso, pero no molesta.
- `README.md` y `protocol/`, también fuera de este repositorio, aún describen el touch.
- El nombre del proyecto sigue siendo "MetaDisplay-C6-Touch".
