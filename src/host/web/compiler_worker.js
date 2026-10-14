// The SDK installs its worker-target compiler factory beside this adapter.
// No UI or editor policy runs in this worker.
importScripts('chibi-compile-worker.js');
let used = false;
self.onmessage = async event => {
  if (used) return;
  used = true;
  const input = event.data;
  let log = '';
  const print = value => { log = (log + String(value) + '\n').slice(-16000); };
  try {
    if (!input || !['compile','migrate'].includes(input.kind) || !Array.isArray(input.files) || input.files.length > 4096) {
      throw Error('Invalid frozen compiler inputs');
    }
    const compiler = await createChibiCompiler({noInitialRun: true, print, printErr: print,
      locateFile: path => new URL(path, self.location.href).href});
    let total = 0, previous = null;
    const compare = (left, right) => {
      const a = new TextEncoder().encode(left), b = new TextEncoder().encode(right);
      for (let i = 0; i < Math.min(a.length, b.length); i++) if (a[i] !== b[i]) return a[i] - b[i];
      return a.length - b.length;
    };
    for (const file of input.files) {
      if (!file || typeof file.path !== 'string' || file.path.length > 1024 ||
          file.path.includes('\\') || file.path.includes('\0') ||
          file.path.split('/').length > 17 ||
          file.path.split('/').some(part => !part || part === '.' || part === '..') ||
          !(file.bytes instanceof ArrayBuffer) || file.bytes.byteLength > 16777216 ||
          (previous !== null && compare(previous, file.path) >= 0)) {
        throw Error('Invalid frozen compiler file');
      }
      total += file.bytes.byteLength;
      if (total > 67108864) throw Error('Frozen compiler input exceeds 64 MiB');
      const path = '/workspace/' + file.path;
      compiler.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      compiler.FS.writeFile(path, new Uint8Array(file.bytes), {flags: 'wx'});
      previous = file.path;
    }
    if (!compiler.FS.analyzePath('/workspace/project.kdl').exists) throw Error('Frozen project has no project.kdl');
    if (input.kind === 'migrate') {
      compiler.FS.mkdirTree('/output');
      let status = 0;
      try { status = compiler.callMain(['--project-root','/workspace','--migrate-copy','/output/upgraded']) || 0; }
      catch(error) { if (error?.name === 'ExitStatus') status = error.status; else throw error; }
      if (status !== 0) throw Error('Project migration exited with status ' + status);
      const files = []; let bytes = 0, entries = 0;
      const visit = (path, relative, depth) => {
        if (depth > 17) throw Error('Migrated directory depth exceeds limit');
        for (const name of compiler.FS.readdir(path).filter(n=>n!=='.'&&n!=='..').sort(compare)) {
          if (++entries > 4100) throw Error('Migrated entry count exceeds limit');
          const child = path+'/'+name, key = relative ? relative+'/'+name : name;
          const info = compiler.FS.lstat(child);
          if (compiler.FS.isDir(info.mode)) { visit(child,key,depth+1); continue; }
          if (!compiler.FS.isFile(info.mode) || info.size > 16777216) throw Error('Invalid migrated output file');
          const data = compiler.FS.readFile(child).slice().buffer;
          bytes += data.byteLength;
          if (bytes > 67108864) throw Error('Migrated output exceeds64MiB');
          files.push({path:key,bytes:data});
        }
      };
      visit('/output/upgraded','',0); files.sort((a,b)=>compare(a.path,b.path));
      self.postMessage({kind:'migrated',ok:true,files,log},files.map(f=>f.bytes));
      return;
    }
    const artifact = '/output/game.chibip';
    const args = ['--project-root', '/workspace', '--output', artifact];
    if (input.authoring) args.push('--authoring');
    if (input.scene) {
      if (typeof input.scene !== 'string' || input.scene.startsWith('/') || input.scene.split('/').some(part => !part || part === '.' || part === '..')) {
        throw Error('Compiler scene must be workspace-relative');
      }
      args.push('--scene', '/workspace/' + input.scene);
    }
    let status = 0;
    try { status = compiler.callMain(args) || 0; }
    catch (error) { if (error?.name === 'ExitStatus') status = error.status; else throw error; }
    if (status !== 0) throw Error('Target compiler exited with status ' + status);
    const bytes = compiler.FS.readFile(artifact);
    const manifest = compiler.FS.readFile(artifact + '.resources.json', {encoding: 'utf8'});
    if (bytes.length > 67108864 || new TextEncoder().encode(manifest).length > 16777216) {
      throw Error('Compiler output exceeds budget');
    }
    const buffer = bytes.slice().buffer;
    self.postMessage({kind: 'compiled', ok: true, artifact: buffer, manifest, log}, [buffer]);
  } catch (error) {
    self.postMessage({kind: input?.kind === 'migrate' ? 'migrated' : 'compiled', ok: false, error: String(error.message || error).slice(0, 4096), log});
  }
};
