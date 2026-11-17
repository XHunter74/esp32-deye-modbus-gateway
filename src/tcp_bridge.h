#pragma once

#include <Arduino.h>

// TCP server that forwards Modbus requests to the inverter's RS232 port.
// Mode RtuOverTcp: raw RTU frames (CRC included) both ways.
// Mode ModbusTcp:  MBAP-framed requests converted to RTU and back.

static constexpr int TCP_MAX_CLIENTS = 4;

void tcpBridgeBegin();  // starts the server task
uint8_t tcpBridgeClientCount();
String tcpBridgeClientsJson();  // ["ip:port", ...]
