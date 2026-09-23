import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const files = new Map([['/content/scenes/Arena3D.json', 'packaged reference']]);
const requests = [];
const errors = [];
const assets = {
  'project.json': JSON.stringify({ version: 1, name: 'Neon Swarm', content: 'content', start_scene: 'scenes/Sandbox.json' }),
  'content/scenes/Sandbox.json': '{"name":"Sandbox"}',
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
assert.equal(files.get('/workspace/content/scenes/Sandbox.json'), assets['content/scenes/Sandbox.json']);
assert.equal(files.get('/content/scenes/Arena3D.json'), 'packaged reference');
assert.equal(Module.chibiSaveAsset('/workspace/content/scenes/Sandbox.json', '{"name":"Edited"}'), 0);
assert.deepEqual(JSON.parse(requests.at(-1).body), { path: 'content/scenes/Sandbox.json', text: '{"name":"Edited"}' });
const before = requests.length;
assert.equal(Module.chibiSaveAsset('/content/scenes/Arena3D.json', '{}'), -1);
assert.equal(requests.length, before);
assert.equal(errors.length, 1);
console.log('PASS: browser opens and saves the selected project without mixing packaged content');
