#include "diag.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

WifiStats gWifiStats;

namespace {

const char *NS = "gwdiag";
uint32_t bootCount = 0;
esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
String lastCause;

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
    p.end();
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

uint32_t diagBootCount() { return bootCount; }
const String &diagLastRestartCause() { return lastCause; }

const char *diagResetReason() {
    switch (resetReason) {
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
