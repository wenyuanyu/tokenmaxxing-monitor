import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import noble from '@abandonware/noble';

const BLE_DEVICE_NAMES = new Set(
  (process.env.QWEN_BLE_DEVICE_NAME || 'QwenToken,Qwen Usage')
    .split(',')
    .map(s => s.trim())
    .filter(Boolean)
);
const SERVICE_UUID = '00112233445566778899aabbccddeeff';
const DATA_CHAR_UUID = '00112233445566778899aabbccddee01';
const INTERVAL_MS = Number(process.env.QWEN_BLE_PUSH_MS ?? 1000);
const RECENT_DAYS = Number(process.env.QWEN_BLE_SCAN_DAYS ?? 7);
const ACTIVE_GAP_MS = 5 * 60 * 1000;
const STATUS_FILE = '/tmp/qwen-token-status.json';
const DATA_SOURCE_NAMES = [
  ...new Set(
    (process.env.TOKEN_MONITOR_DATASOURCES ?? process.env.QWEN_BLE_DATASOURCES ?? 'qwen')
      .split(',')
      .map(s => s.trim().toLowerCase())
      .filter(Boolean)
  ),
];

let dataChar = null;
let bleConnected = false;
let bleDevice = '';
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

function usageFiles() {
  const dir = path.join(runtimeDir(), 'usage');
  const cutoff = Date.now() - (RECENT_DAYS + 1) * 24 * 60 * 60 * 1000;
  const files = [];
  try {
    if (!fs.existsSync(dir)) return files;
    for (const name of fs.readdirSync(dir)) {
      if (!/^token-usage-\d{4}-\d{2}\.jsonl$/.test(name)) continue;
      const file = path.join(dir, name);
      const stat = fs.statSync(file);
      if (stat.mtimeMs >= cutoff) files.push({ file, stat });
    }
  } catch {
    // Keep the bridge alive if a usage file is rotated while scanning.
  }
  return files.sort((a, b) => b.stat.mtimeMs - a.stat.mtimeMs);
}

function codexHome() {
  return process.env.CODEX_HOME ?? path.join(os.homedir(), '.codex');
}

function codexSessionFiles() {
  const root = process.env.CODEX_SESSIONS_DIR ?? path.join(codexHome(), 'sessions');
  const cutoff = Date.now() - (RECENT_DAYS + 1) * 24 * 60 * 60 * 1000;
  const files = [];

  const walk = (dir) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }

    for (const ent of entries) {
      const full = path.join(dir, ent.name);
      if (ent.isDirectory()) {
        walk(full);
        continue;
      }
      if (!ent.isFile() || !/^rollout-.*\.jsonl$/.test(ent.name)) continue;
      try {
        const stat = fs.statSync(full);
        if (stat.mtimeMs >= cutoff) files.push({ file: full, stat });
      } catch {
        // Ignore files that rotate while scanning.
      }
    }
  };

  walk(root);
  return files.sort((a, b) => a.stat.mtimeMs - b.stat.mtimeMs);
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

function safeModelName(model, fallback = 'codex') {
  return String(model ?? fallback).replace(/[|\r\n]/g, ' ').trim().slice(0, 23) || fallback;
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

function todayInfo() {
  const today = new Date();
  today.setHours(0, 0, 0, 0);
  const yyyy = today.getFullYear();
  const mm = String(today.getMonth() + 1).padStart(2, '0');
  const dd = String(today.getDate()).padStart(2, '0');
  return {
    todayStartMs: today.getTime(),
    todayDateStr: `${yyyy}-${mm}-${dd}`,
    weekCutoffMs: Date.now() - RECENT_DAYS * 24 * 60 * 60 * 1000,
  };
}

function emptyTotals(source = 'aggregate') {
  return {
    source,
    todayTotal: 0,
    todayInput: 0,
    todayOutput: 0,
    todayCached: 0,
    todayThought: 0,
    callsToday: 0,
    errorsToday: 0,
    weekTotal: 0,
    todaySessions: new Set(),
    sessionEvents: new Map(),
    modelTotals: new Map(),
    latest: null,
  };
}

function addSessionEvent(totals, key, ts) {
  if (!Number.isFinite(ts)) return;
  const events = totals.sessionEvents.get(key) ?? [];
  events.push(ts);
  totals.sessionEvents.set(key, events);
}

function setLatest(totals, latest) {
  if (!Number.isFinite(latest?.ts)) return;
  if (!totals.latest || latest.ts > totals.latest.ts) totals.latest = latest;
}

function mergeTotals(target, source) {
  target.todayTotal += source.todayTotal;
  target.todayInput += source.todayInput;
  target.todayOutput += source.todayOutput;
  target.todayCached += source.todayCached;
  target.todayThought += source.todayThought;
  target.callsToday += source.callsToday;
  target.errorsToday += source.errorsToday;
  target.weekTotal += source.weekTotal;

  for (const session of source.todaySessions) {
    target.todaySessions.add(`${source.source}:${session}`);
  }
  for (const [session, events] of source.sessionEvents) {
    const key = `${source.source}:${session}`;
    const existing = target.sessionEvents.get(key) ?? [];
    existing.push(...events);
    target.sessionEvents.set(key, existing);
  }
  for (const [model, total] of source.modelTotals) {
    target.modelTotals.set(model, (target.modelTotals.get(model) ?? 0) + total);
  }
  setLatest(target, source.latest);
}

function qwenReport() {
  const { todayDateStr, weekCutoffMs } = todayInfo();
  const files = usageFiles();
  const totals = emptyTotals('qwen');

  for (const { file } of files) {
    let content;
    try {
      content = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }
    for (const line of content.split(/\r?\n/)) {
      if (!line.trim()) continue;
      let rec;
      try {
        rec = JSON.parse(line);
      } catch {
        continue;
      }
      if (typeof rec.inputTokens !== 'number') continue;

      const ts = Date.parse(rec.timestamp ?? '');
      const isToday = rec.localDate === todayDateStr;
      const isWeek = Number.isFinite(ts) && ts >= weekCutoffMs;

      if (isToday) {
        totals.callsToday++;
        const key = String(rec.sessionId ?? '');
        if (rec.sessionId) totals.todaySessions.add(rec.sessionId);
        addSessionEvent(totals, key, ts);

        totals.todayTotal += rec.totalTokens ?? 0;
        totals.todayInput += rec.inputTokens ?? 0;
        totals.todayOutput += rec.outputTokens ?? 0;
        totals.todayCached += rec.cachedTokens ?? 0;
        totals.todayThought += rec.thoughtsTokens ?? 0;

        const model = modelName(rec);
        totals.modelTotals.set(model, (totals.modelTotals.get(model) ?? 0) + (rec.totalTokens ?? 0));
      }

      if (isWeek) {
        totals.weekTotal += rec.totalTokens ?? 0;
      }

      setLatest(totals, {
        ts,
        input: rec.inputTokens ?? 0,
        total: rec.totalTokens ?? 0,
        model: modelName(rec),
      });
    }
  }

  return totals;
}

function codexThreadId(file) {
  return path.basename(file).match(/([0-9a-fA-F-]{36})\.jsonl$/)?.[1] ?? file;
}

function codexModelMap() {
  const db = process.env.CODEX_STATE_DB ?? path.join(codexHome(), 'state_5.sqlite');
  const models = new Map();
  try {
    const out = execFileSync('sqlite3', [
      db,
      "select id || char(9) || coalesce(nullif(model,''),'codex') from threads;",
    ], { encoding: 'utf8', timeout: 2000 });
    for (const line of out.split(/\r?\n/)) {
      if (!line.trim()) continue;
      const [id, model] = line.split('\t');
      if (id) models.set(id, safeModelName(model));
    }
  } catch {
    // sqlite3 is optional; rollout files still carry enough usage data.
  }
  return models;
}

function usageNumber(obj, key) {
  const n = Number(obj?.[key] ?? 0);
  return Number.isFinite(n) ? n : 0;
}

function usageDelta(current, previous) {
  const delta = {
    input: usageNumber(current, 'input_tokens') - usageNumber(previous, 'input_tokens'),
    cached: usageNumber(current, 'cached_input_tokens') - usageNumber(previous, 'cached_input_tokens'),
    output: usageNumber(current, 'output_tokens') - usageNumber(previous, 'output_tokens'),
    thought: usageNumber(current, 'reasoning_output_tokens') - usageNumber(previous, 'reasoning_output_tokens'),
    total: usageNumber(current, 'total_tokens') - usageNumber(previous, 'total_tokens'),
  };

  for (const key of Object.keys(delta)) {
    if (delta[key] < 0) delta[key] = 0;
  }
  return delta;
}

function codexReport() {
  const { todayStartMs, weekCutoffMs } = todayInfo();
  const files = codexSessionFiles();
  const modelByThread = codexModelMap();
  const totals = emptyTotals('codex');

  for (const { file } of files) {
    const threadId = codexThreadId(file);
    const model = modelByThread.get(threadId) ?? 'codex';
    let previousUsage = null;
    let content;
    try {
      content = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }

    for (const line of content.split(/\r?\n/)) {
      if (!line.trim()) continue;
      let rec;
      try {
        rec = JSON.parse(line);
      } catch {
        continue;
      }

      if (rec.type !== 'event_msg' || rec.payload?.type !== 'token_count') continue;
      const ts = Date.parse(rec.timestamp ?? '');
      const currentUsage = rec.payload?.info?.total_token_usage;
      if (!currentUsage || !Number.isFinite(ts)) {
        previousUsage = currentUsage ?? previousUsage;
        continue;
      }

      const delta = usageDelta(currentUsage, previousUsage);
      previousUsage = currentUsage;
      const isToday = ts >= todayStartMs;
      const isWeek = ts >= weekCutoffMs;

      if (isToday && delta.total > 0) {
        totals.callsToday++;
        totals.todaySessions.add(threadId);
        addSessionEvent(totals, threadId, ts);
        totals.todayTotal += delta.total;
        totals.todayInput += delta.input;
        totals.todayOutput += delta.output;
        totals.todayCached += delta.cached;
        totals.todayThought += delta.thought;
        totals.modelTotals.set(model, (totals.modelTotals.get(model) ?? 0) + delta.total);
      }

      if (isWeek) {
        totals.weekTotal += delta.total;
      }

      const last = rec.payload?.info?.last_token_usage;
      setLatest(totals, {
        ts,
        input: usageNumber(last, 'input_tokens') || delta.input,
        total: usageNumber(last, 'total_tokens') || delta.total,
        model,
      });
    }
  }

  return totals;
}

function finalizeReport(totals, sourceNames) {
  const now = Date.now();
  const latestTs = totals.latest?.ts ?? now;
  const models = topModels(totals.modelTotals, totals.todayTotal);
  return {
    todayTotal: totals.todayTotal,
    ctxPct: 0,
    callsToday: totals.callsToday,
    errorsToday: totals.errorsToday,
    sessionsToday: totals.todaySessions.size,
    cacheRate: totals.todayInput > 0 ? clampPct((totals.todayCached / totals.todayInput) * 100) : 0,
    activeMinutes: activeMinutes(totals.sessionEvents),
    currentTokens: totals.latest?.input ?? 0,
    lastCallTokens: totals.latest?.total ?? 0,
    todayInput: totals.todayInput,
    todayOutput: totals.todayOutput,
    todayCached: totals.todayCached,
    todayThought: totals.todayThought,
    model: totals.latest?.model ?? sourceNames[0] ?? 'tokens',
    models,
    updatedAt: formatStamp(now),
    ageSec: Math.max(0, Math.round((now - latestTs) / 1000)),
    weekTotal: totals.weekTotal,
    sources: sourceNames,
  };
}

function buildReport() {
  const aggregate = emptyTotals('aggregate');
  const usedSources = [];
  const readers = {
    qwen: qwenReport,
    codex: codexReport,
  };

  for (const name of DATA_SOURCE_NAMES) {
    const reader = readers[name];
    if (!reader) {
      console.error(`[source] unknown datasource: ${name}`);
      continue;
    }
    try {
      const report = reader();
      mergeTotals(aggregate, report);
      usedSources.push(name);
    } catch (err) {
      console.error(`[source] ${name} failed:`, err.message);
    }
  }

  return finalizeReport(aggregate, usedSources);
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

function writeStatusFile(report) {
  const status = {
    ...report,
    bleConnected,
    bleDevice,
    timestamp: Date.now(),
  };
  try {
    fs.writeFileSync(STATUS_FILE, JSON.stringify(status));
  } catch (err) {
    console.error('[status] write failed:', err.message);
  }
}

async function pushTick() {
  const report = buildReport();
  writeStatusFile(report);
  if (!dataChar) return;
  const payload = toPayload(report);
  await new Promise((resolve, reject) => {
    dataChar.write(Buffer.from(payload), false, (err) => err ? reject(err) : resolve());
  });
  console.log(`[ble] wrote ${payload}`);
}

function startPushLoop() {
  clearInterval(pushTimer);
  pushTick().catch((err) => console.error('[ble] write failed:', err.message));
  pushTimer = setInterval(() => {
    pushTick().catch((err) => console.error('[ble] write failed:', err.message));
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
    bleDevice = peripheral.advertisement.localName ?? 'unknown';
    peripheral.once('disconnect', () => {
      console.log('[ble] disconnected');
      dataChar = null;
      connectedPeripheral = null;
      bleConnected = false;
      startScan();
    });
    try {
      dataChar = await discoverCharacteristic(peripheral);
      bleConnected = true;
      console.log('[ble] ready');
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
  console.log(`[ble] scanning for ${[...BLE_DEVICE_NAMES].join(' or ')}`);
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
  if (!BLE_DEVICE_NAMES.has(name)) return;
  connect(peripheral);
});

startPushLoop();

function shutdown() {
  clearInterval(pushTimer);
  clearInterval(scanTimer);
  if (connectedPeripheral) {
    connectedPeripheral.disconnect(() => {
      noble.stopScanning();
      process.exit(0);
    });
    setTimeout(() => process.exit(0), 2000);
  } else {
    process.exit(0);
  }
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
