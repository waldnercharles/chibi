import assert from 'node:assert/strict';
import { Buffer } from 'node:buffer';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const files = new Map([['/content/scenes/Arena3D.kdl', 'packaged reference']]);
const requests = [];
const errors = [];
const portrait = 'projects/arena2d/content/art/portrait.png';
const backdrop = 'projects/arena2d/content/art/backdrop.jpeg';
const badge = 'projects/arena2d/content/art/badge.png';
const assets = {
  'projects/arena2d/project.kdl': "version 1\nname \"Neon Swarm\"\ncontent \"content\"\nstart_scene \"scenes/Sandbox.kdl\"\n",
  'projects/arena2d/content/scenes/Sandbox.kdl': "name \"Sandbox\"\n",
  [portrait]: {encoding: 'base64', data: 'AP8BgA=='},
  [backdrop]: {encoding: 'base64', data: 'Cgs='},
  [badge]: {encoding: 'base64', data: 'Aw=='},
};
const writes = [];
const Module = {
  FS: {
    mkdirTree() {},
    analyzePath(path) { return { exists: files.has(path) }; },
    readFile(path, options) {
      const value = files.get(path);
      return options?.encoding === 'utf8' && value instanceof Uint8Array
        ? new TextDecoder().decode(value)
        : value;
    },
    writeFile(path, value) {
      writes.push(path);
      files.set(path, value instanceof Uint8Array ? Uint8Array.from(value) : value);
    },
    unlink(path) { files.delete(path); },
  },
  showError(message) { errors.push(message); },
};
class Request {
  open(method, path) { this.method = method; this.path = path; }
  setRequestHeader() {}
  getResponseHeader(name) { return name === 'X-Chibi-Version' ? '4' : null; }
  send(body) {
    requests.push({ method: this.method, path: this.path, body });
    this.status = 200;
    this.responseText = this.path === '/__chibi/project'
      ? JSON.stringify({
        root: '/projects/sample-project/',
        assets: {
          'projects/sample-project/project.kdl': "version 5\nname \"Sample Project\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n",
          'projects/sample-project/content/scenes/Start.kdl': "version 1\nname \"Start\"\n(array)entities\n",
        },
      })
      : JSON.stringify(assets);
  }
}
// The server answers the first wait with an edit, an unchanged file and a removal.
const followed = [];
const location = {search: ''};
const history = {replaceState(_state, _title, search) { location.search = search; }};
const fetch = async url => {
  followed.push(url);
  if (followed.length > 1) return new Promise(() => {});
  return {
    ok: true,
    headers: { get: name => (name === 'X-Chibi-Version' ? '5' : null) },
    json: async () => ({
      'projects/arena2d/content/scenes/Sandbox.kdl': "name \"Edited on disk\"\n",
      'projects/arena2d/project.kdl': assets['projects/arena2d/project.kdl'],
      'content/scenes/Arena3D.kdl': null,
      [portrait]: {encoding: 'base64', data: 'AQIDBAU='},
      [backdrop]: assets[backdrop],
      [badge]: null,
    }),
  };
};
const source = readFileSync(new URL('../tools/web_workspace.js', import.meta.url), 'utf8')
  .replace('__CHIBI_SAVE_TOKEN__', '"test-token"')
  .replace('__CHIBI_PROJECT__', '"/projects/arena2d"');
vm.runInNewContext(source, {
  Module, XMLHttpRequest: Request, URLSearchParams, location, history, fetch, setTimeout,
  atob: value => Buffer.from(value, 'base64').toString('binary'),
});
Module.onRuntimeInitialized();
assert.equal(writes.length, 5);
assert.equal(Module.arguments.join(' '), '--project-root /projects/arena2d');
assert.equal(files.get('/projects/arena2d/project.kdl'), assets['projects/arena2d/project.kdl']);
assert.equal(files.get('/projects/arena2d/content/scenes/Sandbox.kdl'), assets['projects/arena2d/content/scenes/Sandbox.kdl']);
assert.equal(files.get('/content/scenes/Arena3D.kdl'), 'packaged reference');
assert.deepEqual([...files.get('/' + portrait)], [0, 255, 1, 128]);
assert.deepEqual([...files.get('/' + backdrop)], [10, 11]);
assert.deepEqual([...files.get('/' + badge)], [3]);
for (let turn = 0; turn < 10; turn++) await new Promise(setImmediate);
assert.deepEqual(followed, ['/__chibi/changes?since=4', '/__chibi/changes?since=5']);
assert.equal(files.get('/projects/arena2d/content/scenes/Sandbox.kdl'), "name \"Edited on disk\"\n");
assert.deepEqual([...files.get('/' + portrait)], [1, 2, 3, 4, 5]);
assert.deepEqual([...files.get('/' + backdrop)], [10, 11], 'unchanged bytes stay mirrored');
assert(!files.has('/' + badge), 'a removed image leaves MEMFS');
assert.equal(writes.length, 7, 'unchanged text and image files are not rewritten');
assert(!files.has('/content/scenes/Arena3D.kdl'));
assert.deepEqual(JSON.parse(Module.chibiCreateProject('Sample Project', 5)), {root: '/projects/sample-project/'});
assert.deepEqual(JSON.parse(requests.at(-1).body), {name: 'Sample Project', version: 5});
assert.equal(files.get('/projects/sample-project/project.kdl'),
  "version 5\nname \"Sample Project\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n");
assert.equal(files.get('/projects/sample-project/content/scenes/Start.kdl'),
  "version 1\nname \"Start\"\n(array)entities\n");

const originalWriteFile = Module.FS.writeFile;
const simmers = {
  'projects/simmer/project.kdl': "version 5\nname \"Simmer\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n",
  'projects/simmer/content/scenes/Start.kdl': "version 1\nname \"Start\"\n(array)entities\n",
};
const projectRequestCount = requests.filter(request => request.path === '/__chibi/project').length;
const failedPath = '/projects/simmer/content/scenes/Start.kdl';
let failOnce = true;
const originalSend = Request.prototype.send;
Request.prototype.send = function (body) {
  const submitted = JSON.parse(body);
  if (this.path === '/__chibi/project' && submitted.name === 'Simmer') {
    requests.push({method: this.method, path: this.path, body});
    this.status = 200;
    this.responseText = JSON.stringify({root: '/projects/simmer/', assets: simmers});
    return;
  }
  originalSend.call(this, body);
};
Module.FS.writeFile = (path, value) => {
  if (path === failedPath && failOnce) {
    failOnce = false;
    throw new Error(`Cannot write ${path}`);
  }
  originalWriteFile(path, value);
};
const firstAttempt = JSON.parse(Module.chibiCreateProject('Simmer', 5));
assert.match(firstAttempt.error, /Cannot write/);
assert.equal(files.get('/projects/simmer/project.kdl'), simmers['projects/simmer/project.kdl']);
assert.equal(files.has(failedPath), false, 'a failed first sync leaves the start scene missing');
assert.deepEqual(JSON.parse(Module.chibiCreateProject('Simmer', 5)), {root: '/projects/simmer/'});
assert.equal(files.get(failedPath), simmers['projects/simmer/content/scenes/Start.kdl']);
assert.equal(requests.filter(request => request.path === '/__chibi/project').length,
  projectRequestCount + 1, 'retry mirrors the already-created project instead of creating a duplicate');
Module.FS.writeFile = originalWriteFile;
Request.prototype.send = originalSend;

location.search = '?project-root=projects/arena2d&scene=scenes/Sandbox.kdl';
Module.chibiRememberProject('/projects/sample-project/');
assert.equal(location.search, '?project-root=projects/sample-project');
const path = '/projects/arena2d/content/scenes/Sandbox.kdl';
Module.chibiCollabStorage = {root: '/projects/arena2d/', localRoot: '/projects/arena2d/'};
assert.equal(Module.chibiSaveAsset(path, "name \"Edited\"\n"), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path, text: "name \"Edited\"\n" });
assert.equal(Module.chibiDeleteAsset(path), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path, text: null });
Module.chibiCollabStorage.root = '/collab/test/project/';
const beforeRoomSave = requests.length;
assert.equal(Module.chibiSaveAsset('/collab/test/project/content/scenes/Start.kdl', "name \"Room\"\n"), 0);
assert.equal(Module.chibiDeleteAsset('/collab/test/project/content/scenes/Start.kdl'), 0);
assert.equal(requests.length, beforeRoomSave, 'room edits never reach the local project mirror');
assert.equal(Module.chibiSaveAsset(path, "name \"Local\"\n"), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path, text: "name \"Local\"\n" });
Request.prototype.send = function () { this.status = 400; this.responseText = 'Outside project content'; };
assert.equal(Module.chibiSaveAsset('/outside.kdl', '{}'), -1);
assert.deepEqual(errors, ['Outside project content']);
const launchSource = readFileSync(new URL('../tools/web_launch.js', import.meta.url), 'utf8');
for (const search of ['', '?scene=scenes/Sandbox.json']) {
  const location = { search };
  const local = {};
  vm.runInNewContext(source, { Module: local, URLSearchParams, location });
  vm.runInNewContext(launchSource, { Module: local, URLSearchParams, window: { location } });
  assert.equal(local.arguments[0], '--project-root');
  assert.equal(local.arguments[1], '/projects/arena2d');
  if (search) assert.equal(local.arguments[local.arguments.indexOf('--scene') + 1], 'scenes/Sandbox.json');

  const packaged = { arguments: ['--frames', '3'] };
  vm.runInNewContext(launchSource, { Module: packaged, URLSearchParams, window: { location } });
  assert.equal(packaged.arguments[0], '--frames');
  assert.equal(packaged.arguments[1], '3');
}
console.log('PASS: browser mirrors the workspace projects, opens the served one, routes saves to the server and follows edits on disk');
console.log('PASS: local and packaged launches preserve launch arguments');

// Each page load starts with chibi.data again, including assets removed since
// the build. The current server snapshot must replace only mirrored Studio files.
const studioRoot = '/projects/studio/';
const studioPanel = 'projects/studio/content/ui/Panel.rml';
const renamedPanel = 'projects/studio/content/ui/Renamed.rml';
const studioSnapshot = {
  'projects/studio/project.kdl': 'content "content"\n',
  [studioPanel]: '<div>checkout</div>',
};
const preload = new Map([
  ['/projects/studio/project.kdl', 'old manifest'],
  ['/' + studioPanel, '<div>built</div>'],
  [studioRoot + 'content/ui/Deleted.rml', 'deleted after build'],
  [studioRoot + 'content/ui/old/Theme.rcss', 'renamed after build'],
  [studioRoot + 'content/prototypes/Deleted.kdl', 'old prototype'],
  [studioRoot + 'content/art/Deleted.PNG', 'old image'],
  [studioRoot + 'content/font.woff2', 'non-mirrored resource'],
  ['/projects/studio-game/content/Keep.rml', 'unrelated game'],
  ['/projects/arena2d/content/Keep.rml', 'unrelated workspace asset'],
  ['/content/Keep.kdl', 'unrelated packaged asset'],
]);
function loadStudioSnapshot() {
  const memory = new Map(preload);
  const isDirectory = path => [...memory.keys()].some(name => name.startsWith(path + '/'));
  const fs = {
    mkdirTree() {},
    analyzePath: path => ({exists: memory.has(path) || isDirectory(path)}),
    readdir: path => ['.', '..', ...new Set([...memory.keys()]
      .filter(name => name.startsWith(path + '/'))
      .map(name => name.slice(path.length + 1).split('/')[0]))],
    stat: path => ({mode: isDirectory(path) ? 'directory' : 'file'}),
    isDir: mode => mode === 'directory',
    readFile: path => memory.get(path),
    writeFile(path, value) { memory.set(path, value); },
    unlink: path => memory.delete(path),
  };
  class StudioRequest {
    open(method, path) { this.path = path; }
    setRequestHeader() {}
    getResponseHeader() { return '0'; }
    send(body) {
      if (this.path === '/__chibi/save') {
        const {path, text} = JSON.parse(body);
        const name = path.replace(/^\/+/, '');
        if (text === null) delete studioSnapshot[name];
        else studioSnapshot[name] = text;
      }
      this.status = 200;
      this.responseText = JSON.stringify(studioSnapshot);
    }
  }
  const module = {FS: fs, showError: message => assert.fail(message)};
  let deliverChanges;
  vm.runInNewContext(source, {
    Module: module, XMLHttpRequest: StudioRequest, URLSearchParams,
    location: {search: ''}, setTimeout,
    fetch: () => new Promise(resolve => { deliverChanges = resolve; }),
  });
  module.onRuntimeInitialized();
  return {module, memory, async change(delta) {
    deliverChanges({ok: true, headers: {get: () => '1'}, json: async () => delta});
    for (let turn = 0; turn < 5; turn++) await new Promise(setImmediate);
  }};
}
function checkStudioMemory(memory) {
  const expected = new Map([...preload].filter(([path]) =>
    !path.startsWith(studioRoot) || path.endsWith('.woff2')));
  for (const [path, text] of Object.entries(studioSnapshot)) expected.set('/' + path, text);
  assert.deepEqual(memory, expected, 'reload must remove stale Studio assets and keep unrelated files');
}
let page = loadStudioSnapshot();
checkStudioMemory(page.memory);
assert.equal(page.memory.get('/' + studioPanel), '<div>checkout</div>');
page.module.FS.writeFile('/' + studioPanel, '<div>saved</div>');
assert.equal(page.module.chibiSaveAsset('/' + studioPanel, '<div>saved</div>'), 0);
page = loadStudioSnapshot();
checkStudioMemory(page.memory);
assert.equal(page.memory.get('/' + studioPanel), '<div>saved</div>');
// Rename follows the engine's save-new/delete-old sequence. Apply its watcher
// delta, then reload from the original packaged files, not the last MEMFS state.
assert.equal(page.module.chibiSaveAsset('/' + renamedPanel, '<div>renamed</div>'), 0);
assert.equal(page.module.chibiDeleteAsset('/' + studioPanel), 0);
await page.change({[renamedPanel]: '<div>renamed</div>', [studioPanel]: null});
checkStudioMemory(page.memory);
page = loadStudioSnapshot();
checkStudioMemory(page.memory);
assert(!page.memory.has('/' + studioPanel), 'reload must not resurrect the old name');
assert.equal(page.module.chibiDeleteAsset('/' + renamedPanel), 0);
await page.change({[renamedPanel]: null});
checkStudioMemory(page.memory);
page = loadStudioSnapshot();
checkStudioMemory(page.memory);
assert(!page.memory.has('/' + renamedPanel), 'deleted assets stay absent after reload');
console.log('PASS: authoritative Studio snapshot read/save/rename/delete/reload preserves unrelated MEMFS assets');
