import { createHash } from 'node:crypto';

export const PROTOCOL_VERSION = 1;
export const MAX_SNAPSHOT_BYTES = 8 * 1024 * 1024;
export const MAX_FILE_COUNT = 4096;
export const MAX_FILE_BYTES = 2 * 1024 * 1024;
export const MAX_ACTION_BYTES = 16 * 1024;
export const MAX_WS_MESSAGE_BYTES = 24 * 1024 * 1024;
export const MAX_CONTROL_MESSAGE_BYTES = 64 * 1024;

const CLIENT_KINDS = new Set([
  'hello', 'join', 'leave', 'presence', 'snapshot_request', 'snapshot',
  'snapshot_ack', 'proposal', 'accepted', 'rejected', 'ack',
]);

export class ProtocolError extends Error {
  constructor(code, message = code) {
    super(message);
    this.name = 'ProtocolError';
    this.code = code;
  }
}

export function validateEnvelope(value, { client = true } = {}) {
  if (!isRecord(value)) throw new ProtocolError('invalid_envelope');
  const expected = ['protocol', 'room', 'client', 'seq', 'revision', 'kind', 'payload'];
  const keys = Object.keys(value);
  if (keys.length !== expected.length || expected.some((key) => !Object.hasOwn(value, key))) {
    throw new ProtocolError('invalid_envelope_keys');
  }
  if (value.protocol !== PROTOCOL_VERSION) throw new ProtocolError('unsupported_protocol');
  if (!isId(value.room) || !isId(value.client)) throw new ProtocolError('invalid_identity');
  if (!Number.isSafeInteger(value.seq) || value.seq < 0) throw new ProtocolError('invalid_sequence');
  if (!Number.isSafeInteger(value.revision) || value.revision < 0) throw new ProtocolError('invalid_revision');
  if (typeof value.kind !== 'string' || (client && !CLIENT_KINDS.has(value.kind))) {
    throw new ProtocolError('invalid_kind');
  }
  if (!isRecord(value.payload)) throw new ProtocolError('invalid_payload');
  validateSafeTree(value.payload);
  return value;
}

export function validateClientMessage(envelope) {
  switch (envelope.kind) {
    case 'hello':
      exactKeys(envelope.payload, ['token']);
      if (typeof envelope.payload.token !== 'string' || envelope.payload.token.length > 4096) {
        throw new ProtocolError('invalid_hello');
      }
      requireControl(envelope);
      return;
    case 'join':
      exactKeys(envelope.payload, ['last_revision']);
      if (!Number.isSafeInteger(envelope.payload.last_revision) || envelope.payload.last_revision < 0) {
        throw new ProtocolError('invalid_join');
      }
      requireControl(envelope);
      return;
    case 'leave':
      exactKeys(envelope.payload, []);
      requireControl(envelope);
      return;
    case 'ack':
      exactKeys(envelope.payload, []);
      requireControl(envelope);
      return;
    case 'presence':
      exactKeys(envelope.payload, ['presence_seq', 'data']);
      if (!Number.isSafeInteger(envelope.payload.presence_seq) || envelope.payload.presence_seq < 1) {
        throw new ProtocolError('invalid_presence');
      }
      if (!isRecord(envelope.payload.data)) throw new ProtocolError('invalid_presence');
      if (envelope.seq !== 0) throw new ProtocolError('invalid_presence_sequence');
      return;
    case 'snapshot_request':
      exactKeys(envelope.payload, ['last_revision']);
      if (!Number.isSafeInteger(envelope.payload.last_revision) || envelope.payload.last_revision < 0) {
        throw new ProtocolError('invalid_snapshot_request');
      }
      if (envelope.seq !== 0) throw new ProtocolError('invalid_control_sequence');
      return;
    case 'snapshot':
      exactKeys(envelope.payload, ['checkpoint_version', 'workspace', 'files', 'sha256']);
      validateSnapshot(envelope.payload);
      if (envelope.seq !== 0) throw new ProtocolError('invalid_snapshot_sequence');
      return;
    case 'snapshot_ack':
      exactKeys(envelope.payload, ['revision', 'sha256']);
      if (!Number.isSafeInteger(envelope.payload.revision) || envelope.payload.revision < 0 ||
          !isSha256(envelope.payload.sha256) || envelope.revision !== envelope.payload.revision ||
          envelope.seq !== 0) {
        throw new ProtocolError('invalid_snapshot_ack');
      }
      return;
    case 'proposal':
    case 'accepted':
      validateAction(envelope.payload);
      if (envelope.seq < 1) throw new ProtocolError('invalid_action_sequence');
      return;
    case 'rejected':
      exactKeys(envelope.payload, ['reason']);
      if (typeof envelope.payload.reason !== 'string' || envelope.payload.reason.length > 128 ||
          envelope.seq < 1) {
        throw new ProtocolError('invalid_rejection');
      }
      return;
    default:
      throw new ProtocolError('invalid_kind');
  }
}

export function validateSnapshot(snapshot) {
  if (snapshot.checkpoint_version !== 1 || typeof snapshot.workspace !== 'string' ||
      !Array.isArray(snapshot.files) || snapshot.files.length > MAX_FILE_COUNT ||
      !isSha256(snapshot.sha256)) {
    throw new ProtocolError('invalid_snapshot');
  }
  const workspaceBytes = Buffer.byteLength(snapshot.workspace, 'utf8');
  if (workspaceBytes > MAX_SNAPSHOT_BYTES) throw new ProtocolError('snapshot_too_large');
  try {
    const workspace = JSON.parse(snapshot.workspace);
    if (!isRecord(workspace) || workspace.version !== 1 || typeof workspace.project !== 'string' ||
        typeof workspace.scene !== 'string' || typeof workspace.graph !== 'string' ||
        typeof workspace.presentation !== 'string') {
      throw new ProtocolError('invalid_workspace');
    }
    JSON.parse(workspace.scene);
    JSON.parse(workspace.graph);
    JSON.parse(workspace.presentation);
  } catch (error) {
    if (error instanceof ProtocolError) throw error;
    throw new ProtocolError('invalid_workspace');
  }
  let previous = null;
  let fileBytes = 0;
  for (const file of snapshot.files) {
    if (!isRecord(file)) throw new ProtocolError('invalid_snapshot_file');
    exactKeys(file, ['path', 'data']);
    if (typeof file.path !== 'string' || !isSafeRelativePath(file.path) ||
        typeof file.data !== 'string' || file.data.length > Math.ceil(MAX_FILE_BYTES * 4 / 3) + 4) {
      throw new ProtocolError('invalid_snapshot_file');
    }
    if (previous !== null && compareUtf8(previous, file.path) >= 0) {
      throw new ProtocolError('unsorted_snapshot_files');
    }
    const decoded = Buffer.from(file.data, 'base64');
    if (decoded.toString('base64') !== file.data) throw new ProtocolError('invalid_snapshot_file_encoding');
    if (decoded.length > MAX_FILE_BYTES) throw new ProtocolError('snapshot_file_too_large');
    fileBytes += decoded.length;
    if (fileBytes + workspaceBytes > MAX_SNAPSHOT_BYTES) throw new ProtocolError('snapshot_too_large');
    previous = file.path;
  }
  const hashInput = JSON.stringify({
    checkpoint_version: snapshot.checkpoint_version,
    workspace: snapshot.workspace,
    files: snapshot.files,
  });
  const actual = createHash('sha256').update(hashInput, 'utf8').digest('hex');
  if (actual !== snapshot.sha256) throw new ProtocolError('snapshot_hash_mismatch');
}

export function envelope(room, client, seq, revision, kind, payload) {
  return { protocol: PROTOCOL_VERSION, room, client, seq, revision, kind, payload };
}

export function actionFingerprint(action, revision) {
  return createHash('sha256').update(JSON.stringify({ revision, action }), 'utf8').digest('hex');
}

export function isRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value) &&
    (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null);
}

function validateAction(action) {
  if (!isRecord(action) || !['graph', 'scene', 'text', 'command'].includes(action.family)) {
    throw new ProtocolError('invalid_action');
  }
  if (Buffer.byteLength(JSON.stringify(action), 'utf8') > MAX_ACTION_BYTES) {
    throw new ProtocolError('action_too_large');
  }
}

function exactKeys(value, keys) {
  if (!isRecord(value)) throw new ProtocolError('invalid_payload');
  const actual = Object.keys(value);
  if (actual.length !== keys.length || keys.some((key) => !Object.hasOwn(value, key))) {
    throw new ProtocolError('invalid_payload_keys');
  }
}

function requireControl(envelopeValue) {
  if (envelopeValue.seq !== 0) throw new ProtocolError('invalid_control_sequence');
}

function isId(value) {
  return typeof value === 'string' && /^[A-Za-z0-9_-]{1,96}$/.test(value);
}

function isSha256(value) {
  return typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
}

function isSafeRelativePath(value) {
  if (value.length === 0 || value.length > 512 || value.startsWith('/') || value.includes('\\')) return false;
  const parts = value.split('/');
  return parts.every((part) => part.length > 0 && part !== '.' && part !== '..' && !part.includes('\0'));
}

function compareUtf8(left, right) {
  return Buffer.compare(Buffer.from(left, 'utf8'), Buffer.from(right, 'utf8'));
}

function validateSafeTree(value, depth = 0) {
  if (depth > 32) throw new ProtocolError('payload_too_deep');
  if (Array.isArray(value)) {
    for (const item of value) validateSafeTree(item, depth + 1);
    return;
  }
  if (!isRecord(value)) return;
  for (const [key, item] of Object.entries(value)) {
    if (['__proto__', 'prototype', 'constructor'].includes(key)) throw new ProtocolError('unsafe_payload_key');
    validateSafeTree(item, depth + 1);
  }
}
