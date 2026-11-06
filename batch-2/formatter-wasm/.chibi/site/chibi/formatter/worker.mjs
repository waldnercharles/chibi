import createModule from './wasm/engine.mjs';
import { decodeResult } from './protocol.mjs';

let module;
function engine() {
  if (!module) module = createModule({ locateFile: (name) => new URL('./wasm/' + name, import.meta.url).href });
  return module;
}

function runtimeError(engine) {
  const pointer = engine._chibi_formatter_error() >>> 0;
  const heap = engine.HEAPU8;
  if (!pointer || pointer >= heap.length) return 'The formatter could not complete this request';
  const end = heap.indexOf(0, pointer);
  return new TextDecoder('utf-8', { fatal: true }).decode(heap.subarray(pointer, end < 0 ? heap.length : end));
}

self.onmessage = async (event) => {
  const { type, id, bytes } = event.data;
  if (type !== 'format' || !Number.isSafeInteger(id) || !(bytes instanceof ArrayBuffer)) return;
  let instance, pointer = 0;
  try {
    instance = await engine();
    const input = new Uint8Array(bytes);
    pointer = instance._malloc(input.length);
    if (!pointer) throw new Error('The formatter could not allocate its input');
    instance.HEAPU8.set(input, pointer);
    const status = instance._chibi_formatter_format(pointer, input.length);
    if (status !== 0) throw new Error(runtimeError(instance));
    const result = instance._chibi_formatter_result() >>> 0;
    const length = instance._chibi_formatter_result_length() >>> 0;
    const heap = instance.HEAPU8;
    if (!result || result > heap.length || length > heap.length - result) throw new Error('Invalid formatter result buffer');
    // Decode now, while this call still owns the result's bytes.
    self.postMessage({ type: 'result', id, result: decodeResult(heap.subarray(result, result + length)) });
  } catch (error) {
    self.postMessage({ type: 'error', id, error: String(error.message || error) });
  } finally {
    if (pointer) instance._free(pointer);
  }
};
