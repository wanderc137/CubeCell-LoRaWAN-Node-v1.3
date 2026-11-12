# Guía del desarrollador — CubeCell LoRaWAN Bridge v1.3

Documento técnico del firmware `CubeCell-LoRaWAN-Node.ino` (v1.3b). Explica la arquitectura, el flujo de ejecución, cada función, la API de RadioLib usada y cómo extender el nodo sin romper el comportamiento existente.

---

## 1. Vista general

### 1.1 Qué es

Un sketch de Arduino para Heltec CubeCell HTCC-AB01 que implementa un puente bidireccional:

```
        ┌─────────────────────────────────────────────┐
        │              loop() — 10 ms                 │
        │                                             │
 Serial ─┼─► buffer ─► parseHex ─► ensureJoined ─► sendReceive ─► LoRa
        │                                             │
 Serial ◄┼─ printDownlink ◄─ downlinkData ◄───────────┘
        │                                             │
        └─► heartbeat cada 30 s ─────────────────────►┘
```

No hay scheduler, ni RTOS, ni interrupciones de usuario: todo ocurre en `loop()` con sondeo del puerto serie y un temporizador por `millis()`.

### 1.2 Dependencias

| Dependencia | Versión | Uso |
|---|---|---|
| Core CubeCell (Heltec) | estable | `FQBN`, SPI, `Serial`, `millis()` |
| RadioLib | **7.7.1** | Driver SX1262 + stack LoRaWAN |
| Arduino core | — | `Serial.printf`, `String` no usado |

El sketch **no** usa `LoRaWan_APP.h` ni `CubeCellLib.a`. Esa es una decisión deliberada (ver §7).

### 1.3 Mapa de archivos

```
CubeCell-LoRaWAN-Node-v1.3/
├── CubeCell-LoRaWAN-Node.ino   # todo el firmware
├── docs/
│   ├── datasheet.md            # manual de operador
│   ├── developer.md            # este documento
│   ├── REFERENCES.md
│   └── BITACORA.md
└── LICENSE
```

---

## 2. Configuración en tiempo de compilación

### 2.1 Credenciales

```cpp
uint64_t joinEui = 0x0000000000000000;   // JoinEUI / AppEUI
uint64_t devEui  = 0x70B3D57ED0000001;   // DevEUI
uint8_t  appKey[] = { /* 16 bytes */ };
uint8_t  nwkKey[] = { /* 16 bytes, igual a appKey en 1.0.x */ };
```

El orden de `beginOTAA()` es `(joinEui, devEui, nwkKey, appKey)`. Invertir los dos primeros es el error más común y no produce ningún diagnóstico útil: el servidor simplemente no responde.

### 2.2 Constantes

| Constante | Valor | Efecto |
|---|---|---|
| `MAX_PAYLOAD` | 51 | Techo de bytes por uplink |
| `SERIAL_BUF` | 256 | Buffer de línea |
| `STALE_MS` | 500 | Timeout de línea |
| `HEARTBEAT_MS` | 30000 | Periodicidad del heartbeat |
| `scanGuard` | 200 | Margen del stack en el join (ms) |

`MAX_PAYLOAD = 51` no es arbitrario: es el máximo de payload LoRaWAN para DR0–DR5 en AU915 con BW125 y el worst case de airtime que el autor aceptó. Subirlo requiere revisar la tabla de airtime y el DR negociado por ADR.

### 2.3 Región

```cpp
const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;
LoRaWANNode node(&radio, &Region, subBand);
```

Cambiar de región es cambiar `Region` y `subBand`, pero también las credenciales del NS y la configuración del gateway. En AU915 la sub-banda es obligatoria: el nodo no escanea las ocho.

---

## 3. Estructura del sketch

| Bloque | Líneas aprox. | Responsabilidad |
|---|---|---|
| Includes y pines | 1–40 | `RadioLib.h`, `Module`, `SX1262` |
| Credenciales | 40–60 | EUIs y claves |
| Constantes y globales | 60–90 | Buffers y estado |
| Utilidades | 90–150 | `radioSleepDelay`, `debug`, `stateDecode` |
| Parser | 150–190 | `parseHexString` |
| Sesión | 190–220 | `ensureJoined` |
| E/S LoRaWAN | 220–290 | `sendAndReceive`, `printDownlink` |
| Heartbeat | 290–310 | `sendHeartbeat` |
| Línea serie | 310–350 | `processSerialLine` |
| `setup` / `loop` | 350–400 | Arranque y bucle |

---

## 4. Funciones

### 4.1 `radioSleepDelay`

```cpp
static void radioSleepDelay(RadioLibTime_t ms);
```

Sustituye el sleep del stack por una espera activa basada en `millis()`. RadioLib la invoca durante las ventanas de recepción; el core de CubeCell la habría resuelto con `delay()`, que arrastra el bug de escalado de timers del ASR6502 (~100×).

- **Entrada:** milisegundos.
- **Salida:** ninguna.
- **Efecto:** bloquea el CPU el tiempo solicitado.
- **Por qué busy-wait:** es la única forma de garantizar la duración real sin depender del timer defectuoso. Consume CPU, pero las esperas son cortas (1–2 s).

### 4.2 `debug`

```cpp
static void debug(const char* fmt, ...);
```

Envoltorio de `Serial.printf` con formato variádico. Permite silenciar la salida cambiando una sola definición.

### 4.3 `stateDecode`

```cpp
static const char* stateDecode(int16_t state);
```

Traduce códigos de RadioLib a texto legible. Mapea `RADIOLIB_ERR_NONE`, `CHIP_NOT_FOUND`, `PACKET_TOO_LONG`, `RX_TIMEOUT`, `MIC_MISMATCH`, `NETWORK_NOT_JOINED`, `NO_JOIN_ACCEPT`, `LORAWAN_NEW_SESSION`, `SESSION_RESTORED` y devuelve `"UNKNOWN"` para el resto.

Se usa en todos los mensajes de error para imprimir `código (nombre)`. Es la principal herramienta de diagnóstico del firmware.

### 4.4 `parseHexString`

```cpp
static size_t parseHexString(const char* input, uint8_t* output, size_t maxOut);
```

Convierte una línea de texto a bytes.

**Algoritmo:**

1. Recorre `input` y copia a `clean[256]` solo caracteres en `[0-9A-Fa-f]`.
2. Si `cleanLen == 0` o es impar → devuelve `0`.
3. Convierte cada par con `strtoul` en base 16.
4. Detiene la conversión al llegar a `maxOut`.

**Comportamiento a tener en cuenta:**

| Caso | Resultado |
|---|---|
| `"01 02 03"` | 3 bytes |
| `"010203"` | 3 bytes |
| `"01:02:03"` | 3 bytes |
| `"0x01"` | 1 byte `0x01` (la `x` se descarta; `0` y `01` quedan como `00 10`... ver nota) |
| `"ABC"` | 0 (impar) |
| `""` | 0 |
| 120 caracteres hex | 51 bytes, **truncado sin aviso** |

> El caso `0x01` es engañoso: al eliminar la `x` queda `001`, impar, y se rechaza. Con `0x0102` queda `00102`, también impar. En general, el prefijo `0x` rompe la paridad y la línea se descarta.

**Limitación conocida:** el truncado silencioso. Si la aplicación necesita saberlo, hay que comparar `input` limpio con `maxOut * 2` y avisar.

### 4.5 `ensureJoined`

```cpp
static bool ensureJoined();
```

Comprueba `node.isActivated()`. Si es falso, imprime `[REJOIN] Intentando re-join...` e invoca `node.activateOTAA()`.

- **Devuelve:** `true` si hay sesión al salir.
- **Efectos:** imprime `[REJOIN] +++ EXITOSO +++` o `[REJOIN] Fallo: %d (%s)`.
- **Coste:** un join completo puede tardar varios segundos y consume airtime.

No persiste el resultado: si el join falla, el siguiente envío lo reintenta.

### 4.6 `printDownlink`

```cpp
static void printDownlink(const uint8_t* data, size_t len);
```

Imprime el downlink en formato estable:

```
[DOWNLINK] 68 6F 6C 61  →  "hola"
```

- Hex en mayúsculas, dos dígitos, separado por espacios.
- ASCII imprimible (32–126); el resto se muestra como `.`.
- Termina con `\n` y `>` (prompt) y hace `Serial.flush()`.

El `flush()` es importante: si el equipo conectado reinicia o cierra el puerto, el mensaje ya salió del buffer.

### 4.7 `sendAndReceive`

```cpp
static int16_t sendAndReceive(const uint8_t* data, size_t len);
```

Núcleo del bridge.

```cpp
downlinkLen = sizeof(downlinkData);
int state = node.sendReceive(data, len, 1, downlinkData, &downlinkLen, false, NULL, NULL);
```

| Parámetro | Valor | Significado |
|---|---|---|
| `data`, `len` | payload | Bytes a enviar |
| `fPort` | `1` | Puerto de aplicación |
| `downlinkData` | buffer | Salida del downlink |
| `&downlinkLen` | in/out | Capacidad de entrada, longitud real de salida |
| `isConfirmed` | `false` | Sin ACK |
| `dmac`, `fctrl` | `NULL` | Sin comandos MAC adicionales |

**Valor de retorno (crítico):**

| Retorno | Significado |
|---|---|
| `RADIOLIB_ERR_NONE` (0) | Uplink OK, sin downlink |
| `1` o `2` | Uplink OK + downlink en RX1/RX2 |
| `< 0` | Error |

**No es la longitud del downlink.** Ese fue el bug de v1.3: usar el retorno como longitud hacía que siempre se imprimiera 1 byte. La longitud correcta es `downlinkLen`, que el stack escribe en el puntero.

La función imprime el resultado (`→ OK`, `→ OK + downlink (N bytes)`, `→ ERROR ...`) y devuelve el estado.

### 4.8 `sendHeartbeat`

```cpp
static void sendHeartbeat();
```

Llama a `ensureJoined()`, imprime `[HEARTBEAT] `, envía `0xBB` con `sendAndReceive` y actualiza `lastUplinkMs`. Si no hay sesión, no envía nada.

### 4.9 `processSerialLine`

```cpp
static void processSerialLine(char* line);
```

Orquesta una línea completa:

1. Si está vacía, vuelve.
2. `parseHexString`; si devuelve 0, imprime el error de formato y el prompt.
3. `ensureJoined`; si falla, imprime `!! Sin sesion LoRaWAN, reintentando...`, espera 5 s con `radioSleepDelay`, reintenta.
4. Si el segundo intento falla, imprime `!! No se pudo recuperar sesion` y vuelve.
5. `sendAndReceive`, actualiza `lastUplinkMs`, imprime `> `.

### 4.10 `setup`

Secuencia:

1. `Serial.begin(115200)` y `delay(3000)` (margen para abrir el monitor).
2. Banner de versión.
3. `radio.begin()`; si falla, imprime `Radio init FAILED` y entra en bucle infinito.
4. `node.beginOTAA(joinEui, devEui, nwkKey, appKey)`; si falla, `Node init FAILED` y bucle.
5. `node.setSleepFunction(radioSleepDelay)` y `node.scanGuard = 200`.
6. `node.activateOTAA()`; imprime `+++ JOINED +++` o `Join FAILED`.
7. `lastUplinkMs = millis()` y prompt.

El bucle infinito ante fallo de radio es deliberado: sin radio no hay nada que hacer y el operador ve el error en lugar de un nodo mudo.

### 4.11 `loop`

```cpp
void loop() {
    while (Serial.available()) { /* acumula en serialLine */ }
    /* flush por inactividad > STALE_MS */
    /* heartbeat si corresponde */
    delay(10);
}
```

- Acumula hasta 255 caracteres; el exceso se descarta.
- En `\n` o `\r` con `serialPos > 0`, procesa y reinicia.
- Si pasan más de 500 ms desde el último carácter con buffer no vacío, lo descarta (evita líneas a medias).
- Heartbeat si `node.isActivated() && (millis() - lastUplinkMs) >= HEARTBEAT_MS`.
- `delay(10)` limita el sondeo y deja respirar al core.

---

## 5. Máquina de estados

```
                 ┌──────────────┐
                 │   BOOT       │
                 └──────┬───────┘
                        │ radio.begin()
                 ┌──────▼───────┐  fallo
                 │ RADIO_INIT   ├────────► ERROR (bucle)
                 └──────┬───────┘
                        │ beginOTAA()
                 ┌──────▼───────┐  fallo
                 │ NODE_INIT    ├────────► ERROR (bucle)
                 └──────┬───────┘
                        │ activateOTAA()
                 ┌──────▼───────┐  fallo
                 │  JOINING     ├────────► IDLE_SIN_SESION
                 └──────┬───────┘
                        │ OK
                 ┌──────▼───────┐
        ┌───────►│    IDLE      │◄──────────────┐
        │        └──┬────┬──────┘               │
        │   línea    │    │ heartbeat 30 s      │
        │            │    │                     │
        │     ┌──────▼────▼──────┐              │
        │     │  SEND_AND_RX     │              │
        │     └──────┬───────────┘              │
        │            │ OK / downlink            │
        └────────────┴──────────────────────────┘
                     │ error
              ┌──────▼───────┐
              │  REJOIN      │──fallo──► IDLE_SIN_SESION
              └──────────────┘
```

Estados observables por la salida serie: `IDLE` (prompt), `SEND_AND_RX` (`>> Enviando`), `REJOIN`, `IDLE_SIN_SESION` (sin heartbeat).

---

## 6. Flujo de un uplink

```
Usuario escribe "01 02 03\n"
        │
        ▼
loop() detecta '\n' y llama processSerialLine()
        │
        ▼
parseHexString → {0x01,0x02,0x03}, len=3
        │
        ▼
ensureJoined() → true
        │
        ▼
sendAndReceive()
  ├─ imprime ">> Enviando: 01 02 03 "
  ├─ node.sendReceive(..., fPort=1, ...)
  │     ├─ espera canal libre / duty cycle
  │     ├─ transmite
  │     ├─ abre RX1 (~1 s)
  │     └─ abre RX2 (~2 s)
  ├─ state == 0 → "→ OK (sin downlink)"
  └─ state > 0  → "→ OK + downlink (N bytes)" + printDownlink()
        │
        ▼
lastUplinkMs = millis(); imprime "> "
```

---

## 7. El workaround de timers

### 7.1 Síntoma

Con el stack de Heltec, el `JoinRequest` salía y el gateway respondía, pero el nodo no veía el `JoinAccept`. El join funcionaba de forma intermitente.

### 7.2 Causa

Los timers del ASR6502 en el core de CubeCell se escalan ~100×. Las ventanas RX del stack no coinciden con la respuesta del servidor.

### 7.3 Solución

1. Migrar a RadioLib (stack en C++ sobre el driver, sin binario de Heltec).
2. Sustituir el sleep por `millis()`:

```cpp
static void radioSleepDelay(RadioLibTime_t ms) {
    uint32_t start = millis();
    while ((millis() - start) < ms) { /* NOP */ }
}
node.setSleepFunction(radioSleepDelay);
node.scanGuard = 200;
```

`millis()` se alimenta del RTC de 32.768 kHz y no del timer escalado. `scanGuard` añade 200 ms de margen al cálculo de la ventana.

### 7.4 Por qué no se corrigió el core

Parchear el core de Heltec habría atado el proyecto a una versión concreta y a un binario cerrado. RadioLib es mantenible y portable. Ver `BITACORA.md`.

---

## 8. Guía de extensión

### 8.1 Agregar un sensor

El nodo no tiene sensores, pero el patrón es simple:

```cpp
#include <Wire.h>

void sendSensor() {
    uint8_t buf[8];
    float t = readTemperature();          // tu driver
    int16_t ti = (int16_t)(t * 100);      // centésimas
    buf[0] = 0x01;                        // opcode "temperatura"
    buf[1] = (ti >> 8) & 0xFF;            // big-endian
    buf[2] = ti & 0xFF;
    sendAndReceive(buf, 3);
}
```

Llama a `sendSensor()` desde `loop()` con tu propio temporizador. No bloquees más de lo necesario: un uplink en SF12 puede tardar 2,5 s.

### 8.2 Cambiar el payload

| Cambio | Dónde | Cuidado |
|---|---|---|
| Más de 51 bytes | `MAX_PAYLOAD` y buffers | Airtime y DR |
| Formato con cabecera | `parseHexString` o capa nueva | Mantener compatibilidad |
| Confirmación | 5.º argumento de `sendReceive` | Genera downlink de ACK |
| Otro `fPort` | 3.er argumento | Debe coincidir con el NS |

### 8.3 Cambiar `Serial` por `Serial1`

Para integración TTL:

```bash
sed -i 's/\bSerial\b/Serial1/g' CubeCell-LoRaWAN-Node.ino
```

Verifica que no se hayan cambiado `Serial` dentro de nombres como `SerialNumber`. Luego:

```cpp
Serial1.begin(115200);
```

en `setup()`. Los pines son `GPIO6` (TX) y `GPIO7` (RX). El USB deja de mostrar la salida.

### 8.4 Cambiar de región

```cpp
const LoRaWANBand_t Region = EU868;   // ejemplo
const uint8_t subBand = 0;            // EU868 no usa sub-bandas
```

Además:

1. Regenerar credenciales en el NS de la nueva región.
2. Reconfigurar el gateway.
3. Revisar potencia y duty cycle (EU868 sí impone 1 %).
4. Ajustar `MAX_PAYLOAD` si el DR máximo es menor.

### 8.5 Agregar deep sleep

```cpp
#include "Arduino.h"

void goToSleep(uint32_t seconds) {
    radio.sleep();
    node.sleep(seconds);          // según API de RadioLib
    // o deepSleep(seconds) del core
}
```

Consideraciones: el heartbeat desaparece, el join se pierde si no hay NVM y el puerto serie se cierra. No es un cambio trivial.

### 8.6 Persistir la sesión

RadioLib permite NVM para el `DevNonce` y la sesión. Requiere una región de flash y cuidado con el desgaste. Sin esto, cada reinicio genera un join nuevo.

### 8.7 Filtrar downlinks MAC

```cpp
static bool isAppDownlink(const uint8_t* d, size_t n) {
    if (n == 1 && (d[0] == 0x00 || d[0] == 0x03 || d[0] == 0x06)) return false;
    return true;
}
```

Llámalo en `sendAndReceive` antes de `printDownlink`. Es una política de aplicación: el firmware base imprime todo.

---

## 9. Presupuesto de memoria

| Elemento | Bytes | Nota |
|---|---|---|
| `txBuffer[51]` | 51 | Payload |
| `serialLine[256]` | 256 | Línea cruda |
| `clean[256]` (parser) | 256 | Copia temporal |
| `downlinkData[51]` | 51 | Salida |
| Stack RadioLib | ~2–4 KB | Buffers internos |
| Stack del sistema | ~2 KB | — |

Total estático del sketch: ~4 KB de RAM (25 % de 16 KB). El margen es suficiente para un sensor I2C pequeño, no para TLS ni JSON grande.

Flash: ~60 KB de 128 KB (45 %). Queda espacio para una o dos librerías.

---

## 10. Plan de pruebas

### 10.1 Pruebas manuales mínimas

| # | Prueba | Comando | Criterio |
|---|---|---|---|
| 1 | Arranque | Reiniciar | `Radio OK`, `Node OK` |
| 2 | Join | Esperar | `+++ JOINED +++` |
| 3 | Uplink corto | `01` | `→ OK` |
| 4 | Uplink 51 B | 102 caracteres hex | `→ OK`, longitud 51 |
| 5 | Truncado | 60 B | Envía 51, sin aviso |
| 6 | Entrada inválida | `zz` | `?? Error` |
| 7 | Impar | `ABC` | `?? Error` |
| 8 | Downlink | `686F6C61` | `"hola"` ≤ 30 s |
| 9 | Heartbeat | Esperar 30 s | `[HEARTBEAT]` |
| 10 | Re-join | Reiniciar NS | `[REJOIN] +++ EXITOSO +++` |

### 10.2 Pruebas de regresión

- Verificar que `sendReceive` sigue devolviendo ventana, no longitud.
- Verificar que `downlinkLen` se reinicia a `sizeof(downlinkData)` antes de cada llamada.
- Verificar que el parser no acepta longitudes impares.
- Verificar que el heartbeat no se envía sin sesión.

### 10.3 Pruebas no cubiertas

No hay tests automatizados ni CI. Cualquier refactor debe repetir la tabla 10.1 en hardware.

---

## 11. Limitaciones de diseño

| Limitación | Consecuencia | Workaround |
|---|---|---|
| Sin NVM | Join en cada arranque | `setNvm` o ABP |
| Sin RTOS | Un uplink bloquea el bucle | Payloads cortos |
| Un solo `fPort` | Sin multiplexación | Opcode en el payload |
| Sin autenticación de aplicación | Downlink válido = comando válido | Validar en el receptor |
| Sin OTA | Actualización por USB | — |
| `0xBB` ambiguo | Heartbeat vs dato | Opcode reservado |

---

## 12. Referencias internas

- [`datasheet.md`](datasheet.md) — manual de operador.
- [`REFERENCES.md`](REFERENCES.md) — fuentes oficiales.
- [`BITACORA.md`](BITACORA.md) — historia de las decisiones.

---

*Última revisión: 2026. Firmware v1.3b.*
