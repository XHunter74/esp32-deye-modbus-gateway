#include "scanner.h"

#include "rtu.h"

namespace {

ScanParams params;
uint16_t *values = nullptr;
uint8_t *status = nullptr;
volatile uint16_t done = 0;  // registers processed
volatile bool running = false;
volatile bool stopReq = false;

const char *statusName(uint8_t s) {
    switch (s) {
        case SCAN_OK: return "ok";
        case SCAN_TIMEOUT: return "timeout";
        case SCAN_CRC: return "crc";
        case SCAN_EXCEPTION: return "exception";
        default: return "pending";
    }
}

void scanTask(void *) {
    uint16_t regs[125];
    uint16_t pos = 0;
    while (pos < params.count && !stopReq) {
        uint16_t n = min<uint16_t>(params.chunk, params.count - pos);
        uint8_t exc = 0;
        RtuResult r = rtuReadRegisters(params.unit, params.fc, params.start + pos, n, regs, &exc);
        for (uint16_t i = 0; i < n; i++) {
            uint16_t k = pos + i;
            if (r == RtuResult::Ok && exc == 0) {
                values[k] = regs[i];
                status[k] = SCAN_OK;
            } else if (r == RtuResult::Ok) {
                values[k] = exc;
                status[k] = SCAN_EXCEPTION;
            } else {
                values[k] = 0;
                status[k] = (r == RtuResult::BadCrc) ? SCAN_CRC : SCAN_TIMEOUT;
            }
        }
        pos += n;
        done = pos;
        vTaskDelay(pdMS_TO_TICKS(20));  // leave room for bridge traffic
    }
    running = false;
    vTaskDelete(nullptr);
}

}  // namespace

bool scannerStart(const ScanParams &p) {
    if (running) return false;
    if (p.count == 0 || p.count > SCAN_MAX_REGS || p.chunk == 0 || p.chunk > 125) return false;
    if ((uint32_t)p.start + p.count > 65536) return false;
    if (p.fc != 3 && p.fc != 4) return false;
    if (!values) {
        values = (uint16_t *)malloc(SCAN_MAX_REGS * sizeof(uint16_t));
        status = (uint8_t *)malloc(SCAN_MAX_REGS);
        if (!values || !status) return false;
    }
    params = p;
    memset(status, SCAN_PENDING, SCAN_MAX_REGS);
    done = 0;
    stopReq = false;
    running = true;
    if (xTaskCreatePinnedToCore(scanTask, "scanner", 4096, nullptr, 2, nullptr, 1) != pdPASS) {
        running = false;
        return false;
    }
    return true;
}

void scannerStop() { stopReq = true; }
bool scannerRunning() { return running; }

String scannerJson(uint16_t from) {
    String s;
    s.reserve(256 + 40 * 64);
    s += "{\"running\":";
    s += running ? "true" : "false";
    s += ",\"unit\":" + String(params.unit) + ",\"fc\":" + String(params.fc) + ",\"start\":" + String(params.start) +
         ",\"count\":" + String(values ? params.count : 0) + ",\"chunk\":" + String(params.chunk) +
         ",\"done\":" + String(values ? done : 0) + ",\"from\":" + String(from) + ",\"rows\":[";
    if (values) {
        uint16_t d = done;
        uint16_t to = min<uint16_t>(d, (uint16_t)min<uint32_t>(from + 512, 65535));  // limit response size
        for (uint16_t k = from; k < to; k++) {
            if (k > from) s += ',';
            s += '[' + String(params.start + k) + ',' + String(status[k]) + ',' + String(values[k]) + ']';
        }
    }
    s += "]}";
    return s;
}

void scannerCsv(const std::function<void(const String &)> &emit) {
    String s = "address;hex_address;status;value;signed;hex_value\n";
    if (values) {
        char line[80];
        for (uint16_t k = 0; k < done; k++) {
            uint16_t a = params.start + k;
            uint16_t v = values[k];
            if (status[k] == SCAN_OK)
                snprintf(line, sizeof(line), "%u;0x%04X;ok;%u;%d;0x%04X\n", a, a, v, (int16_t)v, v);
            else if (status[k] == SCAN_EXCEPTION)
                snprintf(line, sizeof(line), "%u;0x%04X;exception %u;;;\n", a, a, v);
            else
                snprintf(line, sizeof(line), "%u;0x%04X;%s;;;\n", a, a, statusName(status[k]));
            s += line;
            if (s.length() > 1400) {
                emit(s);
                s = "";
            }
        }
    }
    if (s.length()) emit(s);
}
