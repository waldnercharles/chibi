import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import test from 'node:test';
import { decodeRequest, encodeRequest, MAX_OPTIONS_BYTES } from '../../tools/docs/playground/protocol.mjs';
import { needsProjectContext } from '../../tools/docs/playground/project-context.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const schema = JSON.parse(readFileSync(new URL('../../tools/compiler/source_format/options.json', import.meta.url), 'utf8'));
const definitions = schema.groups.flatMap((group) => group.items);
const browser = vm.runInNewContext(readFileSync(new URL('../../tools/docs/playground/options.js', import.meta.url), 'utf8') +
  '\n({ defaultOptions, optionByKey, profileOptions, configText, groups: OPTION_GROUPS, profiles: FORMAT_PROFILES })');
const plain = (value) => JSON.parse(JSON.stringify(value));
const defaults = Object.fromEntries(definitions.map((item) => [item.key, item.default]));

test('the complete option catalog has unique keys, native fields, valid defaults and versioned profiles', () => {
  assert.equal(new Set(definitions.map((item) => item.key)).size, definitions.length);
  assert.equal(new Set(definitions.map((item) => item.native.field)).size, definitions.length);
  for (const item of definitions) {
    assert.match(item.key, /^[a-z][a-z0-9_]*$/);
    assert.ok(item.help && item.label, item.key + ' needs user-facing help');
    if (item.native.values) {
      assert.deepEqual(Object.keys(item.native.values).sort(), item.choices.map((choice) => String(choice.value)).sort());
      assert.ok(Object.hasOwn(item.native.values, item.default), item.key + ' default must be a known style');
    } else if (item.native.type === 'bool') assert.equal(typeof item.default, 'boolean');
    else if (item.native.type === 'int') {
      assert.ok(Number.isSafeInteger(item.default));
      assert.ok((item.native.allowZero && item.default === 0) ||
        (item.default >= item.native.min && item.default <= item.native.max), item.key + ' default must be in range');
    } else assert.equal(typeof item.default, 'string');
  }
  assert.equal(new Set(schema.profiles.map((profile) => profile.id)).size, schema.profiles.length);
  for (const profile of schema.profiles) {
    assert.match(profile.id, /_v\d+$/);
    for (const key of Object.keys(profile.options)) assert.ok(Object.hasOwn(defaults, key), 'Unknown profile setting ' + key);
  }
});

test('generated browser controls and defaults agree with the owning native option schema', () => {
  assert.deepEqual(plain(browser.defaultOptions()), defaults);
  assert.deepEqual(plain(browser.groups), schema.groups.map((group) => ({ ...group,
    items: group.items.map(({ native, ...item }) => item) })));
  assert.deepEqual(plain(browser.profiles), schema.profiles);
  for (const item of definitions) assert.equal(browser.optionByKey(item.key).default, item.default);
  const check = spawnSync(process.execPath, ['tools/docs/playground/generate-options.mjs', '--check'], { cwd: root, encoding: 'utf8' });
  assert.equal(check.status, 0, check.stderr || check.stdout);
});

test('every default crosses the real request frame without truncation or dropped fields', () => {
  const encoded = encodeRequest('package p\n', browser.defaultOptions());
  assert.ok(encoded.indexOf(10) <= MAX_OPTIONS_BYTES);
  assert.deepEqual({ ...decodeRequest(encoded).options }, Object.fromEntries(Object.entries(defaults).map(([key, value]) => [key, String(value)])));
  assert.equal(needsProjectContext(defaults), false, 'sending the complete default catalog remains layout-only');
  assert.equal(needsProjectContext(decodeRequest(encoded).options), false, 'wire defaults remain layout-only');
});

test('profile exports retain their version and list only overrides against that profile', () => {
  for (const profile of schema.profiles) {
    const expected = { ...defaults, ...profile.options, profile: profile.id };
    const selected = plain(browser.profileOptions(profile.id));
    assert.deepEqual(selected, expected);
    const prefix = profile.id === 'current_v1' ? '' : 'profile=' + profile.id;
    assert.equal(browser.configText(selected), prefix);
    const changed = { ...selected, indent_width: selected.indent_width === 7 ? 6 : 7 };
    assert.equal(browser.configText(changed), [prefix, 'indent_width=' + changed.indent_width].filter(Boolean).join('\n'));
    // profileOptions returns fresh settings, so an edit cannot mutate the preset.
    assert.deepEqual(plain(browser.profileOptions(profile.id)), expected);
  }
});

test('native catalog acceptance fixture contains every current default exactly once', () => {
  const native = readFileSync(new URL('../../tools/compiler/source_format/tests/OptionCatalog.chibi', import.meta.url), 'utf8');
  const literal = /NativeCatalogDefaults :: \(\) -> string \{ return ("(?:[^"\\]|\\.)*"); \}/.exec(native);
  assert.ok(literal, 'Native catalog fixture is present');
  const expected = definitions.map((item) => `${item.key}=${item.default}`).join(' ');
  assert.equal(JSON.parse(literal[1]), expected, 'Update the native catalog fixture after changing options.json');
});
