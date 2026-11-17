#include "tcp_bridge.h"

#include <WiFi.h>

#include "config.h"
#include "rtu.h"

namespace {

struct Slot {
    WiFiClient client;
    uint8_t buf[RTU_MAX_FRAME + 8];
    size_t len = 0;
    uint32_t lastRx = 0;
    uint32_t lastActivity = 0;
};

WiFiServer *server = nullptr;
Slot slots[TCP_MAX_CLIENTS];
volatile uint8_t clientCount = 0;
SemaphoreHandle_t listMutex = nullptr;
String clientsJson = "[]";  // cached for the web UI, rebuilt by the server task

constexpr uint32_t TCP_FRAME_GAP_MS = 50;         // gap that ends an RTU request of unknown length
constexpr uint32_t CLIENT_IDLE_TIMEOUT_MS = 300000;  // drop silent clients after 5 min

void acceptClients() {
    while (server->hasClient()) {
        WiFiClient c = server->available();
        int freeIdx = -1;
        for (int i = 0; i < TCP_MAX_CLIENTS; i++)
            if (!slots[i].client || !slots[i].client.connected()) { freeIdx = i; break; }
        if (freeIdx < 0) {
            // all slots busy: replace the least recently active client
            freeIdx = 0;
            for (int i = 1; i < TCP_MAX_CLIENTS; i++)
                if (slots[i].lastActivity < slots[freeIdx].lastActivity) freeIdx = i;
            slots[freeIdx].client.stop();
        }
        c.setNoDelay(true);
        slots[freeIdx].client = c;
        slots[freeIdx].len = 0;
        slots[freeIdx].lastActivity = millis();
    }
}

void handleRtu(Slot &s) {
    // Determine whether a full RTU request is buffered.
    size_t expected = rtuExpectedRequestLen(s.buf, s.len);
    bool complete = expected ? s.len >= expected : (s.len > 0 && millis() - s.lastRx >= TCP_FRAME_GAP_MS);
    if (!complete) return;
    size_t frameLen = expected ? expected : s.len;

    uint8_t resp[RTU_MAX_FRAME];
    size_t respLen = 0;
    RtuResult r = rtuTransact(s.buf, frameLen, resp, respLen);
    // Pass the response through as received (also on CRC errors: the client validates CRC itself).
    if ((r == RtuResult::Ok || r == RtuResult::BadCrc) && respLen > 0) s.client.write(resp, respLen);

    memmove(s.buf, s.buf + frameLen, s.len - frameLen);
    s.len -= frameLen;
}

void handleMbap(Slot &s) {
    if (s.len < 7) return;
    uint16_t pduLen = (uint16_t)(s.buf[4] << 8) | s.buf[5];  // unit id + PDU
    if (pduLen < 2 || pduLen > RTU_MAX_FRAME - 2) {           // garbage: drop buffer
        s.len = 0;
        return;
    }
    size_t frameLen = 6 + pduLen;
    if (s.len < frameLen) return;

    // MBAP -> RTU: unit + PDU + CRC
    uint8_t req[RTU_MAX_FRAME];
    memcpy(req, s.buf + 6, pduLen);
    uint16_t crc = modbusCrc(req, pduLen);
    req[pduLen] = crc & 0xFF;
    req[pduLen + 1] = crc >> 8;

    uint8_t resp[RTU_MAX_FRAME];
    size_t respLen = 0;
    RtuResult r = rtuTransact(req, pduLen + 2, resp, respLen);

    uint8_t out[RTU_MAX_FRAME + 8];
    memcpy(out, s.buf, 4);  // transaction id + protocol id
    size_t outPdu;
    if (r == RtuResult::Ok && respLen >= 4) {
        outPdu = respLen - 2;  // strip CRC
        memcpy(out + 6, resp, outPdu);
    } else {
        // Gateway target device failed to respond (exception 0x0B)
        out[6] = req[0];
        out[7] = req[1] | 0x80;
        out[8] = 0x0B;
        outPdu = 3;
    }
    out[4] = outPdu >> 8;
    out[5] = outPdu & 0xFF;
    if (req[0] != 0) s.client.write(out, 6 + outPdu);  // no reply to broadcast

    memmove(s.buf, s.buf + frameLen, s.len - frameLen);
    s.len -= frameLen;
}

void serverTask(void *) {
    for (;;) {
        acceptClients();
        uint8_t count = 0;
        bool idle = true;
        for (auto &s : slots) {
            if (!s.client) continue;
            if (!s.client.connected() || millis() - s.lastActivity > CLIENT_IDLE_TIMEOUT_MS) {
                s.client.stop();
                s.len = 0;
                continue;
            }
            count++;
            int avail = s.client.available();
            if (avail > 0) {
                size_t room = sizeof(s.buf) - s.len;
                if (room == 0) s.len = 0, room = sizeof(s.buf);  // overflow: resync
                s.len += s.client.read(s.buf + s.len, min((size_t)avail, room));
                s.lastRx = s.lastActivity = millis();
                idle = false;
            }
            if (s.len) {
                if (gConfig.mode == GwMode::ModbusTcp) handleMbap(s);
                else handleRtu(s);
            }
        }
        clientCount = count;
        static uint32_t lastList = 0;
        if (millis() - lastList > 1000) {
            lastList = millis();
            String js = "[";
            for (auto &slot : slots) {
                if (!slot.client || !slot.client.connected()) continue;
                if (js.length() > 1) js += ',';
                js += '"' + slot.client.remoteIP().toString() + ':' + String(slot.client.remotePort()) + '"';
            }
            js += ']';
            xSemaphoreTake(listMutex, portMAX_DELAY);
            clientsJson = js;
            xSemaphoreGive(listMutex);
        }
        if (idle) vTaskDelay(pdMS_TO_TICKS(2));
    }
}

}  // namespace

void tcpBridgeBegin() {
    listMutex = xSemaphoreCreateMutex();
    server = new WiFiServer(gConfig.tcpPort, TCP_MAX_CLIENTS);
    server->begin();
    server->setNoDelay(true);
    xTaskCreatePinnedToCore(serverTask, "tcpbridge", 8192, nullptr, 3, nullptr, 1);
}

uint8_t tcpBridgeClientCount() { return clientCount; }

String tcpBridgeClientsJson() {
    xSemaphoreTake(listMutex, portMAX_DELAY);
    String s = clientsJson;
    xSemaphoreGive(listMutex);
    return s;
}
