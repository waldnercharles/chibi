const assert = require('node:assert/strict');
const {spawn, spawnSync} = require('node:child_process');
const {mkdtempSync, existsSync, rmSync} = require('node:fs');
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
