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
function active(event, mode = '', sanitizers = false, nativeResult, bootstrap = {}, stopped = false) {
  const context = {github: {event_name: event}, inputs: {mode, sanitizers,
    windows_bootstrap_run: bootstrap.run || '', windows_bootstrap_commit: bootstrap.commit || '',
    windows_bootstrap_sha256: bootstrap.sha256 || ''}, needs: {native: {}},
    always: () => true, cancelled: () => stopped};
  context.needs.native.result = nativeResult ||
    (vm.runInNewContext(jobs.get('native'), context, {timeout: 100}) ? 'success' : 'skipped');
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
    assert.deepEqual(active(event, mode), ['native', 'native-macos', 'native-self-host-windows', 'native-targets']);
  }
  assert.deepEqual(active('workflow_dispatch', 'ci', true), ['native', 'native-macos', 'native-self-host-windows', 'native-targets']);
});
test('full proofs are explicit, and Graph stays opt-in', () => {
  const full = ['build-alone', 'compare', 'native', 'native-macos', 'native-self-host-windows', 'native-targets', 'no-c-compiler', 'recover'];
  assert.deepEqual(active('schedule'), full);
  assert.deepEqual(active('workflow_dispatch', 'full'), full);
  assert.deepEqual(active('workflow_dispatch', 'graph'), ['graph']);
  assert.deepEqual(active('workflow_dispatch', 'unknown'), []);
});
test('only fully pinned manual ci admits Windows without the Linux or macOS work', () => {
  const pins = {run: '123', commit: 'a'.repeat(40), sha256: 'b'.repeat(64)};
  const ordinary = ['native', 'native-macos', 'native-self-host-windows', 'native-targets'];
  for (let mask = 0; mask < 8; mask++) {
    const bootstrap = Object.fromEntries(Object.entries(pins).filter((_, index) => mask & (1 << index)));
    assert.deepEqual(active('workflow_dispatch', 'ci', false, undefined, bootstrap),
      mask === 7 ? ['native-self-host-windows'] : ordinary, `pin subset ${mask}`);
  }
  assert.deepEqual(active('workflow_dispatch', 'ci', true, undefined, pins), ['native-self-host-windows']);
  for (const [event, mode] of [['pull_request', ''], ['push', ''], ['schedule', ''],
    ['workflow_dispatch', 'full'], ['workflow_dispatch', 'graph'], ['workflow_dispatch', 'unknown']]) {
    assert.deepEqual(active(event, mode, false, undefined, pins), active(event, mode));
  }
  assert.ok(!active('workflow_dispatch', 'ci', false, undefined, pins, true).includes('native-self-host-windows'));
});
test('the run name identifies a pinned Windows proof separately from complete ci', () => {
  const expression = /\$\{\{([\s\S]+?)\}\}/.exec(source.split('run-name: ')[1])[1];
  const pins = {windows_bootstrap_run: '123', windows_bootstrap_commit: 'a'.repeat(40), windows_bootstrap_sha256: 'b'.repeat(64)};
  for (const [event, mode, bootstrap, expected] of [
    ['workflow_dispatch', 'ci', pins, 'Windows proof'],
    ['workflow_dispatch', 'ci', {}, 'ci'],
    ['workflow_dispatch', 'ci', {...pins, windows_bootstrap_run: ''}, 'ci'],
    ['workflow_dispatch', 'ci', {...pins, windows_bootstrap_commit: ''}, 'ci'],
    ['workflow_dispatch', 'ci', {...pins, windows_bootstrap_sha256: ''}, 'ci'],
    ['workflow_dispatch', 'full', pins, 'full'],
    ['workflow_dispatch', 'graph', pins, 'graph'],
    ['pull_request', '', pins, 'pull_request'], ['push', '', pins, 'push'], ['schedule', '', pins, 'schedule'],
  ]) {
    const inputs = {mode, windows_bootstrap_run: '', windows_bootstrap_commit: '', windows_bootstrap_sha256: '', ...bootstrap};
    assert.equal(vm.runInNewContext(expression, {github: {event_name: event}, inputs}), expected);
  }
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
  for (const name of ['Format and static checks', 'Core functional tests']) {
    const step = linux.split('    - name: ' + name + '\n')[1].split(/^    - /m)[0];
    assert.doesNotMatch(step, /^      if:/m);
  }
  assert.match(source, /pull_request:\n    branches:\n    - main/);
  assert.match(source, /push:\n    branches:\n    - main/);
});

test('native proof runs after an attempted core phase while preserving its failure', () => {
  const step = source.split('    - name: Native fixed-point proof\n')[1].split(/^    - /m)[0];
  const condition = /^      if: \$\{\{ (.+) \}\}$/m.exec(step)[1];
  for (const outcome of ['success', 'failure', 'skipped']) {
    for (const stopped of [false, true]) {
      assert.equal(vm.runInNewContext(condition, {cancelled: () => stopped, steps: {core: {outcome}}}),
        !stopped && outcome !== 'skipped');
    }
  }
  const core = source.split('    - name: Core functional tests\n')[1].split(/^    - /m)[0];
  assert.doesNotMatch(core, /continue-on-error/);
});

test('ordinary native targets consume the Linux bundle without enabling full-only work', () => {
  for (const event of ['pull_request', 'push', 'workflow_dispatch']) {
    const mode = event === 'workflow_dispatch' ? 'ci' : '';
    for (const job of ['native-targets', 'native-self-host-windows']) {
      for (const result of ['success', 'failure']) assert.ok(active(event, mode, false, result).includes(job));
      for (const result of ['cancelled', 'skipped']) assert.ok(!active(event, mode, false, result).includes(job));
    }
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

test('pinned Windows bootstrap keeps canonical modes and validates producer identities', () => {
  assert.deepEqual(active('workflow_dispatch', 'ci', false, 'success', {run: '123'}), active('workflow_dispatch', 'ci'));
  assert.deepEqual(active('workflow_dispatch', 'full', false, 'success', {run: '123'}), active('workflow_dispatch', 'full'));
  assert.deepEqual(active('workflow_dispatch', 'graph', false, 'skipped', {run: '123'}), ['graph']);
  assert.ok(active('workflow_dispatch', 'ci', false, 'skipped', {run: '123'}).includes('native-self-host-windows'));
  assert.ok(!active('pull_request', '', false, 'skipped', {run: '123'}).includes('native-self-host-windows'));
  for (const input of ['windows_bootstrap_run', 'windows_bootstrap_commit', 'windows_bootstrap_sha256']) {
    assert.match(source, new RegExp(`^      ${input}:$`, 'm'));
  }
  const windows = source.slice(source.indexOf('\n  native-self-host-windows:\n'), source.indexOf('\n  native-targets:\n'));
  assert.match(windows, /\.github\/workflows\/native-self-host\.yml.*\.github\/workflows\/ci\.yml/);
  assert.match(windows, /Engine build and native tests.*Native self-host \(Linux\)/);
  assert.match(windows, /Windows bootstrap compiler SHA-256 differs from its pin/);
  assert.match(windows, /\$env:BOOTSTRAP_COMMIT -eq \$env:GITHUB_SHA/);
  assert.match(windows, /native-runner\.ps1/);
  assert.match(windows, /asset_compiler\/Bridge\.ps1/);
  const save = windows.slice(windows.indexOf('      - name: Cache the verified LLVM tools after Windows validation'));
  assert.match(save, /if: success\(\).*refs\/heads\/main.*schedule.*inputs\.mode == 'full'/);
  assert.ok(windows.indexOf('name: Cache the verified LLVM tools after Windows validation') > windows.indexOf('foreach ($case in $cases)'));
});

test('Windows runs the complete compiler root after its fixed point and before products', () => {
  const windows = source.slice(source.indexOf('\n  native-self-host-windows:\n'), source.indexOf('\n  native-targets:\n'));
  const marker = '      - name: Run complete native Windows compiler tests\n';
  assert.ok(windows.includes(marker), 'Windows must execute the full compiler test root');
  const step = windows.split(marker)[1].split(/^      - /m)[0];
  const condition = /^        if: \$\{\{ (.+) \}\}$/m.exec(step)[1];
  for (const outcome of ['success', 'failure', 'skipped', 'cancelled']) {
    for (const stopped of [false, true]) {
      assert.equal(vm.runInNewContext(condition, {cancelled: () => stopped,
        steps: {'windows-fixed-point': {outcome}}}), !stopped && outcome === 'success');
    }
  }
  assert.match(step, /CHIBI_CORE: \$\{\{ github\.workspace \}\}\/packages\/core/);
  assert.match(step, /& "\$env:RUNNER_TEMP\/stage4\/chibi\.exe" test "\$root\/tools\/compiler" --llvm \$env:CHIBI_WINDOWS_LLVM -O0 2>&1 \| Tee-Object -FilePath "\$env:RUNNER_TEMP\/windows-compiler-tests\.log"/);
  assert.match(step, /^          exit \$LASTEXITCODE$/m);
  assert.doesNotMatch(step, /continue-on-error|CHIBI_TEST_WORKERS|--filter|--package/);
  assert.ok(windows.indexOf(marker) > windows.indexOf('      - name: Keep Windows fixed-point evidence\n'));
  assert.ok(windows.indexOf(marker) < windows.indexOf('      - name: Check the native Windows asset compiler and C consumer\n'));
});

test('Windows preserves compiler test logs after success, failure or cancellation', () => {
  const marker = '      - name: Preserve native Windows compiler test output\n';
  assert.ok(source.includes(marker), 'compiler failures must retain their output');
  const step = source.split(marker)[1].split(/^      - /m)[0];
  const condition = /^        if: (.+)$/m.exec(step)[1];
  for (const outcome of ['success', 'failure', 'skipped', 'cancelled']) {
    assert.equal(vm.runInNewContext(condition, {always: () => true,
      steps: {'windows-compiler-tests': {outcome}}}), outcome !== 'skipped');
  }
  assert.match(step, /uses: actions\/upload-artifact@/);
  assert.match(step, /name: native-windows-compiler-tests-\$\{\{ github\.run_id \}\}-\$\{\{ github\.run_attempt \}\}/);
  assert.match(step, /path: \$\{\{ runner\.temp \}\}\/windows-compiler-tests\.log/);
  assert.match(step, /retention-days: 7/);
});

test('the transitional Windows bootstrap allowance leaves other phase bounds unchanged', () => {
  const windows = source.slice(source.indexOf('\n  native-self-host-windows:\n'), source.indexOf('\n  native-targets:\n'));
  assert.equal(/^    timeout-minutes: (\d+)$/m.exec(windows)[1], '120');
  const linux = source.slice(source.indexOf('\n  native:\n'), source.indexOf('\n  native-macos:\n'));
  const macos = source.slice(source.indexOf('\n  native-macos:\n'), source.indexOf('\n  native-self-host-windows:\n'));
  assert.match(linux, /^    timeout-minutes: 135$/m);
  assert.match(macos, /^    timeout-minutes: 45$/m);
});

test('external Windows bootstrap skips only the unused current producer upload', () => {
  const proof = source.split('    - name: Native fixed-point proof\n')[1].split(/^    - /m)[0];
  assert.match(proof, /CHIBI_WINDOWS_BOOTSTRAP_RUN: \$\{\{ inputs\.windows_bootstrap_run \}\}/);
  const upload = source.split('    - name: Keep stage two for Windows\n')[1].split(/^    - /m)[0];
  const condition = /^      if: (.+)$/m.exec(upload)[1];
  for (const bootstrapRun of ['', '123']) {
    for (const outcome of ['success', 'failure', 'skipped']) {
      assert.equal(vm.runInNewContext(condition, {always: () => true,
        steps: {fixedpoint: {outcome}}, inputs: {windows_bootstrap_run: bootstrapRun}}),
      bootstrapRun === '' && outcome !== 'skipped');
    }
  }
  assert.match(upload, /name: native-stage-two-windows/);
  const modules = source.split('    - name: Keep native platform modules\n')[1].split(/^    - /m)[0];
  assert.doesNotMatch(modules, /windows_bootstrap_run/);
});
