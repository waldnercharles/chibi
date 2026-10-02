#!/usr/bin/env python3
"""Serve a consistent browser build with the workspace's projects and saves routed back to them."""
import argparse
import base64
import errno
import json
import re
import secrets
import shutil
import tempfile
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import parse_qs, urlsplit

import kdl_asset

TEXT_EXTENSIONS = {'.kdl', '.rml', '.rcss'}
IMAGE_EXTENSIONS = {'.png', '.jpg', '.jpeg'}


def content_directory(project):
    manifest = kdl_asset.loads((project / 'project.kdl').read_text())
    content = (project / manifest['content']).resolve()
    if not content.is_relative_to(project):
        raise ValueError('Project content must be inside the project directory')
    return content


def projects(workspace):
    """The workspace's own project and those in its projects directory, as Studio lists them."""
    manifests = [workspace / 'project.kdl', *sorted(workspace.glob('projects/*/project.kdl'))]
    return [manifest.parent for manifest in manifests if manifest.is_file()]


def workspace_files(workspace):
    """Each project's manifest and supported content assets, relative to the workspace."""
    files = {}
    for project in projects(workspace):
        files[str((project / 'project.kdl').relative_to(workspace))] = project / 'project.kdl'
        content = content_directory(project)
        for path in content.rglob('*'):
            if path.is_file() and path.suffix.lower() in TEXT_EXTENSIONS | IMAGE_EXTENSIONS and path.resolve().is_relative_to(content):
                files[str(path.relative_to(workspace))] = path
    return files


def workspace_asset(path):
    if path.suffix.lower() in IMAGE_EXTENSIONS:
        return {'encoding': 'base64', 'data': base64.b64encode(path.read_bytes()).decode('ascii')}
    return path.read_text()


def workspace_assets(workspace):
    return {name: workspace_asset(path) for name, path in workspace_files(workspace).items()}


def create_project(workspace, request):
    """Create a project beside the bundled samples and return its initial files."""
    name = request['name']
    version = request['version']
    if not isinstance(name, str) or not name.strip() or len(name.strip()) > 128:
        raise ValueError('Enter a project name of 1 to 128 characters')
    if not isinstance(version, int) or version < 1:
        raise ValueError('Invalid project version')
    name = name.strip()
    base = re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-') or 'project'
    parent = workspace / 'projects'
    parent.mkdir(parents=True, exist_ok=True)
    project_id = base
    index = 2
    while (parent / project_id).exists():
        project_id = f'{base}-{index}'
        index += 1
    root = parent / project_id
    root.mkdir()
    files = {
        'project.kdl': kdl_asset.dumps({
            'version': version,
            'name': name,
            'content': 'content',
            'start_scene': 'scenes/Start.kdl',
        }),
        'content/scenes/Start.kdl': kdl_asset.dumps({
            'version': 1,
            'name': 'Start',
            'entities': [],
        }),
    }
    try:
        for path, text in files.items():
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text(text)
    except OSError:
        shutil.rmtree(root, ignore_errors=True)
        raise
    return {
        'root': f'/projects/{project_id}/',
        'assets': {f'projects/{project_id}/{path}': text for path, text in files.items()},
    }


class WorkspaceWatch:
    """Versions the workspace's files so the browser can follow edits made on disk."""

    def __init__(self, workspace):
        self.workspace = workspace
        self.version = 0
        self.stamps = self.scan()
        self.changed = {}  # path -> the version that last changed it
        self.condition = threading.Condition()

    def scan(self):
        stamps = {}
        for name, path in workspace_files(self.workspace).items():
            try:
                info = path.stat()
            except OSError:
                continue
            stamps[name] = (info.st_mtime_ns, info.st_size)
        return stamps

    def poll(self):
        stamps = self.scan()
        changed = {name for name in stamps.keys() | self.stamps.keys() if stamps.get(name) != self.stamps.get(name)}
        if not changed:
            return
        with self.condition:
            self.version += 1
            for name in changed:
                self.changed[name] = self.version
            self.stamps = stamps
            self.condition.notify_all()

    def changes(self, since, timeout):
        """Changed files after `since`, as text, a base64 image payload, or None when removed."""
        with self.condition:
            self.condition.wait_for(lambda: self.version > since, timeout)
            version = self.version
            names = [name for name, changed in self.changed.items() if changed > since]
        files = {}
        current = workspace_files(self.workspace)
        roots = [(project.resolve(), content_directory(project)) for project in projects(self.workspace)]
        for name in names:
            path = current.get(name)
            if path is None:
                files[name] = None
                continue
            try:
                # The file may have been replaced after poll() noticed it.
                # Revalidate the resolved target against its project content
                # root immediately before reading, so a swapped symlink is
                # reported as removed instead of mirroring external bytes.
                resolved = path.resolve()
                if not any(
                    resolved == root / 'project.kdl' or resolved.is_relative_to(content)
                    for root, content in roots
                ):
                    files[name] = None
                    continue
                files[name] = workspace_asset(path)
            except (OSError, RuntimeError):
                files[name] = None
        return version, files


def save_asset(workspace, request):
    path = (workspace / request['path'].lstrip('/')).resolve()
    text = request['text']
    contents = [content_directory(project) for project in projects(workspace)]
    # Settings rewrites a project's manifest, which is never removed.
    manifest = text is not None and path in [(project / 'project.kdl').resolve() for project in projects(workspace)]
    image_delete = text is None and path.suffix.lower() in IMAGE_EXTENSIONS
    if not manifest and ((path.suffix.lower() not in TEXT_EXTENSIONS and not image_delete) or
                         not any(path.is_relative_to(content) for content in contents)):
        raise ValueError('Saves must be project manifests, or KDL assets, RML or RCSS inside a project content directory')
    if text is None:
        path.unlink(missing_ok=True)
        return
    # The engine writes asset documents as KDL mappings; a save must reload.
    if path.suffix == '.kdl':
        try:
            document = kdl_asset.loads(text)
        except kdl_asset.Error as error:
            raise ValueError(f'Saves must be KDL asset documents: {error}') from error
        if not isinstance(document, dict):
            raise ValueError('Saves must be KDL asset documents')
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
                        help='directory whose project.kdl and projects/* the browser can open')
    parser.add_argument('--project', type=Path, default=Path('projects/arena2d'))
    parser.add_argument('--port', type=int, default=8099)
    parser.add_argument('--isolated', action='store_true',
                        help='send the isolation headers required by the threaded build')
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    project = args.project.resolve()
    if not project.is_relative_to(workspace):
        parser.error(f'{project} is outside {workspace}')
    # The browser mirrors the workspace at its file system root.
    start = str(PurePosixPath('/') / project.relative_to(workspace).as_posix())
    token = secrets.token_urlsafe(32)
    watch = WorkspaceWatch(workspace)

    # Once a second, as native Studio polls its files; each scan walks every project.
    def follow():
        while True:
            watch.poll()
            threading.Event().wait(1)

    # A served generation never changes under the browser's JS/WASM/data requests.
    with tempfile.TemporaryDirectory(prefix='chibi-web-') as directory:
        snapshot = Path(directory)
        for suffix in ('html', 'js', 'wasm', 'data'):
            shutil.copy2(args.build / f'chibi.{suffix}', snapshot)
        for name in ('chibi-default.js', 'chibi-isolation.js', 'chibi.worker.js'):
            if (args.build / name).is_file():
                shutil.copy2(args.build / name, snapshot)
        if (args.build / 'threads').is_dir():
            shutil.copytree(args.build / 'threads', snapshot / 'threads')
        bridge = (Path(__file__).with_name('web_workspace.js')).read_text()
        bridge = bridge.replace('__CHIBI_SAVE_TOKEN__', json.dumps(token))
        bridge = bridge.replace('__CHIBI_PROJECT__', json.dumps(start))
        html = snapshot / 'chibi.html'
        entries = [html]
        if (snapshot / 'threads/chibi.html').is_file():
            entries.append(snapshot / 'threads/chibi.html')
        for html in entries:
            text = html.read_text()
            entry = '<script async'
            for script in ('chibi-isolation.js', 'chibi-default.js'):
                if (html.parent / script).is_file():
                    entry = f'<script src="{script}"'
                    break
            html.write_text(text.replace(entry, f'<script>{bridge}</script>{entry}'))

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
                if args.isolated:
                    self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
                    self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
                super().end_headers()

            def send_json(self, value, version, status=200):
                body = json.dumps(value).encode()
                self.send_response(status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('X-Chibi-Version', str(version))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                url = urlsplit(self.path)
                if url.path in ('/__chibi/assets', '/__chibi/changes') and self.headers.get('X-Chibi-Token') != token:
                    self.send_error(403)
                    return
                if url.path == '/__chibi/assets':
                    version = watch.version
                    self.send_json(workspace_assets(workspace), version)
                    return
                # Waits for the next edit on disk, so the page can mirror it at once.
                if url.path == '/__chibi/changes':
                    try:
                        since = int(parse_qs(url.query)['since'][0])
                    except (KeyError, ValueError):
                        self.send_error(400, 'since is required')
                        return
                    version, files = watch.changes(since, 20)
                    self.send_json(files, version)
                    return
                # Ignore validators left over from another preview server.
                if 'If-Modified-Since' in self.headers:
                    del self.headers['If-Modified-Since']
                super().do_GET()

            def do_POST(self):
                if self.path not in ('/__chibi/save', '/__chibi/project') or self.headers.get('X-Chibi-Token') != token:
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
                body = self.rfile.read(length)
                if self.path == '/__chibi/project':
                    try:
                        created = create_project(workspace, json.loads(body))
                        watch.poll()
                    except (ValueError, KeyError, TypeError, OSError) as error:
                        self.send_json({'error': str(error)}, watch.version, 400)
                        return
                    self.send_json(created, watch.version)
                    return
                try:
                    save_asset(workspace, json.loads(body))
                except (ValueError, KeyError, TypeError, OSError) as error:
                    self.send_error(400, str(error))
                    return
                self.send_response(204)
                self.end_headers()

        try:
            http = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
        except OSError as error:
            if error.errno != errno.EADDRINUSE:
                raise
            parser.error(
                f'port {args.port} is already in use; retry with another port, '
                f'for example: make web-serve WEB_PORT={args.port + 1}'
            )
        threading.Thread(target=follow, daemon=True).start()
        print(f'Editor: http://127.0.0.1:{args.port}/chibi.html', flush=True)
        print(f'Project: {project}', flush=True)
        http.serve_forever()


if __name__ == '__main__':
    main()
