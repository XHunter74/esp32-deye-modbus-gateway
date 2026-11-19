# rs232-rs485-TCP: Deye RS232 → TCP Modbus gateway (ESP32)

ESP32 firmware that replaces the Solarman WiFi data logger on a Deye hybrid inverter. It plugs into the inverter's DB9 logger port through a MAX3232 RS232-TTL board and exposes the inverter's Modbus RTU interface over WiFi as a plain TCP server.

Developed for a Deye SUN-10K single-phase hybrid (SG0xLP1). The primary client is the [ioBroker.deyeidc](https://github.com/raschy/ioBroker.deyeidc) adapter in its "RS485 converter" mode (raw Modbus RTU over TCP). Any other Modbus software works too.

## Features

- **Modbus RTU over TCP** (default, port 8899): raw RTU frames with CRC are passed through unchanged, like a USR-TCP232 converter.
- **Modbus TCP (MBAP)** mode (port 502 recommended): converts Modbus TCP to RTU and back, for generic Modbus TCP clients.
- Up to 4 TCP clients at once. Requests are serialized on the serial line, so clients never corrupt each other's frames.
- WiFi setup through a captive portal ([WiFiManager](https://github.com/tzapu/WiFiManager)), no hard-coded credentials.
- Tasmota-style web UI:
  - **Status**: WiFi, uptime, TCP clients, request/response/timeout/CRC counters, last frames in hex.
  - **Settings**: gateway mode, TCP port, serial parameters, pins, timeouts, hostname, web password.
  - **Scanner**: reads any register range in blocks, shows values and errors, exports CSV.
  - **Firmware**: OTA update from the browser.
- mDNS: `http://deye-gw.local`
- Hold the **BOOT** button for 5 s to erase WiFi settings.
- Self-healing WiFi: background reconnect with backoff, restart after 5 min without WiFi, gateway ping check that catches a "connected" but dead link, no fallback to the setup AP once configured.
- Diagnostics on the Status page: boot counter, last reset reason (brownout, crash, watchdog, restart cause), history of the last 10 restarts with uptime before each, WiFi drops with reason codes, minimum free heap.
- The on-board LED (GPIO2) lights up while a request is on the serial line.

## Hardware

| Part | Notes |
|---|---|
| ESP32 DevKit (ESP32-WROOM-32) | any board with GPIO16/17 free |
| RS232-TTL board with MAX3232 | must work at 3.3 V (pins: VCC, GND, TXD, RXD) |
| DC-DC buck converter 12 V → 5 V | e.g. MP1584EN mini module, ≥ 500 mA |
| DB9 male connector + wires | to plug into the inverter's logger port |

### Deye DB9 logger port

According to the Deye SG05LP1 manuals (SUN-3.6K…10K), the WIFI/RS232 port is a DB9 with:

| DB9 pin | Signal (inverter side) |
|---|---|
| 2 | TX (inverter transmits) |
| 3 | RX (inverter receives) |
| 5 | D-GND |
| 9 | +12 V DC (logger power) |

Some sources (e.g. SolarAssistant) label pins 2 and 3 the other way round, because they look from the cable side. **Measure before connecting** (see below).

> The port supplies **12 V, not 5 V**. Never connect pin 9 directly to the ESP32 or to the MAX3232 board.

### Wiring diagram

```
  Deye inverter                 RS232-TTL board                ESP32 DevKit
  DB9 logger port               (MAX3232)
 ┌──────────────┐              ┌──────────────────┐           ┌───────────────┐
 │ 2  TX  ──────┼──────────────┼─► RS232 RX       │           │               │
 │              │              │        TTL TXD ──┼──────────►│ GPIO16 (RX2)  │
 │ 3  RX  ◄─────┼──────────────┼── RS232 TX       │           │               │
 │              │              │        TTL RXD ◄─┼───────────│ GPIO17 (TX2)  │
 │ 5  GND ──────┼──────┬───────┼── GND        VCC ◄┼───────────│ 3V3           │
 │              │      │       │              GND ─┼─────┬─────│ GND           │
 │ 9  +12V ─────┼──┐   │       └──────────────────┘     │     │               │
 └──────────────┘  │   │                                │     │               │
                   │   │       ┌──────────────────┐     │     │               │
                   └───┼──────►│ IN+   DC-DC  OUT+├─────┼────►│ VIN (5V)      │
                       └──────►│ IN-   12→5V  OUT-├─────┴────►│ GND           │
                               └──────────────────┘           └───────────────┘
```

On most MAX3232 modules with a female DB9 the RS232 side is already wired to the DB9 pins 2/3/5. In that case connect it to the inverter with a **null-modem** (crossed) adapter, or use the screw terminals / solder pads and cross TX/RX as shown above.

### Connection table

| From | To | Note |
|---|---|---|
| Deye DB9 pin 2 (TX) | MAX3232 RS232 RX (R1IN) | inverter → gateway |
| Deye DB9 pin 3 (RX) | MAX3232 RS232 TX (T1OUT) | gateway → inverter |
| Deye DB9 pin 5 (GND) | MAX3232 GND, DC-DC IN−, ESP32 GND | common ground |
| Deye DB9 pin 9 (+12 V) | DC-DC IN+ | only to the converter |
| DC-DC OUT+ (5.0 V) | ESP32 VIN / 5V | set 5.0 V **before** connecting the ESP32 |
| DC-DC OUT− | ESP32 GND | |
| MAX3232 VCC | ESP32 3V3 | 3.3 V keeps TTL levels safe for the ESP32 |
| MAX3232 TXD (TTL out) | ESP32 GPIO16 (RX2) | configurable in Settings |
| MAX3232 RXD (TTL in) | ESP32 GPIO17 (TX2) | configurable in Settings |

### Measure before connecting

With the logger removed and the inverter on, measure with a multimeter against **pin 5**:

| Pin | Expected | Meaning |
|---|---|---|
| 9 | ≈ +12 V | power for the logger |
| 2 | ≈ −5 … −12 V | idle RS232 TX of the inverter |
| 3 | ≈ 0 V | inverter's RX input |

If pins 2 and 3 are the other way round, swap the two data wires. Also set the DC-DC output to 5.0 V with only the converter connected, before you connect the ESP32.

## Build and flash

Requirements: [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).

```bash
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial log, 115200 baud
```

After the first flash, updates can be uploaded from the web UI (**Firmware** tab) using `.pio/build/esp32dev/firmware.bin`.

## First start

1. Power the ESP32. With no WiFi stored, it opens the access point **`Deye-GW-XXXXXX`**.
2. Connect to it with a phone or laptop. The captive portal opens (or go to `http://192.168.4.1`).
3. Choose your WiFi network and enter the password. The gateway reboots and joins the network.
4. Open `http://deye-gw.local` (or the IP from your router) and check **Status**.
5. Optional: set a web password in **Settings** (user `admin`).

The portal opens only while no WiFi credentials are stored. Once a network is saved, the gateway never falls back to the portal on its own, so a router outage cannot take it off the LAN:

- it keeps reconnecting in the background (every 10 s, backing off to 2 min);
- if WiFi stays down for **5 minutes**, it restarts itself;
- **mesh networks**: it scans all channels and joins the node with the strongest signal (the ESP32 default joins the first node found, often a distant one), and sends a gratuitous ARP right after every (re)connect so the mesh learns the new path at once (lwIP repeats it every 60 s). The Status page shows the node (AP BSSID) and channel;
- every 30 s it pings the router (default gateway). WiFi can report "connected" while the router no longer delivers packets, and the gateway then silently drops off the LAN. After 3 unanswered checks in a row (~1.5 min) it reconnects WiFi, after 8 (~4 min) it restarts with cause `gateway unreachable`. Independently of the WiFi status and of the ping state, it restarts with cause `no gateway reply for 5 min` if no ping was answered for 5 minutes; a ping that does not finish within 10 s counts as failed. If the router never answered ping since boot, the check does nothing;
- to change the network, use **Settings → Reset WiFi settings** or hold **BOOT** for 5 s.

## Default settings

| Setting | Default |
|---|---|
| Mode | Modbus RTU over TCP |
| TCP port | 8899 |
| Serial | 9600 8N1, RX GPIO16, TX GPIO17 |
| Response timeout | 1000 ms |
| Frame gap (end of frame of unknown length) | 20 ms |
| Hostname | `deye-gw` |
| Web password | none |

## ioBroker.deyeidc setup

Use the adapter version with RS485 converter support ([PR 281](https://github.com/raschy/ioBroker.deyeidc/pull/281)):

- Connection type: **RS485 converter** (raw Modbus RTU over TCP)
- IP: the gateway's IP address (or `deye-gw.local`)
- Port: **8899**
- Slave ID: **1**

Register sets you already use with the Solarman logger keep working. The gateway does not filter addresses, so anything the inverter answers on RS232 is passed through.

## Registers checked over RS232

The table below comes from a scan of a SUN-10K (LP1) through the original Solarman logger. That logger sits on the same RS232 port, so the result applies to the gateway too. Settings: slave 1, FC03.

| Range | Result |
|---|---|
| 0–719 | all answered; 0–279 is the main LP1 map; 500+ is almost all zero on LP1 |
| 10000–10199 | all answered; battery (BMS) data |
| block of 125 registers | answered |

### Battery (BMS) registers

The inverter collects battery data from the BMS and exposes it with the **same slave ID** as the inverter (no separate slave). Each battery pack takes a block of 38 registers starting at **10032**:

| Battery | Block | Serial number | Cycles |
|---|---|---|---|
| 1 | 10032–10069 | 10032–10039 | **10056** |
| 2 | 10070–10107 | 10070–10077 | **10094** |
| N | 10032 + 38·(N−1) | +0…+7 | +24 |

The layout matches the "Battery N" groups in the [ha-solarman](https://github.com/davidrapan/ha-solarman) profile `deye_p3.yaml` (Battery 1 cycles = 0x2748, Battery 2 cycles = 0x276E).

## Register scanner (`tools/`)

`tools/scan-registers.ts` reads a register range and writes a CSV. It works with both the original Solarman logger (protocol V5) and this gateway (raw RTU), so you can compare the two.

```bash
cd tools
npm install
```

Through the Solarman logger (before swapping the hardware), in PowerShell:

```powershell
$env:DEYE_HOST="192.168.1.50"; $env:DEYE_LOGGER_SN="1234567890"; $env:SCAN_START="10000"; $env:SCAN_COUNT="200"; npm run scan
```

Through the gateway:

```powershell
$env:SCAN_PROTO="rtu"; $env:DEYE_HOST="deye-gw.local"; $env:SCAN_START="10000"; $env:SCAN_COUNT="200"; npm run scan
```

| Variable | Default | Meaning |
|---|---|---|
| `DEYE_HOST` | – | logger or gateway address |
| `DEYE_PORT` | 8899 | TCP port |
| `DEYE_LOGGER_SN` | – | logger serial number (V5 only) |
| `DEYE_TIMEOUT_MS` | 5000 | per-request timeout |
| `SCAN_PROTO` | `v5` | `v5` (Solarman logger) or `rtu` (this gateway) |
| `SCAN_START` / `SCAN_COUNT` | 0 / 300 | register range |
| `SCAN_CHUNK` | 60 | registers per request (1–125) |
| `SCAN_FC` | 3 | 3 = holding, 4 = input registers |
| `SCAN_UNITS` | 1 | slave IDs to try, comma separated |
| `SCAN_OUT` | `scan-<proto>-<start>-<count>.csv` | output file |
| `SCAN_FIND` | – | value to highlight in console output |

The same scan is available without a PC in the web UI (**Scanner** tab).

## Troubleshooting

### The gateway does not answer ping

`Destination host unreachable` from your own PC means nothing answers ARP: the ESP32 is either not connected to WiFi or its link is dead while it still believes it is connected (not a web server problem).

If it comes back after a reset and the Status page shows the boot counter increased by one only (no restart in between), the link was dead without the ESP32 noticing. The gateway ping check now recovers from this on its own; look at the **Gateway ping** row and for restart cause `gateway unreachable`. Otherwise check, in this order:

1. **Power**: the red power LED must be on. Measure VIN under load: it should stay at 4.8–5.2 V. WiFi transmit peaks reach 300–500 mA, so use a buck converter rated ≥ 1 A and add 470–1000 µF between VIN and GND. A weak supply causes brownout reboot loops.
2. **Serial log**: connect USB (it can stay on the inverter) and run `pio device monitor`. Each boot prints `[diag] boot #N, reset reason: ...` and every WiFi event with its reason code (`Brownout detector was triggered` = power problem).
3. **Setup AP**: look for `Deye-GW-XXXXXX` with a phone next to the inverter. It is only there when no WiFi credentials are stored.
4. **Router**: check the client/DHCP list for `deye-gw`, its signal strength and when it last connected. A fixed DHCP lease makes the address predictable.
5. **Signal**: the inverter's metal case shields the small DevKit antenna. Below about −75 dBm, move the ESP32 outside the case or use a board with an external antenna.

### Status page diagnostics

| Row | Meaning |
|---|---|
| Uptime · boot #N | time since boot; the boot counter survives restarts |
| Last reset | why the current boot happened: power-on, brownout, crash (panic), watchdog, or software restart with its cause (settings saved, firmware update, WiFi down for 5 min, ...) |
| WiFi uptime | time since the current WiFi connection was established |
| Gateway ping | router address, last round-trip time, how long ago it last answered, failed checks and forced reconnects since boot |
| Restart history | last 10 boots, newest first: boot number, how long the previous run lasted, reset reason with restart cause and the raw ROM reset code (e.g. `RTCWDT_RTC`). "ran before" is kept in RTC memory, which survives watchdog, crash and software resets but not a power loss: *unknown (power lost)* means the supply went off, a known value means the chip reset while powered. Each entry also shows the **last heartbeat** of the previous run, saved to flash every 5 min (survives a power loss): uptime, WiFi status, how long before that the gateway last answered ping, failed checks, whether a ping was running, and the mesh node (AP BSSID) and channel. Also at `/api/history` |
| WiFi drops | lost connections since boot, time of the last one and the WiFi reason code (e.g. 200 beacon timeout = signal lost, 15/202 = wrong password) |
| Free heap (min) | current and lowest free memory since boot |

A main loop frozen for more than 30 s is caught by the loop watchdog; the next boot then shows `task watchdog` as the reset reason. (The ESP-IDF default of 5 s is too short: on a dead link a web response can block in TCP send for ~10 s.)

## Testing without the inverter

Short pins 2 and 3 of the DB9 on the MAX3232 side (loopback). Every request sent to TCP port 8899 then comes back as the "response". The Status page counters show the traffic.

## Project layout

```
platformio.ini        build configuration (espressif32 6.12.0, Arduino)
src/main.cpp          startup, BOOT button, loop watchdog
src/diag.*            boot counter, reset reason, restart cause, WiFi statistics
src/config.*          settings stored in NVS
src/rtu.*             UART / Modbus RTU master, bus mutex, statistics
src/tcp_bridge.*      TCP server: RTU-over-TCP and Modbus TCP (MBAP)
src/scanner.*         register scanner task
src/netcheck.*        gateway ping check: reconnect / restart when the link is dead
src/wifi_setup.*      WiFiManager captive portal, reconnect with backoff, WiFi watchdog
src/web_ui.*          HTTP API and OTA
src/web_page.h        single-page web UI
tools/                register scanner for PC (Node.js + TypeScript)
```
