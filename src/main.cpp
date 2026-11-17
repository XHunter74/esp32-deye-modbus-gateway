// Deye RS232 <-> TCP Modbus gateway
// ESP32 + MAX3232 board on the inverter's DB9 logger port, replacing the Solarman data logger.

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "rtu.h"
#include "tcp_bridge.h"
#include "web_ui.h"
#include "wifi_setup.h"

static constexpr int BOOT_BUTTON = 0;              // hold 5 s to erase WiFi settings
static constexpr uint32_t BUTTON_HOLD_MS = 5000;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[boot] Deye RS232 gateway " FW_VERSION);
    pinMode(BOOT_BUTTON, INPUT_PULLUP);

    configLoad();
    rtuBegin();
    wifiSetup();

    if (MDNS.begin(gConfig.hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("modbus", "tcp", gConfig.tcpPort);
    }
    tcpBridgeBegin();
    webBegin();
    Serial.printf("[boot] ready: http://%s.local, %s on TCP port %u\n", gConfig.hostname.c_str(),
                  gConfig.mode == GwMode::ModbusTcp ? "Modbus TCP" : "RTU over TCP", gConfig.tcpPort);
}

void loop() {
    webLoop();

    static uint32_t pressedSince = 0;
    if (digitalRead(BOOT_BUTTON) == LOW) {
        if (!pressedSince) pressedSince = millis();
        if (millis() - pressedSince > BUTTON_HOLD_MS) {
            Serial.println("[wifi] BOOT held, erasing WiFi settings");
            wifiResetAndRestart();
        }
    } else {
        pressedSince = 0;
    }

    static uint32_t lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 30000) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    }
    delay(2);
}
