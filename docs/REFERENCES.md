# Referencias

Fuentes usadas para el firmware, la documentación y los cálculos. Se indica qué se tomó de cada una y su grado de fiabilidad.

---

## 1. Hardware

| Recurso | Enlace | Uso |
|---|---|---|
| Heltec CubeCell (serie ASR650x) | <https://docs.heltec.org/en/node/asr650x/cube_cell_series/index.html> | Pinout, pines internos del SX1262, consumo, esquemáticos |
| Heltec CubeCell HTCC-AB01 — página de producto | <https://heltec.org/project/htcc-ab01/> | Especificaciones del módulo |
| Repositorio del core CubeCell-Arduino | <https://github.com/HelTecAutomation/CubeCell-Arduino> | `FQBN`, API, índice de placas, issues |
| Índice de placas V1.5.0 | <https://github.com/HelTecAutomation/CubeCell-Arduino/releases/download/V1.5.0/package_CubeCell_index.json> | URL para `board_manager.additional_urls` |
| Semtech SX1262 | <https://www.semtech.com/products/wireless-rf/lora-connect/sx1262> | Datasheet del transceptor, modos y consumo |
| Semtech SX1262 — datasheet (PDF) | <https://semtech.my.salesforce.com/sfc/p/#E0000000JelG/a/2R000000HSPf/6DCkMBTQOc7LqQjqTRp8F3H0J8f1u9G7p5F1kQ> | Referencia técnica del radio |

> La URL del PDF puede cambiar. Si falla, búscalo por «SX1262 datasheet» en el sitio de Semtech.

---

## 2. Librería y protocolo

| Recurso | Enlace | Uso |
|---|---|---|
| RadioLib — documentación | <https://jgromes.github.io/RadioLib/> | API de `SX1262`, `LoRaWANNode`, `sendReceive` |
| RadioLib — repositorio | <https://github.com/jgromes/RadioLib> | Código fuente, ejemplos, changelog |
| RadioLib — releases | <https://github.com/jgromes/RadioLib/releases> | Versión 7.7.1 fijada |
| LoRa Alliance — resource hub | <https://lora-alliance.org/resource_hub/> | Especificación LoRaWAN 1.0.4 y Regional Parameters |
| LoRaWAN 1.0.4 — especificación | <https://lora-alliance.org/wp-content/uploads/2020/11/LoRaWAN1.0.4.pdf> | Clase A, OTAA, MIC, contadores |
| Regional Parameters AU915 | <https://lora-alliance.org/resource_hub/rp2-1-0-3-regional-parameters/> | Canales, FSB, RX2, potencias |
| LoRa Alliance — FAQ | <https://lora-alliance.org/about-lorawan/> | Conceptos generales |

### 2.1 Notas de la API de RadioLib

`LoRaWANNode::sendReceive` en 7.x:

```cpp
int16_t sendReceive(const uint8_t* uplink, size_t uplinkLen, uint8_t fPort,
                    uint8_t* downlink, size_t* downlinkLen,
                    bool isConfirmed = false,
                    uint8_t* dmac = NULL, uint8_t* fctrl = NULL);
```

- Retorna `RADIOLIB_ERR_NONE` si no hubo downlink.
- Retorna `1` o `2` (ventana RX) si hubo downlink.
- Retorna negativo en error.
- La longitud real se escribe en `*downlinkLen`.

Esta firma es la causa del bug corregido en v1.3b. Ver `BITACORA.md`.

---

## 3. Gateway y red

| Recurso | Enlace | Uso |
|---|---|---|
| Milesight UG56 | <https://www.milesight.com/iot/product/lorawan-gateway/ug56> | Características del gateway |
| Milesight — soporte y manuales | <https://www.milesight.com/support/> | Guías de configuración del NS embebido |
| The Things Network — docs | <https://www.thethingsnetwork.org/docs/> | Registro OTAA, decoder, downlinks |
| TTN — consola | <https://console.cloud.thethings.network/> | Gestión de dispositivos |
| ChirpStack — docs | <https://www.chirpstack.io/docs/> | Alternativa de Network Server |
| Eclipse Mosquitto | <https://mosquitto.org/documentation/> | Cliente MQTT usado en los ejemplos |

---

## 4. Herramientas

| Recurso | Enlace | Uso |
|---|---|---|
| arduino-cli | <https://arduino.github.io/arduino-cli/> | Compilación y carga |
| arduino-cli — instalación | <https://arduino.github.io/arduino-cli/latest/installation/> | Binario y configuración |
| Arduino — referencia del lenguaje | <https://www.arduino.cc/reference/en/> | `Serial`, `millis`, `delay` |
| RFC 5737 | <https://datatracker.ietf.org/doc/html/rfc5737> | Rangos IP de documentación |
| RFC 2119 | <https://datatracker.ietf.org/doc/html/rfc2119> | Uso de MAY/SHOULD en la spec |

---

## 5. Regulación (AU915)

| Recurso | Enlace | Uso |
|---|---|---|
| ACMA — LIPD Class Licence | <https://www.acma.gov.au/licences/low-interference-potential-devices-lipd-class-licence> | Potencia y bandas en Australia |
| ACMA — radiocommunications | <https://www.acma.gov.au/radiocommunications> | Marco general |

**Resumen:** la clase LIPD permite 915–928 MHz con hasta 1 W EIRP y no impone duty cycle. Con +21 dBm, la antena no debería superar ~9 dBi para no exceder 1 W EIRP.

> Esta síntesis es orientativa y no constituye asesoramiento legal. Verifica la licencia vigente y las condiciones de tu instalación.

---

## 6. Cálculos

Los airtimes de la documentación se calcularon con la fórmula de Semtech para LoRa:

```
Tsym = 2^SF / BW
Tpreamble = (nPreamble + 4.25) * Tsym
Tpayload = (8 + max(ceil((8*PL - 4*SF + 28 + 16*CRC - 20*IH) / (4*(SF - 2*DE))) * (CR + 4), 0)) * Tsym
Ttotal = Tpreamble + Tpayload
```

Parámetros usados: BW = 125 kHz, CR = 4/5, preámbulo = 8, CRC activo, IH = 0, DE = 1 para SF11/SF12.

| SF | 1 B | 13 B | 51 B |
|---|---|---|---|
| 7 | 31 ms | 51 ms | 133 ms |
| 8 | 52 ms | 93 ms | 226 ms |
| 9 | 103 ms | 165 ms | 390 ms |
| 10 | 207 ms | 330 ms | 698 ms |
| 11 | 414 ms | 578 ms | 1315 ms |
| 12 | 827 ms | 1155 ms | 2465 ms |

Los valores son teóricos; el modem añade unos milisegundos de overhead.

---

## 7. Fuentes descartadas

| Fuente | Motivo |
|---|---|
| Ejemplo `LoRaWan_APP.h` de Heltec | Bug de timers en ASR6502; ver `BITACORA.md` |
| Foros sin fecha ni versión | No verificables |
| Blogs con cifras de alcance sin metodología | Se citan solo como órdenes de magnitud |
| `CubeCellLib.a` (binario) | Caja negra; se prefirió RadioLib |

---

## 8. Cómo citar este proyecto

```
wanderc137. CubeCell LoRaWAN Bridge v1.3. 2026.
https://github.com/wanderc137/CubeCell-LoRaWAN-Node-v1.3
```

---

*Si encuentras un enlace roto, abre un issue con la URL y la fecha.*
