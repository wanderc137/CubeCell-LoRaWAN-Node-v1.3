/*
 * CubeCell-LoRaWAN-Node.ino  —  v1.3 Serial-to-LoRaWAN Bridge + Downlink
 * ===================================================================
 * Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262)
 * Region: AU915 sub-banda 2, OTAA Clase A
 * RadioLib 7.7.1
 *
 * FIX v1.3b: sendReceive() retorna el numero de ventana RX (1 o 2),
 *            NO la longitud del downlink. Se usa downlinkLen en lugar
 *            del valor de retorno. Asi se reciben multiples bytes. 
 *            (Gracias al reporte de la comunidad.)
 *
 * WORKAROUND: millis() sleep + scanGuard=200 para timer bug ASR6502.
 *
 * === DOS FUNCIONES EN UNA ===
 *
 * 1) BRIDGE SERIAL (v1.2):
 *    Escribes HEX en el serial y se envia INMEDIATAMENTE al gateway.
 *    Ej: "01 02 03" + Enter → Payload(hex): 010203
 *
 * 2) DOWNLINK  (NUEVO en v1.3):
 *    La CubeCell envia un heartbeat cada 30s automaticamente.
 *    Si el gateway responde con un comando downlink, este SE IMPRIME
 *    en el puerto serial para que lo leas con cutecom (o lo recoja
 *    un ESP32 conectado por TTL).
 *
 *    Salida en cutecom cuando llega un comando:
 *      [DOWNLINK] 68 6F 6C 61  →  "hola"
 *
 *    Tu ESP32 conectado al serial de la CubeCell recibe
 *    el comando y ejecuta la accion (abrir rele, leer sensor, etc.)
 *
 * ===================================================================
 */

#include <RadioLib.h>

/* ===================================================================
 *  PINES SX1262
 *  P4_3=35(NSS), P4_6=38(DIO1), P5_7=47(RST), P4_7=39(BUSY)
 * =================================================================== */
Module mod(35, 38, 47, 39);
SX1262 radio(&mod);

/* ===================================================================
 *  CREDENCIALES OTAA — Dispositivo de ejemplo (reemplazar por las reales)
 * =================================================================== */
uint64_t joinEui = 0x0000000000000000;
uint64_t devEui  = 0x0000000000000000;
uint8_t appKey[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t nwkKey[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;

LoRaWANNode node(&radio, &Region, subBand);

/* ===================================================================
 *  BUFFERS Y CONSTANTES
 * =================================================================== */
#define MAX_PAYLOAD     51
#define SERIAL_BUF      256
#define STALE_MS        500
#define HEARTBEAT_MS    30000UL   /* heartbeat cada 30 segundos */

uint8_t  txBuffer[MAX_PAYLOAD];
size_t   txLen = 0;

char     serialLine[SERIAL_BUF];
size_t   serialPos = 0;
uint32_t lastCharMs = 0;

uint32_t lastUplinkMs = 0;        /* timestamp del ultimo uplink */
const uint8_t heartbeatPayload[] = { 0xBB }; /* marcador de heartbeat */
const size_t heartbeatLen = 1;

/* Buffer para downlink (max 51 bytes en DR2/DR3) */
uint8_t  downlinkData[51];
size_t   downlinkLen = 0;

/* ===================================================================
 *  WORKAROUND: Sleep basado en millis()
 * =================================================================== */
static void radioSleepDelay(RadioLibTime_t ms) {
    if (ms == 0) return;
    uint32_t start = millis();
    while ((millis() - start) < ms) { /* NOP */ }
}

/* ===================================================================
 *  DIAGNOSTICO
 * =================================================================== */
#define debug(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

static const char* stateDecode(int16_t s) {
    switch(s) {
        case RADIOLIB_ERR_NONE:              return "OK";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:    return "CHIP_NOT_FOUND";
        case RADIOLIB_ERR_PACKET_TOO_LONG:   return "PACKET_TOO_LONG";
        case RADIOLIB_ERR_RX_TIMEOUT:        return "RX_TIMEOUT";
        case RADIOLIB_ERR_MIC_MISMATCH:      return "MIC_MISMATCH";
        case RADIOLIB_ERR_NETWORK_NOT_JOINED:return "NOT_JOINED";
        case RADIOLIB_ERR_NO_JOIN_ACCEPT:    return "NO_JOIN_ACCEPT";
        case RADIOLIB_LORAWAN_NEW_SESSION:   return "NEW_SESSION";
        case RADIOLIB_LORAWAN_SESSION_RESTORED: return "SESSION_RESTORED";
        default:                             return "UNKNOWN";
    }
}

/* ===================================================================
 *  PARSER HEX: "DE AD BE EF" o "DEADBEEF" → bytes
 * =================================================================== */
static size_t parseHexString(const char* input, uint8_t* output, size_t maxOut) {
    size_t outLen = 0;
    char   clean[256];
    size_t cleanLen = 0;

    for (size_t i = 0; input[i] != '\0' && cleanLen < sizeof(clean) - 1; i++) {
        char c = input[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            clean[cleanLen++] = c;
        }
    }
    clean[cleanLen] = '\0';

    if (cleanLen == 0 || cleanLen % 2 != 0) return 0;

    for (size_t i = 0; i < cleanLen && outLen < maxOut; i += 2) {
        char byteStr[3] = { clean[i], clean[i+1], '\0' };
        output[outLen++] = (uint8_t)strtoul(byteStr, NULL, 16);
    }
    return outLen;
}

/* ===================================================================
 *  REINTENTAR JOIN
 * =================================================================== */
static bool ensureJoined(void) {
    if (node.isActivated()) return true;
    debug("[REJOIN] Intentando re-join...\n");
    int state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("[REJOIN] +++ EXITOSO +++\n");
        return true;
    }
    debug("[REJOIN] Fallo: %d (%s)\n", state, stateDecode(state));
    return false;
}

/* ===================================================================
 *  MOSTRAR DOWNLINK EN SERIAL
 *  Formato: [DOWNLINK] 68 6F 6C 61  →  "hola"
 * =================================================================== */
static void printDownlink(const uint8_t* data, size_t len) {
    debug("[DOWNLINK] ");
    for (size_t i = 0; i < len; i++) {
        debug("%02X ", data[i]);
    }
    debug(" →  \"");
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c >= 32 && c <= 126) {
            debug("%c", c);
        } else {
            debug(".");
        }
    }
    debug("\"\n> ");
    Serial.flush();
}

/* ===================================================================
 *  ENVIAR PAYLOAD POR LORAWAN Y CAPTURAR DOWNLINK
 * =================================================================== */
static int sendAndReceive(const uint8_t* data, size_t len) {
    if (len == 0) return RADIOLIB_ERR_NONE;

    /* Mostrar lo que se envia */
    debug(">> Enviando: ");
    for (size_t i = 0; i < len; i++) {
        debug("%02X ", data[i]);
    }

    /* Preparar buffer de downlink */
    downlinkLen = sizeof(downlinkData);

    /* Enviar uplink y escuchar RX1/RX2 */
    int state = node.sendReceive(data, len, 1, downlinkData, &downlinkLen,
                                  false, NULL, NULL);

    if (state == RADIOLIB_ERR_NONE) {
        debug("→ OK (sin downlink)\n");
    } else if (state > 0) {
        debug("→ OK + downlink (%d bytes)\n", downlinkLen);
        /* Mostrar downlink inmediatamente */
        printDownlink(downlinkData, downlinkLen);
    } else {
        debug("→ ERROR %d (%s)\n", state, stateDecode(state));
    }

    return state;
}

/* ===================================================================
 *  ENVIAR HEARTBEAT (abre ventana RX para posibles downlinks)
 * =================================================================== */
static void sendHeartbeat(void) {
    if (!ensureJoined()) return;

    debug("[HEARTBEAT] ");
    int state = sendAndReceive(heartbeatPayload, heartbeatLen);
    lastUplinkMs = millis();

    /* Si el heartbeat fallo por sesion, ensureJoined ya lo manejo */
    (void)state;
}

/* ===================================================================
 *  PROCESAR UNA LINEA COMPLETA DEL SERIAL
 * =================================================================== */
static void processSerialLine(const char* line) {
    if (line[0] == '\0') return;

    txLen = parseHexString(line, txBuffer, MAX_PAYLOAD);
    if (txLen == 0) {
        debug("?? Error: escribe hex valido. Ej: DE AD BE EF\n> ");
        Serial.flush();
        return;
    }

    if (!ensureJoined()) {
        debug("!! Sin sesion LoRaWAN, reintentando...\n");
        radioSleepDelay(5000);
        if (!ensureJoined()) {
            debug("!! No se pudo recuperar sesion\n> ");
            Serial.flush();
            return;
        }
    }

    sendAndReceive(txBuffer, txLen);
    lastUplinkMs = millis();
    debug("> ");
    Serial.flush();
}

/* ===================================================================
 *  SETUP
 * =================================================================== */
void setup() {
    Serial.begin(115200);
    delay(3000);
    debug("\n=== CubeCell LoRaWAN Bridge v1.3 (Serial ↔ LoRaWAN + Downlink) ===\n");

    /* Inicializar radio */
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        debug("Radio init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Radio OK\n");

    /* Inicializar nodo OTAA */
    state = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
    if (state != RADIOLIB_ERR_NONE) {
        debug("Node init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Node OK\n");

    /* Workaround */
    node.setSleepFunction(radioSleepDelay);
    node.scanGuard = 200;
    debug("scanGuard=%dms, millis sleep\n", node.scanGuard);

    /* Join inicial */
    debug("Joining...\n");
    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("+++ JOINED +++\n");
    } else {
        debug("Join FAILED: %d (%s)\n", state, stateDecode(state));
    }

    /* Inicializar timer de heartbeat */
    lastUplinkMs = millis();

    /* Prompt */
    debug("=== v1.3: Bridge Serial + Downlink ===\n");
    debug("Escribe HEX + Enter para enviar.\n");
    debug("Downlinks del gateway se mostraran como [DOWNLINK]\n> ");
    Serial.flush();
}

/* ===================================================================
 *  LOOP
 * =================================================================== */
void loop() {
    /* Leer caracteres del Serial */
    while (Serial.available() > 0) {
        char c = Serial.read();
        lastCharMs = millis();

        if (c == '\n' || c == '\r') {
            if (serialPos > 0) {
                serialLine[serialPos] = '\0';
                processSerialLine(serialLine);
                serialPos = 0;
            }
        } else if (serialPos < SERIAL_BUF - 1) {
            serialLine[serialPos++] = c;
        }
    }

    /* Stale buffer flush */
    if (serialPos > 0 && (millis() - lastCharMs) > STALE_MS) {
        serialPos = 0;
    }

    /* Heartbeat: enviar cada HEARTBEAT_MS para abrir ventana RX */
    if (node.isActivated() && (millis() - lastUplinkMs) >= HEARTBEAT_MS) {
        sendHeartbeat();
    }

    delay(10);
}
