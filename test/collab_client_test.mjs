import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../tools/collab/client.js', import.meta.url), 'utf8');

const client = href => {
    const order = [];
    const received = [];
    const feedback = [];
    const allocated = new Map();
    const sockets = [];
    const values = new Map();
    let nextPointer = 1;
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
            this.sent = [];
            sockets.push(this);
        }
        send(value) { this.sent.push(value); }
        close() {}
    }
    FakeWebSocket.OPEN = 1;
    const Module = {
        collabConfig: { roomUrl: 'https://rooms.example.test' },
        chibiCollabStorage: { root: '/room/' },
        stringToNewUTF8: text => {
            const pointer = nextPointer++;
            allocated.set(pointer, text);
            return pointer;
        },
        UTF8ToString: pointer => allocated.get(pointer),
        _collab_start() {},
        _collab_receive: pointer => {
            received.push(JSON.parse(allocated.get(pointer)));
            order.push('c');
        },
        _collab_feedback: (pointer, error) => {
            feedback.push({ text: allocated.get(pointer), error: !!error });
            order.push('status');
        },
        _free: pointer => allocated.delete(pointer),
    };
    vm.runInNewContext(source, {
        Module,
        URL,
        URLSearchParams,
        WebSocket: FakeWebSocket,
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
        window: {},
    });
    return { Module, allocated, feedback, order, received, sockets, values };
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
    assert.equal(app.feedback.at(-1).error, true);
    assert.match(app.feedback[0].text, /^Joining room/);
    assert.equal(app.allocated.size, 0, 'feedback strings are freed after crossing the Wasm bridge');
}

{
    const app = client('https://studio.example.test/?room=room-1&role=viewer');
    await app.Module.chibiCollab.control('join', JSON.stringify(invite('wss://rooms.example.test/v1/rooms/room-1/ws')));
    assert.equal(app.sockets.length, 1);
    assert.equal(app.sockets[0].url, 'wss://rooms.example.test/v1/rooms/room-1/ws');

    const socket = app.sockets[0];
    socket.readyState = 1;
    socket.onopen();
    await socket.onmessage({data: JSON.stringify({
        protocol: 1,
        room: 'room-1',
        client: 'service',
        seq: 0,
        revision: 0,
        kind: 'error',
        payload: {reason: 'Room is full'},
    })});

    assert.equal(app.received.length, 1);
    assert.equal(app.received[0].kind, 'error');
    assert.equal(app.received[0].payload.reason, 'Room is full');
    assert.deepEqual(app.order.slice(-2), ['c', 'status']);
    assert.equal(app.feedback.at(-1).text, 'Room is full');
    assert.equal(app.feedback.at(-1).error, true);
    assert.ok(app.feedback.some(({ text }) => text.startsWith('Joining room')));
}

console.log('Collaboration client invite checks passed.');
