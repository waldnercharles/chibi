const assert = require('node:assert/strict');
const {mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync} = require('node:fs');
const {spawnSync} = require('node:child_process');
const {tmpdir} = require('node:os');
const path = require('node:path');
const test = require('node:test');
const core = path.resolve(__dirname, '../../.github/ci/linux-core.sh');

// Real orchestration with disposable commands checks bootstrap reuse and failure
// propagation. Language and ownership cases execute in their owning suites.
for (const failure of ['', 'native']) {
  test(`core builds native products once and preserves selector failure (${failure || 'success'})`, () => {
    const root = mkdtempSync(path.join(tmpdir(), 'chibi-ci-core-'));
    try {
      mkdirSync(path.join(root, 'tools'));
      mkdirSync(path.join(root, 'test/native_aot/targets'), {recursive: true});
      writeFileSync(path.join(root, 'test/native_aot/targets/build.sh'), `#!/bin/sh
set -eu
test "$1" = "$PWD/build-release/sdk/chibi"
test -x "$1"
test "$2" = fixture-llvm
test "$3" = "$RUNNER_TEMP/native-bundle"
printf 'target-runners\\n' >> "$RUNNER_TEMP/commands"
`);
      writeFileSync(path.join(root, 'tools/build.sh'), `#!/bin/sh
set -eu
printf '%s\\n' "$*" >> "$RUNNER_TEMP/commands"
case "$1:$2" in
  build:--release)
    test -n "$CHIBI_BUILD_TIMINGS"
    mkdir -p build-release/sdk
    printf '#!/bin/sh\\nexit 0\\n' > build-release/sdk/chibi
    chmod +x build-release/sdk/chibi ;;
  native-evidence:*) test -z "\${CHIBI_BUILD_TIMINGS:-}" ;;
  test:*)
    test -z "\${CHIBI_BUILD_TIMINGS:-}"
    case "$*" in *' native '*) [ -z "$FAIL_SELECTOR" ] || exit 17;; esac ;;
  *) exit 91 ;;
esac
`);
      const env = {...process.env, RUNNER_TEMP: root, GITHUB_SHA: 'fixture', GITHUB_RUN_ID: '1',
        GITHUB_RUN_ATTEMPT: '1', GITHUB_JOB: 'native', FAIL_SELECTOR: failure, CHIBI_LLVM_BIN: 'fixture-llvm'};
      delete env.CHIBI_BUILD_TIMINGS;
      const result = spawnSync('bash', [core], {cwd: root, env, encoding: 'utf8', timeout: 5000});
      assert.equal(result.status, failure ? 17 : 0, result.stderr);
      const commands = readFileSync(path.join(root, 'commands'), 'utf8').trim().split('\n');
      assert.equal(commands.filter(line => line.startsWith('build --release')).length, 1);
      assert.equal(commands.filter(line => line.startsWith('native-evidence ')).length, 1);
      assert.equal(commands.includes('target-runners'), process.platform === 'linux');
      assert.ok(commands.some(line => line === 'test short native projects language language-scalar-native'));
      assert.ok(commands.every(line => !/--host|--compiler-host|--sanitizers/.test(line)));
    } finally { rmSync(root, {recursive: true, force: true}); }
  });
}
