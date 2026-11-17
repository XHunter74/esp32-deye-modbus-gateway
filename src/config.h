#pragma once

#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// TCP framing used by the gateway
enum class GwMode : uint8_t {
    RtuOverTcp = 0,  // raw Modbus RTU frames (with CRC) over TCP, like USR-TCP232
    ModbusTcp = 1,   // Modbus TCP (MBAP header) <-> Modbus RTU conversion
};

struct GwConfig {
    String hostname = "deye-gw";
    String adminPassword = "";  // empty = no authentication on the web UI

    uint32_t baud = 9600;
    uint8_t dataBits = 8;
    char parity = 'N';  // N, E, O
    uint8_t stopBits = 1;
    uint8_t rxPin = 16;
    uint8_t txPin = 17;

    GwMode mode = GwMode::RtuOverTcp;
    uint16_t tcpPort = 8899;
    uint16_t respTimeoutMs = 1000;  // max wait for inverter response
    uint16_t frameGapMs = 20;       // silence that ends a frame of unknown length
};

extern GwConfig gConfig;

void configLoad();
void configSave();
uint32_t configSerialMode();  // SERIAL_8N1 etc. from dataBits/parity/stopBits
