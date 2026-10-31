#!/usr/bin/env python3
"""Interim, local-only host for this playground until a Chibi formatter runs in the browser: static page, repo file list, and a resident
host/Main.chibi process reached through a mailbox directory.
Usage: serve.py REPO CHIBI PORT, with CHIBI the built SDK launcher, e.g. build-release/sdk/chibi."""
import json, os, shutil, subprocess, sys, tempfile, threading, time, http.server, urllib.parse

repo, chibi, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
page = os.path.join(repo, 'tools/docs/playground')
mailbox = tempfile.mkdtemp(prefix='chibi-playground-')

# The host program: host/Main.chibi with the packages it imports beside it, as
# `chibi run` wants a program's packages inside its directory.
program = tempfile.mkdtemp(prefix='chibi-playground-host-')
shutil.copy(os.path.join(page, 'host', 'Main.chibi'), program)
for package in ('compiler_syntax', 'source_format', 'source_highlight', 'source_parser', 'source_tokenizer', 'syntax_diagnostics'):
    shutil.copytree(os.path.join(repo, 'tools/compiler', package), os.path.join(program, package),
                    ignore=shutil.ignore_patterns('tests'))
lock = threading.Lock()
worker = None
serial = 0

def tracked():
    out = subprocess.run(['git', '-C', repo, 'ls-files', '*.chibi'], capture_output=True, text=True, check=True).stdout
    return [p for p in out.splitlines() if p]

files = tracked()
allowed = set(files)

def ensure_worker():
    global worker
    if worker is None or worker.poll() is not None:
        for leftover in ('request', 'response'):
            try: os.remove(os.path.join(mailbox, leftover))
            except FileNotFoundError: pass
        print('starting formatter (about 25 s)...', flush=True)
        worker = subprocess.Popen([chibi, 'run', program, '--', mailbox])

def format_source(source, options):
    """Formats `source` with `options` (a dict of formatter option names to values) in the resident
    formatter. Returns {ok, text|error, original_html, formatted_html}."""
    global serial
    pairs = ' '.join(f'{k}={str(v).lower() if isinstance(v, bool) else v}' for k, v in options.items())
    if '\n' in pairs:
        return {'ok': False, 'error': 'Invalid options'}
    with lock:
        ensure_worker()
        serial += 1
        ident = str(serial)
        tmp = os.path.join(mailbox, 'request.tmp')
        with open(tmp, 'w', encoding='utf-8', newline='') as f:
            f.write(f'{ident}\n{pairs}\n{source}')
        os.replace(tmp, os.path.join(mailbox, 'request'))
        response = os.path.join(mailbox, 'response')
        deadline = time.time() + 180
        while time.time() < deadline:
            if worker.poll() is not None and not os.path.exists(response):
                return {'ok': False, 'error': 'The formatter stopped; try again'}
            if os.path.exists(response):
                with open(response, 'rb') as f:
                    data = f.read()
                if data.endswith(b'\n--end--'):
                    os.remove(response)
                    head, status, sizes, body = data.split(b'\n', 3)
                    if head.decode() != ident:
                        continue
                    a, b, c = (int(n) for n in sizes.split())
                    parts = [body[:a], body[a:a + b], body[a + b:a + b + c]]
                    text, original, formatted = (part.decode('utf-8') for part in parts)
                    if status == b'ok':
                        return {'ok': True, 'text': text, 'original_html': original, 'formatted_html': formatted}
                    return {'ok': False, 'error': text, 'original_html': original}
            time.sleep(0.01)
        return {'ok': False, 'error': 'The formatter did not answer in time'}

class Handler(http.server.BaseHTTPRequestHandler):
    def send(self, code, body, kind):
        data = body.encode('utf-8') if isinstance(body, str) else body
        self.send_response(code)
        self.send_header('content-type', kind)
        self.send_header('content-length', str(len(data)))
        self.send_header('cache-control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == '/api/files':
            listing = [{'path': p, 'bytes': os.path.getsize(os.path.join(repo, p))} for p in files]
            return self.send(200, json.dumps({'files': listing}), 'application/json')
        if url.path == '/api/file':
            path = urllib.parse.parse_qs(url.query).get('path', [''])[0]
            if path not in allowed:
                return self.send(404, 'unknown file', 'text/plain')
            with open(os.path.join(repo, path), encoding='utf-8', newline='') as f:
                return self.send(200, f.read(), 'text/plain; charset=utf-8')
        name = {'/': 'index.html'}.get(url.path, url.path.lstrip('/'))
        # The page's own files, and the documentation theme it shares with the site.
        assets = {'theme.css': os.path.join(page, '..', 'theme.css')}
        for own in ('index.html', 'app.js', 'app.css', 'diff.js', 'options.js', 'tree.js'):
            assets[own] = os.path.join(page, own)
        if name in assets:
            kind = {'html': 'text/html', 'js': 'text/javascript', 'css': 'text/css'}[name.rsplit('.', 1)[1]]
            with open(assets[name], 'rb') as f:
                return self.send(200, f.read(), kind + '; charset=utf-8')
        self.send(404, 'not found', 'text/plain')

    def do_POST(self):
        if self.path != '/api/format':
            return self.send(404, 'not found', 'text/plain')
        body = json.loads(self.rfile.read(int(self.headers['content-length'])))
        source, options = body.get('source', ''), body.get('options', {})
        if not isinstance(options, dict) or len(source) > 400000 or \
                any(not str(k).replace('_', '').isalnum() or not str(v).replace('_', '').isalnum() for k, v in options.items()):
            return self.send(400, json.dumps({'ok': False, 'error': 'Invalid request'}), 'application/json')
        self.send(200, json.dumps(format_source(source, options)), 'application/json')

    def log_message(self, *args): pass

print(f'playground on http://127.0.0.1:{port}/  (mailbox {mailbox})', flush=True)
http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()
