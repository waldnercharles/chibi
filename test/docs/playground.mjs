import assert from 'node:assert/strict';
import test from 'node:test';
import { FormatterClient } from '../../tools/docs/playground/client.mjs';
import { decodeResult, encodeRequest, sourceUrl } from '../../tools/docs/playground/protocol.mjs';

const encoder = new TextEncoder();
function frame(status, parts) {
  return encoder.encode(`${status}\n${parts.map((part) => encoder.encode(part).length).join(' ')}\n${parts.join('')}`);
}
const result = (text) => ({ ok: true, text, original_html: text, formatted_html: text });

class FakeWorker {
  messages = [];
  terminated = false;
  postMessage(message) { this.messages.push(message); }
  terminate() { this.terminated = true; }
  answer(value) {
    this.onmessage({ data: { type: 'result', id: this.messages.at(-1).id, result: value } });
  }
}

test('UTF-8 framing retains NULs and newlines and counts bytes', () => {
  const parts = ['é\0\n漢', '<span>é\0\n漢</span>', '<span>漢</span>'];
  assert.deepEqual(decodeResult(frame('ok', parts)), {
    ok: true, text: parts[0], original_html: parts[1], formatted_html: parts[2],
  });
  assert.deepEqual(decodeResult(frame('error', ['line 2: invalid source', 'original', ''])), {
    ok: false, error: 'line 2: invalid source', original_html: 'original',
  });
  assert.equal(new TextDecoder().decode(encodeRequest('é\0\n', { indent_width: 2, use_tabs: false })),
    'indent_width=2 use_tabs=false\né\0\n');
});

test('truncated, oversized, extra and invalid UTF-8 results are rejected', () => {
  for (const bytes of [
    encoder.encode('ok\n3 0 0\nxx'),
    encoder.encode('ok\n9007199254740992 0 0\n'),
    encoder.encode('ok\n0 0 0\nx'),
    encoder.encode('unknown\n0 0 0\n'),
    encoder.encode('ok\n1 -1 0\n'),
    new Uint8Array([...encoder.encode('ok\n1 0 0\n'), 0xff]),
  ]) assert.throws(() => decodeResult(bytes));
});

test('analysis notes retain UTF-8 text independently of formatted source', () => {
  const parts = ['package p\n', 'original', 'formatted', 'Kept café: its declared type changes inference.\n'];
  assert.deepEqual(decodeResult(frame('ok', parts)), {
    ok: true, text: parts[0], original_html: parts[1], formatted_html: parts[2], notes: parts[3],
  });
  assert.throws(() => decodeResult(frame('ok', [...parts, 'extra'])), /response header/);
});

test('source limits use UTF-8 bytes and options cannot insert a framing line', () => {
  assert.equal(encodeRequest('é'.repeat(200000), {}).length, 400001);
  assert.throws(() => encodeRequest('é'.repeat(200001), {}), /UTF-8 bytes/);
  assert.throws(() => encodeRequest('source', { indent_width: '2\nsource' }), /Invalid formatter option/);
  assert.throws(() => encodeRequest('source', { ['bad\nkey']: 2 }), /Invalid formatter option/);
  assert.throws(() => encodeRequest('source', { preferred_width: NaN }), /Invalid formatter option/);
});

test('static source URLs retain a Pages prefix and cannot escape it', () => {
  assert.equal(sourceUrl('tools/a #é.chibi', 'https://example.github.io/chibi/formatter/').href,
    'https://example.github.io/chibi/formatter/sources/tools/a%20%23%C3%A9.chibi');
  for (const path of ['../outside', '/outside', 'tools/../outside', 'tools//file', 'tools\\file', 'bad\0file']) {
    assert.throws(() => sourceUrl(path, 'https://example.github.io/chibi/formatter/'));
  }
});

test('rapid changes run only the current and latest queued requests', async () => {
  const worker = new FakeWorker();
  const client = new FormatterClient({ makeWorker: () => worker });
  const first = client.format('first', {});
  const skipped = client.format('second', {});
  const skippedRejection = assert.rejects(skipped, { name: 'AbortError' });
  const last = client.format('third', {});
  assert.equal(worker.messages.length, 1);
  worker.answer(result('first'));
  assert.equal(worker.messages.length, 2);
  assert.equal(new TextDecoder().decode(worker.messages[1].bytes), '\nthird');
  worker.answer(result('third'));
  assert.equal((await first).text, 'first');
  assert.equal((await last).text, 'third');
  await skippedRejection;
});

test('worker failure rejects active and queued work, and a retry creates a worker', async () => {
  const workers = [];
  const client = new FormatterClient({ makeWorker: () => { const worker = new FakeWorker(); workers.push(worker); return worker; } });
  const first = assert.rejects(client.format('first', {}), /worker could not run/);
  const waiting = assert.rejects(client.format('last', {}), /worker could not run/);
  workers[0].onerror({ message: 'worker unavailable' });
  await Promise.all([first, waiting]);
  assert.equal(workers[0].terminated, true);
  const retried = client.format('retry', {});
  assert.equal(workers.length, 2);
  workers[0].answer(result('stale'));
  workers[1].answer(result('retry'));
  assert.equal((await retried).text, 'retry');
});

test('timeout settles every pending request and terminates the worker', async () => {
  const worker = new FakeWorker();
  const client = new FormatterClient({ makeWorker: () => worker, timeoutMs: 15 });
  await Promise.all([
    assert.rejects(client.format('first', {}), /did not answer/),
    assert.rejects(client.format('waiting', {}), /did not answer/),
  ]);
  assert.equal(worker.terminated, true);
});

test('malformed worker replies and runtime errors fail the queue', async () => {
  for (const payload of [{ type: 'result', result: { ok: true } },
    { type: 'result', result: { ...result('source'), notes: 1 } }, { type: 'error', error: 'quota exceeded' }]) {
    const worker = new FakeWorker();
    const client = new FormatterClient({ makeWorker: () => worker });
    const rejected = assert.rejects(client.format('source', {}), /Invalid formatter worker response|quota exceeded/);
    worker.onmessage({ data: { id: worker.messages[0].id, ...payload } });
    await rejected;
    assert.equal(worker.terminated, true);
  }
});

test('source rejection keeps the worker available for the corrected source', async () => {
  const worker = new FakeWorker();
  const client = new FormatterClient({ makeWorker: () => worker });
  const invalid = client.format('invalid', {});
  worker.answer({ ok: false, error: 'invalid source', original_html: 'invalid' });
  assert.equal((await invalid).ok, false);
  assert.equal(worker.terminated, false);
  const corrected = client.format('corrected', {});
  worker.answer(result('corrected'));
  assert.equal((await corrected).text, 'corrected');
});
