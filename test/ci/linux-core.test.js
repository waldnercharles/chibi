const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {tmpdir} = require('node:os');
const {createHash} = require('node:crypto');
const {spawnSync} = require('node:child_process');
const test = require('node:test');
const source = path.resolve(__dirname, '../..');
const nm = spawnSync('sh', ['-c', 'command -v nm'], {encoding: 'utf8'}).stdout.trim();

// Exercise both real shell entries and the real seed checksum verifier. Only
// their process boundaries are controlled: no Chibi, LLVM or C work executes.
for (const [failure, status] of [
  ['', 0], ['release', 17], ['release-partial', 18], ['target', 19],
  ['graph', 23], ['selector', 29], ['scalar', 31], ['compiler', 37],
  ['selector-and-scalar', 29], ['evidence', 43],
]) {
  test(`core reuses the successful Release compiler (${failure || 'success'})`, () => {
    const scratch = fs.realpathSync(fs.mkdtempSync(path.join(tmpdir(), 'chibi-linux-core-')));
    const root = path.join(scratch, 'checkout with spaces');
    const logs = path.join(scratch, 'logs');
    const write = (name, text, executable = false) => {
      const file = path.join(root, name);
      fs.mkdirSync(path.dirname(file), {recursive: true});
      fs.writeFileSync(file, text, {mode: executable ? 0o755 : 0o644});
    };
    try {
      fs.mkdirSync(logs);
      for (const name of ['.github/ci/linux-core.sh', 'tools/build.sh', 'tools/bootstrap/native-seed.sh']) {
        write(name, fs.readFileSync(path.join(source, name)));
      }
      for (const name of ['tools/toolchain/Driver.chibi', 'tools/graph/Fixture.chibi', 'packages/core/Fixture.chibi']) {
        write(name, 'package fixture\n');
      }
      write('bin/uname', '#!/bin/sh\ncase "$1" in -s) echo Linux ;; -m) echo x86_64 ;; *) exit 90 ;; esac\n', true);
      for (const name of ['opt', 'llc', 'ld.lld']) {
        write(`llvm/${name}`, '#!/bin/sh\n[ "$1" = --version ] || exit 91\necho "LLVM version 22.1.8 LLD 22.1.8"\n', true);
      }
      const logPrelude = `const fs = require('node:fs');
const path = require('node:path');
const argv = process.argv.slice(2);
const log = event => fs.appendFileSync(process.env.FIXTURE_LOG, JSON.stringify(event) + '\\n');
`;
      write('bin/ccache', `#!/usr/bin/env node
${logPrelude}log({kind: 'ccache', argv});
`, true);
      write('bin/gh', `#!/usr/bin/env node
${logPrelude}if (argv[0] !== 'release' || argv[1] !== 'download') process.exit(92);
log({kind: 'download', argv});
fs.copyFileSync(process.env.FIXTURE_SEED, path.join(argv[argv.indexOf('--dir') + 1], argv[argv.indexOf('--pattern') + 1]));
`, true);
      write('test/native_aot/targets/build.sh', '#!/bin/sh\nexec node "$RUNNER_TEMP/target" "$@"\n');
      fs.writeFileSync(path.join(logs, 'target'), `#!/usr/bin/env node
${logPrelude}log({kind: 'target', argv});
if (argv.length !== 3 || argv[0] !== path.join(process.cwd(), 'build-release/sdk/chibi') ||
    argv[1] !== process.env.CHIBI_LLVM_BIN || argv[2] !== process.env.CHIBI_NATIVE_BUNDLE) process.exit(93);
fs.accessSync(argv[0], fs.constants.X_OK);
if (process.env.FIXTURE_FAILURE === 'target') process.exit(19);
`);
      // This entry is materialized by either controlled compiler, then cached
      // and executed by the unchanged tools/build.sh, including warm hits.
      fs.writeFileSync(path.join(logs, 'entry'), `#!/usr/bin/env node
${logPrelude}const compiler = @COMPILER@;
const env = Object.fromEntries(['CHIBI_BUILD_TIMINGS', 'CHIBI_TEST_TIMINGS', 'CHIBI_TEST_WORKERS', 'CHIBI_TOOLS_BUILD_DIR',
  'CHIBI_LANGUAGE_BUILD_DIR', 'CHIBI_NATIVE_BUNDLE', 'UBSAN_OPTIONS',
  'CC', 'CXX', 'CFLAGS', 'CXXFLAGS'].map(name => [name, process.env[name] || '']));
log({kind: 'command', compiler, entry: __filename, argv, env});
const failure = process.env.FIXTURE_FAILURE;
function executable(name, contents) {
  fs.mkdirSync(path.dirname(name), {recursive: true});
  fs.writeFileSync(name, contents, {mode: 0o755});
}
if (argv.includes('--release')) {
  if (failure === 'release') process.exit(17);
  executable('build-release/sdk/chibi', fs.readFileSync(process.env.FIXTURE_CURRENT));
  if (failure === 'release-partial') process.exit(18);
} else if (argv[1] === 'native-evidence') {
  if (failure === 'evidence') process.exit(43);
} else if (argv[1] === 'test') {
  if (argv.includes('graph-engine') && failure === 'graph') process.exit(23);
  if (argv.includes('native') && ['selector', 'selector-and-scalar'].includes(failure)) process.exit(29);
  if (argv.includes('language-scalar-native') && ['scalar', 'selector-and-scalar'].includes(failure)) process.exit(31);
} else process.exit(95);
`);
      function compiler(role) {
        return `#!/usr/bin/env node
${logPrelude}log({kind: 'compile', compiler: ${JSON.stringify(role)}, argv});
const output = argv[argv.indexOf('--output') + 1];
fs.writeFileSync(output, fs.readFileSync(path.join(process.env.RUNNER_TEMP, 'entry'), 'utf8').replace('@COMPILER@', ${JSON.stringify(JSON.stringify(role))}), {mode: 0o755});
if (${JSON.stringify(role)} === 'current' && process.env.FIXTURE_FAILURE === 'compiler') process.exit(37);
`;
      }
      fs.writeFileSync(path.join(logs, 'seed'), compiler('seed'), {mode: 0o755});
      fs.writeFileSync(path.join(logs, 'current'), compiler('current'), {mode: 0o755});
      const sha = createHash('sha256').update(fs.readFileSync(path.join(logs, 'seed'))).digest('hex');
      write('tools/bootstrap/native-seed.tsv', `linux-x64\tfixture-seed\tfixture-compiler\t${sha}\n`);
      const env = {...process.env, PATH: `${root}/bin:${process.env.PATH}`, RUNNER_TEMP: logs,
        CHIBI_LLVM_BIN: path.join(root, 'llvm'), FIXTURE_LOG: path.join(logs, 'events.jsonl'),
        FIXTURE_SEED: path.join(logs, 'seed'), FIXTURE_CURRENT: path.join(logs, 'current'), FIXTURE_FAILURE: failure,
        CHIBI_TEST_WORKERS: '5', CC: 'clang-18', CXX: 'clang++-18',
        CFLAGS: '-DFIXTURE_C=1', CXXFLAGS: '-DFIXTURE_CXX=1',
        GITHUB_SHA: 'fixture-head', GITHUB_RUN_ID: '11', GITHUB_RUN_ATTEMPT: '2', GITHUB_JOB: 'native'};
      for (const name of ['CHIBI_BUILD_HOST', 'CHIBI_BUILD_TIMINGS', 'CHIBI_COMPILER_HOST', 'CHIBI_NATIVE_BUNDLE', 'CHIBI_TOOLS_BUILD_DIR', 'CHIBI_LANGUAGE_BUILD_DIR']) delete env[name];
      const result = spawnSync('bash', ['.github/ci/linux-core.sh'], {cwd: root, env, encoding: 'utf8', timeout: 10000});
      assert.equal(result.error, undefined);
      assert.equal(result.status, status, result.stdout + result.stderr);
      const events = fs.readFileSync(env.FIXTURE_LOG, 'utf8').trim().split('\n').map(line => JSON.parse(line));
      const commands = events.filter(event => event.kind === 'command');
      const current = path.join(root, 'build-release/sdk/chibi');
      const seed = path.join(root, '.chibi/native-seed/linux-x64/chibi');
      const selected = event => event.argv[event.argv.indexOf('--native-driver') + 1];
      assert.equal(commands[0].compiler, 'seed');
      assert.equal(selected(commands[0]), seed);
      for (const command of commands.slice(1)) {
        assert.equal(command.compiler, 'current', 'later core command selected the seed');
        assert.equal(selected(command), current, 'later core command lost explicit Release compiler');
      }
      const bare = command => command.argv.filter((arg, index, argv) =>
        !['--native-driver', '--llvm'].includes(arg) && !['--native-driver', '--llvm'].includes(argv[index - 1]));
      const release = ['build', '--release'];
      const evidence = ['build', 'native-evidence', '--sha', 'fixture-head', '--run', '11', '--attempt', '2', '--job', 'native',
        '--outcome', 'success', '--build', 'build-release', '--output', `${logs}/native-profile`, '--nm', nm];
      const graph = ['build', 'test', 'graph-engine', 'graph-driver', 'graph-commands', 'graph-evidence', 'graph-discovery', 'graph-wasm', 'graph-os'];
      const selectors = ['build', 'test', 'short', 'native', 'projects', 'language', 'language-scalar-native'];
      const released = !failure.startsWith('release');
      const currentReady = released && failure !== 'compiler';
      const expected = [release];
      if (currentReady) expected.push(evidence);
      if (currentReady && !['evidence', 'target'].includes(failure)) expected.push(graph);
      if (currentReady && !['evidence', 'target', 'graph'].includes(failure)) expected.push(selectors);
      assert.deepEqual(commands.map(bare), expected, 'native core command inventory/order/options changed');
      const flow = expected.flatMap(command => [command,
        ...(command === evidence && failure !== 'evidence' ? ['target'] : [])]);
      assert.deepEqual(events.filter(event => ['command', 'target'].includes(event.kind))
        .map(event => event.kind === 'command' ? bare(event) : event.kind), flow,
      'target-module ordering changed');
      assert.equal(events.filter(event => event.kind === 'target').length,
        currentReady && failure !== 'evidence' ? 1 : 0);
      const compiles = events.filter(event => event.kind === 'compile');
      assert.deepEqual(compiles.map(event => event.compiler), released ? ['seed', 'current'] : ['seed']);
      assert.equal(events.filter(event => event.kind === 'download').length, 1, 'seed fallback/download');
      const entries = fs.readdirSync(path.join(root, '.chibi/build-entry'));
      assert.equal(entries.some(name => name.startsWith('work.')), false, 'partial entry work leaked');
      assert.equal(entries.length, currentReady ? 2 : 1);
      assert.equal(new Set(commands.filter(event => event.compiler === 'current').map(event => event.entry)).size,
        currentReady ? 1 : 0, 'current entry was not reused warm');
      assert.deepEqual(events.filter(event => event.kind === 'ccache'), [], 'retired C cache phase ran');
      for (const command of commands) {
        assert.equal(command.env.CHIBI_TEST_WORKERS, '5');
        assert.equal(command.env.CHIBI_TEST_TIMINGS, `${logs}/case-timings`);
        assert.equal(command.env.UBSAN_OPTIONS, 'halt_on_error=1');
        assert.equal(command.env.CC, 'clang-18'); assert.equal(command.env.CXX, 'clang++-18');
        assert.equal(command.env.CFLAGS, '-DFIXTURE_C=1'); assert.equal(command.env.CXXFLAGS, '-DFIXTURE_CXX=1');
        assert.equal(command.env.CHIBI_NATIVE_BUNDLE, bare(command)[1] === 'test' ? `${logs}/native-bundle` : '');
        assert.equal(command.env.CHIBI_BUILD_TIMINGS, bare(command).includes('--release') ? `${logs}/build-timings` : '');
        assert.equal(command.env.CHIBI_TOOLS_BUILD_DIR, '');
        assert.equal(command.env.CHIBI_LANGUAGE_BUILD_DIR, '');
      }
      if (failure === 'release-partial') assert.ok(fs.existsSync(current), 'partial Release output fixture missing');
    } finally { fs.rmSync(scratch, {recursive: true, force: true}); }
  });
}
