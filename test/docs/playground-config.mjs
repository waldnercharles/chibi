import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';
import { exportConfig, importConfig, normalizeOption, optionError } from '../../tools/docs/playground/config.mjs';

// Test the actual generated catalog, so additions do not create a second schema.
const catalog = vm.runInNewContext(fs.readFileSync(new URL('../../tools/docs/playground/options.js', import.meta.url), 'utf8') +
  '; ({ items: OPTION_GROUPS.flatMap(group => group.items), profileOptions, profiles: FORMAT_PROFILES })');
const current = () => ({ ...catalog.profileOptions('current_v1') });
const parse = (text, extra = {}) => importConfig(text, {
  format: 'editorconfig', path: 'apps/host/Host.chibi', items: catalog.items,
  profileOptions: catalog.profileOptions, currentOptions: current(), ...extra,
});
const values = (options, format) => exportConfig(options, { format, items: catalog.items, profileOptions: catalog.profileOptions });
const matched = (glob, path) => parse(`[${glob}]\nindent_size = 3`, { path }).options?.indent_width === 3;

test('both config formats round-trip every profile and explicit default-valued overrides', () => {
  for (const profile of catalog.profiles) {
    const options = { ...catalog.profileOptions(profile.id), indent_width: 3, use_tabs: true,
      preferred_width: 0, final_newline: false, line_ending: 'crlf', data_column_alignment: 'left,right,auto' };
    for (const format of ['chibi', 'editorconfig']) {
      const text = values(options, format), result = parse(text, { format });
      assert.equal(result.ok, true, JSON.stringify(result.diagnostics));
      assert.deepEqual({ ...result.options }, options, `${profile.id}: ${format}`);
      assert.match(text, /profile\s*=\s*\w+_v1/);
    }
  }
});

test('the entire generated catalog uses supported kinds and valid defaults', () => {
  for (const item of catalog.items) {
    assert.equal(['bool', 'choice', 'number', 'text'].includes(item.kind), true, `${item.key}: ${item.kind}`);
    assert.equal(optionError(item, item.default), '', item.key);
  }
  assert.match(optionError({ kind: 'boolean' }, true), /unsupported option kind/);
});

test('EditorConfig common values are case-insensitive and resolve indent_size=tab after all pairs', () => {
  const result = parse(`ROOT = TRUE\n[*]\nIndent_Style = TAB\nIndent_Size = TAB\nTAB_WIDTH = 3\nEND_OF_LINE = CRLF\nINSERT_FINAL_NEWLINE = FALSE\nMAX_LINE_LENGTH = OFF`);
  assert.equal(result.ok, true);
  assert.deepEqual([result.options.use_tabs, result.options.indent_width, result.options.tab_width,
    result.options.line_ending, result.options.final_newline, result.options.preferred_width], [true, 3, 3, 'crlf', false, 0]);
  assert.equal(parse('[*]\nindent_size=2').options.tab_width, 0);
  assert.equal(parse('[*]\nindent_size=tab', { currentOptions: { ...current(), tab_width: 6 } }).options.indent_width, 6);
});

test('later matching sections override earlier pairs while unrelated sections have no effect', () => {
  const result = parse('[*]\nindent_size=2\n[apps/*.chibi]\nindent_size=8\n[apps/**/Host.chibi]\nindent_size=3\n[*.cpp]\nindent_size=16');
  assert.equal(result.options.indent_width, 3);
  assert.equal(result.matched, 2);
  assert.equal(parse('[*.cpp]\nindent_size=invalid').ok, true);
});

test('slashless patterns match any basename and slashed patterns stay relative to the config root', () => {
  assert.equal(matched('*.chibi', 'apps/a/A.chibi'), true);
  assert.equal(matched('apps/*.chibi', 'apps/A.chibi'), true);
  assert.equal(matched('apps/*.chibi', 'apps/a/A.chibi'), false);
  assert.equal(matched('/apps/*.chibi', 'apps/A.chibi'), true);
  assert.equal(matched('/A.chibi', 'apps/A.chibi'), false);
  assert.equal(matched('apps/', 'apps/A.chibi'), false);
  assert.equal(matched('*.chibi', 'apps/A.Chibi'), false);
});

test('double stars match zero or multiple directories and question marks avoid separators', () => {
  for (const path of ['A.chibi', 'apps/A.chibi', 'apps/a/A.chibi']) assert.equal(matched('**/A.chibi', path), true);
  assert.equal(matched('apps/**/A.chibi', 'apps/A.chibi'), true);
  assert.equal(matched('apps/**/A.chibi', 'apps/a/b/A.chibi'), true);
  assert.equal(matched('apps/**.chibi', 'apps/a/A.chibi'), true);
  assert.equal(matched('apps/?.chibi', 'apps/A.chibi'), true);
  assert.equal(matched('apps/?.chibi', 'apps/AB.chibi'), false);
});

test('nested brace alternatives, escaped wildcards and literal or negated character classes match', () => {
  assert.equal(matched('{apps,{tools,packages}}/**/*.chibi', 'tools/compiler/A.chibi'), true);
  assert.equal(matched('{apps,{tools,packages}}/**/*.chibi', 'docs/A.chibi'), false);
  assert.equal(matched('{*,apps/A.chibi}', 'otherapps/A.chibi'), false);
  assert.equal(matched('A[ab].chibi', 'apps/Aa.chibi'), true);
  assert.equal(matched('A[!ab].chibi', 'apps/Ac.chibi'), true);
  assert.equal(matched('A[!ab].chibi', 'apps/Aa.chibi'), false);
  assert.equal(matched('A[a-c].chibi', 'apps/Ab.chibi'), false); // '-' is literal in the specification.
  assert.equal(matched('A[a-c].chibi', 'apps/A-.chibi'), true);
  assert.equal(matched('A\\*.chibi', 'apps/A*.chibi'), true);
  assert.equal(matched('A\\*.chibi', 'apps/Ab.chibi'), false);
  assert.equal(matched('A{only}.chibi', 'apps/A{only}.chibi'), true);
});

test('signed numeric brace ranges match integers without expanding the range', () => {
  for (const value of ['-2', '0', '3', '003']) assert.equal(matched('A{-2..3}.chibi', `apps/A${value}.chibi`), true);
  for (const value of ['-3', '4', '2.5']) assert.equal(matched('A{-2..3}.chibi', `apps/A${value}.chibi`), false);
  assert.equal(matched('A{-999999999999999999999..999999999999999999999}.chibi', 'A12345678901234567890.chibi'), true);
});

test('whole-line comments and BOM are accepted; inline comments remain part of a value', () => {
  const result = parse('\ufeff# comment\r\n; comment\r\n[*.chibi]\r\nindent_size = 2\r\n');
  assert.equal(result.options.indent_width, 2);
  const inline = parse('[*]\nindent_size = 2 # not an inline comment');
  assert.equal(inline.ok, false);
  assert.match(inline.diagnostics[0].message, /whole number/);
});

test('unset removes an earlier property instead of importing a literal value', () => {
  const existing = { ...current(), indent_width: 7 };
  const result = parse('[*]\nindent_size=2\nchibi_profile=expanded_v1\n[*.chibi]\nindent_size=UnSeT\nchibi_profile=unset', { currentOptions: existing });
  assert.equal(result.options.indent_width, 7);
  assert.equal(result.options.profile, 'current_v1');
});

test('profile is applied before explicit overrides; custom keys win over standard aliases', () => {
  for (const body of ['chibi_indent_width=4\nchibi_profile=compact_v1\nindent_size=2',
    'indent_size=2\nchibi_profile=compact_v1\nchibi_indent_width=4']) {
    const result = parse(`[*]\n${body}`);
    assert.equal(result.options.profile, 'compact_v1');
    assert.equal(result.options.indent_width, 4);
    assert.equal(result.options.preferred_width, 100);
  }
});

test('unknown settings are reported and ignored while recognized settings still apply', () => {
  const result = parse('[*]\nindent_size=2\ncharset=utf-8\nother_plugin_setting=true\nchibi_typo=4');
  assert.equal(result.ok, true);
  assert.equal(result.options.indent_width, 2);
  assert.equal(result.diagnostics.length, 3);
  assert.equal(result.diagnostics.every((item) => item.level === 'warning'), true);
  assert.equal(parse('[*.cpp]\nindent_size=4').diagnostics.some((item) => /No section matches/.test(item.message)), true);
});

test('all catalog numeric bounds, booleans and choices are validated atomically', () => {
  for (const item of catalog.items) {
    if (item.kind !== 'number') continue;
    for (const value of [item.min - 1, item.max + 1, '1.5', '2junk']) {
      const result = parse(`${item.key}=${value}`, { format: 'chibi' });
      assert.equal(result.ok, false, `${item.key}=${value}`);
      assert.equal(result.options, null);
    }
  }
  for (const body of ['insert_final_newline=yes', 'indent_style=tabs', 'end_of_line=cr', 'max_line_length=19',
    'chibi_profile=missing_v9', 'chibi_data_column_alignment=left,,right']) {
    const result = parse(`[*]\nindent_size=2\n${body}`);
    assert.equal(result.ok, false, body);
    assert.equal(result.options, null);
  }
});

test('column direction input uses the shared normalization and validation', () => {
  const item = catalog.items.find((item) => item.key === 'data_column_alignment');
  assert.equal(normalizeOption(item, ' Left, RIGHT ,auto '), 'left,right,auto');
  assert.equal(optionError(item, 'left,right,auto'), '');
  assert.equal(optionError(item, Array(32).fill('left').join(',')), '');
  assert.notEqual(optionError(item, Array(33).fill('left').join(',')), '');
  assert.equal(parse('[*]\nchibi_data_column_alignment= Left, RIGHT ').options.data_column_alignment, 'left,right');
});

test('Chibi import overlays current settings without a profile and supports unset', () => {
  const result = parse('# settings\nindent_width=2\nindent_width=unset\nfinal_newline=FALSE\nunknown=3',
    { format: 'chibi', currentOptions: { ...current(), indent_width: 6 } });
  assert.equal(result.ok, true);
  assert.equal(result.options.indent_width, 6);
  assert.equal(result.options.final_newline, false);
  assert.equal(result.diagnostics.length, 1);
});

test('preamble keys, malformed input and root placement produce useful diagnostics', () => {
  const result = parse('indent_size=2\nroot=true\n[*]\nroot=true\nindent_size=3');
  assert.equal(result.options.indent_width, 3);
  assert.equal(result.diagnostics.length, 2);
  for (const text of ['[*\nindent_size=2', '[*]\nbroken line', 'root=maybe\n[*]\nindent_size=2']) {
    const bad = parse(text);
    assert.equal(bad.ok, false);
    assert.equal(bad.diagnostics.some((item) => item.line === 1 || item.line === 2), true);
  }
});

test('Scratch uses a Chibi filename and imports have bounded text, glob and matching work', () => {
  assert.equal(parse('[*.chibi]\nindent_size=3', { path: '' }).options.indent_width, 3);
  assert.equal(parse(' '.repeat(131073)).ok, false);
  assert.match(parse(`[${'a'.repeat(1025)}]\nindent_size=2`).diagnostics[0].message, /1024/);
  const difficult = parse(`[${'*a'.repeat(450)}b]\nindent_size=2`, { path: 'a'.repeat(2000) + '.chibi' });
  assert.equal(difficult.ok, false);
  assert.equal(difficult.diagnostics.some((item) => /work limit/.test(item.message)), true);
});
