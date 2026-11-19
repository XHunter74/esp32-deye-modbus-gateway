#include "wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "diag.h"

namespace {

constexpr uint32_t BOOT_CONNECT_WAIT_MS = 20000;
constexpr uint32_t RETRY_MIN_MS = 10000;
constexpr uint32_t RETRY_MAX_MS = 120000;

uint32_t disconnectedSince = 0;  // 0 = connected
uint32_t nextRetryAt = 0;
uint32_t retryInterval = RETRY_MIN_MS;

void runPortal() {
    WiFiManager wm;
    wm.setTitle("Deye RS232 Gateway");
    wm.setConnectTimeout(30);
    wm.setConfigPortalTimeout(300);
    std::vector<const char *> menu = {"wifi", "info", "restart"};
    wm.setMenu(menu);
    wm.setShowInfoUpdate(false);

    String ap = wifiApName();
    Serial.printf("[wifi] no credentials stored, opening setup AP %s\n", ap.c_str());
    if (!wm.startConfigPortal(ap.c_str())) diagRestart("setup portal timeout");
}

}  // namespace

String wifiApName() {
    char buf[24];
    uint64_t mac = ESP.getEfuseMac();
    snprintf(buf, sizeof(buf), "Deye-GW-%02X%02X%02X", (uint8_t)(mac >> 24), (uint8_t)(mac >> 32),
             (uint8_t)(mac >> 40));
    return String(buf);
}

void wifiSetup() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(gConfig.hostname.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);  // lower latency for Modbus polling

    WiFiManager wm;
    if (!wm.getWiFiIsSaved()) {
        runPortal();
    } else {
        Serial.printf("[wifi] connecting to %s\n", wm.getWiFiSSID(true).c_str());
        WiFi.begin();  // stored credentials
    }

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < BOOT_CONNECT_WAIT_MS) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[wifi] not connected yet, continuing; will keep retrying");
        disconnectedSince = millis() ? millis() : 1;
        nextRetryAt = millis() + RETRY_MIN_MS;
    }
}

void wifiLoop() {
    if (WiFi.status() == WL_CONNECTED) {
        disconnectedSince = 0;
        retryInterval = RETRY_MIN_MS;
        return;
    }
    uint32_t now = millis();
    if (!disconnectedSince) {
        disconnectedSince = now ? now : 1;
        nextRetryAt = now + RETRY_MIN_MS;  // give auto-reconnect a chance first
    }
    if (now - disconnectedSince > WIFI_WATCHDOG_MS) diagRestart("WiFi down for 5 min");
    if ((int32_t)(now - nextRetryAt) >= 0) {
        Serial.printf("[wifi] reconnect attempt (down %lus)\n", (unsigned long)((now - disconnectedSince) / 1000));
        WiFi.disconnect(false, false);
        WiFi.begin();
        nextRetryAt = now + retryInterval;
        retryInterval = min(retryInterval * 2, RETRY_MAX_MS);
    }
}

void wifiResetAndRestart() {
    WiFiManager wm;
    wm.resetSettings();
    delay(500);
    diagRestart("WiFi settings erased");
}
