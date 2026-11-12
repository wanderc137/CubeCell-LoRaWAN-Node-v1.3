# Datasheet — CubeCell LoRaWAN Bridge v1.3

**Manual de operador**

| Campo | Valor |
|---|---|
| Producto | Nodo puente Serial ↔ LoRaWAN |
| Firmware | `v1.3b` |
| Hardware de referencia | Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262) |
| Región | AU915, sub-banda 2 (916.8–918.2 MHz) |
| Activación | OTAA, Clase A |
| Stack | RadioLib 7.7.1 |
| Documento | Rev. 1 — 2026 |
| Licencia | MIT © 2026 wanderc137 |

---

## 1. Propósito

Este dispositivo transporta **bytes arbitrarios** entre un puerto serie y una red LoRaWAN, en ambas direcciones. No adquiere datos: es infraestructura de comunicación.

Se usa cuando un equipo solo dispone de UART y se necesita conectividad de largo alcance sin WiFi ni celular. El equipo conectado envía una línea en hexadecimal; el nodo la transmite como uplink. Cuando el servidor de red tiene un comando encolado, el nodo lo recibe en la ventana de recepción y lo imprime por el mismo puerto.

### 1.1 Lo que el operador debe saber antes de usarlo

1. **No hay esquema de payload.** Los bytes que entran son los que salen. La interpretación es responsabilidad de la aplicación.
2. **La recepción es por ventanas.** En Clase A el nodo solo escucha ~1 s y ~2 s después de cada transmisión. Un downlink puede tardar hasta el próximo heartbeat (30 s).
3. **El heartbeat es obligatorio para recibir.** Sin uplinks no hay ventanas, y sin ventanas no hay comandos.
4. **La sesión no persiste.** Cada reinicio implica un join nuevo; algunos servidores rechazan `DevNonce` repetidos.
5. **Las credenciales están en el firmware.** No hay archivo de configuración externo en v1.3.

---

## 2. Especificaciones

### 2.1 Hardware

| Parámetro | Valor |
|---|---|
| MCU | ASR6502 (Cortex-M0+ @ 48 MHz) |
| Radio | Semtech SX1262 (SiP, integrado en el módulo) |
| Memoria | 128 KB Flash, 16 KB SRAM |
| Alimentación | 5 V por USB o 3.7 V LiPo |
| Antena | U.FL, 915 MHz, 50 Ω |
| Consumo TX | ~100–120 mA a +21 dBm |
| Consumo idle / RX | ~10–15 mA |
| Deep sleep | ~3.5 µA (no usado por el firmware) |
| Temperatura de operación | -20 a +70 °C (típica del módulo) |
| Dimensiones | ~50 × 23 mm (sin antena) |

### 2.2 Radio y protocolo

| Parámetro | Valor |
|---|---|
| Banda | AU915 (902–928 MHz) |
| Sub-banda | FSB 2: canales 8–15, 916.8–918.2 MHz |
| Espaciado de canal | 200 kHz |
| Potencia de transmisión | Hasta +21 dBm |
| Modulación | LoRa CSS, BW125 para uplink |
| RX2 | 923.3 MHz, SF12 / BW500 (DR8) |
| Downlinks | 923.3–927.5 MHz |
| Clase | A |
| Activación | OTAA 1.0.3/1.0.4 |
| Puertos | `fPort 1` para toda la aplicación |
| Payload máximo | 51 bytes |
| Confirmación | Deshabilitada |

### 2.3 Puerto serie

| Parámetro | Valor |
|---|---|
| Interfaz | USB CDC de la placa (`/dev/ttyUSB0`, `/dev/ttyACM0`, `COMx`) |
| Baudrate | 115200 |
| Formato | 8N1, sin flow control |
| Fin de línea | `\n` o `\r` |
| Entrada | HEX, máximo 255 caracteres por línea |
| Timeout de línea | 500 ms desde el último carácter |
| Prompt | `> ` |

### 2.4 Tiempo en el aire (referencia)

| SF | 1 byte | 13 bytes | 51 bytes |
|---|---|---|---|
| SF7 | 31 ms | 51 ms | 133 ms |
| SF9 | 103 ms | 165 ms | 390 ms |
| SF12 | 827 ms | 1155 ms | 2465 ms |

Con heartbeat cada 30 s y SF12, el nodo ocupa ~40 minutos de aire por día. En una red pública esto es excesivo; ver §9.

---

## 3. Instalación física

### 3.1 Contenido necesario

- 1 × Heltec CubeCell HTCC-AB01
- 1 × antena 915 MHz con conector U.FL
- 1 × cable USB **de datos**
- 1 × gateway AU915 configurado en FSB 2
- 1 × PC con monitor serie

### 3.2 Montaje

1. **Conecta la antena antes de energizar.** Transmitir sin carga puede dañar el PA del SX1262.
2. Conecta el USB al PC. La placa enumera como puerto serie.
3. Verifica el puerto con `arduino-cli board list` o el administrador de dispositivos.
4. Abre el monitor a **115200 8N1**.
5. Reinicia la placa (botón `RST` o reconexión del USB).

### 3.3 Integración por UART (opcional)

El firmware usa `Serial`, que sale por USB. Para conectar un equipo externo por TTL hay que recompilar con `Serial1` (ver `developer.md`, §8.3) y cablear:

| CubeCell | Equipo externo |
|---|---|
| `GPIO6` (TX1) | RX |
| `GPIO7` (RX1) | TX |
| `GND` | GND |

Nunca apliques 5 V a los GPIO. Usa level shifter o divisor si el otro equipo es de 5 V.

---

## 4. Puesta en marcha

### 4.1 Registro en el Network Server

1. Crear la aplicación.
2. Registrar el gateway (si el NS es externo): región AU915, sub-banda 2.
3. Registrar el dispositivo:
   - Activación: **OTAA**
   - Versión: **1.0.3 / 1.0.4**
   - Región: **AU915, FSB 2**
   - Clase: **A**
   - `DevEUI`, `JoinEUI`, `AppKey`
4. Guardar la `AppKey` en un lugar seguro.
5. Anotar el `DevEUI` para identificarlo en el tráfico.

### 4.2 Primer arranque

La secuencia esperada por el puerto serie es:

```
=== CubeCell LoRaWAN Bridge v1.3 (Serial ↔ LoRaWAN + Downlink) ===
Radio OK
Node OK
scanGuard=200ms, millis sleep
Joining...
+++ JOINED +++
=== v1.3: Bridge Serial + Downlink ===
Escribe HEX + Enter para enviar.
Downlinks del gateway se mostraran como [DOWNLINK]
>
```

Si el join falla, el nodo queda operativo pero sin sesión: cada envío posterior intentará un re-join.

### 4.3 Prueba de humo

```
> DE AD BE EF
>> Enviando: DE AD BE EF → OK (sin downlink)
>
```

Verifica en el NS que el uplink llegó con el payload `DEADBEEF` en `fPort 1`. Luego envía un downlink `686F6C61`; en un máximo de 30 s debe aparecer:

```
[DOWNLINK] 68 6F 6C 61  →  "hola"
```

---

## 5. Operación

### 5.1 Enviar un uplink

Escribe bytes en hexadecimal, separados o no, y pulsa `Enter`.

| Entrada | Resultado |
|---|---|
| `01 02 03` | 3 bytes |
| `010203` | 3 bytes |
| `01:02:03` | 3 bytes |
| `0x01 0x02` | **Rechazado** (longitud impar tras limpiar) |
| `ABC` | **Rechazado** (longitud impar) |
| `HOLA` | **Rechazado** (sin dígitos hex) |
| 52 bytes o más | **Truncado a 51 sin aviso** |

Reglas prácticas:

- No uses prefijo `0x`.
- Pega la línea completa; una pausa mayor a 500 ms descarta el buffer.
- Verifica siempre la línea `>> Enviando:` para confirmar la longitud real.
- Una línea vacía no hace nada.

### 5.2 Recibir un downlink

El formato es fijo:

```
[DOWNLINK] <hex separado por espacios>  →  "<ascii>"
```

Los bytes no imprimibles se muestran como `.`. El prefijo `[DOWNLINK] ` es un delimitador estable; la línea termina en `\n`. El firmware hace `Serial.flush()` después de imprimir para no perder el mensaje si el equipo conectado se reinicia.

### 5.3 Heartbeat

Cada 30 s sin uplinks:

```
[HEARTBEAT] >> Enviando: BB → OK (sin downlink)
```

No es telemetría: su función es abrir la ventana de recepción. Cualquier uplink manual reinicia el contador. Si el nodo no está unido, el heartbeat no se envía.

### 5.4 Re-join

Antes de cada envío se comprueba la sesión. Si se perdió:

```
[REJOIN] Intentando re-join...
[REJOIN] +++ EXITOSO +++
```

Si falla:

```
[REJOIN] Fallo: -1116 (NO_JOIN_ACCEPT)
!! Sin sesion LoRaWAN, reintentando...
```

Se reintenta una vez tras 5 s. Si vuelve a fallar, el comando se descarta y se avisa con `!! No se pudo recuperar sesion`.

---

## 6. Downlinks

### 6.1 Desde la interfaz web del gateway

1. Abrir la web del gateway (ej. `https://192.0.2.10`).
2. **LoRa → Network Server → Device**.
3. Seleccionar el dispositivo por `DevEUI`.
4. **Send Downlink**.
5. Payload en HEX, `Port = 1`, `Confirmed = false`, `Class = A`.
6. Enviar y esperar ≤ 30 s.

### 6.2 Desde MQTT

```bash
mosquitto_pub -h 192.0.2.20 -p 1883 \
  -u '<USUARIO_MQTT>' -P '<PASSWORD_MQTT>' \
  -t '/downlink/<DevEUI>' -m '686F6C61'
```

El topic exacto depende del NS. Verifica el esquema en la configuración del gateway antes de publicar.

### 6.3 Comportamiento y límites

| Situación | Comportamiento |
|---|---|
| Nodo en ventana RX | Entrega inmediata |
| Nodo fuera de ventana | Encolado hasta el próximo uplink |
| Sin uplinks | Nunca se entrega |
| Downlink > 51 bytes | Rechazado por el NS o truncado |
| Downlink confirmado | Genera uplink de ACK; consume airtime |
| Downlink MAC (`00`, `03`, `06`) | Se imprime igual, sin filtrar |

---

## 7. Formato de payload

### 7.1 Uplink

| Offset | Tamaño | Campo | Valor |
|---|---|---|---|
| 0 | 1–51 | Datos de aplicación | Bytes crudos introducidos por serie |

No hay cabecera, versión, longitud ni checksum de aplicación. El cifrado y el MIC los añade el stack.

- `fPort`: 1
- Confirmado: no
- Endianidad: la del protocolo que uses; se recomienda big-endian para multibyte.

### 7.2 Heartbeat

| Payload | Longitud | Significado |
|---|---|---|
| `BB` | 1 | Heartbeat |

`BB` es ambiguo con un dato de usuario. Si la aplicación necesita distinguirlos, reserva un opcode propio.

### 7.3 Downlink

Mismo formato: bytes crudos en `fPort 1`. No hay ACK de aplicación.

### 7.4 Decoder de referencia

```js
function decodeUplink(input) {
  const hex = input.bytes.map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  const ascii = input.bytes.map(b => (b >= 32 && b <= 126) ? String.fromCharCode(b) : '.').join('');
  return {
    data: {
      hex, ascii,
      length: input.bytes.length,
      heartbeat: input.bytes.length === 1 && input.bytes[0] === 0xBB
    },
    warnings: [], errors: []
  };
}
```

---

## 8. Diagnóstico

### 8.1 Mensajes de arranque

| Mensaje | Significado | Acción |
|---|---|---|
| `Radio OK` | SX1262 responde por SPI | — |
| `Radio init FAILED: -2` | `CHIP_NOT_FOUND` | Revisar placa/FQBN; posible daño |
| `Node OK` | Credenciales válidas en RAM | — |
| `Node init FAILED` | Error de parámetros | Revisar longitudes y orden |
| `scanGuard=200ms` | Workaround de timers activo | — |
| `+++ JOINED +++` | Join aceptado | — |
| `Join FAILED: -1116` | Rechazo del servidor | Ver §8.3 |

### 8.2 Mensajes de operación

| Mensaje | Significado |
|---|---|
| `>> Enviando: ...` | Uplink en curso |
| `→ OK (sin downlink)` | Transmitido, sin respuesta |
| `→ OK + downlink (N bytes)` | Transmitido y recibido downlink |
| `→ ERROR -N (NOMBRE)` | Fallo de radio o de red |
| `?? Error: escribe hex valido` | Entrada inválida |
| `[REJOIN] ...` | Recuperación de sesión |
| `!! Sin sesion LoRaWAN` | Sin sesión tras reintento |

### 8.3 Códigos de error

| Código | Nombre | Causa | Solución |
|---|---|---|---|
| `-2` | `CHIP_NOT_FOUND` | SPI/placa incorrecta | Verificar FQBN y hardware |
| `-3` | `MIC_MISMATCH` | Claves distintas o trama corrupta | Revisar `AppKey` |
| `-4` | `PACKET_TOO_LONG` | Payload > DR | Acortar |
| `-6` | `RX_TIMEOUT` | Sin downlink en la ventana | Normal |
| `-1105` | `NETWORK_NOT_JOINED` | Sin sesión | Re-join |
| `-1116` | `NO_JOIN_ACCEPT` | `DevNonce` repetido o credenciales | Reset en NS / revisar claves |

### 8.4 Tabla de resolución rápida

| Síntoma | Causa más probable | Acción |
|---|---|---|
| Join eterno sin código | Gateway en otra FSB | Configurar FSB 2 |
| `NO_JOIN_ACCEPT` repetido | Nonce o claves | Reset en NS; verificar `AppKey` |
| Downlink nunca llega | Sin uplinks o nada encolado | Enviar comando manual |
| Downlink a los 30 s | Clase A normal | Forzar uplink |
| Downlink truncado | > 51 bytes | Reducir |
| Texto ilegible | Baudrate | 115200 8N1 |
| Se resetea al transmitir | Caída de tensión | Fuente robusta |
| El equipo externo no ve nada | `Serial` vs `Serial1` | Recompilar con `Serial1` |

---

## 9. Operación en red pública

Un heartbeat de 30 s genera **2880 uplinks/día**. En TTN o cualquier red compartida esto viola las políticas de uso justo. Antes de desplegar:

1. Cambia `HEARTBEAT_MS` a 300000–900000 (5–15 min).
2. Reduce el payload al mínimo.
3. Considera deshabilitar el heartbeat si no necesitas downlinks.
4. Si necesitas baja latencia, usa Clase C (fuera del alcance de v1.3).
5. Revisa el duty cycle local: AU915 no impone 1 %, pero la cortesía de espectro sí.

---

## 10. Mantenimiento

| Tarea | Frecuencia | Procedimiento |
|---|---|---|
| Verificar join | Mensual | Reiniciar y mirar el puerto |
| Revisar antena | Trimestral | Conexión U.FL firme, sin corrosión |
| Rotar `AppKey` | Anual o ante incidente | Recompilar y re-registrar |
| Actualizar firmware | Según necesidad | `arduino-cli upload` |
| Limpiar polvo | Semestral | Aire comprimido, carcasa cerrada |

No hay actualización OTA. Cualquier cambio requiere acceso físico al USB.

---

## 11. Checklist de aceptación

- [ ] La placa enumera como puerto serie a 115200 8N1.
- [ ] Aparece `Radio OK` al reiniciar.
- [ ] Aparece `Node OK`.
- [ ] El join termina en `+++ JOINED +++`.
- [ ] Un uplink de `DE AD BE EF` se ve en el NS como `DEADBEEF`.
- [ ] Un downlink `686F6C61` imprime `"hola"` en ≤ 30 s.
- [ ] El heartbeat aparece cada 30 s sin tráfico.
- [ ] Un payload de 51 bytes transmite sin error.
- [ ] Un payload de 60 bytes se trunca a 51 (comportamiento conocido).
- [ ] El nodo se recupera de un reinicio del NS con re-join.

---

## 12. Glosario

| Término | Definición |
|---|---|
| **ADR** | Adaptive Data Rate: el NS ajusta SF/potencia |
| **Airtime** | Tiempo que ocupa una transmisión en el canal |
| **AppKey** | Clave raíz de 128 bits para OTAA |
| **AppSKey** | Clave de sesión para cifrar payload |
| **Clase A** | Dos ventanas RX tras cada uplink |
| **DevEUI** | Identificador único de 64 bits del dispositivo |
| **DevNonce** | Contador antirrepetición del join |
| **DR** | Data Rate: combinación de SF y BW |
| **FSB** | Sub-banda de 8 canales en AU915 |
| **JoinEUI** | Identificador del servidor de join |
| **MIC** | Código de integridad de 4 bytes |
| **NwkSKey** | Clave de sesión para el MIC |
| **OTAA** | Activación por join en el aire |
| **SF** | Spreading Factor (7–12) |
| **Uplink** | Mensaje nodo → red |

---

## 13. Advertencias

- **Antena siempre conectada** antes de energizar.
- **No aplicar 5 V a GPIO.** El ASR6502 no es tolerante.
- **No exponer el USB** en una instalación final: da acceso total al nodo.
- **No publicar credenciales.** `AppKey`, `NwkKey` y `DevEUI` reales no van al repositorio.
- **No usar en control crítico** sin validación propia: el firmware es un prototipo.
- **No asumir entrega de downlinks.** Clase A no garantiza que el comando llegue; diseña idempotente.

---

*Documento derivado del firmware `v1.3b` y validado en hardware real (HTCC-AB01 + UG56). Las cifras de consumo y alcance son orientativas.*
