// Installed only by the local development server. Packaged builds stay standalone.
(() => {
    const token = __CHIBI_SAVE_TOKEN__;
    const query = new URLSearchParams(location.search);
    const scene = query.get('scene');
    const root = '/workspace/';
    Module['arguments'] = ['--project-root', root];
    if (scene) Module['arguments'].push('--scene', scene);
    if (query.get('play') === '1') Module['arguments'].push('--play');
    if (query.has('imgui')) Module['arguments'].push('--imgui');
    const request = (method, path, body) => {
        const xhr = new XMLHttpRequest();
        xhr.open(method, path, false);
        xhr.setRequestHeader('X-Chibi-Token', token);
        if (body) xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send(body ? JSON.stringify(body) : null);
        if (xhr.status < 200 || xhr.status >= 300) throw new Error(xhr.responseText || 'Local save failed');
        return xhr.responseText;
    };
    const post = async (path, body, signal) => {
        const response = await fetch('/__chibi/' + path, {
            method: 'POST',
            headers: {'Content-Type': 'application/json', 'X-Chibi-Token': token},
            body: typeof body === 'string' ? body : JSON.stringify(body),
            signal,
        });
        if (!response.ok) throw new Error(`Local server returned HTTP ${response.status}`);
        return response.text();
    };
    Module['chibiSaveAsset'] = (path, text) => {
        try {
            if (!path.startsWith(root)) throw new Error('Asset is outside the served project');
            request('POST', '/__chibi/save', {path: path.slice(root.length), text});
            return 0;
        } catch (error) {
            Module['showError'](error.message);
            return -1;
        }
    };

    // Editor command model requests: the server forwards each {url, body}
    // envelope to the loopback model host the page cannot reach itself.
    let nextCommand = 0;
    const commands = new Map();
    Module['chibiCommandStart'] = (envelope) => {
        const id = ++nextCommand;
        const command = {controller: new AbortController()};
        commands.set(id, command);
        post('model', envelope, command.controller.signal)
            .then(text => { command.result = text; })
            .catch(error => { command.result = JSON.stringify({ok: false, error: error.message}); });
        return id;
    };
    Module['chibiCommandPoll'] = id => commands.get(id)?.result;
    Module['chibiCommandCancel'] = id => {
        commands.get(id)?.controller.abort();
        commands.delete(id);
    };

    // A live session lets external command clients reach this editor.
    const inbox = [];
    let session;
    Module['chibiEditorNext'] = () => inbox.shift();
    Module['chibiEditorTrace'] = record => post('trace', record).catch(() => {});
    Module['chibiEditorReply'] = (id, result) => post('session/reply', {session, id, result}).catch(() => {});
    const connect = async () => {
        session = JSON.parse(await post('session/register', {})).session;
        for (;;) {
            try {
                inbox.push(...JSON.parse(await post('session/poll', {session})));
            } catch {
                await new Promise(resolve => setTimeout(resolve, 2000));
            }
        }
    };

    const initialize = Module['onRuntimeInitialized'];
    Module['onRuntimeInitialized'] = () => {
        try {
            const files = JSON.parse(request('GET', '/__chibi/assets'));
            for (const [path, text] of Object.entries(files)) {
                const destination = root + path;
                Module['FS'].mkdirTree(destination.slice(0, destination.lastIndexOf('/')));
                Module['FS'].writeFile(destination, text);
            }
        } catch (error) {
            Module['showError'](error.message);
            throw error;
        }
        initialize?.();
        connect().catch(() => {});
    };
})();
