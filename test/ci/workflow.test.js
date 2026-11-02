const assert = require('node:assert/strict');
const {readFileSync, readdirSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');
const root = path.resolve(__dirname, '../..');
const source = readFileSync(path.join(root, '.github/workflows/ci.yml'), 'utf8');

// Exercise the actual job conditions, not a parallel production planner. These
// conditions deliberately use only comparisons, booleans and always(). Actionlint
// separately checks the complete GitHub workflow schema/expressions.
const jobs = new Map();
let current;
let condition = false;
for (const line of source.slice(source.indexOf('\njobs:\n') + 7).split('\n')) {
  const job = /^  ([a-z][a-z0-9-]*):$/.exec(line);
  if (job) { current = job[1]; jobs.set(current, ''); condition = false; }
  const start = /^    if: (.*)$/.exec(line);
  if (start) { jobs.set(current, start[1]); condition = true; }
  else if (condition && /^      \S/.test(line)) jobs.set(current, jobs.get(current) + ' ' + line.trim());
  else if (/^    \S/.test(line)) condition = false;
}
function active(event, mode = '', sanitizers = false, nativeResult) {
  const context = {github: {event_name: event}, inputs: {mode, sanitizers},
    needs: {native: {result: nativeResult || (event !== 'workflow_dispatch' || ['ci', 'full'].includes(mode) ? 'success' : 'skipped')}}, always: () => true};
  return [...jobs].filter(([, condition]) => {
    assert.notEqual(condition, '', 'every job explicitly states its trigger');
    return vm.runInNewContext(condition, context, {timeout: 100});
  }).map(([name]) => name).sort();
}

test('one workflow; no privileged PR trigger or publishing permissions', () => {
  assert.deepEqual(readdirSync(path.join(root, '.github/workflows')).filter(x => /\.ya?ml$/.test(x)), ['ci.yml']);
  assert.doesNotMatch(source, /pull_request_target|workflow_run:|contents: write|actions: write|pull-requests: write/);
  assert.doesNotMatch(source, /paths-ignore:|ci-full|ci-rerun|ci-seed/);
});
test('ordinary PR, push and manual ci run Linux, macOS and both native targets', () => {
  for (const [event, mode] of [['pull_request', ''], ['push', ''], ['workflow_dispatch', 'ci']]) {
    assert.deepEqual(active(event, mode), ['native', 'native-macos', 'native-targets']);
  }
  assert.deepEqual(active('workflow_dispatch', 'ci', true), ['native', 'native-macos', 'native-targets']);
});
test('full proofs are explicit, and Graph stays opt-in', () => {
  const full = ['build-alone', 'compare', 'native', 'native-macos', 'native-targets', 'no-c-compiler', 'recover'];
  assert.deepEqual(active('schedule'), full);
  assert.deepEqual(active('workflow_dispatch', 'full'), full);
  assert.deepEqual(active('workflow_dispatch', 'graph'), ['graph']);
  assert.deepEqual(active('workflow_dispatch', 'unknown'), []);
});
test('both canonical phase budgets and nested source gate are enforced', () => {
  assert.match(source, /CHIBI_CORE_DEADLINE=\$\(\(\$\(date \+%s\) \+ 2700\)\)/);
  assert.match(source, /budget\.js --reserve 360 "\$CHIBI_CORE_DEADLINE"/);
  assert.match(source, /budget\.js "\$CHIBI_CORE_DEADLINE" bash \.github\/ci\/linux-core\.sh/);
  assert.match(source, /deadline=\$\(\(\$\(date \+%s\) \+ 5400\)\)/);
  const setup = readFileSync(path.join(root, '.github/ci/linux-setup.sh'), 'utf8');
  assert.match(setup, /budget\.js "\$\(\(\$\(date \+%s\) \+ 300\)\)" bash \.github\/ci\/format\.sh/);
});

test('canonical work has no classifier, receipt or silent source-path bypass', () => {
  const linux = source.slice(source.indexOf('\n  native:\n'), source.indexOf('\n  native-macos:\n'));
  assert.doesNotMatch(source, /receipt|ci-policy|CHIBI_CI_PROFILE|Prepare native bootstrap|ci-tested-tree/);
  assert.doesNotMatch(readFileSync(path.join(root, '.github/ci/linux-setup.sh'), 'utf8'), /CHIBI_CI_PROFILE|static\)/);
  for (const name of ['Format and static checks', 'Core functional tests', 'Native fixed-point proof']) {
    const step = linux.split('    - name: ' + name + '\n')[1].split(/^    - /m)[0];
    assert.doesNotMatch(step, /^      if:/m);
  }
  assert.match(source, /pull_request:\n    branches:\n    - main/);
  assert.match(source, /push:\n    branches:\n    - main/);
});

test('ordinary native targets consume the Linux bundle without enabling full-only work', () => {
  for (const event of ['pull_request', 'push', 'workflow_dispatch']) {
    const mode = event === 'workflow_dispatch' ? 'ci' : '';
    for (const result of ['success', 'failure']) assert.ok(active(event, mode, false, result).includes('native-targets'));
    for (const result of ['cancelled', 'skipped']) assert.ok(!active(event, mode, false, result).includes('native-targets'));
  }
  const upload = source.split('    - name: Keep native platform modules\n')[1].split(/^    - /m)[0];
  const condition = /^      if: (.+)$/m.exec(upload)[1];
  for (const outcome of ['success', 'failure', 'skipped']) {
    assert.equal(vm.runInNewContext(condition, {always: () => true, steps: {core: {outcome}}}), outcome !== 'skipped');
  }
  assert.match(upload, /name: native-bundle/);
  assert.match(source, /CHIBI_FULL: \$\{\{ github.event_name == 'schedule' \|\| \(github.event_name == 'workflow_dispatch' && inputs.mode == 'full'\) \}\}/);
  assert.match(source, /platform: macos-arm64/);
  assert.match(source, /platform: windows-x64/);
});

test('ordinary platform validation never enables sanitizer or full-only certification', () => {
  const full = /CHIBI_FULL: \$\{\{ (.+) \}\}/.exec(source)[1];
  const sanitizer = /CHIBI_CI_SANITIZERS: \$\{\{ ([\s\S]+?) \}\}/.exec(source)[1];
  for (const [event, mode, requested, expected] of [
    ['pull_request', '', false, false], ['push', '', false, false],
    ['workflow_dispatch', 'ci', false, false], ['workflow_dispatch', 'ci', true, false],
    ['workflow_dispatch', 'full', false, false], ['workflow_dispatch', 'full', true, true],
    ['workflow_dispatch', 'graph', false, false], ['workflow_dispatch', 'graph', true, true],
    ['schedule', '', false, true],
  ]) {
    const context = {github: {event_name: event}, inputs: {mode, sanitizers: requested}};
    assert.equal(vm.runInNewContext(sanitizer, context), expected ? 'ON' : 'OFF');
    assert.equal(vm.runInNewContext(full, context), event === 'schedule' || (event === 'workflow_dispatch' && mode === 'full'));
  }
});
