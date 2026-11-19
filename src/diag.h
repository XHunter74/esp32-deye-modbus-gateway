#pragma once

#include <Arduino.h>
#include <esp_system.h>

// Persistent diagnostics: boot counter, reset reason and the cause of the last
// software restart (stored in NVS), plus WiFi connection statistics (RAM).

void diagBegin();  // call first in setup()
void diagLoop();   // call from loop(): keeps the uptime marker in RTC memory current

// Restart history (newest first, DIAG_HISTORY_LEN entries kept in NVS) as a JSON array:
// [{"boot":N,"reason":"...","raw":R,"cause":"...","ranS":S,"hb":{...}}], ranS = how long the previous
// run lasted, -1 if unknown (RTC memory lost, i.e. the power went off). "hb" is the last heartbeat of
// the previous run (saved to NVS every 5 min, survives a power loss): uptime, WiFi status,
// seconds since the gateway answered ping, failed checks in a row, ping session running.
String diagHistoryJson();
static constexpr int DIAG_HISTORY_LEN = 10;

// Records why the firmware restarts itself, then restarts.
[[noreturn]] void diagRestart(const char *cause);

uint32_t diagBootCount();
const char *diagResetReasonName(esp_reset_reason_t r);
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
