// The playground: browse the repository's Chibi sources, set the formatter's options, and read the
// result as formatted code, as the changes against the original, or as the original. Highlighting
// comes from the host, which runs source_highlight on the same parse it formats, so the page colors
// code exactly as the documentation site does. The page reaches its host through the three calls of
// `host` at the bottom, so a WebAssembly formatter can replace the local server without a change here.
'use strict';

const $ = (id) => document.getElementById(id);
const els = {
  app: $('app'), crumb: $('crumb'), status: $('status'), statusText: $('status-text'), spinner: document.querySelector('.spinner'),
  filter: $('filter'), count: $('count'), tree: $('tree'), view: $('view'), theme: $('theme'),
  viewport: $('viewport'), editor: $('editor'), foot: $('foot'), edit: $('edit'), copy: $('copy'), whitespace: $('whitespace'),
  changesNav: $('changes-nav'), changePosition: $('change-position'), prev: $('prev-change'), next: $('next-change'),
  optionsBody: $('options-body'), config: $('config'), reset: $('reset'), copyConfig: $('copy-config'),
  scrim: $('scrim'), tip: $('tip'), toggleFiles: $('toggle-files'), toggleOptions: $('toggle-options'),
};

const CHUNK_LINES = 120;     // lines per block, so the browser can skip blocks that are off screen
const CONTEXT_LINES = 3;     // unchanged lines kept around a change in the Changes view
const SLOW_MS = 1500;        // after this long, say that the formatter may be starting
const TIP_DELAY_MS = 450;    // before the first tooltip; later ones while one is open show at once
const WIDE = window.matchMedia('(min-width: 1100px)');   // side panels sit beside the code; narrower, they slide over it

const state = {
  files: [], current: '', original: '', formatted: '', error: '', ok: false,
  view: 'formatted', editing: true, whitespace: false,
  options: loadOptions(),
  lines: { original: [], formatted: [] },      // each side's text, split into lines
  html: { original: [], formatted: [] },       // the same lines as highlighted HTML from the host
  diff: null, ops: null, hunks: [], hunk: -1,
  expanded: new Set(), request: 0, timer: 0, slow: 0,
};

// ---------------------------------------------------------------- settings

function loadOptions() {
  const options = defaultOptions();
  try {
    const saved = JSON.parse(localStorage.getItem('chibi-format-options-2') || '{}');
    for (const key of Object.keys(options)) if (key in saved && typeof saved[key] === typeof options[key]) options[key] = saved[key];
  } catch (_) { /* a corrupt value falls back to the defaults */ }
  return options;
}

function saveOptions() {
  try { localStorage.setItem('chibi-format-options-2', JSON.stringify(state.options)); } catch (_) { /* private mode */ }
}

// ---------------------------------------------------------------- small widgets

// A row of buttons of which one is on, as a native radio group: arrows move and select.
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
    const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[event.key];
    if (at < 0 || !step) return;
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

function escapeHtml(text) {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// ---------------------------------------------------------------- theme and panels

function initTheme() {
  setSegmented(els.theme, document.documentElement.dataset.theme || 'system');
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
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !WIDE.matches) { setPanel('files', false); setPanel('options', false); }
  });
}

// ---------------------------------------------------------------- tooltips

// One tooltip for the options pane. It waits before the first one, so passing over the pane does
// not flash it, then follows the pointer or focus from option to option without the wait.
const tip = { timer: 0, open: false, closed: 0 };

function showTip(label) {
  const item = optionByKey(label.closest('.option').dataset.key);
  clearTimeout(tip.timer);
  const reveal = () => {
    els.tip.replaceChildren(document.createTextNode(item.help));
    if (item.also) {
      const also = document.createElement('span');
      also.className = 'also';
      also.textContent = `Like ${item.also}`;
      els.tip.appendChild(also);
    }
    const instant = tip.open || performance.now() - tip.closed < 300;
    els.tip.toggleAttribute('data-instant', instant);
    els.tip.hidden = false;
    const box = label.getBoundingClientRect(), pane = els.optionsBody.getBoundingClientRect();
    const width = els.tip.offsetWidth, height = els.tip.offsetHeight;
    // To the left of the pane, level with the option; under it when the pane is a drawer at the edge.
    let left = pane.left - width - 12, top = box.top + box.height / 2 - height / 2;
    els.tip.style.transformOrigin = 'right center';
    if (left < 8) { left = Math.max(8, pane.left + 12); top = box.bottom + 6; els.tip.style.transformOrigin = 'top left'; }
    els.tip.style.left = `${left}px`;
    els.tip.style.top = `${Math.max(8, Math.min(top, window.innerHeight - height - 8))}px`;
    requestAnimationFrame(() => els.tip.removeAttribute('data-hidden'));
    tip.open = true;
  };
  if (tip.open || performance.now() - tip.closed < 300) reveal();
  else tip.timer = setTimeout(reveal, TIP_DELAY_MS);
}

function hideTip() {
  clearTimeout(tip.timer);
  if (!tip.open) return;
  tip.open = false;
  tip.closed = performance.now();
  els.tip.setAttribute('data-hidden', '');
  setTimeout(() => { if (!tip.open) els.tip.hidden = true; }, 140);
}

// ---------------------------------------------------------------- options pane

function optionControl(item) {
  if (item.planned) {
    const tag = document.createElement('span');
    tag.className = 'tag';
    tag.textContent = 'Planned';
    return tag;
  }
  if (item.kind === 'bool') {
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = state.options[item.key];
    box.setAttribute('aria-labelledby', `label-${item.key}`);
    box.addEventListener('change', () => setOption(item.key, box.checked));
    return box;
  }
  if (item.kind === 'choice') {
    const group = document.createElement('div');
    group.className = 'segmented';
    group.setAttribute('role', 'radiogroup');
    group.setAttribute('aria-labelledby', `label-${item.key}`);
    for (const choice of item.choices) {
      const button = document.createElement('button');
      button.type = 'button';
      button.setAttribute('role', 'radio');
      button.dataset.value = String(choice.value);
      button.textContent = choice.label;
      group.appendChild(button);
    }
    setSegmented(group, state.options[item.key]);
    wireSegmented(group, (value) => setOption(item.key, item.choices.find((choice) => String(choice.value) === value).value));
    return group;
  }
  const stepper = document.createElement('div');
  stepper.className = 'stepper';
  const input = document.createElement('input');
  input.type = 'text';
  input.inputMode = 'numeric';
  input.setAttribute('aria-labelledby', `label-${item.key}`);
  const show = () => { input.value = state.options[item.key] === 0 && item.zero ? item.zero : String(state.options[item.key]); };
  const step = (delta) => {
    const typed = parseInt(input.value, 10);
    let value = Number.isNaN(typed) ? (input.value === item.zero ? 0 : item.default) : typed;
    value += delta * (item.step || 1);
    // A width below 20 is not a width: stepping down from 20 turns wrapping off, stepping up from off starts at 80.
    if (item.key === 'preferred_width') value = delta < 0 && value < 20 ? 0 : delta > 0 && state.options[item.key] === 0 ? 80 : value;
    value = Math.min(item.max, Math.max(item.min, value));
    if (item.key === 'preferred_width' && value > 0 && value < 20) value = 20;
    setOption(item.key, value);
    show();
  };
  const minus = document.createElement('button'), plus = document.createElement('button');
  minus.type = plus.type = 'button';
  minus.textContent = '−';
  plus.textContent = '+';
  minus.setAttribute('aria-label', `Decrease ${item.label}`);
  plus.setAttribute('aria-label', `Increase ${item.label}`);
  minus.addEventListener('click', () => step(-1));
  plus.addEventListener('click', () => step(1));
  input.addEventListener('change', () => step(0));
  input.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowUp' || event.key === 'ArrowDown') { event.preventDefault(); step(event.key === 'ArrowUp' ? 1 : -1); }
  });
  show();
  stepper.append(minus, input, plus);
  return stepper;
}

function renderOptions() {
  const fragment = document.createDocumentFragment();
  for (const group of OPTION_GROUPS) {
    const section = document.createElement('section');
    section.className = 'group';
    const heading = document.createElement('h3');
    heading.textContent = group.name;
    section.appendChild(heading);
    for (const item of group.items) {
      const row = document.createElement('div');
      row.className = 'option' + (item.planned ? ' planned' : '');
      row.dataset.key = item.key;
      const label = document.createElement('span');
      label.className = 'option-label';
      label.id = `label-${item.key}`;
      label.textContent = item.label;
      label.tabIndex = 0;
      // The explanation is for screen readers too, not only for the pointer.
      const description = document.createElement('span');
      description.id = `help-${item.key}`;
      description.hidden = true;
      description.textContent = item.help;
      label.setAttribute('aria-describedby', description.id);
      row.append(label, optionControl(item), description);
      section.appendChild(row);
    }
    fragment.appendChild(section);
  }
  els.optionsBody.replaceChildren(fragment);
  refreshOptions();
}

// Marks the options that differ from their defaults and shows them as a .chibi-format file.
function refreshOptions() {
  for (const row of els.optionsBody.querySelectorAll('.option')) {
    const item = optionByKey(row.dataset.key);
    row.classList.toggle('modified', !item.planned && state.options[item.key] !== item.default);
  }
  const text = configText(state.options);
  els.config.textContent = text;
  els.reset.disabled = text === '';
  els.copyConfig.disabled = text === '';
  document.documentElement.style.setProperty('--tab-size', String(state.options.indent_width));
}

function setOption(key, value) {
  state.options[key] = value;
  saveOptions();
  refreshOptions();
  scheduleReformat(120);
}

// ---------------------------------------------------------------- formatting

function splitText(text) {
  if (text === '') return [];
  const lines = text.split('\n').map((line) => (line.endsWith('\r') ? line.slice(0, -1) : line));
  if (lines.length > 1 && lines[lines.length - 1] === '') lines.pop();
  return lines;
}

function scheduleReformat(delay) {
  clearTimeout(state.timer);
  state.timer = setTimeout(reformat, delay);
}

function setOriginal(text) {
  state.original = text;
  state.lines.original = splitText(text);
  state.html.original = state.lines.original.map(escapeHtml);
}

async function reformat() {
  const id = ++state.request;
  const source = state.original;
  clearTimeout(state.slow);
  if (source.trim() === '') {
    Object.assign(state, { formatted: '', error: '', ok: false, diff: null, ops: null });
    state.lines.formatted = [];
    state.html.formatted = [];
    setStatus('', '', false);
    render();
    return;
  }
  setStatus('Formatting', '', true);
  state.slow = setTimeout(() => {
    if (id === state.request) setStatus('Starting the formatter, about 30 seconds', '', true);
  }, SLOW_MS);
  try {
    const result = await host.format(source, state.options);
    if (id !== state.request) return;
    clearTimeout(state.slow);
    if (result.original_html !== undefined && result.original_html !== '') state.html.original = splitText(result.original_html);
    if (!result.ok) {
      Object.assign(state, { formatted: '', error: result.error, ok: false, diff: null, ops: null });
      state.lines.formatted = [];
      state.html.formatted = [];
      setStatus(result.error, 'error', false);
    } else {
      Object.assign(state, { formatted: result.text, error: '', ok: true });
      state.lines.formatted = splitText(result.text);
      state.html.formatted = splitText(result.formatted_html);
      state.diff = diffLines(state.lines.original, state.lines.formatted);
      state.ops = state.diff ? diffOps(state.lines.original, state.lines.formatted, state.diff) : null;
      state.expanded.clear();
      const changed = state.diff ? state.diff.b.size + state.diff.a.size : -1;
      if (state.original === result.text) setStatus('Already formatted', 'good', false);
      else if (changed === 0) setStatus('Only line endings or the final newline change', 'warn', false);
      else if (changed < 0) setStatus('Formatted; too many changes to compare', 'warn', false);
      else setStatus(`${state.diff.b.size.toLocaleString()} new, ${state.diff.a.size.toLocaleString()} removed lines`, 'warn', false);
    }
    render();
  } catch (error) {
    if (id !== state.request) return;
    clearTimeout(state.slow);
    setStatus(String(error.message || error), 'error', false);
  }
}

// ---------------------------------------------------------------- code view

function lineHtml(html) {
  if (html === '' || html === undefined) return '​';
  if (!state.whitespace) return html;
  return html.replace(/^[ \t]+/, (run) => `<span class="ws">${run.replace(/ /g, '·').replace(/\t/g, '→')}</span>`);
}

function chunked(rows) {
  const parts = [];
  for (let i = 0; i < rows.length; i += CHUNK_LINES) parts.push(`<div class="chunk">${rows.slice(i, i + CHUNK_LINES).join('')}</div>`);
  return parts.join('');
}

// The first line of each run of consecutive changed lines.
function groupStarts(changed, count) {
  const starts = [];
  for (let i = 0; i < count; i++) if (changed.has(i) && !changed.has(i - 1)) starts.push(i);
  return starts;
}

function plainView(side) {
  const html = state.html[side];
  const changed = state.diff ? (side === 'formatted' ? state.diff.b : state.diff.a) : new Set();
  const mark = side === 'formatted' ? ' chg' : ' gone';
  const rows = html.map((line, i) =>
    `<div class="ln${changed.has(i) ? mark : ''}" id="L${i}"><span class="no">${i + 1}</span><span class="tx">${lineHtml(line)}</span></div>`);
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
      while (end < ops.length && !show[end] && !state.expanded.has(end)) end++;
      const n = end - i;
      rows.push(`<button type="button" class="fold" data-fold="${i}" data-end="${end}">${n.toLocaleString()} unchanged ${n === 1 ? 'line' : 'lines'}</button>`);
      i = end;
      continue;
    }
    const op = ops[i], previous = ops[i - 1];
    const id = op.kind !== '=' && (!previous || previous.kind === '=') ? `H${state.hunks.length}` : '';
    if (id) state.hunks.push(id);
    const html = op.kind === '-' ? state.html.original[op.a] : state.html.formatted[op.b];
    const kind = op.kind === '+' ? 'add' : op.kind === '-' ? 'del' : 'ctx';
    rows.push(`<div class="ln ${kind}"${id ? ` id="${id}"` : ''}><span class="no">${op.a >= 0 ? op.a + 1 : ''}</span>` +
      `<span class="no">${op.b >= 0 ? op.b + 1 : ''}</span><span class="mk">${op.kind === '=' ? '' : op.kind === '-' ? '−' : '+'}</span>` +
      `<span class="tx">${lineHtml(html)}</span></div>`);
    i++;
  }
  return chunked(rows);
}

function render() {
  els.editor.hidden = !state.editing;
  els.viewport.hidden = state.editing;
  els.edit.setAttribute('aria-pressed', String(state.editing));
  els.edit.textContent = state.editing ? 'Done' : 'Edit';
  els.copy.disabled = !state.ok;
  state.hunks = [];
  if (!state.editing) {
    let html;
    if (state.original.trim() === '') html = '<div class="notice">Choose a file, or open Scratch and paste a source.</div>';
    else if (state.view === 'original') html = plainView('original');
    else if (!state.ok && state.error) {
      html = `<div class="notice error"><strong>Cannot format this source.</strong> ${escapeHtml(state.error)}</div>` + plainView('original');
      state.hunks = [];
    } else if (!state.ok) html = '<div class="notice">Formatting…</div>';
    else html = state.view === 'diff' ? diffView() : plainView('formatted');
    els.viewport.innerHTML = html;
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
  if (state.original === '') { els.foot.textContent = ''; return; }
  const bytes = new TextEncoder().encode(state.original).length;
  const after = state.ok ? `, formatted ${state.lines.formatted.length.toLocaleString()} lines` : '';
  els.foot.textContent = `${state.lines.original.length.toLocaleString()} lines, ${bytes.toLocaleString()} bytes${after}`;
}

// ---------------------------------------------------------------- files

async function openFile(path) {
  setStatus('Loading', '', true);
  try {
    const text = await host.read(path);
    state.current = path;
    state.editing = false;
    state.ok = false;
    state.error = '';
    setOriginal(text);
    els.editor.value = text;
    els.crumb.textContent = path;
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
  history.replaceState(null, '', location.pathname);
  tree.select('');
  render();
  els.editor.focus();
  if (!WIDE.matches) setPanel('files', false);
}

const tree = new FileTree(els.tree, els.count, (path) => (path === '' ? openScratch() : openFile(path)));

// ---------------------------------------------------------------- events

function initEvents() {
  wireSegmented(els.view, (value) => {
    state.view = value;
    if (state.editing) { setOriginal(els.editor.value); state.editing = false; scheduleReformat(0); }
    render();
  });
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
    if (state.current !== '') { state.current = ''; els.crumb.textContent = 'Scratch, edited'; tree.select(''); }
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
    setTimeout(() => { els.copy.textContent = 'Copy'; }, 1200);
  });
  els.copyConfig.addEventListener('click', async () => {
    await navigator.clipboard.writeText(els.config.textContent + '\n');
    els.copyConfig.textContent = 'Copied';
    setTimeout(() => { els.copyConfig.textContent = 'Copy'; }, 1200);
  });
  els.reset.addEventListener('click', () => { state.options = defaultOptions(); saveOptions(); renderOptions(); scheduleReformat(0); });
  els.optionsBody.addEventListener('pointerover', (event) => { const label = event.target.closest('.option-label'); if (label) showTip(label); });
  els.optionsBody.addEventListener('pointerout', (event) => { if (event.target.closest('.option-label')) hideTip(); });
  els.optionsBody.addEventListener('focusin', (event) => { const label = event.target.closest('.option-label'); if (label) showTip(label); });
  els.optionsBody.addEventListener('focusout', hideTip);
  els.options.addEventListener('scroll', hideTip, { passive: true });
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
  // Resolves to {ok, text | error, original_html, formatted_html}.
  async format(source, options) {
    const response = await fetch('/api/format', {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ source, options }),
    });
    return response.json();
  },
};

(async function start() {
  els.options = $('options');
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
