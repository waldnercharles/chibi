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
      exactKeys(envelope.payload, ['checkpoint_version', 'workspace', 'archive', 'sha256']);
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
  exactKeys(snapshot, ['checkpoint_version', 'workspace', 'archive', 'sha256']);
  if (snapshot.checkpoint_version !== 3 || !isRecord(snapshot.workspace) ||
      !isRecord(snapshot.archive) || !isSha256(snapshot.sha256)) {
    throw new ProtocolError('invalid_snapshot');
  }
  const workspace = snapshot.workspace;
  exactKeys(workspace, ['version', 'active_path', 'presentation']);
  if (workspace.version !== 3 || typeof workspace.active_path !== 'string' ||
      (workspace.active_path !== '' && !isSafeRelativePath(workspace.active_path)) ||
      !isRecord(workspace.presentation)) {
    throw new ProtocolError('invalid_workspace');
  }
  validateSafeTree(workspace);
  const workspaceBytes = Buffer.byteLength(JSON.stringify(workspace), 'utf8');
  if (workspaceBytes > MAX_CONTROL_MESSAGE_BYTES) throw new ProtocolError('snapshot_too_large');
  const archive = snapshot.archive;
  exactKeys(archive, ['files', 'bytes', 'sha256', 'attachments']);
  if (!Array.isArray(archive.attachments) || archive.attachments.length !== 1) throw new ProtocolError('invalid_snapshot_attachments');
  if (!Array.isArray(archive.files) || archive.files.length > MAX_FILE_COUNT ||
      !Number.isSafeInteger(archive.bytes) || archive.bytes < 0 || !isSha256(archive.sha256)) {
    throw new ProtocolError('invalid_snapshot_archive');
  }
  let previous = null;
  let fileBytes = 0;
  for (const file of archive.files) {
    if (!isRecord(file)) throw new ProtocolError('invalid_snapshot_file');
    exactKeys(file, ['path', 'base64', 'sha256']);
    if (typeof file.path !== 'string' || !isSafeRelativePath(file.path) ||
        typeof file.base64 !== 'string' || file.base64.length > Math.ceil(MAX_FILE_BYTES * 4 / 3) + 4 ||
        !isSha256(file.sha256)) {
      throw new ProtocolError('invalid_snapshot_file');
    }
    if (previous !== null && compareUtf8(previous, file.path) >= 0) {
      throw new ProtocolError('unsorted_snapshot_files');
    }
    const decoded = Buffer.from(file.base64, 'base64');
    if (decoded.toString('base64') !== file.base64) throw new ProtocolError('invalid_snapshot_file_encoding');
    if (decoded.length > MAX_FILE_BYTES) throw new ProtocolError('snapshot_file_too_large');
    fileBytes += decoded.length;
    if (fileBytes + workspaceBytes > MAX_SNAPSHOT_BYTES) throw new ProtocolError('snapshot_too_large');
    if (createHash('sha256').update(decoded).digest('hex') !== file.sha256) {
      throw new ProtocolError('snapshot_file_hash_mismatch');
    }
    previous = file.path;
  }
  const documents = archive.attachments[0];
  if (!isRecord(documents)) throw new ProtocolError('invalid_snapshot_documents');
  exactKeys(documents, ['name', 'base64', 'bytes', 'sha256']);
  if (documents.name !== 'workspace-documents-v1' || typeof documents.base64 !== 'string' ||
      documents.base64.length > Math.ceil(MAX_SNAPSHOT_BYTES * 4 / 3) + 4 ||
      !Number.isSafeInteger(documents.bytes) || documents.bytes < 36 || documents.bytes > MAX_SNAPSHOT_BYTES || !isSha256(documents.sha256)) {
    throw new ProtocolError('invalid_snapshot_documents');
  }
  const state = Buffer.from(documents.base64, 'base64');
  if (state.toString('base64') !== documents.base64 || state.length !== documents.bytes ||
      state.subarray(0, 8).toString('ascii') !== 'CHBIDOCS' || state.readUInt32LE(8) !== 1) {
    throw new ProtocolError('invalid_snapshot_documents_encoding');
  }
  if (createHash('sha256').update(state).digest('hex') !== documents.sha256) throw new ProtocolError('snapshot_documents_hash_mismatch');
  fileBytes += state.length;
  if (fileBytes + workspaceBytes > MAX_SNAPSHOT_BYTES) throw new ProtocolError('snapshot_too_large');
  if (fileBytes !== archive.bytes) throw new ProtocolError('snapshot_byte_count_mismatch');
  if (createHash('sha256').update(JSON.stringify({files: archive.files, attachments: archive.attachments}), 'utf8').digest('hex') !== archive.sha256) {
    throw new ProtocolError('snapshot_archive_hash_mismatch');
  }
  const hashInput = JSON.stringify({checkpoint_version: 3, workspace, archive});
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
  if (!isRecord(action) || !['graph', 'scene', 'text', 'document-edits', 'command'].includes(action.family)) {
    throw new ProtocolError('invalid_action');
  }
  if (Buffer.byteLength(JSON.stringify(action), 'utf8') > MAX_ACTION_BYTES) {
    throw new ProtocolError('action_too_large');
  }
  if (action.family === 'document-edits') {
    exactKeys(action, ['family', 'anchor', 'edits']);
    if (typeof action.anchor !== 'string' || !isSafeRelativePath(action.anchor) || !Array.isArray(action.edits) ||
        action.edits.length < 1 || action.edits.length > 8) throw new ProtocolError('invalid_document_edits');
    const paths = new Set();
    for (const edit of action.edits) {
      exactKeys(edit, ['path', 'text']);
      if (typeof edit.path !== 'string' || !isSafeRelativePath(edit.path) || typeof edit.text !== 'string' ||
          edit.text.includes('\0') || Buffer.from(edit.text, 'utf8').toString('utf8') !== edit.text ||
          Buffer.from(edit.path, 'utf8').toString('utf8') !== edit.path || paths.has(edit.path)) throw new ProtocolError('invalid_document_edits');
      paths.add(edit.path);
    }
    if (!paths.has(action.anchor)) throw new ProtocolError('invalid_document_edits');
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
