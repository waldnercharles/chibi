const assert = require('node:assert/strict');
const {mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync} = require('node:fs');
const {spawnSync} = require('node:child_process');
const {tmpdir} = require('node:os');
const path = require('node:path');
const test = require('node:test');
const core = path.resolve(__dirname, '../../.github/ci/linux-core.sh');

// Run the real orchestration against disposable command fixtures, never against
// language/ownership fixtures. This checks fresh-checkout bootstrap and env scope.
for (const failure of ['', 'native']) {
  test(`core orchestration bootstraps once and preserves failure/scalar evidence (${failure || 'success'})`, () => {
    const root = mkdtempSync(path.join(tmpdir(), 'chibi-ci-core-'));
    try {
      mkdirSync(path.join(root, 'tools'));
      mkdirSync(path.join(root, 'bin'));
      writeFileSync(path.join(root, 'bin/ccache'), '#!/bin/sh\nexit 0\n', {mode: 0o755});
      writeFileSync(path.join(root, 'tools/build.sh'), `#!/bin/sh
set -eu
printf '%s\\n' "$*" >> "$RUNNER_TEMP/commands"
case "$1:$2" in
  build:--release)
    test -z "\${CHIBI_BUILD_HOST:-}"
    test -n "$CHIBI_BUILD_TIMINGS"
    mkdir -p build-release/sdk
    for name in chibi chibi-compile chibi-build-host chibi-compiler-host; do
      printf '#!/bin/sh\\nexit 0\\n' > "build-release/sdk/$name"; chmod +x "build-release/sdk/$name"
    done ;;
  build:--build)
    test "$CHIBI_BUILD_HOST" = build-release/sdk/chibi-build-host
    test "$VERBOSE" = 1
    test -n "$CHIBI_BUILD_TIMINGS"
    mkdir -p build/test/tools build/test/language
    for name in tools/test_tools language/test_language; do
      printf '#!/bin/sh\\nexit 0\\n' > "build/test/$name"; chmod +x "build/test/$name"
    done ;;
  native-evidence:*) test -z "\${CHIBI_BUILD_TIMINGS:-}" ;;
  test:*)
    test "$CHIBI_BUILD_HOST" = build-release/sdk/chibi-build-host
    test "$CHIBI_TOOLS_BUILD_DIR" = build/test/tools
    test -z "\${CHIBI_BUILD_TIMINGS:-}"
    for last do :; done
    if [ -n "$FAIL_SELECTOR" ] && [ "$last" = "$FAIL_SELECTOR" ]; then exit 17; fi ;;
  *) exit 91 ;;
esac
`);
      const env = {...process.env, PATH: `${root}/bin:${process.env.PATH}`, RUNNER_TEMP: root,
        BUILD_PROFILE: 'debug', BUILD_DEBUG_OPTIMIZATION: '1', BUILD_DEBUG_FRAME_POINTERS: 'on',
        CHIBI_CI_SANITIZERS: 'OFF', GITHUB_SHA: 'fixture', GITHUB_RUN_ID: '1',
        GITHUB_RUN_ATTEMPT: '1', GITHUB_JOB: 'native', FAIL_SELECTOR: failure};
      delete env.CHIBI_BUILD_HOST;
      delete env.CHIBI_BUILD_TIMINGS;
      const result = spawnSync('bash', [core], {cwd: root, env, encoding: 'utf8', timeout: 5000});
      assert.equal(result.status, failure ? 17 : 0, result.stderr);
      const commands = readFileSync(path.join(root, 'commands'), 'utf8').trim().split('\n');
      assert.equal(commands.filter(line => line.startsWith('build --release')).length, 1);
      assert.equal(commands.filter(line => line.startsWith('build --build')).length, 1);
      assert.equal(commands.filter(line => line.endsWith('language-scalar-native')).length, 1);
      assert.equal(commands.filter(line => line.startsWith('native-evidence ')).length, 1);
      assert.equal(commands.filter(line => line.endsWith('build-timings')).length, 0);
      assert.ok(commands.some(line => line.endsWith(' native')));
    } finally { rmSync(root, {recursive: true, force: true}); }
  });
}
