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
const Module = {
  FS: { mkdirTree() {}, writeFile(path, text) { files.set(path, text); } },
  showError(message) { errors.push(message); },
};
class Request {
  open(method, path) { this.method = method; this.path = path; }
  setRequestHeader() {}
  send(body) {
    requests.push({ method: this.method, path: this.path, body });
    this.status = 200;
    this.responseText = JSON.stringify(assets);
  }
}
const source = readFileSync(new URL('../tools/web_workspace.js', import.meta.url), 'utf8')
  .replace('__CHIBI_SAVE_TOKEN__', '"test-token"')
  .replace('__CHIBI_PROJECT__', '"/projects/arena2d"');
vm.runInNewContext(source, { Module, XMLHttpRequest: Request, URLSearchParams, location: { search: '' } });
Module.onRuntimeInitialized();
assert.equal(Module.arguments.join(' '), '--project-root /projects/arena2d');
assert.equal(files.get('/projects/arena2d/project.yaml'), assets['projects/arena2d/project.yaml']);
assert.equal(files.get('/projects/arena2d/content/scenes/Sandbox.yaml'), assets['projects/arena2d/content/scenes/Sandbox.yaml']);
assert.equal(files.get('/content/scenes/Arena3D.yaml'), 'packaged reference');
const path = '/projects/arena2d/content/scenes/Sandbox.yaml';
assert.equal(Module.chibiSaveAsset(path, 'name: Edited\n'), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path, text: 'name: Edited\n' });
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
console.log('PASS: browser mirrors the workspace projects, opens the served one and routes saves to the server');
console.log('PASS: local and packaged launches preserve launch arguments');
