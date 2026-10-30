#!/usr/bin/env python3
"""Interim, local-only host for this playground until a Chibi formatter runs in the browser: static page, repo file list, and a resident
`chibi run apps/fmt -- --serve DIR` process reached through a mailbox directory.
Usage: serve.py REPO CHIBI PORT, with CHIBI the built SDK launcher, e.g. build-release/sdk/chibi."""
import json, os, subprocess, sys, tempfile, threading, time, http.server, urllib.parse

repo, chibi, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
page = os.path.join(repo, 'tools/docs/playground')
fmt_dir = os.path.join(os.path.dirname(chibi), '..', 'share/chibi/apps/fmt')
mailbox = tempfile.mkdtemp(prefix='chibi-playground-')
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
        worker = subprocess.Popen([chibi, 'run', fmt_dir, '--', '--serve', mailbox])

def format_source(source, indent, tabs, blanks, crlf):
    global serial
    with lock:
        ensure_worker()
        serial += 1
        ident = str(serial)
        tmp = os.path.join(mailbox, 'request.tmp')
        with open(tmp, 'w', encoding='utf-8', newline='') as f:
            f.write(f'{ident} {indent} {1 if tabs else 0} {blanks} {1 if crlf else 0}\n{source}')
        os.replace(tmp, os.path.join(mailbox, 'request'))
        response = os.path.join(mailbox, 'response')
        deadline = time.time() + 180
        while time.time() < deadline:
            if worker.poll() is not None and not os.path.exists(response):
                return {'ok': False, 'error': 'The formatter stopped; try again'}
            if os.path.exists(response):
                with open(response, encoding='utf-8', newline='') as f:
                    text = f.read()
                if text.endswith('\n--end--'):
                    os.remove(response)
                    head, status, body = text.split('\n', 2)
                    body = body[:-len('\n--end--')]
                    if head != ident:
                        continue
                    return {'ok': True, 'text': body} if status == 'ok' else {'ok': False, 'error': body}
            time.sleep(0.02)
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
        if name in ('index.html', 'app.js', 'app.css', 'diff.js', 'highlight.js', 'options.js', 'tree.js'):
            kind = {'html': 'text/html', 'js': 'text/javascript', 'css': 'text/css'}[name.rsplit('.', 1)[1]]
            with open(os.path.join(page, name), 'rb') as f:
                return self.send(200, f.read(), kind + '; charset=utf-8')
        self.send(404, 'not found', 'text/plain')

    def do_POST(self):
        if self.path != '/api/format':
            return self.send(404, 'not found', 'text/plain')
        body = json.loads(self.rfile.read(int(self.headers['content-length'])))
        indent = int(body.get('indent_width', 4))
        blanks = int(body.get('max_blank_lines', 1))
        if not 1 <= indent <= 16 or not 0 <= blanks <= 3 or len(body['source']) > 400000:
            return self.send(400, json.dumps({'ok': False, 'error': 'Invalid request'}), 'application/json')
        self.send(200, json.dumps(format_source(body['source'], indent, bool(body.get('use_tabs')), blanks, bool(body.get('crlf')))), 'application/json')

    def log_message(self, *args): pass

print(f'playground on http://127.0.0.1:{port}/  (mailbox {mailbox})', flush=True)
http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()
