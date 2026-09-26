import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const files = new Map([['/content/scenes/Arena3D.yaml', 'packaged reference']]);
const requests = [];
const errors = [];
const assets = {
  'projects/arena2d/project.yaml': 'version: 1\nname: Neon Swarm\ncontent: content\nstart_scene: scenes/Sandbox.yaml\n',
  'projects/arena2d/content/scenes/Sandbox.yaml': 'name: Sandbox\n',
};
const writes = [];
const Module = {
  FS: {
    mkdirTree() {},
    analyzePath(path) { return { exists: files.has(path) }; },
    readFile(path) { return files.get(path); },
    writeFile(path, text) { writes.push(path); files.set(path, text); },
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
    this.responseText = JSON.stringify(assets);
  }
}
// The server answers the first wait with an edit, an unchanged file and a removal.
const followed = [];
const fetch = async url => {
  followed.push(url);
  if (followed.length > 1) return new Promise(() => {});
  return {
    ok: true,
    headers: { get: name => (name === 'X-Chibi-Version' ? '5' : null) },
    json: async () => ({
      'projects/arena2d/content/scenes/Sandbox.yaml': 'name: Edited on disk\n',
      'projects/arena2d/project.yaml': assets['projects/arena2d/project.yaml'],
      'content/scenes/Arena3D.yaml': null,
    }),
  };
};
const source = readFileSync(new URL('../tools/web_workspace.js', import.meta.url), 'utf8')
  .replace('__CHIBI_SAVE_TOKEN__', '"test-token"')
  .replace('__CHIBI_PROJECT__', '"/projects/arena2d"');
vm.runInNewContext(source, { Module, XMLHttpRequest: Request, URLSearchParams, location: { search: '' }, fetch, setTimeout });
Module.onRuntimeInitialized();
assert.equal(writes.length, 2);
assert.equal(Module.arguments.join(' '), '--project-root /projects/arena2d');
assert.equal(files.get('/projects/arena2d/project.yaml'), assets['projects/arena2d/project.yaml']);
assert.equal(files.get('/projects/arena2d/content/scenes/Sandbox.yaml'), assets['projects/arena2d/content/scenes/Sandbox.yaml']);
assert.equal(files.get('/content/scenes/Arena3D.yaml'), 'packaged reference');
for (let turn = 0; turn < 10; turn++) await new Promise(setImmediate);
assert.deepEqual(followed, ['/__chibi/changes?since=4', '/__chibi/changes?since=5']);
assert.equal(files.get('/projects/arena2d/content/scenes/Sandbox.yaml'), 'name: Edited on disk\n');
assert.equal(writes.length, 3, 'an unchanged file is not rewritten');
assert(!files.has('/content/scenes/Arena3D.yaml'));
const path = '/projects/arena2d/content/scenes/Sandbox.yaml';
assert.equal(Module.chibiSaveAsset(path, 'name: Edited\n'), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path, text: 'name: Edited\n' });
assert.equal(Module.chibiDeleteAsset(path), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path, text: null });
Request.prototype.send = function () { this.status = 400; this.responseText = 'Outside project content'; };
assert.equal(Module.chibiSaveAsset('/outside.yaml', '{}'), -1);
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
