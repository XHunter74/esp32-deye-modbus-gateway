#include "rtu.h"

#include "config.h"

RtuStats gRtuStats;

static HardwareSerial &bus = Serial2;
static SemaphoreHandle_t busMutex = nullptr;

static constexpr int LED_PIN = 2;  // on-board LED of most ESP32 DevKits

void rtuBegin() {
    busMutex = xSemaphoreCreateMutex();
    bus.setRxBufferSize(512);
    bus.begin(gConfig.baud, configSerialMode(), gConfig.rxPin, gConfig.txPin);
    // Hand bytes to the driver as soon as they arrive, so silence detection is accurate.
    bus.setRxFIFOFull(1);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
}

uint16_t modbusCrc(const uint8_t *buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

bool rtuCrcOk(const uint8_t *frame, size_t len) {
    if (len < 4) return false;
    uint16_t crc = modbusCrc(frame, len - 2);
    return frame[len - 2] == (crc & 0xFF) && frame[len - 1] == (crc >> 8);
}

size_t rtuExpectedRequestLen(const uint8_t *b, size_t have) {
    if (have < 2) return 0;
    switch (b[1]) {
        case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
            return 8;
        case 0x0F: case 0x10:
            return have >= 7 ? 9 + b[6] : 0;
        case 0x17:  // read/write multiple
            return have >= 11 ? 13 + b[10] : 0;
        default:
            return 0;
    }
}

size_t rtuExpectedResponseLen(const uint8_t *b, size_t have) {
    if (have < 2) return 0;
    if (b[1] & 0x80) return 5;  // exception
    switch (b[1]) {
        case 0x01: case 0x02: case 0x03: case 0x04: case 0x17:
            return have >= 3 ? 5 + b[2] : 0;
        case 0x05: case 0x06: case 0x0F: case 0x10:
            return 8;
        default:
            return 0;
    }
}

static void toHex(char *dst, size_t dstSize, const uint8_t *src, size_t len) {
    static const char *H = "0123456789ABCDEF";
    size_t n = 0;
    for (size_t i = 0; i < len && n + 2 < dstSize; i++) {
        dst[n++] = H[src[i] >> 4];
        dst[n++] = H[src[i] & 0xF];
    }
    dst[n] = 0;
}

RtuResult rtuTransact(const uint8_t *req, size_t reqLen, uint8_t *resp, size_t &respLen, uint32_t waitBusMs) {
    respLen = 0;
    if (xSemaphoreTake(busMutex, pdMS_TO_TICKS(waitBusMs)) != pdTRUE) return RtuResult::Busy;

    gRtuStats.requests++;
    toHex(gRtuStats.lastRequest, sizeof(gRtuStats.lastRequest), req, reqLen);
    digitalWrite(LED_PIN, HIGH);

    // Modbus needs >= 3.5 char silence between frames; also drop any stale bytes.
    delay(2);
    while (bus.available()) bus.read();

    bus.write(req, reqLen);
    bus.flush();  // wait until transmitted

    const bool broadcast = reqLen > 0 && req[0] == 0;
    const uint32_t start = millis();
    uint32_t lastByte = 0;
    size_t expected = 0;
    // 3.5 character times, but never below a few ms (UART driver latency).
    const uint32_t charUs = (1000000UL * 11) / gConfig.baud;
    const uint32_t gapMs = max<uint32_t>(gConfig.frameGapMs, (charUs * 35 / 10) / 1000 + 2);

    RtuResult result = RtuResult::Timeout;
    while (!broadcast) {
        int avail = bus.available();
        if (avail > 0) {
            while (avail-- > 0 && respLen < RTU_MAX_FRAME) resp[respLen++] = (uint8_t)bus.read();
            lastByte = millis();
            if (!expected) expected = rtuExpectedResponseLen(resp, respLen);
            if (expected && respLen >= expected) break;
            if (respLen >= RTU_MAX_FRAME) break;
            continue;
        }
        uint32_t now = millis();
        if (respLen > 0 && !expected && now - lastByte >= gapMs) break;      // unknown FC: ended by silence
        if (respLen > 0 && expected && now - lastByte >= 200) break;         // truncated frame
        if (now - start >= gConfig.respTimeoutMs) break;
        delay(1);
    }

    if (broadcast) {
        result = RtuResult::Ok;
    } else if (respLen == 0) {
        gRtuStats.timeouts++;
        result = RtuResult::Timeout;
    } else if (!rtuCrcOk(resp, respLen)) {
        gRtuStats.crcErrors++;
        result = RtuResult::BadCrc;
    } else {
        gRtuStats.responses++;
        if (resp[1] & 0x80) gRtuStats.exceptions++;
        result = RtuResult::Ok;
    }
    toHex(gRtuStats.lastResponse, sizeof(gRtuStats.lastResponse), resp, respLen);
    gRtuStats.lastActivityMs = millis();
    digitalWrite(LED_PIN, LOW);

    xSemaphoreGive(busMutex);
    return result;
}

RtuResult rtuReadRegisters(uint8_t unit, uint8_t fc, uint16_t addr, uint16_t count, uint16_t *regs, uint8_t *exc) {
    uint8_t req[8] = {unit, fc, (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(count >> 8), (uint8_t)count};
    uint16_t crc = modbusCrc(req, 6);
    req[6] = crc & 0xFF;
    req[7] = crc >> 8;

    uint8_t resp[RTU_MAX_FRAME];
    size_t len = 0;
    *exc = 0;
    RtuResult r = rtuTransact(req, sizeof(req), resp, len);
    if (r != RtuResult::Ok) return r;
    if (resp[1] & 0x80) {
        *exc = len >= 3 ? resp[2] : 0xFF;
        return RtuResult::Ok;
    }
    if (len < 5 || resp[2] != count * 2) {
        *exc = 0xFF;  // malformed / unexpected length
        return RtuResult::Ok;
    }
    for (uint16_t i = 0; i < count; i++) regs[i] = (uint16_t)(resp[3 + 2 * i] << 8) | resp[4 + 2 * i];
    return RtuResult::Ok;
}
