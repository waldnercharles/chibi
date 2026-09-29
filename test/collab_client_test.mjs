import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../tools/collab/client.js', import.meta.url), 'utf8');

const client = href => {
    const events = [];
    const sockets = [];
    const values = new Map();
    const location = { href };
    const updateLocation = value => {
        const url = new URL(value, location.href);
        Object.assign(location, {
            href: url.href,
            hash: url.hash,
            pathname: url.pathname,
            search: url.search,
        });
    };
    updateLocation(href);
    class FakeWebSocket {
        constructor(url) {
            this.url = url;
            this.readyState = 0;
            sockets.push(this);
        }
        close() {}
    }
    const Module = {
        collabConfig: { roomUrl: 'https://rooms.example.test' },
        chibiCollabStorage: { root: '/room/' },
    };
    const window = { dispatchEvent: event => events.push(event) };
    vm.runInNewContext(source, {
        Module,
        URL,
        URLSearchParams,
        WebSocket: FakeWebSocket,
        CustomEvent: class { constructor(type, options) { this.type = type; this.detail = options.detail; } },
        clearTimeout() {},
        fetch() { throw new Error('unexpected fetch'); },
        history: { replaceState: (_state, _title, value) => updateLocation(value) },
        location,
        navigator: {},
        queueMicrotask() {},
        sessionStorage: {
            getItem: key => values.get(key) || null,
            removeItem: key => values.delete(key),
            setItem: (key, value) => values.set(key, value),
        },
        setTimeout() { return 1; },
        window,
    });
    return { Module, events, sockets, values };
};

const invite = ws_url => ({
    room: 'room-1',
    client: 'client-1',
    role: 'viewer',
    token: 'secret-token',
    ws_url,
    expires_at: Math.floor(Date.now() / 1000) + 600,
});

{
    const app = client('https://studio.example.test/?room=room-1&role=viewer');
    await app.Module.chibiCollab.control('join', JSON.stringify(invite('wss://attacker.example.test/steal')));
    assert.equal(app.sockets.length, 0);
    assert.equal(app.values.size, 0);
    assert.equal(app.events.at(-1).detail.error, true);
}

{
    const app = client('https://studio.example.test/?room=room-1&role=viewer');
    await app.Module.chibiCollab.control('join', JSON.stringify(invite('wss://rooms.example.test/v1/rooms/room-1/ws')));
    assert.equal(app.sockets.length, 1);
    assert.equal(app.sockets[0].url, 'wss://rooms.example.test/v1/rooms/room-1/ws');
}

console.log('Collaboration client invite checks passed.');
