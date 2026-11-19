#include "netcheck.h"

#include <WiFi.h>

#include "diag.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

NetcheckStats gNetcheck;

namespace {

esp_ping_handle_t session = nullptr;
volatile bool running = false;
volatile bool finished = false;
volatile uint32_t received = 0;
uint32_t pingStartedMs = 0;
uint32_t nextCheckAt = 0;

void onSuccess(esp_ping_handle_t hdl, void *) {
    uint32_t rtt = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &rtt, sizeof(rtt));
    gNetcheck.lastRttMs = rtt;
}

void onEnd(esp_ping_handle_t hdl, void *) {
    uint32_t rx = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
    received = rx;
    finished = true;
}

bool startPing(IPAddress gw) {
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 3;
    cfg.interval_ms = 300;
    cfg.timeout_ms = 1000;
    cfg.data_size = 16;
    IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = onSuccess;
    cbs.on_ping_end = onEnd;
    if (esp_ping_new_session(&cfg, &cbs, &session) != ESP_OK) {
        session = nullptr;
        return false;
    }
    received = 0;
    finished = false;
    running = true;
    pingStartedMs = millis();
    if (esp_ping_start(session) != ESP_OK) {
        esp_ping_delete_session(session);
        session = nullptr;
        running = false;
        return false;
    }
    return true;
}

void endSession() {
    if (session) {
        esp_ping_stop(session);
        esp_ping_delete_session(session);
        session = nullptr;
    }
    running = false;
}

void countFailure(const char *what) {
    gNetcheck.failedChecks++;
    uint32_t fails = ++gNetcheck.failsInRow;
    Serial.printf("[net] gateway %s: %s (%u in a row)\n", WiFi.gatewayIP().toString().c_str(), what, fails);
    if (!gNetcheck.lastOkMs) return;  // never reachable since boot: router ignores ping, do nothing
    if (fails >= NETCHECK_RESTART_FAILS) diagRestart("gateway unreachable");
    if (fails == NETCHECK_RECONNECT_FAILS) {
        Serial.println("[net] link looks dead, forcing WiFi reconnect");
        gNetcheck.reconnects++;
        WiFi.disconnect(false, false);
        WiFi.begin();
    }
}

void handleResult() {
    uint32_t rx = received;
    endSession();
    if (rx > 0) {
        gNetcheck.lastOkMs = millis() ? millis() : 1;
        gNetcheck.failsInRow = 0;
        return;
    }
    countFailure("no answer");
}

}  // namespace

bool netcheckPingRunning() { return running; }

void netcheckLoop() {
    uint32_t now = millis();

    // Proof of life: independent of WiFi status and of the ping state machine below.
    if (gNetcheck.lastOkMs && now - gNetcheck.lastOkMs > NETCHECK_DEAD_MS) diagRestart("no gateway reply for 5 min");

    if (running) {
        if (finished) {
            handleResult();
        } else if (now - pingStartedMs > NETCHECK_PING_STUCK_MS) {
            gNetcheck.stuckPings++;
            endSession();
            countFailure("ping session stuck");
        }
        return;
    }
    if ((int32_t)(now - nextCheckAt) < 0) return;
    nextCheckAt = now + NETCHECK_INTERVAL_MS;

    if (WiFi.status() != WL_CONNECTED) {
        // wifiLoop() handles a reported disconnect. A dead link that we already tried
        // to reconnect still counts towards the restart limit.
        if (gNetcheck.failsInRow >= NETCHECK_RECONNECT_FAILS) countFailure("WiFi not connected");
        return;
    }
    IPAddress gw = WiFi.gatewayIP();
    if (gw == IPAddress(0, 0, 0, 0)) {
        countFailure("no gateway address");
        return;
    }
    if (!startPing(gw)) countFailure("cannot start ping");
}
