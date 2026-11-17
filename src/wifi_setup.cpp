#include "wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"

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
    wm.setTitle("Deye RS232 Gateway");
    wm.setConnectTimeout(30);
    wm.setConfigPortalTimeout(300);
    std::vector<const char *> menu = {"wifi", "info", "restart"};
    wm.setMenu(menu);
    wm.setShowInfoUpdate(false);

    String ap = wifiApName();
    Serial.printf("[wifi] connecting (portal AP: %s)\n", ap.c_str());
    if (!wm.autoConnect(ap.c_str())) {
        Serial.println("[wifi] no connection, rebooting");
        delay(1000);
        ESP.restart();
    }
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
}

void wifiResetAndRestart() {
    WiFiManager wm;
    wm.resetSettings();
    delay(500);
    ESP.restart();
}
