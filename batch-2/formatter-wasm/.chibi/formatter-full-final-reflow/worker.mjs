import createModule from './wasm/engine.mjs';
import { contextPath, decodeRequest, decodeResult, encodeImports } from './protocol.mjs';
import { ContextUnavailable, needsProjectContext, ProjectContext } from './project-context.mjs';

let module;
let projects;
let activeContext = null;
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

function invoke(instance, input) {
  const pointer = instance._malloc(input.length);
  if (!pointer) throw new Error('The formatter could not allocate its input');
  try {
    instance.HEAPU8.set(input, pointer);
    const status = instance._chibi_formatter_format(pointer, input.length);
    if (status !== 0) throw new Error(runtimeError(instance));
    const result = instance._chibi_formatter_result() >>> 0;
    const length = instance._chibi_formatter_result_length() >>> 0;
    const heap = instance.HEAPU8;
    if (!result || result > heap.length || length > heap.length - result) throw new Error('Invalid formatter result buffer');
    // Decode now, while this call still owns the result's bytes.
    return decodeResult(heap.subarray(result, result + length));
  } finally { instance._free(pointer); }
}

function setContext(instance, bytes) {
  if (bytes === activeContext) return;
  if (typeof instance._chibi_formatter_set_context !== 'function') {
    if (bytes !== null) throw new ContextUnavailable('This formatter build does not support project context');
    return;
  }
  let pointer = 0;
  try {
    if (bytes !== null) {
      pointer = instance._malloc(bytes.length);
      if (!pointer) throw new Error('The formatter could not allocate its project context');
      instance.HEAPU8.set(bytes, pointer);
    }
    if (instance._chibi_formatter_set_context(pointer, bytes?.length ?? 0) !== 0) throw new Error(runtimeError(instance));
    activeContext = bytes;
  } finally { if (pointer) instance._free(pointer); }
}

function projectLoader(instance) {
  if (!projects) projects = new ProjectContext({
    base: new URL('./', import.meta.url),
    imports(source) {
      const result = invoke(instance, encodeImports(source));
      if (!result.ok) throw new Error(result.error);
      try { return JSON.parse(result.text); }
      catch { throw new Error('Invalid compiler import metadata'); }
    },
  });
  return projects;
}

self.onmessage = async (event) => {
  const { type, id, bytes, contextRef } = event.data;
  if (type !== 'format' || !Number.isSafeInteger(id) || !(bytes instanceof ArrayBuffer)) return;
  try {
    const instance = await engine();
    const input = new Uint8Array(bytes);
    const request = decodeRequest(input);
    const path = contextPath(contextRef);
    let note = '';
    if (path !== null && needsProjectContext(request.options)) {
      try {
        const context = await projectLoader(instance).load({ path }, request.source);
        setContext(instance, context.bytes);
      } catch (error) {
        if (!(error instanceof ContextUnavailable)) throw error;
        setContext(instance, null);
        note = 'Project analysis unavailable: ' + error.message + '. Compiler-dependent edits may be preserved.\n';
      }
    } else setContext(instance, null);
    const result = invoke(instance, input);
    if (note) result.notes = note + (result.notes || '');
    self.postMessage({ type: 'result', id, result });
  } catch (error) {
    self.postMessage({ type: 'error', id, error: String(error.message || error) });
  }
};
