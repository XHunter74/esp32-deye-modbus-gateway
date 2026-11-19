#include "diag.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <rom/rtc.h>

WifiStats gWifiStats;

namespace {

const char *NS = "gwdiag";
uint32_t bootCount = 0;
esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
String lastCause;

// Survives software, panic and watchdog resets, but not a power loss.
struct RtcMarker {
    uint32_t magic;
    uint32_t uptimeS;
    uint32_t check;  // magic ^ uptimeS ^ 0xA5A5A5A5
};
RTC_NOINIT_ATTR RtcMarker rtcMarker;
constexpr uint32_t RTC_MAGIC = 0x44475731;  // "DGW1"
uint32_t lastMarkerS = UINT32_MAX;

struct HistEntry {
    uint32_t boot;
    int32_t ranS;      // uptime of the previous run, -1 unknown
    uint8_t reason;    // esp_reset_reason_t
    uint8_t raw;       // ROM reset code of CPU0 (RESET_REASON)
    uint8_t pad[2];
    char cause[28];
};
HistEntry history[DIAG_HISTORY_LEN];

void writeMarker(uint32_t s) {
    rtcMarker.magic = RTC_MAGIC;
    rtcMarker.uptimeS = s;
    rtcMarker.check = RTC_MAGIC ^ s ^ 0xA5A5A5A5;
}

String esc(const char *in) {
    String out;
    for (const char *p = in; *p; ++p) {
        if (*p == '"' || *p == '\\') out += '\\';
        if ((uint8_t)*p >= 0x20) out += *p;
    }
    return out;
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            gWifiStats.connectedSinceMs = millis() ? millis() : 1;
            Serial.printf("[wifi] got IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            // Only count the loss of a working connection, not every failed reconnect attempt.
            if (gWifiStats.connectedSinceMs) {
                gWifiStats.disconnects++;
                gWifiStats.lastDisconnectMs = millis() ? millis() : 1;
            }
            gWifiStats.lastReason = info.wifi_sta_disconnected.reason;
            gWifiStats.connectedSinceMs = 0;
            Serial.printf("[wifi] disconnected, reason %u (%s)\n", info.wifi_sta_disconnected.reason,
                          diagWifiReasonName(info.wifi_sta_disconnected.reason));
            break;
        default:
            break;
    }
}

}  // namespace

void diagBegin() {
    resetReason = esp_reset_reason();
    Preferences p;
    p.begin(NS, false);
    bootCount = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", bootCount);
    // The saved cause belongs to this boot only if the reset was a software restart.
    lastCause = (resetReason == ESP_RST_SW) ? p.getString("cause", "") : "";
    p.remove("cause");

    bool markerOk = rtcMarker.magic == RTC_MAGIC && rtcMarker.check == (RTC_MAGIC ^ rtcMarker.uptimeS ^ 0xA5A5A5A5);
    HistEntry e = {};
    e.boot = bootCount;
    e.ranS = markerOk ? (int32_t)rtcMarker.uptimeS : -1;
    e.reason = (uint8_t)resetReason;
    e.raw = (uint8_t)rtc_get_reset_reason(0);
    strlcpy(e.cause, lastCause.c_str(), sizeof(e.cause));
    if (p.getBytesLength("hist") == sizeof(history)) p.getBytes("hist", history, sizeof(history));
    memmove(&history[1], &history[0], sizeof(HistEntry) * (DIAG_HISTORY_LEN - 1));
    history[0] = e;
    p.putBytes("hist", history, sizeof(history));
    p.end();
    writeMarker(0);
    Serial.printf("[diag] boot #%u, reset reason: %s%s%s\n", bootCount, diagResetReason(),
                  lastCause.isEmpty() ? "" : ", cause: ", lastCause.c_str());
    WiFi.onEvent(onWifiEvent);
}

void diagRestart(const char *cause) {
    Serial.printf("[diag] restart: %s\n", cause);
    Preferences p;
    p.begin(NS, false);
    p.putString("cause", cause);
    p.end();
    delay(200);
    ESP.restart();
    for (;;) {
    }
}

void diagLoop() {
    uint32_t s = millis() / 1000;
    if (s != lastMarkerS) {
        lastMarkerS = s;
        writeMarker(s);
    }
}

String diagHistoryJson() {
    String j = "[";
    for (int i = 0; i < DIAG_HISTORY_LEN; ++i) {
        const HistEntry &e = history[i];
        if (!e.boot) break;
        if (i) j += ',';
        j += "{\"boot\":" + String(e.boot) + ",\"reason\":\"" + diagResetReasonName((esp_reset_reason_t)e.reason) +
             "\",\"raw\":" + String(e.raw) + ",\"cause\":\"" + esc(e.cause) + "\",\"ranS\":" + String(e.ranS) + "}";
    }
    return j + "]";
}

uint32_t diagBootCount() { return bootCount; }
const String &diagLastRestartCause() { return lastCause; }

const char *diagResetReason() { return diagResetReasonName(resetReason); }

const char *diagResetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external pin";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "crash (panic)";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT: return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT: return "brownout (supply voltage dropped)";
        case ESP_RST_SDIO: return "SDIO";
        default: return "unknown";
    }
}

const char *diagWifiReasonName(uint8_t r) {
    switch (r) {
        case 0: return "none";
        case 1: return "unspecified";
        case 2: return "auth expired";
        case 3: return "auth leave";
        case 4: return "inactivity";
        case 5: return "AP too many stations";
        case 6: case 7: return "not authenticated/associated";
        case 8: return "assoc leave";
        case 15: return "4-way handshake timeout (wrong password?)";
        case 16: return "group key update timeout";
        case 23: return "802.1X auth failed";
        case 200: return "beacon timeout (signal lost)";
        case 201: return "no AP found";
        case 202: return "auth failed (wrong password?)";
        case 203: return "association failed";
        case 204: return "handshake timeout";
        case 205: return "connection failed";
        default: return "other";
    }
}
