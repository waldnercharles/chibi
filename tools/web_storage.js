// Without the local server bridge, the project persists in IndexedDB, seeded once from the bundle.
if (typeof window !== 'undefined' && !Module['chibiSaveAsset']) {
    const launch = new URLSearchParams(window.location.search);
    const bundled = `/${launch.get('project-root') || ''}/`.replace(/\/+/g, '/');
    // IDBFS names each database after its mount point, so every bundled project keeps its own.
    const mount = '/workspace' + bundled.slice(0, -1);
    Module['arguments'] = ['--project-root', mount + '/'];
    const report = error => {
        if (error) Module['showError'](`Browser storage failed: ${error.message || error}`);
    };
    let timer;
    const persist = () => {
        clearTimeout(timer);
        timer = setTimeout(() => Module['FS'].syncfs(false, report), 250);
    };
    const copy = (from, to) => {
        const FS = Module['FS'];
        if (!FS.isDir(FS.stat(from).mode)) {
            FS.writeFile(to, FS.readFile(from));
            return;
        }
        FS.mkdirTree(to);
        for (const name of FS.readdir(from)) {
            if (name !== '.' && name !== '..') copy(`${from}/${name}`, `${to}/${name}`);
        }
    };
    // Saves write the file right after this hook returns, so the flush sees them.
    Module['chibiSaveAsset'] = () => {
        persist();
        return 0;
    };
    (Module['preRun'] ||= []).push(() => {
        const FS = Module['FS'];
        FS.mkdirTree(mount);
        FS.mount(FS.filesystems.IDBFS, {}, mount);
        addRunDependency('chibi-storage');
        FS.syncfs(true, error => {
            report(error);
            removeRunDependency('chibi-storage');
        });
    });
    // Preloaded bundle files exist only once every run dependency has resolved.
    Module['onRuntimeInitialized'] = () => {
        const FS = Module['FS'];
        if (FS.analyzePath(mount + '/project.json').exists) return;
        const manifest = JSON.parse(FS.readFile(bundled + 'project.json', {encoding: 'utf8'}));
        copy(bundled + 'project.json', mount + '/project.json');
        copy(bundled + manifest.content, `${mount}/${manifest.content}`);
        persist();
    };
}
