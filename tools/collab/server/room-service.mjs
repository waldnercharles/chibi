import {
  createHmac,
  randomBytes,
  timingSafeEqual,
} from 'node:crypto';
import {
  MAX_CONTROL_MESSAGE_BYTES,
  MAX_WS_MESSAGE_BYTES,
  ProtocolError,
  actionFingerprint,
  envelope,
  isRecord,
  validateClientMessage,
  validateEnvelope,
} from './protocol.mjs';

const DEFAULT_HOST_GRACE_MS = 30_000;
const DEFAULT_TOKEN_TTL_MS = 15 * 60 * 1000;
const DEFAULT_HOST_TOKEN_TTL_MS = 60 * 60 * 1000;
const MAX_PEERS_PER_ROOM = 32;
const MAX_PENDING_PER_ROOM = 32;
const MAX_ACTION_LOG = 512;
const MAX_ACTION_LOG_BYTES = 8 * 1024 * 1024;
const MAX_DECISIONS_PER_CLIENT = 8;
const MAX_ROOMS = 32;

export function createRoomService(options = {}) {
  const config = {
    secret: options.secret ?? randomBytes(32),
    adminToken: options.adminToken ?? null,
    hostGraceMs: options.hostGraceMs ?? DEFAULT_HOST_GRACE_MS,
    tokenTtlMs: options.tokenTtlMs ?? DEFAULT_TOKEN_TTL_MS,
    hostTokenTtlMs: options.hostTokenTtlMs ?? DEFAULT_HOST_TOKEN_TTL_MS,
    now: options.now ?? Date.now,
    onRoomClosed: options.onRoomClosed ?? (() => {}),
  };
  const rooms = new Map();

  function createRoom() {
    if (rooms.size >= MAX_ROOMS) throw new ProtocolError('room_capacity');
    const roomId = `room_${randomId(12)}`;
    const hostClient = `client_${randomId(12)}`;
    const room = {
      id: roomId,
      hostClient,
      revision: 0,
      clients: new Map(),
      presence: new Map(),
      sequences: new Map([[hostClient, newSequenceState()]]),
      pending: new Map(),
      log: [],
      logBytes: 0,
      checkpoint: null,
      hostGraceTimer: null,
      closed: false,
      closeTimer: null,
    };
    rooms.set(roomId, room);
    return {
      room: roomId,
      client: hostClient,
      role: 'host',
      token: makeToken(room, hostClient, 'host', config.hostTokenTtlMs),
      expires_at: Math.floor((config.now() + config.hostTokenTtlMs) / 1000),
    };
  }

  function issueInvite(hostToken, role, expectedRoom = null) {
    if (role !== 'editor' && role !== 'viewer') throw new ProtocolError('invalid_role');
    const claims = verifyToken(hostToken);
    if (claims.role !== 'host') throw new ProtocolError('forbidden');
    if (expectedRoom && claims.room !== expectedRoom) throw new ProtocolError('forbidden');
    const room = rooms.get(claims.room);
    if (!room || room.closed || claims.client !== room.hostClient) throw new ProtocolError('room_closed');
    const client = `client_${randomId(12)}`;
    room.sequences.set(client, newSequenceState());
    const expiresIn = config.tokenTtlMs;
    return {
      room: room.id,
      client,
      role,
      token: makeToken(room, client, role, expiresIn),
      expires_at: Math.floor((config.now() + expiresIn) / 1000),
    };
  }

  function authenticate(token, roomId, clientId) {
    const claims = verifyToken(token);
    if (claims.room !== roomId || claims.client !== clientId) throw new ProtocolError('invalid_credentials');
    const room = rooms.get(roomId);
    if (!room || room.closed || (claims.role === 'host' && claims.client !== room.hostClient)) {
      throw new ProtocolError('room_closed');
    }
    if (!room.sequences.has(clientId)) room.sequences.set(clientId, newSequenceState());
    return { room, client: clientId, role: claims.role };
  }

  function createConnection(io) {
    return {
      io,
      id: randomId(8),
      auth: null,
      member: null,
      joined: false,
      lastPresenceSeq: 0,
      messageTimes: [],
      presenceTimes: [],
      closed: false,
    };
  }

  function receive(connection, source) {
    if (connection.closed) return;
    let raw;
    try {
      raw = typeof source === 'string' ? source : source.toString('utf8');
      const byteLength = Buffer.byteLength(raw, 'utf8');
      if (byteLength > MAX_WS_MESSAGE_BYTES) throw new ProtocolError('message_too_large');
      rejectDuplicateKeys(raw);
      const value = JSON.parse(raw);
      validateEnvelope(value);
      if (value.kind !== 'snapshot' && byteLength > MAX_CONTROL_MESSAGE_BYTES) {
        throw new ProtocolError('message_too_large');
      }
      validateClientMessage(value);
      if (!takeMessageRate(connection, value.kind, config.now())) {
        throw new ProtocolError('rate_limited');
      }
      if (!connection.auth) {
        if (value.kind !== 'hello') throw new ProtocolError('hello_required');
        handleHello(connection, value);
        return;
      }
      if (!connection.joined) {
        if (value.kind !== 'join') throw new ProtocolError('join_required');
        handleJoin(connection, value);
        return;
      }
      if (value.room !== connection.auth.room.id) throw new ProtocolError('wrong_room');
      if (value.kind === 'hello' || value.kind === 'join') throw new ProtocolError('duplicate_handshake');
      handleJoinedMessage(connection, value);
    } catch (error) {
      const protocolError = error instanceof ProtocolError ? error : new ProtocolError('invalid_json');
      sendError(connection, protocolError.code);
      if (isFatal(protocolError.code)) closeConnection(connection, 1008, protocolError.code);
    }
  }

  function handleHello(connection, message) {
    const auth = authenticate(message.payload.token, message.room, message.client);
    connection.auth = auth;
  }

  function handleJoin(connection, message) {
    const { room, client, role } = connection.auth;
    if (message.room !== room.id || message.client !== client) throw new ProtocolError('identity_mismatch');
    if (room.closed) {
      send(connection, envelope(room.id, client, 0, room.revision, 'room_closed', { reason: 'closed' }));
      closeConnection(connection, 1000, 'room closed');
      return;
    }
    if (room.clients.size >= MAX_PEERS_PER_ROOM && !room.clients.has(client)) {
      throw new ProtocolError('room_capacity');
    }
    const existing = room.clients.get(client);
    if (existing && existing !== connection) closeConnection(existing, 4001, 'reconnected');
    connection.joined = true;
    connection.member = {
      room,
      client,
      role,
      syncedRevision: null,
      syncRevision: null,
      pendingSnapshots: new Map(),
      waitingMinimum: null,
      lastSentRevision: room.revision,
      ackedRevision: 0,
      syncing: true,
    };
    room.clients.set(client, connection);
    if (client === room.hostClient && room.hostGraceTimer) {
      clearTimeout(room.hostGraceTimer);
      room.hostGraceTimer = null;
      broadcastHostStatus(room, true);
    }
    const sequence = room.sequences.get(client) ?? newSequenceState();
    room.sequences.set(client, sequence);
    send(connection, envelope(room.id, client, 0, room.revision, 'joined', {
      role,
      host_client: room.hostClient,
      head_revision: room.revision,
      next_seq: sequence.next,
      peers: [...room.clients.entries()]
        .filter(([peerClient]) => peerClient !== client)
        .map(([peerClient, peer]) => ({
          client: peerClient,
          role: peer.member.role,
          name: peerClient.slice(-6),
          color: peerColor(peerClient),
        })),
      snapshot_revision: room.checkpoint?.revision ?? null,
    }));
    broadcastPresence(room, client, role, { online: true });
    if (room.checkpoint) {
      beginSync(connection, room.checkpoint);
      return;
    }
    if (client === room.hostClient) {
      send(connection, envelope(room.id, room.hostClient, 0, room.revision, 'snapshot_request', {
        last_revision: 0,
      }));
      return;
    }
    connection.member.waitingMinimum = 0;
    send(connection, envelope(room.id, client, 0, room.revision, 'resync_required', {
      reason: 'awaiting_initial_snapshot',
      revision: room.revision,
    }));
  }

  function handleJoinedMessage(connection, message) {
    const { room, client, role } = connection.member;
    switch (message.kind) {
      case 'leave':
        if (client === room.hostClient) closeRoom(room, 'host_left');
        else removeClient(connection, 'left');
        return;
      case 'presence':
        if (message.client !== client) throw new ProtocolError('identity_mismatch');
        if (message.payload.presence_seq <= connection.lastPresenceSeq) return;
        connection.lastPresenceSeq = message.payload.presence_seq;
        room.presence.set(client, message.payload.data);
        broadcast(room, envelope(room.id, client, 0, room.revision, 'presence', {
          presence_seq: message.payload.presence_seq,
          data: message.payload.data,
        }), client);
        return;
      case 'ack':
        if (message.client !== client || message.revision > room.revision ||
            message.revision > connection.member.lastSentRevision) {
          throw new ProtocolError('invalid_ack');
        }
        connection.member.ackedRevision = Math.max(connection.member.ackedRevision, message.revision);
        return;
      case 'proposal':
        handleProposal(connection, message);
        return;
      case 'accepted':
        if (client !== room.hostClient) throw new ProtocolError('host_authority_required');
        handleAccepted(connection, message);
        return;
      case 'rejected':
        if (client !== room.hostClient) throw new ProtocolError('host_authority_required');
        handleRejected(connection, message);
        return;
      case 'snapshot':
        if (client !== room.hostClient) throw new ProtocolError('host_authority_required');
        handleSnapshot(connection, message);
        return;
      case 'snapshot_ack':
        handleSnapshotAck(connection, message);
        return;
      case 'snapshot_request':
        if (message.client !== client) throw new ProtocolError('identity_mismatch');
        sendCheckpointOrRequest(connection, message.payload.last_revision);
        return;
      default:
        throw new ProtocolError('invalid_kind');
    }
  }

  function handleProposal(connection, message) {
    const { room, client, role } = connection.member;
    if (message.client !== client) throw new ProtocolError('identity_mismatch');
    if (role !== 'editor') {
      sendRejected(connection, message, room, 'read_only');
      return;
    }
    const sequence = room.sequences.get(client);
    const key = actionKey(client, message.seq);
    const fingerprint = actionFingerprint(message.payload, message.revision);
    const priorPending = room.pending.get(key);
    if (priorPending) {
      if (priorPending.fingerprint !== fingerprint) throw new ProtocolError('sequence_conflict');
      sendAck(connection, message, room, 'pending');
      return;
    }
    const decision = sequence.decisions.get(message.seq);
    if (decision) {
      if (decision.fingerprint !== fingerprint) throw new ProtocolError('sequence_conflict');
      send(connection, decision.envelope);
      return;
    }
    if (message.seq < sequence.next) {
      send(connection, envelope(room.id, client, message.seq, room.revision, 'resync_required', {
        reason: 'decision_expired',
        expected_seq: sequence.next,
        revision: room.revision,
      }));
      return;
    }
    if (message.seq !== sequence.next) {
      sendRejected(connection, message, room, 'sequence_gap', sequence.next, false);
      return;
    }
    if (!room.clients.has(room.hostClient)) {
      sendRejected(connection, message, room, 'host_unavailable');
      return;
    }
    if (room.pending.size >= MAX_PENDING_PER_ROOM) {
      sendRejected(connection, message, room, 'room_busy');
      return;
    }
    if (message.revision !== room.revision) {
      sendRejected(connection, message, room, 'stale_revision', sequence.next + 1);
      return;
    }
    sequence.next++;
    room.pending.set(key, {
      client,
      seq: message.seq,
      baseRevision: message.revision,
      action: message.payload,
      fingerprint,
      connection,
    });
    sendAck(connection, message, room, 'pending');
    const host = room.clients.get(room.hostClient);
    send(host, message);
  }

  function handleAccepted(connection, message) {
    const room = connection.member.room;
    const key = actionKey(message.client, message.seq);
    const hostSequence = room.sequences.get(room.hostClient);
    const decisionSequence = room.sequences.get(message.client);
    const fingerprint = actionFingerprint(message.payload, message.revision);
    const prior = decisionSequence?.decisions.get(message.seq);
    if (prior) {
      if (prior.fingerprint !== fingerprint) throw new ProtocolError('sequence_conflict');
      send(connection, prior.envelope);
      return;
    }
    let pending = room.pending.get(key);
    const localHostAction = message.client === room.hostClient;
    if (!localHostAction && !pending) throw new ProtocolError('unknown_proposal');
    if (localHostAction && message.seq !== hostSequence.next) {
      throw new ProtocolError(message.seq > hostSequence.next ? 'sequence_gap' : 'sequence_conflict');
    }
    if (pending && pending.baseRevision !== message.revision) throw new ProtocolError('proposal_revision_mismatch');
    if (message.revision !== room.revision) {
      if (pending) {
        room.pending.delete(key);
        storeDecision(room, pending.client, pending.seq, pending.fingerprint,
          rejectedEnvelope(room, pending.client, pending.seq, 'stale_revision'));
      }
      sendResync(connection, room, 'stale_host_revision');
      return;
    }
    const acceptedBytes = Buffer.byteLength(JSON.stringify(envelope(
      room.id, message.client, message.seq, room.revision + 1, 'accepted', message.payload,
    )), 'utf8');
    if ((room.log.length >= MAX_ACTION_LOG || room.logBytes + acceptedBytes > MAX_ACTION_LOG_BYTES) &&
        room.log[0]?.revision > (room.checkpoint?.revision ?? 0)) {
      if (pending) {
        room.pending.delete(key);
        const rejected = rejectedEnvelope(room, pending.client, pending.seq, 'checkpoint_required');
        storeDecision(room, pending.client, pending.seq, pending.fingerprint, rejected);
        const proposalClient = room.clients.get(pending.client);
        if (proposalClient) send(proposalClient, rejected);
        return;
      }
      sendResync(connection, room, 'checkpoint_required');
      return;
    }
    if (localHostAction) hostSequence.next++;
    room.pending.delete(key);
    const actorSequence = room.sequences.get(message.client) ?? newSequenceState();
    room.sequences.set(message.client, actorSequence);
    if (!localHostAction && actorSequence.next <= message.seq) actorSequence.next = message.seq + 1;
    room.revision++;
    const accepted = envelope(room.id, message.client, message.seq, room.revision, 'accepted', message.payload);
    const actionDecision = { fingerprint, envelope: accepted };
    rememberDecision(actorSequence, message.seq, actionDecision);
    room.log.push(accepted);
    room.logBytes += Buffer.byteLength(JSON.stringify(accepted), 'utf8');
    broadcast(room, accepted);
  }

  function handleRejected(connection, message) {
    const room = connection.member.room;
    const key = actionKey(message.client, message.seq);
    const pending = room.pending.get(key);
    if (!pending || pending.baseRevision !== message.revision) throw new ProtocolError('unknown_proposal');
    room.pending.delete(key);
    const result = rejectedEnvelope(room, pending.client, pending.seq, message.payload.reason);
    storeDecision(room, pending.client, pending.seq, pending.fingerprint, result);
    const proposalClient = room.clients.get(pending.client);
    if (proposalClient) send(proposalClient, result);
  }

  function handleSnapshot(connection, message) {
    const room = connection.member.room;
    if (message.client !== room.hostClient) throw new ProtocolError('identity_mismatch');
    if (message.revision !== room.revision || room.pending.size > 0) {
      sendResync(connection, room, room.pending.size > 0 ? 'pending_actions' : 'stale_snapshot');
      return;
    }
    const checkpoint = {
      revision: room.revision,
      checkpointVersion: message.payload.checkpoint_version,
      workspace: message.payload.workspace,
      files: message.payload.files,
      sha256: message.payload.sha256,
    };
    room.checkpoint = checkpoint;
    room.log = room.log.filter((action) => action.revision > checkpoint.revision);
    room.logBytes = room.log.reduce((size, action) => size + Buffer.byteLength(JSON.stringify(action), 'utf8'), 0);
    connection.member.syncing = false;
    connection.member.syncedRevision = room.revision;
    connection.member.syncRevision = null;
    connection.member.lastSentRevision = room.revision;
    send(connection, envelope(room.id, room.hostClient, 0, room.revision, 'snapshot_ack', {
      revision: room.revision,
      sha256: checkpoint.sha256,
    }));
    for (const peer of room.clients.values()) {
      if (peer === connection) continue;
      if (peer.member.waitingMinimum !== null) {
        if (checkpoint.revision >= peer.member.waitingMinimum) beginSync(peer, checkpoint);
        continue;
      }
      if (peer.member.syncing) beginSync(peer, checkpoint);
    }
  }

  function handleSnapshotAck(connection, message) {
    const { room, client } = connection.member;
    const member = connection.member;
    if (message.client !== client) throw new ProtocolError('identity_mismatch');
    const sentHash = member.pendingSnapshots.get(message.payload.revision);
    if (sentHash !== message.payload.sha256) {
      sendResync(connection, room, 'snapshot_ack_mismatch');
      return;
    }
    member.pendingSnapshots.delete(message.payload.revision);
    if (!room.checkpoint || message.payload.revision !== room.checkpoint.revision ||
        message.payload.sha256 !== room.checkpoint.sha256) return;
    member.pendingSnapshots.clear();
    member.syncedRevision = message.payload.revision;
    member.syncRevision = null;
    const tail = room.log.filter((action) => action.revision > member.syncedRevision);
    for (const action of tail) {
      send(connection, action);
      member.lastSentRevision = action.revision;
    }
    member.syncedRevision = room.revision;
    member.syncing = false;
  }

  function sendCheckpointOrRequest(connection, minimumRevision) {
    const room = connection.member.room;
    if (minimumRevision > room.revision) {
      sendResync(connection, room, 'revision_ahead');
      return;
    }
    if (room.checkpoint && room.checkpoint.revision >= minimumRevision) {
      beginSync(connection, room.checkpoint);
      return;
    }
    if (connection.member.client === room.hostClient) {
      send(connection, envelope(room.id, room.hostClient, 0, room.revision, 'snapshot_request', {
        last_revision: minimumRevision,
        reason: room.checkpoint ? 'checkpoint_refresh' : 'initial_snapshot',
      }));
      return;
    }
    connection.member.waitingMinimum = minimumRevision;
    const host = room.clients.get(room.hostClient);
    if (host) {
      send(host, envelope(room.id, room.hostClient, 0, room.revision, 'snapshot_request', {
        last_revision: minimumRevision,
        reason: 'replay_recovery',
      }));
    }
    sendResync(connection, room, host ? 'waiting_for_checkpoint' : 'host_unavailable');
  }

  function beginSync(connection, checkpoint) {
    connection.member.syncRevision = checkpoint.revision;
    connection.member.syncedRevision = null;
    connection.member.syncing = true;
    connection.member.waitingMinimum = null;
    connection.member.lastSentRevision = checkpoint.revision;
    sendSnapshot(connection, checkpoint);
  }

  function sendSnapshot(connection, checkpoint) {
    const member = connection.member;
    member.pendingSnapshots.set(checkpoint.revision, checkpoint.sha256);
    while (member.pendingSnapshots.size > 8) {
      member.pendingSnapshots.delete(member.pendingSnapshots.keys().next().value);
    }
    send(connection, envelope(connection.member.room.id, connection.member.room.hostClient, 0,
      checkpoint.revision, 'snapshot', {
        checkpoint_version: checkpoint.checkpointVersion,
        workspace: checkpoint.workspace,
        files: checkpoint.files,
        sha256: checkpoint.sha256,
      }));
  }

  function sendAck(connection, message, room, status) {
    send(connection, envelope(room.id, message.client, message.seq, room.revision, 'ack', { status }));
  }

  function sendRejected(connection, message, room, reason, expectedSeq, consume = true) {
    const result = rejectedEnvelope(room, message.client, message.seq, reason, expectedSeq);
    const sequence = room.sequences.get(message.client);
    if (consume && sequence && message.seq === sequence.next) {
      sequence.next++;
      rememberDecision(sequence, message.seq, {
        fingerprint: actionFingerprint(message.payload, message.revision),
        envelope: result,
      });
    }
    send(connection, result);
  }

  function rejectedEnvelope(room, client, seq, reason, expectedSeq) {
    return envelope(room.id, client, seq, room.revision, 'rejected', {
      reason,
      expected_revision: room.revision,
      expected_seq: expectedSeq ?? room.sequences.get(client)?.next ?? 1,
    });
  }

  function storeDecision(room, client, seq, fingerprint, result) {
    const sequence = room.sequences.get(client) ?? newSequenceState();
    room.sequences.set(client, sequence);
    if (sequence.next <= seq) sequence.next = seq + 1;
    rememberDecision(sequence, seq, { fingerprint, envelope: result });
  }

  function sendResync(connection, room, reason) {
    send(connection, envelope(room.id, connection.member.client, 0, room.revision, 'resync_required', {
      reason,
      revision: room.revision,
    }));
  }

  function broadcastHostStatus(room, online) {
    broadcast(room, envelope(room.id, room.hostClient, 0, room.revision, 'host_changed', {
      host_client: room.hostClient,
      online,
    }));
  }

  function broadcastPresence(room, client, role, state) {
    const peer = room.clients.get(client);
    broadcast(room, envelope(room.id, client, 0, room.revision, 'presence', {
      presence_seq: 0,
      role: peer?.member.role ?? role,
      name: client.slice(-6),
      color: peerColor(client),
      data: state,
    }), client);
  }

  function broadcast(room, message, exceptClient = null) {
    for (const [client, peer] of room.clients) {
      if (client === exceptClient || peer.closed) continue;
      if (peer.member?.syncing && message.kind === 'accepted') continue;
      send(peer, message);
      if (message.kind === 'accepted') peer.member.lastSentRevision = message.revision;
    }
  }

  function removeClient(connection, reason) {
    const member = connection.member;
    if (!member) return;
    const { room, client } = member;
    if (room.clients.get(client) === connection) room.clients.delete(client);
    room.presence.delete(client);
    connection.joined = false;
    connection.member = null;
    broadcastPresence(room, client, member.role, { online: false, reason });
    if (client === room.hostClient && !room.closed) {
      rejectPendingForHostLoss(room);
      broadcastHostStatus(room, false);
      if (room.hostGraceTimer) clearTimeout(room.hostGraceTimer);
      room.hostGraceTimer = setTimeout(() => closeRoom(room, 'host_disconnected'), config.hostGraceMs);
      room.hostGraceTimer.unref?.();
    }
  }

  function rejectPendingForHostLoss(room) {
    for (const [key, pending] of room.pending) {
      room.pending.delete(key);
      const result = rejectedEnvelope(room, pending.client, pending.seq, 'host_disconnected');
      storeDecision(room, pending.client, pending.seq, pending.fingerprint, result);
      const peer = room.clients.get(pending.client);
      if (peer) send(peer, result);
    }
  }

  function closeRoom(room, reason) {
    if (room.closed) return;
    room.closed = true;
    if (room.hostGraceTimer) clearTimeout(room.hostGraceTimer);
    room.hostGraceTimer = null;
    const notice = envelope(room.id, room.hostClient, 0, room.revision, 'room_closed', { reason });
    broadcast(room, notice);
    config.onRoomClosed(room.id, reason);
    for (const peer of [...room.clients.values()]) closeConnection(peer, 1000, reason);
    room.clients.clear();
    room.closeTimer = setTimeout(() => rooms.delete(room.id), 60_000);
    room.closeTimer.unref?.();
  }

  function closeConnection(connection, code, reason) {
    if (connection.closed) return;
    connection.closed = true;
    const wasHost = connection.member?.client === connection.member?.room.hostClient;
    removeClient(connection, reason);
    try { connection.io.close(code, reason); } catch {}
    if (wasHost && connection.member) connection.member = null;
  }

  function sendError(connection, code) {
    const roomId = connection.auth?.room.id ?? 'server';
    const clientId = connection.auth?.client ?? 'server';
    try {
      send(connection, envelope(roomId, clientId, 0, connection.auth?.room.revision ?? 0, 'error', { code }));
    } catch {}
  }

  function send(connection, message) {
    if (connection.closed) return;
    connection.io.send(message);
  }

  function takeMessageRate(connection, kind, now) {
    const window = kind === 'presence' ? 1000 : 60_000;
    const limit = kind === 'presence' ? 30 : 120;
    const times = kind === 'presence' ? connection.presenceTimes : connection.messageTimes;
    while (times.length && now - times[0] >= window) times.shift();
    if (times.length >= limit) return false;
    times.push(now);
    return true;
  }

  function makeToken(room, client, role, ttlMs) {
    const claims = {
      iss: 'chibi-collab-v1',
      room: room.id,
      client,
      role,
      exp: Math.floor((config.now() + ttlMs) / 1000),
      nonce: randomId(12),
    };
    const body = Buffer.from(JSON.stringify(claims)).toString('base64url');
    return `${body}.${sign(body)}`;
  }

  function verifyToken(token) {
    if (typeof token !== 'string' || token.length > 4096) throw new ProtocolError('invalid_credentials');
    const parts = token.split('.');
    if (parts.length !== 2) throw new ProtocolError('invalid_credentials');
    const [body, signature] = parts;
    const expected = sign(body);
    if (signature.length !== expected.length || !timingSafeEqual(Buffer.from(signature), Buffer.from(expected))) {
      throw new ProtocolError('invalid_credentials');
    }
    let claims;
    try { claims = JSON.parse(Buffer.from(body, 'base64url').toString('utf8')); }
    catch { throw new ProtocolError('invalid_credentials'); }
    if (!isRecord(claims) || claims.iss !== 'chibi-collab-v1' ||
        !['host', 'editor', 'viewer'].includes(claims.role) ||
        typeof claims.room !== 'string' || typeof claims.client !== 'string' ||
        !Number.isSafeInteger(claims.exp) || claims.exp * 1000 <= config.now()) {
      throw new ProtocolError('invalid_credentials');
    }
    return claims;
  }

  function sign(body) {
    return createHmac('sha256', config.secret).update(body).digest('base64url');
  }

  function getRoom(roomId) {
    const room = rooms.get(roomId);
    if (!room) return null;
    return {
      id: room.id,
      hostClient: room.hostClient,
      revision: room.revision,
      closed: room.closed,
      peers: room.clients.size,
      snapshotRevision: room.checkpoint?.revision ?? null,
    };
  }

  function closeAll(reason = 'server_shutdown') {
    for (const room of rooms.values()) closeRoom(room, reason);
  }

  return {
    authenticate,
    createConnection,
    createRoom,
    getRoom,
    issueInvite,
    receive,
    closeAll,
    disconnect(connection, reason = 'disconnected') {
      if (!connection.closed) closeConnection(connection, 1001, reason);
    },
  };
}

function newSequenceState() {
  return { next: 1, decisions: new Map() };
}

function rememberDecision(sequence, seq, decision) {
  sequence.decisions.set(seq, decision);
  while (sequence.decisions.size > MAX_DECISIONS_PER_CLIENT) {
    sequence.decisions.delete(sequence.decisions.keys().next().value);
  }
}

function actionKey(client, seq) {
  return `${client}:${seq}`;
}

function randomId(bytes) {
  return randomBytes(bytes).toString('base64url');
}

function peerColor(client) {
  let hash = 2166136261;
  for (let index = 0; index < client.length; index++) {
    hash ^= client.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return `hsl(${(hash >>> 0) % 360} 68% 62%)`;
}

function isFatal(code) {
  return !['sequence_gap', 'sequence_conflict', 'stale_revision', 'rate_limited', 'room_busy',
    'host_unavailable', 'checkpoint_required', 'read_only', 'unknown_proposal',
    'proposal_revision_mismatch'].includes(code);
}

function rejectDuplicateKeys(source) {
  const stack = [];
  let index = 0;
  while (index < source.length) {
    const char = source[index];
    if (/\s/.test(char)) { index++; continue; }
    const parent = stack.at(-1);
    if (char === '{' || char === '[') {
      if (parent && (parent.type === 'array' || parent.state === 'value')) parent.state = 'comma';
      stack.push(char === '{' ? { type: 'object', state: 'key', keys: new Set() } : { type: 'array', state: 'value' });
      index++;
      continue;
    }
    if (char === '}' || char === ']') { stack.pop(); index++; continue; }
    if (char === ',') {
      if (parent) parent.state = parent.type === 'object' ? 'key' : 'value';
      index++;
      continue;
    }
    if (char === ':') {
      if (parent?.type === 'object') parent.state = 'value';
      index++;
      continue;
    }
    if (char === '"') {
      let end = index + 1;
      for (; end < source.length; end++) {
        if (source[end] === '\\') { end++; continue; }
        if (source[end] === '"') break;
      }
      if (end >= source.length) return;
      const stringToken = source.slice(index, end + 1);
      if (parent?.type === 'object' && parent.state === 'key') {
        const key = JSON.parse(stringToken);
        if (parent.keys.has(key)) throw new ProtocolError('duplicate_json_key');
        parent.keys.add(key);
        parent.state = 'colon';
      } else if (parent) {
        parent.state = 'comma';
      }
      index = end + 1;
      continue;
    }
    if (parent) parent.state = 'comma';
    while (index < source.length && !/[\s,}\]:]/.test(source[index])) index++;
  }
}
