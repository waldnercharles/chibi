// Installed only by the local development server. Packaged builds stay standalone.
(() => {
    const token = __CHIBI_SAVE_TOKEN__;
    const query = new URLSearchParams(location.search);
    const scene = query.get('scene');
    const root = '/workspace/';
    Module['arguments'] = ['--project-root', root];
    if (scene) Module['arguments'].push('--scene', scene);
    if (query.get('play') === '1') Module['arguments'].push('--play');
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
            if (!path.startsWith(root)) throw new Error('Asset is outside the served project');
            request('POST', '/__chibi/save', {path: path.slice(root.length), text});
            return 0;
        } catch (error) {
            Module['showError'](error.message);
            return -1;
        }
    };
    let nextCommand = 0;
    const commands = new Map();
    Module['chibiCommandStart'] = (body) => {
        const id = ++nextCommand;
        const controller = new AbortController();
        const command = { controller };
        commands.set(id, command);
        fetch('/__chibi/command', {
            method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Chibi-Token': token },
            body, signal: controller.signal,
        }).then(async response => {
            if (!response.ok) throw new Error(`Laya proxy returned HTTP ${response.status}`);
            command.result = await response.text();
        }).catch(error => { command.result = JSON.stringify({ok: false, error: error.message}); });
        return id;
    };
    Module['chibiCommandPoll'] = id => commands.get(id)?.result;
    Module['chibiCommandCancel'] = id => {
        commands.get(id)?.controller.abort();
        commands.delete(id);
    };
    const inbox = [];
    let session;
    const post = async (path, body) => {
        const response = await fetch('/__chibi/' + path, {
            method: 'POST', headers: {'Content-Type': 'application/json', 'X-Chibi-Token': token},
            body: JSON.stringify(body),
        });
        if (!response.ok) throw new Error(`Editor bridge returned HTTP ${response.status}`);
        return response.json();
    };
    Module['chibiEditorNext'] = () => inbox.shift();
    Module['chibiEditorTrace'] = record => post('trace', record).catch(console.error);
    Module['chibiEditorReply'] = (id, result) => post('session/reply', {session, id, result}).catch(console.error);
    const register = async () => {
        session = (await post('session/register', {})).session;
        Module['chibiEditorSession'] = session;
        while (session) {
            try {
                const messages = await post('session/poll', {session});
                inbox.push(...messages);
            } catch (error) {
                console.error(error);
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
        register().catch(console.error);
    };
})();
