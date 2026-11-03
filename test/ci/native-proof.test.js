const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {tmpdir} = require('node:os');
const {spawnSync, execFileSync} = require('node:child_process');
const test = require('node:test');
const source = path.resolve(__dirname, '../..');

for (const [full, bootstrapRun] of [['false', ''], ['false', '123'], ['true', ''], ['true', '123']]) {
  test(`native proof uses the native entry and explicit Release comparison hosts (full=${full}, pinned=${bootstrapRun !== ''})`, () => {
    const scratch = fs.realpathSync(fs.mkdtempSync(path.join(tmpdir(), 'chibi-ci-native-host-')));
    const root = path.join(scratch, 'repo');
    const logs = path.join(scratch, 'logs');
    try {
      fs.mkdirSync(root);
      fs.mkdirSync(logs);
      const checks = ['native_aot/native_driver/run.sh', 'native_aot/native_driver/cache.sh',
        'native_aot/native_driver/minimum.sh', 'native_aot/text_format/run.sh', 'native_aot/link/memory.sh', 'native_aot/output/run.sh',
        'native_aot/diagnostics/run.sh', 'native_aot/generations/run.sh', 'native_aot/test_attributes/run.sh',
        'native_aot/failures/run.sh', 'native_aot/task_busy/run.sh', 'native_aot/optionals/run.sh',
        'native_aot/fallible/run.sh',
        'bootstrap/native-entry.sh', 'bootstrap/native-runner.sh', 'native_aot/tasks/run.sh',
        'native_aot/math_scalar/run.sh', 'native_aot/math_builtin/select.sh', 'native_aot/characters/run.sh', 'native_aot/text_arguments/run.sh', 'native_aot/nested_arrays/run.sh', 'native_aot/managed_values/run.sh',
        'native_aot/managed_pointers/run.sh', 'native_aot/maps/run.sh',
        'native_cli/run.sh', 'native_cli/watch.sh'];
      for (const directory of ['tools/bootstrap', 'tools/graph', 'packages/core', 'llvm', '.github/ci', 'build/sdk', 'build-release/sdk',
        'test/native_aot/asset_compiler',
        ...checks.map(file => path.dirname('test/' + file))]) {
        fs.mkdirSync(path.join(root, directory), {recursive: true});
      }
      for (const file of ['tools/build.sh', '.github/ci/native-proof.sh', '.github/ci/budget.js']) {
        fs.copyFileSync(path.join(source, file), path.join(root, file));
      }
      fs.writeFileSync(path.join(root, 'tools/bootstrap/native-seed.tsv'), '# no pinned seed in this launcher fixture\n');
      fs.writeFileSync(path.join(root, 'tools/bootstrap/seed.sh'), 'exit 0\n');
      fs.writeFileSync(path.join(root, 'tools/bootstrap.sh'), 'exit 91\n');
      fs.writeFileSync(path.join(root, 'tools/bootstrap/native-seed.sh'), '#!/bin/sh\nmkdir -p "$(dirname "$2")"\ncp "$RUNNER_TEMP/native-driver" "$2"\n');
      fs.mkdirSync(path.join(root, 'tools/toolchain'));
      fs.writeFileSync(path.join(root, 'tools/toolchain/Driver.chibi'), 'fixture\n');
      fs.writeFileSync(path.join(root, '.gitignore'), '.chibi/\n');
      for (const file of checks) {
        fs.writeFileSync(path.join(root, 'test', file), `printf '%s\\n' '${file}' >> "$RUNNER_TEMP/checks"\n`);
      }
      fs.writeFileSync(path.join(root, 'test/native_aot/asset_compiler/bridge.sh'), `set -eu
test "$1" = "$PWD/build-release/sdk/chibi-asset-compiler"
test "$2" = "$PWD/build-release/sdk/chibi"
test "$3" = "$CHIBI_LLVM_BIN"
test "$4" = clang-18
printf '%s\\n' native_aot/asset_compiler/bridge.sh >> "$RUNNER_TEMP/checks"
`);
      for (const name of ['opt', 'llc', 'ld.lld', 'ld64.lld']) {
        fs.writeFileSync(path.join(root, 'llvm', name), '#!/bin/sh\necho "LLVM version 22.1.8 LLD 22.1.8"\n', {mode: 0o755});
      }
      for (const directory of ['build', 'build-release']) {
        fs.writeFileSync(path.join(root, directory, 'sdk/chibi-build-host'), `#!/bin/sh
exit 92
`, {mode: 0o755});
      }
      fs.writeFileSync(path.join(logs, 'command-driver'), `#!/bin/sh
printf '%s\\n' 'native' >> "$RUNNER_TEMP/selected"
printf '%s\\n' "$*" >> "$RUNNER_TEMP/commands"
`, {mode: 0o755});
      fs.writeFileSync(path.join(logs, 'native-driver'), `#!/bin/sh
while [ "$#" -gt 0 ]; do
  if [ "$1" = --output ]; then cp "$RUNNER_TEMP/command-driver" "$2"; exit 0; fi
  shift
done
exit 93
`, {mode: 0o755});
      const git = args => execFileSync('git', args, {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}).trim();
      git(['init', '-q']); git(['add', '.']);
      git(['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'launcher fixture']);
      const env = {...process.env, RUNNER_TEMP: logs, CHIBI_FULL: full, CHIBI_LLVM_BIN: path.join(root, 'llvm'),
        CHIBI_WINDOWS_BOOTSTRAP_RUN: bootstrapRun,
        GITHUB_SHA: git(['rev-parse', 'HEAD'])};
      delete env.CHIBI_BUILD_HOST;
      const result = spawnSync('bash', ['.github/ci/native-proof.sh'], {cwd: root, env, encoding: 'utf8', timeout: 5000});
      assert.equal(result.status, 0, result.stderr);
      const selected = fs.readFileSync(path.join(logs, 'selected'), 'utf8').trim().split('\n');
      assert.deepEqual(selected, full === 'true' ? ['native', 'native', 'native'] : ['native']);
      const commands = fs.readFileSync(path.join(logs, 'commands'), 'utf8').trim().split('\n');
      assert.ok(commands[0].includes(`--build-host ${root}/build-release/sdk/chibi-build-host`));
      assert.ok(commands[0].includes(`--compiler-host ${root}/build-release/sdk/chibi-compiler-host`));
      assert.ok(commands[0].includes('--compare-vm'));
      assert.equal(commands[0].includes(`--windows ${logs}/windows`), bootstrapRun === '');
      assert.equal(commands[0].includes('--skip-vm-comparison'), full !== 'true');
      assert.equal(commands[0].includes(' --skip-vm '), false);
      if (full === 'true') {
        assert.ok(commands[1].includes('build --release --target chibi --target chibi-asset-compiler'));
        assert.ok(commands[2].includes('graph-sdk-install'));
      }
      assert.deepEqual(fs.readFileSync(path.join(logs, 'checks'), 'utf8').trim().split('\n'),
        full === 'true' ? [...checks, 'native_aot/asset_compiler/bridge.sh'] : checks);
    } finally { fs.rmSync(scratch, {recursive: true, force: true}); }
  });
}
