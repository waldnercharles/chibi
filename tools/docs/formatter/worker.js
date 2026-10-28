import { options, sourceBytes } from './state.js';

let engine;

function text(pointer) {
    const heap = engine.HEAPU8;
    const end = heap.indexOf(0, pointer);
    if (pointer <= 0 || end < pointer) throw new Error('Invalid formatter text result.');
    // Preserve a literal leading U+FEFF rather than silently treating it as a
    // transport BOM. Formatted bytes must match the native CLI exactly.
    return new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(heap.subarray(pointer, end));
}

function format(data) {
    const selected = options(data.options);
    const bytes = sourceBytes(data.source);
    let input = 0;
    let result = 0;
    try {
        input = engine._malloc(bytes.length + 1);
        if (!input) throw new Error('The formatter ran out of memory.');
        engine.HEAPU8.set(bytes, input);
        engine.HEAPU8[input + bytes.length] = 0;
        result = engine._chibi_format_web(input, bytes.length, selected.indent_width,
            selected.preferred_width, Number(selected.use_tabs), Number(selected.remove_optional_semicolons));
        if (!result) {
            return { error: text(engine._chibi_format_web_error()) };
        }
        return { text: text(result) };
    } finally {
        if (result) engine._free(result);
        if (input) engine._free(input);
    }
}

try {
    const { default: createFormatter } = await import('./engine.mjs');
    engine = await createFormatter();
    const revision = text(engine._chibi_format_web_revision());
    if (engine._chibi_format_web_api() !== 1 || !/^[0-9a-f]{40}$/.test(revision)) {
        throw new Error('Formatter version does not match this playground.');
    }
    self.onmessage = ({ data }) => {
        if (data.type !== 'format' || !Number.isSafeInteger(data.id)) return;
        try {
            self.postMessage({ type: 'result', id: data.id, ...format(data) });
        } catch (failure) {
            // A trapped/aborted WASM instance is not reused. The main page owns
            // Retry and a wall-time watchdog; no fallback formatter exists.
            self.postMessage({ type: 'fatal', error: `The formatter stopped: ${failure.message}` });
            self.close();
        }
    };
    self.postMessage({ type: 'ready', revision });
} catch (failure) {
    self.postMessage({ type: 'fatal', error: `The formatter could not load: ${failure.message}` });
    self.close();
}
