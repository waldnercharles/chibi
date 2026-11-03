import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { closeSync, mkdtempSync, openSync, readFileSync, rmSync, writeSync } from 'node:fs';
import { request as httpRequest } from 'node:http';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const admin = '0123456789abcdef0123456789abcdef';
const signals = { skip: process.platform === 'win32' };

function request(port, method = 'GET', path = '/healthz', token, body, timeout = 1000) {
  return new Promise((resolve, reject) => {
    const data = body === undefined ? '' : JSON.stringify(body);
    const headers = { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) };
    if (token) headers.Authorization = `Bearer ${token}`;
    const outgoing = httpRequest({ hostname: '127.0.0.1', port, method, path, headers, agent: false }, (response) => {
      const chunks = [];
      response.on('data', (chunk) => chunks.push(chunk));
      response.on('error', reject);
      response.on('end', () => {
        try { resolve({ status: response.statusCode, body: JSON.parse(Buffer.concat(chunks)) }); }
        catch (error) { reject(error); }
      });
    });
    outgoing.setTimeout(timeout, () => outgoing.destroy(Object.assign(new Error('receive timeout'), { code: 'ETIMEDOUT' })));
    outgoing.on('error', reject);
    outgoing.end(data);
  });
}

// The real server owns an ephemeral port. Each failed test keeps its complete
// log; process cleanup does not depend on health or graceful signal handling.
async function withServer(t, env, run) {
  const directory = mkdtempSync(join(tmpdir(), 'chibi-collab-'));
  const log = join(directory, 'server.log');
  const fd = openSync(log, 'w');
  let output = '';
  let spawnError;
  const child = spawn(process.execPath, [fileURLToPath(new URL('./server.mjs', import.meta.url))], {
    env: { ...process.env, NODE_ENV: 'development', COLLAB_HOST: '127.0.0.1', COLLAB_PORT: '0',
      COLLAB_LOCAL_TOKEN_ISSUANCE: '0', COLLAB_ALLOWED_ORIGINS: '', COLLAB_PUBLIC_WS_URL: '',
      COLLAB_HOST_GRACE_MS: '30000', COLLAB_ADMIN_TOKEN: admin,
      COLLAB_TOKEN_SECRET: 'fedcba9876543210fedcba9876543210', ...env },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.once('error', (error) => { spawnError = error; });
  const closed = new Promise((resolve) => child.once('close', resolve));
  for (const stream of [child.stdout, child.stderr]) {
    stream?.on('data', (bytes) => { output += bytes.toString(); writeSync(fd, bytes); });
  }
  const fixture = {
    child, log, closed,
    async ready(timeout = 5000) {
      const deadline = performance.now() + timeout;
      let lastError;
      while (performance.now() < deadline) {
        if (spawnError) throw spawnError;
        if (child.exitCode !== null || child.signalCode !== null) {
          throw new Error(`server exited with status ${child.exitCode ?? child.signalCode} before readiness`);
        }
        const listening = /listening on 127\.0\.0\.1:(\d+)/.exec(output);
        if (listening) {
          fixture.port = Number(listening[1]);
          try {
            const response = await request(fixture.port, 'GET', '/healthz', undefined, undefined,
              Math.max(1, Math.min(100, deadline - performance.now())));
            if (response.status === 200) return;
          } catch (error) { lastError = error; }
        }
        await delay(20);
      }
      throw new Error('health deadline expired', { cause: lastError });
    },
  };
  let passed = false;
  try { await run(fixture); passed = true; }
  finally {
    try {
      if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
      await closed;
    } finally {
      closeSync(fd);
      if (passed) rmSync(directory, { recursive: true });
      else t.diagnostic(`Server log retained at ${log}\n${output}`);
    }
  }
}

test('room creation and invites require the correct authority', async (t) => {
  await withServer(t, {}, async (server) => {
    await server.ready();
    assert.equal((await request(server.port, 'POST', '/v1/rooms', 'invalid-token-with-at-least-32-bytes', {})).status, 401);
    const created = await request(server.port, 'POST', '/v1/rooms', admin, {});
    assert.equal(created.status, 201);
    assert.equal(created.body.role, 'host');
    assert.ok(created.body.room && created.body.token);
    const path = `/v1/rooms/${created.body.room}/invites`;
    const invited = await request(server.port, 'POST', path, created.body.token, { role: 'viewer' });
    assert.equal(invited.status, 201);
    assert.equal(invited.body.room, created.body.room);
    assert.equal(invited.body.role, 'viewer');
    assert.ok(invited.body.token);
    const denied = await request(server.port, 'POST', path, invited.body.token, { role: 'editor' });
    assert.equal(denied.status, 403);
    assert.equal(denied.body.error, 'forbidden');
  });
});

test('server exit fails readiness and keeps its log', async (t) => {
  await withServer(t, { COLLAB_HOST_GRACE_MS: '0' }, async (server) => {
    await assert.rejects(server.ready(), /server exited with status/);
    await server.closed;
    assert.notEqual(server.child.exitCode, 0);
    assert.match(readFileSync(server.log, 'utf8'), /COLLAB_HOST_GRACE_MS must be a positive integer/);
  });
});

test('occupied port fails readiness', async (t) => {
  const occupied = createServer();
  occupied.listen(0, '127.0.0.1');
  await once(occupied, 'listening');
  try {
    await withServer(t, { COLLAB_PORT: String(occupied.address().port) }, async (server) => {
      await assert.rejects(server.ready(), /server exited with status/);
      await server.closed;
      assert.notEqual(server.child.exitCode, 0);
      assert.match(readFileSync(server.log, 'utf8'), /EADDRINUSE/);
    });
  } finally { await new Promise((resolve) => occupied.close(resolve)); }
});

test('live process without health fails readiness', signals, async (t) => {
  await withServer(t, {}, async (server) => {
    await server.ready();
    assert.equal(server.child.kill('SIGSTOP'), true);
    try { await assert.rejects(server.ready(100), /health deadline expired/); }
    finally { server.child.kill('SIGCONT'); }
    assert.equal(server.child.exitCode, null);
    assert.equal(server.child.signalCode, null);
  });
});

test('requests report real receive and connection failures', signals, async (t) => {
  await withServer(t, {}, async (server) => {
    await server.ready();
    assert.equal(server.child.kill('SIGSTOP'), true);
    try { await assert.rejects(request(server.port), { code: 'ETIMEDOUT' }); }
    finally { server.child.kill('SIGCONT'); }
    server.child.kill('SIGKILL');
    await server.closed;
    await assert.rejects(request(server.port), { code: 'ECONNREFUSED' });
  });
});
