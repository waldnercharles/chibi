const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {createHash} = require('node:crypto');
const {spawnSync} = require('node:child_process');
const test = require('node:test');
const {validateRecovery} = require('../../.github/maintenance/package-runtime.js');
const root = path.resolve(__dirname, '../..');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const runtime = Buffer.from('fixture runtime');
const compiler = Buffer.from('fixture compiler');
const revision = 'a'.repeat(40), tree = 'b'.repeat(40);
const evidence = {
  revision, source_tree: tree, runner_os: 'Linux', runner_arch: 'X64',
  runtime_sha256: hash(runtime), proof_exit_code: 0,
  compiler_bytes: compiler.length, compiler_sha256: hash(compiler),
  native_build_seconds: 1, proof_seconds: 2,
  child_environment: {PATH: '', CC: 'unavailable-in-recovery-proof', CXX: 'unavailable-in-recovery-proof',
    TEMP: '/proof', TMP: '/proof', TMPDIR: '/proof'},
  input_files: ['chibi-compiler-host', 'tools/compiler/bootstrap/compiler.chibip'],
};
test('runtime preparation validates actual bytes and rejects changed provenance/isolation', () => {
  const validate = value => validateRecovery('linux-x64', revision, tree, value, runtime, compiler, `${hash(compiler)}  compiler.chibip\n`);
  assert.equal(validate(evidence), hash(compiler));
  for (const mutate of [
    value => { value.revision = 'c'.repeat(40); },
    value => { value.source_tree = 'c'.repeat(40); },
    value => { value.runner_arch = 'ARM64'; },
    value => { value.runtime_sha256 = '0'.repeat(64); },
    value => { value.compiler_sha256 = '0'.repeat(64); },
    value => { value.compiler_bytes++; },
    value => { value.proof_exit_code = 1; },
    value => { value.child_environment.PATH = '/usr/bin'; },
    value => { value.child_environment.TOKEN = 'not-a-secret-fixture'; },
    value => { value.child_environment.TMP = '/different'; },
    value => { value.input_files.push('tools/compiler/hidden.C'); },
    value => { value.input_files = []; },
    value => { value.proof_seconds = -1; },
  ]) {
    const changed = structuredClone(evidence);
    mutate(changed);
    assert.throws(() => validate(changed), /linux-x64:/);
  }
  assert.throws(() => validateRecovery('linux-x64', revision, tree, evidence, runtime, Buffer.from('changed'), ''), /compiler bytes/);
  const mac = {...evidence, runner_os: 'macOS', runner_arch: 'ARM64'};
  assert.equal(validateRecovery('macos-arm64', revision, tree, mac, runtime, compiler, `${hash(compiler)}  compiler.chibip`), hash(compiler));
});

function fixture(t) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'chibi-maintenance-test-'));
  t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
  const repo = path.join(directory, 'repo');
  fs.mkdirSync(repo);
  const write = (name, text) => {
    const file = path.join(repo, name);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, text, {mode: 0o755});
  };
  for (const name of ['.github/ci/budget.js', '.github/maintenance/source-bundle.sh', '.github/maintenance/bootstrap-host.sh', '.github/maintenance/native-seed.sh']) {
    write(name, fs.readFileSync(path.join(root, name)));
  }
  const git = (...args) => {
    const result = spawnSync('git', args, {cwd: repo, encoding: 'utf8', timeout: 10000});
    assert.equal(result.status, 0, result.stderr);
    return result.stdout.trim();
  };
  git('init', '-b', 'main');
  git('config', 'user.name', 'Maintenance test');
  git('config', 'user.email', 'test@example.invalid');
  return {directory, repo, write, git};
}

test('source bundle is self-contained, exact HEAD, and excludes unrelated history', t => {
  const {directory, repo, write, git} = fixture(t);
  write('source.txt', 'fixture\n');
  git('add', '.'); git('commit', '-m', 'fixture');
  const head = git('rev-parse', 'HEAD'), sourceTree = git('rev-parse', 'HEAD^{tree}');
  git('checkout', '-b', 'unrelated'); write('unrelated.txt', 'must not be bundled\n');
  git('add', '.'); git('commit', '-m', 'unrelated');
  const unrelated = git('rev-parse', 'HEAD');
  git('checkout', 'main');
  const refs = git('show-ref');
  const output = path.join(directory, 'output with spaces');
  const result = spawnSync('bash', ['.github/maintenance/source-bundle.sh', output], {cwd: repo, encoding: 'utf8', timeout: 10000});
  assert.equal(result.status, 0, result.stderr);
  const manifest = JSON.parse(fs.readFileSync(path.join(output, 'manifest.json')));
  assert.equal(manifest.commit, head); assert.equal(manifest.tree, sourceTree);
  assert.equal(manifest.sha256, hash(fs.readFileSync(path.join(output, 'chibi.bundle'))));
  assert.equal(git('show-ref'), refs);
  const clone = path.join(directory, 'clone.git');
  git('clone', '--bare', path.join(output, 'chibi.bundle'), clone);
  assert.notEqual(spawnSync('git', ['-C', clone, 'cat-file', '-e', unrelated]).status, 0);
  assert.equal(git('status', '--porcelain'), '');
});

test('host/native command construction preserves protocol, platform, pin and fixed point', t => {
  // No Chibi compiler runs: this disposable repository contains only fixture programs.
  const {directory, repo, write, git} = fixture(t);
  write('.gitignore', 'build-release/\nbuild-native-self-host/\n');
  write('tools/bootstrap/native-seed.tsv', `linux-x64\tfixture-tag\tfixture-asset\t${'a'.repeat(64)}\n`);
  write('tools/bootstrap/native-seed.sh', '#!/bin/sh\nset -eu\nprintf "%s\\n" "$@" > "$SEED_LOG"\nmkdir -p "$(dirname "$2")"\nprintf fixture > "$2"\nchmod +x "$2"\n');
  write('tools/build.sh', '#!/bin/sh\nset -eu\nprintf "%s\\n" "$@" > "$BUILD_LOG"\nmkdir -p build-release/sdk build-native-self-host/stage3\nprintf \'#!/bin/sh\\nprintf "%%s\\\\n" "${PROTOCOL:-15}"\\n\' > build-release/sdk/chibi-build-host\nchmod +x build-release/sdk/chibi-build-host\nprintf fixture > build-native-self-host/stage3/chibi\nchmod +x build-native-self-host/stage3/chibi\necho "native-self-host: the fixed point is $PWD/build-native-self-host/stage3/chibi"\n');
  git('add', '.'); git('commit', '-m', 'fixture tools');
  const bin = path.join(directory, 'bin'); fs.mkdirSync(bin);
  fs.writeFileSync(path.join(bin, 'uname'), '#!/bin/sh\ncase "$1" in -s) echo Linux;; -m) echo x86_64;; esac\n', {mode: 0o755});
  const env = {...process.env, PATH: `${bin}:${process.env.PATH}`, LIBCLANG_ROOT: '/fixture llvm', LLVM_BIN: '/fixture llvm/bin',
    CC: 'fixture-clang', CXX: 'fixture-clang++', BUILD_LOG: path.join(directory, 'build.log'), SEED_LOG: path.join(directory, 'seed.log')};
  for (const [script, output, protocol] of [['bootstrap-host', 'host', '15'], ['bootstrap-host', 'bad-host', '14'], ['native-seed', 'native', '15']]) {
    const result = spawnSync('bash', [`.github/maintenance/${script}.sh`, path.join(directory, output)],
      {cwd: repo, env: {...env, PROTOCOL: protocol}, encoding: 'utf8', timeout: 10000});
    assert.equal(result.status === 0, protocol === '15', result.stderr);
  }
  const args = fs.readFileSync(env.BUILD_LOG, 'utf8').trim().split('\n');
  assert.deepEqual(args.slice(0, 9), ['build', 'native-self-host', '--llvm', env.LLVM_BIN, '--libclang-root', env.LIBCLANG_ROOT, '--jobs', '4', '--skip-vm']);
  assert.deepEqual(args.slice(9), ['--stage-one', path.join(directory, 'native/previous/chibi')]);
  assert.equal(fs.readFileSync(path.join(directory, 'native/chibi-native-linux-x64'), 'utf8'), 'fixture');
  assert.equal(fs.readFileSync(path.join(directory, 'host/hosts.tsv'), 'utf8').split('\t')[0], 'linux-x64');
  assert.equal(git('status', '--porcelain'), '');
});
