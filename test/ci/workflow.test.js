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
function active(event, mode = '', sanitizers = false) {
  const context = {github: {event_name: event}, inputs: {mode, sanitizers},
    needs: {native: {result: 'success'}}, always: () => true};
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
test('ordinary PR, push and manual ci run exactly one Linux job', () => {
  for (const [event, mode] of [['pull_request', ''], ['push', ''], ['workflow_dispatch', 'ci']]) {
    assert.deepEqual(active(event, mode), ['native']);
  }
  assert.deepEqual(active('workflow_dispatch', 'ci', true), ['native']);
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
  assert.match(source, /budget\.js --reserve 120 "\$CHIBI_CORE_DEADLINE"/);
  assert.match(source, /budget\.js "\$CHIBI_CORE_DEADLINE" bash \.github\/ci\/linux-core\.sh/);
  assert.match(source, /deadline=\$\(\(\$\(date \+%s\) \+ 5400\)\)/);
  const setup = readFileSync(path.join(root, '.github/ci/linux-setup.sh'), 'utf8');
  assert.match(setup, /budget\.js "\$\(\(\$\(date \+%s\) \+ 300\)\)" bash \.github\/ci\/format\.sh/);
});
