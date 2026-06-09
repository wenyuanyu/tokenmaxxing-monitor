import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import noble from '@abandonware/noble';

const DEVICE_NAMES = new Set(['QwenToken', 'Qwen Usage']);
const SERVICE_UUID = '00112233445566778899aabbccddeeff';
const DATA_CHAR_UUID = '00112233445566778899aabbccddee01';
const INTERVAL_MS = Number(process.env.QWEN_BLE_PUSH_MS ?? 1000);
const RECENT_DAYS = Number(process.env.QWEN_BLE_SCAN_DAYS ?? 7);
const TAIL_BYTES = Number(process.env.QWEN_BLE_TAIL_BYTES ?? 2 * 1024 * 1024);
const ACTIVE_GAP_MS = 5 * 60 * 1000;

let dataChar = null;
let connectedPeripheral = null;
let scanTimer = null;
let pushTimer = null;
let connecting = false;

function parseEnvFile(file) {
  try {
    const out = {};
    for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
      const m = line.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)\s*$/);
      if (!m) continue;
      out[m[1]] = m[2].replace(/^['"]|['"]$/g, '');
    }
    return out;
  } catch {
    return {};
  }
}

function runtimeDir() {
  const home = os.homedir();
  const qwenHome = process.env.QWEN_HOME ?? parseEnvFile(path.join(home, '.qwen', '.env')).QWEN_HOME ?? path.join(home, '.qwen');
  const homeEnv = parseEnvFile(path.join(home, '.env'));
  return process.env.QWEN_RUNTIME_DIR ?? parseEnvFile(path.join(qwenHome, '.env')).QWEN_RUNTIME_DIR ?? homeEnv.QWEN_RUNTIME_DIR ?? qwenHome;
}

function usageTotal(u) {
  return Number(u.totalTokenCount ?? 0) ||
    Number(u.promptTokenCount ?? 0) +
    Number(u.candidatesTokenCount ?? 0) +
    Number(u.thoughtsTokenCount ?? 0);
}

function sessionFiles() {
  const base = runtimeDir();
  const cutoff = Date.now() - RECENT_DAYS * 24 * 60 * 60 * 1000;
  const files = [];
  const scanRoot = (root) => {
    try {
      if (!fs.existsSync(root)) return;
      for (const project of fs.readdirSync(root)) {
        const chats = path.join(root, project, 'chats');
        if (!fs.existsSync(chats)) continue;
        for (const name of fs.readdirSync(chats)) {
          if (!/^[0-9a-fA-F-]{32,36}\.jsonl$/.test(name)) continue;
          const file = path.join(chats, name);
          const stat = fs.statSync(file);
          if (stat.mtimeMs >= cutoff) files.push({ file, stat });
        }
      }
    } catch {
      // Keep the bridge alive if a session file is rotated while scanning.
    }
  };
  scanRoot(path.join(base, 'projects'));
  scanRoot(path.join(base, 'tmp'));
  return files.sort((a, b) => b.stat.mtimeMs - a.stat.mtimeMs);
}

function readTail(file, size) {
  const stat = fs.statSync(file);
  const fd = fs.openSync(file, 'r');
  try {
    if (stat.size <= size) return fs.readFileSync(fd, 'utf8');
    const buf = Buffer.alloc(size);
    fs.readSync(fd, buf, 0, size, stat.size - size);
    return buf.toString('utf8').replace(/^[^\n]*\n/, '');
  } finally {
    fs.closeSync(fd);
  }
}

function clampPct(n) {
  return Math.max(0, Math.min(100, Math.round(n)));
}

function formatStamp(ms) {
  const d = new Date(ms);
  const mm = String(d.getMonth() + 1).padStart(2, '0');
  const dd = String(d.getDate()).padStart(2, '0');
  const hh = String(d.getHours()).padStart(2, '0');
  const mi = String(d.getMinutes()).padStart(2, '0');
  const ss = String(d.getSeconds()).padStart(2, '0');
  return `${mm}-${dd} ${hh}:${mi}:${ss}`;
}

function modelName(rec) {
  return String(rec.model ?? 'qwen').replace(/[|\r\n]/g, ' ').trim().slice(0, 23) || 'qwen';
}

function isErrorRecord(rec) {
  if (rec.type === 'error') return true;
  const status = Number(rec.status_code ?? rec.statusCode ?? rec.status);
  return Number.isFinite(status) && status >= 400;
}

function sessionKey(rec, file) {
  return String(rec.sessionId ?? file);
}

function activeMinutes(sessionEvents) {
  let activeMs = 0;
  for (const events of sessionEvents.values()) {
    events.sort((a, b) => a - b);
    for (let i = 1; i < events.length; i++) {
      const delta = events[i] - events[i - 1];
      if (delta > 0) activeMs += Math.min(delta, ACTIVE_GAP_MS);
    }
  }
  return Math.round(activeMs / 60000);
}

function topModels(modelTotals, todayTotal) {
  const rows = [...modelTotals.entries()]
    .sort((a, b) => b[1] - a[1])
    .slice(0, 3)
    .map(([model, total]) => ({
      model,
      pct: todayTotal > 0 ? clampPct((total / todayTotal) * 100) : 0,
    }));
  while (rows.length < 3) rows.push({ model: '--', pct: 0 });
  return rows;
}

function buildReport() {
  const today = new Date();
  today.setHours(0, 0, 0, 0);
  const todayMs = today.getTime();
  const files = sessionFiles();

  let todayTotal = 0;
  let todayInput = 0;
  let todayOutput = 0;
  let todayCached = 0;
  let todayThought = 0;
  let callsToday = 0;
  let errorsToday = 0;
  let weekTotal = 0;
  const todaySessions = new Set();
  const sessionEvents = new Map();
  const modelTotals = new Map();
  let latest = null;

  for (const { file } of files) {
    for (const line of readTail(file, TAIL_BYTES).split(/\r?\n/)) {
      if (!line.trim()) continue;
      let rec;
      try {
        rec = JSON.parse(line);
      } catch {
        continue;
      }
      const ts = Date.parse(rec.timestamp ?? '');
      const isToday = Number.isFinite(ts) && ts >= todayMs;
      if (isToday) {
        const key = sessionKey(rec, file);
        const events = sessionEvents.get(key) ?? [];
        events.push(ts);
        sessionEvents.set(key, events);
      }
      if (isToday && isErrorRecord(rec)) errorsToday++;

      if (rec.type !== 'assistant' || !rec.usageMetadata) continue;

      const u = rec.usageMetadata;
      const input = Number(u.promptTokenCount ?? 0);
      const output = Number(u.candidatesTokenCount ?? 0);
      const thought = Number(u.thoughtsTokenCount ?? 0);
      const cached = Number(u.cachedContentTokenCount ?? 0);
      const total = usageTotal(u);

      weekTotal += total;

      if (Number.isFinite(ts) && (!latest || ts > latest.ts)) {
        latest = {
          ts,
          input,
          total,
          model: modelName(rec),
          context: Number(rec.contextWindowSize ?? 0),
        };
      }

      if (isToday) {
        callsToday++;
        todaySessions.add(sessionKey(rec, file));
        todayTotal += total;
        todayInput += input;
        todayOutput += output;
        todayCached += cached;
        todayThought += thought;
        const model = modelName(rec);
        modelTotals.set(model, (modelTotals.get(model) ?? 0) + total);
      }
    }
  }

  const now = Date.now();
  const latestTs = latest?.ts ?? now;
  const ctxPct = latest?.context > 0 ? (latest.input / latest.context) * 100 : 0;
  const models = topModels(modelTotals, todayTotal);
  return {
    todayTotal,
    ctxPct: clampPct(ctxPct),
    callsToday,
    errorsToday,
    sessionsToday: todaySessions.size,
    cacheRate: todayInput > 0 ? clampPct((todayCached / todayInput) * 100) : 0,
    activeMinutes: activeMinutes(sessionEvents),
    currentTokens: latest?.input ?? 0,
    lastCallTokens: latest?.total ?? 0,
    todayInput,
    todayOutput,
    todayCached,
    todayThought,
    model: latest?.model ?? 'qwen',
    models,
    updatedAt: formatStamp(now),
    ageSec: Math.max(0, Math.round((now - latestTs) / 1000)),
    weekTotal,
  };
}

function toPayload(r) {
  return [
    3,
    r.todayTotal,
    r.sessionsToday,
    r.todayCached,
    r.cacheRate,
    r.activeMinutes,
    r.updatedAt,
    r.models[0].model,
    r.models[0].pct,
    r.models[1].model,
    r.models[1].pct,
    r.models[2].model,
    r.models[2].pct,
    r.errorsToday,
    r.ageSec,
    r.todayOutput,
    r.weekTotal,
    r.todayInput,
  ].join('|');
}

async function writePayload() {
  if (!dataChar) return;
  const report = buildReport();
  const payload = toPayload(report);
  await new Promise((resolve, reject) => {
    dataChar.write(Buffer.from(payload), false, (err) => err ? reject(err) : resolve());
  });
  console.log(`[ble] wrote ${payload}`);
}

function startPushLoop() {
  clearInterval(pushTimer);
  writePayload().catch((err) => console.error('[ble] write failed:', err.message));
  pushTimer = setInterval(() => {
    writePayload().catch((err) => console.error('[ble] write failed:', err.message));
  }, INTERVAL_MS);
}

async function discoverCharacteristic(peripheral) {
  const services = await new Promise((resolve, reject) => {
    peripheral.discoverServices([SERVICE_UUID], (err, svcs) => err ? reject(err) : resolve(svcs));
  });
  if (!services[0]) throw new Error('service not found');
  const chars = await new Promise((resolve, reject) => {
    services[0].discoverCharacteristics([DATA_CHAR_UUID], (err, cs) => err ? reject(err) : resolve(cs));
  });
  if (!chars[0]) throw new Error('data characteristic not found');
  return chars[0];
}

function connect(peripheral) {
  if (connecting || dataChar) return;
  connecting = true;
  noble.stopScanning();
  console.log(`[ble] connecting ${peripheral.advertisement.localName ?? peripheral.id}`);

  const timeout = setTimeout(() => {
    console.error('[ble] connect timeout');
    try { peripheral.disconnect(); } catch {}
    connecting = false;
    startScan();
  }, 10000);

  peripheral.connect(async (err) => {
    clearTimeout(timeout);
    connecting = false;
    if (err) {
      console.error('[ble] connect failed:', err.message);
      startScan();
      return;
    }
    connectedPeripheral = peripheral;
    peripheral.once('disconnect', () => {
      console.log('[ble] disconnected');
      dataChar = null;
      connectedPeripheral = null;
      clearInterval(pushTimer);
      startScan();
    });
    try {
      dataChar = await discoverCharacteristic(peripheral);
      console.log('[ble] ready');
      startPushLoop();
    } catch (e) {
      console.error('[ble] discover failed:', e.message);
      try { peripheral.disconnect(); } catch {}
      startScan();
    }
  });
}

function startScan() {
  if (dataChar || connecting) return;
  clearTimeout(scanTimer);
  console.log('[ble] scanning for QwenToken');
  try {
    noble.startScanning([], false);
  } catch (e) {
    console.error('[ble] scan start failed:', e.message);
  }
  scanTimer = setTimeout(() => {
    noble.stopScanning();
    startScan();
  }, 15000);
}

noble.on('stateChange', (state) => {
  console.log(`[ble] adapter state: ${state}`);
  if (state === 'poweredOn') startScan();
  else noble.stopScanning();
});

noble.on('discover', (peripheral) => {
  const name = peripheral.advertisement.localName;
  if (!DEVICE_NAMES.has(name)) return;
  connect(peripheral);
});

process.on('SIGINT', () => {
  clearInterval(pushTimer);
  if (connectedPeripheral) connectedPeripheral.disconnect();
  process.exit(0);
});
