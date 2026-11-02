'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {classify, fromCheckout} = require('../../.github/ci-policy.js');

test('only explicit documentation paths are static', () => {
  assert.equal(classify(['README.md', 'docs/ci.md', 'docs/nested/guide.md']), 'static');
  for (const name of ['AGENTS.md', 'notes.md', 'tools/README.md', '.github/policy.md',
    'docs/script.sh', 'docs/source.chibi', 'docs/image.svg',
    'docs/native-compiler-census.md', 'docs/distribution-licensing.md']) assert.equal(classify([name]), 'full', name);
});

test('native source must be in the actual exercised production closure', () => {
  for (const name of ['tools/compiler/native_driver/Main.chibi',
    'tools/compiler/native_driver/Compile.chibi', 'tools/compiler/package_compiler/Body.chibi',
    'tools/compiler/scalar_llvm/Emit.chibi', 'tools/compiler/llvm_native/Emit.chibi',
    'tools/compiler/source_parser/Parser.chibi', 'tools/compiler/json_string/JsonString.chibi',
    'tools/compiler/scalar_llvm/NewProductionFile.chibi', 'packages/core/strings/Strings.chibi',
    'test/native_aot/diagnostics/Probe.chibi', 'test/native_aot/diagnostics/run.sh',
    'test/native_aot/test_attributes/run.sh']) {
    assert.equal(classify([name]), 'native', name);
    assert.equal(classify(['docs/ci.md', name]), 'native', name);
  }
});

test('C/runtime/ABI/bootstrap/build/registration and unexecuted tests fail closed to full', () => {
  for (const name of ['src/execution/host.c', 'include/chibi/application.h',
    'tools/reflect/collect.c', 'tools/compiler/native_abi/Abi.chibi',
    'tools/compiler/native_driver/Catalog.chibi', 'tools/compiler/native_link/Start.chibi',
    'tools/compiler/package_compiler/Compile.chibi', 'tools/compiler/prepared_writer/Writer.chibi',
    'tools/compiler/asset_compiler/Compile.chibi', 'tools/compiler/unknown/New.chibi',
    'tools/compiler/runtime.json', 'tools/compiler/bootstrap/bootstrap.json',
    'packages/core/os/Os.chibi', 'packages/core/target/Target.chibi',
    'packages/core/time/Time.chibi', 'packages/core/bytes/Bytes.chibi',
    'packages/core/math/Math.chibi', 'packages/core/math/Spring.chibi',
    'packages/core/utf8/Utf8.chibi', 'packages/core/strings/Tests.chibi',
    'tools/compiler/scalar_llvm/Tests.chibi', 'tools/compiler/scalar_llvm/NewTest.chibi',
    'tools/compiler/source_parser/tests/Tests.chibi', 'test/compiler/samples/math/Tests.chibi',
    'test/native_aot/new_test/run.sh', 'tools/tests.tsv', 'tools/build.sh',
    'tools/toolchain/Driver.chibi', 'tools/graph/Graph.chibi', 'tools/bootstrap/hosts.tsv',
    'third_party/archives.tsv', 'Build.chibi', '.github/workflows/ci.yml', '.github/ci-policy.js']) {
    assert.equal(classify([name]), 'full', name);
    assert.equal(classify(['docs/ci.md', 'tools/compiler/native_driver/Main.chibi', name]), 'full', name);
  }
});

test('empty, malformed, oversized and ambiguous paths fail closed', () => {
  for (const paths of [null, {}, [], [null], [''], ['/docs/a.md'], ['./docs/a.md'],
    ['docs/../source.md'], ['docs//a.md'], ['docs/./a.md'], ['docs\\a.md'],
    ['docs/a.md\n'], ['docs/a.md\0'], ['docs/é.md'], ['docs/' + 'x'.repeat(4097) + '.md'],
    Array(10001).fill('README.md')]) assert.equal(classify(paths), 'full');
});

test('real Git diff includes both rename sides and missing ancestry defaults full', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'chibi-policy-test-'));
  const git = args => execFileSync('git', args, {cwd: directory, encoding: 'utf8'}).trim();
  const write = (name, content) => {
    fs.mkdirSync(path.dirname(path.join(directory, name)), {recursive: true});
    fs.writeFileSync(path.join(directory, name), content);
  };
  try {
    git(['init', '-q']); git(['config', 'user.name', 'CI test']);
    git(['config', 'user.email', 'ci-test@example.invalid']);
    write('src/runtime.c', 'old source'); write('README.md', 'old docs');
    git(['add', '.']); git(['commit', '-qm', 'base']);
    const base = git(['rev-parse', 'HEAD']);
    write('README.md', 'new docs'); git(['add', '.']); git(['commit', '-qm', 'docs']);
    assert.equal(fromCheckout(base, directory), 'static');
    write('tools/compiler/scalar_llvm/NewProductionFile.chibi', 'new native source');
    git(['add', '.']); git(['commit', '-qm', 'native']);
    assert.equal(fromCheckout(base, directory), 'native');
    fs.renameSync(path.join(directory, 'src/runtime.c'),
      path.join(directory, 'tools/compiler/scalar_llvm/Renamed.chibi'));
    git(['add', '-A']); git(['commit', '-qm', 'rename']);
    assert.equal(fromCheckout(base, directory), 'full');
    assert.equal(fromCheckout('f'.repeat(40), directory), 'full');
    assert.equal(fromCheckout('HEAD; echo unsafe', directory), 'full');
    assert.equal(fromCheckout(git(['rev-parse', 'HEAD']), directory), 'full');
  } finally { fs.rmSync(directory, {recursive: true, force: true}); }
});
