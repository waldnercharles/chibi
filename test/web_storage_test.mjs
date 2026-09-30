import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { deflateRawSync } from 'node:zlib';

const storage = readFileSync(new URL('../tools/web_storage.js', import.meta.url), 'utf8');
const collabStorage = readFileSync(new URL('../tools/collab/storage.js', import.meta.url), 'utf8');
const launchSource = readFileSync(new URL('../tools/web_launch.js', import.meta.url), 'utf8');
const DIRECTORY = 1;
const manifest = 'version: 1\nname: Neon Swarm\ncontent: content\nstart_scene: scenes/Sandbox.yaml\n';
const text = data => typeof data === 'string' ? data : new TextDecoder().decode(data);

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
      nodes.set(path, typeof data === 'string' ? new TextEncoder().encode(data) : Uint8Array.from(data));
    },
    readFile(path, options) { return options?.encoding ? text(nodes.get(path)) : nodes.get(path); },
    stat(path) { return { mode: nodes.get(path) === DIRECTORY ? DIRECTORY : 0 }; },
    isDir(mode) { return mode === DIRECTORY; },
    readdir(path) {
      const names = [...nodes.keys()].filter(p => p.startsWith(path + '/') && !p.slice(path.length + 1).includes('/'));
      return ['.', '..', ...names.map(p => p.slice(path.length + 1))];
    },
    analyzePath(path) { return { exists: nodes.has(path) }; },
    unlink(path) { nodes.delete(path); },
    rmdir(path) { nodes.delete(path); },
    mount(type, options, path) { fs.mounts.push({ type, path }); },
    syncfs(populate, callback) { fs.syncs.push({ populate, callback }); },
    file(path) { return nodes.has(path) ? text(nodes.get(path)) : undefined; },
  };
  for (const [path, data] of Object.entries(entries)) fs.writeFile(path, data);
  return fs;
};

const launch = (search, fs, Module = { FS: fs }) => {
  const page = { timers: [], dependencies: [], errors: [], links: [], inputs: [], persisted: 0, location: { search } };
  Module.showError = message => page.errors.push(message);
  const context = {
    Module, URLSearchParams, Blob, Response, DecompressionStream, TextEncoder, TextDecoder, Uint8Array,
    window: { location: page.location },
    location: page.location,
    history: { replaceState: (state, title, url) => { page.location.search = url; } },
    navigator: { storage: { persist: () => ++page.persisted } },
    document: {
      createElement: tag => {
        const element = { click() {} };
        (tag === 'a' ? page.links : page.inputs).push(element);
        return element;
      },
    },
    URL: { createObjectURL: blob => blob, revokeObjectURL() {} },
    setTimeout: run => page.timers.push(run),
    clearTimeout: () => page.timers.length = 0,
    addRunDependency: id => page.dependencies.push(id),
    removeRunDependency: id => page.dependencies.splice(page.dependencies.indexOf(id), 1),
  };
  // The page's pre-js order: storage, then collaboration storage, then launch.
  vm.runInNewContext(storage, context);
  vm.runInNewContext(collabStorage, context);
  vm.runInNewContext(launchSource, context);
  page.Module = Module;
  page.flush = () => page.timers.splice(0).forEach(run => run());
  page.start = () => {
    Module.preRun.forEach(run => run());
    fs.syncs.shift().callback(null);
    Module.onRuntimeInitialized();
  };
  return page;
};
const settle = () => new Promise(resolve => setTimeout(resolve, 10));

// With the local server bridge installed, the served workspace stays in charge.
{
  const bridge = { chibiSaveAsset() { return 0; }, arguments: ['--project-root', '/workspace/'] };
  launch('?project-root=projects/arena2d', makeFS({}), bridge);
  assert.deepEqual(bridge.arguments, ['--project-root', '/workspace/']);
  assert.equal(bridge.preRun, undefined);
  assert.equal(bridge.chibiExportProject, undefined);
}

// Opening a sample the first time copies its folder into a project of the user's own.
const fs = makeFS({
  '/projects/arena2d/project.yaml': manifest,
  '/projects/arena2d/content/scenes/Sandbox.yaml': 'name: Sandbox\n',
  '/projects/arena3d/content/scenes/Arena3D.yaml': 'packaged reference',
});
const first = launch('?project-root=projects/arena2d&scene=scenes/Sandbox.yaml', fs);
assert.deepEqual([...first.Module.arguments], ['--project-root', '', '--scene', 'scenes/Sandbox.yaml']);
first.Module.preRun.forEach(run => run());
assert.deepEqual(fs.mounts, [{ type: 'IDBFS', path: '/workspace' }]);
assert.deepEqual(first.dependencies, ['chibi-storage']);
assert.equal(fs.syncs[0].populate, true);
fs.syncs.shift().callback(null);
assert.deepEqual(first.dependencies, []);
first.Module.onRuntimeInitialized();
assert.equal(first.Module.arguments[1], '/workspace/arena2d/');
assert.equal(fs.file('/workspace/arena2d/project.yaml'), manifest);
assert.equal(fs.file('/workspace/arena2d/content/scenes/Sandbox.yaml'), 'name: Sandbox\n');
assert.equal(fs.file('/workspace/arena2d/content/scenes/Arena3D.yaml'), undefined);
assert.equal(fs.file('/workspace/.last'), 'arena2d');
first.flush();
assert.equal(fs.syncs.shift().populate, false);
assert.equal(first.persisted, 0);

// Studio lists the user's projects, then each sample not yet copied into one.
fs.writeFile('/projects/studio/project.yaml', 'version: 1\nname: Studio\ncontent: content\n');
assert.deepEqual(JSON.parse(first.Module.chibiProjects()), [
  { name: 'Neon Swarm', root: '/workspace/arena2d/' },
  { name: 'Studio', root: '/projects/studio/' },
]);

// Without a project to reopen, the page opens Neon Swarm.
const empty = makeFS({ '/projects/arena2d/project.yaml': manifest });
const byDefault = launch('', empty);
byDefault.start();
assert.equal(byDefault.Module.arguments[1], '/workspace/arena2d/');

// A collaboration room runs from an isolated memory tree and never selects or
// mutates the user's persistent project as its working directory.
const room = launch('?room=room-123&role=host', fs);
room.start();
const roomPath = '/collab/room-123/project';
assert.equal(room.Module.arguments[1], `${roomPath}/`);
assert.equal(fs.file(`${roomPath}/content/scenes/Sandbox.yaml`), 'name: Sandbox\n');
assert.equal(fs.file('/workspace/.last'), 'arena2d');
room.Module.chibiCollabStorage.installFiles([
  {path: 'project.yaml', data: new TextEncoder().encode(manifest)},
  {path: 'content/scenes/Shared.yaml', data: new TextEncoder().encode('name: Shared\n')},
]);
assert.equal(fs.file(`${roomPath}/content/scenes/Sandbox.yaml`), undefined);
assert.equal(fs.file(`${roomPath}/content/scenes/Shared.yaml`), 'name: Shared\n');
assert.equal(fs.file('/workspace/arena2d/content/scenes/Sandbox.yaml'), 'name: Sandbox\n');

// Saves flush once after a burst, ask to keep storage, and report failures.
assert.equal(first.Module.chibiSaveAsset('/workspace/arena2d/content/scenes/Sandbox.yaml', 'a'), 0);
assert.equal(first.Module.chibiSaveAsset('/workspace/arena2d/content/scenes/Sandbox.yaml', 'b'), 0);
assert.equal(first.persisted, 1);
assert.equal(first.timers.length, 1);
first.flush();
assert.equal(fs.syncs.length, 1);
fs.syncs.shift().callback(new Error('quota exceeded'));
await settle();
assert.deepEqual(first.errors, ['Browser storage failed: quota exceeded']);

// A later visit reopens the last project with its edits.
fs.writeFile('/workspace/arena2d/content/scenes/Sandbox.yaml', 'name: Edited\n');
const second = launch('', fs);
second.start();
assert.equal(second.Module.arguments[1], '/workspace/arena2d/');
assert.equal(fs.file('/workspace/arena2d/content/scenes/Sandbox.yaml'), 'name: Edited\n');
assert.equal(second.timers.length, 0);

// Export downloads the project as a zip; importing it creates a separate project and opens it.
second.Module.chibiExportProject();
const download = second.links[0];
assert.equal(download.download, 'arena2d.zip');
const exported = await download.href.arrayBuffer();
const archive = new TextDecoder('latin1').decode(exported);
assert.ok(archive.includes('arena2d/content/scenes/Sandbox.yaml'));
assert.ok(archive.includes('name: Edited\n'));
const importFile = async (page, bytes, name) => {
  page.Module.chibiImportProject();
  const input = page.inputs.at(-1);
  input.files = [{ name, arrayBuffer: async () => bytes }];
  let finished = false;
  const done = input.onchange().finally(() => { finished = true; });
  // Unzipping takes as long as it takes; answer the sync once it is asked for.
  while (!finished && !fs.syncs.length) await settle();
  fs.syncs.splice(0).forEach(sync => sync.callback(null));
  await done;
};
await importFile(second, exported, 'arena2d.zip');
assert.equal(second.location.search, '?project=arena2d-2');
assert.equal(fs.file('/workspace/arena2d-2/content/scenes/Sandbox.yaml'), 'name: Edited\n');
assert.equal(fs.file('/workspace/arena2d/content/scenes/Sandbox.yaml'), 'name: Edited\n');
assert.equal(fs.file('/workspace/.last'), 'arena2d-2');
assert.equal(second.persisted, 1);

// Archives from other tools use deflate and may carry a top folder and platform extras.
const deflated = entries => {
  const parts = [], directory = [];
  let offset = 0;
  for (const [name, data] of entries) {
    const path = Buffer.from(name), body = deflateRawSync(Buffer.from(data));
    const local = Buffer.alloc(30), central = Buffer.alloc(46);
    local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(8, 8); local.writeUInt32LE(body.length, 18);
    local.writeUInt32LE(data.length, 22); local.writeUInt16LE(path.length, 26);
    central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(8, 10); central.writeUInt32LE(body.length, 20);
    central.writeUInt32LE(data.length, 24); central.writeUInt16LE(path.length, 28); central.writeUInt32LE(offset, 42);
    parts.push(local, path, body);
    directory.push(central, path);
    offset += 30 + path.length + body.length;
  }
  const size = directory.reduce((total, part) => total + part.length, 0);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(size, 12); end.writeUInt32LE(offset, 16);
  const bytes = Buffer.concat([...parts, ...directory, end]);
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length);
};
const third = launch('?project=arena2d', fs);
third.start();
await importFile(third, deflated([
  ['My Game/project.yaml', 'version: 1\nname: My Game\n'],
  ['My Game/content/scenes/Level.yaml', 'name: Level\n'],
  ['My Game/notes.txt', 'part of the project folder'],
  ['__MACOSX/My Game/._project.yaml', 'resource fork'],
]), 'My Game.zip');
assert.equal(third.location.search, '?project=my-game');
assert.equal(fs.file('/workspace/my-game/content/scenes/Level.yaml'), 'name: Level\n');
assert.equal(fs.file('/workspace/my-game/notes.txt'), 'part of the project folder');
assert.equal([...fs.nodes.keys()].some(path => path.includes('MACOSX')), false);

// Archives that are not projects leave storage untouched.
const before = fs.nodes.size;
await importFile(third, deflated([['readme.txt', 'hello']]), 'readme.zip');
assert.deepEqual(third.errors, ['Could not import project: The archive has no project.yaml']);
assert.equal(fs.nodes.size, before);

// Once Chibi stops, the shell offers export of the open project and a new project.
const labels = page => page.Module.recovery.map(([label]) => label).join(', ');
assert.equal(labels(third), 'Export Project, New Project');
third.Module.recovery[1][1]();
assert.equal(third.location.search, '?new=');
const missing = launch('?project=missing&project-root=projects/arena2d', fs);
assert.throws(() => missing.start(), /Project not found: missing/);
assert.equal(labels(missing), 'New Project');
missing.Module.recovery[0][1]();
assert.equal(missing.location.search, '?new=&project-root=projects%2Farena2d');
const fresh = launch('?new=&project-root=projects/arena2d', fs);
fresh.start();
assert.equal(fresh.Module.arguments[1], '/workspace/arena2d-3/');
assert.equal(fresh.location.search, '?project=arena2d-3');
assert.equal(fs.file('/workspace/arena2d-3/content/scenes/Sandbox.yaml'), 'name: Sandbox\n');

// Without a project-root, the Arena3D sample opens.
const root = makeFS({ '/projects/arena3d/project.yaml': 'version: 1\nname: Arena3D\n', '/projects/arena3d/content/scenes/Arena3D.yaml': 'name: Root\n' });
const packaged = launch('', root);
packaged.start();
assert.equal(packaged.Module.arguments[1], '/workspace/arena3d/');
assert.equal(root.file('/workspace/arena3d/content/scenes/Arena3D.yaml'), 'name: Root\n');
console.log('PASS: browser projects persist, reopen, export, import and recover without the server bridge');
