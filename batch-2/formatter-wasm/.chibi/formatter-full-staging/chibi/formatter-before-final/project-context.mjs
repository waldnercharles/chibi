import { contextPath, MAX_SOURCE_BYTES, sourceUrl } from './protocol.mjs';

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
// The compiler's native snapshot currently accepts at most 256 documents.
export const MAX_CONTEXT_FILES = 256;
export const MAX_CONTEXT_BYTES = 16 * 1024 * 1024;
const MAX_INDEX_FILES = 65536;
const MAX_INDEX_BYTES = 8 * 1024 * 1024;
const within = (root, path) => path === root || path.startsWith(root + '/');
const parent = (path) => path.slice(0, path.lastIndexOf('/'));

// Layout-only requests do not read the repository. Values arrive either from
// the public client or from its wire header, where they are strings.
export function needsProjectContext(options) {
  const compact = options.profile === 'compact_v1' || options.profile === 1 || options.profile === '1';
  const binding = options.binding_types ?? (compact ? 'omit_redundant' : 'preserve');
  const active = (value) => value !== undefined && !['preserve', 'preserve_aliases', 'inherit', '0', '-1', 0, -1].includes(value);
  if (active(binding) || ['local_types', 'constant_types', 'parameter_types', 'return_types',
    'member_types', 'public_types', 'type_spelling'].some((key) => active(options[key]))) return true;
  const body = options.function_body ?? (compact ? 'expression' : 'preserve');
  return body === 'expression' || body === 2 || body === '2' ||
    active(options.imports) || active(options.numeric_base);
}

export class ContextUnavailable extends Error {
  constructor(message) { super(message); this.name = 'ContextUnavailable'; }
}

function projectRoot(path) {
  if (path.startsWith('tools/compiler/') || path.startsWith('tools/docs/playground/wasm/')) return '/repo/tools/compiler';
  // Collection roots must be distinct. Their longer paths own files below them.
  if (path.startsWith('packages/core/') || path.startsWith('tools/graph/')) return '/repo';
  const directory = parent(path);
  return directory ? '/repo/' + directory : '/repo';
}

function rootsFor(path) {
  const roots = new Map([['project', projectRoot(path)], ['core', '/repo/packages/core'], ['graph', '/repo/tools/graph']]);
  const directory = '/repo/' + parent(path);
  // The formatter entry lives outside the compiler's project root while
  // importing project: packages from it. Give that document its own collection.
  if (![...roots.values()].some((root) => within(root, directory))) roots.set('document', directory);
  return roots;
}

function importDirectory(path, literal, roots) {
  if (typeof literal !== 'string' || /[\\\0]/.test(literal) || encoder.encode(literal).length >= 4096) {
    throw new ContextUnavailable('Invalid import path in ' + path);
  }
  const directory = '/repo/' + parent(path);
  let floor = '';
  for (const root of roots.values()) if (within(root, directory) && root.length > floor.length) floor = root;
  if (!floor) throw new ContextUnavailable('Source is outside the configured collections: ' + path);
  let base = directory;
  let relative = literal;
  const colon = literal.indexOf(':');
  if (colon >= 0) {
    floor = roots.get(literal.slice(0, colon));
    if (!floor) throw new ContextUnavailable('Unknown import collection in ' + path + ': ' + literal);
    base = floor;
    relative = literal.slice(colon + 1);
  }
  if ((colon < 0 && relative === '') || relative.startsWith('/')) {
    throw new ContextUnavailable('Invalid import path in ' + path + ': ' + literal);
  }
  const segments = base.split('/').filter(Boolean);
  const minimum = floor.split('/').filter(Boolean).length;
  for (const segment of relative.split('/')) {
    if (segment === '..') {
      if (segments.length <= minimum) throw new ContextUnavailable('Import escapes its collection in ' + path + ': ' + literal);
      segments.pop();
    } else if (segment !== '' && segment !== '.') segments.push(segment);
  }
  const resolved = '/' + segments.join('/');
  if (encoder.encode(resolved).length >= 4096) throw new ContextUnavailable('Import path is too long in ' + path);
  return resolved.slice('/repo/'.length);
}

// Metadata is supplied by the Wasm compiler, not by a second source parser.
// Static file contents and import lists are bounded LRU caches; only the current
// transitive package closure is serialized, never the whole source inventory.
export class ProjectContext {
  constructor({ base, fetch: fetcher = globalThis.fetch, imports, maxFiles = MAX_CONTEXT_FILES,
    maxBytes = MAX_CONTEXT_BYTES } = {}) {
    this.base = new URL('./', base);
    if (!['https:', 'http:'].includes(this.base.protocol) || this.base.username || this.base.password ||
        typeof fetcher !== 'function' || typeof imports !== 'function') throw new Error('Invalid project context configuration');
    if (!Number.isSafeInteger(maxFiles) || maxFiles < 1 || maxFiles > MAX_CONTEXT_FILES ||
        !Number.isSafeInteger(maxBytes) || maxBytes < 1 || maxBytes > MAX_CONTEXT_BYTES) throw new Error('Invalid project context bounds');
    // WorkerGlobalScope.fetch requires its global receiver in browsers.
    this.fetch = fetcher.bind(globalThis);
    this.imports = imports;
    this.maxFiles = maxFiles;
    this.maxBytes = maxBytes;
    this.inventoryPromise = null;
    this.files = new Map();
    this.fileBytes = 0;
    this.contexts = new Map();
    this.contextBytes = 0;
    this.edited = null;
  }

  async read(url, limit, optional = false) {
    let response;
    try { response = await this.fetch(url, { signal: AbortSignal.timeout(15000), redirect: 'error' }); }
    catch { throw new ContextUnavailable('Could not load project source ' + url.pathname); }
    if (optional && response.status === 404) return null;
    if (!response.ok) throw new ContextUnavailable('Could not load project source ' + url.pathname + ' (HTTP ' + response.status + ')');
    if (Number(response.headers?.get('Content-Length')) > limit) throw new ContextUnavailable('Project source exceeds its size limit: ' + url.pathname);
    // Stream the response so the limit bounds allocation even for a bad server.
    if (response.body?.getReader) {
      const reader = response.body.getReader();
      const chunks = [];
      let length = 0;
      try {
        for (;;) {
          const part = await reader.read();
          if (part.done) break;
          length += part.value.length;
          if (length > limit) { await reader.cancel(); throw new ContextUnavailable('Project source exceeds its size limit: ' + url.pathname); }
          chunks.push(part.value);
        }
        const bytes = new Uint8Array(length);
        let offset = 0;
        for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
        return { text: decoder.decode(bytes), bytes: length };
      } catch (error) {
        if (error instanceof ContextUnavailable) throw error;
        throw new ContextUnavailable('Could not decode project source ' + url.pathname);
      } finally { reader.releaseLock(); }
    }
    let text;
    try { text = await response.text(); }
    catch { throw new ContextUnavailable('Could not read project source ' + url.pathname); }
    const bytes = encoder.encode(text).length;
    if (bytes > limit) throw new ContextUnavailable('Project source exceeds its size limit: ' + url.pathname);
    return { text, bytes };
  }

  async inventory() {
    if (this.inventoryPromise) return this.inventoryPromise;
    this.inventoryPromise = (async () => {
      const records = new Map();
      for (const [name, optional] of [['files.json', false], ['context_files.json', true]]) {
        const response = await this.read(new URL(name, this.base), MAX_INDEX_BYTES, optional);
        if (response === null) continue;
        let manifest;
        try { manifest = JSON.parse(response.text); }
        catch { throw new ContextUnavailable('Invalid project source index: ' + name); }
        const entries = Array.isArray(manifest) ? manifest : manifest?.files;
        if (!Array.isArray(entries) || entries.length > MAX_INDEX_FILES) throw new ContextUnavailable('Invalid project source index: ' + name);
        for (const entry of entries) {
          try { contextPath(entry); }
          catch { throw new ContextUnavailable('Invalid project source index: ' + name); }
          if (!Number.isSafeInteger(entry.bytes) || entry.bytes < 0) throw new ContextUnavailable('Invalid project source size in ' + name);
          const previous = records.get(entry.path);
          if (previous && previous.bytes !== entry.bytes) throw new ContextUnavailable('Conflicting project source indexes: ' + entry.path);
          records.set(entry.path, entry);
          if (records.size > MAX_INDEX_FILES) throw new ContextUnavailable('Project source index exceeds its file limit');
        }
      }
      const packages = new Map();
      for (const path of [...records.keys()].sort()) {
        const directory = parent(path);
        if (!packages.has(directory)) packages.set(directory, []);
        packages.get(directory).push(path);
      }
      return { records, packages };
    })();
    try { return await this.inventoryPromise; }
    catch (error) { this.inventoryPromise = null; throw error; }
  }

  async importPaths(source, path) {
    let imports;
    try { imports = await this.imports(source); }
    catch (error) { throw new ContextUnavailable('Could not inspect imports in ' + path + ': ' + String(error.message || error)); }
    if (!Array.isArray(imports) || imports.length > this.maxFiles || imports.some((value) => typeof value !== 'string')) {
      throw new ContextUnavailable('Invalid compiler import metadata for ' + path);
    }
    return imports;
  }

  async file(path, record) {
    const cached = this.files.get(path);
    if (cached) { this.files.delete(path); this.files.set(path, cached); return cached; }
    const result = await this.read(sourceUrl(path, this.base), Math.min(MAX_SOURCE_BYTES, this.maxBytes));
    if (result.bytes !== record.bytes) throw new ContextUnavailable('Project source changed since its index was built: ' + path);
    const imports = await this.importPaths(result.text, path);
    const file = { ...result, imports };
    while (this.fileBytes + file.bytes > this.maxBytes && this.files.size) {
      const oldest = this.files.keys().next().value;
      this.fileBytes -= this.files.get(oldest).bytes;
      this.files.delete(oldest);
    }
    this.files.set(path, file);
    this.fileBytes += file.bytes;
    return file;
  }

  async load(reference, source) {
    const path = contextPath(reference);
    if (path === null || typeof source !== 'string') throw new ContextUnavailable('A repository source path is required for project formatting');
    const sourceBytes = encoder.encode(source).length;
    if (sourceBytes > MAX_SOURCE_BYTES || sourceBytes > this.maxBytes) throw new ContextUnavailable('Edited source exceeds its size limit');
    const index = await this.inventory();
    if (!index.records.has(path)) throw new ContextUnavailable('Selected source is not present in the project index: ' + path);
    let selectedImports;
    if (this.edited?.path === path && this.edited.source === source) selectedImports = this.edited.imports;
    else {
      selectedImports = await this.importPaths(source, path);
      this.edited = { path, source, imports: selectedImports };
    }
    const key = JSON.stringify([path, selectedImports]);
    const cached = this.contexts.get(key);
    if (cached) {
      this.contexts.delete(key); this.contexts.set(key, cached);
      return cached;
    }
    const roots = rootsFor(path);
    const selectedDirectory = parent(path);
    const selectedTests = path.endsWith('Tests.chibi') || selectedDirectory.endsWith('/tests');
    const paths = [path];
    const sources = [source];
    const present = new Set(paths);
    let totalBytes = sourceBytes;
    const addPackage = (directory) => {
      const members = index.packages.get(directory);
      if (!members) throw new ContextUnavailable('Imported package is not present in the project index: ' + directory);
      let eligible = false;
      for (const member of members) {
        // Match native_driver.PackageFiles: ordinary imports omit adjacent
        // test files; editing tests includes tests in that selected package.
        if (member.endsWith('Tests.chibi') && !(selectedTests && directory === selectedDirectory)) continue;
        eligible = true;
        if (present.has(member)) continue;
        if (paths.length >= this.maxFiles) throw new ContextUnavailable('Project import closure exceeds ' + this.maxFiles + ' files');
        present.add(member); paths.push(member);
      }
      if (!eligible) throw new ContextUnavailable('Imported package has no production source files: ' + directory);
    };
    addPackage(selectedDirectory);
    for (let at = 0; at < paths.length; at += 1) {
      let imports = selectedImports;
      if (at > 0) {
        const file = await this.file(paths[at], index.records.get(paths[at]));
        totalBytes += file.bytes;
        if (totalBytes > this.maxBytes) throw new ContextUnavailable('Project import closure exceeds ' + this.maxBytes + ' UTF-8 bytes');
        sources.push(file.text);
        imports = file.imports;
      }
      for (const literal of imports) addPackage(importDirectory(paths[at], literal, roots));
    }
    const collections = [...roots.entries()].filter(([name]) => name !== 'project');
    const context = { project: roots.get('project'), collection_names: collections.map(([name]) => name),
      collection_paths: collections.map(([, root]) => root), paths: paths.map((item) => '/repo/' + item), sources };
    const bytes = encoder.encode(JSON.stringify(context));
    if (bytes.length > this.maxBytes) throw new ContextUnavailable('Serialized project context exceeds ' + this.maxBytes + ' UTF-8 bytes');
    const result = { bytes, files: paths.length };
    while ((this.contextBytes + bytes.length > this.maxBytes || this.contexts.size >= 4) && this.contexts.size) {
      const oldest = this.contexts.keys().next().value;
      this.contextBytes -= this.contexts.get(oldest).bytes.length;
      this.contexts.delete(oldest);
    }
    this.contexts.set(key, result);
    this.contextBytes += bytes.length;
    return result;
  }
}
