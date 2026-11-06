import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { encodeImports, encodeRequest, decodeResult } from '../../tools/docs/playground/protocol.mjs';

const modulePath = process.env.CHIBI_FORMATTER_MODULE;
assert.ok(modulePath, 'Set CHIBI_FORMATTER_MODULE to the built wasm/engine.mjs');
const { default: createModule } = await import(pathToFileURL(resolve(modulePath)).href);

function invoke(engine, bytes) {
  const input = engine._malloc(Math.max(1, bytes.length));
  try {
    engine.HEAPU8.set(bytes, input);
    const status = engine._chibi_formatter_format(input, bytes.length);
    if (status !== 0) return { status };
    const start = engine._chibi_formatter_result();
    const end = start + engine._chibi_formatter_result_length();
    return { status, result: decodeResult(engine.HEAPU8.slice(start, end)) };
  } finally {
    engine._free(input);
  }
}

function format(engine, source, options = {}) {
  const response = invoke(engine, encodeRequest(source, options));
  assert.equal(response.status, 0);
  return response.result;
}

function setContext(engine, context) {
  if (context === null) { assert.equal(engine._chibi_formatter_set_context(0, 0), 0); return; }
  const bytes = new TextEncoder().encode(typeof context === 'string' ? context : JSON.stringify(context));
  const pointer = engine._malloc(bytes.length);
  try {
    engine.HEAPU8.set(bytes, pointer);
    assert.equal(engine._chibi_formatter_set_context(pointer, bytes.length), 0);
    // A following format must use the setter's copy, not this caller's buffer.
    engine.HEAPU8.fill(0, pointer, pointer + bytes.length);
  } finally { engine._free(pointer); }
}

function fixedPoint(engine, source, options) {
  const result = format(engine, source, options);
  assert.equal(result.ok, true, result.error);
  const repeated = format(engine, result.text, options);
  assert.equal(repeated.ok, true, repeated.error);
  assert.equal(repeated.text, result.text, 'formatting the formatted result is stable');
  assert.equal(result.formatted_html, repeated.original_html, 'token styles retain the highlighting of their resulting source');
  return result;
}

test('real Wasm formats and highlights source with the selected indentation', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () { x:=1\nif x>0 { x=x+1 }\n}\n';
  const expected = 'package p\nMain :: () {\n  x := 1\n  if x > 0 {\n    x = x + 1\n  }\n}\n';
  const result = format(engine, source, { indent_width: 2 });
  assert.equal(result.ok, true);
  assert.equal(result.text, expected);
  assert.match(result.original_html, /chibi-function/);
  assert.match(result.formatted_html, /chibi-keyword/);
  assert.equal(format(engine, expected, { indent_width: 2 }).text, expected);
});

test('real Wasm preserves Unicode and embedded NUL bytes in literals', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () { value:="café 👾 a\0b" }\n';
  const expected = 'package p\nMain :: () {\n    value := "café 👾 a\0b"\n}\n';
  const result = format(engine, source);
  assert.equal(result.ok, true);
  assert.equal(result.text, expected);
  assert.ok(result.original_html.includes('a\0b'));
  assert.ok(result.formatted_html.includes('a\0b'));
});

test('real Wasm publishes output larger than a foreign cstring buffer', async () => {
  const engine = await createModule();
  const comment = '// ' + 'a<&👾'.repeat(15000) + '\n';
  const source = 'package p\n' + comment + 'Main :: () {}\n';
  const result = format(engine, source);
  assert.equal(result.ok, true);
  assert.ok(result.text.includes(comment));
  assert.ok(result.formatted_html.length > 65536);
  assert.ok(result.formatted_html.includes('&lt;&amp;👾'));
});

test('real Wasm reports bad source and options, then accepts another request', async () => {
  const engine = await createModule();
  const rejected = format(engine, 'package p\nMain :: () {\n');
  assert.equal(rejected.ok, false);
  assert.match(rejected.error, /syntax errors/);
  assert.equal(format(engine, 'package p\nMain :: () {}\n', { indent_width: 0 }).ok, false);
  assert.equal(format(engine, 'package p\nMain :: () {}\n').ok, true);
  assert.equal(format(engine, '').text, '');
});

test('real Wasm recovers from a malformed transport request', async () => {
  const engine = await createModule();
  assert.notEqual(invoke(engine, new TextEncoder().encode('missing newline')).status, 0);
  assert.equal(format(engine, 'package p\nMain :: () {}\n').ok, true);
});

test('real Wasm preserves unbraced scope and applies independent brace styles', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () { if true return; while false break; }\n';
  const preserved = format(engine, source);
  assert.equal(preserved.ok, true);
  assert.match(preserved.text, /if true\n        return;/);
  assert.equal(format(engine, preserved.text).text, preserved.text);
  const options = { branch_braces: 'always', brace_placement: 'next_line' };
  const braced = format(engine, source, options);
  assert.equal(braced.ok, true);
  assert.match(braced.text, /Main :: \(\)\n\{/);
  assert.match(braced.text, /if true\n    \{\n        return;/);
  assert.match(braced.text, /while false\n    \{\n        break;/);
  assert.equal(format(engine, braced.text, options).text, braced.text);
  const omitted = format(engine, braced.text, { branch_braces: 'omit_single' });
  assert.equal(omitted.ok, true);
  assert.match(omitted.text, /if true\n        return;/);
  assert.ok(!omitted.text.includes('if true {'));
});

test('real Wasm prints resolved binding types and reclassifies inserted tokens', async () => {
  const engine = await createModule();
  const source = 'package p\nCount :: () -> i8 { return 2; }\nMain :: () { count := Count(); yes := true; }\n';
  const options = { binding_types: 'explicit' };
  const typed = format(engine, source, options);
  assert.equal(typed.ok, true);
  assert.match(typed.text, /count: i8 = Count\(\);/);
  assert.match(typed.text, /yes: bool = true;/);
  assert.match(typed.formatted_html, /chibi-builtin[^>]*>i8</);
  assert.match(typed.formatted_html, /chibi-builtin[^>]*>bool</);
  assert.equal(format(engine, typed.text, options).text, typed.text);
  const inferred = format(engine, typed.text, { binding_types: 'omit_redundant' });
  assert.equal(inferred.ok, true);
  assert.match(inferred.text, /count := Count\(\);/);
  assert.match(inferred.text, /yes := true;/);
});

test('real Wasm retains contextual numeric types while omitting independent redundant ones', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () { narrow: i8 = 1; yes: bool = true; }\n';
  const typed = format(engine, source, { binding_types: 'omit_redundant' });
  assert.equal(typed.ok, true);
  assert.match(typed.text, /narrow: i8 = 1;/);
  assert.match(typed.text, /yes := true;/);
  assert.ok(typed.notes, 'retained annotation is explained');
});

test('real Wasm keeps layout available when type analysis lacks an imported package', async () => {
  const engine = await createModule();
  const source = 'package p\nimport missing "project:missing"\nMain :: () { value:=missing.Count(); }\n';
  const typed = format(engine, source, { binding_types: 'explicit' });
  assert.equal(typed.ok, true);
  assert.match(typed.text, /value := missing.Count\(\);/);
  assert.match(typed.notes, /not applied/);
  assert.equal(format(engine, 'package p\nMain :: () {}\n').ok, true);
});

test('real Wasm type edits use byte ranges after multibyte literals', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () { word := "café 👾"; yes := true; }\n';
  const options = { binding_types: 'explicit' };
  const typed = format(engine, source, options);
  assert.equal(typed.ok, true);
  assert.match(typed.text, /word: string = "café 👾";/);
  assert.match(typed.text, /yes: bool = true;/);
  assert.equal(format(engine, typed.text, options).text, typed.text);
});

test('real Wasm import metadata decodes source paths, follows both conditional branches and excludes foreign libraries', async () => {
  const engine = await createModule();
  const source = 'package p\nimport common "core:str\\u0069ngs"\nforeign import libc "system:c"\n' +
    'when true { import selected "project:one" } else { import other "../two" }\n';
  const response = invoke(engine, encodeImports(source));
  assert.equal(response.status, 0);
  assert.equal(response.result.ok, true, response.result.error);
  assert.deepEqual(JSON.parse(response.result.text), ['core:strings', 'project:one', '../two']);
  const invalid = invoke(engine, encodeImports('package p\nMain :: () {'));
  assert.equal(invalid.status, 0);
  assert.equal(invalid.result.ok, false);
  assert.equal(invoke(engine, encodeImports('package p\n')).result.text, '[]');
});

test('real Wasm analyzes package siblings and imported documents while replacing the edited source', async () => {
  const engine = await createModule();
  const source = 'package p\nimport dep "project:dep"\nMain :: () { count:=dep.Count(); yes:=Companion(); }\n' +
    'Done :: () {}\nInvoke :: () { Done(); }\nIgnored :: () { dep.Count(); }\n';
  const project = {
    project: '/repo/compiler', collection_names: ['core', 'graph'],
    collection_paths: ['/repo/packages/core', '/repo/tools/graph'],
    paths: ['/repo/compiler/p/Main.chibi', '/repo/compiler/p/Companion.chibi', '/repo/compiler/dep/Dep.chibi'],
    sources: ['package p\nMain :: () { obsolete := false; }\n',
      'package p\nCompanion :: () -> bool { return true; }\n',
      'package dep\npub Count :: () -> i8 { return 2; }\n'],
  };
  setContext(engine, project);
  const options = { binding_types: 'explicit', function_body: 'expression', function_keyword: 'always' };
  const result = format(engine, source, options);
  assert.equal(result.ok, true, result.error);
  assert.match(result.text, /count: i8 = dep.Count\(\);/);
  assert.match(result.text, /yes: bool = Companion\(\);/);
  assert.match(result.text, /Invoke :: fn\(\) => Done\(\);/);
  assert.match(result.text, /Ignored :: fn\(\) \{\n    dep.Count\(\);\n\}/);
  assert.match(result.formatted_html, /chibi-builtin[^>]*>i8</);
  assert.equal(format(engine, result.text, options).text, result.text);
  // A rejected oversized setter leaves the last valid project intact.
  assert.notEqual(engine._chibi_formatter_set_context(0, 16 * 1024 * 1024 + 1), 0);
  assert.match(format(engine, source, { binding_types: 'explicit' }).text, /count: i8/);
  setContext(engine, null);
  const closed = format(engine, source, { binding_types: 'explicit' });
  assert.equal(closed.ok, true);
  assert.match(closed.notes, /not applied|unavailable|Cannot/);
  assert.ok(!closed.text.includes('count: i8'));
});

test('real Wasm rejects invalid project contexts without losing transport recovery', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () { value:=true; }\n';
  const malformed = ['{', '{}', { project: '/repo', collection_names: [], collection_paths: [],
    paths: ['/repo/Main.chibi'], sources: [] }, { project: '/repo', collection_names: [], collection_paths: [],
    paths: ['/repo/Main.chibi', '/repo/Main.chibi'], sources: [source, source] }];
  for (const context of malformed) {
    setContext(engine, context);
    const rejected = format(engine, source, { binding_types: 'explicit' });
    assert.equal(rejected.ok, false);
    assert.match(rejected.error, /context|Duplicate/i);
  }
  setContext(engine, null);
  assert.equal(format(engine, source).ok, true);
  const missing = { project: '/repo', collection_names: [], collection_paths: [],
    paths: ['/repo/Main.chibi'], sources: [source] };
  setContext(engine, missing);
  const unavailable = format(engine, 'package p\nimport dep "project:missing"\nMain :: () { value:=dep.Count(); }\n',
    { binding_types: 'explicit' });
  assert.equal(unavailable.ok, true);
  assert.match(unavailable.notes, /not applied|unavailable|Cannot/);
  setContext(engine, null);
  assert.equal(format(engine, source).ok, true);
});

test('real Wasm accepts the complete option catalog through the extended header', async () => {
  const engine = await createModule();
  const schema = JSON.parse(readFileSync(new URL('../../tools/compiler/source_format/options.json', import.meta.url), 'utf8'));
  const defaults = Object.fromEntries(schema.groups.flatMap((group) => group.items).map((item) => [item.key, item.default]));
  assert.equal(format(engine, 'package p\n', defaults).ok, true);
  // Repeated valid options exercise a >4 KiB header without requesting any
  // unrelated formatting policies or relying on the current catalog's size.
  const header = 'indent_width=4 '.repeat(350).trim();
  assert.ok(header.length > 4096 && header.length < 32768);
  const response = invoke(engine, new TextEncoder().encode(header + '\npackage p\n'));
  assert.equal(response.status, 0);
  assert.equal(response.result.ok, true);
});

test('real Wasm applies all four versioned profiles to a complete program at a fixed point', async () => {
  const engine = await createModule();
  const source = 'package p\nDouble::(value:int)->int{return value*2;}\n' +
    'Main::(){first:int=2;longer:int=Double(first);if(longer>first){longer-=1;}assert(longer==3,"same value");}\n';
  const outputs = new Map();
  for (const profile of ['current_v1', 'compact_v1', 'expanded_v1', 'tabular_v1']) {
    const result = fixedPoint(engine, source, { profile });
    assert.ok(result.text.includes('same value'));
    assert.match(result.formatted_html, /chibi-function/);
    outputs.set(profile, result.text);
  }
  assert.match(outputs.get('compact_v1'), /=>/);
  assert.ok(!outputs.get('compact_v1').includes('first: int'));
  assert.match(outputs.get('expanded_v1'), /Double :: fn\(/);
  assert.match(outputs.get('expanded_v1'), /\n\{\n/);
  assert.equal(new Set(outputs.values()).size, 4, 'profiles produce visibly distinct layouts');
});

test('real Wasm combines foreign and when indentation with function keyword, arrow and body styles', async () => {
  const engine = await createModule();
  const source = 'package p\nforeign { External :: (value:int)->int }\n' +
    'when true { Enabled:int:1; } else { Disabled:int:2; }\n' +
    'Add :: (left:int,right:int)->int { return left+right; }\nMain :: () {}\n';
  const result = fixedPoint(engine, source, { indent_foreign_blocks: true, indent_when_blocks: true,
    function_keyword: 'always', return_arrow: 'omit', function_body: 'expression' });
  assert.match(result.text, /foreign \{\n    External/);
  assert.match(result.text, /when true \{\n    Enabled/);
  assert.match(result.text, /else \{\n    Disabled/);
  assert.match(result.text, /Add :: fn\(left: int, right: int\) int => left \+ right;/);
  const blocks = fixedPoint(engine, result.text, { function_body: 'block', function_keyword: 'omit', return_arrow: 'always' });
  assert.match(blocks.text, /Add :: \(left: int, right: int\) -> int \{/);
  assert.match(blocks.text, /return left \+ right;/);
});

test('real Wasm follows SEGMENT object overlaps, similarity refinement and singleton merging', async () => {
  const engine = await createModule();
  const cases = [
    [
      't: Tree = {};\nt.getNodeTable().addColumns(LABEL_SCHEMA);\nr: Node = t.addRoot();\nr.setString(LABEL, "0,0");',
      '    t: Tree = {};\n    t.getNodeTable().addColumns(LABEL_SCHEMA);\n\n    r: Node = t.addRoot();\n    r.setString(LABEL, "0,0");',
    ],
    [
      'strTmp = 1;\nstrMsg = 2;\nsrcPixels = 3;\ndstInPixels = 4;',
      '    strTmp = 1;\n    strMsg = 2;\n\n    srcPixels = 3;\n    dstInPixels = 4;',
    ],
    [
      'label1: JLabel = {text: "Search Name:"};\nsearchNameField = {size: 12};\nlabel2: JLabel = {text: "Search Type:"};',
      '    label1: JLabel = {text: "Search Name:"};\n    searchNameField = {size: 12};\n    label2: JLabel = {text: "Search Type:"};',
    ],
  ];
  for (const [body, expected] of cases) {
    const result = fixedPoint(engine, `package p\nMain :: () {\n${body}\n}\n`, { statement_paragraphs: 'logical' });
    assert.equal(result.text, `package p\nMain :: () {\n${expected}\n}\n`);
  }
});

test('real Wasm logical grouping is stable across project code and a large uniform run', async () => {
  const engine = await createModule();
  for (const path of [
    '../../packages/core/bytes/Bytes.chibi',
    '../../packages/core/bytes/Tests.chibi',
    '../../apps/host/Host.chibi',
    '../../apps/chibi/Projects.chibi',
    '../../tools/compiler/source_format/SegmentActions.chibi',
    '../../tools/compiler/source_format/SegmentFacts.chibi',
  ]) {
    const source = readFileSync(new URL(path, import.meta.url), 'utf8');
    fixedPoint(engine, source, { statement_paragraphs: 'logical' });
  }
  const body = Array.from({length: 5000}, (_, index) => `value${index} := ${index};`).join('\n');
  const result = fixedPoint(engine, `package p\nMain :: () {\n${body}\n}\n`, { statement_paragraphs: 'logical' });
  assert.ok(!result.text.includes('\n\n'), 'a uniform SynS run has no arbitrary paragraph size limit');
});

test('real Wasm composes mixed parentheses, per-list commas, alignment, literals and prose reflow', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () {\nfirst := 1234567;\nlong_name := 20;\n' +
    'calc := alpha * beta + gamma * delta;\nvalues := [first, long_name];\n' +
    'record := {short: first, much_longer: long_name};\nCall(first, long_name);\n' +
    'text := "café";\n// This paragraph explains the formatting choices while keeping its useful words together for the reader.\n}\n';
  const options = { expression_parens: 'readability', readability_mixed_operators: true,
    arguments_layout: 'vertical', arrays_layout: 'vertical', records_layout: 'vertical',
    trailing_commas_arguments: 'multiline', trailing_commas_arrays: 'omit', trailing_commas_records: 'multiline',
    assignment_alignment: 'consecutive', record_value_alignment: 'consecutive', alignment_max_padding: 24,
    numeric_separators: 'group', string_escapes: 'ascii', comment_reflow: 'prose', comment_width: 48 };
  const result = fixedPoint(engine, source, options);
  assert.match(result.text, /first\s+:= 1_234_567;/);
  assert.match(result.text, /\(alpha \* beta\) \+ \(gamma \* delta\)/);
  assert.match(result.text, /Call\(\n\s*first,\n\s*long_name,\n\s*\);/);
  const array = /values\s*:= (\[[\s\S]*?\]);/.exec(result.text)?.[1];
  assert.ok(array);
  assert.match(array, /long_name\n\s*\]/);
  assert.match(result.text, /much_longer:\s+long_name,\n\s*\}/);
  assert.ok(result.text.includes('"caf\\u00e9"'));
  assert.match(result.text, /\/\/ This paragraph[^\n]*\n    \/\/ /);
  assert.match(result.formatted_html, /1_234_567/);
  const unindented = 'package p\nMain :: () {\n// aaaa bbbb cccc dddd eeee ffff gggg hhhh.\n}\n';
  const wrapped = fixedPoint(engine, unindented, { comment_reflow: 'prose', comment_width: 40 });
  assert.match(wrapped.text, /    \/\/ aaaa bbbb cccc dddd eeee ffff\n    \/\/ gggg hhhh\./);
});

test('real Wasm resolves parameter and result policies through target-dependent project imports', async () => {
  const engine = await createModule();
  const source = 'package p\nimport dep "project:dep"\nChoose :: (value = 3) { return dep.Count(); }\n' +
    'Main :: () { answer := Choose(); }\n';
  const context = { project: '/formatter', collection_names: ['core'], collection_paths: ['/core'],
    paths: ['/formatter/p/Main.chibi', '/formatter/dep/Dep.chibi', '/core/target/Target.chibi'],
    sources: [source, 'package dep\nimport target "core:target"\n' +
      'when target.Os == target.Linux { pub Count :: () -> i8 { return 4; } } else { pub Count :: () -> f64 { return 4; } }\n',
    'package target\npub Linux: int : 1\npub Os: int : 0\n'] };
  setContext(engine, context);
  for (const [analysis_target, type] of [['linux64', 'i8'], ['windows64', 'f64']]) {
    const explicit = fixedPoint(engine, source, { analysis_target, semantic_targets: 'selected', parameter_types: 'explicit', return_types: 'explicit', binding_types: 'explicit' });
    assert.match(explicit.text, new RegExp('Choose :: \\(value: int = 3\\) -> ' + type));
    assert.ok(explicit.text.includes('answer: ' + type + ' = Choose();'));
    const inferred = fixedPoint(engine, explicit.text, { analysis_target, semantic_targets: 'selected', parameter_types: 'omit_redundant', return_types: 'omit_redundant' });
    assert.match(inferred.text, /Choose :: \(value = 3\) \{/);
  }
  setContext(engine, null);
});

test('real Wasm preserves cross-platform inferred types by default and allows an explicit selected-target style', async () => {
  const engine = await createModule();
  const source = 'package p\nimport target "core:target"\n' +
    'when target.Os == target.Linux { Count :: () -> i8 { return 4; } } else { Count :: () -> f64 { return 4; } }\n' +
    'Main :: () { value := Count(); }\n';
  setContext(engine, { project: '/formatter', collection_names: ['core'], collection_paths: ['/core'],
    paths: ['/formatter/Main.chibi', '/core/target/Target.chibi'],
    sources: [source, 'package target\npub Linux: int : 1\npub Os: int : 0\n'] });
  const portable = fixedPoint(engine, source, { binding_types: 'explicit' });
  assert.match(portable.text, /value := Count\(\);/);
  assert.ok(!portable.text.includes('value: i8'));
  assert.match(portable.notes, /target|platform/i);
  const selected = fixedPoint(engine, source, { binding_types: 'explicit', analysis_target: 'windows64', semantic_targets: 'selected' });
  assert.match(selected.text, /value: f64 = Count\(\);/);
  setContext(engine, null);
});

test('real Wasm indents when-contained functions after converting them to expression syntax', async () => {
  const engine = await createModule();
  const source = 'package p\nwhen true { Enabled :: () -> int { return 1; } } else { Disabled :: () -> int { return 2; } }\n';
  const result = fixedPoint(engine, source, { indent_when_blocks: true, function_keyword: 'always',
    return_arrow: 'omit', function_body: 'expression' });
  assert.match(result.text, /when true \{\n    Enabled :: fn\(\) int => 1;/);
  assert.match(result.text, /else \{\n    Disabled :: fn\(\) int => 2;/);
});

test('real Wasm readability grouping distinguishes mixed equal-precedence operators and reclassifies leaves', async () => {
  const engine = await createModule();
  const source = 'package p\nMain :: () { value := a - b + c; text := "retained string"; }\n';
  const result = fixedPoint(engine, source, { expression_parens: 'readability', readability_mixed_operators: true });
  assert.match(result.text, /\(a - b\) \+ c/);
});

test('real Wasm converts proven void calls while preserving ignored non-void results', async () => {
  const engine = await createModule();
  const source = 'package p\nDone :: () {}\nInvoke :: () { Done(); }\n' +
    'Read :: () -> int { return 7; }\nIgnored :: () { Read(); }\n';
  const result = fixedPoint(engine, source, { function_body: 'expression', function_keyword: 'always' });
  assert.match(result.text, /Invoke :: fn\(\) => Done\(\);/);
  assert.match(result.text, /Ignored :: fn\(\) \{\n    Read\(\);\n\}/);
  assert.match(result.notes, /stayed as blocks/);
});

test('real Wasm keeps a legal function named fn callable from a constant initializer', async () => {
  const engine = await createModule();
  const source = 'package p\nfn :: (value: int) -> int { return value; }\nValue :: fn(1);\n';
  const result = fixedPoint(engine, source, {});
  assert.match(result.text, /Value :: fn\(1\);/);
});
