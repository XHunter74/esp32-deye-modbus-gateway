#pragma once

#include <Arduino.h>

// WiFi lifecycle:
// - no credentials stored: opens the WiFiManager captive portal (AP "Deye-GW-XXXXXX");
// - credentials stored: connects in the background, never falls back to the portal
//   (a short router outage must not take the gateway off the LAN);
// - wifiLoop() retries with backoff and restarts the ESP32 if WiFi stays down too long.
void wifiSetup();
void wifiLoop();

// Erases stored WiFi credentials and reboots into the captive portal.
[[noreturn]] void wifiResetAndRestart();

String wifiApName();

static constexpr uint32_t WIFI_WATCHDOG_MS = 5UL * 60 * 1000;  // restart after 5 min without WiFi
