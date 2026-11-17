#pragma once

#include <Arduino.h>

// Connects to the stored WiFi network. If none is stored or the connection fails,
// opens the WiFiManager captive portal (AP "Deye-GW-XXXXXX") for 5 minutes, then reboots.
void wifiSetup();

// Erases stored WiFi credentials and reboots into the captive portal.
void wifiResetAndRestart();

String wifiApName();
