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
