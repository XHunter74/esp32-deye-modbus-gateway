#include "web_ui.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "diag.h"
#include "netcheck.h"
#include "rtu.h"
#include "scanner.h"
#include "tcp_bridge.h"
#include "web_page.h"
#include "wifi_setup.h"

namespace {

WebServer server(80);
uint32_t restartAt = 0;  // deferred restart so the HTTP reply is delivered first
const char *restartCause = "web UI";
bool wifiResetPending = false;

bool authorized() {
    if (gConfig.adminPassword.isEmpty()) return true;
    if (server.authenticate("admin", gConfig.adminPassword.c_str())) return true;
    server.requestAuthentication(BASIC_AUTH, "Deye RS232 Gateway");
    return false;
}

String jsonEscape(const String &in) {
    String out;
    for (char c : in) {
        if (c == '"' || c == '\\') out += '\\';
        if ((uint8_t)c >= 0x20) out += c;
    }
    return out;
}

void handleIndex() {
    if (!authorized()) return;
    server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
    if (!authorized()) return;
    char serial[32];
    snprintf(serial, sizeof(serial), "%lu %u%c%u, RX%u TX%u", (unsigned long)gConfig.baud, gConfig.dataBits,
             gConfig.parity, gConfig.stopBits, gConfig.rxPin, gConfig.txPin);
    long lastAgo = gRtuStats.lastActivityMs ? (long)((millis() - gRtuStats.lastActivityMs) / 1000) : -1;
    String j = "{";
    j += "\"version\":\"" FW_VERSION "\"";
    j += ",\"built\":\"" __DATE__ " " __TIME__ "\"";
    j += ",\"sdk\":\"" + String(ESP.getSdkVersion()) + "\"";
    j += ",\"chip\":\"" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\"";
    j += ",\"host\":\"" + jsonEscape(gConfig.hostname) + "\"";
    j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    j += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
    j += ",\"rssi\":" + String(WiFi.RSSI());
    j += ",\"uptime\":" + String(millis() / 1000);
    j += ",\"heap\":" + String(ESP.getFreeHeap());
    j += ",\"mode\":" + String((int)gConfig.mode);
    j += ",\"port\":" + String(gConfig.tcpPort);
    j += ",\"serial\":\"" + String(serial) + "\"";
    j += ",\"clients\":" + tcpBridgeClientsJson();
    j += ",\"requests\":" + String(gRtuStats.requests);
    j += ",\"responses\":" + String(gRtuStats.responses);
    j += ",\"exceptions\":" + String(gRtuStats.exceptions);
    j += ",\"timeouts\":" + String(gRtuStats.timeouts);
    j += ",\"crc\":" + String(gRtuStats.crcErrors);
    j += ",\"lastAgo\":" + String(lastAgo);
    j += ",\"lastReq\":\"" + String(gRtuStats.lastRequest) + "\"";
    j += ",\"lastResp\":\"" + String(gRtuStats.lastResponse) + "\"";
    j += ",\"minHeap\":" + String(ESP.getMinFreeHeap());
    j += ",\"boots\":" + String(diagBootCount());
    j += ",\"resetReason\":\"" + String(diagResetReason()) + "\"";
    j += ",\"restartCause\":\"" + jsonEscape(diagLastRestartCause()) + "\"";
    j += ",\"wifiDisc\":" + String(gWifiStats.disconnects);
    j += ",\"wifiReason\":\"" + (gWifiStats.lastReason ? String(gWifiStats.lastReason) + " " + diagWifiReasonName(gWifiStats.lastReason) : String("")) + "\"";
    j += ",\"wifiDiscAgo\":" + String(gWifiStats.lastDisconnectMs ? (long)((millis() - gWifiStats.lastDisconnectMs) / 1000) : -1L);
    j += ",\"wifiUp\":" + String(gWifiStats.connectedSinceMs ? (long)((millis() - gWifiStats.connectedSinceMs) / 1000) : -1L);
    j += ",\"gw\":\"" + WiFi.gatewayIP().toString() + "\"";
    j += ",\"gwOkAgo\":" + String(gNetcheck.lastOkMs ? (long)((millis() - gNetcheck.lastOkMs) / 1000) : -1L);
    j += ",\"gwRtt\":" + String(gNetcheck.lastRttMs);
    j += ",\"gwFails\":" + String(gNetcheck.failedChecks);
    j += ",\"gwReconn\":" + String(gNetcheck.reconnects);
    j += "}";
    server.send(200, "application/json", j);
}

void handleConfigGet() {
    if (!authorized()) return;
    String j = "{";
    j += "\"mode\":" + String((int)gConfig.mode);
    j += ",\"port\":" + String(gConfig.tcpPort);
    j += ",\"tmo\":" + String(gConfig.respTimeoutMs);
    j += ",\"baud\":" + String(gConfig.baud);
    j += ",\"bits\":" + String(gConfig.dataBits);
    j += ",\"par\":\"" + String(gConfig.parity) + "\"";
    j += ",\"stop\":" + String(gConfig.stopBits);
    j += ",\"rx\":" + String(gConfig.rxPin);
    j += ",\"tx\":" + String(gConfig.txPin);
    j += ",\"gap\":" + String(gConfig.frameGapMs);
    j += ",\"host\":\"" + jsonEscape(gConfig.hostname) + "\"";
    j += "}";
    server.send(200, "application/json", j);
}

long argInt(const char *name, long def, long lo, long hi) {
    if (!server.hasArg(name)) return def;
    long v = server.arg(name).toInt();
    return (v < lo || v > hi) ? def : v;
}

void handleConfigPost() {
    if (!authorized()) return;
    GwConfig c = gConfig;
    c.mode = (GwMode)argInt("mode", (long)c.mode, 0, 1);
    c.tcpPort = argInt("port", c.tcpPort, 1, 65535);
    c.respTimeoutMs = argInt("tmo", c.respTimeoutMs, 50, 10000);
    c.baud = argInt("baud", c.baud, 300, 921600);
    c.dataBits = argInt("bits", c.dataBits, 7, 8);
    String par = server.arg("par");
    if (par == "N" || par == "E" || par == "O") c.parity = par[0];
    c.stopBits = argInt("stop", c.stopBits, 1, 2);
    c.rxPin = argInt("rx", c.rxPin, 0, 39);
    c.txPin = argInt("tx", c.txPin, 0, 33);
    c.frameGapMs = argInt("gap", c.frameGapMs, 2, 500);
    String host = server.arg("host");
    host.trim();
    if (host.length() > 0 && host.length() < 32) c.hostname = host;
    if (server.arg("clearpass") == "1") c.adminPassword = "";
    else if (server.arg("pass").length() > 0) c.adminPassword = server.arg("pass");
    if (c.rxPin == c.txPin) {
        server.send(400, "text/plain", "RX and TX pins must differ");
        return;
    }
    gConfig = c;
    configSave();
    server.send(200, "text/plain", "Saved. Rebooting...");
    restartCause = "settings saved";
    restartAt = millis() + 1000;
}

void handleScanStart() {
    if (!authorized()) return;
    ScanParams p;
    p.unit = argInt("unit", 1, 0, 247);
    p.fc = argInt("fc", 3, 3, 4);
    p.start = argInt("start", 0, 0, 65535);
    p.count = argInt("count", 100, 1, SCAN_MAX_REGS);
    p.chunk = argInt("chunk", 32, 1, 125);
    if (!scannerStart(p)) {
        server.send(409, "text/plain", "Scanner is busy or parameters are invalid (start + count must be <= 65536)");
        return;
    }
    server.send(200, "text/plain", "started");
}

void handleScanStop() {
    if (!authorized()) return;
    scannerStop();
    server.send(200, "text/plain", "stopping");
}

void handleScanGet() {
    if (!authorized()) return;
    server.send(200, "application/json", scannerJson(argInt("from", 0, 0, SCAN_MAX_REGS)));
}

void handleScanCsv() {
    if (!authorized()) return;
    server.sendHeader("Content-Disposition", "attachment; filename=\"deye-scan.csv\"");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "");
    scannerCsv([](const String &chunk) {
        server.sendContent(chunk);
        feedLoopWDT();  // large CSV over a slow link must not trip the loop watchdog
    });
    server.sendContent("");
}

void handleReboot() {
    if (!authorized()) return;
    server.send(200, "text/plain", "Rebooting...");
    restartCause = "reboot from web UI";
    restartAt = millis() + 500;
}

void handleWifiReset() {
    if (!authorized()) return;
    server.send(200, "text/plain", "WiFi settings erased, rebooting into setup AP...");
    wifiResetPending = true;
    restartAt = millis() + 500;
}

// OTA: POST /update (multipart). Result is sent when the upload completes.
void handleUpdateDone() {
    if (!authorized()) return;
    bool ok = !Update.hasError();
    server.send(ok ? 200 : 500, "text/plain", ok ? "Update OK, rebooting..." : "Update failed: " + String(Update.errorString()));
    if (ok) {
        restartCause = "firmware update";
        restartAt = millis() + 1000;
    }
}

void handleUpdateUpload() {
    if (!gConfig.adminPassword.isEmpty() && !server.authenticate("admin", gConfig.adminPassword.c_str())) return;
    feedLoopWDT();  // the whole upload runs inside one handleClient() call and takes longer than the loop watchdog
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[ota] %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[ota] done, %u bytes\n", up.totalSize);
        else Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
    }
}

}  // namespace

void webBegin() {
    server.on("/", HTTP_GET, handleIndex);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/config", HTTP_GET, handleConfigGet);
    server.on("/api/config", HTTP_POST, handleConfigPost);
    server.on("/api/scan/start", HTTP_POST, handleScanStart);
    server.on("/api/scan/stop", HTTP_POST, handleScanStop);
    server.on("/api/scan", HTTP_GET, handleScanGet);
    server.on("/api/scan.csv", HTTP_GET, handleScanCsv);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    server.on("/api/wifireset", HTTP_POST, handleWifiReset);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();
}

void webLoop() {
    server.handleClient();
    if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
        if (wifiResetPending) wifiResetAndRestart();
        diagRestart(restartCause);
    }
}
