#!/usr/bin/env node
// Run a phase within its original wall-clock budget, including earlier setup.
const {spawn, spawnSync} = require('node:child_process');
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
let stopping = false;
function stopGroup() {
  if (!child?.pid || stopping) return;
  stopping = true;
  const frozen = new Map();
  const cleanupDeadline = Date.now() + 1000;
  let group = nested ? undefined : child.pid;
  try {
    for (;;) {
      // os.Start gives selectors and compiler workers separate process groups.
      // Discover them only during teardown; successful phases do not run ps.
      const remaining = cleanupDeadline - Date.now();
      if (remaining <= 0) throw new Error('process teardown exceeded one second');
      const snapshot = spawnSync('/bin/ps', ['-A', '-o', 'pid=,ppid=,pgid='], {
        encoding: 'utf8', timeout: remaining, maxBuffer: 16 * 1024 * 1024,
      });
      if (snapshot.error) throw snapshot.error;
      if (snapshot.status !== 0) throw new Error(`ps exited ${snapshot.status}: ${snapshot.stderr.trim()}`);
      const rows = snapshot.stdout.trim().split('\n').map(line => {
        const fields = /^\s*(\d+)\s+(\d+)\s+(\d+)\s*$/.exec(line);
        if (!fields) throw new Error('ps returned an invalid process row');
        return {pid: Number(fields[1]), parent: Number(fields[2]), group: Number(fields[3])};
      }).filter(row => row.pid !== snapshot.pid);
      for (const row of rows) {
        if (frozen.has(row.pid)) frozen.set(row.pid, row.group);
      }
      if (group === undefined) {
        group = rows.find(row => row.pid === process.pid)?.group;
        if (group === undefined) throw new Error('ps did not report this watchdog');
      }
      const children = new Map();
      for (const row of rows) {
        if (!children.has(row.parent)) children.set(row.parent, []);
        children.get(row.parent).push(row);
      }
      const owned = new Set();
      const queue = rows.filter(row => row.group === group || frozen.has(row.pid));
      for (let index = 0; index < queue.length; index++) {
        const row = queue[index];
        if (owned.has(row.pid)) continue;
        owned.add(row.pid);
        for (const descendant of children.get(row.pid) || []) {
          if (!owned.has(descendant.pid)) queue.push(descendant);
        }
      }
      // Freeze ancestors first, then repeat the snapshot to include children
      // created before their parent stopped. The nested watchdog stays awake
      // until it has killed the workers outside its own group.
      const ordered = rows.filter(row => owned.has(row.pid) && !owned.has(row.parent));
      let added = false;
      for (let index = 0; index < ordered.length; index++) {
        const row = ordered[index];
        for (const descendant of children.get(row.pid) || []) {
          if (owned.has(descendant.pid)) ordered.push(descendant);
        }
        if (row.pid === process.pid || frozen.has(row.pid)) continue;
        try {
          process.kill(row.pid, 'SIGSTOP');
          frozen.set(row.pid, row.group);
          added = true;
        } catch (error) { if (error.code !== 'ESRCH') throw error; }
      }
      if (!added) break;
    }
  } catch (error) {
    console.error(`Cannot enumerate CI phase descendants: ${error.message}`);
  }
  // Descendants stop before their owners. Killing the original group first
  // would orphan them, and would also kill a nested watchdog doing cleanup.
  try {
    for (const [pid, processGroup] of [...frozen].reverse()) {
      // Keep the original group alive until its one final group kill. Darwin
      // can report EPERM when a second kill races that group's last exit.
      if (processGroup === group) continue;
      try { process.kill(pid, 'SIGKILL'); }
      catch (error) {
        if (error.code !== 'ESRCH') console.error(`Cannot kill CI phase descendant ${pid}: ${error.message}`);
      }
    }
  } finally {
    try { process.kill(nested ? 0 : -child.pid, 'SIGKILL'); }
    catch (error) {
      if (error.code !== 'ESRCH') {
        console.error(`Cannot kill CI phase process group: ${error.message}`);
        // An inaccessible group cannot be reaped here. Report it and retain
        // the requested failure status instead of waiting forever on it.
        process.exitCode = nested ? 137 : (cancelled || (expired ? 124 : 1));
        child.unref();
      }
    }
  }
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
