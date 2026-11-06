// Single-file config interchange for the playground. EditorConfig globs are
// relative to the repository root; no filesystem or parent config is loaded.
// Syntax and precedence: https://spec.editorconfig.org/ (0.17.2).
const MAX_TEXT = 131072;
const MAX_MATCH_STEPS = 1000000;

export function normalizeOption(item, value) {
  if (item.key === 'data_column_alignment' && typeof value === 'string')
    return value.trim().toLowerCase().split(',').map((word) => word.trim()).join(',') || 'auto';
  return value;
}

// The same schema validation serves saved settings, controls and both import
// formats. Only the two constraints not expressible by kind/min/max live here.
export function optionError(item, value) {
  if (item.kind === 'choice') return item.choices.some((choice) => Object.is(choice.value, value)) ? '' :
    `expected ${item.choices.map((choice) => String(choice.value)).join(', ')}`;
  if (item.kind === 'bool') return typeof value === 'boolean' ? '' : 'expected true or false';
  if (item.kind === 'number') {
    if (!Number.isSafeInteger(value) || value < item.min || value > item.max)
      return `expected a whole number from ${item.min} to ${item.max}`;
    if (item.key === 'preferred_width' && value > 0 && value < 20) return `expected 0 (off) or 20..${item.max}`;
    return '';
  }
  if (item.kind === 'text') {
    if (typeof value !== 'string') return 'expected text';
    if (item.key === 'data_column_alignment' &&
        (!/^(?:auto|left|right)(?:,(?:auto|left|right))*$/.test(value) || value.split(',').length > 32))
      return 'expected up to 32 comma-separated column styles: auto, left, or right';
    return '';
  }
  return 'unsupported option kind';
}

function readValue(item, text) {
  const lower = text.toLowerCase();
  if (item.kind === 'bool') return lower === 'true' ? true : lower === 'false' ? false : text;
  if (item.kind === 'number') return /^-?\d+$/.test(text) ? Number(text) : NaN;
  if (item.kind === 'choice') {
    const choice = item.choices.find((choice) => String(choice.value).toLowerCase() === lower);
    return choice ? choice.value : text;
  }
  return normalizeOption(item, text);
}

function integerParts(value) {
  const negative = value[0] === '-';
  const digits = (negative ? value.slice(1) : value).replace(/^0+/, '') || '0';
  return { negative: negative && digits !== '0', digits };
}

function compareInteger(left, right) {
  const a = integerParts(left), b = integerParts(right);
  if (a.negative !== b.negative) return a.negative ? -1 : 1;
  const order = a.digits.length - b.digits.length || (a.digits < b.digits ? -1 : a.digits > b.digits ? 1 : 0);
  return a.negative ? -order : order;
}

// A small NFA avoids regex backtracking on pasted patterns such as *a*a*a*.
// The shared exploration budget bounds work across every section in one import.
function globMatches(glob, path, budget) {
  if (budget.steps < 0) throw new Error('section matching exceeds the import work limit');
  if (glob.length > 1024) throw new Error('section glob exceeds 1024 characters');
  if (path.length > 4096) throw new Error('source path exceeds 4096 characters');
  const states = [{ edges: [], epsilon: [] }];
  const state = () => { states.push({ edges: [], epsilon: [] }); return states.length - 1; };
  const edge = (from, to, kind, data = {}) => states[from].edges.push({ to, kind, ...data });
  const literal = (from, char) => { const to = state(); edge(from, to, 'char', { char }); return to; };
  let slash = false, bracket = false, escaped = false;
  for (const char of glob) {
    if (!escaped && char === '[') bracket = true;
    else if (!escaped && char === ']') bracket = false;
    else if (char === '/' && !bracket) slash = true;
    if (!escaped && char === '\\') escaped = true; else escaped = false;
  }
  if (glob.startsWith('/')) glob = glob.slice(1);
  if (!slash) path = path.slice(path.lastIndexOf('/') + 1);

  const sequence = (text, start, depth) => {
    if (depth > 64) throw new Error('section glob nests more than 64 brace groups');
    let current = start;
    for (let i = 0; i < text.length; i++) {
      const char = text[i];
      if (char === '\\' && i + 1 < text.length) { current = literal(current, text[++i]); continue; }
      if (char === '*') {
        const all = text[i + 1] === '*';
        if (all) i++;
        const to = state();
        states[current].epsilon.push(to);
        if (all && text[i + 1] === '/') {
          // **/ permits zero directories, or a string ending in a separator.
          const loop = state();
          states[current].epsilon.push(loop);
          edge(loop, loop, 'any', { all: true });
          edge(loop, to, 'char', { char: '/' });
          i++;
        } else edge(current, current, 'any', { all });
        current = to;
      } else if (char === '?') {
        const to = state(); edge(current, to, 'any', { all: false }); current = to;
      } else if (char === '[' && text.indexOf(']', i + 1) !== -1) {
        const end = text.indexOf(']', i + 1);
        const content = text.slice(i + 1, end), negate = content.startsWith('!');
        const to = state();
        edge(current, to, 'class', { chars: negate ? content.slice(1) : content, negate });
        current = to; i = end;
      } else if (char === '{') {
        let end = i + 1, nesting = 1, inClass = false;
        const commas = [];
        for (; end < text.length; end++) {
          const part = text[end];
          if (part === '\\') { end++; continue; }
          if (part === '[') inClass = true;
          if (part === ']') inClass = false;
          if (inClass) continue;
          if (part === '{') nesting++;
          if (part === '}') { nesting--; if (nesting === 0) break; }
          if (part === ',' && nesting === 1) commas.push(end);
        }
        if (nesting !== 0) { current = literal(current, char); continue; }
        const content = text.slice(i + 1, end);
        const range = /^(-?\d+)\.\.(-?\d+)$/.exec(content);
        if (commas.length) {
          const to = state();
          let begin = i + 1;
          for (const stop of [...commas, end]) {
            const branch = state();
            states[current].epsilon.push(branch);
            const finish = sequence(text.slice(begin, stop), branch, depth + 1);
            states[finish].epsilon.push(to); begin = stop + 1;
          }
          current = to;
        } else if (range && compareInteger(range[1], range[2]) < 0) {
          const to = state(); edge(current, to, 'range', { min: range[1], max: range[2] }); current = to;
        } else {
          for (const part of text.slice(i, end + 1)) current = literal(current, part);
        }
        i = end;
      } else current = literal(current, char);
    }
    return current;
  };
  const finish = sequence(glob, 0, 0);
  const pending = [[0, 0]], seen = new Set([0]);
  const enqueue = (node, position) => {
    const key = position * states.length + node;
    if (!seen.has(key)) { seen.add(key); pending.push([node, position]); }
  };
  while (pending.length) {
    if (--budget.steps < 0) throw new Error('section matching exceeds the import work limit');
    const [node, position] = pending.pop();
    if (node === finish && position === path.length) return true;
    for (const next of states[node].epsilon) {
      if (--budget.steps < 0) throw new Error('section matching exceeds the import work limit');
      enqueue(next, position);
    }
    for (const next of states[node].edges) {
      if (--budget.steps < 0) throw new Error('section matching exceeds the import work limit');
      const char = path[position];
      if (char === undefined) continue;
      if (next.kind === 'char' && char === next.char ||
          next.kind === 'any' && (next.all || char !== '/') ||
          next.kind === 'class' && char !== '/' && next.chars.includes(char) !== next.negate) enqueue(next.to, position + 1);
      if (next.kind === 'range') {
        let end = position + (char === '-' ? 1 : 0);
        while (end < path.length && path[end] >= '0' && path[end] <= '9') {
          end++;
          // Integer comparisons scan the candidate digits, not just one edge.
          budget.steps -= end - position;
          if (budget.steps < 0) throw new Error('section matching exceeds the import work limit');
          const value = path.slice(position, end);
          if (compareInteger(value, next.min) >= 0 && compareInteger(value, next.max) <= 0) enqueue(next.to, end);
        }
      }
    }
  }
  return false;
}

const STANDARD = new Map([
  ['indent_style', 'use_tabs'], ['indent_size', 'indent_width'], ['tab_width', 'tab_width'],
  ['end_of_line', 'line_ending'], ['insert_final_newline', 'final_newline'], ['max_line_length', 'preferred_width'],
]);

export function importConfig(text, { format = 'chibi', path = '', items, profileOptions, currentOptions }) {
  const diagnostics = [], properties = new Map(), byKey = new Map(items.map((item) => [item.key, item]));
  const note = (line, message, level = 'error') => diagnostics.push({ line, message, level });
  const failed = () => ({ ok: false, diagnostics, options: null, applied: 0 });
  if (text.length > MAX_TEXT) { note(0, 'Config exceeds 128 Ki characters.'); return failed(); }
  const editor = format === 'editorconfig', budget = { steps: MAX_MATCH_STEPS };
  let active = !editor, section = false, matched = 0;
  const lines = text.replace(/^\uFEFF/, '').split(/\r?\n/);
  for (let index = 0; index < lines.length; index++) {
    const line = lines[index].trim(), number = index + 1;
    if (!line || line.startsWith('#') || line.startsWith(';')) continue;
    if (editor && line.startsWith('[')) {
      section = true;
      if (!line.endsWith(']')) { note(number, 'Section header must end with ].'); active = false; continue; }
      try { active = globMatches(line.slice(1, -1), path || 'scratch.chibi', budget); }
      catch (error) { note(number, error.message); active = false; }
      if (active) matched++;
      continue;
    }
    const separator = line.indexOf('=');
    if (separator < 1) { note(number, 'Expected a NAME=VALUE setting.'); continue; }
    const key = line.slice(0, separator).trim().toLowerCase(), value = line.slice(separator + 1).trim();
    if (editor && !section) {
      if (key !== 'root') note(number, `${key} outside a section has no effect.`, 'warning');
      else if (!/^(true|false)$/i.test(value)) note(number, 'root expects true or false.');
      continue;
    }
    if (!active) continue;
    if (editor && key === 'root') { note(number, 'root only takes effect before the first section.', 'warning'); continue; }
    if (value.toLowerCase() === 'unset') properties.delete(key);
    else properties.set(key, { value, line: number });
  }
  if (editor && !matched) note(0, `No section matches ${path || 'Scratch (*.chibi)'}.`, 'warning');
  const customKey = (key) => editor ? key.startsWith('chibi_') ? key.slice(6) : null : key;
  const profile = properties.get(editor ? 'chibi_profile' : 'profile');
  let options = { ...currentOptions }, applied = 0;
  if (profile) {
    const item = byKey.get('profile'), value = readValue(item, profile.value), error = optionError(item, value);
    if (error) note(profile.line, `profile: ${error}.`);
    else { options = profileOptions(value); applied++; }
  }
  const assign = (key, value, line) => {
    const item = byKey.get(key);
    if (!item) { note(line, `Unknown Chibi setting ${key}; ignored.`, 'warning'); return; }
    const error = optionError(item, value);
    if (error) note(line, `${key}: ${error}.`);
    else { options[key] = value; applied++; }
  };
  if (editor) {
    for (const [key, entry] of properties) {
      if (key.startsWith('chibi_')) continue;
      if (!STANDARD.has(key)) { note(entry.line, `EditorConfig setting ${key} is not used by the formatter.`, 'warning'); continue; }
      let value = entry.value.toLowerCase();
      if (key === 'indent_style') value = value === 'tab' ? true : value === 'space' ? false : value;
      else if (key === 'indent_size' && value === 'tab') {
        const tab = properties.get('tab_width');
        value = tab && /^\d+$/.test(tab.value) ? Number(tab.value) : options.tab_width || options.indent_width;
      } else if (key === 'max_line_length' && value === 'off') value = 0;
      else value = readValue(byKey.get(STANDARD.get(key)), value);
      assign(STANDARD.get(key), value, entry.line);
    }
    if (properties.has('indent_size') && !properties.has('tab_width')) options.tab_width = 0;
  }
  // Chibi-specific settings always override their common EditorConfig aliases,
  // independent of the order of different keys in the pasted file.
  for (const [key, entry] of properties) {
    const name = customKey(key);
    if (name === null || name === 'profile') continue;
    const item = byKey.get(name);
    if (!item) { note(entry.line, `Unknown Chibi setting ${name}; ignored.`, 'warning'); continue; }
    assign(name, readValue(item, entry.value), entry.line);
  }
  const ok = !diagnostics.some((item) => item.level === 'error');
  return { ok, diagnostics, options: ok ? options : null, applied, matched };
}

export function exportConfig(options, { format = 'chibi', items, profileOptions }) {
  const profile = options.profile || 'current_v1', baseline = profileOptions(profile);
  const overrides = items.filter((item) => item.key !== 'profile' && options[item.key] !== baseline[item.key]);
  if (format !== 'editorconfig') return [`profile=${profile}`, ...overrides.map((item) => `${item.key}=${options[item.key]}`)].join('\n');
  const lines = ['root = true', '', '[*.chibi]',
    `indent_style = ${options.use_tabs ? 'tab' : 'space'}`, `indent_size = ${options.indent_width}`,
    `tab_width = ${options.tab_width || options.indent_width}`, `end_of_line = ${options.line_ending}`,
    `insert_final_newline = ${options.final_newline}`, `max_line_length = ${options.preferred_width || 'off'}`,
    '', `chibi_profile = ${profile}`, ...overrides.map((item) => `chibi_${item.key} = ${options[item.key]}`)];
  // Preserve the formatter's inherit-tab-width sentinel as well as its effect.
  if (options.tab_width === 0 && !overrides.some((item) => item.key === 'tab_width')) lines.push('chibi_tab_width = 0');
  return lines.join('\n');
}
