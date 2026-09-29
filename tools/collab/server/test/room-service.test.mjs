import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import test from 'node:test';
import { configFromEnvironment } from '../server.mjs';
import { createRoomService } from '../room-service.mjs';
import { envelope, validateEnvelope, validateSnapshot } from '../protocol.mjs';

function makeSnapshot(files = []) {
  const workspace = JSON.stringify({
    version: 1,
    project: 'projects/demo',
    scene: '{}',
    graph: '{}',
    presentation: '{}',
  });
  const snapshotBody = { checkpoint_version: 1, workspace, files };
  return {
    ...snapshotBody,
    sha256: createHash('sha256').update(JSON.stringify(snapshotBody), 'utf8').digest('hex'),
  };
}

function newClient(service, credentials) {
  const messages = [];
  const state = { closed: false, code: null, reason: null };
  const connection = service.createConnection({
    send(message) { messages.push(message); },
    close(code, reason) { state.closed = true; state.code = code; state.reason = reason; },
  });
  send(service, connection, envelope(credentials.room, credentials.client, 0, 0, 'hello', {
    token: credentials.token,
  }));
  send(service, connection, envelope(credentials.room, credentials.client, 0, 0, 'join', {
    last_revision: 0,
  }));
  return { connection, credentials, messages, state };
}

function send(service, connection, message) {
  service.receive(connection, JSON.stringify(message));
}

function last(messages, kind) {
  return messages.findLast((message) => message.kind === kind);
}

function clear(messages) {
  messages.splice(0, messages.length);
}

function startRoom(options = {}) {
  const service = createRoomService({ secret: 'test secret with at least 32 bytes......', ...options });
  const hostCredentials = service.createRoom();
  const host = newClient(service, hostCredentials);
  const request = last(host.messages, 'snapshot_request');
  assert.ok(request, 'new host receives an initial snapshot request');
  send(service, host.connection, envelope(hostCredentials.room, hostCredentials.client, 0, 0, 'snapshot',
    makeSnapshot()));
  assert.ok(last(host.messages, 'snapshot_ack'), 'host checkpoint is stored and acknowledged');
  clear(host.messages);
  return { service, host, hostCredentials };
}

function acknowledgeSnapshot(service, peer) {
  const snapshot = last(peer.messages, 'snapshot');
  assert.ok(snapshot, 'joining peer receives a checkpoint');
  send(service, peer.connection, envelope(peer.credentials.room, peer.credentials.client, 0,
    snapshot.revision, 'snapshot_ack', {
      revision: snapshot.revision,
      sha256: snapshot.payload.sha256,
    }));
  return snapshot;
}

test('strict envelopes reject extra fields and duplicate JSON object keys', () => {
  const valid = envelope('room_a', 'client_a', 0, 0, 'hello', { token: 'token' });
  assert.throws(() => validateEnvelope({ ...valid, debug: true }), { code: 'invalid_envelope_keys' });

  const service = createRoomService({ secret: 'test secret with at least 32 bytes......' });
  const connection = service.createConnection({ send() {}, close() {} });
  service.receive(connection,
    '{"protocol":1,"room":"room_a","client":"client_a","seq":0,"revision":0,"kind":"hello","kind":"join","payload":{"token":"x"}}');
  assert.equal(connection.closed, true);
});

test('snapshot validation checks sorted safe files and exact UTF-8 hash input', () => {
  const first = { path: 'assets/a.bin', data: Buffer.from([1, 2, 3]).toString('base64') };
  const second = { path: 'assets/b.bin', data: '' };
  const snapshot = makeSnapshot([first, second]);
  assert.doesNotThrow(() => validateSnapshot(snapshot));
  assert.throws(() => validateSnapshot({ ...snapshot, sha256: '0'.repeat(64) }), {
    code: 'snapshot_hash_mismatch',
  });
  assert.throws(() => validateSnapshot(makeSnapshot([second, first])), {
    code: 'unsorted_snapshot_files',
  });
  assert.throws(() => validateSnapshot(makeSnapshot([{ path: '../secret', data: '' }])), {
    code: 'invalid_snapshot_file',
  });
});

test('host authority orders editor proposals, deduplicates retries, and rejects gaps and stale bases', () => {
  const { service, host, hostCredentials } = startRoom();
  const editorCredentials = service.issueInvite(hostCredentials.token, 'editor');
  const editor = newClient(service, editorCredentials);
  acknowledgeSnapshot(service, editor);
  clear(editor.messages);

  const firstAction = { family: 'graph', op: 'set_pin_default', graph: 'g1', node: 'n1' };
  const firstProposal = envelope(editorCredentials.room, editorCredentials.client, 1, 0, 'proposal', firstAction);
  send(service, editor.connection, firstProposal);
  assert.equal(last(editor.messages, 'ack').payload.status, 'pending');
  assert.equal(last(host.messages, 'proposal').client, editorCredentials.client);
  assert.equal(service.getRoom(hostCredentials.room).revision, 0);

  send(service, host.connection,
    envelope(hostCredentials.room, editorCredentials.client, 1, 0, 'accepted', firstAction));
  const accepted = last(editor.messages, 'accepted');
  assert.equal(accepted.revision, 1);
  assert.equal(last(host.messages, 'accepted').revision, 1);

  send(service, editor.connection, firstProposal);
  assert.equal(last(editor.messages, 'accepted').revision, 1, 'retry returns the stored result');
  assert.equal(service.getRoom(hostCredentials.room).revision, 1, 'retry does not create a revision');

  send(service, editor.connection,
    envelope(editorCredentials.room, editorCredentials.client, 3, 1, 'proposal', firstAction));
  assert.equal(last(editor.messages, 'rejected').payload.reason, 'sequence_gap');
  assert.equal(last(editor.messages, 'rejected').payload.expected_seq, 2);

  send(service, editor.connection,
    envelope(editorCredentials.room, editorCredentials.client, 2, 0, 'proposal', firstAction));
  assert.equal(last(editor.messages, 'rejected').payload.reason, 'stale_revision');
  assert.equal(service.getRoom(hostCredentials.room).revision, 1);

  send(service, editor.connection, envelope(editorCredentials.room, editorCredentials.client, 0, 1, 'ack', {}));
  assert.equal(editor.connection.member.ackedRevision, 1);
});

test('viewers cannot submit mutations', () => {
  const { service, host, hostCredentials } = startRoom();
  const viewerCredentials = service.issueInvite(hostCredentials.token, 'viewer');
  const viewer = newClient(service, viewerCredentials);
  acknowledgeSnapshot(service, viewer);
  send(service, viewer.connection,
    envelope(viewerCredentials.room, viewerCredentials.client, 1, 0, 'proposal', { family: 'scene', op: 'rename' }));
  assert.equal(last(viewer.messages, 'rejected').payload.reason, 'read_only');
  assert.equal(service.getRoom(hostCredentials.room).revision, 0);
  assert.equal(last(host.messages, 'proposal'), undefined);
});

test('joining peers receive a snapshot before ordered checkpoint-tail actions', () => {
  const { service, host, hostCredentials } = startRoom();
  const action1 = { family: 'scene', op: 'select_entity', entity: 'entity-1' };
  send(service, host.connection, envelope(hostCredentials.room, hostCredentials.client, 1, 0, 'accepted', action1));

  const viewerCredentials = service.issueInvite(hostCredentials.token, 'viewer');
  const viewer = newClient(service, viewerCredentials);
  const initialSnapshot = last(viewer.messages, 'snapshot');
  assert.equal(initialSnapshot.revision, 0);

  const action2 = { family: 'scene', op: 'set_camera', x: 10, y: 20 };
  send(service, host.connection, envelope(hostCredentials.room, hostCredentials.client, 2, 1, 'accepted', action2));
  assert.equal(last(viewer.messages, 'accepted'), undefined, 'actions wait for snapshot acknowledgement');
  acknowledgeSnapshot(service, viewer);
  const replay = viewer.messages.filter((message) => message.kind === 'accepted');
  assert.deepEqual(replay.map((message) => message.revision), [1, 2]);
});

test('stale checkpoint acknowledgements cannot release a syncing peer', () => {
  const { service, host, hostCredentials } = startRoom();
  const viewerCredentials = service.issueInvite(hostCredentials.token, 'viewer');
  const viewer = newClient(service, viewerCredentials);
  const firstSnapshot = last(viewer.messages, 'snapshot');

  send(service, host.connection, envelope(hostCredentials.room, hostCredentials.client, 1, 0, 'accepted', {
    family: 'scene', op: 'select_entity', entity: 'e1',
  }));
  send(service, host.connection, envelope(hostCredentials.room, hostCredentials.client, 0, 1, 'snapshot', makeSnapshot()));
  const currentSnapshot = last(viewer.messages, 'snapshot');
  assert.equal(currentSnapshot.revision, 1);

  send(service, viewer.connection, envelope(viewerCredentials.room, viewerCredentials.client, 0,
    firstSnapshot.revision, 'snapshot_ack', {
      revision: firstSnapshot.revision,
      sha256: firstSnapshot.payload.sha256,
    }));
  assert.equal(viewer.connection.member.syncing, true, 'old acknowledgement does not release the client');
  assert.equal(last(viewer.messages, 'resync_required'), undefined);
  acknowledgeSnapshot(service, viewer);
  assert.equal(viewer.connection.member.syncing, false);
});

test('replay recovery requests a checkpoint at or beyond the failed revision', () => {
  const { service, host, hostCredentials } = startRoom();
  send(service, host.connection, envelope(hostCredentials.room, hostCredentials.client, 1, 0, 'accepted', {
    family: 'scene', op: 'set_system', system: 'Motion',
  }));
  const viewerCredentials = service.issueInvite(hostCredentials.token, 'viewer');
  const viewer = newClient(service, viewerCredentials);
  acknowledgeSnapshot(service, viewer);
  clear(viewer.messages);

  send(service, viewer.connection, envelope(viewerCredentials.room, viewerCredentials.client, 0, 1,
    'snapshot_request', { last_revision: 1 }));
  assert.equal(last(host.messages, 'snapshot_request').payload.last_revision, 1);
  assert.equal(last(host.messages, 'snapshot_request').payload.reason, 'replay_recovery');
  assert.equal(viewer.connection.member.waitingMinimum, 1);

  send(service, host.connection, envelope(hostCredentials.room, hostCredentials.client, 0, 1, 'snapshot', makeSnapshot()));
  const freshSnapshot = last(viewer.messages, 'snapshot');
  assert.equal(freshSnapshot.revision, 1);
  send(service, viewer.connection, envelope(viewerCredentials.room, viewerCredentials.client, 0, 1,
    'snapshot_ack', { revision: 1, sha256: freshSnapshot.payload.sha256 }));
  assert.equal(viewer.connection.member.syncing, false);
  assert.equal(last(viewer.messages, 'accepted'), undefined, 'the fresh checkpoint replaces the failing replay tail');
});

test('disconnecting the host rejects in-flight proposals and closes the room after grace', async () => {
  const { service, host, hostCredentials } = startRoom({ hostGraceMs: 15 });
  const editorCredentials = service.issueInvite(hostCredentials.token, 'editor');
  const editor = newClient(service, editorCredentials);
  acknowledgeSnapshot(service, editor);
  send(service, editor.connection,
    envelope(editorCredentials.room, editorCredentials.client, 1, 0, 'proposal', { family: 'text', op: 'edit' }));
  service.disconnect(host.connection, 'dropped');
  assert.equal(last(editor.messages, 'rejected').payload.reason, 'host_disconnected');
  await new Promise((resolve) => setTimeout(resolve, 30));
  assert.equal(service.getRoom(hostCredentials.room).closed, true);
  assert.equal(last(editor.messages, 'room_closed').payload.reason, 'host_disconnected');
});

test('host can reconnect inside grace and receives committed state', async () => {
  const { service, host, hostCredentials } = startRoom({ hostGraceMs: 40 });
  send(service, host.connection,
    envelope(hostCredentials.room, hostCredentials.client, 1, 0, 'accepted', { family: 'scene', op: 'rename' }));
  service.disconnect(host.connection, 'reload');
  const restoredHost = newClient(service, hostCredentials);
  assert.equal(service.getRoom(hostCredentials.room).closed, false);
  assert.equal(last(restoredHost.messages, 'joined').payload.next_seq, 2);
  assert.equal(last(restoredHost.messages, 'snapshot').revision, 0);
  assert.equal(last(restoredHost.messages, 'accepted'), undefined, 'tail waits for snapshot acknowledgement');
  acknowledgeSnapshot(service, restoredHost);
  assert.equal(last(restoredHost.messages, 'accepted').revision, 1);
  await new Promise((resolve) => setTimeout(resolve, 50));
  assert.equal(service.getRoom(hostCredentials.room).closed, false);
});

test('production configuration requires explicit origin, secret, admin, and WSS settings', () => {
  assert.throws(() => configFromEnvironment({ NODE_ENV: 'production' }), /COLLAB_ADMIN_TOKEN/);
  const base = {
    NODE_ENV: 'production',
    COLLAB_ADMIN_TOKEN: 'admin-token-that-is-long-enough-for-production',
    COLLAB_TOKEN_SECRET: 'token-secret-that-is-long-enough-for-production',
    COLLAB_ALLOWED_ORIGINS: 'https://editor.example',
    COLLAB_PUBLIC_WS_URL: 'wss://rooms.example',
  };
  const config = configFromEnvironment(base);
  assert.equal(config.production, true);
  assert.throws(() => configFromEnvironment({ ...base, COLLAB_PUBLIC_WS_URL: 'ws://rooms.example' }), /requires wss/);
});
