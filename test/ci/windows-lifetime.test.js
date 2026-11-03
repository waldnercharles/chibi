'use strict';
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');
const root = path.resolve(__dirname, '../..');
const read = name => readFileSync(path.join(root, name), 'utf8');
const workflow = read('.github/workflows/ci.yml');
const text = read('test/native_aot/managed_pointers/RunTextWindows.ps1');
const nested = read('test/native_aot/managed_pointers/RunNestedWindows.ps1');
const quoted = text => [...text.matchAll(/'([^']+)'/g)].map(match => match[1]);

test('Windows lifetime steps fail closed on the existing exact-head fixed point', () => {
  for (const [name, id, runner, evidence] of [
    ['nested-pointer collection and schema guards', 'windows-nested-pointer-proof', 'RunNestedWindows.ps1', 'chibi-nested-pointers-'],
    ['Text imports across image lifetimes', 'windows-text-import-proof', 'RunTextWindows.ps1', 'chibi-text-import-']]) {
    const step = workflow.split(`      - name: Check native Windows ${name}\n`)[1].split(/^      - /m)[0];
    const condition = /^        if: \$\{\{ (.+) \}\}$/m.exec(step)[1];
    for (const outcome of ['success', 'failure', 'skipped', 'cancelled']) {
      for (const cancelled of [false, true]) {
        assert.equal(vm.runInNewContext(condition, {cancelled: () => cancelled,
          steps: {'windows-fixed-point': {outcome}}}), !cancelled && outcome === 'success');
      }
    }
    assert.ok(step.includes(`id: ${id}`));
    assert.ok(step.includes(`/managed_pointers/${runner} -Compiler "$env:RUNNER_TEMP/stage4/chibi.exe"`));
    assert.ok(workflow.includes(`steps['${id}'].outcome == 'failure'`));
    assert.ok(workflow.includes(`path: \${{ runner.temp }}/${evidence}*/**`));
    assert.doesNotMatch(step, /continue-on-error|timeout-minutes/);
  }
});

test('Windows Text runner preserves the POSIX boundary inventory and all 68 cases', () => {
  const posix = read('test/native_aot/managed_pointers/text-import.sh');
  const common = /boundaries='([^']+)'/.exec(posix)[1].split(' ');
  const labels = /boundaries="\$boundaries ([^"]+)"/.exec(posix)[1].split(' ');
  assert.deepEqual(quoted(/\$boundaries = @\(([^\n]+)\)/.exec(text)[1]), common);
  assert.deepEqual(quoted(/\$boundaries \+= @\(([^\n]+)\)/.exec(text)[1]), labels);
  assert.equal((2 * common.length + labels.length) * 2 + 2 * 2 + 2 * 2 * 4 + 4 + 2 * 4, 68);
  for (const fixture of ['Labels', 'Leaf', 'Copies.chibi', 'Graphs.chibi', 'DAG', 'Deep']) assert.ok(text.includes(fixture));
  for (const budget of ['%bounded = icmp ule i64 %total, 64', '%within.budget = icmp ule i64 %allocated, 512', '%bounded = icmp ule i64 %total, 512']) {
    assert.ok(text.includes(budget)); assert.ok(posix.includes(budget));
  }
  assert.match(text, /one-site bypass O\$level" 33/);
  assert.ok(text.includes("$lines[0] -ne 'boundary Text owner missing before unload'"));
  assert.ok(text.includes('$passed -ne 68'));
  assert.ok(text.includes('$unexpected.Count -ne 0'));
  assert.doesNotMatch(text, /run\.sh|TaskSuite|continue-on-error/);
});

test('Windows nested runner keeps the narrow 24-case audit and both schema rejects', () => {
  const cases = [...nested.matchAll(/name = '([^']+)'; flags = @\(([^)]*)\); modules = @\(([^)]*)\); count = (\d+)/g)]
    .map(match => ({name: match[1], flags: quoted(match[2]), modules: quoted(match[3]), count: Number(match[4])}));
  assert.deepEqual(cases, [
    {name: 'normal', flags: [], modules: ['Cells', 'ScopedAliases', 'NestedPointers'], count: 12},
    {name: 'assertion', flags: ['--assertion'], modules: ['NestedPointerFailure'], count: 4},
    {name: 'callbacks', flags: ['--callbacks'], modules: ['NestedPointers'], count: 4},
    {name: 'callback-assertion', flags: ['--callbacks', '--assertion'], modules: ['NestedPointerFailure'], count: 4}
  ]);
  assert.equal(cases.reduce((sum, current) => sum + current.count, 0), 24);
  for (const name of ['PointerArraySchema', 'NestedPointerSchema']) {
    assert.ok(nested.includes(`'${name}'`));
    assert.match(read(`test/native_aot/managed_pointers/source/rejected/${name}.chibi`), /Keep ::/);
  }
  assert.ok(nested.includes("[IO.File]::ReadAllText($output) -ne 'preserved'"));
  assert.ok(nested.includes('--entry Main --emit-llvm'));
  assert.doesNotMatch(nested, /--entry Keep/);
  assert.ok(nested.includes('scalar proof requires number, bool, string, record or union parameters and one number, bool, string, record or union result'));
  assert.doesNotMatch(nested, /run\.sh|TaskSuite|continue-on-error/);
  assert.match(nested, /\n}\n[^\n]*\nexit 0\n$/);
  assert.doesNotMatch(nested.split('} finally {')[1].split('\n}')[0], /exit 0/);
});

test('Windows nested reload uses the real audited close and existing timeout', () => {
  const runner = read('test/native_aot/task_owners/RunWindows.ps1');
  assert.ok(runner.includes("Invoke-TypedGeneration 'nested' $level -Swap $true"));
  assert.ok(runner.includes("if ($Mode -eq 'nested') { $expected = 7 }"));
  assert.ok(runner.includes('"$run/old-done"'));
  assert.ok(runner.includes('if ($Swap) { $expectedClosed = 2 }'));
  assert.ok(runner.includes('closed := windows.CloseLibrary(handle);'));
  assert.ok(runner.includes('$deadline.ElapsedMilliseconds -ge 30000'));
  assert.ok(runner.includes("$expected = -1073741795"));
  assert.ok(runner.includes("Invoke-TypedGeneration 'positive' $level -Bad $true"));
});
