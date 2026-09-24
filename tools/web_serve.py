#!/usr/bin/env python3
"""Serve a consistent browser build with saves routed to the selected local project."""
import argparse
import json
import re
import secrets
import shutil
import tempfile
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


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
    # The engine writes asset documents as YAML mappings.
    if not re.match(r'[A-Za-z_][A-Za-z0-9_]*:', text):
        raise ValueError('Saves must be YAML asset documents')
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as file:
        temporary = Path(file.name)
        file.write(text)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build-web'))
    parser.add_argument('--project', type=Path, default=Path('projects/arena2d'))
    parser.add_argument('--port', type=int, default=8099)
    args = parser.parse_args()
    project = args.project.resolve()
    content = content_directory(project)
    token = secrets.token_urlsafe(32)
    # A served generation never changes under the browser's JS/WASM/data requests.
    with tempfile.TemporaryDirectory(prefix='chibi-web-') as directory:
        snapshot = Path(directory)
        for suffix in ('html', 'js', 'wasm', 'data'):
            shutil.copy2(args.build / f'chibi.{suffix}', snapshot)
        bridge = (Path(__file__).with_name('web_workspace.js')).read_text()
        bridge = bridge.replace('__CHIBI_SAVE_TOKEN__', json.dumps(token))
        html = snapshot / 'chibi.html'
        html.write_text(html.read_text().replace('<script async', f'<script>{bridge}</script><script async'))

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

            def do_GET(self):
                if urlsplit(self.path).path == '/__chibi/assets':
                    if self.headers.get('X-Chibi-Token') != token:
                        self.send_error(403)
                        return
                    assets = {str(p.relative_to(project)): p.read_text()
                              for p in content.rglob('*.yaml') if p.resolve().is_relative_to(content)}
                    assets['project.json'] = (project / 'project.json').read_text()
                    body = json.dumps(assets).encode()
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/json')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                # Ignore validators left over from another preview server.
                if 'If-Modified-Since' in self.headers:
                    del self.headers['If-Modified-Since']
                super().do_GET()

            def do_POST(self):
                if self.path != '/__chibi/save' or self.headers.get('X-Chibi-Token') != token:
                    self.send_error(403)
                    return
                try:
                    length = int(self.headers.get('Content-Length', '0'))
                except ValueError:
                    self.send_error(400, 'Invalid content length')
                    return
                if not 0 < length <= 16 * 1024 * 1024:
                    self.send_error(413)
                    return
                try:
                    save_asset(project, content, json.loads(self.rfile.read(length)))
                except (ValueError, KeyError, TypeError, OSError) as error:
                    self.send_error(400, str(error))
                    return
                self.send_response(204)
                self.end_headers()

        print(f'Editor: http://127.0.0.1:{args.port}/chibi.html', flush=True)
        print(f'Saves: {content}', flush=True)
        ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
