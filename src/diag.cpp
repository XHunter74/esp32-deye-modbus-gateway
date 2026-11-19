#include "diag.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <rom/rtc.h>

#include "netcheck.h"

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

// Heartbeat: state snapshot saved to NVS every few minutes, survives a power loss.
struct Heartbeat {
    uint32_t boot;
    uint32_t uptimeS;
    int32_t gwOkAgoS;  // seconds since the gateway last answered ping, -1 never
    uint16_t gwFails;  // failed checks in a row
    uint8_t wifi;      // wl_status_t
    uint8_t ping;      // ping session running
    uint8_t bssid[6];  // mesh node / AP we were connected to
    uint8_t chan;
    uint8_t pad;
};
constexpr uint32_t HB_FIRST_S = 60;
constexpr uint32_t HB_EVERY_S = 300;
uint32_t nextHbS = HB_FIRST_S;

struct HistEntryV1 {
    uint32_t boot;
    int32_t ranS;
    uint8_t reason;
    uint8_t raw;
    uint8_t pad[2];
    char cause[28];
};

struct HistEntry {
    uint32_t boot;
    int32_t ranS;      // uptime of the previous run from RTC memory, -1 unknown (power lost)
    uint8_t reason;    // esp_reset_reason_t
    uint8_t raw;       // ROM reset code of CPU0 (RESET_REASON)
    uint8_t hbWifi;    // last heartbeat of the previous run
    uint8_t hbPing;
    char cause[28];
    int32_t hbUptimeS;  // -1 = no heartbeat
    int32_t hbGwOkAgoS;
    uint16_t hbFails;
    uint8_t pad[2];
    uint8_t hbBssid[6];
    uint8_t hbChan;
    uint8_t pad2;
};
HistEntry history[DIAG_HISTORY_LEN];
constexpr size_t HIST_V2_SIZE = offsetof(HistEntry, hbBssid);  // layout before the BSSID fields

void loadHistory(Preferences &p) {
    if (p.getBytesLength("hist3") == sizeof(history)) {
        p.getBytes("hist3", history, sizeof(history));
        return;
    }
    if (p.getBytesLength("hist2") == HIST_V2_SIZE * DIAG_HISTORY_LEN) {
        uint8_t raw[HIST_V2_SIZE * DIAG_HISTORY_LEN];
        p.getBytes("hist2", raw, sizeof(raw));
        for (int i = 0; i < DIAG_HISTORY_LEN; ++i) memcpy(&history[i], raw + i * HIST_V2_SIZE, HIST_V2_SIZE);
        p.remove("hist2");
        return;
    }
    HistEntryV1 old[DIAG_HISTORY_LEN];
    if (p.getBytesLength("hist") != sizeof(old)) return;
    p.getBytes("hist", old, sizeof(old));
    for (int i = 0; i < DIAG_HISTORY_LEN; ++i) {
        history[i].boot = old[i].boot;
        history[i].ranS = old[i].ranS;
        history[i].reason = old[i].reason;
        history[i].raw = old[i].raw;
        memcpy(history[i].cause, old[i].cause, sizeof(history[i].cause));
        history[i].hbUptimeS = -1;
    }
    p.remove("hist");
}

void saveHeartbeat(uint32_t s) {
    Heartbeat hb = {};
    hb.boot = bootCount;
    hb.uptimeS = s;
    hb.gwOkAgoS = gNetcheck.lastOkMs ? (int32_t)((millis() - gNetcheck.lastOkMs) / 1000) : -1;
    hb.gwFails = (uint16_t)(gNetcheck.failsInRow > 65535 ? 65535 : gNetcheck.failsInRow);
    hb.wifi = (uint8_t)WiFi.status();
    hb.ping = netcheckPingRunning() ? 1 : 0;
    if (WiFi.status() == WL_CONNECTED) {
        const uint8_t *b = WiFi.BSSID();
        if (b) memcpy(hb.bssid, b, 6);
        hb.chan = (uint8_t)WiFi.channel();
    }
    Preferences p;
    p.begin(NS, false);
    p.putBytes("hb", &hb, sizeof(hb));
    p.end();
}

void writeMarker(uint32_t s) {
    rtcMarker.magic = RTC_MAGIC;
    rtcMarker.uptimeS = s;
    rtcMarker.check = RTC_MAGIC ^ s ^ 0xA5A5A5A5;
}

String macStr(const uint8_t *m) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
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
    e.hbUptimeS = -1;
    Heartbeat hb;
    if (p.getBytesLength("hb") == sizeof(hb) && p.getBytes("hb", &hb, sizeof(hb)) == sizeof(hb) &&
        hb.boot + 1 == bootCount) {
        e.hbUptimeS = (int32_t)hb.uptimeS;
        e.hbGwOkAgoS = hb.gwOkAgoS;
        e.hbFails = hb.gwFails;
        e.hbWifi = hb.wifi;
        e.hbPing = hb.ping;
        memcpy(e.hbBssid, hb.bssid, 6);
        e.hbChan = hb.chan;
    }
    loadHistory(p);
    memmove(&history[1], &history[0], sizeof(HistEntry) * (DIAG_HISTORY_LEN - 1));
    history[0] = e;
    p.putBytes("hist3", history, sizeof(history));
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
        if (s >= nextHbS) {
            nextHbS = s + HB_EVERY_S;
            saveHeartbeat(s);
        }
    }
}

String diagHistoryJson() {
    String j = "[";
    for (int i = 0; i < DIAG_HISTORY_LEN; ++i) {
        const HistEntry &e = history[i];
        if (!e.boot) break;
        if (i) j += ',';
        j += "{\"boot\":" + String(e.boot) + ",\"reason\":\"" + diagResetReasonName((esp_reset_reason_t)e.reason) +
             "\",\"raw\":" + String(e.raw) + ",\"cause\":\"" + esc(e.cause) + "\",\"ranS\":" + String(e.ranS);
        if (e.hbUptimeS >= 0)
            j += ",\"hb\":{\"up\":" + String(e.hbUptimeS) + ",\"wifi\":" + String(e.hbWifi) + ",\"gwAgo\":" +
                 String(e.hbGwOkAgoS) + ",\"fails\":" + String(e.hbFails) + ",\"ping\":" + String(e.hbPing) +
                 ",\"ap\":\"" + macStr(e.hbBssid) + "\",\"ch\":" + String(e.hbChan) + "}";
        j += "}";
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
