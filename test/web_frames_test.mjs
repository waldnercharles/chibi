import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const csv = [
  'time_ms,interval_ms,steps,dropped_ms,behavior_ms,physics_ms,systems_ms,rendering_ms',
  '0,16.5,1,0,4,2,1,0.5',
  '32,32,2,0,8,4,1,0.5',
  '132,100,8,116.667,40,25,2,1',
  '148,16,0,0,1,0,0,0.5',
].join('\n') + '\n';
let freed = 0;
const Module = {
  _chibi_frame_log_csv: () => 1,
  UTF8ToString: () => csv,
  _free: () => freed++,
};
vm.runInNewContext(readFileSync(new URL('../tools/web_frames.js', import.meta.url), 'utf8'), {
  Module,
  window: {},
  document: {},
  URL: {},
  Blob,
  setTimeout,
});

const frames = Module.chibiFrames();
assert.equal(frames.length, 4);
assert.equal(frames[2].steps, 8);
assert.equal(freed, 1, 'CSV memory is released after reading');

const summary = Module.chibiFrameSummary();
assert.equal(summary.frames, 4);
assert.equal(freed, 2, 'the default summary reads and frees the CSV log');
assert.equal(summary.interval_ms.p95, 100);
assert.equal(summary.steps.max, 8);
assert.equal(summary.dropped_ms.max, 116.667);
assert.equal(summary.catch_up_frames, 2);
assert.equal(summary.dropped_frames, 1);
assert.equal(summary.dropped_ms_total, 116.667);
assert.deepEqual(JSON.parse(JSON.stringify(summary.step_counts)), { 0: 1, 1: 1, 2: 1, 8: 1 });

const empty = Module.chibiFrameSummary([]);
assert.equal(empty.interval_ms.p50, null);
assert.deepEqual(JSON.parse(JSON.stringify(empty.step_counts)), {});
console.log('PASS: browser frame summaries report correlated catch-up and dropped-time evidence');
