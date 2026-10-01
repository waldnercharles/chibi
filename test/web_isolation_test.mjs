import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const source = readFileSync(new URL('../tools/web_isolation.js', import.meta.url), 'utf8');
const tick = () => new Promise(resolve => setImmediate(resolve));

async function page(isolated, controller, supported = true, options = {}) {
    const appended = [], errors = [];
    let reloads = 0, registrations = 0;
    const redirects = [];
    const listeners = {};
    const serviceWorker = supported ? {
        controller,
        ready: Promise.resolve(),
        addEventListener() {}, removeEventListener() {},
        async register(url) {
            registrations++;
            if (options.reject) throw new Error('Blocked');
            if (options.timeout) return new Promise(() => {});
            this.controller = { scriptURL: url };
        },
    } : undefined;
    const module = { showError: error => errors.push(error), onAbort: error => errors.push(error) };
    runInNewContext(source, {
        URL, Worker: options.noWorker ? undefined : class {
            constructor() {
                if (options.throwWorker) throw new Error('Blocked constructor');
                if (options.workerTimeout) return;
                setImmediate(() => options.rejectWorker ? this.onerror(new Error('Blocked worker')) : this.onmessage({ data: options.unsupported ? 'chibi-worker-unsupported' : 'chibi-worker-ready' }));
            }
            terminate() {}
        },
        SharedArrayBuffer: options.noMemory ? undefined : SharedArrayBuffer,
        setTimeout: options.timeout || options.workerTimeout ? callback => setImmediate(callback) : setTimeout,
        clearTimeout: options.timeout || options.workerTimeout ? clearImmediate : clearTimeout,
        document: { createElement: () => ({}), body: { append: script => appended.push(script) } },
        window: {
            crossOriginIsolated: isolated, isSecureContext: true,
            addEventListener: (event, callback) => listeners[event] = callback,
            removeEventListener: event => delete listeners[event],
            location: { href: 'https://example.github.io/chibi/threads/chibi.html?play#scene', reload: () => reloads++, replace: url => redirects.push(url) },
        },
        navigator: { serviceWorker },
        Module: module,
    });
    await tick();
    await tick();
    return { appended, errors, reloads, registrations, redirects, module, listeners };
}

const first = await page(false, null);
assert.equal(first.registrations, 1);
assert.equal(first.reloads, 1);
assert.equal(first.appended.length, 0, 'never initialize shared memory before isolation');
const ready = await page(true, null);
assert.equal(ready.appended[0].src, 'chibi.js');
assert.equal(ready.registrations, 0);
const failed = await page(false, { scriptURL: 'https://example.github.io/chibi/threads/chibi-isolation.js' });
assert.equal(failed.reloads, 0, 'a blocked policy must not loop reloads');
assert.equal(failed.errors.length, 0);
assert.equal(failed.redirects[0], 'https://example.github.io/chibi/chibi.html?play=&serial=1#scene');
assert.equal((await page(false, null, false)).redirects.length, 1);
for (const options of [{ noWorker: true }, { reject: true }, { timeout: true }]) {
    assert.equal((await page(false, null, true, options)).redirects.length, 1);
}
assert.equal((await page(true, null, true, { noMemory: true })).redirects.length, 1);
for (const options of [{ rejectWorker: true }, { throwWorker: true }, { workerTimeout: true }, { unsupported: true }]) {
    assert.equal((await page(true, null, true, options)).redirects.length, 1);
}
ready.module.onAbort('Worker startup failed');
assert.equal(ready.redirects.length, 1);
const running = await page(true, null);
running.module.postRun[0]();
running.module.onAbort('Engine failed');
assert.equal(running.redirects.length, 0);
assert.equal(running.errors[0], 'Engine failed');
assert.equal(Object.keys(running.listeners).length, 0);
const rejectedWorker = await page(true, null);
rejectedWorker.listeners.error();
assert.equal(rejectedWorker.redirects.length, 1);

let probeMessage;
runInNewContext(source, { self: { postMessage: message => probeMessage = message } });
assert.equal(probeMessage, 'chibi-worker-ready');
runInNewContext(source, {
    self: { postMessage: message => probeMessage = message },
    WebAssembly: { validate: () => false },
});
assert.equal(probeMessage, 'chibi-worker-unsupported');

const handlers = {};
let claimed = false, installed = false;
runInNewContext(source, {
    URL, Headers, Response,
    self: {
        location: { origin: 'https://example.github.io' },
        addEventListener: (name, handler) => handlers[name] = handler,
        skipWaiting: async () => installed = true,
        clients: { claim: async () => claimed = true },
    },
    fetch: async () => new Response('wasm', { status: 200, headers: { 'Content-Type': 'application/wasm' } }),
});
await handlers.install({ waitUntil: promise => promise });
await handlers.activate({ waitUntil: promise => promise });
assert(installed && claimed);
let response;
handlers.fetch({
    request: { url: 'https://example.github.io/chibi/threads/chibi.wasm' },
    respondWith: promise => response = promise,
});
response = await response;
assert.equal(response.headers.get('Cross-Origin-Opener-Policy'), 'same-origin');
assert.equal(response.headers.get('Cross-Origin-Embedder-Policy'), 'require-corp');
assert.equal(response.headers.get('Content-Type'), 'application/wasm');
assert.equal(await response.text(), 'wasm');
handlers.fetch({
    request: { url: 'https://elsewhere.example/resource' },
    respondWith: () => assert.fail('worker must not intercept another origin'),
});
const defaultSource = readFileSync(new URL('../tools/web_default.js', import.meta.url), 'utf8');
async function defaultPage({ available = true, supported = true, search = '?play', fail = false } = {}) {
    const appended = [], redirects = [], requests = [];
    runInNewContext(defaultSource, {
        URL, AbortController, setTimeout, clearTimeout,
        Worker: supported ? class {} : undefined,
        document: { createElement: () => ({}), body: { append: script => appended.push(script) } },
        window: {
            crossOriginIsolated: false, isSecureContext: true,
            location: { href: `https://example.github.io/chibi/index.html${search}#scene`, replace: url => redirects.push(url) },
        },
        navigator: { serviceWorker: {} },
        fetch: async url => { requests.push(url.href); if (fail) throw new Error('Offline'); return { ok: available }; },
    });
    await tick();
    return { appended, redirects, requests };
}
assert.equal((await defaultPage()).redirects[0], 'https://example.github.io/chibi/threads/index.html?play#scene');
for (const options of [{ available: false }, { supported: false }, { search: '?serial=1&play' }, { fail: true }]) {
    const result = await defaultPage(options);
    assert.equal(result.redirects.length, 0);
    assert.equal(result.appended[0].src, 'chibi.js');
}
console.log('PASS: worker startup, automatic serial fallback, paired selection, preserved URLs, and isolation responses');
