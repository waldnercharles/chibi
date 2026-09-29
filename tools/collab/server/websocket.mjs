import { createHash } from 'node:crypto';
import { TextDecoder } from 'node:util';
import { MAX_WS_MESSAGE_BYTES, ProtocolError } from './protocol.mjs';

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const utf8 = new TextDecoder('utf-8', { fatal: true });

export function attachWebSockets(server, { service, originAllowed }) {
  server.on('upgrade', (request, socket, head) => {
    const route = parseRoute(request.url);
    if (!route || request.method !== 'GET' || !originAllowed(request.headers.origin)) {
      rejectUpgrade(socket, 403, 'Forbidden');
      return;
    }
    if (!isWebSocketHandshake(request)) {
      rejectUpgrade(socket, 400, 'Bad Request');
      return;
    }
    const accept = createHash('sha1').update(`${request.headers['sec-websocket-key']}${GUID}`).digest('base64');
    socket.write([
      'HTTP/1.1 101 Switching Protocols',
      'Upgrade: websocket',
      'Connection: Upgrade',
      `Sec-WebSocket-Accept: ${accept}`,
      '\r\n',
    ].join('\r\n'));

    const ws = new WebSocketPeer(socket);
    const connection = service.createConnection(ws);
    const authTimer = setTimeout(() => {
      if (!connection.auth) service.disconnect(connection, 'hello_timeout');
    }, 5000);
    authTimer.unref?.();
    ws.onText = (text) => {
      service.receive(connection, text);
      if (connection.auth) clearTimeout(authTimer);
    };
    ws.onClose = (reason) => {
      clearTimeout(authTimer);
      service.disconnect(connection, reason || 'disconnected');
    };
    ws.onError = () => {
      clearTimeout(authTimer);
      service.disconnect(connection, 'socket_error');
    };
    if (head?.length) ws.feed(head);
  });
}

export function isWebSocketHandshake(request) {
  const key = request.headers['sec-websocket-key'];
  if (request.headers.upgrade?.toLowerCase() !== 'websocket' ||
      !String(request.headers.connection ?? '').toLowerCase().split(/\s*,\s*/).includes('upgrade') ||
      request.headers['sec-websocket-version'] !== '13' ||
      typeof key !== 'string' || request.headers['sec-websocket-extensions']) return false;
  const decoded = Buffer.from(key, 'base64');
  return decoded.length === 16 && decoded.toString('base64') === key;
}

function parseRoute(path) {
  if (typeof path !== 'string') return null;
  const pathname = path.split('?', 1)[0];
  const match = /^\/v1\/rooms\/([A-Za-z0-9_-]{1,96})\/ws$/.exec(pathname);
  return match ? match[1] : null;
}

function rejectUpgrade(socket, status, reason) {
  if (socket.destroyed) return;
  socket.end(`HTTP/1.1 ${status} ${reason}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
}

class WebSocketPeer {
  constructor(socket) {
    this.socket = socket;
    this.buffer = Buffer.alloc(0);
    this.fragments = [];
    this.fragmentBytes = 0;
    this.fragmentOpcode = 0;
    this.closed = false;
    this.closeNotified = false;
    this.lastPong = Date.now();
    this.onText = () => {};
    this.onClose = () => {};
    this.onError = () => {};
    socket.on('data', (chunk) => this.feed(chunk));
    socket.on('error', () => this.notifyError());
    socket.on('close', () => this.notifyClose('socket_closed'));
    this.pingTimer = setInterval(() => {
      if (Date.now() - this.lastPong > 90_000) {
        this.close(1001, 'ping_timeout');
        return;
      }
      this.writeFrame(0x9, Buffer.alloc(0));
    }, 30_000);
    this.pingTimer.unref?.();
  }

  send(message) {
    if (this.closed) return;
    const text = JSON.stringify(message);
    const payload = Buffer.from(text, 'utf8');
    if (payload.length > MAX_WS_MESSAGE_BYTES) {
      this.close(1009, 'message_too_large');
      return;
    }
    this.writeFrame(0x1, payload);
  }

  close(code = 1000, reason = '') {
    if (this.closed) return;
    this.closed = true;
    clearInterval(this.pingTimer);
    const reasonBytes = Buffer.from(reason, 'utf8').subarray(0, 123);
    const payload = Buffer.alloc(2 + reasonBytes.length);
    payload.writeUInt16BE(code, 0);
    reasonBytes.copy(payload, 2);
    this.writeFrame(0x8, payload);
    this.socket.end();
    const timer = setTimeout(() => this.socket.destroy(), 1000);
    timer.unref?.();
    this.notifyClose(reason);
  }

  feed(chunk) {
    if (this.closed) return;
    this.buffer = this.buffer.length ? Buffer.concat([this.buffer, chunk]) : chunk;
    while (this.readFrame()) {}
  }

  readFrame() {
    if (this.buffer.length < 2) return false;
    const first = this.buffer[0];
    const second = this.buffer[1];
    const fin = (first & 0x80) !== 0;
    const opcode = first & 0x0f;
    const masked = (second & 0x80) !== 0;
    if ((first & 0x70) !== 0 || !masked) return this.protocolClose('invalid_frame');
    let length = second & 0x7f;
    let headerLength = 2;
    if (length === 126) {
      if (this.buffer.length < 4) return false;
      length = this.buffer.readUInt16BE(2);
      if (length < 126) return this.protocolClose('noncanonical_length');
      headerLength = 4;
    } else if (length === 127) {
      if (this.buffer.length < 10) return false;
      const largeLength = this.buffer.readBigUInt64BE(2);
      if (largeLength < 65536n || largeLength > BigInt(MAX_WS_MESSAGE_BYTES)) {
        return this.protocolClose('frame_too_large', 1009);
      }
      length = Number(largeLength);
      headerLength = 10;
    }
    if (length > MAX_WS_MESSAGE_BYTES) return this.protocolClose('frame_too_large', 1009);
    const control = opcode >= 0x8;
    if (control && (!fin || length > 125)) return this.protocolClose('invalid_control_frame');
    const totalLength = headerLength + 4 + length;
    if (this.buffer.length < totalLength) return false;
    const mask = this.buffer.subarray(headerLength, headerLength + 4);
    const payload = Buffer.from(this.buffer.subarray(headerLength + 4, totalLength));
    this.buffer = this.buffer.subarray(totalLength);
    for (let index = 0; index < payload.length; index++) payload[index] ^= mask[index & 3];
    if (control) return this.readControl(opcode, payload);
    return this.readData(opcode, fin, payload);
  }

  readControl(opcode, payload) {
    if (opcode === 0x8) {
      if (payload.length === 1) return this.protocolClose('invalid_close_frame');
      if (!this.closed) {
        this.closed = true;
        clearInterval(this.pingTimer);
        this.writeFrame(0x8, payload);
        this.socket.end();
      }
      let reason = 'peer_closed';
      if (payload.length >= 2) {
        try { reason = utf8.decode(payload.subarray(2)); } catch { return this.protocolClose('invalid_utf8'); }
      }
      this.notifyClose(reason);
      return true;
    }
    if (opcode === 0x9) {
      this.writeFrame(0xA, payload);
      return true;
    }
    if (opcode === 0xA) {
      this.lastPong = Date.now();
      return true;
    }
    return this.protocolClose('invalid_control_opcode');
  }

  readData(opcode, fin, payload) {
    if (opcode === 0x1 || opcode === 0x2) {
      if (this.fragmentOpcode !== 0) return this.protocolClose('unexpected_data_frame');
      if (opcode === 0x2) return this.protocolClose('binary_not_supported', 1003);
      if (fin) return this.deliverText(payload);
      this.fragmentOpcode = opcode;
      this.fragments = [payload];
      this.fragmentBytes = payload.length;
      return true;
    }
    if (opcode !== 0x0 || this.fragmentOpcode === 0) return this.protocolClose('unexpected_continuation');
    this.fragmentBytes += payload.length;
    if (this.fragmentBytes > MAX_WS_MESSAGE_BYTES) return this.protocolClose('message_too_large', 1009);
    this.fragments.push(payload);
    if (!fin) return true;
    const message = Buffer.concat(this.fragments, this.fragmentBytes);
    this.fragments = [];
    this.fragmentBytes = 0;
    this.fragmentOpcode = 0;
    return this.deliverText(message);
  }

  deliverText(payload) {
    let text;
    try { text = utf8.decode(payload); }
    catch { return this.protocolClose('invalid_utf8'); }
    try { this.onText(text); }
    catch { this.close(1011, 'message_handler_error'); }
    return true;
  }

  protocolClose(reason, code = 1002) {
    this.close(code, reason);
    return false;
  }

  writeFrame(opcode, payload) {
    if (this.socket.destroyed) return;
    const length = payload.length;
    let header;
    if (length < 126) {
      header = Buffer.from([0x80 | opcode, length]);
    } else if (length <= 0xffff) {
      header = Buffer.alloc(4);
      header[0] = 0x80 | opcode;
      header[1] = 126;
      header.writeUInt16BE(length, 2);
    } else {
      header = Buffer.alloc(10);
      header[0] = 0x80 | opcode;
      header[1] = 127;
      header.writeBigUInt64BE(BigInt(length), 2);
    }
    this.socket.write(payload.length ? Buffer.concat([header, payload]) : header);
  }

  notifyClose(reason) {
    if (this.closeNotified) return;
    this.closeNotified = true;
    clearInterval(this.pingTimer);
    this.onClose(reason);
  }

  notifyError() {
    this.onError();
    this.notifyClose('socket_error');
  }
}
