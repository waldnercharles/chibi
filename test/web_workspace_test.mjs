import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const files = new Map([['/content/scenes/Arena3D.yaml', 'packaged reference']]);
const requests = [];
const errors = [];
const assets = {
  'project.json': JSON.stringify({ version: 1, name: 'Neon Swarm', content: 'content', start_scene: 'scenes/Sandbox.yaml' }),
  'content/scenes/Sandbox.yaml': 'name: Sandbox\n',
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
  .replace('__CHIBI_SAVE_TOKEN__', '"test-token"');
vm.runInNewContext(source, { Module, XMLHttpRequest: Request, URLSearchParams, location: { search: '' } });
Module.onRuntimeInitialized();
assert.equal(Module.arguments.join(' '), '--project-root /workspace/');
assert.equal(files.get('/workspace/project.json'), assets['project.json']);
assert.equal(files.get('/workspace/content/scenes/Sandbox.yaml'), assets['content/scenes/Sandbox.yaml']);
assert.equal(files.get('/content/scenes/Arena3D.yaml'), 'packaged reference');
assert.equal(Module.chibiSaveAsset('/workspace/content/scenes/Sandbox.yaml', 'name: Edited\n'), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path: 'content/scenes/Sandbox.yaml', text: 'name: Edited\n' });
const before = requests.length;
assert.equal(Module.chibiSaveAsset('/content/scenes/Arena3D.yaml', '{}'), -1);
assert.equal(requests.length, before);
assert.equal(errors.length, 1);
const launchSource = readFileSync(new URL('../tools/web_launch.js', import.meta.url), 'utf8');
for (const search of ['', '?scene=scenes/Sandbox.json']) {
  const location = { search };
  const local = {};
  vm.runInNewContext(source, { Module: local, URLSearchParams, location });
  vm.runInNewContext(launchSource, { Module: local, URLSearchParams, window: { location } });
  assert.equal(local.arguments[0], '--project-root');
  assert.equal(local.arguments[1], '/workspace/');
  if (search) assert.equal(local.arguments[local.arguments.indexOf('--scene') + 1], 'scenes/Sandbox.json');

  const packaged = { arguments: ['--frames', '3'] };
  vm.runInNewContext(launchSource, { Module: packaged, URLSearchParams, window: { location } });
  assert.equal(packaged.arguments[0], '--frames');
  assert.equal(packaged.arguments[1], '3');
}
console.log('PASS: browser opens and saves the selected project without mixing packaged content');
console.log('PASS: local and packaged launches preserve launch arguments');
