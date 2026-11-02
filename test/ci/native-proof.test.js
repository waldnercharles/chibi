const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {tmpdir} = require('node:os');
const {spawnSync, execFileSync} = require('node:child_process');
const test = require('node:test');
const source = path.resolve(__dirname, '../..');

for (const full of ['false', 'true']) {
  test(`native proof's real launcher selects Release over the existing Debug host (full=${full})`, () => {
    const scratch = fs.mkdtempSync(path.join(tmpdir(), 'chibi-ci-native-host-'));
    const root = path.join(scratch, 'repo');
    const logs = path.join(scratch, 'logs');
    try {
      fs.mkdirSync(root);
      fs.mkdirSync(logs);
      for (const directory of ['tools/bootstrap', '.github/ci', 'build/sdk', 'build-release/sdk',
        'test/native_aot/diagnostics', 'test/native_aot/test_attributes']) {
        fs.mkdirSync(path.join(root, directory), {recursive: true});
      }
      for (const file of ['tools/build.sh', '.github/ci/native-proof.sh', '.github/ci/budget.js']) {
        fs.copyFileSync(path.join(source, file), path.join(root, file));
      }
      fs.writeFileSync(path.join(root, 'tools/bootstrap/native-seed.tsv'), '# no pinned seed in this launcher fixture\n');
      fs.writeFileSync(path.join(root, 'tools/bootstrap/seed.sh'), 'exit 0\n');
      fs.writeFileSync(path.join(root, 'tools/bootstrap.sh'), 'exit 91\n');
      for (const name of ['diagnostics', 'test_attributes']) {
        fs.writeFileSync(path.join(root, `test/native_aot/${name}/run.sh`), 'exit 0\n');
      }
      for (const [directory, identity] of [['build', 'debug'], ['build-release', 'release']]) {
        fs.writeFileSync(path.join(root, directory, 'sdk/chibi-build-host'), `#!/bin/sh
if [ "\${1:-}" = --protocol-version ]; then echo 15; exit 0; fi
printf '%s\\n' '${identity}' >> "$RUNNER_TEMP/selected"
printf '%s\\n' "$*" >> "$RUNNER_TEMP/commands"
`, {mode: 0o755});
      }
      const git = args => execFileSync('git', args, {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}).trim();
      git(['init', '-q']); git(['add', '.']);
      git(['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'launcher fixture']);
      const env = {...process.env, RUNNER_TEMP: logs, CHIBI_FULL: full, GITHUB_SHA: git(['rev-parse', 'HEAD'])};
      delete env.CHIBI_BUILD_HOST;
      const result = spawnSync('bash', ['.github/ci/native-proof.sh'], {cwd: root, env, encoding: 'utf8', timeout: 5000});
      assert.equal(result.status, 0, result.stderr);
      const selected = fs.readFileSync(path.join(logs, 'selected'), 'utf8').trim().split('\n');
      assert.deepEqual(selected, full === 'true' ? ['release', 'release'] : ['release']);
      const commands = fs.readFileSync(path.join(logs, 'commands'), 'utf8').trim().split('\n');
      assert.ok(commands[0].includes(`--build-host ${root}/build-release/sdk/chibi-build-host`));
      assert.equal(commands[0].includes('--skip-vm-comparison'), full !== 'true');
      assert.equal(commands[0].includes(' --skip-vm '), false);
    } finally { fs.rmSync(scratch, {recursive: true, force: true}); }
  });
}
