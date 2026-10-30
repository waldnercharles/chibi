// The playground page: browse the repository's Chibi sources, edit or paste a source, and see the
// formatter's output with the current settings. It talks to a host through three calls, defined at
// the bottom, so the same page can later sit on a WebAssembly formatter instead of a local server.
'use strict';

const $ = (id) => document.getElementById(id);
const els = {
  indent: $('indent'), tabs: $('tabs'), status: $('status'), filter: $('filter'), count: $('count'),
  tree: $('tree'), name: $('name'), source: $('source'), original: $('original-view'),
  formatted: $('formatted'), copy: $('copy'),
};

const state = { files: [], current: '', request: 0, timer: 0, lastFormatted: '' };

function settings() {
  const width = Math.min(16, Math.max(1, parseInt(els.indent.value, 10) || 4));
  return { indent_width: width, use_tabs: els.tabs.checked };
}

function saveSettings() {
  localStorage.setItem('chibi-format-settings', JSON.stringify(settings()));
}

function loadSettings() {
  try {
    const saved = JSON.parse(localStorage.getItem('chibi-format-settings') || '{}');
    if (saved.indent_width) els.indent.value = saved.indent_width;
    els.tabs.checked = !!saved.use_tabs;
  } catch (_) { /* ignore a corrupt value */ }
}

function setStatus(text, error) {
  els.status.textContent = text;
  els.status.className = error ? 'error' : '';
}

// Renders `lines` into `into`, one block per line, marking those whose index is in `changed`.
function render(into, lines, changed) {
  const fragment = document.createDocumentFragment();
  for (let i = 0; i < lines.length; i++) {
    const row = document.createElement('span');
    row.className = changed && changed.has(i) ? 'line changed' : 'line';
    row.dataset.n = String(i + 1);
    row.textContent = lines[i] === '' ? ' ' : lines[i];
    fragment.appendChild(row);
  }
  into.replaceChildren(fragment);
}

function splitLines(text) {
  const lines = text.split('\n');
  if (lines.length > 1 && lines[lines.length - 1] === '') lines.pop();
  return lines;
}

function show(original, formatted) {
  const before = splitLines(original), after = splitLines(formatted);
  const diff = before.length + after.length > 40000 ? null : diffLines(before, after);
  render(els.original, before, diff && diff.a);
  render(els.formatted, after, diff && diff.b);
  els.original.scrollTop = els.source.scrollTop;
  if (diff) {
    const changed = diff.b.size;
    setStatus(changed === 0 ? 'Already formatted' : `${changed} line${changed === 1 ? '' : 's'} would change`);
  } else {
    setStatus('Formatted');
  }
}

async function reformat() {
  const id = ++state.request;
  const original = els.source.value;
  if (original.trim() === '') {
    render(els.original, [], null);
    render(els.formatted, [], null);
    setStatus('');
    return;
  }
  setStatus('Formatting…');
  try {
    const result = await host.format(original, settings());
    if (id !== state.request) return;
    if (!result.ok) {
      render(els.original, splitLines(original), null);
      els.formatted.textContent = '';
      setStatus(result.error, true);
      return;
    }
    state.lastFormatted = result.text;
    show(original, result.text);
  } catch (error) {
    if (id === state.request) setStatus(String(error.message || error), true);
  }
}

function schedule() {
  clearTimeout(state.timer);
  state.timer = setTimeout(reformat, 300);
}

function buildTree(filter) {
  const needle = filter.trim().toLowerCase();
  const root = {};
  let shown = 0;
  for (const file of state.files) {
    if (needle && !file.path.toLowerCase().includes(needle)) continue;
    shown++;
    let node = root;
    const parts = file.path.split('/');
    for (let i = 0; i < parts.length - 1; i++) node = node[parts[i]] = node[parts[i]] || {};
    node[parts[parts.length - 1] + '\0'] = file.path;
  }
  els.count.textContent = `${shown} of ${state.files.length} files`;
  const open = needle !== '';
  const build = (node, into, depth) => {
    for (const key of Object.keys(node).sort((a, b) => (a.endsWith('\0') - b.endsWith('\0')) || a.localeCompare(b))) {
      if (key.endsWith('\0')) {
        const link = document.createElement('a');
        link.className = 'file' + (node[key] === state.current ? ' current' : '');
        link.textContent = key.slice(0, -1);
        link.title = node[key];
        link.dataset.path = node[key];
        into.appendChild(link);
      } else {
        const details = document.createElement('details');
        details.open = open || depth < 1;
        const summary = document.createElement('summary');
        summary.textContent = key;
        details.appendChild(summary);
        build(node[key], details, depth + 1);
        into.appendChild(details);
      }
    }
  };
  const fragment = document.createDocumentFragment();
  build(root, fragment, 0);
  els.tree.replaceChildren(fragment);
}

async function open(path) {
  setStatus('Loading…');
  try {
    const text = await host.read(path);
    state.current = path;
    els.name.textContent = path;
    els.source.value = text;
    els.source.scrollTop = 0;
    history.replaceState(null, '', '#' + encodeURIComponent(path));
    for (const link of els.tree.querySelectorAll('.file')) link.classList.toggle('current', link.dataset.path === path);
    reformat();
  } catch (error) {
    setStatus(String(error.message || error), true);
  }
}

els.tree.addEventListener('click', (event) => {
  const link = event.target.closest('.file');
  if (link) open(link.dataset.path);
});
els.filter.addEventListener('input', () => buildTree(els.filter.value));
els.source.addEventListener('input', () => {
  state.current = '';
  els.name.textContent = 'Scratch';
  // Show the edited text at once; the formatted pane catches up after the pause.
  render(els.original, splitLines(els.source.value), null);
  schedule();
});
els.source.addEventListener('scroll', () => { els.original.scrollTop = els.source.scrollTop; });
for (const input of [els.indent, els.tabs]) {
  input.addEventListener('change', () => { saveSettings(); els.original.style.tabSize = els.formatted.style.tabSize = String(settings().indent_width); schedule(); });
}
els.copy.addEventListener('click', () => navigator.clipboard.writeText(state.lastFormatted));

// The host: how the page reaches the repository and the formatter. This one speaks to the local
// development server (serve.py); a WebAssembly build would replace these three functions.
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
  loadSettings();
  try {
    state.files = await host.list();
    buildTree('');
    const wanted = decodeURIComponent(location.hash.slice(1));
    if (wanted && state.files.some((file) => file.path === wanted)) open(wanted);
    else setStatus('Pick a file, or paste a source on the left');
  } catch (error) {
    setStatus(String(error.message || error), true);
  }
})();
