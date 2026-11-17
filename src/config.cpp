#include "config.h"

#include <Preferences.h>

GwConfig gConfig;

static const char *NS = "gwcfg";

void configLoad() {
    Preferences p;
    if (!p.begin(NS, true)) return;  // nothing stored yet -> defaults
    GwConfig d;
    gConfig.hostname = p.getString("host", d.hostname);
    gConfig.adminPassword = p.getString("pass", d.adminPassword);
    gConfig.baud = p.getUInt("baud", d.baud);
    gConfig.dataBits = p.getUChar("bits", d.dataBits);
    gConfig.parity = (char)p.getUChar("par", d.parity);
    gConfig.stopBits = p.getUChar("stop", d.stopBits);
    gConfig.rxPin = p.getUChar("rx", d.rxPin);
    gConfig.txPin = p.getUChar("tx", d.txPin);
    gConfig.mode = (GwMode)p.getUChar("mode", (uint8_t)d.mode);
    gConfig.tcpPort = p.getUShort("port", d.tcpPort);
    gConfig.respTimeoutMs = p.getUShort("tmo", d.respTimeoutMs);
    gConfig.frameGapMs = p.getUShort("gap", d.frameGapMs);
    p.end();
}

void configSave() {
    Preferences p;
    p.begin(NS, false);
    p.putString("host", gConfig.hostname);
    p.putString("pass", gConfig.adminPassword);
    p.putUInt("baud", gConfig.baud);
    p.putUChar("bits", gConfig.dataBits);
    p.putUChar("par", (uint8_t)gConfig.parity);
    p.putUChar("stop", gConfig.stopBits);
    p.putUChar("rx", gConfig.rxPin);
    p.putUChar("tx", gConfig.txPin);
    p.putUChar("mode", (uint8_t)gConfig.mode);
    p.putUShort("port", gConfig.tcpPort);
    p.putUShort("tmo", gConfig.respTimeoutMs);
    p.putUShort("gap", gConfig.frameGapMs);
    p.end();
}

uint32_t configSerialMode() {
    // Arduino-ESP32 encodes SERIAL_xyz as: 0x8000000 | parity | (data bits) | (stop bits)
    uint32_t m = 0x8000000;
    switch (gConfig.dataBits) {
        case 5: m |= 0x00; break;
        case 6: m |= 0x04; break;
        case 7: m |= 0x08; break;
        default: m |= 0x0c; break;
    }
    if (gConfig.parity == 'E') m |= 0x02;
    else if (gConfig.parity == 'O') m |= 0x03;
    m |= (gConfig.stopBits == 2) ? 0x30 : 0x10;
    return m;
}
