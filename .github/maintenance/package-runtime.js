#!/usr/bin/env node
const fs = require('node:fs');
const path = require('node:path');
const {createHash} = require('node:crypto');
const {execFileSync} = require('node:child_process');
const digest = bytes => createHash('sha256').update(bytes).digest('hex');

// Validate actual bytes, not merely two matching claims in evidence files.
function validateRecovery(platform, revision, tree, evidence, runtime, compiler, recorded) {
  const fail = message => { throw new Error(`${platform}: ${message}`); };
  const expected = {'linux-x64': ['Linux', 'X64'], 'macos-arm64': ['macOS', 'ARM64']}[platform];
  if (!expected) fail('unsupported runtime platform');
  if (evidence.revision !== revision || evidence.source_tree !== tree) fail('recovery source differs from checkout');
  if (evidence.runner_os !== expected[0] || evidence.runner_arch !== expected[1]) fail('wrong recovery platform');
  if (!runtime.length || evidence.runtime_sha256 !== digest(runtime)) fail('runtime bytes differ from recovery');
  const compilerHash = digest(compiler);
  if (evidence.proof_exit_code !== 0 || !compiler.length || evidence.compiler_bytes !== compiler.length ||
      evidence.compiler_sha256 !== compilerHash || recorded.trim() !== `${compilerHash}  compiler.chibip`) {
    fail('compiler bytes or successful recovery evidence differ');
  }
  const environment = evidence.child_environment;
  if (!environment || Object.keys(environment).sort().join(',') !== 'CC,CXX,PATH,TEMP,TMP,TMPDIR' ||
      environment.PATH !== '' || environment.CC !== 'unavailable-in-recovery-proof' ||
      environment.CXX !== 'unavailable-in-recovery-proof' || !environment.TEMP ||
      environment.TEMP !== environment.TMP || environment.TEMP !== environment.TMPDIR) fail('recovery environment was not isolated');
  if (!Array.isArray(evidence.input_files) || !evidence.input_files.includes('chibi-compiler-host') ||
      !evidence.input_files.includes('tools/compiler/bootstrap/compiler.chibip') ||
      evidence.input_files.some(file => /\.(c|h|cc|cpp|cxx|m|mm)$/i.test(file))) fail('invalid clean-room input inventory');
  for (const field of ['native_build_seconds', 'proof_seconds']) {
    if (!Number.isFinite(evidence[field]) || evidence[field] < 0) fail(`invalid ${field}`);
  }
  return compilerHash;
}

if (require.main === module) {
  const [linux, macos, destination, evidenceURL] = process.argv.slice(2);
  if (process.argv.length !== 6 || !/^https:\/\/\S+$/.test(evidenceURL)) {
    throw new Error('usage: package-runtime.sh LINUX_PROOF MACOS_PROOF NEW_OUTPUT_DIRECTORY HTTPS_EVIDENCE_URL');
  }
  const run = (command, args) => execFileSync(command, args, {encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit']}).trim();
  if (run('git', ['status', '--porcelain', '--untracked-files=normal'])) throw new Error('Use a clean source checkout');
  const revision = run('git', ['rev-parse', 'HEAD']);
  const tree = run('git', ['rev-parse', 'HEAD^{tree}']);
  const current = JSON.parse(fs.readFileSync('tools/compiler/runtime.json'));
  if (current.format !== 2 || !current.repository || !current.platforms['windows-x64']) throw new Error('Incomplete format-2 runtime manifest');
  const version = fs.readFileSync('src/execution/prepared.h', 'utf8').match(/^#define EXECUTION_PREPARED_VERSION ([0-9]+)$/m)?.[1];
  const bound = fs.readFileSync('src/toolchain/compiler_host.h', 'utf8').match(/#define COMPILER_HOST_MAX_ARTIFACT_BYTES \(([0-9]+)u \* 1024u \* 1024u\)/)?.[1];
  if (!version || !bound) throw new Error('Missing prepared version or runtime artifact bound');
  const tag = `compiler-runtime-prepared${version}-${revision}`;
  const records = [];
  for (const [platform, directory] of [['linux-x64', linux], ['macos-arm64', macos]]) {
    const asset = `chibi-compiler-host-${platform}`;
    const runtime = fs.readFileSync(path.join(directory, asset));
    const compiler = fs.readFileSync(path.join(directory, 'compiler.chibip'));
    const evidence = JSON.parse(fs.readFileSync(path.join(directory, 'evidence.json')));
    const compilerHash = validateRecovery(platform, revision, tree, evidence, runtime, compiler,
      fs.readFileSync(path.join(directory, 'compiler.chibip.sha256'), 'utf8'));
    const imports = fs.readFileSync(path.join(directory, 'native-imports.txt'));
    if (!imports.toString().trim()) throw new Error(`${platform}: missing native imports`);
    records.push({platform, directory, asset, runtime, compilerHash, evidence, imports});
  }
  if (records[0].compilerHash !== records[1].compilerHash) throw new Error('Recovered compiler bytes differ across platforms');

  // No existing directory or published assets are overwritten. Windows stays pinned.
  const out = path.resolve(destination);
  fs.mkdirSync(out);
  const platforms = {...current.platforms};
  for (const record of records) {
    fs.writeFileSync(path.join(out, record.asset), record.runtime, {mode: 0o755});
    fs.writeFileSync(path.join(out, `seed-evidence-${record.platform}.json`), JSON.stringify(record.evidence, null, 2) + '\n');
    fs.writeFileSync(path.join(out, `native-imports-${record.platform}.txt`), record.imports);
    for (const name of ['recovery.stdout.txt', 'recovery.stderr.txt']) {
      fs.copyFileSync(path.join(record.directory, name), path.join(out, `${record.platform}-${name}`));
    }
    platforms[record.platform] = {tag, source_revision: revision, source_tree: tree, run_url: evidenceURL,
      max_artifact_bytes: Number(bound) * 1024 * 1024, asset: record.asset,
      sha256: digest(record.runtime), bytes: record.runtime.length};
  }
  fs.writeFileSync(path.join(out, 'runtime.json'), JSON.stringify({format: 2, repository: current.repository, platforms}, null, 2) + '\n');
  const short = revision.slice(0, 12);
  run('git', ['archive', '--format=tar.gz', `--prefix=chibi-${short}/`, '-o', path.join(out, `chibi-source-${short}.tar.gz`), revision]);
  const notices = path.join(out, 'chibi-runtime-notices');
  fs.mkdirSync(path.join(notices, 'chibi', 'docs'), {recursive: true});
  for (const name of ['COPYING', 'COPYING.EXAMPLES', 'COPYING.LESSER', 'LICENSE.md', 'docs/distribution-licensing.md']) {
    fs.copyFileSync(name, path.join(notices, 'chibi', name));
  }
  for (const [name, files] of [['fmt', ['LICENSE', 'README.md']], ['yyjson', ['LICENSE']]]) {
    fs.mkdirSync(path.join(notices, name));
    for (const file of files) fs.copyFileSync(`third_party/${name}/${file}`, path.join(notices, name, file));
  }
  const pins = fs.readFileSync('archive/third_party/CMakeLists.txt', 'utf8');
  for (const [name, repository, prefix] of [['sljit', 'zherczeg/sljit', 'sljit'], ['tree_sitter', 'tree-sitter/tree-sitter', 'tree-sitter']]) {
    const pin = pins.match(new RegExp(`chibi_third_party\\(${name} https://github\\.com/${repository}\\.git\\s+([0-9a-f]{40})(?:\\s|\\))`))?.[1];
    if (!pin) throw new Error(`Missing ${name} source pin`);
    const archive = path.join(out, `${name}-${pin}.tar.gz`);
    run('curl', ['-fsSL', '-o', archive, `https://github.com/${repository}/archive/${pin}.tar.gz`]);
    fs.mkdirSync(path.join(notices, name));
    run('tar', ['-xzf', archive, '-C', path.join(notices, name), '--strip-components=1', `${prefix}-${pin}/LICENSE`]);
  }
  run('tar', ['-czf', path.join(out, 'chibi-runtime-notices.tar.gz'), '-C', out, 'chibi-runtime-notices']);
  fs.rmSync(notices, {recursive: true});
  if (run('git', ['rev-parse', 'HEAD']) !== revision || run('git', ['status', '--porcelain', '--untracked-files=normal'])) {
    throw new Error('Source checkout changed while assembling release material');
  }
  const sums = fs.readdirSync(out).sort().map(name => `${digest(fs.readFileSync(path.join(out, name)))}  ${name}\n`).join('');
  fs.writeFileSync(path.join(out, 'SHA256SUMS'), sums);
  console.log(`Prepared ${tag} in ${out}. Nothing published, pinned, committed or dispatched.`);
}
module.exports = {validateRecovery};
