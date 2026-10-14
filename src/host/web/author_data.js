// A deployment supplies explicit virtual paths in author.json. Application
// resources and the editable workspace are separate trees under /data.
Module['arguments'] = Module['arguments'] || [];
Module['preRun'] = [].concat(Module['preRun'] || [], () => {
  const fail = error => {
    const message = 'Could not load the authored application: ' + error.message;
    if (Module['chibiFail']) Module['chibiFail'](message);
    else console.error(message);
  };
  const bytes = async url => {
    const response = await fetch(url);
    if (!response.ok) throw new Error(url + ': ' + response.status);
    return new Uint8Array(await response.arrayBuffer());
  };
  const relative = path => typeof path === 'string' && path.length > 0 &&
    path.length <= 4096 && !path.includes('\\') && !path.includes('\0') &&
    path.split('/').every(part => part && part !== '.' && part !== '..');
  const mounted = path => typeof path === 'string' && path.startsWith('/data/') &&
    relative(path.slice('/data/'.length));
  const load = async () => {
    const config = JSON.parse(new TextDecoder().decode(await bytes('author.json')));
    const files = JSON.parse(new TextDecoder().decode(await bytes('files.json')));
    const keys = ['artifact', 'resource_root', 'sdk_resource_root'];
    if (keys.some(key => !mounted(config[key])) || (config.workspace_root != null && !mounted(config.workspace_root))) throw new Error('author.json requires explicit /data paths');
    if (!Array.isArray(files) || files.length > 65536 ||
        files.some(path => !relative(path)) || new Set(files).size !== files.length) {
      throw new Error('Invalid deployment file inventory');
    }
    // Sequential loading bounds concurrent fetch/memory overhead and preserves
    // useful failure paths for large editable workspaces.
    for (const path of files) {
      const target = '/data/' + path;
      const content = await bytes('data/' + path.split('/').map(encodeURIComponent).join('/'));
      FS.mkdirTree(target.slice(0, target.lastIndexOf('/')));
      FS.writeFile(target, content);
    }
    Module['browserMessageConfig'] = config.channels || {};
    Module['browserCompilerWorkerURL'] = config.compiler_worker || 'compiler/compiler_worker.js';
    const args = [config.artifact, '--resource-root', config.resource_root, '--sdk-resource-root', config.sdk_resource_root];
    if (config.workspace_root != null) args.push('--workspace-root', config.workspace_root);
    Module['arguments'].unshift(...args);
  };
  addRunDependency('chibi-author-data');
  load().then(() => removeRunDependency('chibi-author-data'), fail);
});
