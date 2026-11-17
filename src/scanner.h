#pragma once

#include <Arduino.h>

#include <functional>

// Register scanner: reads a range of registers in blocks through the shared RTU bus
// and records per-register value or error. Runs in its own task; the web UI polls it.

static constexpr uint16_t SCAN_MAX_REGS = 4096;

struct ScanParams {
    uint8_t unit = 1;
    uint8_t fc = 3;        // 3 = holding, 4 = input
    uint16_t start = 0;
    uint16_t count = 100;
    uint8_t chunk = 32;    // registers per request (1..125)
};

enum ScanStatus : uint8_t {
    SCAN_PENDING = 0,
    SCAN_OK = 1,
    SCAN_TIMEOUT = 2,
    SCAN_CRC = 3,
    SCAN_EXCEPTION = 4,  // modbus exception, code stored in value
};

bool scannerStart(const ScanParams &p);  // false if already running or invalid params
void scannerStop();
bool scannerRunning();
// JSON with params, progress and results from index `from` (for incremental polling)
String scannerJson(uint16_t from);
// CSV of the full result, emitted in pieces (to stream it without a big buffer)
void scannerCsv(const std::function<void(const String &)> &emit);
