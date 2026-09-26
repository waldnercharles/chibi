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
        if (xhr.status < 200 || xhr.status >= 300) throw new Error(xhr.responseText || 'Local save failed');
        return xhr;
    };
    Module['chibiSaveAsset'] = (path, text) => {
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
    // An unchanged file keeps its time, so the page's own saves do not read as edits.
    const mirror = (path, text) => {
        const fs = Module['FS'];
        const destination = '/' + path;
        const exists = fs.analyzePath(destination).exists;
        if (text === null) {
            if (exists) fs.unlink(destination);
            return;
        }
        if (exists && fs.readFile(destination, {encoding: 'utf8'}) === text) return;
        fs.mkdirTree(destination.slice(0, destination.lastIndexOf('/')));
        fs.writeFile(destination, text);
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
    const initialize = Module['onRuntimeInitialized'];
    Module['onRuntimeInitialized'] = () => {
        try {
            const xhr = request('GET', '/__chibi/assets');
            for (const [path, text] of Object.entries(JSON.parse(xhr.responseText))) mirror(path, text);
            follow(Number(xhr.getResponseHeader('X-Chibi-Version')));
        } catch (error) {
            Module['showError'](error.message);
            throw error;
        }
        initialize?.();
    };
})();
