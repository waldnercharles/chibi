'use strict';
const {execFileSync} = require('node:child_process');

// Only production modules exercised by the current native compiler qualify.
// Unknown modules, tests, prepared/asset writers and ABI contracts take full CI.
const nativeModules = new Set(`compiler_syntax json_string lexical_binding llvm_native math_types
native_driver native_link native_link_host package_compiler package_graph package_paths
package_routes package_syntax scalar_facts scalar_llvm source_parser source_snapshot
source_tokenizer syntax_diagnostics`.split(/\s+/));
// The compiler directly imports os + strings; os has further core dependencies.
// Only strings is approved for this narrow profile; other core changes take full CI.
const pureCore = new Set(['packages/core/strings/Strings.chibi']);
const contracts = new Set([
  'tools/compiler/native_driver/Catalog.chibi',
  'tools/compiler/native_link/Start.chibi',
  'tools/compiler/package_compiler/Compile.chibi',
]);
const nativeFixtures = new Set([
  'test/native_aot/diagnostics/Probe.chibi', 'test/native_aot/diagnostics/run.sh',
  'test/native_aot/test_attributes/run.sh',
]);

function classify(paths) {
  if (!Array.isArray(paths) || paths.length === 0 || paths.length > 10000) return 'full';
  let profile = 'static';
  for (const name of paths) {
    if (typeof name !== 'string' || name.length > 4096 ||
        !/^[A-Za-z0-9_.\/-]+$/.test(name) ||
        name.split('/').some(part => !part || part === '.' || part === '..')) return 'full';
    // These Markdown files are consumed by runtime diagnostics or SDK packaging.
    if (name === 'docs/native-compiler-census.md' || name === 'docs/distribution-licensing.md') return 'full';
    if (name === 'README.md' || (name.startsWith('docs/') && name.endsWith('.md'))) continue;
    if (contracts.has(name)) return 'full';
    const module = /^tools\/compiler\/([a-z_]+)\/([A-Za-z0-9_]+\.chibi)$/.exec(name);
    if (pureCore.has(name) || nativeFixtures.has(name) ||
        (module && nativeModules.has(module[1]) && !/(?:Tests?|Cases)\.chibi$/.test(module[2]))) {
      profile = 'native';
    } else {
      return 'full';
    }
  }
  return profile;
}

function fromCheckout(base, cwd = process.cwd()) {
  try {
    if (!/^[0-9a-f]{40}$/.test(base)) return 'full';
    // Checkout fetch-depth:2 supplies the actual synthetic merge's first parent.
    // No rename inference: moving a C/ABI path into a fast directory remains full.
    const names = execFileSync('git', ['diff', '--name-only', '--no-renames', '-z', base, 'HEAD', '--'],
      {cwd, encoding: 'utf8', timeout: 3000, maxBuffer: 1048576, stdio: ['ignore', 'pipe', 'pipe']});
    if (!names.endsWith('\0')) return 'full';
    return classify(names.slice(0, -1).split('\0'));
  } catch {
    return 'full';
  }
}

module.exports = {classify, fromCheckout};
