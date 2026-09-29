import { createServer } from 'node:http';
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { pathToFileURL } from 'node:url';
import { ProtocolError, isRecord } from './protocol.mjs';
import { createRoomService } from './room-service.mjs';
import { attachWebSockets } from './websocket.mjs';

const MAX_HTTP_BODY_BYTES = 8192;

export function createCollabServer(options = {}) {
  const config = options.config ?? configFromEnvironment(options.env ?? process.env);
  const service = options.service ?? createRoomService({
    secret: config.tokenSecret,
    adminToken: config.adminToken,
    hostGraceMs: config.hostGraceMs,
  });
  const server = createServer((request, response) => {
    handleHttp(request, response).catch((error) =>
      reply(response, errorStatus(error), { error: errorCode(error) }));
  });
  server.headersTimeout = 10_000;
  server.requestTimeout = 15_000;
  server.keepAliveTimeout = 5_000;

  const originAllowed = (origin) => isAllowedOrigin(origin, config);
  attachWebSockets(server, { service, originAllowed });

  async function handleHttp(request, response) {
    const origin = request.headers.origin;
    if (origin !== undefined && !originAllowed(origin)) {
      reply(response, 403, { error: 'origin_denied' });
      return;
    }
    if (origin && originAllowed(origin)) setCors(response, origin);
    if (request.method === 'OPTIONS') {
      if (!origin || !originAllowed(origin)) {
        reply(response, 403, { error: 'origin_denied' });
        return;
      }
      response.writeHead(204, {
        'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
        'Access-Control-Allow-Headers': 'Authorization, Content-Type',
        'Access-Control-Max-Age': '600',
      });
      response.end();
      return;
    }
    const url = new URL(request.url ?? '/', 'http://localhost');
    if (request.method === 'GET' && url.pathname === '/healthz') {
      reply(response, 200, { status: 'ok' });
      return;
    }
    if (request.method !== 'POST') {
      reply(response, 405, { error: 'method_not_allowed' }, { Allow: 'GET, POST, OPTIONS' });
      return;
    }
    if (url.pathname === '/v1/rooms') {
      const body = await readJsonBody(request);
      if (!isRecord(body) || Object.keys(body).length !== 0) {
        reply(response, 400, { error: 'invalid_request' });
        return;
      }
      if (!authorizedRoomCreate(request, config, origin)) {
        reply(response, 401, { error: 'unauthorized' });
        return;
      }
      const created = service.createRoom();
      reply(response, 201, { ...created, ws_url: wsUrl(config, created.room) });
      return;
    }
    const inviteMatch = /^\/v1\/rooms\/([A-Za-z0-9_-]{1,96})\/invites$/.exec(url.pathname);
    if (inviteMatch) {
      const body = await readJsonBody(request);
      if (!isRecord(body) || Object.keys(body).length !== 1 || !['editor', 'viewer'].includes(body.role)) {
        reply(response, 400, { error: 'invalid_request' });
        return;
      }
      const token = bearerToken(request.headers.authorization);
      if (!token) {
        reply(response, 401, { error: 'unauthorized' });
        return;
      }
      try {
        const invite = service.issueInvite(token, body.role, inviteMatch[1]);
        if (invite.room !== inviteMatch[1]) throw new ProtocolError('forbidden');
        reply(response, 201, { ...invite, ws_url: wsUrl(config, invite.room) });
      } catch (error) {
        reply(response, errorStatus(error), { error: errorCode(error) });
      }
      return;
    }
    reply(response, 404, { error: 'not_found' });
  }

  return {
    config,
    server,
    service,
    listen(port = config.port, host = config.host) {
      return new Promise((resolve, reject) => {
        const onError = (error) => {
          server.off('listening', onListening);
          reject(error);
        };
        const onListening = () => {
          server.off('error', onError);
          resolve(server.address());
        };
        server.once('error', onError);
        server.once('listening', onListening);
        server.listen(port, host);
      });
    },
    close() {
      service.closeAll?.('server_shutdown');
      return new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    },
  };
}

export function configFromEnvironment(env = process.env) {
  const production = env.NODE_ENV === 'production';
  const host = env.COLLAB_HOST ?? '127.0.0.1';
  const port = parsePort(env.COLLAB_PORT ?? '8787');
  const allowedOrigins = (env.COLLAB_ALLOWED_ORIGINS ?? '')
    .split(',').map((value) => value.trim()).filter(Boolean).map(parseOrigin);
  const adminToken = env.COLLAB_ADMIN_TOKEN || null;
  const secretValue = env.COLLAB_TOKEN_SECRET || null;
  const publicWsUrl = env.COLLAB_PUBLIC_WS_URL || null;
  if (production) {
    if (!adminToken || Buffer.byteLength(adminToken) < 32) {
      throw new Error('COLLAB_ADMIN_TOKEN must contain at least 32 bytes in production');
    }
    if (!secretValue || Buffer.byteLength(secretValue) < 32) {
      throw new Error('COLLAB_TOKEN_SECRET must contain at least 32 bytes in production');
    }
    if (allowedOrigins.length === 0) throw new Error('COLLAB_ALLOWED_ORIGINS is required in production');
    if (!publicWsUrl) throw new Error('COLLAB_PUBLIC_WS_URL is required in production');
    const parsed = new URL(publicWsUrl);
    if (!['ws:', 'wss:'].includes(parsed.protocol) || parsed.pathname !== '/' || parsed.search || parsed.hash) {
      throw new Error('COLLAB_PUBLIC_WS_URL must be a ws(s) origin without a path');
    }
    if (parsed.protocol !== 'wss:') throw new Error('Production room service requires wss');
  }
  const tokenSecret = secretValue ? Buffer.from(secretValue, 'utf8') : randomBytes(32);
  return {
    production,
    host,
    port,
    adminToken,
    tokenSecret,
    allowedOrigins,
    publicWsUrl,
    hostGraceMs: parsePositiveInteger(env.COLLAB_HOST_GRACE_MS ?? '30000', 'COLLAB_HOST_GRACE_MS'),
    localTokenIssuance: env.COLLAB_LOCAL_TOKEN_ISSUANCE !== '0',
  };
}

function authorizedRoomCreate(request, config, origin) {
  if (config.adminToken && constantTimeEqual(bearerToken(request.headers.authorization), config.adminToken)) return true;
  if (config.production || !config.localTokenIssuance || !isLoopbackBind(config.host)) return false;
  if (!origin) return true;
  return isLoopbackOrigin(origin);
}

function isAllowedOrigin(origin, config) {
  if (typeof origin !== 'string' || origin === 'null') return false;
  let parsed;
  try { parsed = new URL(origin); } catch { return false; }
  if (parsed.origin !== origin || parsed.username || parsed.password || parsed.pathname !== '/' ||
      parsed.search || parsed.hash) return false;
  if (config.allowedOrigins.length) return config.allowedOrigins.includes(origin);
  return !config.production && isLoopbackOrigin(origin) && isLoopbackBind(config.host);
}

function setCors(response, origin) {
  response.setHeader('Access-Control-Allow-Origin', origin);
  response.setHeader('Vary', 'Origin');
}

function wsUrl(config, room) {
  if (config.publicWsUrl) return `${config.publicWsUrl.replace(/\/$/, '')}/v1/rooms/${room}/ws`;
  const protocol = config.production ? 'wss' : 'ws';
  const host = config.host === '0.0.0.0' || config.host === '::' ? '127.0.0.1' : config.host;
  return `${protocol}://${host}:${config.port}/v1/rooms/${room}/ws`;
}

function parseOrigin(value) {
  let url;
  try { url = new URL(value); } catch { throw new Error(`Invalid origin: ${value}`); }
  if (!['http:', 'https:'].includes(url.protocol) || url.origin !== value || url.pathname !== '/' ||
      url.search || url.hash || url.username || url.password) {
    throw new Error(`Invalid origin: ${value}`);
  }
  return url.origin;
}

function isLoopbackOrigin(value) {
  let url;
  try { url = new URL(value); } catch { return false; }
  return url.protocol === 'http:' && ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname);
}

function isLoopbackBind(host) {
  return host === 'localhost' || host === '127.0.0.1' || host === '::1' || host === '[::1]';
}

function bearerToken(header) {
  if (typeof header !== 'string') return null;
  const match = /^Bearer ([A-Za-z0-9._~-]{32,4096})$/.exec(header);
  return match?.[1] ?? null;
}

function constantTimeEqual(candidate, expected) {
  if (typeof candidate !== 'string' || typeof expected !== 'string') return false;
  const left = Buffer.from(candidate);
  const right = Buffer.from(expected);
  return left.length === right.length && timingSafeEqual(left, right);
}

async function readJsonBody(request) {
  if (request.headers['content-type']?.split(';', 1)[0].trim().toLowerCase() !== 'application/json') {
    throw new ProtocolError('invalid_content_type');
  }
  const declaredLength = Number(request.headers['content-length'] ?? 0);
  if (declaredLength > MAX_HTTP_BODY_BYTES) throw new ProtocolError('request_too_large');
  const chunks = [];
  let length = 0;
  for await (const chunk of request) {
    length += chunk.length;
    if (length > MAX_HTTP_BODY_BYTES) throw new ProtocolError('request_too_large');
    chunks.push(chunk);
  }
  if (length === 0) return {};
  try { return JSON.parse(Buffer.concat(chunks, length).toString('utf8')); }
  catch { throw new ProtocolError('invalid_json'); }
}

function parsePort(value) {
  const port = Number(value);
  if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error('COLLAB_PORT must be between 0 and 65535');
  return port;
}

function parsePositiveInteger(value, name) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1) throw new Error(`${name} must be a positive integer`);
  return parsed;
}

function errorCode(error) {
  if (error instanceof ProtocolError) return error.code;
  return 'internal_error';
}

function errorStatus(error) {
  switch (errorCode(error)) {
    case 'invalid_credentials': return 401;
    case 'unauthorized': return 401;
    case 'forbidden': return 403;
    case 'origin_denied': return 403;
    case 'room_closed': return 410;
    case 'room_capacity': return 429;
    case 'invalid_role': return 400;
    case 'invalid_content_type':
    case 'request_too_large':
    case 'invalid_json': return 400;
    case 'internal_error': return 500;
    default: return error instanceof ProtocolError ? 400 : 500;
  }
}

function reply(response, status, body, extraHeaders = {}) {
  if (response.headersSent) {
    response.destroy();
    return;
  }
  const data = Buffer.from(JSON.stringify(body), 'utf8');
  response.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': data.length,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
    ...extraHeaders,
  });
  response.end(data);
}

async function run() {
  const app = createCollabServer();
  const address = await app.listen();
  process.stdout.write(`Collaboration room service listening on ${address.address}:${address.port}\n`);
  const shutdown = () => app.close().finally(() => process.exit(0));
  process.once('SIGINT', shutdown);
  process.once('SIGTERM', shutdown);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  run().catch((error) => {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 1;
  });
}
