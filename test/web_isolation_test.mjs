import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const source = readFileSync(new URL('../tools/web_isolation.js', import.meta.url), 'utf8');
const tick = () => new Promise(resolve => setImmediate(resolve));

async function page(isolated, controller, supported = true) {
    const appended = [], errors = [];
    let reloads = 0, registrations = 0;
    const serviceWorker = supported ? {
        controller,
        ready: Promise.resolve(),
        addEventListener() {},
        async register(url) {
            registrations++;
            this.controller = { scriptURL: url };
        },
    } : undefined;
    runInNewContext(source, {
        URL,
        document: { createElement: () => ({}), body: { append: script => appended.push(script) } },
        window: {
            crossOriginIsolated: isolated, isSecureContext: true,
            location: { href: 'https://example.github.io/chibi/threads/chibi.html?play', reload: () => reloads++ },
        },
        navigator: { serviceWorker },
        Module: { showError: error => errors.push(error) },
    });
    await tick();
    return { appended, errors, reloads, registrations };
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
assert.equal(failed.errors.length, 1);
assert.equal((await page(false, null, false)).errors.length, 1);

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
console.log('PASS: static-host isolation gates startup, preserves responses, and avoids reload loops');
