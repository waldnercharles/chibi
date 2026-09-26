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
        return xhr.responseText;
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
    const initialize = Module['onRuntimeInitialized'];
    Module['onRuntimeInitialized'] = () => {
        try {
            const files = JSON.parse(request('GET', '/__chibi/assets'));
            for (const [path, text] of Object.entries(files)) {
                const destination = '/' + path;
                Module['FS'].mkdirTree(destination.slice(0, destination.lastIndexOf('/')));
                Module['FS'].writeFile(destination, text);
            }
        } catch (error) {
            Module['showError'](error.message);
            throw error;
        }
        initialize?.();
    };
})();
