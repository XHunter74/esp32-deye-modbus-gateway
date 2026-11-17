// Deye register scanner.
// Reads a range of registers in blocks and writes a CSV (address, value or error).
//
// Two transports:
//   SCAN_PROTO=v5   — through the original Solarman logger (Solarman V5 protocol, port 8899, needs DEYE_LOGGER_SN)
//   SCAN_PROTO=rtu  — raw Modbus RTU frames over TCP, e.g. this ESP32 gateway (port 8899)
//
// Based on src/scan-registers.ts from the "Deye Bridge" test project.
//
// Usage (PowerShell):
//   $env:DEYE_HOST="192.168.1.50"; $env:DEYE_LOGGER_SN="1234567890"; npx tsx scan-registers.ts
//   $env:SCAN_PROTO="rtu"; $env:DEYE_HOST="deye-gw.local"; $env:SCAN_START="10032"; $env:SCAN_COUNT="76"; npx tsx scan-registers.ts
//
// Environment variables:
//   DEYE_HOST, DEYE_PORT (8899), DEYE_LOGGER_SN, DEYE_TIMEOUT_MS (5000)
//   SCAN_PROTO   v5 | rtu                              (default v5)
//   SCAN_START   first register                        (default 0)
//   SCAN_COUNT   number of registers                   (default 300)
//   SCAN_CHUNK   registers per request, 1..125         (default 60)
//   SCAN_FC      3 (holding) | 4 (input)               (default 3)
//   SCAN_UNITS   comma separated slave ids to try      (default 1)
//   SCAN_OUT     CSV file name                         (default scan-<proto>-<start>-<count>.csv)
//   SCAN_FIND    value to highlight in console output

import * as fs from 'node:fs';
import * as net from 'node:net';

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

function envNumber(name: string, fallback: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw === '') return fallback;
  const n = Number(raw);
  if (Number.isNaN(n)) throw new Error(`${name} must be a number, got: ${raw}`);
  return n;
}

const proto = (process.env.SCAN_PROTO || 'v5').toLowerCase();
if (proto !== 'v5' && proto !== 'rtu') throw new Error('SCAN_PROTO must be v5 or rtu');

const config = {
  host: process.env.DEYE_HOST || '',
  port: envNumber('DEYE_PORT', 8899),
  timeoutMs: envNumber('DEYE_TIMEOUT_MS', 5000),
  loggerSn: envNumber('DEYE_LOGGER_SN', 0),
};

const SCAN_START = envNumber('SCAN_START', 0);
const SCAN_COUNT = envNumber('SCAN_COUNT', 300);
const SCAN_CHUNK = envNumber('SCAN_CHUNK', 60);
const SCAN_FC = envNumber('SCAN_FC', 3);
const SCAN_UNITS = (process.env.SCAN_UNITS || '1').split(',').map((s) => Number(s.trim()));
const SCAN_FIND = process.env.SCAN_FIND ? Number(process.env.SCAN_FIND) : NaN;
const SCAN_OUT = process.env.SCAN_OUT || `scan-${proto}-${SCAN_START}-${SCAN_COUNT}.csv`;

if (SCAN_CHUNK < 1 || SCAN_CHUNK > 125) throw new Error('SCAN_CHUNK must be 1..125');
if (SCAN_FC !== 3 && SCAN_FC !== 4) throw new Error('SCAN_FC must be 3 or 4');
if (SCAN_UNITS.some((u) => Number.isNaN(u) || u < 0 || u > 247)) throw new Error('SCAN_UNITS must be 0..247');

// ---------------------------------------------------------------------------
// Modbus RTU / Solarman V5 helpers
// ---------------------------------------------------------------------------

function modbusRtuCrc(buf: Buffer): number {
  let crc = 0xffff;
  for (let i = 0; i < buf.length; i++) {
    crc ^= buf[i];
    for (let j = 0; j < 8; j++) {
      const bit = crc & 0x0001;
      crc >>>= 1;
      if (bit) crc ^= 0xa001;
    }
  }
  return crc;
}

function buildReadRequest(unitId: number, fc: number, startAddr: number, count: number): Buffer {
  const buf = Buffer.alloc(8);
  buf.writeUInt8(unitId, 0);
  buf.writeUInt8(fc, 1);
  buf.writeUInt16BE(startAddr, 2);
  buf.writeUInt16BE(count, 4);
  buf.writeUInt16LE(modbusRtuCrc(buf.subarray(0, 6)), 6);
  return buf;
}

function solarmanChecksum(buf: Buffer): number {
  let sum = 0;
  for (let i = 1; i < buf.length - 2; i++) sum += buf[i] & 0xff;
  return sum & 0xff;
}

function buildSolarmanV5Frame(loggerSn: number, modbusRtu: Buffer): Buffer {
  const PAYLOAD_HDR = 15;
  const total = 11 + PAYLOAD_HDR + modbusRtu.length + 2;
  const buf = Buffer.alloc(total, 0);
  buf.writeUInt8(0xa5, 0);
  buf.writeUInt16LE(PAYLOAD_HDR + modbusRtu.length, 1);
  buf.writeUInt16LE(0x4510, 3);
  buf.writeUInt8(0x01, 5);
  buf.writeUInt32LE(loggerSn >>> 0, 7);
  buf.writeUInt8(0x02, 11);
  buf.writeUInt32LE(Math.floor(Date.now() / 1000), 14);
  modbusRtu.copy(buf, 26);
  buf.writeUInt8(solarmanChecksum(buf), total - 2);
  buf.writeUInt8(0x15, total - 1);
  return buf;
}

class ModbusException extends Error {
  constructor(public code: number) {
    super(`Modbus exception ${code}`);
  }
}

// Parses an RTU response starting at `off`. Returns registers, null if incomplete; throws on exception.
function parseRtuResponse(data: Buffer, off: number, fc: number): number[] | null {
  if (data.length < off + 2) return null;
  const f = data[off + 1];
  if (f === (fc | 0x80)) {
    if (data.length < off + 5) return null;
    throw new ModbusException(data[off + 2]);
  }
  if (f !== fc) throw new Error(`Unexpected function code 0x${f.toString(16)}`);
  if (data.length < off + 3) return null;
  const byteCount = data[off + 2];
  if (data.length < off + 3 + byteCount + 2) return null;
  const frame = data.subarray(off, off + 3 + byteCount + 2);
  if (modbusRtuCrc(frame.subarray(0, frame.length - 2)) !== frame.readUInt16LE(frame.length - 2)) {
    throw new Error('CRC error');
  }
  const regs: number[] = [];
  for (let i = 0; i < byteCount; i += 2) regs.push(frame.readUInt16BE(3 + i));
  return regs;
}

function extractV5(data: Buffer, fc: number): number[] | null {
  for (let s = 0; s <= data.length - 3; s++) {
    if (data[s] !== 0xa5) continue;
    const payloadLen = data.readUInt16LE(s + 1);
    const frameLen = 11 + payloadLen + 2;
    if (data.length < s + frameLen) continue;
    const f = data.subarray(s, s + frameLen);
    if (f[frameLen - 1] !== 0x15) continue;
    const ctrl = f.readUInt16LE(3);
    if (ctrl !== 0x1510 && ctrl !== 0x4510) continue;
    // Modbus RTU frame starts at offset 25 (after 11-byte header + 14-byte payload header)
    if (frameLen < 25 + 5) throw new Error('Logger returned no Modbus data (inverter did not answer)');
    return parseRtuResponse(f.subarray(25, frameLen - 2), 0, fc);
  }
  return null;
}

// One TCP connection per request: simple and robust for both the logger and the gateway.
function readRegisters(unit: number, start: number, count: number): Promise<number[]> {
  const rtu = buildReadRequest(unit, SCAN_FC, start, count);
  const frame = proto === 'v5' ? buildSolarmanV5Frame(config.loggerSn, rtu) : rtu;

  return new Promise<number[]>((resolve, reject) => {
    const socket = new net.Socket();
    const chunks: Buffer[] = [];
    let settled = false;
    const settle = (fn: () => void) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      socket.destroy();
      fn();
    };
    const timer = setTimeout(() => settle(() => reject(new Error('timeout'))), config.timeoutMs);

    socket.connect(config.port, config.host, () => socket.write(frame));
    socket.on('data', (chunk: Buffer) => {
      chunks.push(chunk);
      try {
        const data = Buffer.concat(chunks);
        const regs = proto === 'v5' ? extractV5(data, SCAN_FC) : parseRtuResponse(data, 0, SCAN_FC);
        if (regs !== null) settle(() => resolve(regs));
      } catch (err) {
        settle(() => reject(err));
      }
    });
    socket.on('error', (err: Error) => settle(() => reject(err)));
    socket.on('close', () => settle(() => reject(new Error('connection closed'))));
  });
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

const hex = (v: number) => `0x${v.toString(16).toUpperCase().padStart(4, '0')}`;

async function scan(): Promise<void> {
  if (!config.host) throw new Error('Set DEYE_HOST');
  if (proto === 'v5' && !config.loggerSn) throw new Error('Set DEYE_LOGGER_SN for SCAN_PROTO=v5');

  const lines = ['unit;address;hex_address;status;value;signed;hex_value'];
  console.log(`Scanning ${SCAN_START}-${SCAN_START + SCAN_COUNT - 1}, FC${SCAN_FC}, chunk ${SCAN_CHUNK}, ` +
    `units [${SCAN_UNITS.join(',')}], ${proto} via ${config.host}:${config.port}`);

  for (const unit of SCAN_UNITS) {
    let ok = 0;
    let failed = 0;
    for (let reg = SCAN_START; reg < SCAN_START + SCAN_COUNT; ) {
      const n = Math.min(SCAN_CHUNK, SCAN_START + SCAN_COUNT - reg);
      try {
        const values = await readRegisters(unit, reg, n);
        values.forEach((v, i) => {
          const a = reg + i;
          const s = v >= 0x8000 ? v - 0x10000 : v;
          lines.push(`${unit};${a};${hex(a)};ok;${v};${s};${hex(v)}`);
          const mark = !Number.isNaN(SCAN_FIND) && v === SCAN_FIND ? '  <<<' : '';
          console.log(`  unit ${unit}  ${String(a).padStart(5)}  ${hex(a)}  ${String(v).padStart(6)}  ${hex(v)}${mark}`);
        });
        ok += n;
      } catch (err) {
        const msg = (err as Error).message;
        for (let i = 0; i < n; i++) lines.push(`${unit};${reg + i};${hex(reg + i)};${msg};;;`);
        console.error(`  unit ${unit}  ${reg}-${reg + n - 1}: ${msg}`);
        failed += n;
      }
      reg += n;
    }
    console.log(`unit ${unit}: ${ok} registers answered, ${failed} failed`);
  }

  fs.writeFileSync(SCAN_OUT, lines.join('\n') + '\n');
  console.log(`CSV written to ${SCAN_OUT}`);
}

scan().catch((err) => {
  console.error((err as Error).message);
  process.exitCode = 1;
});
