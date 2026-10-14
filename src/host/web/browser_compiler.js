// One isolated target compiler worker per candidate. The main browser runtime
// keeps the last committed world while its next frozen candidate is compiled.
(() => {
  const jobs = new Map();
  const complete = (instance, state, root, artifact, log, error) => {
    const pointers = [root, artifact, log, error].map(value => Module['stringToNewUTF8'](value || ''));
    try { return !!Module['_browser_compiler_complete'](instance, state, ...pointers); }
    finally { for (const pointer of pointers) Module['_free'](pointer); }
  };
  const remove = root => { if (root) Module['browserWorkspaceFiles'].remove(root); };
  const dispose = job => {
    job.cancelled = true;
    job.worker?.terminate();
    clearTimeout(job.timer);
    if (job.root) { remove(job.root); job.root = ''; }
  };
  const available = () => typeof Worker === 'function' && !!Module['browserWorkspaceFiles'];
  Module['browserCompiler'] = {
    available,
    start: (instance, root, overlaysJson, scene, authoring) => {
      if (!available()) return false;
      const prior = jobs.get(instance);
      if (prior) { dispose(prior); jobs.delete(instance); }
      const job = {root: '', worker: null, cancelled: false, timer: null};
      jobs.set(instance, job);
      let files;
      try {
        // Freeze before returning to authored handlers; later same-frame edits
        // must not change this candidate's disk bytes or captured overlays.
        files = Module['browserWorkspaceFiles'].captureBytes(root, JSON.parse(overlaysJson));
        job.root = Module['browserWorkspaceFiles'].installBytes(files);
      } catch (error) {
        dispose(job);
        jobs.delete(instance);
        queueMicrotask(() => complete(instance, 3, '', '', '', String(error.message || error).slice(0, 4096)));
        return true;
      }
      Promise.resolve().then(() => {
        if (job.cancelled) return;
        const workerUrl = new URL(Module['browserCompilerWorkerURL'] || 'compiler/compiler_worker.js', location.href);
        if (workerUrl.origin !== location.origin) throw Error('Compiler worker must be installed on this origin');
        const worker = new Worker(workerUrl);
        job.worker = worker;
        const fail = reason => {
          if (job.cancelled || jobs.get(instance) !== job) return;
          dispose(job);
          jobs.delete(instance);
          complete(instance, 3, '', '', '', String(reason).slice(0, 4096));
        };
        worker.onerror = event => fail(event.message || 'Compiler worker failed');
        job.timer = setTimeout(() => fail('Browser compilation exceeded its 60-second deadline'), 60000);
        worker.onmessage = event => {
          if (job.cancelled || jobs.get(instance) !== job) return;
          const result = event.data;
          if (!result || result.kind !== 'compiled') { fail('Invalid compiler worker reply'); return; }
          clearTimeout(job.timer);
          worker.terminate();
          job.worker = null;
          if (!result.ok) {
            const log = String(result.log || '').slice(-16000);
            dispose(job);
            jobs.delete(instance);
            complete(instance, 3, '', '', log, String(result.error || 'Compilation failed').slice(0, 4096));
            return;
          }
          try {
            if (!(result.artifact instanceof ArrayBuffer) || result.artifact.byteLength > 67108864 ||
                typeof result.manifest !== 'string' || new TextEncoder().encode(result.manifest).length > 16777216) {
              throw Error('Compiler output exceeds the artifact/manifest limit');
            }
            const artifact = job.root + '/.chibi/worker/game.chibip';
            FS.mkdirTree(job.root + '/.chibi/worker');
            FS.writeFile(artifact, new Uint8Array(result.artifact), {flags: 'wx'});
            FS.writeFile(artifact + '.resources.json', result.manifest, {flags: 'wx'});
            if (!complete(instance, 2, job.root, artifact, String(result.log || '').slice(-16000), '')) {
              dispose(job);
              jobs.delete(instance);
            }
          } catch (error) { fail(error.message); }
        };
        // Transfer independent copies; the frozen host FS remains available for
        // external resources after candidate adoption.
        const transferred = files.map(([path, data]) => ({path, bytes: data.slice().buffer}));
        worker.postMessage({kind: 'compile', files: transferred, scene, authoring: !!authoring}, transferred.map(file => file.bytes));
      }).catch(error => {
        if (job.cancelled || jobs.get(instance) !== job) return;
        dispose(job);
        jobs.delete(instance);
        complete(instance, 3, '', '', '', String(error.message || error).slice(0, 4096));
      });
      return true;
    },
    cancel: instance => {
      const job = jobs.get(instance);
      if (!job) return;
      dispose(job);
      jobs.delete(instance);
      complete(instance, 4, '', '', '', 'Browser compilation cancelled');
    },
    destroy: instance => {
      const job = jobs.get(instance);
      if (job) dispose(job);
      jobs.delete(instance);
    },
    detach: instance => {
      const job = jobs.get(instance);
      if (job) job.root = '';
    },
    remove,
  };
})();
