const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');

const source = readFileSync(path.join(__dirname, '../../.github/workflows/ci.yml'), 'utf8');
const linux = source.slice(source.indexOf('\n  native:\n'), source.indexOf('\n  native-macos:\n'));
const steps = linux.split(/^    - /m).slice(1);
const restores = steps.filter(step => step.includes('uses: actions/cache/restore@v4'));
const saves = steps.filter(step => step.includes('uses: actions/cache/save@v4'));
function field(step, name) {
  const match = new RegExp(`^ +${name}: (.*)$`, 'm').exec(step);
  assert.ok(match, name);
  return match[1];
}
function condition(step, context) {
  const match = /^      if: (.*(?:\n        .*\n?)*)/m.exec(step);
  assert.ok(match);
  const expression = match[1].trim().replace(/\n\s*/g, ' ')
    .replace(/\.([a-zA-Z_][\w-]*-[\w-]+)/g, "['$1']");
  return vm.runInNewContext(expression, context, {timeout: 100});
}
const stores = [
  ['${{ env.CCACHE_DIR }}', '${{ steps.ccache-config.outputs.prefix }}-${{ github.sha }}'],
  ['.chibi/asset-compiler-cache', 'asset-compiler-v2-${{ runner.os }}-${{ github.sha }}'],
  ['.chibi/compiler-cache', 'compiler-cache-${{ runner.os }}-${{ github.job }}-${{ github.run_id }}'],
];

test('Linux restores and saves the three established path-version stores', () => {
  for (const actions of [restores, saves]) {
    assert.deepEqual(actions.map(step => [field(step, 'path'), field(step, 'key')]), stores);
  }
  assert.match(restores[0], /restore-keys: \|\n          \$\{\{ steps\.ccache-config\.outputs\.prefix \}\}-\n          \$\{\{ steps\.ccache-config\.outputs\.compatibility-prefix \}\}-/);
  assert.equal(field(restores[1], 'restore-keys'), 'asset-compiler-v2-${{ runner.os }}-');
  assert.equal(field(restores[2], 'restore-keys'), 'compiler-cache-${{ runner.os }}-');
  assert.doesNotMatch(linux, /ci-v1-/);
  const caps = restores.map(step => Number(field(step, 'timeout-minutes')));
  assert.deepEqual(caps, [2, 2, 2]);
  assert.match(linux, new RegExp(`full\\) node .* --reserve ${caps.reduce((a, b) => a + b) * 60} `));
  assert.match(linux, /native\) node .* --reserve 120 /);
});

test('cache writes require successful full validation on main, never PR or push', () => {
  for (const event of ['pull_request', 'push', 'schedule', 'workflow_dispatch']) {
    for (const ref of ['refs/heads/main', 'refs/heads/topic', 'refs/pull/1330/merge']) {
      for (const mode of ['ci', 'full', 'graph']) {
        for (const success of [false, true]) {
          for (const hit of ['', 'false', 'true']) {
            const context = {github: {event_name: event, ref}, inputs: {mode}, success: () => success,
              steps: {ccache: {outputs: {'cache-hit': hit}}, 'asset-cache': {outputs: {'cache-hit': hit}}}};
            const trusted = success && ref === 'refs/heads/main'
              && (event === 'schedule' || (event === 'workflow_dispatch' && mode === 'full'));
            assert.deepEqual(saves.map(step => condition(step, context)),
              [trusted && hit !== 'true', trusted && hit !== 'true', trusted]);
          }
        }
      }
    }
  }
  for (const skip of ['true', 'false', '']) {
    for (const profile of ['static', 'native', 'full']) {
      assert.deepEqual(restores.map(step => condition(step, {steps: {receipt: {outputs: {skip, profile}}}})),
        [skip !== 'true' && profile === 'full', skip !== 'true' && profile === 'full',
          skip !== 'true' && profile !== 'static']);
    }
  }
});
