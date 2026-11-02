const assert = require('node:assert/strict');
const {spawn, spawnSync} = require('node:child_process');
const {mkdtempSync, existsSync, readFileSync, writeFileSync, rmSync} = require('node:fs');
const {tmpdir} = require('node:os');
const path = require('node:path');
const test = require('node:test');
const budget = path.resolve(__dirname, '../../.github/ci/budget.js');
// The suite itself runs inside format's budget. Each intentional timeout owns
// a fresh group; otherwise it would kill the real CI phase rather than a fixture.
const fixtureEnvironment = {...process.env};
delete fixtureEnvironment.CHIBI_CI_BUDGET_GROUP;
const run = (seconds, command = [], reserve) => spawnSync(process.execPath,
  [budget, ...(reserve === undefined ? [] : ['--reserve', String(reserve)]),
   String(Date.now() / 1000 + seconds), ...command], {encoding: 'utf8', timeout: 5000, env: fixtureEnvironment});
test('success and failure status are preserved', () => {
  assert.equal(run(2, [process.execPath, '-e', '']).status, 0);
  assert.equal(run(2, [process.execPath, '-e', 'process.exit(17)']).status, 17);
});
test('expired deadline never starts a command; invalid deadline rejects', () => {
  assert.equal(run(-1, ['/nonexistent']).status, 124);
  assert.equal(run(NaN).status, 2);
  assert.equal(run(2, ['/nonexistent']).status, 1);
});
test('action reservation is charged to the original phase', () => {
  assert.equal(run(1, [], 2).status, 124);
  assert.equal(run(2, [], 1).status, 0);
});
for (const nested of [false, true]) {
  test(`deadline kills real process group (nested=${nested})`, async () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'chibi-ci-budget-'));
    const marker = path.join(directory, 'must-not-exist');
    try {
      const command = [process.execPath, '-e',
        `setTimeout(() => require('node:fs').writeFileSync(${JSON.stringify(marker)}, ''), 1000)`];
      const started = Date.now();
      const result = nested
        ? run(3, [process.execPath, budget, String(Date.now() / 1000 + .2), ...command])
        : run(.2, command);
      assert.equal(result.status, nested ? 137 : 124);
      assert.ok(Date.now() - started < 900);
      await new Promise(resolve => setTimeout(resolve, 1100));
      assert.equal(existsSync(marker), false);
    } finally { rmSync(directory, {recursive: true, force: true}); }
  });
}

for (const signal of ['SIGTERM', 'SIGINT']) {
  test(`cancellation reaps the detached tool group (${signal})`, async () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'chibi-ci-cancel-'));
    const marker = path.join(directory, 'must-not-exist');
    const ready = path.join(directory, 'ready');
    const code = `const fs=require('node:fs'); fs.writeFileSync(${JSON.stringify(ready)}, ''); setTimeout(() => fs.writeFileSync(${JSON.stringify(marker)}, ''), 700)`;
    const child = spawn(process.execPath, [budget, String(Date.now() / 1000 + 3), process.execPath, '-e', code], {stdio: 'ignore', env: fixtureEnvironment});
    const closed = new Promise(resolve => child.once('close', (status, termination) => resolve({status, termination})));
    try {
      const deadline = Date.now() + 1500;
      while (!existsSync(ready) && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
      assert.equal(existsSync(ready), true, 'fixture started');
      child.kill(signal);
      const result = await closed;
      assert.equal(result.termination, null);
      assert.equal(result.status, signal === 'SIGTERM' ? 143 : 130);
      await new Promise(resolve => setTimeout(resolve, 800));
      assert.equal(existsSync(marker), false);
    } finally {
      child.kill('SIGTERM');
      await closed;
      rmSync(directory, {recursive: true, force: true});
    }
  });
}

// Real Chibi selectors/workers own process groups beneath the phase group.
// A fixture parent stays alive as they do, while one detached worker attempts
// a late write and keeps running. Cleanup may touch only these recorded PIDs.
for (const variant of ['deadline', 'nested-deadline', 'SIGTERM', 'SIGINT']) {
  test(`teardown kills separately grouped descendants (${variant})`, async () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'chibi-ci-descendant-'));
    const state = path.join(directory, 'parent.json');
    const ready = path.join(directory, 'ready.json');
    const marker = path.join(directory, 'must-not-exist');
    const controlMarker = path.join(directory, 'unrelated-must-exist');
    const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
    const running = pid => {
      try { process.kill(pid, 0); return true; }
      catch (error) { if (error.code === 'ESRCH') return false; throw error; }
    };
    const exactPids = new Set();
    const worker = `const fs=require('node:fs'); fs.writeFileSync(${JSON.stringify(ready)}, JSON.stringify({at:Date.now(),pid:process.pid})); setTimeout(() => fs.writeFileSync(${JSON.stringify(marker)}, ''), 1600); setInterval(() => {}, 1000)`;
    const parent = `const fs=require('node:fs'); const child=require('node:child_process').spawn(process.execPath, ['-e', ${JSON.stringify(worker)}], {detached:true,stdio:'ignore'}); fs.writeFileSync(${JSON.stringify(state)}, JSON.stringify({pid:process.pid,ppid:process.ppid,worker:child.pid})); setInterval(() => {}, 1000)`;
    let command = [process.execPath, '-e', parent];
    const deadline = String(Date.now() / 1000 + 1);
    if (variant === 'nested-deadline') command = [process.execPath, budget, deadline, ...command];
    const child = spawn(process.execPath, [budget, variant === 'deadline' ? deadline : String(Date.now() / 1000 + 5), ...command], {
      stdio: 'ignore', env: fixtureEnvironment,
    });
    exactPids.add(child.pid);
    const closed = new Promise(resolve => child.once('close', (status, signal) => resolve({status, signal})));
    let control;
    try {
      if (variant === 'SIGTERM') {
        // A process outside the owned ancestry must survive the same teardown.
        control = spawn(process.execPath, ['-e', `setTimeout(() => require('node:fs').writeFileSync(${JSON.stringify(controlMarker)}, ''), 400); setInterval(() => {}, 1000)`], {
          detached: true, stdio: 'ignore', env: fixtureEnvironment,
        });
        exactPids.add(control.pid);
      }
      const startDeadline = Date.now() + 2000;
      while (!existsSync(ready) && Date.now() < startDeadline) await delay(10);
      assert.equal(existsSync(ready), true, 'detached worker started');
      const fixture = JSON.parse(readFileSync(state, 'utf8'));
      const began = JSON.parse(readFileSync(ready, 'utf8')).at;
      for (const pid of [fixture.pid, fixture.ppid, fixture.worker]) exactPids.add(pid);
      if (variant.startsWith('SIG')) child.kill(variant);
      const result = await closed;
      assert.deepEqual(result, {status: {'deadline': 124, 'nested-deadline': 137, SIGTERM: 143, SIGINT: 130}[variant], signal: null});
      // Let an escaped worker reach the write, rather than hiding a leak with
      // test cleanup. Then independently check that no fixture worker remains.
      await delay(Math.max(0, began + 1800 - Date.now()));
      assert.deepEqual({lateWrite: existsSync(marker), parentRunning: running(fixture.pid), workerRunning: running(fixture.worker)},
        {lateWrite: false, parentRunning: false, workerRunning: false});
      if (control) assert.deepEqual({running: running(control.pid), wrote: existsSync(controlMarker)}, {running: true, wrote: true});
    } finally {
      // Read any startup record even if an assertion failed before readiness.
      if (existsSync(state)) {
        const fixture = JSON.parse(readFileSync(state, 'utf8'));
        for (const pid of [fixture.pid, fixture.ppid, fixture.worker]) exactPids.add(pid);
      }
      for (const pid of [...exactPids].reverse()) {
        try { process.kill(pid, 'SIGKILL'); }
        catch (error) { if (error.code !== 'ESRCH') throw error; }
      }
      await closed;
      rmSync(directory, {recursive: true, force: true});
    }
  });
}

for (const failure of ['descendant', 'group']) {
  test(`cleanup attempts every owned kill and preserves cancellation after a ${failure} error`, async t => {
    const directory = mkdtempSync(path.join(tmpdir(), 'chibi-ci-kill-error-'));
    const state = path.join(directory, 'state.json');
    const attempts = path.join(directory, 'attempts.jsonl');
    const preload = path.join(directory, 'inject.js');
    const ready = [0, 1].map(index => path.join(directory, `ready-${index}`));
    const markers = [0, 1].map(index => path.join(directory, `marker-${index}`));
    const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
    const running = pid => {
      try { process.kill(pid, 0); return true; }
      catch (error) { if (error.code === 'ESRCH') return false; throw error; }
    };
    // The only injected failure targets a PID/group from this real fixture's
    // startup record. Other signals reach the OS unchanged; no credentials change.
    writeFileSync(preload, `const fs=require('node:fs');
const kill=process.kill.bind(process); let injected=false;
process.kill=(pid,signal)=>{
  if(signal==='SIGKILL') {
    const state=JSON.parse(fs.readFileSync(${JSON.stringify(state)}, 'utf8'));
    const owned=${JSON.stringify(failure)}==='descendant' ? state.workers.includes(pid) : pid===-state.pid;
    const fail=owned&&!injected;
    fs.appendFileSync(${JSON.stringify(attempts)}, JSON.stringify({pid,signal,injected:fail})+'\\n');
    if(fail) { injected=true; const error=new Error('fixture-owned kill EPERM'); error.code='EPERM'; throw error; }
  }
  return kill(pid,signal);
};
`);
    const worker = index => `const fs=require('node:fs'); fs.writeFileSync(${JSON.stringify(ready[index])}, ''); setTimeout(() => fs.writeFileSync(${JSON.stringify(markers[index])}, ''), 500); setInterval(() => {}, 1000)`;
    const parent = `const fs=require('node:fs'); const spawn=require('node:child_process').spawn; const workers=${JSON.stringify([worker(0), worker(1)])}.map(code=>spawn(process.execPath,['-e',code],{detached:true,stdio:'ignore'}).pid); fs.writeFileSync(${JSON.stringify(state)},JSON.stringify({pid:process.pid,workers})); setInterval(() => {},1000)`;
    const child = spawn(process.execPath, ['--require', preload, budget, String(Date.now() / 1000 + 5), process.execPath, '-e', parent], {
      stdio: ['ignore', 'ignore', 'pipe'], env: fixtureEnvironment,
    });
    let stderr = '';
    child.stderr.on('data', data => { stderr += data; });
    // A frozen fixture parent keeps stderr open after the watchdog exits.
    // Observe exit first; close follows the exact-PID rescue in finally.
    const exited = new Promise(resolve => child.once('exit', (status, signal) => resolve({status, signal})));
    const closed = new Promise(resolve => child.once('close', resolve));
    const exactPids = new Set([child.pid]);
    let evidence = {failure};
    try {
      const deadline = Date.now() + 2000;
      while (!ready.every(file => existsSync(file)) && Date.now() < deadline) await delay(10);
      assert.equal(ready.every(file => existsSync(file)), true, 'both real detached workers started');
      const fixture = JSON.parse(readFileSync(state, 'utf8'));
      for (const pid of [fixture.pid, ...fixture.workers]) exactPids.add(pid);
      child.kill('SIGTERM');
      let exitTimer;
      let result;
      try {
        result = await Promise.race([exited, new Promise((_, reject) => {
          exitTimer = setTimeout(() => reject(new Error('watchdog did not exit after cleanup failure')), 2000);
        })]);
      } finally {
        clearTimeout(exitTimer);
      }
      evidence = {...evidence, fixture, watchdog: result};
      assert.deepEqual(result, {status: 143, signal: null});
      await delay(700);
      const calls = readFileSync(attempts, 'utf8').trim().split('\n').map(line => JSON.parse(line));
      const injected = calls.filter(call => call.injected);
      const snapshot = spawnSync('/bin/ps', ['-o', 'pid=,ppid=,pgid=,stat=', '-p', [fixture.pid, ...fixture.workers].join(',')], {encoding: 'utf8'}).stdout;
      // A frozen owner cannot reap its killed children. Zombies are dead;
      // the exact-PID rescue below must still remove every remaining PID.
      const survivors = snapshot.trim().split('\n').filter(Boolean).map(line => line.trim().split(/\s+/))
        .filter(fields => !fields[3].startsWith('Z')).map(fields => Number(fields[0]));
      evidence = {...evidence, calls, liveBeforeRescue: survivors, snapshot, stderr};
      assert.equal(injected.length, 1, 'one owned kill was denied');
      assert.deepEqual(calls.filter(call => call.pid > 0).map(call => call.pid).sort(), [...fixture.workers].sort(), 'every descendant kill was attempted');
      assert.equal(calls.at(-1).pid, -fixture.pid, 'the final original-group kill was attempted');
      assert.deepEqual(survivors, [failure === 'descendant' ? injected[0].pid : fixture.pid]);
      assert.deepEqual(markers.map(file => existsSync(file)), [false, false], 'denied processes remain stopped until rescue');
      assert.match(stderr, failure === 'descendant' ? /Cannot kill CI phase descendant.*fixture-owned kill EPERM/ : /Cannot kill CI phase process group.*fixture-owned kill EPERM/);
    } finally {
      if (existsSync(state)) {
        const fixture = JSON.parse(readFileSync(state, 'utf8'));
        for (const pid of [fixture.pid, ...fixture.workers]) exactPids.add(pid);
      }
      if (existsSync(attempts)) {
        evidence.calls = readFileSync(attempts, 'utf8').trim().split('\n').map(line => JSON.parse(line));
      }
      evidence.pidsBeforeRescue = [...exactPids].filter(running);
      evidence.snapshotBeforeRescue = spawnSync('/bin/ps', ['-o', 'pid=,ppid=,pgid=,stat=', '-p', [...exactPids].join(',')], {encoding: 'utf8'}).stdout;
      const cleanup = [];
      for (const pid of [...exactPids].reverse()) {
        try { process.kill(pid, 'SIGKILL'); cleanup.push({pid, killed: true}); }
        catch (error) { if (error.code !== 'ESRCH') throw error; else cleanup.push({pid, alreadyExited: true}); }
      }
      await closed;
      const deadline = Date.now() + 1000;
      while ([...exactPids].some(running) && Date.now() < deadline) await delay(10);
      const survivorsAfterRescue = [...exactPids].filter(running);
      t.diagnostic(JSON.stringify({...evidence, stderr, cleanup, survivorsAfterRescue}));
      rmSync(directory, {recursive: true, force: true});
      assert.deepEqual(survivorsAfterRescue, [], 'all exact fixture PIDs were reaped after rescue');
    }
  });
}
