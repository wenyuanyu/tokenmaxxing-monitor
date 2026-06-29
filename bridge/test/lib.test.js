import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  activityLevel,
  addDailyTokens,
  addSessionEvent,
  addTaskEvent,
  activeMinutes,
  clampPct,
  emptyTotals,
  encodeActivityColumn,
  localDateKey,
  longestTaskMinutes,
  mergeTotals,
  modelName,
  peakDailyTotal,
  safeModelName,
  setLatest,
  streakDays,
  topModels,
  usageDelta,
  usageNumber,
} from '../src/lib.js';

test('activityLevel honours bucket boundaries', () => {
  assert.equal(activityLevel(0), 0);
  assert.equal(activityLevel(1), 1);
  assert.equal(activityLevel(9_999_999), 1);
  assert.equal(activityLevel(10_000_000), 2);
  assert.equal(activityLevel(49_999_999), 2);
  assert.equal(activityLevel(50_000_000), 3);
  assert.equal(activityLevel(99_999_999), 3);
  assert.equal(activityLevel(100_000_000), 4);
  assert.equal(activityLevel(10_000_000_000), 4);
});

test('encodeActivityColumn packs base-5 levels into 4 base36 chars', () => {
  assert.equal(encodeActivityColumn([0, 0, 0, 0, 0, 0, 0]), '0000');
  // Single level-4 at row 0 = 4 * 5^0 = 4
  assert.equal(encodeActivityColumn([100_000_000, 0, 0, 0, 0, 0, 0]), '0004');
  // Level-4 row 0 + level-3 row 1 = 4 + 3*5 = 19 = "j" in base36
  assert.equal(encodeActivityColumn([100_000_000, 50_000_000, 0, 0, 0, 0, 0]), '000j');
  // All level-4 = 4 * (1+5+25+...+5^6) = 4 * 19531 = 78124
  assert.equal(encodeActivityColumn(Array(7).fill(100_000_000)), (4 * (5 ** 7 - 1) / (5 - 1)).toString(36).padStart(4, '0'));
});

test('usageNumber coerces missing or non-numeric values to 0', () => {
  assert.equal(usageNumber({ a: 5 }, 'a'), 5);
  assert.equal(usageNumber({ a: '7' }, 'a'), 7);
  assert.equal(usageNumber({ a: 'oops' }, 'a'), 0);
  assert.equal(usageNumber({}, 'a'), 0);
  assert.equal(usageNumber(null, 'a'), 0);
  assert.equal(usageNumber(undefined, 'a'), 0);
});

test('usageDelta clamps negative diffs to zero', () => {
  const d = usageDelta(
    { input_tokens: 100, output_tokens: 20, total_tokens: 130 },
    { input_tokens: 80, output_tokens: 30, total_tokens: 120 },
  );
  assert.equal(d.input, 20);
  assert.equal(d.output, 0);
  assert.equal(d.total, 10);
});

test('usageDelta handles null previous (first call)', () => {
  const d = usageDelta({ input_tokens: 50, output_tokens: 10, total_tokens: 60 }, null);
  assert.equal(d.input, 50);
  assert.equal(d.output, 10);
  assert.equal(d.total, 60);
});

test('clampPct rounds and clamps to [0, 100]', () => {
  assert.equal(clampPct(50.6), 51);
  assert.equal(clampPct(50.4), 50);
  assert.equal(clampPct(-5), 0);
  assert.equal(clampPct(150), 100);
  assert.equal(clampPct(0), 0);
  assert.equal(clampPct(100), 100);
});

test('safeModelName falls back when empty or missing', () => {
  assert.equal(safeModelName(undefined), 'codex');
  assert.equal(safeModelName(null), 'codex');
  assert.equal(safeModelName(''), 'codex');
  assert.equal(safeModelName('', 'claude'), 'claude');
  assert.equal(safeModelName('  '), 'codex');
});

test('safeModelName strips pipes and newlines (would corrupt the BLE payload)', () => {
  assert.equal(safeModelName('foo|bar\nbaz'), 'foo bar baz');
});

test('safeModelName truncates to 23 chars', () => {
  assert.equal(safeModelName('a'.repeat(50)).length, 23);
});

test('modelName falls back to "qwen" when missing', () => {
  assert.equal(modelName({}), 'qwen');
  assert.equal(modelName({ model: 'qwen3-coder' }), 'qwen3-coder');
});

test('localDateKey produces stable YYYY-MM-DD in local time', () => {
  // Construct a date in local time so the assertion is timezone-agnostic.
  const d = new Date(2026, 0, 5, 12, 30); // Jan 5, 2026 local
  assert.equal(localDateKey(d.getTime()), '2026-01-05');
});

test('topModels returns top 3 with placeholder padding', () => {
  const rows = topModels(new Map([['a', 60], ['b', 30], ['c', 10]]), 100);
  assert.deepEqual(rows, [
    { model: 'a', pct: 60 },
    { model: 'b', pct: 30 },
    { model: 'c', pct: 10 },
  ]);
});

test('topModels pads to 3 entries with -- when fewer models seen', () => {
  const rows = topModels(new Map([['a', 60]]), 100);
  assert.equal(rows.length, 3);
  assert.equal(rows[0].model, 'a');
  assert.equal(rows[1].model, '--');
  assert.equal(rows[2].model, '--');
});

test('topModels returns 0 pct when todayTotal is 0', () => {
  const rows = topModels(new Map([['a', 0]]), 0);
  assert.equal(rows[0].pct, 0);
});

test('addDailyTokens skips zero / negative / NaN', () => {
  const t = emptyTotals();
  addDailyTokens(t, '2026-06-29', 100);
  addDailyTokens(t, '2026-06-29', 50);
  addDailyTokens(t, '2026-06-29', 0);
  addDailyTokens(t, '2026-06-29', -10);
  addDailyTokens(t, '', 5);
  addDailyTokens(t, '2026-06-29', NaN);
  assert.equal(t.dailyTotals.get('2026-06-29'), 150);
});

test('addSessionEvent / addTaskEvent ignore non-finite ts', () => {
  const t = emptyTotals();
  addSessionEvent(t, 's1', 1000);
  addSessionEvent(t, 's1', NaN);
  addTaskEvent(t, 's1', 2000);
  addTaskEvent(t, 's1', undefined);
  assert.deepEqual(t.sessionEvents.get('s1'), [1000]);
  assert.deepEqual(t.taskEvents.get('s1'), [2000]);
});

test('setLatest keeps the highest-ts entry', () => {
  const t = emptyTotals();
  setLatest(t, { ts: 1000, total: 1 });
  setLatest(t, { ts: 500, total: 2 });
  setLatest(t, { ts: 2000, total: 3 });
  setLatest(t, { ts: NaN, total: 99 });
  assert.equal(t.latest.ts, 2000);
  assert.equal(t.latest.total, 3);
});

test('activeMinutes sums per-session gaps, capped at ACTIVE_GAP_MS', () => {
  const events = new Map([
    // Within-gap deltas accumulate: 2 + 3 = 5 minutes.
    ['s1', [0, 2 * 60_000, 5 * 60_000]],
    // 1-hour gap is capped at the 5-minute window.
    ['s2', [0, 60 * 60_000]],
  ]);
  assert.equal(activeMinutes(events), 10);
});

test('longestTaskMinutes finds longest contiguous run per session', () => {
  const events = new Map([
    // Run #1: 0→3 min, then 1h gap breaks; Run #2: 60min→62min (2 min)
    // Longest is run #1 = 3 minutes.
    ['s1', [0, 3 * 60_000, 60 * 60_000, 62 * 60_000]],
  ]);
  assert.equal(longestTaskMinutes(events), 3);
});

test('peakDailyTotal returns the max daily value', () => {
  const totals = new Map([
    ['2026-06-27', 100],
    ['2026-06-28', 500],
    ['2026-06-29', 200],
  ]);
  assert.equal(peakDailyTotal(totals), 500);
  assert.equal(peakDailyTotal(new Map()), 0);
});

test('streakDays counts consecutive days back from "now"', () => {
  // Anchor "now" to Jan 5, 2026 12:00 local — the function uses local midnight.
  const now = new Date(2026, 0, 5, 12, 0).getTime();
  const dailyTotals = new Map([
    ['2026-01-05', 100],
    ['2026-01-04', 100],
    ['2026-01-03', 100],
    // 2026-01-02 missing — breaks the streak.
    ['2026-01-01', 100],
  ]);
  assert.equal(streakDays(dailyTotals, now), 3);
});

test('streakDays returns 0 when today has no usage', () => {
  const now = new Date(2026, 0, 5, 12, 0).getTime();
  assert.equal(streakDays(new Map([['2026-01-04', 100]]), now), 0);
});

test('mergeTotals namespaces session keys by source to avoid id collisions', () => {
  const target = emptyTotals('aggregate');
  const a = emptyTotals('qwen');
  const b = emptyTotals('claude');
  a.todaySessions.add('sess-1');
  b.todaySessions.add('sess-1'); // same id, different source — must not collide
  a.todayTotal = 100;
  b.todayTotal = 200;
  a.modelTotals.set('gpt-5', 100);
  b.modelTotals.set('claude-opus', 200);
  addSessionEvent(a, 'sess-1', 1000);
  addSessionEvent(b, 'sess-1', 2000);

  mergeTotals(target, a);
  mergeTotals(target, b);

  assert.equal(target.todayTotal, 300);
  assert.equal(target.todaySessions.size, 2, 'session ids namespaced by source');
  assert.ok(target.todaySessions.has('qwen:sess-1'));
  assert.ok(target.todaySessions.has('claude:sess-1'));
  assert.equal(target.modelTotals.get('gpt-5'), 100);
  assert.equal(target.modelTotals.get('claude-opus'), 200);
  assert.deepEqual(target.sessionEvents.get('qwen:sess-1'), [1000]);
  assert.deepEqual(target.sessionEvents.get('claude:sess-1'), [2000]);
});

test('mergeTotals sums daily totals across sources for the same date', () => {
  const target = emptyTotals('aggregate');
  const a = emptyTotals('qwen');
  const b = emptyTotals('claude');
  addDailyTokens(a, '2026-06-29', 1_000_000);
  addDailyTokens(b, '2026-06-29', 5_000_000);
  mergeTotals(target, a);
  mergeTotals(target, b);
  assert.equal(target.dailyTotals.get('2026-06-29'), 6_000_000);
});
