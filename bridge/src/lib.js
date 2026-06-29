export const DAY_MS = 24 * 60 * 60 * 1000;
export const ACTIVITY_DAYS = 26 * 7;
export const ACTIVE_GAP_MS = 5 * 60 * 1000;

export function clampPct(n) {
  return Math.max(0, Math.min(100, Math.round(n)));
}

export function formatStamp(ms) {
  const d = new Date(ms);
  const mm = String(d.getMonth() + 1).padStart(2, '0');
  const dd = String(d.getDate()).padStart(2, '0');
  const hh = String(d.getHours()).padStart(2, '0');
  const mi = String(d.getMinutes()).padStart(2, '0');
  const ss = String(d.getSeconds()).padStart(2, '0');
  return `${mm}-${dd} ${hh}:${mi}:${ss}`;
}

export function timezoneOffsetMinutes(ms = Date.now()) {
  return -new Date(ms).getTimezoneOffset();
}

export function localDateKey(ms) {
  const d = new Date(ms);
  const yyyy = d.getFullYear();
  const mm = String(d.getMonth() + 1).padStart(2, '0');
  const dd = String(d.getDate()).padStart(2, '0');
  return `${yyyy}-${mm}-${dd}`;
}

export function modelName(rec) {
  return String(rec.model ?? 'qwen').replace(/[|\r\n]/g, ' ').trim().slice(0, 23) || 'qwen';
}

export function safeModelName(model, fallback = 'codex') {
  return String(model ?? fallback).replace(/[|\r\n]/g, ' ').trim().slice(0, 23) || fallback;
}

export function usageNumber(obj, key) {
  const n = Number(obj?.[key] ?? 0);
  return Number.isFinite(n) ? n : 0;
}

export function usageDelta(current, previous) {
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

export function activityLevel(tokens) {
  if (tokens === 0) return 0;
  if (tokens < 10000000) return 1;
  if (tokens < 50000000) return 2;
  if (tokens < 100000000) return 3;
  return 4;
}

export function encodeActivityColumn(levels) {
  let packed = 0;
  let mul = 1;
  for (const level of levels) {
    packed += activityLevel(level) * mul;
    mul *= 5;
  }
  return packed.toString(36).padStart(4, '0');
}

export function emptyTotals(source = 'aggregate') {
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
    taskEvents: new Map(),
    modelTotals: new Map(),
    dailyTotals: new Map(),
    latest: null,
  };
}

export function addDailyTokens(totals, dateKey, tokens) {
  const n = Number(tokens ?? 0);
  if (!dateKey || !Number.isFinite(n) || n <= 0) return;
  totals.dailyTotals.set(dateKey, (totals.dailyTotals.get(dateKey) ?? 0) + n);
}

export function addSessionEvent(totals, key, ts) {
  if (!Number.isFinite(ts)) return;
  const events = totals.sessionEvents.get(key) ?? [];
  events.push(ts);
  totals.sessionEvents.set(key, events);
}

export function addTaskEvent(totals, key, ts) {
  if (!Number.isFinite(ts)) return;
  const events = totals.taskEvents.get(key) ?? [];
  events.push(ts);
  totals.taskEvents.set(key, events);
}

export function setLatest(totals, latest) {
  if (!Number.isFinite(latest?.ts)) return;
  if (!totals.latest || latest.ts > totals.latest.ts) totals.latest = latest;
}

export function mergeTotals(target, source) {
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
  for (const [session, events] of source.taskEvents) {
    const key = `${source.source}:${session}`;
    const existing = target.taskEvents.get(key) ?? [];
    existing.push(...events);
    target.taskEvents.set(key, existing);
  }
  for (const [model, total] of source.modelTotals) {
    target.modelTotals.set(model, (target.modelTotals.get(model) ?? 0) + total);
  }
  for (const [dateKey, total] of source.dailyTotals) {
    addDailyTokens(target, dateKey, total);
  }
  setLatest(target, source.latest);
}

export function activeMinutes(sessionEvents) {
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

export function longestTaskMinutes(taskEvents) {
  let longestMs = 0;
  for (const events of taskEvents.values()) {
    events.sort((a, b) => a - b);
    let startedAt = null;
    let previous = null;
    for (const ts of events) {
      if (!Number.isFinite(ts)) continue;
      if (startedAt === null || previous === null || ts - previous > ACTIVE_GAP_MS) {
        startedAt = ts;
      } else {
        const span = ts - startedAt;
        if (span > longestMs) longestMs = span;
      }
      previous = ts;
    }
  }
  return Math.round(longestMs / 60000);
}

export function streakDays(dailyTotals, now = Date.now()) {
  const d = new Date(now);
  d.setHours(0, 0, 0, 0);
  let streak = 0;
  while ((dailyTotals.get(localDateKey(d.getTime())) ?? 0) > 0) {
    streak++;
    d.setDate(d.getDate() - 1);
  }
  return streak;
}

export function peakDailyTotal(dailyTotals) {
  let peak = 0;
  for (const total of dailyTotals.values()) {
    if (total > peak) peak = total;
  }
  return peak;
}

export function topModels(modelTotals, todayTotal) {
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
