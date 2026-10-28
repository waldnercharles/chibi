import { DEFAULTS, configuration, options, readLink, shareLink, sourceBytes } from './state.js';

const get = id => document.getElementById(id);
const source = get('format-source');
const output = get('format-output');
const status = get('format-status');
const error = get('format-error');
const resultLabel = get('format-result-label');
const feedback = get('format-feedback');
const form = get('format-options');
const copy = get('format-copy');
const download = get('format-download');
const configButtons = [get('format-config-copy'), get('format-config-download')];
const share = get('format-share');
const retry = get('format-retry');
const example = `package example

// Change an option to see the real formatter at work.
Greeting :: (name: string, punctuation: string) -> string {
return format_text("Hello, {}{}",name,punctuation);
}

Names :: () -> string[] {
return ["Ada", "Grace", "Margaret", "Katherine"];
}
`;
let worker = null;
let ready = false;
let revision = '';
let sharedRevision = '';
let generation = 0;
let running = null;
let debounce = 0;
let watchdog = 0;

function selected() {
    return options({
        indent_width: form.elements.indent_width.valueAsNumber,
        preferred_width: form.elements.preferred_width.valueAsNumber,
        use_tabs: form.elements.use_tabs.checked,
        remove_optional_semicolons: form.elements.remove_optional_semicolons.checked,
    });
}

function choose(value) {
    for (const [key, setting] of Object.entries(value)) {
        const field = form.elements[key];
        if (typeof setting === 'boolean') field.checked = setting;
        else field.value = String(setting);
    }
}

function invalidate() {
    generation += 1;
    copy.disabled = download.disabled = true;
    get('format-share-result').hidden = true;
    resultLabel.textContent = output.value ? 'Out of date — updating' : 'Waiting for preview';
    error.hidden = true;
    source.removeAttribute('aria-invalid');
}

function showError(message) {
    error.textContent = message;
    error.hidden = false;
    output.value = '';
    copy.disabled = download.disabled = true;
    resultLabel.textContent = 'No preview';
}

function refreshConfig() {
    try {
        const value = selected();
        get('format-config-text').textContent = configuration(value);
        source.style.tabSize = output.style.tabSize = String(value.indent_width);
        configButtons.forEach(button => { button.disabled = false; });
        share.disabled = !ready;
        return true;
    } catch (failure) {
        get('format-config-text').textContent = 'Enter valid formatting options to export a configuration.';
        configButtons.forEach(button => { button.disabled = true; });
        share.disabled = true;
        showError(failure.message);
        status.textContent = 'Enter valid formatting options.';
        return false;
    }
}

function revisionWarning() {
    const warning = get('format-version-warning');
    warning.hidden = !sharedRevision || !revision || sharedRevision === revision;
    warning.textContent = warning.hidden ? '' :
        `This link was created with formatter ${sharedRevision}. This page runs ${revision}. Its settings are loaded, but the preview may differ.`;
}

function stop(message) {
    clearTimeout(debounce);
    clearTimeout(watchdog);
    worker?.terminate();
    worker = null;
    ready = false;
    running = null;
    revision = '';
    get('format-revision').textContent = 'not loaded';
    revisionWarning();
    share.disabled = true;
    retry.hidden = false;
    status.textContent = message;
    showError('Your source and settings are unchanged. Retry to start a fresh formatter.');
}

function format() {
    clearTimeout(debounce);
    if (!refreshConfig() || !ready || running !== null) return;
    try {
        sourceBytes(source.value);
        running = generation;
        worker.postMessage({ type: 'format', id: running, source: source.value, options: selected() });
        status.textContent = 'Formatting in your browser…';
        watchdog = setTimeout(() => stop('Formatting took too long and was stopped. Try a smaller source.'), 3000);
    } catch (failure) {
        running = null;
        showError(failure.message);
        status.textContent = 'Source could not be formatted.';
    }
}

function changed() {
    invalidate();
    refreshConfig();
    clearTimeout(debounce);
    debounce = setTimeout(format, 180);
}

function start() {
    clearTimeout(debounce);
    clearTimeout(watchdog);
    worker?.terminate();
    ready = false;
    revision = '';
    get('format-revision').textContent = 'not loaded';
    revisionWarning();
    running = null;
    retry.hidden = true;
    status.textContent = 'Loading the Chibi formatter…';
    invalidate();
    refreshConfig();
    try {
        const current = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
        worker = current;
        current.onerror = event => {
            if (worker !== current) return;
            event.preventDefault();
            stop('The formatter could not run. Its WebAssembly files may be unavailable.');
        };
        current.onmessage = ({ data }) => {
            if (worker !== current) return;
            if (data.type === 'fatal') {
                stop(data.error);
                return;
            }
            if (data.type === 'ready') {
                clearTimeout(watchdog);
                revision = data.revision;
                ready = true;
                get('format-revision').textContent = revision;
                revisionWarning();
                format();
                return;
            }
            if (data.type !== 'result' || data.id !== running) return;
            clearTimeout(watchdog);
            running = null;
            if (data.id !== generation) {
                format();
                return;
            }
            if (data.error) {
                showError(data.error);
                source.setAttribute('aria-invalid', 'true');
                status.textContent = 'Source could not be formatted.';
                return;
            }
            output.value = data.text;
            copy.disabled = download.disabled = false;
            resultLabel.textContent = data.text === source.value ? 'Already formatted' : 'Up to date';
            status.textContent = 'Preview ready. Source stays in your browser.';
        };
        watchdog = setTimeout(() => stop('The formatter did not finish loading.'), 20000);
    } catch {
        stop('This browser could not start the formatter worker.');
    }
}

function loadLink() {
    try {
        const saved = readLink(location.hash);
        if (!saved) return;
        choose(saved.options);
        if (saved.source !== undefined) source.value = saved.source;
        sharedRevision = saved.engine;
        revisionWarning();
        feedback.textContent = saved.source === undefined ? 'Shared settings loaded.' : 'Shared settings and source loaded.';
        changed();
    } catch (failure) {
        feedback.textContent = failure.message;
    }
}

async function copyText(text, message) {
    try {
        await navigator.clipboard.writeText(text);
        feedback.textContent = message;
    } catch {
        feedback.textContent = 'Clipboard access is unavailable. Select and copy the text, or use Download.';
    }
}

function save(text, filename) {
    const url = URL.createObjectURL(new Blob([text], { type: 'text/plain;charset=utf-8' }));
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = filename;
    document.body.append(anchor);
    anchor.click();
    anchor.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    feedback.textContent = `Download started: ${filename}`;
}

form.addEventListener('submit', event => event.preventDefault());
form.addEventListener('input', changed);
source.addEventListener('input', changed);
retry.addEventListener('click', start);
copy.addEventListener('click', () => copyText(output.value, 'Formatted source copied.'));
download.addEventListener('click', () => save(output.value, 'formatted.chibi'));
configButtons[0].addEventListener('click', () => copyText(configuration(selected()), 'Configuration copied.'));
configButtons[1].addEventListener('click', () => save(configuration(selected()), '.chibi-format'));
get('format-example').addEventListener('click', () => {
    if (source.value && source.value !== example && !confirm('Replace your current source with the example?')) return;
    source.value = example;
    changed();
    source.focus();
});
get('format-share-source').addEventListener('change', () => { get('format-share-result').hidden = true; });
share.addEventListener('click', () => {
    try {
        const url = shareLink(location.href, revision, selected(),
            get('format-share-source').checked ? source.value : undefined);
        get('format-share-url').value = url;
        get('format-share-result').hidden = false;
        feedback.textContent = get('format-share-source').checked ? 'Link created with your source included.' : 'Link created with settings only.';
    } catch (failure) {
        get('format-share-result').hidden = true;
        feedback.textContent = failure.message;
    }
});
get('format-share-copy').addEventListener('click', () => copyText(get('format-share-url').value, 'Share link copied.'));
addEventListener('hashchange', loadLink);
addEventListener('pagehide', () => { clearTimeout(watchdog); worker?.terminate(); });
addEventListener('pageshow', event => { if (event.persisted) start(); });
choose(DEFAULTS);
source.value = example;
loadLink();
start();
