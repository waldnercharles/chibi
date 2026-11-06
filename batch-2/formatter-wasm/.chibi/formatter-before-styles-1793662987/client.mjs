import { encodeRequest } from './protocol.mjs';

// Keep one running request and the latest waiting one. A failure settles every
// promise before discarding the worker; the next request starts a fresh worker.
export class FormatterClient {
  constructor({ makeWorker = () => new Worker(new URL('./worker.mjs', import.meta.url), { type: 'module' }), timeoutMs = 30000 } = {}) {
    this.makeWorker = makeWorker;
    this.timeoutMs = timeoutMs;
    this.worker = null;
    this.active = null;
    this.waiting = null;
    this.serial = 0;
    this.timer = null;
  }

  format(source, options) {
    let bytes;
    try { bytes = encodeRequest(source, options); }
    catch (error) { return Promise.reject(error); }
    return new Promise((resolve, reject) => {
      const job = { id: ++this.serial, bytes, resolve, reject };
      if (this.active) {
        if (this.waiting) {
          const error = new Error('Formatting request was superseded');
          error.name = 'AbortError';
          this.waiting.reject(error);
        }
        this.waiting = job;
      } else this.start(job);
    });
  }

  start(job) {
    this.active = job;
    try {
      if (!this.worker) {
        const worker = this.makeWorker();
        this.worker = worker;
        worker.onmessage = (event) => {
          if (worker !== this.worker || !this.active || event.data?.id !== this.active.id) return;
          if (event.data.type === 'error') {
            this.fail(new Error(String(event.data.error || 'The formatter failed')));
            return;
          }
          const result = event.data.result;
          if (event.data.type !== 'result' || !result || typeof result.ok !== 'boolean' ||
              (result.ok && (typeof result.text !== 'string' || typeof result.formatted_html !== 'string')) ||
              (!result.ok && typeof result.error !== 'string') || typeof result.original_html !== 'string') {
            this.fail(new Error('Invalid formatter worker response'));
            return;
          }
          clearTimeout(this.timer);
          const finished = this.active;
          this.active = null;
          finished.resolve(result);
          const next = this.waiting;
          this.waiting = null;
          if (next) this.start(next);
        };
        worker.onerror = () => { if (worker === this.worker) this.fail(new Error('The formatter worker could not run. Retry to reload it.')); };
        worker.onmessageerror = () => { if (worker === this.worker) this.fail(new Error('The formatter worker returned an unreadable message')); };
      }
      this.timer = setTimeout(() => this.fail(new Error('The formatter did not answer within 30 seconds. Retry to restart it.')), this.timeoutMs);
      this.worker.postMessage({ type: 'format', id: job.id, bytes: job.bytes.buffer }, [job.bytes.buffer]);
    } catch (error) { this.fail(error); }
  }

  fail(error) {
    clearTimeout(this.timer);
    this.worker?.terminate();
    this.worker = null;
    this.active?.reject(error);
    this.waiting?.reject(error);
    this.active = null;
    this.waiting = null;
  }
}
