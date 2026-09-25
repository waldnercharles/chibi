#!/usr/bin/env python3
"""Serve a consistent browser build with saves routed to the selected local project."""
import argparse
import json
import os
import queue
import secrets
import shutil
import signal
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

import yaml

LOOPBACK = {'127.0.0.1', 'localhost', '::1'}
REQUEST_LIMIT = 16 * 1024 * 1024
# Direct connections only: a configured HTTP proxy must not see loopback traffic.
DIRECT = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def content_directory(project):
    manifest = json.loads((project / 'project.json').read_text())
    content = (project / manifest['content']).resolve()
    if not content.is_relative_to(project):
        raise ValueError('Project content must be inside the project directory')
    return content


def save_asset(project, content, request):
    path = (project / request['path'].lstrip('/')).resolve()
    if not path.is_relative_to(content) or path.suffix != '.yaml':
        raise ValueError('Saves must be YAML assets inside this project content directory')
    text = request['text']
    # The engine writes asset documents as YAML mappings; a save must reload.
    try:
        document = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ValueError(f'Saves must be YAML asset documents: {error}') from error
    if not isinstance(document, dict):
        raise ValueError('Saves must be YAML asset documents')
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as file:
        temporary = Path(file.name)
        file.write(text)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def model_request(envelope, timeout=60):
    """Forward an editor command model request to its loopback model host.

    The browser cannot reach Ollama or another OpenAI-compatible host across
    origins, so the editor hands its request envelope ({url, body}) to this
    server. Only plain HTTP on local loopback is forwarded; failures return the
    error envelope the editor reads."""
    url = urlsplit(envelope['url'])
    body = envelope['body']
    if url.scheme != 'http' or url.hostname not in LOOPBACK or url.username or url.password:
        raise ValueError('Model host must use http:// on local loopback')
    if not isinstance(body, str):
        raise TypeError('Model request body must be JSON text')
    request = urllib.request.Request(envelope['url'], data=body.encode(), method='POST',
                                     headers={'Content-Type': 'application/json'})
    try:
        with DIRECT.open(request, timeout=timeout) as response:
            return response.read().decode()
    except urllib.error.HTTPError as error:
        return json.dumps({'ok': False, 'error': f'Model host returned HTTP {error.code}'})
    except (urllib.error.URLError, OSError) as error:
        reason = getattr(error, 'reason', error)
        return json.dumps({'ok': False, 'error': f'Could not reach the model host: {reason}'})


class Sessions:
    """Live browser editors that external command clients can reach.

    A page registers and long-polls for command packets; forward() queues one
    for the page and waits for its reply."""

    def __init__(self):
        self.lock = threading.Lock()
        self.sessions = {}

    def register(self):
        with self.lock:
            self.sessions = {k: v for k, v in self.sessions.items() if time.monotonic() - v['seen'] < 60}
            if len(self.sessions) >= 16:
                raise ValueError('Too many editor sessions')
            key = secrets.token_hex(16)
            self.sessions[key] = {'seen': time.monotonic(), 'queue': queue.Queue(), 'waiting': {}}
            return {'session': key}

    def list(self):
        with self.lock:
            return [{'session': k, 'active': time.monotonic() - v['seen'] < 30} for k, v in self.sessions.items()]

    def get(self, key):
        session = self.sessions.get(key)
        if not session:
            raise ValueError('Unknown editor session')
        return session

    def poll(self, key, timeout=15):
        with self.lock:
            session = self.get(key)
            session['seen'] = time.monotonic()
        try:
            return [session['queue'].get(timeout=timeout)]
        except queue.Empty:
            return []

    def reply(self, key, request, result):
        with self.lock:
            channel = self.get(key)['waiting'].get(request)
            if channel:
                channel.put_nowait(result)
        return {'ok': True}

    def forward(self, key, packet, timeout=10):
        with self.lock:
            session = self.get(key)
            if time.monotonic() - session['seen'] > 30:
                raise ValueError('Editor session disconnected')
            request = secrets.token_hex(12)
            channel = queue.Queue(maxsize=1)
            session['waiting'][request] = channel
            session['queue'].put({'id': request, 'packet': packet})
        try:
            return channel.get(timeout=timeout)
        except queue.Empty:
            # A timed-out submit must be polled using its request_id; never retry with a new ID.
            raise ValueError('Editor did not respond; inspect the task using the same request_id') from None
        finally:
            with self.lock:
                session['waiting'].pop(request, None)


def write_connection(port, token, project):
    """Private (0600) connection file for command clients such as the MCP."""
    connection = Path('.chibi') / f'web-editor-{port}.json'
    connection.parent.mkdir(exist_ok=True)
    descriptor = os.open(connection, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, 'w') as file:
        json.dump({'url': f'http://127.0.0.1:{port}', 'token': token, 'project': str(project),
                   'pid': os.getpid()}, file)
    return connection


def append_trace(project, record):
    traces = project / '.chibi'
    traces.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(traces / 'command-traces.jsonl', os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    with os.fdopen(descriptor, 'w') as file:
        file.write(json.dumps(record) + '\n')
    return {'ok': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build-web'))
    parser.add_argument('--project', type=Path, default=Path('projects/arena2d'))
    parser.add_argument('--port', type=int, default=8099)
    args = parser.parse_args()
    project = args.project.resolve()
    content = content_directory(project)
    token = secrets.token_urlsafe(32)
    sessions = Sessions()
    connection = write_connection(args.port, token, project)
    # A served generation never changes under the browser's JS/WASM/data requests.
    with tempfile.TemporaryDirectory(prefix='chibi-web-') as directory:
        snapshot = Path(directory)
        for suffix in ('html', 'js', 'wasm', 'data'):
            shutil.copy2(args.build / f'chibi.{suffix}', snapshot)
        bridge = (Path(__file__).with_name('web_workspace.js')).read_text()
        bridge = bridge.replace('__CHIBI_SAVE_TOKEN__', json.dumps(token))
        html = snapshot / 'chibi.html'
        html.write_text(html.read_text().replace('<script async', f'<script>{bridge}</script><script async'))

        routes = {
            '/__chibi/model': model_request,
            '/__chibi/editor': lambda packet: sessions.forward(packet['session'], packet['packet']),
            '/__chibi/session/register': lambda packet: sessions.register(),
            '/__chibi/session/poll': lambda packet: sessions.poll(packet['session']),
            '/__chibi/session/reply': lambda packet: sessions.reply(packet['session'], packet['id'], packet['result']),
            '/__chibi/trace': lambda packet: append_trace(project, packet),
        }

        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *a, **kw):
                super().__init__(*a, directory=directory, **kw)

            def parse_request(self):
                if not super().parse_request():
                    return False
                if self.headers.get('Host') != f'127.0.0.1:{args.port}':
                    self.send_error(403)
                    return False
                return True

            def end_headers(self):
                self.send_header('Cache-Control', 'no-store')
                super().end_headers()

            def send_json(self, result):
                body = (result if isinstance(result, str) else json.dumps(result)).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                path = urlsplit(self.path).path
                if path in ('/__chibi/assets', '/__chibi/sessions') and self.headers.get('X-Chibi-Token') != token:
                    self.send_error(403)
                    return
                if path == '/__chibi/sessions':
                    self.send_json(sessions.list())
                    return
                if path == '/__chibi/assets':
                    assets = {str(p.relative_to(project)): p.read_text()
                              for p in content.rglob('*.yaml') if p.resolve().is_relative_to(content)}
                    assets['project.json'] = (project / 'project.json').read_text()
                    self.send_json(assets)
                    return
                # Ignore validators left over from another preview server.
                if 'If-Modified-Since' in self.headers:
                    del self.headers['If-Modified-Since']
                super().do_GET()

            def do_POST(self):
                if (self.path != '/__chibi/save' and self.path not in routes) or \
                        self.headers.get('X-Chibi-Token') != token:
                    self.send_error(403)
                    return
                try:
                    length = int(self.headers.get('Content-Length', '0'))
                except ValueError:
                    self.send_error(400, 'Invalid content length')
                    return
                if not 0 < length <= REQUEST_LIMIT:
                    self.send_error(413)
                    return
                try:
                    packet = json.loads(self.rfile.read(length))
                    if self.path != '/__chibi/save':
                        self.send_json(routes[self.path](packet))
                        return
                    save_asset(project, content, packet)
                except (ValueError, KeyError, TypeError, OSError) as error:
                    self.send_error(400, str(error))
                    return
                self.send_response(204)
                self.end_headers()

        print(f'Editor: http://127.0.0.1:{args.port}/chibi.html', flush=True)
        print(f'Saves: {content}', flush=True)
        print(f'Commands: {connection}', flush=True)
        # Termination runs the cleanup below, which removes the connection file.
        signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
        try:
            ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
        finally:
            connection.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
