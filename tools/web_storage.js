// Without the local server bridge, projects persist in IndexedDB, one folder each under /workspace.
if (typeof window !== 'undefined' && !Module['chibiSaveAsset']) {
    const launch = new URLSearchParams(window.location.search);
    const workspace = '/workspace';
    const last = workspace + '/.last';
    // The project is chosen once IndexedDB has loaded; main reads this same array.
    const args = Module['arguments'] = ['--project-root', ''];
    let project;
    const fs = () => Module['FS'];
    const report = error => {
        if (error) Module['showError'](`Browser storage failed: ${error.message || error}`);
    };
    const sync = () => new Promise((done, fail) => fs().syncfs(false, error => (error ? fail(error) : done())));
    let timer;
    const persist = () => {
        clearTimeout(timer);
        timer = setTimeout(() => sync().catch(report), 250);
    };
    // Browsers may evict site storage; ask once the user has made data of their own.
    let kept;
    const keep = () => {
        kept ||= navigator.storage?.persist?.();
    };
    const exists = path => fs().analyzePath(path).exists;
    const slug = name => String(name).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
    const found = id => /^[a-z0-9-]+$/.test(id) && exists(`${workspace}/${id}/project.json`);
    const unique = name => {
        const base = slug(name) || 'project';
        let id = base;
        for (let n = 2; exists(`${workspace}/${id}`); n++) id = `${base}-${n}`;
        return id;
    };
    const readManifest = root => JSON.parse(fs().readFile(root + 'project.json', {encoding: 'utf8'}));
    const walk = (path, visit, name = '') => {
        if (!fs().isDir(fs().stat(path).mode)) {
            visit(name, fs().readFile(path));
            return;
        }
        for (const child of fs().readdir(path)) {
            if (child !== '.' && child !== '..') walk(`${path}/${child}`, visit, name ? `${name}/${child}` : child);
        }
    };
    const write = (path, data) => {
        fs().mkdirTree(path.slice(0, path.lastIndexOf('/')));
        fs().writeFile(path, data);
    };
    const projectFiles = (root, visit) => {
        visit('project.json', fs().readFile(root + 'project.json'));
        const content = readManifest(root).content;
        walk(root + content, (name, data) => visit(`${content}/${name}`, data));
    };
    const bundled = `/${launch.get('project-root') || ''}/`.replace(/\/+/g, '/');
    const copySample = id => {
        projectFiles(bundled, (name, data) => write(`${workspace}/${id}/${name}`, data));
        persist();
        return id;
    };
    const lastOpened = () => (exists(last) ? fs().readFile(last, {encoding: 'utf8'}) : '');
    // ?project= opens a stored project and ?new copies the sample. Until samples open
    // read-only, launching one opens the user's own copy of it.
    const chooseProject = () => {
        const requested = launch.get('project');
        if (requested !== null && !found(requested)) throw new Error(`Project not found: ${requested}`);
        if (requested) return requested;
        if (launch.has('new')) {
            const id = copySample(unique(readManifest(bundled).name));
            history.replaceState(null, '', `?project=${id}`);
            return id;
        }
        const previous = lastOpened();
        if (!launch.has('project-root') && found(previous)) return previous;
        const id = slug(readManifest(bundled).name) || 'project';
        return found(id) ? id : copySample(id);
    };

    const crcTable = Array.from({length: 256}, (_, n) => {
        for (let k = 0; k < 8; k++) n = n & 1 ? 0xedb88320 ^ (n >>> 1) : n >>> 1;
        return n >>> 0;
    });
    const crc32 = bytes => {
        let crc = ~0;
        for (const byte of bytes) crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
        return ~crc >>> 0;
    };
    // An uncompressed zip: small, and every archiver opens it.
    const zip = files => {
        const now = new Date();
        const time = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
        const date = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
        const parts = [], directory = [];
        let offset = 0;
        const header = (size, fields) => {
            const view = new DataView(new ArrayBuffer(size));
            fields.forEach(([at, bytes, value]) => bytes === 2 ? view.setUint16(at, value, true) : view.setUint32(at, value, true));
            return new Uint8Array(view.buffer);
        };
        for (const [name, data] of files) {
            const path = new TextEncoder().encode(name);
            const crc = crc32(data);
            // UTF-8 names, stored, then time, date, CRC, both sizes and the name length.
            const shared = [[6, 2, 0x0800], [10, 2, time], [12, 2, date], [14, 4, crc], [18, 4, data.length], [22, 4, data.length], [26, 2, path.length]];
            const local = header(30, [[0, 4, 0x04034b50], [4, 2, 20], ...shared]);
            const central = header(46, [[0, 4, 0x02014b50], [4, 2, 20], [6, 2, 20], ...shared.map(([at, bytes, value]) => [at + 2, bytes, value]), [42, 4, offset]]);
            parts.push(local, path, data);
            directory.push(central, path);
            offset += local.length + path.length + data.length;
        }
        const size = directory.reduce((total, part) => total + part.length, 0);
        const end = header(22, [[0, 4, 0x06054b50], [8, 2, files.length], [10, 2, files.length], [12, 4, size], [16, 4, offset]]);
        return new Blob([...parts, ...directory, end], {type: 'application/zip'});
    };
    const inflate = async data => new Uint8Array(
        await new Response(new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer()
    );
    const unzip = async buffer => {
        const view = new DataView(buffer);
        let end = buffer.byteLength - 22;
        while (end >= 0 && view.getUint32(end, true) !== 0x06054b50) end--;
        if (end < 0) throw new Error('Not a zip archive');
        const files = [];
        for (let i = 0, at = view.getUint32(end + 16, true); i < view.getUint16(end + 10, true); i++) {
            const method = view.getUint16(at + 10, true);
            const size = view.getUint32(at + 20, true);
            const length = view.getUint16(at + 28, true);
            const local = view.getUint32(at + 42, true);
            const name = new TextDecoder().decode(new Uint8Array(buffer, at + 46, length));
            at += 46 + length + view.getUint16(at + 30, true) + view.getUint16(at + 32, true);
            if (name.endsWith('/')) continue;
            if (method !== 0 && method !== 8) throw new Error(`Unsupported compression in ${name}`);
            const start = local + 30 + view.getUint16(local + 26, true) + view.getUint16(local + 28, true);
            const data = new Uint8Array(buffer, start, size);
            files.push([name, method === 8 ? await inflate(data) : data]);
        }
        return files;
    };
    // A project archive holds project.json and its content folder, possibly inside one top folder.
    const importFiles = (archive, file) => {
        const manifestName = archive.map(([name]) => name)
            .filter(name => name === 'project.json' || name.endsWith('/project.json'))
            .sort((a, b) => a.split('/').length - b.split('/').length)[0];
        if (!manifestName) throw new Error('The archive has no project.json');
        const prefix = manifestName.slice(0, -'project.json'.length);
        const manifest = JSON.parse(new TextDecoder().decode(archive.find(([name]) => name === manifestName)[1]));
        const content = manifest.content;
        if (typeof content !== 'string' || !content || content.startsWith('/') || content.split('/').includes('..')) {
            throw new Error('The project content folder must be inside the project');
        }
        const id = unique(manifest.name || file.replace(/\.zip$/i, ''));
        for (const [name, data] of archive) {
            const path = name.slice(prefix.length);
            const inProject = path === 'project.json' || path.startsWith(content + '/');
            if (name.startsWith(prefix) && inProject && !path.split('/').includes('..')) write(`${workspace}/${id}/${path}`, data);
        }
        return id;
    };

    Module['chibiSaveAsset'] = () => {
        keep();
        persist();
        return 0;
    };
    const exportProject = () => {
        const files = [];
        projectFiles(`${workspace}/${project}/`, (name, data) => files.push([`${project}/${name}`, data]));
        const link = document.createElement('a');
        link.href = URL.createObjectURL(zip(files));
        link.download = `${project}.zip`;
        link.click();
        // Revoking right away can cancel the download in some browsers.
        setTimeout(() => URL.revokeObjectURL(link.href), 40000);
    };
    const newProject = () => {
        const search = new URLSearchParams({new: ''});
        if (launch.has('project-root')) search.set('project-root', launch.get('project-root'));
        location.search = `?${search}`;
    };
    // The shell offers these once Chibi stops, so a project that cannot load never traps the user.
    Module['recovery'] = [['New Project', newProject]];
    Module['chibiExportProject'] = exportProject;
    Module['chibiImportProject'] = () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.zip,application/zip';
        input.onchange = async () => {
            try {
                const file = input.files[0];
                const id = importFiles(await unzip(await file.arrayBuffer()), file.name);
                fs().writeFile(last, id);
                keep();
                await sync();
                location.search = `?project=${id}`;
            } catch (error) {
                Module['showError'](`Could not import project: ${error.message}`);
            }
        };
        input.click();
    };
    (Module['preRun'] ||= []).push(() => {
        fs().mkdirTree(workspace);
        fs().mount(fs().filesystems.IDBFS, {}, workspace);
        addRunDependency('chibi-storage');
        fs().syncfs(true, error => {
            report(error);
            removeRunDependency('chibi-storage');
        });
    });
    // Bundled samples are readable only once every run dependency has resolved.
    Module['onRuntimeInitialized'] = () => {
        const previous = lastOpened();
        project = chooseProject();
        args[1] = `${workspace}/${project}/`;
        Module['recovery'].unshift(['Export Project', exportProject]);
        if (previous === project) return;
        fs().writeFile(last, project);
        persist();
    };
}
