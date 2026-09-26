#!/usr/bin/env python3
"""Serve a consistent browser build with the workspace's projects and saves routed back to them."""
import argparse
import json
import secrets
import shutil
import tempfile
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit

import yaml


def content_directory(project):
    manifest = json.loads((project / 'project.json').read_text())
    content = (project / manifest['content']).resolve()
    if not content.is_relative_to(project):
        raise ValueError('Project content must be inside the project directory')
    return content


def projects(workspace):
    """The workspace's own project and those in its projects directory, as Studio lists them."""
    manifests = [workspace / 'project.json', *sorted(workspace.glob('projects/*/project.json'))]
    return [manifest.parent for manifest in manifests if manifest.is_file()]


def workspace_assets(workspace):
    assets = {}
    for project in projects(workspace):
        assets[str((project / 'project.json').relative_to(workspace))] = (project / 'project.json').read_text()
        content = content_directory(project)
        for path in content.rglob('*.yaml'):
            if path.resolve().is_relative_to(content):
                assets[str(path.relative_to(workspace))] = path.read_text()
    return assets


def save_asset(workspace, request):
    path = (workspace / request['path'].lstrip('/')).resolve()
    contents = [content_directory(project) for project in projects(workspace)]
    if path.suffix != '.yaml' or not any(path.is_relative_to(content) for content in contents):
        raise ValueError('Saves must be YAML assets inside a project content directory')
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build-web'))
    parser.add_argument('--workspace', type=Path, default=Path(__file__).resolve().parents[1],
                        help='directory whose project.json and projects/* the browser can open')
    parser.add_argument('--project', type=Path, default=Path('projects/arena2d'))
    parser.add_argument('--port', type=int, default=8099)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    project = args.project.resolve()
    if not project.is_relative_to(workspace):
        parser.error(f'{project} is outside {workspace}')
    # The browser mirrors the workspace at its file system root.
    start = str(PurePosixPath('/') / project.relative_to(workspace).as_posix())
    token = secrets.token_urlsafe(32)
    # A served generation never changes under the browser's JS/WASM/data requests.
    with tempfile.TemporaryDirectory(prefix='chibi-web-') as directory:
        snapshot = Path(directory)
        for suffix in ('html', 'js', 'wasm', 'data'):
            shutil.copy2(args.build / f'chibi.{suffix}', snapshot)
        bridge = (Path(__file__).with_name('web_workspace.js')).read_text()
        bridge = bridge.replace('__CHIBI_SAVE_TOKEN__', json.dumps(token))
        bridge = bridge.replace('__CHIBI_PROJECT__', json.dumps(start))
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
                    body = json.dumps(workspace_assets(workspace)).encode()
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
                    save_asset(workspace, json.loads(self.rfile.read(length)))
                except (ValueError, KeyError, TypeError, OSError) as error:
                    self.send_error(400, str(error))
                    return
                self.send_response(204)
                self.end_headers()

        print(f'Editor: http://127.0.0.1:{args.port}/chibi.html', flush=True)
        print(f'Project: {project}', flush=True)
        ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
