#!/usr/bin/env node
// Run a phase within its original wall-clock budget, including earlier setup.
const {spawn} = require('node:child_process');
const {constants} = require('node:os');
let args = process.argv.slice(2);
let reserve = 0;
if (args[0] === '--reserve') {
  reserve = Number(args[1]);
  args = args.slice(2);
}
const remaining = Number(args[0]) * 1000 - Date.now();
if (!Number.isFinite(remaining) || !Number.isFinite(reserve) || reserve < 0) {
  console.error('Invalid CI phase deadline');
  process.exit(2);
}
if (remaining <= reserve * 1000) {
  console.error('CI phase deadline expired or cannot fit the reserved action');
  process.exit(124);
}
if (args.length === 1) process.exit(0);
// Nested caps share the outer tool group. A shorter cap kills the whole phase;
// it must not orphan detached grandchildren when the outer deadline expires.
const nested = process.env.CHIBI_CI_BUDGET_GROUP === '1';
let child;
let timer;
let expired = false;
let failedToStart = false;
let cancelled = 0;
function stopGroup() {
  if (!child?.pid) return;
  try { process.kill(nested ? 0 : -child.pid, 'SIGKILL'); }
  catch (error) { if (error.code !== 'ESRCH') throw error; }
}
// Install cancellation ownership before starting any child. A manual Ctrl-C
// must not detach the work from its watchdog; nested caps own the shared group.
for (const signal of ['SIGTERM', 'SIGINT']) {
  process.on(signal, () => {
    cancelled = 128 + constants.signals[signal];
    clearTimeout(timer);
    stopGroup();
  });
}
child = spawn(args[1], args.slice(2), {
  detached: !nested,
  stdio: 'inherit',
  env: {...process.env, CHIBI_CI_BUDGET_GROUP: '1'},
});
timer = setTimeout(() => {
  expired = true;
  console.error('CI phase deadline expired');
  stopGroup();
}, remaining);
child.on('error', error => {
  failedToStart = true;
  clearTimeout(timer);
  console.error(error.message);
  process.exitCode = 1;
});
child.on('close', (status, signal) => {
  clearTimeout(timer);
  process.exitCode = cancelled || (failedToStart ? 1 : (expired ? 124 : (status ?? 128 + constants.signals[signal])));
});
