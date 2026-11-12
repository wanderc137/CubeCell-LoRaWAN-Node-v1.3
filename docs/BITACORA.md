# Bitácora — CubeCell LoRaWAN Bridge

Registro de decisiones, problemas y descartes durante el desarrollo de v1.1 → v1.3b. El objetivo es que quien retome el proyecto no repita los mismos caminos.

---

## 1. Cronología

### v1.1 — Primer intento con el stack de Heltec

**Objetivo:** nodo Clase A con payload fijo cada 60 s usando el ejemplo oficial (`LoRaWan_APP.h`).

**Qué funcionó:** compilación, radio, transmisión.

**Qué falló:** el join era intermitente. El `JoinRequest` salía, el gateway registraba la petición, pero el nodo no veía el `JoinAccept`.

**Hipótesis descartadas:**

| Hipótesis | Prueba | Resultado |
|---|---|---|
| Credenciales incorrectas | Verificar EUIs y `AppKey` contra el NS | Descartada |
| Gateway en otra sub-banda | Revisar configuración del UG56 | Descartada (estaba en FSB 2) |
| Antena o potencia | Probar con otra antena y distancia corta | Descartada |
| Cobertura | Acercar el nodo al gateway | Descartada |

**Causa:** los timers del core de CubeCell sobre ASR6502 se escalan ~100×. Las ventanas RX no coinciden con la respuesta del servidor. Es un problema conocido y comentado en issues del repositorio de Heltec, sin corrección en el binario.

**Decisión:** abandonar `CubeCellLib.a`.

---

### v1.2 — Migración a RadioLib

**Cambio:** reescribir el sketch sobre RadioLib 7.7.1 (`SX1262` + `LoRaWANNode`).

**Por qué RadioLib:**

- Stack LoRaWAN en C++ puro, sin binario cerrado.
- Driver del SX1262 maduro y documentado.
- Control explícito de las ventanas y del sleep.
- Portabilidad a otros radios.

**Problema residual:** el sleep de RadioLib terminaba llamando a `delay()`, que arrastra el mismo escalado.

**Solución:** inyectar `radioSleepDelay()` con `millis()`:

```cpp
static void radioSleepDelay(RadioLibTime_t ms) {
    uint32_t start = millis();
    while ((millis() - start) < ms) { /* NOP */ }
}
node.setSleepFunction(radioSleepDelay);
node.scanGuard = 200;
```

`millis()` usa el RTC de 32.768 kHz, preciso. `scanGuard = 200` añade margen al cálculo de la ventana.

**Resultado:** join reproducible en todas las pruebas.

**Coste:** busy-wait durante las ventanas (1–2 s). Aceptable para un nodo alimentado por USB.

---

### v1.3 — Bridge interactivo, heartbeat y downlink

**Objetivo:** dejar de enviar un payload fijo y transportar bytes arbitrarios; agregar recepción de comandos.

**Implementado:**

- Lectura de líneas HEX por `Serial`.
- `parseHexString` con limpieza de separadores.
- Heartbeat `0xBB` cada 30 s.
- `printDownlink` con hex y ASCII.

**Bug encontrado:** los downlinks de varios bytes se imprimían truncados a 1 byte.

**Diagnóstico:** el código usaba el valor de retorno de `sendReceive()` como longitud:

```cpp
int state = node.sendReceive(...);
if (state > 0) printDownlink(downlinkData, state);   // MAL
```

`sendReceive()` devuelve la **ventana RX** (1 o 2), no la longitud. Como el retorno era 1, siempre se imprimía 1 byte.

**Corrección (v1.3b):**

```cpp
size_t downlinkLen = sizeof(downlinkData);
int state = node.sendReceive(data, len, 1, downlinkData, &downlinkLen, false, NULL, NULL);
if (state > 0) printDownlink(downlinkData, downlinkLen);
```

**Lección:** leer la firma de la función, no inferirla por el nombre. `downlinkLen` es in/out y el stack lo actualiza.

---

## 2. Decisiones técnicas

| Decisión | Alternativa descartada | Motivo |
|---|---|---|
| RadioLib en vez de `CubeCellLib.a` | Stack oficial de Heltec | Bug de timers; caja negra |
| Sleep con `millis()` | `delay()` del core | Escalado ~100× |
| `scanGuard = 200` | Ajuste empírico menor | Margen suficiente y estable |
| Payload crudo sin esquema | Protocolo con cabecera | Flexibilidad; la app decide |
| `fPort 1` fijo | Puertos por tipo de dato | Simplicidad; un solo flujo |
| Heartbeat 30 s | 5 min o 1 h | Latencia de downlink aceptable en pruebas |
| Sin confirmación | Uplinks confirmados | Ahorro de airtime |
| Credenciales en el `.ino` | Header externo | Simplicidad del prototipo (deuda) |
| Sin NVM | Persistencia de sesión | Tiempo de desarrollo; se documenta |
| Sin deep sleep | Ahorro de batería | Nodo alimentado por USB |
| Impresión de todo downlink | Filtro de MAC commands | Diagnóstico primero |
| `Serial` (USB) | `Serial1` (TTL) | Comodidad de desarrollo |

---

## 3. Caminos descartados

### 3.1 Parchear el core de Heltec

Se consideró modificar los timers del core. Se descartó por:

- Atar el proyecto a una versión concreta.
- Dependencia de un binario sin fuente clara.
- Difícil de mantener y de auditar.

### 3.2 Usar ABP

ABP evita el join y el `DevNonce`, pero:

- Pierde forward secrecy.
- Requiere fijar claves de sesión en el firmware.
- El NS debe aceptar contadores reiniciados.

Se dejó como opción documentada, no como implementación.

### 3.3 Agregar un protocolo binario propio

Se evaluó una cabecera `[opcode][len][payload][crc]`. Se descartó porque el nodo debe ser transparente: la aplicación que lo use puede tener su propio framing. La documentación sugiere una convención, pero el firmware no la impone.

### 3.4 Deep sleep con heartbeat

Incompatible: dormir cierra las ventanas RX. Se priorizó la recepción de comandos.

### 3.5 Clase C

Requiere escucha continua y consume mucha más energía. El hardware puede, pero el caso de uso no lo justificaba.

### 3.6 MQTT directo desde el nodo

Imposible: el ASR6502 no tiene pila TCP/IP y LoRaWAN no transporta IP. El gateway es el puente.

---

## 4. Problemas y soluciones

| Problema | Síntoma | Causa | Solución |
|---|---|---|---|
| Join intermitente | `Joining...` sin `JOINED` | Timers escalados | RadioLib + `millis()` |
| Downlink de 1 byte | `[DOWNLINK] 68 → "h"` | Retorno de `sendReceive` | Usar `downlinkLen` |
| Join rechazado tras reinicios | `-1116` | `DevNonce` repetido | Reset en el NS |
| Línea ignorada | Sin respuesta | Pausa > 500 ms | Pegar de una vez |
| Payload inesperado | Longitud menor | Truncado a 51 | Verificar `>> Enviando` |
| Gateway no ve el nodo | Sin uplink en el NS | FSB distinta | Configurar FSB 2 |
| Texto ilegible | Basura en el monitor | Baudrate | 115200 8N1 |

---

## 5. Lecciones aprendidas

1. **Los stacks propietarios pueden tener bugs que no se documentan.** Si el síntoma es «casi funciona», sospecha del timing.
2. **`millis()` es más confiable que `delay()`** en plataformas con timers mal configurados.
3. **Lee la firma de las funciones de una librería.** Un valor de retorno puede significar algo distinto de lo que sugiere el nombre.
4. **Un prefijo estable (`[DOWNLINK] `) vale más que un formato bonito.** Permite parsear sin ambigüedad.
5. **El truncado silencioso es una deuda peligrosa.** En la próxima versión, avisar.
6. **En Clase A, el heartbeat es la API de recepción.** Sin uplinks no hay downlinks.
7. **Documentar los descartes ahorra tiempo.** La mitad de este documento evita repetir pruebas.
8. **Las credenciales en el código son una deuda de seguridad.** Extraerlas antes de publicar cualquier fork.
9. **La sub-banda es un parámetro de despliegue, no de firmware.** Un nodo correcto puede fallar por un gateway mal configurado.
10. **Los números de alcance y consumo son orientativos.** Sin instrumentación, no son especificaciones.

---

## 6. Deuda técnica

| Deuda | Impacto | Prioridad |
|---|---|---|
| Credenciales en el `.ino` | Seguridad | Alta |
| Sin persistencia de `DevNonce` | Operación | Alta |
| Truncado silencioso | Integridad | Media |
| Heartbeat no configurable | Airtime | Media |
| Sin filtro de MAC commands | Integración | Baja |
| Sin watchdog | Robustez | Baja |
| Sin tests automatizados | Mantenimiento | Baja |
| Sin OTA | Operación | Baja |

---

## 7. Próximos pasos sugeridos

1. Extraer credenciales a `credentials.h` con `credentials.example.h` y `.gitignore`.
2. Implementar NVM para `DevNonce` y sesión.
3. Avisar del truncado en `parseHexString`.
4. Hacer `HEARTBEAT_MS` configurable por downlink o constante de compilación.
5. Filtrar downlinks MAC en una capa de aplicación.
6. Agregar watchdog.
7. Evaluar Clase C para el caso de uso con baja latencia.

---

## 8. Contexto de desarrollo

El firmware y la documentación se desarrollaron de forma iterativa con asistencia de IA, con validación en hardware real (HTCC-AB01 + UG56). Las decisiones se tomaron priorizando que el nodo fuera **reproducible y diagnosticable** antes que óptimo. Las cifras de consumo y alcance no se midieron con instrumentación de laboratorio.

---

*Bitácora cerrada en v1.3b. Cualquier cambio futuro debería agregar una entrada nueva, no reescribir esta.*
