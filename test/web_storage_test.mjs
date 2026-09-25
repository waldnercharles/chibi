import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const storage = readFileSync(new URL('../tools/web_storage.js', import.meta.url), 'utf8');
const launchSource = readFileSync(new URL('../tools/web_launch.js', import.meta.url), 'utf8');
const DIRECTORY = 1;
const manifest = JSON.stringify({ version: 1, name: 'Neon Swarm', content: 'content', start_scene: 'scenes/Sandbox.yaml' });

const makeFS = entries => {
  const nodes = new Map([['/', DIRECTORY]]);
  const fs = {
    nodes, mounts: [], syncs: [],
    filesystems: { IDBFS: 'IDBFS' },
    mkdirTree(path) {
      let at = '';
      for (const part of path.split('/').filter(Boolean)) nodes.set(at += '/' + part, DIRECTORY);
    },
    writeFile(path, data) {
      fs.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      nodes.set(path, data);
    },
    readFile(path) { return nodes.get(path); },
    stat(path) { return { mode: nodes.get(path) === DIRECTORY ? DIRECTORY : 0 }; },
    isDir(mode) { return mode === DIRECTORY; },
    readdir(path) {
      const names = [...nodes.keys()].filter(p => p.startsWith(path + '/') && !p.slice(path.length + 1).includes('/'));
      return ['.', '..', ...names.map(p => p.slice(path.length + 1))];
    },
    analyzePath(path) { return { exists: nodes.has(path) }; },
    mount(type, options, path) { fs.mounts.push({ type, path }); },
    syncfs(populate, callback) { fs.syncs.push({ populate, callback }); },
  };
  for (const [path, text] of Object.entries(entries)) fs.writeFile(path, text);
  return fs;
};

const launch = (search, fs, Module = { FS: fs }) => {
  const timers = [];
  const dependencies = [];
  const errors = [];
  Module.showError = message => errors.push(message);
  const context = {
    Module, URLSearchParams, window: { location: { search } },
    setTimeout: run => timers.push(run),
    clearTimeout: () => timers.length = 0,
    addRunDependency: id => dependencies.push(id),
    removeRunDependency: id => dependencies.splice(dependencies.indexOf(id), 1),
  };
  vm.runInNewContext(storage, context);
  vm.runInNewContext(launchSource, context);
  const flush = () => timers.splice(0).forEach(run => run());
  return { Module, timers, dependencies, errors, flush };
};

// With the local server bridge installed, the served workspace stays in charge.
{
  const bridge = { chibiSaveAsset() { return 0; }, arguments: ['--project-root', '/workspace/'] };
  launch('?project-root=projects/arena2d', makeFS({}), bridge);
  assert.deepEqual(bridge.arguments, ['--project-root', '/workspace/']);
  assert.equal(bridge.preRun, undefined);
}

// A first visit seeds IndexedDB from the bundled project.
const fs = makeFS({
  '/projects/arena2d/project.json': manifest,
  '/projects/arena2d/content/scenes/Sandbox.yaml': 'name: Sandbox\n',
  '/content/scenes/Arena3D.yaml': 'packaged reference',
});
const first = launch('?project-root=projects/arena2d&scene=scenes/Sandbox.yaml', fs);
assert.deepEqual([...first.Module.arguments], ['--project-root', '/workspace/projects/arena2d/', '--scene', 'scenes/Sandbox.yaml']);
first.Module.preRun.forEach(run => run());
assert.deepEqual(fs.mounts, [{ type: 'IDBFS', path: '/workspace/projects/arena2d' }]);
assert.deepEqual(first.dependencies, ['chibi-storage']);
assert.equal(fs.syncs[0].populate, true);
fs.syncs.shift().callback(null);
assert.deepEqual(first.dependencies, []);
first.Module.onRuntimeInitialized();
assert.equal(fs.nodes.get('/workspace/projects/arena2d/project.json'), manifest);
assert.equal(fs.nodes.get('/workspace/projects/arena2d/content/scenes/Sandbox.yaml'), 'name: Sandbox\n');
assert.equal(fs.nodes.has('/workspace/projects/arena2d/content/scenes/Arena3D.yaml'), false);
first.flush();
assert.equal(fs.syncs.shift().populate, false);

// Saves flush once after a burst, and failures are reported.
assert.equal(first.Module.chibiSaveAsset('/workspace/projects/arena2d/content/scenes/Sandbox.yaml', 'a'), 0);
assert.equal(first.Module.chibiSaveAsset('/workspace/projects/arena2d/content/scenes/Sandbox.yaml', 'b'), 0);
assert.equal(first.timers.length, 1);
first.flush();
assert.equal(fs.syncs.length, 1);
fs.syncs.shift().callback(new Error('quota exceeded'));
assert.deepEqual(first.errors, ['Browser storage failed: quota exceeded']);

// A later visit keeps the persisted edits instead of reseeding.
fs.nodes.set('/workspace/projects/arena2d/content/scenes/Sandbox.yaml', 'name: Edited\n');
const second = launch('?project-root=projects/arena2d', fs);
second.Module.onRuntimeInitialized();
assert.equal(fs.nodes.get('/workspace/projects/arena2d/content/scenes/Sandbox.yaml'), 'name: Edited\n');
assert.equal(second.timers.length, 0);

// Without a project-root parameter, the root bundled project persists under /workspace.
const root = makeFS({ '/project.json': manifest, '/content/scenes/Sandbox.yaml': 'name: Root\n' });
const packaged = launch('', root);
assert.deepEqual([...packaged.Module.arguments], ['--project-root', '/workspace/']);
packaged.Module.preRun.forEach(run => run());
assert.equal(root.mounts[0].path, '/workspace');
packaged.Module.onRuntimeInitialized();
assert.equal(root.nodes.get('/workspace/content/scenes/Sandbox.yaml'), 'name: Root\n');
console.log('PASS: browser storage seeds once, persists saves, and yields to the server bridge');
