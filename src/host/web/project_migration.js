// Fixed project migration backend. Owns workers and fresh result installations;
// application approval, project switching and status presentation stay authored.
(() => {
  const jobs = new Map();
  const encoder = new TextEncoder();
  const hash = async bytes => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)), b=>b.toString(16).padStart(2,'0')).join('');
  const receipt = async files => {
    const rows = [];
    for (const [path, bytes] of files) rows.push({path,bytes:bytes.byteLength,sha256:await hash(bytes)});
    return hash(encoder.encode(JSON.stringify(rows)));
  };
  const discard = job => {
    job.cancelled = true;
    clearTimeout(job.timer);
    job.worker?.terminate();
    job.worker = null;
    if (job.root) { Module.browserWorkspaceFiles.remove(job.root); job.root = ''; }
  };
  Module.browserProjectMigration = Object.freeze({
    start(token, source, complete) {
      if (jobs.size >= 8 || jobs.has(token) || typeof Worker !== 'function') return false;
      const job = {cancelled:false,worker:null,root:'',timer:null};
      jobs.set(token,job);
      let files;
      try {
        // Synchronous freeze before later authored handlers can change source.
        files = Module.browserWorkspaceFiles.captureBytes(source,[]);
        if (files.length > 4096) throw Error('Migration input exceeds4096 files');
      } catch(error) { jobs.delete(token); complete(false,'',String(error.message||error)); return true; }
      const fail = error => {
        if (job.cancelled || jobs.get(token)!==job) return;
        discard(job); jobs.delete(token);
        complete(false,'',String(error.message||error).slice(0,4096));
      };
      Promise.resolve().then(async()=>{
        const sourceHash = await receipt(files);
        if (job.cancelled) return;
        const url = new URL(Module.browserCompilerWorkerURL || 'compiler/compiler_worker.js',location.href);
        if (url.origin !== location.origin) throw Error('Migration worker must be installed on this origin');
        const worker = new Worker(url); job.worker = worker;
        job.timer = setTimeout(()=>fail('Migration exceeded60-second deadline'),60000);
        worker.onerror = event=>fail(event.message||'Migration worker failed');
        worker.onmessage = async event=>{
          if (job.cancelled || jobs.get(token)!==job) return;
          const result=event.data;
          try {
            if (!result || result.kind!=='migrated') throw Error('Invalid migration worker reply');
            if (!result.ok) {
              const reason=String(result.error||'Migration failed').slice(0,4096);
              const log=String(result.log||'').slice(-16000);
              discard(job);jobs.delete(token);
              complete(false,'',reason,log);return;
            }
            if (!Array.isArray(result.files) || result.files.length>4096 || typeof result.log!=='string' || encoder.encode(result.log).length>32768) throw Error('Migration result exceeds bounded receipt/files budget');
            const output=result.files.map(file=>{
              if (!file || typeof file.path!=='string' || !(file.bytes instanceof ArrayBuffer)) throw Error('Invalid migrated file');
              return [file.path,new Uint8Array(file.bytes)];
            });
            // installBytes validates sorted unique paths, regular byte arrays,
            // per-file/aggregate budgets before creating a fresh owned root.
            const root=Module.browserWorkspaceFiles.installBytes(output);
            job.root=root;
            const resultHash=await receipt(output);
            if (job.cancelled || jobs.get(token)!==job) { if(job.root)discard(job); return; }
            clearTimeout(job.timer); worker.terminate();job.worker=null;
            const proof=JSON.stringify({version:1,source_sha256:sourceHash,result_sha256:resultHash,files:output.length});
            // C callback returns false for a retired token. Never leak its root.
            if (!complete(true,root,proof,result.log)) { discard(job); jobs.delete(token); }
          } catch(error) { fail(error); }
        };
        const transferred=files.map(([path,data])=>({path,bytes:data.slice().buffer}));
        worker.postMessage({kind:'migrate',files:transferred},transferred.map(f=>f.bytes));
      }).catch(fail);
      return true;
    },
    cancel(token) { const job=jobs.get(token);if(job)discard(job);jobs.delete(token); },
    detach(token) { const job=jobs.get(token);if(job)job.root='';jobs.delete(token); },
  });
})();
