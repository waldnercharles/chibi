import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { editorCommand } from './editor-command.mjs';

async function temporaryDirectory() {
  return mkdtemp(join(tmpdir(), 'chibi-editor-api-'));
}

test('editor commands validate operations and forward browser requests', async () => {
  await assert.rejects(
    editorCommand({ operation: 'unknown' }),
    /Unknown editor operation/
  );

  const directory = await temporaryDirectory();
  const file = join(directory, 'connection.json');
  const requests = [];
  const previousFetch = globalThis.fetch;
  globalThis.fetch = async (url, options = {}) => {
    const address = String(url);
    requests.push({ address, options });
    if (address.endsWith('/__chibi/sessions')) {
      return { ok: true, json: async () => [{ session: 'one', active: true }] };
    }
    const packet = JSON.parse(options.body).packet;
    return { ok: true, json: async () => ({ status: 'applied', task_id: packet.request_id }) };
  };

  try {
    await writeFile(
      file,
      JSON.stringify({ url: 'http://127.0.0.1:8099', token: 'local-test-token' })
    );
    const lookup = await editorCommand({ connection_file: file, operation: 'inspect' });
    assert.equal(lookup.status, 'needs_clarification');
    assert.equal(lookup.sessions[0].session, 'one');

    const result = await editorCommand({
      connection_file: file,
      session: 'one',
      operation: 'submit',
      command: 'Open Marker',
    });
    assert.equal(result.status, 'applied');
    assert.match(result.task_id, /^[0-9a-f-]{36}$/);
    assert.equal(requests[1].address, 'http://127.0.0.1:8099/__chibi/editor');
    assert.equal(requests[1].options.headers['X-Chibi-Token'], 'local-test-token');
    assert.equal(JSON.parse(requests[1].options.body).packet.command, 'Open Marker');

    await writeFile(file, JSON.stringify({ url: 'https://example.com', token: 'test' }));
    await assert.rejects(
      editorCommand({ connection_file: file, session: 'one', operation: 'inspect' }),
      /loopback/
    );
  } finally {
    globalThis.fetch = previousFetch;
    await rm(directory, { recursive: true, force: true });
  }
});
