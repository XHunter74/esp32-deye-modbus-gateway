#pragma once

#include <Arduino.h>

// Link check: WiFi can report "connected" while the router no longer delivers
// packets (the ESP32 stops answering ARP and drops off the LAN without noticing).
// netcheckLoop() pings the default gateway every NETCHECK_INTERVAL_MS:
// - NETCHECK_RECONNECT_FAILS failed checks in a row -> WiFi reconnect;
// - NETCHECK_RESTART_FAILS failed checks in a row  -> restart ("gateway unreachable");
// - proof of life: no answered ping for NETCHECK_DEAD_MS -> restart ("no gateway reply"),
//   whatever the WiFi driver reports and even if a ping session hangs.
// A ping session that does not finish within NETCHECK_PING_STUCK_MS counts as failed.
// Nothing is done until the gateway has answered at least once since boot,
// so a router that ignores ping never causes reconnects or restarts.
void netcheckLoop();

struct NetcheckStats {
    volatile uint32_t lastOkMs = 0;     // millis() of the last answered ping, 0 = never
    volatile uint32_t failsInRow = 0;   // failed checks since the last answered one
    volatile uint32_t failedChecks = 0; // failed checks since boot
    volatile uint32_t reconnects = 0;   // reconnects forced by the link check
    volatile uint32_t stuckPings = 0;   // ping sessions that never finished
    volatile uint32_t lastRttMs = 0;
};
extern NetcheckStats gNetcheck;
bool netcheckPingRunning();

static constexpr uint32_t NETCHECK_INTERVAL_MS = 30000;
static constexpr uint32_t NETCHECK_RECONNECT_FAILS = 3;  // ~1.5 min
static constexpr uint32_t NETCHECK_RESTART_FAILS = 8;    // ~4 min
static constexpr uint32_t NETCHECK_DEAD_MS = 5UL * 60 * 1000;
static constexpr uint32_t NETCHECK_PING_STUCK_MS = 10000;
