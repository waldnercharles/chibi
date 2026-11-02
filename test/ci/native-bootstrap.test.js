const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {tmpdir} = require('node:os');
const {spawnSync, execFileSync} = require('node:child_process');
const test = require('node:test');
const source = path.resolve(__dirname, '../..');

test('native bootstrap verifies pinned inputs and prepares graph archives without building C products', () => {
  for (const failure of ['', 'fetch', 'protocol', 'pin', 'archives']) {
    const root = fs.mkdtempSync(path.join(tmpdir(), 'chibi-ci-native-bootstrap-'));
    try {
      fs.mkdirSync(path.join(root, 'tools/bootstrap'), {recursive: true});
      fs.mkdirSync(path.join(root, 'logs'));
      const host = `#!/bin/sh
if [ "\${1:-}" = --protocol-version ]; then
  if [ "$FAILURE" = protocol ]; then echo 14; else echo 15; fi
  exit 0
fi
printf '%s\\n' "$*" >> "$RUNNER_TEMP/commands"
if [ "$FAILURE" = archives ]; then exit 17; fi
`;
      fs.writeFileSync(path.join(root, 'host'), host, {mode: 0o755});
      fs.writeFileSync(path.join(root, 'tools/bootstrap.sh'), `set -eu
echo build-host >> "$RUNNER_TEMP/commands"
if [ "$FAILURE" = fetch ]; then exit 19; fi
mkdir -p "$(dirname "$1")"; cp host "$1"
`);
      fs.writeFileSync(path.join(root, 'tools/bootstrap/runtime.sh'), 'echo compiler-host >> "$RUNNER_TEMP/commands"\n');
      fs.writeFileSync(path.join(root, 'tools/bootstrap/seed.sh'), 'echo vm-seed >> "$RUNNER_TEMP/commands"\n');
      fs.writeFileSync(path.join(root, 'tools/bootstrap/native-seed.tsv'),
        failure === 'pin' ? '# no native pin\n' : 'linux-x64\tfixture\n');
      const result = spawnSync('bash', [path.join(source, '.github/ci/linux-native.sh')], {
        cwd: root, env: {...process.env, RUNNER_TEMP: path.join(root, 'logs'), FAILURE: failure},
        encoding: 'utf8', timeout: 5000});
      assert.equal(result.status, {fetch: 19, protocol: 1, pin: 1, archives: 17}[failure] || 0, result.stderr);
      const commands = fs.readFileSync(path.join(root, 'logs/commands'), 'utf8').trim().split('\n');
      assert.deepEqual(commands.slice(0, 3), failure === 'fetch' ? ['build-host'] : ['build-host', 'compiler-host', 'vm-seed']);
      if (failure === '' || failure === 'archives') {
        assert.equal(commands.length, 4);
        assert.equal(commands[3], `--compiler tools/compiler --compiler-core packages/core --root ${root} --source tools/toolchain/Build.chibi --core packages/core --entry Build --timeout-ms 1800000 -- third-party ${root}/logs/native-prerequisites`);
      } else assert.equal(commands.length, failure === 'fetch' ? 1 : 3);
    } finally { fs.rmSync(root, {recursive: true, force: true}); }
  }
});

test('static setup runs checks but installs no native prerequisites; unknown profile rejects', () => {
  for (const profile of ['static', 'native', 'full', 'unknown']) {
    const scratch = fs.mkdtempSync(path.join(tmpdir(), 'chibi-ci-setup-'));
    const root = path.join(scratch, 'repo');
    const logs = path.join(scratch, 'logs');
    try {
      fs.mkdirSync(path.join(root, '.github/ci'), {recursive: true});
      fs.mkdirSync(path.join(scratch, 'bin'));
      fs.mkdirSync(logs);
      fs.copyFileSync(path.join(source, '.github/ci/budget.js'), path.join(root, '.github/ci/budget.js'));
      fs.writeFileSync(path.join(root, '.github/ci/format.sh'), 'echo format >> "$RUNNER_TEMP/commands"\n');
      for (const file of ['native-profile', 'native-ccache']) {
        fs.writeFileSync(path.join(root, `.github/${file}.sh`), `echo ${file} >> "$RUNNER_TEMP/commands"\n`);
      }
      fs.writeFileSync(path.join(scratch, 'bin/sudo'), '#!/bin/sh\nprintf "%s\\n" "$*" >> "$RUNNER_TEMP/commands"\n', {mode: 0o755});
      const git = args => execFileSync('git', args, {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']}).trim();
      git(['init', '-q']); git(['add', '.']);
      git(['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'setup fixture']);
      const result = spawnSync('bash', [path.join(source, '.github/ci/linux-setup.sh')], {cwd: root,
        env: {...process.env, PATH: `${scratch}/bin:${process.env.PATH}`, RUNNER_TEMP: logs,
          CHIBI_CI_PROFILE: profile, GITHUB_SHA: git(['rev-parse', 'HEAD']), XDG_RUNTIME_DIR: path.join(logs, 'runtime')},
        encoding: 'utf8', timeout: 5000});
      assert.equal(result.status, profile === 'unknown' ? 2 : 0, result.stderr);
      const commands = fs.readFileSync(path.join(logs, 'commands'), 'utf8').trim().split('\n');
      if (profile === 'static' || profile === 'unknown') assert.deepEqual(commands, ['format']);
      else {
        assert.equal(commands.length, 5);
        assert.equal(commands[0], 'format');
        assert.match(commands[1], /^apt-get update /);
        assert.match(commands[2], /^apt-get install .*llvm-18/);
        assert.deepEqual(commands.slice(3), ['native-profile', 'native-ccache']);
      }
    } finally { fs.rmSync(scratch, {recursive: true, force: true}); }
  }
});
