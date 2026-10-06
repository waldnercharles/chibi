// Installed only by the local development server. Packaged builds stay standalone.
(() => {
    const token = __CHIBI_SAVE_TOKEN__;
    const query = new URLSearchParams(location.search);
    const scene = query.get('scene');
    // ?project-root= opens another mirrored project, as in the packaged build.
    const project = query.has('project-root') ? '/' + query.get('project-root').replace(/^\/+/, '') : __CHIBI_PROJECT__;
    Module['arguments'] = ['--project-root', project];
    if (scene) Module['arguments'].push('--scene', scene);
    if (query.get('play') === '1') Module['arguments'].push('--play');
    for (const name of ['open', 'graph']) {
        if (query.get(name)) Module['arguments'].push(`--${name}`, query.get(name));
    }
    const request = (method, path, body) => {
        const xhr = new XMLHttpRequest();
        xhr.open(method, path, false);
        xhr.setRequestHeader('X-Chibi-Token', token);
        if (body) xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send(body ? JSON.stringify(body) : null);
        if (xhr.status < 200 || xhr.status >= 300) {
            let message = xhr.responseText || 'Local save failed';
            try { message = JSON.parse(xhr.responseText).error || message; } catch {}
            throw new Error(message);
        }
        return xhr;
    };
    const isRoomAsset = path => {
        const storage = Module['chibiCollabStorage'];
        const root = storage?.root;
        if (!root || root === storage.localRoot) return false;
        const absolute = path.startsWith('/') ? path : `/${path}`;
        return absolute.startsWith(root.endsWith('/') ? root : `${root}/`);
    };
    Module['chibiSaveAsset'] = (path, text) => {
        // Room files live in MEMFS and are carried by snapshots. The local
        // development mirror must never write them back to the source project.
        if (isRoomAsset(path)) return 0;
        try {
            request('POST', '/__chibi/save', {path, text});
            return 0;
        } catch (error) {
            Module['showError'](error.message);
            return -1;
        }
    };
    // A save without text deletes the file, as a rename or delete does.
    Module['chibiDeleteAsset'] = path => Module['chibiSaveAsset'](path, null);
    const sameBytes = (left, right) => {
        if (!left || left.length !== right.length) return false;
        for (let i = 0; i < right.length; i++) if (left[i] !== right[i]) return false;
        return true;
    };
    // An unchanged file keeps its time, so the page's own saves do not read as edits.
    const mirror = (path, value) => {
        const fs = Module['FS'];
        const destination = '/' + path;
        const exists = fs.analyzePath(destination).exists;
        if (value === null) {
            if (exists) fs.unlink(destination);
            return;
        }
        if (typeof value === 'string') {
            if (exists && fs.readFile(destination, {encoding: 'utf8'}) === value) return;
        } else {
            if (value?.encoding !== 'base64' || typeof value.data !== 'string') {
                throw new Error(`Unsupported asset payload for ${path}`);
            }
            const decoded = atob(value.data);
            value = Uint8Array.from(decoded, character => character.charCodeAt(0));
            if (exists && sameBytes(fs.readFile(destination), value)) return;
        }
        fs.mkdirTree(destination.slice(0, destination.lastIndexOf('/')));
        fs.writeFile(destination, value);
    };
    const mirrorProject = project => {
        for (const [path, value] of Object.entries(project.assets)) mirror(path, value);
    };
    let pendingProject = null;
    Module['chibiCreateProject'] = (name, version) => {
        try {
            if (pendingProject && (pendingProject.name !== name || pendingProject.version !== version)) {
                mirrorProject(pendingProject);
                pendingProject = null;
            }
            if (!pendingProject || pendingProject.name !== name || pendingProject.version !== version) {
                const project = JSON.parse(request('POST', '/__chibi/project', {name, version}).responseText);
                if (typeof project.root !== 'string' || !project.assets || typeof project.assets !== 'object') {
                    throw new Error('Invalid local project response');
                }
                pendingProject = {name, version, root: project.root, assets: project.assets};
            }
            const project = pendingProject;
            mirrorProject(project);
            pendingProject = null;
            return JSON.stringify({root: project.root});
        } catch (error) {
            return JSON.stringify({error: error.message});
        }
    };
    Module['chibiRememberProject'] = root => {
        const match = root.match(/^\/projects\/([a-z0-9-]+)\/$/);
        if (match) history.replaceState(null, '', `?project-root=projects/${match[1]}`);
    };
    // Edits made on disk reach the editor, which reloads them as it does natively.
    const follow = async version => {
        for (;;) {
            try {
                const response = await fetch(`/__chibi/changes?since=${version}`, {headers: {'X-Chibi-Token': token}});
                if (!response.ok) throw new Error(await response.text());
                version = Number(response.headers.get('X-Chibi-Version'));
                for (const [path, text] of Object.entries(await response.json())) mirror(path, text);
            } catch {
                await new Promise(retry => setTimeout(retry, 1000));
            }
        }
    };
    // chibi.data preloads Studio before this hook. Remove mirrored files absent
    // from the checkout snapshot so deletes/renames survive a page reload.
    // Keep bundled resources the server does not mirror (for example fonts).
    const reconcileStudio = assets => {
        const fs = Module['FS'];
        const root = '/projects/studio';
        if (!fs.analyzePath(root).exists) return;
        const visit = directory => {
            for (const name of fs.readdir(directory)) {
                if (name === '.' || name === '..') continue;
                const path = `${directory}/${name}`;
                if (fs.isDir(fs.stat(path).mode)) visit(path);
                // Matches web_serve.py's TEXT_EXTENSIONS and IMAGE_EXTENSIONS.
                else if (/\.(kdl|rml|rcss|png|jpe?g)$/i.test(name) &&
                         !Object.prototype.hasOwnProperty.call(assets, path.slice(1))) fs.unlink(path);
            }
        };
        visit(root);
    };
    const initialize = Module['onRuntimeInitialized'];
    Module['onRuntimeInitialized'] = () => {
        try {
            const xhr = request('GET', '/__chibi/assets');
            const assets = JSON.parse(xhr.responseText);
            reconcileStudio(assets);
            for (const [path, text] of Object.entries(assets)) mirror(path, text);
            follow(Number(xhr.getResponseHeader('X-Chibi-Version')));
        } catch (error) {
            Module['showError'](error.message);
            throw error;
        }
        initialize?.();
    };
})();
