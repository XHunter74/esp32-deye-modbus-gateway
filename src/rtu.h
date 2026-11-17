#pragma once

#include <Arduino.h>

// Modbus RTU master side: owns the UART connected to the inverter.
// All users (TCP bridge, scanner) go through rtuTransact(), which holds a mutex,
// so only one request is ever on the serial line at a time.

static constexpr size_t RTU_MAX_FRAME = 256;

enum class RtuResult : uint8_t {
    Ok = 0,
    Timeout,    // no byte received at all
    BadCrc,     // response received but CRC mismatch
    Busy,       // could not get the bus in time
};

struct RtuStats {
    volatile uint32_t requests = 0;
    volatile uint32_t responses = 0;
    volatile uint32_t timeouts = 0;
    volatile uint32_t crcErrors = 0;
    volatile uint32_t exceptions = 0;
    char lastRequest[2 * 32 + 1] = "";   // hex, truncated to 32 bytes
    char lastResponse[2 * 32 + 1] = "";
    volatile uint32_t lastActivityMs = 0;
};

extern RtuStats gRtuStats;

void rtuBegin();
uint16_t modbusCrc(const uint8_t *buf, size_t len);
bool rtuCrcOk(const uint8_t *frame, size_t len);

// Expected total length of an RTU *request* frame from its first bytes, or 0 if unknown yet / unknown FC.
size_t rtuExpectedRequestLen(const uint8_t *buf, size_t have);
// Expected total length of an RTU *response* frame, or 0 if unknown yet / unknown FC.
size_t rtuExpectedResponseLen(const uint8_t *buf, size_t have);

// Sends a complete RTU frame (CRC included) and waits for the response.
// resp must hold RTU_MAX_FRAME bytes. On return respLen holds the received byte count
// (also on BadCrc, so callers can pass the frame through).
RtuResult rtuTransact(const uint8_t *req, size_t reqLen, uint8_t *resp, size_t &respLen,
                      uint32_t waitBusMs = 5000);

// Convenience: FC03/FC04 read. Returns Ok and fills regs, or error; exception code in *exc (0 if none).
RtuResult rtuReadRegisters(uint8_t unit, uint8_t fc, uint16_t addr, uint16_t count, uint16_t *regs,
                           uint8_t *exc);
