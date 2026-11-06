import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

// One schema owns native validation/defaults and the browser controls.
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const schema = JSON.parse(readFileSync(resolve(root, 'tools/compiler/source_format/options.json'), 'utf8'));
const definitions = schema.groups.flatMap(group => group.items);
const literal = value => typeof value === 'string' ? JSON.stringify(value) : String(value);
const nativeValue = (item, value) => item.native.values ? item.native.values[String(value)] :
  item.native.default !== undefined ? item.native.default : value;
const lines = [
  'package source_format', '', '// Generated from options.json by tools/docs/playground/generate-options.mjs.',
  'import strings "core:strings"', '', 'pub MaxBytes: int : 8388608;',
  'pub Keep: int : 0;', 'pub Omit: int : 1;', 'pub Always: int : 2;',
  'pub Collapse: int : 1;', 'pub Expand: int : 2;', 'pub Attach: int : 1;',
  'pub NextLine: int : 2;', 'pub OmitSingle: int : 1;', '', 'pub Options :: struct {',
];
for (const item of definitions) lines.push(`    ${item.native.field}: ${item.native.type};`);
lines.push('}', '', 'pub Defaults :: () -> Options {', '    return {');
for (const item of definitions) lines.push(`        ${item.native.field}: ${literal(nativeValue(item, item.default))},`);
lines.push('    };', '}', '', 'pub Profile :: (profile: int) -> Options {', '    options := Defaults();');
for (const [index, profile] of schema.profiles.entries()) {
  lines.push(`    if (profile == ${index}) {`);
  for (const [key, value] of Object.entries(profile.options)) {
    const item = definitions.find(item => item.key === key);
    if (!item) throw new Error(`Unknown profile option ${key}`);
    lines.push(`        options.${item.native.field} = ${literal(nativeValue(item, value))};`);
  }
  lines.push(`        options.profile = ${index};`, '    }');
}
lines.push('    return options;', '}', '', 'pub Validate :: (options: Options) -> string {');
for (const item of definitions) {
  const field = `options.${item.native.field}`;
  if (item.native.type === 'bool') continue;
  if (item.native.parser) {
    lines.push(`    if (!ValidAlignmentColumns(${field})) {`,
      '        return "Invalid formatter options: data_column_alignment contains an invalid column policy";', '    }');
    continue;
  }
  if (item.native.values) {
    const allowed = [...new Set(Object.values(item.native.values))];
    const names = item.choices.map(choice => choice.value);
    const expected = names.length > 1 ? `${names.slice(0, -1).join(', ')} or ${names.at(-1)}` : names[0];
    lines.push(`    if (${allowed.map(value => `${field} != ${literal(value)}`).join(' && ')}) {`,
      `        return ${literal(`Invalid formatter options: ${item.key} must be ${expected}`)};`, '    }');
  } else {
    const { min, max, allowZero } = item.native;
    if (item.key === 'preferred_width') {
      lines.push(`    if (${field} != 0 && (${field} < 20 || ${field} > 400)) {`,
        '        return "Invalid formatter options: preferred_width must be 0 or 20..400";', '    }');
    } else {
      const condition = `${field} < ${min} || ${field} > ${max}`;
      lines.push(`    if (${allowZero ? `${field} != 0 && (${condition})` : condition}) {`,
        `        return ${literal(`Invalid formatter options: ${item.key} must be ${allowZero ? '0 or ' : ''}${min}..${max}`)};`, '    }');
    }
  }
}
lines.push('    if (options.end_line > 0 && options.start_line > options.end_line) {',
  '        return "Invalid formatter options: end_line must not precede start_line";', '    }',
  '    return "";', '}', '',
  'Number :: (text: string) -> int {', '    if (text == "") { return -2147483648; }',
  '    value: int = 0;', '    negative := strings.ByteAt(text, 0) == 45;',
  '    index: int = 0;', '    if (negative) { index = 1; }',
  '    if (index == strings.ByteLength(text)) { return -2147483648; }',
  '    while (index < strings.ByteLength(text)) {', '        digit := strings.ByteAt(text, index) - 48;',
  '        if (digit < 0 || digit > 9 || value > 100000000) { return -2147483648; }',
  '        value = value * 10 + digit;', '        index += 1;', '    }',
  '    if (negative) { return -value; }', '    return value;', '}', '',
  'pub ParseOptions :: (base: Options, text: string) -> (options: Options, error: string) {',
  '    pairs: string[] = [];', '    for (line in strings.SplitLines(text)) {',
  '        content := line;', '        hash := strings.IndexByte(line, 35);',
  '        if (hash >= 0) { content = strings.Slice(line, 0, hash); }',
  '        pairs.extend(strings.Fields(content));', '    }', '    options := base;',
  '    for (pair in pairs) {', '        if (strings.HasPrefix(pair, "profile=")) {',
  '            value := strings.TrimPrefix(pair, "profile=");');
for (const [index, profile] of schema.profiles.entries()) {
  lines.push(`            ${index ? 'else ' : ''}if (value == ${literal(profile.id)}) { options = Profile(${index}); }`);
}
lines.push('            else { return (options, "Invalid formatter profile"); }', '        }', '    }',
  '    for (pair in pairs) {', '        equals := strings.IndexByte(pair, 61);',
  '        if (equals <= 0) { return (options, format_text("Invalid formatter option {}; write NAME=VALUE", pair)); }',
  '        name := strings.Slice(pair, 0, equals);',
  '        value := strings.Slice(pair, equals + 1, strings.ByteLength(pair) - equals - 1);',
  '        number := Number(value);', '        if (name == "profile") { continue; }');
for (const item of definitions.filter(item => item.key !== 'profile')) {
  const field = `options.${item.native.field}`;
  if (item.native.parser) {
    lines.push(`        else if (name == ${literal(item.key)}) {`,
      `            parsed := ${item.native.parser}(value);`,
      '            if (parsed.error != "") { return (options, parsed.error); }',
      `            ${field} = parsed.bits;`, '        }');
  } else if (item.native.values) {
    for (const [value, target] of Object.entries(item.native.values)) {
      lines.push(`        else if (name == ${literal(item.key)} && value == ${literal(value)}) { ${field} = ${literal(target)}; }`);
    }
  } else if (item.native.type === 'bool') {
    lines.push(`        else if (name == ${literal(item.key)} && (value == "true" || value == "false")) { ${field} = value == "true"; }`);
  } else {
    lines.push(`        else if (name == ${literal(item.key)} && number != -2147483648) { ${field} = number; }`);
  }
}
lines.push('        else { return (options, format_text("Invalid formatter option {}", pair)); }',
  '    }', '    return (options, Validate(options));', '}', '');

const groups = schema.groups.map(group => ({ ...group, items: group.items.map(({ native, ...item }) => item) }));
const javascript = `// Generated from tools/compiler/source_format/options.json.\nconst OPTION_GROUPS = ${JSON.stringify(groups, null, 2)};\n` +
  `const FORMAT_PROFILES = ${JSON.stringify(schema.profiles, null, 2)};\n` +
  'function eachOption(visit) { for (const group of OPTION_GROUPS) for (const item of group.items) visit(item, group); }\n' +
  'function defaultOptions() { const options = {}; eachOption(item => { options[item.key] = item.default; }); return options; }\n' +
  'function optionByKey(key) { let found = null; eachOption(item => { if (item.key === key) found = item; }); return found; }\n' +
  'function profileOptions(id) { const profile = FORMAT_PROFILES.find(profile => profile.id === id); return { ...defaultOptions(), ...(profile?.options || {}), profile: id }; }\n' +
  'function configText(options) { const id = options.profile || "current_v1"; const baseline = profileOptions(id); const lines = id === "current_v1" ? [] : [`profile=${id}`]; eachOption(item => { if (item.key !== "profile" && options[item.key] !== baseline[item.key]) lines.push(`${item.key}=${options[item.key]}`); }); return lines.join("\\n"); }\n';
for (const [path, contents] of [
  ['tools/compiler/source_format/Options.chibi', lines.join('\n')],
  ['tools/docs/playground/options.js', javascript],
]) {
  const file = resolve(root, path);
  if (process.argv.includes('--check')) {
    if (readFileSync(file, 'utf8') !== contents) throw new Error(`${path} is stale; run generate-options.mjs`);
  } else writeFileSync(file, contents);
}
console.log(`Formatter option schema: ${definitions.length} settings, ${schema.profiles.length} versioned profiles.`);
