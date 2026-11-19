#pragma once

#include <Arduino.h>

// Persistent diagnostics: boot counter, reset reason and the cause of the last
// software restart (stored in NVS), plus WiFi connection statistics (RAM).

void diagBegin();  // call first in setup()

// Records why the firmware restarts itself, then restarts.
[[noreturn]] void diagRestart(const char *cause);

uint32_t diagBootCount();
const char *diagResetReason();       // hardware reason of the current boot (brownout, panic, ...)
const String &diagLastRestartCause();  // software cause saved before the previous restart ("" if none)

// WiFi statistics, updated from WiFi events
struct WifiStats {
    volatile uint32_t disconnects = 0;
    volatile uint8_t lastReason = 0;
    volatile uint32_t lastDisconnectMs = 0;  // millis() of the last disconnect, 0 = never
    volatile uint32_t connectedSinceMs = 0;  // millis() of the current connection, 0 = not connected
};
extern WifiStats gWifiStats;
const char *diagWifiReasonName(uint8_t reason);
