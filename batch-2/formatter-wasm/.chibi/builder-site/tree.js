// The file tree: folders that open and close, a filter that opens every folder with a match, and
// keyboard movement (arrows, Home, End, Enter) over the rows that are showing, as in a native tree.
class FileTree {
  constructor(container, countEl, onOpen) {
    this.container = container;
    this.countEl = countEl;
    this.onOpen = onOpen;
    this.root = this.folder('', '');
    this.open = new Set(['']);
    this.filter = '';
    this.current = null;      // the path that is open, '' for the scratch entry
    this.focused = null;      // the path whose row holds the tab stop
    this.rows = [];
    this.total = 0;
    container.addEventListener('click', (event) => this.click(event));
    container.addEventListener('keydown', (event) => this.key(event));
  }

  folder(name, path) { return { name, path, folders: new Map(), files: [], count: 0 }; }

  setFiles(files) {
    this.root = this.folder('', '');
    this.total = files.length;
    for (const file of files) {
      let node = this.root;
      const parts = file.path.split('/');
      node.count++;
      for (let i = 0; i < parts.length - 1; i++) {
        const path = parts.slice(0, i + 1).join('/');
        if (!node.folders.has(parts[i])) node.folders.set(parts[i], this.folder(parts[i], path));
        node = node.folders.get(parts[i]);
        node.count++;
      }
      node.files.push({ name: parts[parts.length - 1], path: file.path, bytes: file.bytes });
    }
    this.render();
  }

  setFilter(text) {
    this.filter = text.trim().toLowerCase();
    this.render();
  }

  // Shows the file at `path` (opening the folders above it) and marks it current.
  select(path) {
    this.current = path;
    this.focused = path;
    const parts = path.split('/');
    for (let i = 1; i < parts.length; i++) this.open.add(parts.slice(0, i).join('/'));
    this.render();
    const row = this.rowFor(path);
    if (row) row.scrollIntoView({ block: 'nearest' });
  }

  matches(file) { return !this.filter || file.path.toLowerCase().includes(this.filter); }

  // Rows for `node`'s children that are showing, appended to this.rows with their depth.
  collect(node, depth) {
    const folders = [...node.folders.values()].sort((a, b) => a.name.localeCompare(b.name));
    const files = [...node.files].sort((a, b) => a.name.localeCompare(b.name));
    let shown = 0;
    for (const folder of folders) {
      const start = this.rows.length;
      const row = { kind: 'folder', node: folder, depth };
      this.rows.push(row);
      const open = this.filter ? true : this.open.has(folder.path);
      row.open = open;
      const inside = this.collect(folder, depth + 1);
      row.shown = inside;
      if (inside === 0) this.rows.length = start;      // a folder with no match disappears
      else {
        shown += inside;
        if (!open) this.rows.length = start + 1;
      }
    }
    for (const file of files) {
      if (!this.matches(file)) continue;
      shown++;
      this.rows.push({ kind: 'file', file, depth });
    }
    return shown;
  }

  render() {
    this.rows = [];
    const shown = this.collect(this.root, 0);
    this.countEl.textContent = this.filter ? `${shown} of ${this.total} files` : `${this.total} files`;
    const fragment = document.createDocumentFragment();
    fragment.appendChild(this.rowElement({ kind: 'scratch', depth: 0 }));
    for (const row of this.rows) fragment.appendChild(this.rowElement(row));
    if (shown === 0) {
      const empty = document.createElement('div');
      empty.className = 'tree-empty';
      empty.textContent = 'No file matches.';
      fragment.appendChild(empty);
    }
    this.container.replaceChildren(fragment);
    this.ensureTabStop();
  }

  rowPath(row) {
    return row.kind === 'folder' ? row.node.path : row.kind === 'file' ? row.file.path : '';
  }

  rowElement(row) {
    const element = document.createElement('div');
    element.setAttribute('role', 'treeitem');
    element.className = 'row ' + row.kind;
    element.style.setProperty('--depth', String(row.depth));
    element.setAttribute('aria-level', String(row.depth + 1));
    element.dataset.kind = row.kind;
    element.dataset.depth = String(row.depth);
    element.dataset.path = this.rowPath(row);
    element.tabIndex = -1;
    const label = document.createElement('span');
    label.className = 'label';
    if (row.kind === 'folder') {
      element.setAttribute('aria-expanded', String(row.open));
      const chevron = document.createElement('span');
      chevron.className = 'chevron';
      chevron.setAttribute('aria-hidden', 'true');
      element.appendChild(chevron);
      label.textContent = row.node.name;
      const count = document.createElement('span');
      count.className = 'badge';
      count.textContent = String(this.filter ? row.shown : row.node.count);
      element.append(label, count);
    } else if (row.kind === 'file') {
      const name = row.file.name;
      const dot = name.lastIndexOf('.');
      const stem = dot > 0 ? name.slice(0, dot) : name, extension = dot > 0 ? name.slice(dot) : '';
      this.labelWith(label, stem);
      const ext = document.createElement('span');
      ext.className = 'ext';
      ext.textContent = extension;
      label.appendChild(ext);
      element.title = `${row.file.path} (${row.file.bytes.toLocaleString()} bytes)`;
      element.appendChild(label);
      if (row.file.path === this.current) { element.classList.add('current'); element.setAttribute('aria-selected', 'true'); }
    } else {
      label.textContent = 'Scratch';
      element.appendChild(label);
      const hint = document.createElement('span');
      hint.className = 'badge';
      hint.textContent = 'paste a source';
      element.appendChild(hint);
      if (this.current === '') { element.classList.add('current'); element.setAttribute('aria-selected', 'true'); }
    }
    return element;
  }

  // Sets `text` into `label`, wrapping the part the filter matched.
  labelWith(label, text) {
    const at = this.filter ? text.toLowerCase().indexOf(this.filter) : -1;
    if (at < 0) { label.append(text); return; }
    const mark = document.createElement('mark');
    mark.textContent = text.slice(at, at + this.filter.length);
    label.append(text.slice(0, at), mark, text.slice(at + this.filter.length));
  }

  rowFor(path) {
    for (const element of this.container.querySelectorAll('.row')) if (element.dataset.path === path) return element;
    return null;
  }

  elements() { return [...this.container.querySelectorAll('.row')]; }

  ensureTabStop() {
    const elements = this.elements();
    const stop = elements.find((e) => e.dataset.path === this.focused) || elements.find((e) => e.classList.contains('current')) || elements[0];
    if (stop) stop.tabIndex = 0;
  }

  focusRow(element) {
    if (!element) return;
    for (const other of this.container.querySelectorAll('.row[tabindex="0"]')) other.tabIndex = -1;
    element.tabIndex = 0;
    this.focused = element.dataset.path;
    element.focus();
    element.scrollIntoView({ block: 'nearest' });
  }

  toggle(path) {
    if (this.open.has(path)) this.open.delete(path); else this.open.add(path);
    this.focused = path;
    this.render();
    this.focusRow(this.rowFor(path));
  }

  activate(element) {
    if (element.dataset.kind === 'folder') this.toggle(element.dataset.path);
    else {
      this.focused = element.dataset.path;
      this.onOpen(element.dataset.path);
    }
  }

  click(event) {
    const element = event.target.closest('.row');
    if (element) this.activate(element);
  }

  key(event) {
    const element = event.target.closest('.row');
    if (!element) return;
    const elements = this.elements();
    const at = elements.indexOf(element);
    const folder = element.dataset.kind === 'folder';
    const open = element.getAttribute('aria-expanded') === 'true';
    let handled = true;
    switch (event.key) {
      case 'ArrowDown': this.focusRow(elements[Math.min(at + 1, elements.length - 1)]); break;
      case 'ArrowUp': this.focusRow(elements[Math.max(at - 1, 0)]); break;
      case 'Home': this.focusRow(elements[0]); break;
      case 'End': this.focusRow(elements[elements.length - 1]); break;
      case 'ArrowRight':
        if (folder && !open) this.toggle(element.dataset.path);
        else if (folder) this.focusRow(elements[at + 1]);
        break;
      case 'ArrowLeft': {
        if (folder && open) { this.toggle(element.dataset.path); break; }
        const level = Number(element.getAttribute('aria-level'));
        for (let i = at - 1; i >= 0; i--) {
          if (Number(elements[i].getAttribute('aria-level')) < level) { this.focusRow(elements[i]); break; }
        }
        break;
      }
      case 'Enter': case ' ': this.activate(element); break;
      default: handled = false;
    }
    if (handled) event.preventDefault();
  }
}
