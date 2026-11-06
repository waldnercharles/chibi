import assert from 'node:assert/strict';
import test from 'node:test';
import { ContextUnavailable, needsProjectContext, ProjectContext } from '../../tools/docs/playground/project-context.mjs';
import { contextPath, decodeRequest, encodeImports, encodeRequest, MAX_OPTIONS_BYTES } from '../../tools/docs/playground/protocol.mjs';
import { FormatterClient } from '../../tools/docs/playground/client.mjs';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const base = 'https://example.github.io/chibi/formatter/';
const decode = (value) => JSON.parse(decoder.decode(value.bytes));

// The loader consumes compiler metadata as an API. These opaque source names
// deliberately cannot be parsed by a JavaScript approximation of Chibi syntax.
function fixture(files, metadata, { extra = [], responses = new Map(), ...bounds } = {}) {
  const requested = [];
  const inspected = [];
  const records = (paths) => paths.map((path) => ({ path, bytes: encoder.encode(files[path]).length }));
  const extraSet = new Set(extra);
  const fetch = async (url) => {
    requested.push(url.href);
    if (responses.has(url.pathname)) return responses.get(url.pathname)();
    if (url.href === new URL('files.json', base).href) {
      return new Response(JSON.stringify({ files: records(Object.keys(files).filter((path) => !extraSet.has(path))) }));
    }
    if (url.href === new URL('context_files.json', base).href) {
      return extra.length ? new Response(JSON.stringify({ files: records(extra) })) : new Response('', { status: 404 });
    }
    const prefix = new URL('sources/', base).href;
    if (!url.href.startsWith(prefix)) throw new Error('escaped site');
    const path = url.href.slice(prefix.length).split('/').map(decodeURIComponent).join('/');
    return Object.hasOwn(files, path) ? new Response(files[path]) : new Response('', { status: 404 });
  };
  const imports = async (source) => {
    inspected.push(source);
    const result = metadata[source];
    if (result instanceof Error) throw result;
    return result ?? [];
  };
  return { loader: new ProjectContext({ base, fetch, imports, ...bounds }), requested, inspected };
}

test('project context is requested only by compiler-dependent policies', () => {
  for (const options of [{}, { indent_width: 2 }, { profile: 'expanded_v1' }, { imports: 'preserve' },
    { binding_types: 'preserve', local_types: 'inherit', type_spelling: 'preserve_aliases', numeric_base: 'preserve' },
    { function_body: 'block' }, { analysis_target: 'windows64' }, { semantic_targets: 'selected' }]) {
    assert.equal(needsProjectContext(options), false);
  }
  for (const options of [{ binding_types: 'explicit' }, { local_types: 'omit_redundant' },
    { constant_types: 3 }, { parameter_types: 'evident' }, { return_types: 1 }, { member_types: 'explicit' },
    { public_types: 'explicit' }, { type_spelling: 'canonical' }, { function_body: 'expression' },
    { imports: 'sort' }, { imports: 1 }, { imports: 'organize' }, { numeric_base: 'hex' }, { profile: 'compact_v1' }]) {
    assert.equal(needsProjectContext(options), true);
  }
  assert.equal(needsProjectContext({ profile: 'compact_v1', binding_types: 'preserve', function_body: 'preserve' }), false);
});

test('project fetch retains the worker global receiver', async () => {
  const path = 'tools/compiler/p/Main.chibi';
  const fetch = async function (url) {
    assert.equal(this, globalThis, 'browser fetch must not receive the ProjectContext instance');
    if (url.href.endsWith('/files.json')) return new Response(JSON.stringify({ files: [{ path, bytes: 4 }] }));
    return new Response('', { status: 404 });
  };
  const loader = new ProjectContext({ base, fetch, imports: () => [] });
  assert.equal((await loader.load({ path }, 'main')).files, 1);
});

test('wire metadata requests retain source bytes and full option catalogs fit the header bound', () => {
  const source = 'package café\n\0é';
  assert.deepEqual({ ...decodeRequest(encodeRequest(source, { binding_types: 'explicit', use_tabs: false })).options },
    { binding_types: 'explicit', use_tabs: 'false' });
  assert.equal(decodeRequest(encodeRequest(source, {})).source, source);
  assert.equal(decoder.decode(encodeImports(source)), '!imports\n' + source);
  const options = Object.fromEntries(Array.from({ length: 200 }, (_, index) => ['option_' + index, 'long_style_preference']));
  assert.ok(encodeRequest(source, options).length > 4096);
  assert.equal(MAX_OPTIONS_BYTES, 32768);
  assert.equal(decodeRequest(encodeRequest('', { data_column_alignment: 'left,right,auto' })).options.data_column_alignment, 'left,right,auto');
  assert.throws(() => encodeRequest('', { numeric_base: 'left,right' }), /Invalid formatter option/);
  assert.throws(() => encodeRequest('', { data_column_alignment: 'left, right' }), /Invalid formatter option/);
  assert.throws(() => encodeRequest('', { data_column_alignment: Array(33).fill('auto').join(',') }), /Invalid formatter option/);
  assert.throws(() => decodeRequest(encoder.encode('bad-header\nsource')), /request options/);
  assert.throws(() => decodeRequest(new Uint8Array([10, 255])), /encoded data/);
});

test('closure includes package siblings and project, relative, core and graph imports without unrelated files', async () => {
  const files = {
    'tools/compiler/example/Main.chibi': 'disk main',
    'tools/compiler/example/Support.chibi': 'support',
    'tools/compiler/example/child/Child.chibi': 'child',
    'tools/compiler/shared/Shared.chibi': 'shared',
    'tools/compiler/lib/Lib.chibi': 'lib',
    'packages/core/strings/Strings.chibi': 'strings',
    'packages/core/target/Target.chibi': 'target',
    'tools/graph/files/Files.chibi': 'graph files',
    'tools/graph/files/child/Child.chibi': 'graph child',
    'tools/compiler/unrelated/Other.chibi': 'unrelated',
  };
  const { loader, requested, inspected } = fixture(files, {
    edited: ['./child', 'project:lib', 'core:strings', 'graph:files'],
    support: ['../shared'], lib: ['core:target'], target: ['../strings'],
    'graph files': ['./child'], 'graph child': ['graph:files'],
  });
  const result = await loader.load({ path: 'tools/compiler/example/Main.chibi' }, 'edited');
  const context = decode(result);
  assert.equal(result.files, 9);
  assert.equal(context.project, '/repo/tools/compiler');
  assert.deepEqual(context.collection_names, ['core', 'graph']);
  assert.deepEqual(context.collection_paths, ['/repo/packages/core', '/repo/tools/graph']);
  assert.equal(context.paths[0], '/repo/tools/compiler/example/Main.chibi');
  assert.equal(context.sources[0], 'edited');
  assert.ok(context.paths.includes('/repo/tools/compiler/shared/Shared.chibi'));
  assert.ok(context.paths.includes('/repo/tools/graph/files/child/Child.chibi'));
  assert.equal(new Set(context.paths).size, context.paths.length);
  assert.equal(requested.some((url) => url.includes('Main.chibi') || url.includes('unrelated')), false);
  assert.equal(inspected.includes('disk main'), false);
  assert.equal(inspected.includes('unrelated'), false);
  assert.ok(requested.every((url) => url.startsWith(base)));
});

test('conditional import metadata is followed conservatively and extra compiler sources augment the index', async () => {
  const { loader } = fixture({
    'tools/compiler/p/Main.chibi': 'main',
    'tools/compiler/windows/Windows.chibi': 'windows',
    'tools/compiler/posix/Posix.chibi': 'posix',
    'tools/compiler/posix/New.chibi': 'new untracked source',
  }, { edited: ['project:windows', 'project:posix'] }, { extra: ['tools/compiler/posix/New.chibi'] });
  const context = decode(await loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'edited'));
  assert.equal(context.paths.length, 4);
  assert.ok(context.sources.includes('windows'));
  assert.ok(context.sources.includes('posix'));
  assert.ok(context.sources.includes('new untracked source'));
});

test('normal dependency packages exclude adjacent tests while an edited test keeps its package helpers', async () => {
  const { loader, requested } = fixture({
    'tools/compiler/p/Main.chibi': 'main', 'tools/compiler/p/ExtraTests.chibi': 'selected tests',
    'tools/compiler/lib/Lib.chibi': 'library', 'tools/compiler/lib/LibTests.chibi': 'dependency tests',
    'tools/compiler/p/tests/Case.chibi': 'case', 'tools/compiler/p/tests/Tests.chibi': 'helpers',
  }, { main: ['project:lib'], 'selected tests': ['network:unrelated'], 'dependency tests': ['project:missing'],
    'edited tests': ['project:lib'] });
  const normal = decode(await loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'main'));
  assert.equal(normal.paths.length, 2);
  assert.equal(requested.some((url) => url.endsWith('Tests.chibi')), false);
  const selected = decode(await loader.load({ path: 'tools/compiler/p/ExtraTests.chibi' }, 'edited tests'));
  assert.equal(selected.paths.length, 3);
  assert.ok(selected.paths.includes('/repo/tools/compiler/p/Main.chibi'));
  assert.equal(selected.paths.some((path) => path.endsWith('/LibTests.chibi')), false);
  const helper = decode(await loader.load({ path: 'tools/compiler/p/tests/Case.chibi' }, 'case'));
  assert.ok(helper.sources.includes('helpers'));
});

test('static dependencies and serialized contexts are reused, while changed imports rebuild the closure', async () => {
  const { loader, requested, inspected } = fixture({
    'tools/compiler/p/Main.chibi': 'main',
    'tools/compiler/a/A.chibi': 'a',
    'tools/compiler/b/B.chibi': 'b',
  }, { first: ['project:a'], 'body changed': ['project:a'], 'imports changed': ['project:b'] });
  const first = await loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'first');
  const again = await loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'first');
  const body = await loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'body changed');
  assert.strictEqual(first.bytes, again.bytes);
  assert.strictEqual(first.bytes, body.bytes);
  assert.equal(inspected.filter((source) => source === 'first').length, 1);
  assert.equal(requested.filter((url) => url.endsWith('/a/A.chibi')).length, 1);
  const changed = decode(await loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'imports changed'));
  assert.ok(changed.paths.includes('/repo/tools/compiler/b/B.chibi'));
  assert.equal(changed.paths.includes('/repo/tools/compiler/a/A.chibi'), false);
  assert.equal(requested.filter((url) => url.endsWith('files.json')).length, 2);
});

test('selected core and graph documents have distinct project roots, and fixture project imports stay local', async () => {
  for (const path of ['packages/core/strings/Strings.chibi', 'tools/graph/files/Files.chibi']) {
    const { loader } = fixture({ [path]: 'source' }, {});
    assert.equal(decode(await loader.load({ path }, 'source')).project, '/repo');
  }
  const formatter = fixture({ 'tools/docs/playground/wasm/Main.chibi': 'formatter',
    'tools/compiler/source_format/Format.chibi': 'format' }, { formatter: ['project:source_format'] });
  const formatterContext = decode(await formatter.loader.load({ path: 'tools/docs/playground/wasm/Main.chibi' }, 'formatter'));
  assert.equal(formatterContext.project, '/repo/tools/compiler');
  assert.equal(formatterContext.paths.length, 2);
  assert.ok(formatterContext.collection_paths.includes('/repo/tools/docs/playground/wasm'));
  const { loader } = fixture({
    'test/native_aot/example/Main.chibi': 'main', 'test/native_aot/example/dep/Dep.chibi': 'dep',
  }, { edited: ['project:dep'] });
  const context = decode(await loader.load({ path: 'test/native_aot/example/Main.chibi' }, 'edited'));
  assert.equal(context.project, '/repo/test/native_aot/example');
  assert.equal(context.paths.length, 2);
});

test('missing imports, unknown collections, escapes and invalid compiler metadata produce precise context failures', async () => {
  const files = { 'tools/compiler/p/Main.chibi': 'main' };
  for (const [imports, pattern] of [
    [['project:missing'], /Imported package.*missing/], [['network:module'], /Unknown import collection/],
    [['../../../outside'], /escapes its collection/], [['core:../outside'], /escapes its collection/],
    [['/outside'], /Invalid import path/], [['core:bad\0path'], /Invalid import path/],
    [[17], /Invalid compiler import metadata/], [new Error('draft source is invalid'), /draft source is invalid/],
  ]) {
    const { loader } = fixture(files, { edited: imports });
    await assert.rejects(loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'edited'), (error) =>
      error instanceof ContextUnavailable && pattern.test(error.message));
  }
});

test('context bounds stop package expansion and serialized growth', async () => {
  const files = { 'tools/compiler/p/Main.chibi': 'main', 'tools/compiler/p/One.chibi': 'one',
    'tools/compiler/p/Two.chibi': 'two' };
  const limited = fixture(files, {}, { maxFiles: 2 });
  await assert.rejects(limited.loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'edited'), /exceeds 2 files/);
  const bytes = fixture({ 'tools/compiler/p/Main.chibi': 'main' }, {}, { maxBytes: 80 });
  await assert.rejects(bytes.loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'edited'), /Serialized project context/);
  assert.equal(limited.requested.some((url) => url.endsWith('.chibi')), false);
});

test('stale static indexes and fetch failures are recoverable and never cache a partial context', async () => {
  let failing = true;
  const { loader } = fixture({
    'tools/compiler/p/Main.chibi': 'main', 'tools/compiler/dep/Dep.chibi': 'dep',
  }, { edited: ['project:dep'] }, { responses: new Map([['/chibi/formatter/sources/tools/compiler/dep/Dep.chibi', () =>
    failing ? new Response('outage', { status: 503 }) : new Response('dep')]]) });
  await assert.rejects(loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'edited'), /HTTP 503/);
  failing = false;
  assert.equal((await loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'edited')).files, 2);
  const stale = fixture({ 'tools/compiler/p/Main.chibi': 'main', 'tools/compiler/dep/Dep.chibi': 'dep' },
    { edited: ['project:dep'] }, { responses: new Map([['/chibi/formatter/sources/tools/compiler/dep/Dep.chibi', () => new Response('different')]]) });
  await assert.rejects(stale.loader.load({ path: 'tools/compiler/p/Main.chibi' }, 'edited'), /changed since its index/);
});

test('client keeps project references with the correct queued request and rejects unsafe paths', async () => {
  const messages = [];
  const worker = { postMessage(message) { messages.push(message); }, terminate() {} };
  const client = new FormatterClient({ makeWorker: () => worker });
  const first = client.format('first', {}, { path: 'apps/first/Main.chibi' });
  const second = client.format('second', {}, { path: 'apps/second/Main.chibi' });
  assert.deepEqual(messages[0].contextRef, { path: 'apps/first/Main.chibi' });
  const reply = (text) => worker.onmessage({ data: { type: 'result', id: messages.at(-1).id,
    result: { ok: true, text, original_html: '', formatted_html: '' } } });
  reply('first');
  assert.deepEqual(messages[1].contextRef, { path: 'apps/second/Main.chibi' });
  reply('second');
  await Promise.all([first, second]);
  for (const path of ['../secret.chibi', '/secret.chibi', 'x/../secret.chibi', 'x\\secret.chibi', 'x/source.txt']) {
    await assert.rejects(client.format('source', {}, { path }), /Invalid/);
  }
  assert.equal(contextPath(undefined), null);
  assert.equal(messages.length, 2);
});
