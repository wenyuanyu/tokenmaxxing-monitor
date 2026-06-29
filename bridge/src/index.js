import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import noble from '@abandonware/noble';
import {
  ACTIVITY_DAYS,
  DAY_MS,
  activeMinutes,
  addDailyTokens,
  addSessionEvent,
  addTaskEvent,
  clampPct,
  emptyTotals,
  encodeActivityColumn,
  formatStamp,
  localDateKey,
  longestTaskMinutes,
  mergeTotals,
  modelName,
  peakDailyTotal,
  safeModelName,
  setLatest,
  streakDays,
  timezoneOffsetMinutes,
  topModels,
  usageDelta,
  usageNumber,
} from './lib.js';

const BLE_DEVICE_NAMES = new Set(
  (process.env.QWEN_BLE_DEVICE_NAME || 'QwenToken,Qwen Usage')
    .split(',')
    .map(s => s.trim())
    .filter(Boolean)
);
const SERVICE_UUID = '00112233445566778899aabbccddeeff';
const DATA_CHAR_UUID = '00112233445566778899aabbccddee01';
const INTERVAL_MS = Number(process.env.QWEN_BLE_PUSH_MS ?? 5000);
const HEARTBEAT_MS = Number(process.env.QWEN_BLE_HEARTBEAT_MS ?? 60000);
const RECENT_DAYS = Number(process.env.QWEN_BLE_SCAN_DAYS ?? 7);
const HISTORY_DAYS = Number(process.env.TOKEN_MONITOR_HISTORY_DAYS ?? 3650);
const STATUS_FILE = '/tmp/qwen-token-status.json';
const WRITE_TIMEOUT_MS = Number(process.env.QWEN_BLE_WRITE_TIMEOUT_MS ?? 3000);
const STALE_WRITE_MS = Number(process.env.QWEN_BLE_STALE_WRITE_MS ?? Math.max(HEARTBEAT_MS * 2, 10000));
const WAKE_GAP_MS = Number(process.env.QWEN_BLE_WAKE_GAP_MS ?? 15000);
const CONNECT_TIMEOUT_RESTARTS = Number(process.env.QWEN_BLE_CONNECT_TIMEOUT_RESTARTS ?? 3);
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
let pushInFlight = false;
let lastTickAt = 0;
let lastSuccessfulWriteAt = 0;
let lastStablePayloadKey = '';
let consecutiveConnectTimeouts = 0;

function resetBleConnection(reason) {
  if (reason) console.error(`[ble] resetting connection: ${reason}`);
  dataChar = null;
  bleConnected = false;
  bleDevice = '';
  connecting = false;
  lastSuccessfulWriteAt = 0;
  consecutiveConnectTimeouts = 0;
  const peripheral = connectedPeripheral;
  connectedPeripheral = null;
  if (peripheral) {
    try { peripheral.disconnect(); } catch {}
  }
  startScan();
}

function restartAfterConnectTimeout() {
  if (CONNECT_TIMEOUT_RESTARTS <= 0) return false;
  consecutiveConnectTimeouts += 1;
  if (consecutiveConnectTimeouts < CONNECT_TIMEOUT_RESTARTS) return false;

  console.error(`[ble] ${consecutiveConnectTimeouts} consecutive connect timeouts, exiting for launchd restart`);
  try { noble.stopScanning(); } catch {}
  setTimeout(() => process.exit(75), 250);
  return true;
}

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
  const cutoff = Date.now() - (Math.max(RECENT_DAYS, ACTIVITY_DAYS, HISTORY_DAYS) + 1) * 24 * 60 * 60 * 1000;
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

function claudeProjectsDir() {
  const home = process.env.CLAUDE_HOME ?? path.join(os.homedir(), '.claude');
  return process.env.CLAUDE_PROJECTS_DIR ?? path.join(home, 'projects');
}

function claudeSessionFiles() {
  const root = claudeProjectsDir();
  const cutoff = Date.now() - (Math.max(RECENT_DAYS, ACTIVITY_DAYS, HISTORY_DAYS) + 1) * 24 * 60 * 60 * 1000;
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
      if (!ent.isFile() || !ent.name.endsWith('.jsonl')) continue;
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

function codexSessionFiles() {
  const root = process.env.CODEX_SESSIONS_DIR ?? path.join(codexHome(), 'sessions');
  const cutoff = Date.now() - (Math.max(RECENT_DAYS, ACTIVITY_DAYS, HISTORY_DAYS) + 1) * 24 * 60 * 60 * 1000;
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
    activityCutoffMs: today.getTime() - (ACTIVITY_DAYS - 1) * 24 * 60 * 60 * 1000,
  };
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
      const totalTokens = rec.totalTokens ?? 0;
      if (Number.isFinite(ts) && totalTokens > 0) {
        const key = String(rec.sessionId ?? file);
        addTaskEvent(totals, key, ts);
        addDailyTokens(totals, rec.localDate || localDateKey(ts), totalTokens);
      }

      if (isToday) {
        totals.callsToday++;
        const key = String(rec.sessionId ?? '');
        if (rec.sessionId) totals.todaySessions.add(rec.sessionId);
        addSessionEvent(totals, key, ts);

        totals.todayTotal += totalTokens;
        totals.todayInput += rec.inputTokens ?? 0;
        totals.todayOutput += rec.outputTokens ?? 0;
        totals.todayCached += rec.cachedTokens ?? 0;
        totals.todayThought += rec.thoughtsTokens ?? 0;

        const model = modelName(rec);
        totals.modelTotals.set(model, (totals.modelTotals.get(model) ?? 0) + totalTokens);
      }

      if (isWeek) {
        totals.weekTotal += totalTokens;
      }

      setLatest(totals, {
        ts,
        input: rec.inputTokens ?? 0,
        total: totalTokens,
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
      if (delta.total > 0) {
        addTaskEvent(totals, threadId, ts);
        addDailyTokens(totals, localDateKey(ts), delta.total);
      }

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

function claudeSessionIdFromFile(file) {
  return path.basename(file).match(/([0-9a-fA-F-]{36})\.jsonl$/)?.[1] ?? file;
}

function claudeReport() {
  const { todayStartMs, weekCutoffMs } = todayInfo();
  const files = claudeSessionFiles();
  const totals = emptyTotals('claude');

  for (const { file } of files) {
    const fileSessionId = claudeSessionIdFromFile(file);
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

      if (rec.type !== 'assistant') continue;
      const msg = rec.message;
      if (!msg || msg.role !== 'assistant') continue;
      const usage = msg.usage;
      if (!usage) continue;

      const rawModel = msg.model ?? '';
      if (rawModel === '<synthetic>') continue;
      const model = safeModelName(rawModel, 'claude');

      const input = usageNumber(usage, 'input_tokens');
      const cacheCreate = usageNumber(usage, 'cache_creation_input_tokens');
      const cacheRead = usageNumber(usage, 'cache_read_input_tokens');
      const output = usageNumber(usage, 'output_tokens');
      const inputTotal = input + cacheCreate + cacheRead;
      const total = inputTotal + output;
      if (total <= 0) continue;

      const ts = Date.parse(rec.timestamp ?? '');
      if (!Number.isFinite(ts)) continue;
      const sessionId = String(rec.sessionId ?? fileSessionId);

      addTaskEvent(totals, sessionId, ts);
      addDailyTokens(totals, localDateKey(ts), total);

      const isToday = ts >= todayStartMs;
      const isWeek = ts >= weekCutoffMs;

      if (isToday) {
        totals.callsToday++;
        totals.todaySessions.add(sessionId);
        addSessionEvent(totals, sessionId, ts);
        totals.todayTotal += total;
        totals.todayInput += inputTotal;
        totals.todayOutput += output;
        totals.todayCached += cacheRead;
        totals.modelTotals.set(model, (totals.modelTotals.get(model) ?? 0) + total);
      }

      if (isWeek) {
        totals.weekTotal += total;
      }

      setLatest(totals, {
        ts,
        input: inputTotal,
        total,
        model,
      });
    }
  }

  return totals;
}

function activityPayload(dailyTotals) {
  const today = new Date();
  today.setHours(0, 0, 0, 0);
  const todayWeekStart = today.getTime() - today.getDay() * DAY_MS;
  const cells = Array.from({ length: 26 }, () => Array(7).fill(0));

  for (let age = 0; age < ACTIVITY_DAYS; age++) {
    const d = new Date(today.getTime() - age * 24 * 60 * 60 * 1000);
    const row = d.getDay();
    const weekStart = d.getTime() - row * DAY_MS;
    const weekAge = Math.round((todayWeekStart - weekStart) / (7 * DAY_MS));
    const col = 25 - weekAge;
    if (col < 0 || col >= 26) continue;
    cells[col][row] = dailyTotals.get(localDateKey(d.getTime())) ?? 0;
  }

  return cells.map(encodeActivityColumn).join('');
}

function finalizeReport(totals, sourceNames) {
  const now = Date.now();
  const latestTs = totals.latest?.ts ?? now;
  const models = topModels(totals.modelTotals, totals.todayTotal);
  const lifetimeTotal = [...totals.dailyTotals.values()].reduce((sum, n) => sum + n, 0);
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
    updatedAtUnix: Math.floor(now / 1000),
    timezoneOffsetMinutes: timezoneOffsetMinutes(now),
    ageSec: Math.max(0, Math.round((now - latestTs) / 1000)),
    weekTotal: totals.weekTotal,
    lifetimeTotal,
    peakDailyTotal: peakDailyTotal(totals.dailyTotals),
    streakDays: streakDays(totals.dailyTotals),
    longestTaskMinutes: longestTaskMinutes(totals.taskEvents),
    sources: sourceNames,
    activity: activityPayload(totals.dailyTotals),
  };
}

function buildReport() {
  const aggregate = emptyTotals('aggregate');
  const usedSources = [];
  const readers = {
    qwen: qwenReport,
    codex: codexReport,
    claude: claudeReport,
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
    r.updatedAtUnix,
    r.timezoneOffsetMinutes,
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
    r.activity,
    r.lifetimeTotal,
    r.peakDailyTotal,
    r.streakDays,
    r.longestTaskMinutes,
  ].join('|');
}

function stablePayloadKey(r) {
  return JSON.stringify({
    todayTotal: r.todayTotal,
    sessionsToday: r.sessionsToday,
    todayCached: r.todayCached,
    cacheRate: r.cacheRate,
    activeMinutes: r.activeMinutes,
    timezoneOffsetMinutes: r.timezoneOffsetMinutes,
    models: r.models,
    errorsToday: r.errorsToday,
    todayOutput: r.todayOutput,
    weekTotal: r.weekTotal,
    todayInput: r.todayInput,
    activity: r.activity,
    lifetimeTotal: r.lifetimeTotal,
    peakDailyTotal: r.peakDailyTotal,
    streakDays: r.streakDays,
    longestTaskMinutes: r.longestTaskMinutes,
  });
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
  if (pushInFlight) return;
  pushInFlight = true;
  const startedAt = Date.now();
  if (lastTickAt && startedAt - lastTickAt > WAKE_GAP_MS) {
    resetBleConnection(`timer gap ${startedAt - lastTickAt}ms`);
  }
  lastTickAt = startedAt;
  const report = buildReport();
  writeStatusFile(report);
  try {
    if (!dataChar) return;
    const stableKey = stablePayloadKey(report);
    const isHeartbeatDue = !lastSuccessfulWriteAt || startedAt - lastSuccessfulWriteAt >= HEARTBEAT_MS;
    if (stableKey === lastStablePayloadKey && !isHeartbeatDue) return;

    if (lastSuccessfulWriteAt && startedAt - lastSuccessfulWriteAt > STALE_WRITE_MS) {
      resetBleConnection(`stale write ${startedAt - lastSuccessfulWriteAt}ms`);
      return;
    }
    const payload = toPayload(report);
    await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('write timeout')), WRITE_TIMEOUT_MS);
      dataChar.write(Buffer.from(payload), false, (err) => {
        clearTimeout(timeout);
        err ? reject(err) : resolve();
      });
    });
    lastSuccessfulWriteAt = Date.now();
    lastStablePayloadKey = stableKey;
    console.log(`[ble] wrote ${payload}`);
  } catch (err) {
    resetBleConnection(err.message);
  } finally {
    pushInFlight = false;
  }
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
  clearTimeout(scanTimer);
  noble.stopScanning();
  console.log(`[ble] connecting ${peripheral.advertisement.localName ?? peripheral.id}`);

  const timeout = setTimeout(() => {
    console.error('[ble] connect timeout');
    try { peripheral.disconnect(); } catch {}
    connecting = false;
    if (restartAfterConnectTimeout()) return;
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
    consecutiveConnectTimeouts = 0;
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
      lastSuccessfulWriteAt = 0;
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
  if (noble.state !== 'poweredOn' && noble.state !== 'unknown') {
    console.error(`[ble] scan deferred, adapter state: ${noble.state}`);
    scanTimer = setTimeout(startScan, 5000);
    return;
  }
  console.log(`[ble] scanning for ${[...BLE_DEVICE_NAMES].join(' or ')} (adapter state: ${noble.state})`);
  try {
    noble.startScanning([], false);
  } catch (e) {
    console.error('[ble] scan start failed:', e.message);
    scanTimer = setTimeout(startScan, 5000);
    return;
  }
  scanTimer = setTimeout(() => {
    noble.stopScanning();
    startScan();
  }, 15000);
}

noble.on('stateChange', (state) => {
  console.log(`[ble] adapter state: ${state}`);
  if (state === 'poweredOn') startScan();
  else {
    dataChar = null;
    connectedPeripheral = null;
    bleConnected = false;
    connecting = false;
    lastSuccessfulWriteAt = 0;
    clearTimeout(scanTimer);
    noble.stopScanning();
  }
});

noble.on('discover', (peripheral) => {
  const name = peripheral.advertisement.localName;
  if (!BLE_DEVICE_NAMES.has(name)) return;
  connect(peripheral);
});

if (noble.state === 'poweredOn') {
  startScan();
} else {
  console.log(`[ble] waiting for adapter, state: ${noble.state}`);
  scanTimer = setTimeout(startScan, 1000);
}

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
process.on('SIGCONT', () => {
  console.error('[process] resumed, resetting BLE connection');
  resetBleConnection('process resume');
});
process.on('unhandledRejection', (err) => {
  console.error('[process] unhandled rejection:', err?.message ?? err);
  resetBleConnection('unhandled rejection');
});
