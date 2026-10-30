// The playground: browse the repository's Chibi sources, set the formatter's options, and read the
// result as formatted code, as a diff against the original, or as the original. The page reaches
// its host through the three calls of `host` at the bottom, so a WebAssembly formatter can replace
// the local development server without a change here.
'use strict';

const $ = (id) => document.getElementById(id);
const els = {
  app: $('app'), crumb: $('crumb'), status: $('status'), statusText: $('status-text'), spinner: document.querySelector('.spinner'),
  filter: $('filter'), count: $('count'), tree: $('tree'), view: $('view'), theme: $('theme'),
  viewport: $('viewport'), editor: $('editor'), foot: $('foot'), edit: $('edit'), copy: $('copy'), whitespace: $('whitespace'),
  changesNav: $('changes-nav'), changePosition: $('change-position'), prev: $('prev-change'), next: $('next-change'),
  optionsBody: $('options-body'), optionsJson: $('options-json'), reset: $('reset'), copyOptions: $('copy-options'),
  scrim: $('scrim'), toggleFiles: $('toggle-files'), toggleOptions: $('toggle-options'),
};

const CHUNK_LINES = 120;     // lines per block, so the browser can skip blocks that are off screen
const CONTEXT_LINES = 3;     // unchanged lines kept around a change in the Changes view
const SLOW_MS = 1500;        // after this long, say that the formatter may be starting
const WIDE = window.matchMedia('(min-width: 1100px)');   // side panels sit beside the code; narrower, they slide over it

const state = {
  files: [], current: '', original: '', formatted: '', error: '', ok: false,
  view: 'formatted', editing: true, whitespace: false,
  options: loadOptions(),
  lines: { original: [], formatted: [] },      // the text of each side, split into lines
  html: { original: [], formatted: [] },       // the same lines, highlighted
  diff: null, ops: null, hunks: [], hunk: -1,
  expanded: new Set(), request: 0, timer: 0, slow: 0,
};

// ---------------------------------------------------------------- settings

function loadOptions() {
  const options = defaultOptions();
  try {
    const saved = JSON.parse(localStorage.getItem('chibi-format-options') || '{}');
    for (const key of Object.keys(options)) if (key in saved && typeof saved[key] === typeof options[key]) options[key] = saved[key];
  } catch (_) { /* a corrupt value falls back to the defaults */ }
  return options;
}

function saveOptions() {
  try { localStorage.setItem('chibi-format-options', JSON.stringify(state.options)); } catch (_) { /* private mode */ }
}

// ---------------------------------------------------------------- small widgets

// A row of buttons of which one is on, as in a native radio group: arrows move and select.
function wireSegmented(element, onChange) {
  const buttons = () => [...element.querySelectorAll('[role=radio]')];
  const choose = (button, focus) => {
    for (const other of buttons()) { other.setAttribute('aria-checked', String(other === button)); other.tabIndex = other === button ? 0 : -1; }
    if (focus) button.focus();
    onChange(button.dataset.value);
  };
  element.addEventListener('click', (event) => { const button = event.target.closest('[role=radio]'); if (button && !button.disabled) choose(button, false); });
  element.addEventListener('keydown', (event) => {
    const all = buttons(), at = all.indexOf(document.activeElement);
    if (at < 0) return;
    const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[event.key];
    if (!step) return;
    event.preventDefault();
    choose(all[(at + step + all.length) % all.length], true);
  });
  for (const button of buttons()) button.tabIndex = button.getAttribute('aria-checked') === 'true' ? 0 : -1;
}

function setSegmented(element, value) {
  for (const button of element.querySelectorAll('[role=radio]')) {
    const on = button.dataset.value === String(value);
    button.setAttribute('aria-checked', String(on));
    button.tabIndex = on ? 0 : -1;
  }
}

function setStatus(text, kind, busy) {
  els.statusText.textContent = text;
  els.status.className = 'status' + (kind ? ' ' + kind : '');
  els.spinner.hidden = !busy;
}

// ---------------------------------------------------------------- theme and panels

function initTheme() {
  const saved = document.documentElement.dataset.theme || 'system';
  setSegmented(els.theme, saved);
  wireSegmented(els.theme, (value) => {
    document.documentElement.dataset.theme = value;
    try { localStorage.setItem('chibi-format-theme', value); } catch (_) { /* private mode */ }
  });
}

function setPanel(name, open) {
  els.app.dataset[name] = open ? 'open' : 'closed';
  (name === 'files' ? els.toggleFiles : els.toggleOptions).setAttribute('aria-expanded', String(open));
  els.scrim.hidden = WIDE.matches || (els.app.dataset.files !== 'open' && els.app.dataset.options !== 'open');
}

function initPanels() {
  const place = () => { setPanel('files', WIDE.matches); setPanel('options', WIDE.matches); };
  place();
  WIDE.addEventListener('change', place);
  els.toggleFiles.addEventListener('click', () => {
    const open = els.app.dataset.files !== 'open';
    if (!WIDE.matches && open) setPanel('options', false);
    setPanel('files', open);
  });
  els.toggleOptions.addEventListener('click', () => {
    const open = els.app.dataset.options !== 'open';
    if (!WIDE.matches && open) setPanel('files', false);
    setPanel('options', open);
  });
  els.scrim.addEventListener('click', () => { setPanel('files', false); setPanel('options', false); });
}

// ---------------------------------------------------------------- options pane

function optionControl(item) {
  const wrap = document.createElement('div');
  wrap.className = 'control';
  if (item.kind === 'choice') {
    const group = document.createElement('div');
    group.className = 'segmented';
    group.setAttribute('role', 'radiogroup');
    group.setAttribute('aria-label', item.label);
    for (const choice of item.choices) {
      const button = document.createElement('button');
      button.type = 'button';
      button.setAttribute('role', 'radio');
      button.dataset.value = String(choice.value);
      button.textContent = choice.label;
      group.appendChild(button);
    }
    setSegmented(group, state.options[item.key]);
    wireSegmented(group, (value) => {
      const match = item.choices.find((choice) => String(choice.value) === value);
      setOption(item.key, match.value);
    });
    wrap.appendChild(group);
  } else {
    const stepper = document.createElement('div');
    stepper.className = 'stepper';
    const input = document.createElement('input');
    input.type = 'number';
    input.min = String(item.min);
    input.max = String(item.max);
    input.value = String(state.options[item.key]);
    input.setAttribute('aria-label', item.label);
    input.disabled = !!item.planned;
    const step = (delta) => {
      const value = Math.min(item.max, Math.max(item.min, (parseInt(input.value, 10) || item.default) + delta));
      input.value = String(value);
      setOption(item.key, value);
    };
    const minus = document.createElement('button'), plus = document.createElement('button');
    minus.type = plus.type = 'button';
    minus.textContent = '-';
    plus.textContent = '+';
    minus.setAttribute('aria-label', `Decrease ${item.label}`);
    plus.setAttribute('aria-label', `Increase ${item.label}`);
    minus.disabled = plus.disabled = !!item.planned;
    minus.addEventListener('click', () => step(-1));
    plus.addEventListener('click', () => step(1));
    input.addEventListener('change', () => step(0));
    stepper.append(minus, input, plus);
    wrap.appendChild(stepper);
  }
  return wrap;
}

function renderOptions() {
  const fragment = document.createDocumentFragment();
  for (const group of OPTION_GROUPS) {
    const section = document.createElement('section');
    section.className = 'option-group';
    const heading = document.createElement('h3');
    heading.textContent = group.name;
    section.appendChild(heading);
    for (const item of group.items) {
      const row = document.createElement('div');
      row.className = 'option' + (item.planned ? ' planned' : '');
      row.dataset.key = item.key;
      const top = document.createElement('div');
      top.className = 'option-top';
      const label = document.createElement('span');
      label.className = 'option-label';
      label.textContent = item.label;
      top.appendChild(label);
      if (item.planned) {
        const tag = document.createElement('span');
        tag.className = 'tag';
        tag.textContent = 'Not implemented';
        top.appendChild(tag);
      }
      top.appendChild(optionControl(item));
      const help = document.createElement('p');
      help.className = 'option-help';
      help.textContent = item.help;
      const changed = document.createElement('p');
      changed.className = 'option-default';
      row.append(top, help, changed);
      section.appendChild(row);
    }
    fragment.appendChild(section);
  }
  els.optionsBody.replaceChildren(fragment);
  refreshOptionLabels();
}

// Shows which options differ from their defaults, and the JSON for the current ones.
function refreshOptionLabels() {
  for (const row of els.optionsBody.querySelectorAll('.option')) {
    const item = optionByKey(row.dataset.key);
    const different = state.options[item.key] !== item.default && !item.planned;
    row.classList.toggle('modified', different);
    const note = row.querySelector('.option-default');
    const shown = item.choices ? item.choices.find((choice) => choice.value === item.default).label : String(item.default);
    note.textContent = different ? `Changed from the default, ${shown}` : '';
    note.hidden = !different;
  }
  els.optionsJson.textContent = JSON.stringify(formatterOptions(state.options), null, 2);
  els.reset.disabled = JSON.stringify(state.options) === JSON.stringify(defaultOptions());
  document.documentElement.style.setProperty('--tab-size', String(state.options.indent_width));
}

function setOption(key, value) {
  state.options[key] = value;
  saveOptions();
  refreshOptionLabels();
  scheduleReformat(120);
}

// ---------------------------------------------------------------- formatting

function splitLines(text) {
  const lines = text.replace(/\r\n/g, '\n').split('\n');
  if (lines.length > 1 && lines[lines.length - 1] === '') lines.pop();
  return text === '' ? [] : lines;
}

function scheduleReformat(delay) {
  clearTimeout(state.timer);
  state.timer = setTimeout(reformat, delay);
}

async function reformat() {
  const id = ++state.request;
  const source = state.original;
  clearTimeout(state.slow);
  if (source.trim() === '') {
    Object.assign(state, { formatted: '', error: '', ok: false, diff: null, ops: null });
    state.lines.formatted = []; state.html.formatted = [];
    setStatus('', '', false);
    render();
    return;
  }
  setStatus('Formatting', '', true);
  state.slow = setTimeout(() => { if (id === state.request) setStatus('Starting the formatter. The first request takes about 25 seconds.', '', true); }, SLOW_MS);
  try {
    const result = await host.format(source, formatterOptions(state.options));
    if (id !== state.request) return;
    clearTimeout(state.slow);
    if (!result.ok) {
      Object.assign(state, { formatted: '', error: result.error, ok: false, diff: null, ops: null });
      state.lines.formatted = []; state.html.formatted = [];
      setStatus(result.error, 'error', false);
    } else {
      state.formatted = result.text;
      state.error = '';
      state.ok = true;
      state.lines.formatted = splitLines(result.text);
      state.html.formatted = highlightChibi(result.text);
      state.diff = diffLines(state.lines.original, state.lines.formatted);
      state.ops = state.diff ? diffOps(state.lines.original, state.lines.formatted, state.diff) : null;
      state.expanded.clear();
      const changed = state.diff ? state.diff.b.size : -1;
      if (state.original === result.text) setStatus('Already formatted', 'good', false);
      else if (changed === 0) setStatus('Only line endings change', '', false);
      else if (changed < 0) setStatus('Formatted, with too many changes to compare', '', false);
      else setStatus(`${changed.toLocaleString()} ${changed === 1 ? 'line' : 'lines'} would change`, 'warn', false);
    }
    render();
  } catch (error) {
    if (id !== state.request) return;
    clearTimeout(state.slow);
    setStatus(String(error.message || error), 'error', false);
  }
}

function setOriginal(text) {
  state.original = text;
  state.lines.original = splitLines(text);
  state.html.original = highlightChibi(text);
}

// ---------------------------------------------------------------- code view

function lineHtml(html) {
  const text = html === '' ? '&#8203;' : html;
  return state.whitespace ? showIndentation(text) : text;
}

function chunked(rows) {
  const parts = [];
  for (let i = 0; i < rows.length; i += CHUNK_LINES) parts.push(`<div class="chunk">${rows.slice(i, i + CHUNK_LINES).join('')}</div>`);
  return parts.join('');
}

// Groups of consecutive changed line indexes, as the index of each group's first line.
function groupStarts(changed, count) {
  const starts = [];
  for (let i = 0; i < count; i++) if (changed.has(i) && !changed.has(i - 1)) starts.push(i);
  return starts;
}

function plainView(side) {
  const html = state.html[side];
  const changed = state.diff ? (side === 'formatted' ? state.diff.b : state.diff.a) : new Set();
  const rows = html.map((line, i) =>
    `<div class="ln${changed.has(i) ? ' chg' : ''}" id="L${i}"><span class="no">${i + 1}</span><span class="tx">${lineHtml(line)}</span></div>`);
  state.hunks = groupStarts(changed, html.length).map((i) => `L${i}`);
  return chunked(rows);
}

function diffView() {
  if (!state.ops) return '<div class="notice">There are too many differences to show line by line. Use the Formatted view.</div>';
  const ops = state.ops, show = new Array(ops.length).fill(false);
  for (let i = 0; i < ops.length; i++) {
    if (ops[i].kind === '=') continue;
    for (let k = Math.max(0, i - CONTEXT_LINES); k <= Math.min(ops.length - 1, i + CONTEXT_LINES); k++) show[k] = true;
  }
  const rows = [];
  state.hunks = [];
  let i = 0;
  while (i < ops.length) {
    if (!show[i] && !state.expanded.has(i)) {
      let end = i;
      while (end < ops.length && !show[end]) end++;
      rows.push(`<button type="button" class="fold" data-fold="${i}" data-end="${end}">Show ${(end - i).toLocaleString()} unchanged ${end - i === 1 ? 'line' : 'lines'}</button>`);
      i = end;
      continue;
    }
    const op = ops[i];
    const previous = ops[i - 1];
    const id = op.kind !== '=' && (!previous || previous.kind === '=') ? `H${state.hunks.length}` : '';
    if (id) state.hunks.push(id);
    const html = op.kind === '-' ? state.html.original[op.a] : state.html.formatted[op.b];
    const mark = op.kind === '=' ? '' : op.kind;
    rows.push(`<div class="ln ${op.kind === '+' ? 'add' : op.kind === '-' ? 'del' : 'ctx'}"${id ? ` id="${id}"` : ''}>` +
      `<span class="no">${op.a >= 0 ? op.a + 1 : ''}</span><span class="no">${op.b >= 0 ? op.b + 1 : ''}</span>` +
      `<span class="mk">${mark}</span><span class="tx">${lineHtml(html)}</span></div>`);
    i++;
  }
  return chunked(rows);
}

function render() {
  const showEditor = state.editing;
  els.editor.hidden = !showEditor;
  els.viewport.hidden = showEditor;
  els.edit.setAttribute('aria-pressed', String(showEditor));
  els.edit.textContent = showEditor ? 'Done' : 'Edit';
  els.copy.disabled = !state.ok;
  const hasContent = state.original.trim() !== '';
  let html = '';
  if (!showEditor) {
    if (!hasContent) html = '<div class="notice">Choose a file on the left, or open Scratch and paste a source.</div>';
    else if (state.view === 'original') html = plainView('original');
    else if (!state.ok) html = `<div class="notice error"><strong>Cannot format this source.</strong> ${escapeHtml(state.error || 'Waiting for the formatter.')}</div>` + (state.error ? plainView('original') : '');
    else html = state.view === 'diff' ? diffView() : plainView('formatted');
    els.viewport.innerHTML = html;
    if (state.view !== 'original' && !state.ok) state.hunks = [];
  }
  state.hunk = -1;
  updateChangeNav();
  updateFoot();
}

function updateChangeNav() {
  const count = state.hunks.length;
  els.changesNav.hidden = state.editing || count === 0;
  els.changePosition.textContent = state.hunk < 0 ? `${count} ${count === 1 ? 'change' : 'changes'}` : `${state.hunk + 1} of ${count}`;
}

function goToChange(step) {
  const count = state.hunks.length;
  if (!count) return;
  state.hunk = state.hunk < 0 ? (step > 0 ? 0 : count - 1) : (state.hunk + step + count) % count;
  const row = document.getElementById(state.hunks[state.hunk]);
  if (row) {
    row.scrollIntoView({ block: 'center' });
    row.classList.remove('pulse');
    void row.offsetWidth;
    row.classList.add('pulse');
  }
  updateChangeNav();
}

function updateFoot() {
  const lines = state.lines.original.length;
  const bytes = new TextEncoder().encode(state.original).length;
  els.foot.textContent = state.original === '' ? '' : `${lines.toLocaleString()} lines, ${bytes.toLocaleString()} bytes`;
}

// ---------------------------------------------------------------- files

async function openFile(path) {
  setStatus('Loading', '', true);
  try {
    const text = await host.read(path);
    state.current = path;
    state.editing = false;
    setOriginal(text);
    els.editor.value = text;
    els.crumb.textContent = path;
    els.crumb.title = path;
    history.replaceState(null, '', '#' + encodeURIComponent(path));
    tree.select(path);
    els.viewport.scrollTop = 0;
    if (!WIDE.matches) setPanel('files', false);
    render();
    scheduleReformat(0);
  } catch (error) {
    setStatus(String(error.message || error), 'error', false);
  }
}

function openScratch() {
  state.current = '';
  state.editing = true;
  setOriginal(els.editor.value);
  els.crumb.textContent = 'Scratch';
  els.crumb.title = '';
  history.replaceState(null, '', location.pathname);
  tree.select('');
  render();
  els.editor.focus();
  if (!WIDE.matches) setPanel('files', false);
}

const tree = new FileTree(els.tree, els.count, (path) => (path === '' ? openScratch() : openFile(path)));

// ---------------------------------------------------------------- events

function initEvents() {
  wireSegmented(els.view, (value) => { state.view = value; state.editing = false; render(); });
  els.filter.addEventListener('input', () => tree.setFilter(els.filter.value));
  els.whitespace.addEventListener('change', () => { state.whitespace = els.whitespace.checked; render(); });
  els.edit.addEventListener('click', () => {
    if (state.editing) {
      setOriginal(els.editor.value);
      state.editing = false;
      render();
      scheduleReformat(0);
    } else {
      els.editor.value = state.original;
      state.editing = true;
      render();
      els.editor.focus();
    }
  });
  els.editor.addEventListener('input', () => {
    if (state.current !== '') { state.current = ''; els.crumb.textContent = 'Scratch (edited)'; tree.select(''); }
    setOriginal(els.editor.value);
    updateFoot();
    scheduleReformat(400);
  });
  els.viewport.addEventListener('click', (event) => {
    const fold = event.target.closest('.fold');
    if (!fold) return;
    for (let i = Number(fold.dataset.fold); i < Number(fold.dataset.end); i++) state.expanded.add(i);
    const top = els.viewport.scrollTop;
    render();
    els.viewport.scrollTop = top;
  });
  els.prev.addEventListener('click', () => goToChange(-1));
  els.next.addEventListener('click', () => goToChange(1));
  document.addEventListener('keydown', (event) => {
    if (event.metaKey || event.ctrlKey || event.altKey || /^(INPUT|TEXTAREA|SELECT)$/.test(event.target.tagName) || event.target.closest('.tree')) return;
    if (event.key === 'n') goToChange(1);
    else if (event.key === 'p') goToChange(-1);
  });
  els.copy.addEventListener('click', async () => {
    await navigator.clipboard.writeText(state.formatted);
    els.copy.textContent = 'Copied';
    setTimeout(() => { els.copy.textContent = 'Copy formatted'; }, 1200);
  });
  els.reset.addEventListener('click', () => { state.options = defaultOptions(); saveOptions(); renderOptions(); scheduleReformat(0); });
  els.copyOptions.addEventListener('click', () => navigator.clipboard.writeText(els.optionsJson.textContent));
}

// ---------------------------------------------------------------- host

// How the page reaches the repository and the formatter. This one speaks to the local development
// server (serve.py); a WebAssembly build would replace these three functions.
const host = {
  async list() {
    const response = await fetch('/api/files');
    if (!response.ok) throw new Error('Cannot list files');
    return (await response.json()).files;
  },
  async read(path) {
    const response = await fetch('/api/file?path=' + encodeURIComponent(path));
    if (!response.ok) throw new Error(`Cannot read ${path}`);
    return response.text();
  },
  async format(source, options) {
    const response = await fetch('/api/format', {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ source, ...options }),
    });
    return response.json();
  },
};

(async function start() {
  initTheme();
  initPanels();
  renderOptions();
  initEvents();
  render();
  setStatus('Loading files', '', true);
  try {
    state.files = await host.list();
    tree.setFiles(state.files);
    setStatus('', '', false);
    const wanted = decodeURIComponent(location.hash.slice(1));
    if (wanted && state.files.some((file) => file.path === wanted)) openFile(wanted);
    else tree.select('');
  } catch (error) {
    setStatus(String(error.message || error), 'error', false);
  }
})();
