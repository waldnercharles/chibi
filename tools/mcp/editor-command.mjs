import net from 'node:net';
import { readFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';

const operations = ['inspect', 'resolve_objects', 'submit', 'status', 'respond', 'cancel'];

export const editorCommandDefinition = {
  name: 'editorCommand',
  title: 'Run a command in a live editor',
  annotations: { readOnlyHint: false, idempotentHint: false, destructiveHint: false, openWorldHint: false },
  description: 'Inspect, resolve names, submit natural language or typed operations, poll, reply, and cancel in a running editor. Uses the same revision-checked transaction and Undo path as the Studio command palette. Specify a native socket (CHIBI_EDITOR_SOCKET) or a browser connection file plus session. Inspect first for typed operations and pass its revision. A submit returns a task ID; poll status while it is pending, until applied, failed, ready (dry run), needs_review, or needs_clarification. Reuse request_id after a timeout. Never substitute offline file edits for live commands.',
  outputSchema: { type: 'object', additionalProperties: true },
  inputSchema: {
    type: 'object',
    required: ['operation'],
    properties: {
      socket: { type: 'string' },
      connection_file: { type: 'string' },
      session: { type: 'string' },
      operation: { type: 'string', enum: operations },
      command: { type: 'string', maxLength: 1024 },
      query: { type: 'string' },
      kinds: { type: 'array', items: { type: 'string' } },
      operations: { type: 'array', items: { type: 'object' } },
      revision: { type: 'string' },
      dry_run: { type: 'boolean' },
      request_id: { type: 'string' },
      task_id: { type: 'string' },
      approve: { type: 'boolean' },
      value: { type: 'string' },
      undo: { type: 'boolean' },
      feedback: { type: 'string', enum: ['incorrect'] },
    },
    additionalProperties: false,
  },
};

async function socketRequest(path, packet) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(path);
    let text = '';
    const fail = (error) => {
      socket.destroy();
      reject(error);
    };
    socket.setTimeout(15000, () => fail(new Error(`Editor timeout; poll with task_id ${packet.request_id || packet.task_id || ''}`)));
    socket.once('error', fail);
    socket.once('connect', () => socket.write(JSON.stringify(packet) + '\n'));
    socket.on('data', (chunk) => {
      text += chunk;
      if (Buffer.byteLength(text) > 16 * 1024 * 1024) return fail(new Error('Editor response too large'));
      const end = text.indexOf('\n');
      if (end < 0) return;
      try {
        const result = JSON.parse(text.slice(0, end));
        socket.destroy();
        resolve(result);
      } catch (error) {
        fail(error);
      }
    });
    socket.on('end', () => {
      if (!text.includes('\n')) fail(new Error('Editor disconnected'));
    });
  });
}

async function browserRequest(connectionFile, session, packet) {
  const config = JSON.parse(await readFile(connectionFile, 'utf8'));
  const url = new URL(config.url);
  if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || url.username || url.password) {
    throw new Error('Browser bridge must be on local loopback');
  }
  const headers = { 'Content-Type': 'application/json', 'X-Chibi-Token': config.token };
  if (!session) {
    const response = await fetch(new URL('/__chibi/sessions', url), { headers, signal: AbortSignal.timeout(15000) });
    if (!response.ok) throw new Error(`Session lookup returned HTTP ${response.status}`);
    return { ok: false, status: 'needs_clarification', message: 'Choose a live browser session', sessions: await response.json() };
  }
  const response = await fetch(new URL('/__chibi/editor', url), {
    method: 'POST',
    headers,
    body: JSON.stringify({ session, packet }),
    signal: AbortSignal.timeout(15000),
  });
  if (!response.ok) {
    throw new Error(`Live editor returned HTTP ${response.status}; poll with task_id ${packet.request_id || packet.task_id || ''}`);
  }
  return response.json();
}

export async function editorCommand(args) {
  const { socket, connection_file: connectionFile, session, ...packet } = args;
  if (!operations.includes(packet.operation)) throw new Error('Unknown editor operation');
  if (packet.operation === 'submit' && !packet.request_id) packet.request_id = randomUUID();
  const native = socket || (!connectionFile ? process.env.CHIBI_EDITOR_SOCKET : undefined);
  if (native && !connectionFile) return socketRequest(native, packet);
  if (!connectionFile || native) throw new Error('Choose one native socket or browser connection file');
  return browserRequest(connectionFile, session, packet);
}
