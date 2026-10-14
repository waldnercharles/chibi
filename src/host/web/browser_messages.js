// Generic browser I/O for authored applications. Application protocols, roles,
// revisions and recovery policy remain in authored behaviors.
(() => {
  const MAX_TEXT = 65536, MAX_ATTACHMENT = 32 * 1024 * 1024, MAX_RETAINED = 64 * 1024 * 1024;
  const encoder = new TextEncoder();
  const owners = new Map();
  let nextSource = 1;
  const owner = id => {
    if (!owners.has(id)) owners.set(id, {sources: new Map(), sockets: new Map(), jobs: new Map(), installed: new Map(), epochs: new Map(), bytes: 0});
    return owners.get(id);
  };
  const retain = (state, text) => {
    const size = encoder.encode(text).length;
    if (size > MAX_ATTACHMENT || state.sources.size >= 16 || state.bytes + size > MAX_RETAINED || nextSource >= 2147483647) {
      throw Error('Browser attachment budget exceeded');
    }
    const source = nextSource++;
    state.sources.set(source, {text, size});
    state.bytes += size;
    return source;
  };
  const release = (id, source) => {
    const state = owners.get(id), record = state?.sources.get(source);
    if (!record) return false;
    state.sources.delete(source);
    state.bytes -= record.size;
    return true;
  };
  const deliver = (id, request, channel, kind, text = '', source = 0) => {
    if (!owners.has(id)) return false;
    text = String(text);
    if (encoder.encode(text).length > MAX_TEXT) text = 'Browser result metadata exceeded its limit';
    const pointers = [channel, kind, text].map(value => Module['stringToNewUTF8'](value));
    try {
      const accepted = !!Module['_browser_messages_deliver'](id, request, ...pointers, source);
      if (!accepted && source) release(id, source);
      return accepted;
    } finally { for (const pointer of pointers) Module['_free'](pointer); }
  };
  const url = (value, protocols) => {
    const result = new URL(value, location.href);
    if (!protocols.includes(result.protocol) || result.username || result.password || result.hash) throw Error('Invalid transport URL');
    return result;
  };
  const boundedResponse = async response => {
    if (!response.body) return '';
    const reader = response.body.getReader(), chunks = [];
    let bytes = 0;
    try {
      while (true) {
        const item = await reader.read();
        if (item.done) break;
        bytes += item.value.length;
        if (bytes > MAX_ATTACHMENT) throw Error('HTTP response exceeds attachment limit');
        chunks.push(item.value);
      }
    } finally {
      try { await reader.cancel(); } catch {}
      reader.releaseLock();
    }
    const data = new Uint8Array(bytes);
    let at = 0;
    for (const chunk of chunks) { data.set(chunk, at); at += chunk.length; }
    return new TextDecoder('utf-8', {fatal: true}).decode(data);
  };
  const pointer = (root, path, replacement) => {
    if (!path.startsWith('/') || path.length > 4096) throw Error('Expected JSON attachment pointer');
    const parts = path.slice(1).split('/').map(part => {
      if (/~(?:[^01]|$)/.test(part)) throw Error('Invalid JSON pointer escape');
      const key = part.replace(/~1/g, '/').replace(/~0/g, '~');
      if (['__proto__', 'prototype', 'constructor'].includes(key)) throw Error('Unsafe JSON attachment pointer');
      return key;
    });
    let value = root;
    for (let i = 0; i < parts.length; i++) {
      if (!value || typeof value !== 'object' || Array.isArray(value)) throw Error('Attachment pointer requires existing object parents');
      const key = parts[i];
      if (i === parts.length - 1 && replacement !== undefined) { value[key] = replacement; return root; }
      if (!Object.prototype.hasOwnProperty.call(value, key)) throw Error('Attachment pointer is missing');
      value = value[key];
    }
    return value;
  };
  const request = (id, requestId, channel, operation, value, json, headers, source, root, documentState) => {
    const state = owner(id);
    const emit = (kind, text = '', attachment = 0) => deliver(id, requestId, channel, kind, text, attachment);
    const epoch = state.epochs.get(channel) || 0;
    const live = () => owners.get(id) === state && (state.epochs.get(channel) || 0) === epoch;
    try {
      if (!state.epochs.has(channel)) {
        if (state.epochs.size >= 64) throw Error('Browser channel limit exceeded');
        state.epochs.set(channel, epoch);
      }
      if (operation === 'configuration') {
        emit('result', '', retain(state, JSON.stringify({...Module['browserMessageConfig']?.[channel], workspace_root: root})));
      } else if (operation === 'make-url') {
        const parsed = new URL(value, location.href), options = JSON.parse(json || '{}');
        if (!['http:', 'https:'].includes(parsed.protocol) || parsed.username || parsed.password) throw Error('Invalid output URL');
        for (const [key, item] of Object.entries(options.query || {})) parsed.searchParams.set(key, String(item));
        if (options.fragment) parsed.hash = new URLSearchParams(options.fragment).toString();
        emit('result', parsed.href);
      } else if (operation === 'verify-json') {
        if (state.jobs.size >= 8) throw Error('Browser integrity request limit exceeded');
        const attachment = state.sources.get(source);
        if (!attachment) throw Error('Unknown integrity attachment');
        const selected = value ? pointer(JSON.parse(attachment.text), value) : JSON.parse(attachment.text);
        if (!selected || typeof selected !== 'object' || Array.isArray(selected) || !/^[a-f0-9]{64}$/.test(selected.sha256 || '')) {
          throw Error('Integrity target requires a SHA-256 digest');
        }
        const expected = selected.sha256;
        delete selected.sha256;
        state.jobs.set(requestId, null);
        crypto.subtle.digest('SHA-256', encoder.encode(JSON.stringify(selected))).then(hash => {
          const actual = [...new Uint8Array(hash)].map(byte => byte.toString(16).padStart(2, '0')).join('');
          if (actual !== expected) throw Error('JSON integrity digest mismatch');
          if (live()) emit('result', JSON.stringify({sha256: actual}));
        }).catch(error => { if (live()) emit('error', error.message); }).finally(() => state.jobs.delete(requestId));
      } else if (operation === 'parse-url') {
        const parsed = new URL(value, location.href);
        emit('result', '', retain(state, JSON.stringify({protocol: parsed.protocol, origin: parsed.origin, host: parsed.host,
          pathname: parsed.pathname, hash: parsed.hash, query: Object.fromEntries(parsed.searchParams),
          fragment: Object.fromEntries(new URLSearchParams(parsed.hash.slice(1)))})));
      } else if (operation === 'location') {
        emit('result', '', retain(state, JSON.stringify({href: location.href, origin: location.origin, hash: location.hash, search: location.search, now: Date.now() / 1000})));
      } else if (operation === 'session-read' || operation === 'session-write') {
        if (!/^[A-Za-z0-9_-]{1,64}$/.test(value)) throw Error('Invalid session key');
        const key = 'chibi.message.' + channel + '.' + value;
        if (operation === 'session-write') {
          if (json) sessionStorage.setItem(key, json); else sessionStorage.removeItem(key);
          emit('result');
        } else {
          const text = sessionStorage.getItem(key) || '';
          if (encoder.encode(text).length > MAX_TEXT) throw Error('Stored session metadata exceeds limit');
          emit('result', text);
        }
      } else if (operation === 'connect') {
        if (state.sockets.has(channel)) throw Error('Close the existing channel before connecting');
        if (state.sockets.size >= 8) throw Error('Browser socket limit exceeded');
        const socket = new WebSocket(url(value, ['ws:', 'wss:']));
        state.sockets.set(channel, socket);
        socket.onopen = () => { if (live() && state.sockets.get(channel) === socket) emit('open'); };
        socket.onmessage = event => {
          if (!live() || state.sockets.get(channel) !== socket) return;
          try {
            if (typeof event.data !== 'string') throw Error('Only UTF-8 JSON text messages are supported');
            emit('message', '', retain(state, event.data));
          } catch (error) { emit('error', error.message); socket.close(1009, 'Attachment limit or type refusal'); }
        };
        socket.onerror = () => { if (live()) emit('error', 'Browser socket failed'); };
        socket.onclose = event => {
          if (!live() || state.sockets.get(channel) !== socket) return;
          state.sockets.delete(channel);
          emit('closed', event.reason || 'Socket closed (' + event.code + ')');
        };
      } else if (operation === 'close') {
        state.epochs.set(channel, epoch + 1);
        const socket = state.sockets.get(channel);
        state.sockets.delete(channel);
        socket?.close(1000, 'Application closed channel');
        for (const [installed, ownerChannel] of state.installed) {
          if (ownerChannel !== channel) continue;
          try { Module['browserWorkspaceFiles'].remove(installed); } catch (_) { /* Adopted roots belong to the active host. */ }
          state.installed.delete(installed);
        }
        emit('closed', 'Application closed channel');
      } else if (operation === 'compose-json') {
        if (state.jobs.size >= 8) throw Error('Browser composition request limit exceeded');
        const attachment = state.sources.get(source);
        if (!attachment) throw Error('Unknown attachment');
        const payload = pointer(JSON.parse(json), value, JSON.parse(attachment.text));
        const options = JSON.parse(headers || '{}');
        state.jobs.set(requestId, null);
        Promise.resolve().then(async () => {
          let sha256 = '';
          if (typeof options.sha256 === 'string') {
            const target = options.sha256 ? pointer(payload, options.sha256) : payload;
            if (!target || typeof target !== 'object' || Array.isArray(target) || Object.hasOwn(target, 'sha256')) {
              throw Error('SHA-256 target must be an object without an existing digest');
            }
            const bytes = encoder.encode(JSON.stringify(target));
            if (bytes.length > MAX_ATTACHMENT) throw Error('JSON composition exceeds attachment limit');
            sha256 = [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map(byte => byte.toString(16).padStart(2, '0')).join('');
            target.sha256 = sha256;
          }
          if (live()) emit('result', JSON.stringify({sha256}), retain(state, JSON.stringify(payload)));
        }).catch(error => { if (live()) emit('error', error.message); })
          .finally(() => state.jobs.delete(requestId));
      } else if (operation === 'send') {
        const socket = state.sockets.get(channel);
        if (!socket || socket.readyState !== WebSocket.OPEN) throw Error('Browser socket is not open');
        let payload = json;
        if (source) {
          const attachment = state.sources.get(source);
          if (!attachment) throw Error('Unknown attachment');
          payload = !json && !value ? attachment.text : JSON.stringify(pointer(JSON.parse(json), value, JSON.parse(attachment.text)));
        }
        if (encoder.encode(payload).length > MAX_ATTACHMENT || socket.bufferedAmount + encoder.encode(payload).length > MAX_RETAINED) {
          throw Error('Browser socket send budget exceeded');
        }
        socket.send(payload);
        emit('sent');
      } else if (operation === 'http-post') {
        if (state.jobs.size >= 8) throw Error('Browser HTTP request limit exceeded');
        const target = url(value, ['http:', 'https:']);
        const fields = JSON.parse(headers || '{}');
        if (!fields || typeof fields !== 'object' || Array.isArray(fields) || Object.values(fields).some(item => typeof item !== 'string')) {
          throw Error('Expected HTTP header object');
        }
        const controller = new AbortController();
        state.jobs.set(requestId, controller);
        fetch(target, {method: 'POST', headers: fields, body: json || '{}', signal: controller.signal})
          .then(async response => {
            const text = await boundedResponse(response);
            if (!live()) return;
            if (!response.ok) { emit('error', 'HTTP ' + response.status + ': ' + text.slice(0, 4096)); return; }
            emit('result', '', retain(state, text));
          }).catch(error => { if (live()) emit('error', error.message); })
          .finally(() => state.jobs.delete(requestId));
      } else if (operation === 'archive-capture' || operation === 'archive-install') {
        if (!Module['browserWorkspaceFiles']) throw Error('Workspace archive adapter unavailable');
        if (state.jobs.size >= 8) throw Error('Browser archive request limit exceeded');
        if (operation === 'archive-capture' && (!(documentState instanceof Uint8Array) || !documentState.length || documentState.length > MAX_ATTACHMENT)) throw Error('Document checkpoint bytes unavailable or oversized');
        // capture() copies files and document bytes before its first await, while
        // the C snapshot is still borrowed and before this request returns.
        const captured = operation === 'archive-capture'
          ? Module['browserWorkspaceFiles'].capture(root, [], [{name: 'workspace-documents-v1', data: documentState}]) : null;
        state.jobs.set(requestId, null);
        Promise.resolve().then(async () => {
          if (operation === 'archive-capture') {
            const archive = await captured;
            if (live()) emit('result', JSON.stringify({bytes: archive.bytes, sha256: archive.sha256}), retain(state, JSON.stringify(archive)));
          } else {
            const attachment = state.sources.get(source);
            if (!attachment) throw Error('Unknown archive attachment');
            const archive = value ? pointer(JSON.parse(attachment.text), value) : JSON.parse(attachment.text);
            if (!live()) return;
            if (state.installed.size >= 8) throw Error('Browser installed workspace limit exceeded');
            const installed = await Module['browserWorkspaceFiles'].install(archive);
            if (live()) {
              if (state.installed.size >= 8) { Module['browserWorkspaceFiles'].remove(installed); throw Error('Browser installed workspace limit exceeded'); }
              state.installed.set(installed, channel);
              emit('result', installed);
            }
            else Module['browserWorkspaceFiles'].remove(installed);
          }
        }).catch(error => { if (live()) emit('error', error.message); })
          .finally(() => state.jobs.delete(requestId));
      } else { throw Error('Unknown browser message operation'); }
      return true;
    } catch (error) { emit('error', error.message); return true; }
  };
  Module['browserMessages'] = {
    request,
    text: (id, source) => owners.get(id)?.sources.get(source)?.text,
    release,
    destroy: id => {
      const state = owners.get(id);
      if (!state) return;
      owners.delete(id);
      for (const socket of state.sockets.values()) socket.close(1000, 'Host released');
      for (const controller of state.jobs.values()) controller?.abort();
      for (const installed of state.installed.keys()) {
        try { Module['browserWorkspaceFiles'].remove(installed); } catch (_) { /* Adopted roots belong to the active host. */ }
      }
      state.installed.clear();
      state.sources.clear();
    },
  };
})();
