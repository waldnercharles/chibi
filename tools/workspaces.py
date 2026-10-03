#!/usr/bin/env python3
"""Local workspaces shared by desktop agents; source checkout paths stay stable."""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

VERSION = 1
MARKER = '.chibi-workspace.json'


def state_home():
    return Path(os.environ.get('CHIBI_WORKSPACE_HOME', '~/.local/share/chibi-workspaces')).expanduser().resolve()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as out:
            json.dump(value, out, indent=2)
            out.write('\n')
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


@contextlib.contextmanager
def registry_lock():
    home = state_home()
    home.mkdir(parents=True, exist_ok=True)
    with (home / 'registry.lock').open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = home / 'registry.json'
        registry = json.loads(path.read_text()) if path.exists() else {'version': VERSION, 'repos': {}, 'workspaces': {}}
        if registry.get('version') != VERSION:
            raise ValueError('unsupported workspace registry version')
        yield registry
        atomic_json(path, registry)


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True, stderr=subprocess.PIPE).strip()


def repository(path):
    root = Path(git(path, 'rev-parse', '--show-toplevel')).resolve()
    common = Path(git(root, 'rev-parse', '--path-format=absolute', '--git-common-dir')).resolve()
    repo_id = hashlib.sha256(str(common).encode()).hexdigest()[:16]
    return root, common, repo_id


def init_repository(registry, path, limit=4):
    root, common, repo_id = repository(path)
    if repo_id not in registry['repos']:
        registry['repos'][repo_id] = {'root': str(root), 'common_dir': str(common),
                                    'pool_root': str(state_home() / 'checkouts' / repo_id), 'limit': limit}
        # Shared exclude covers every linked checkout without modifying tracked files.
        exclude = common / 'info' / 'exclude'
        exclude.parent.mkdir(parents=True, exist_ok=True)
        content = exclude.read_text() if exclude.exists() else ''
        if '/' + MARKER not in content.splitlines():
            with exclude.open('a') as out:
                out.write(('\n' if content and not content.endswith('\n') else '') + '/' + MARKER + '\n')
    return repo_id


def workspace_for_path(registry, path):
    candidate = Path(path).resolve()
    matches = [entry for entry in registry['workspaces'].values()
               if candidate == Path(entry['path']) or Path(entry['path']) in candidate.parents]
    return max(matches, key=lambda entry: len(entry['path'])) if matches else None


def marker_path(entry):
    return Path(entry['path']) / MARKER


def validate_marker(entry):
    root = Path(entry['path'])
    marker = json.loads(marker_path(entry).read_text())
    stat = root.stat()
    if (root.resolve() != root or root.is_symlink() or marker.get('version') != VERSION or marker.get('id') != entry['id']
            or marker.get('state_home') != str(state_home()) or marker.get('path') != str(root)
            or marker.get('device') != stat.st_dev or marker.get('inode') != stat.st_ino):
        raise ValueError('workspace identity changed: ' + str(root))
    return marker


@contextlib.contextmanager
def workspace_lock(entry, blocking=True):
    validate_marker(entry)
    lock_path = state_home() / 'locks' / (entry['id'] + '.lock')
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | (0 if blocking else fcntl.LOCK_NB))
        yield lock


def acquire(task, repo, app=None):
    with registry_lock() as registry:
        repo_id = init_repository(registry, repo)
        candidates = [entry for entry in registry['workspaces'].values()
                      if entry['repo'] == repo_id and entry['task'] == task]
        if candidates:
            entry = candidates[0]
            validate_marker(entry)
            if entry['status'] == 'finished':
                raise ValueError('task is finished; use reopen PATH to resume it')
            if app and entry.get('owner') and entry['owner']['app'] != app:
                raise ValueError('workspace is owned by ' + entry['owner']['app'] + '; release it before handoff')
            if app and entry.get('reserved_app') not in (None, app):
                raise ValueError('workspace is reserved for ' + entry['reserved_app'])
            entry['status'] = 'active'
            entry['reserved_app'] = app or entry.get('reserved_app')
            entry['last_used'] = time.time()
            return dict(entry)
        config = registry['repos'][repo_id]
        active = sum(entry['repo'] == repo_id and entry['status'] != 'finished'
                     for entry in registry['workspaces'].values())
        if active >= config['limit']:
            raise ValueError('workspace limit reached; finish an existing task before acquiring another')
        workspace_id = uuid.uuid4().hex[:12]
        slug = re.sub(r'[^a-z0-9]+', '-', task.lower()).strip('-')[:40] or 'task'
        path = Path(config['pool_root']) / workspace_id
        branch = 'cwaldner/' + slug + '-' + workspace_id
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', '-C', str(repo), 'worktree', 'add', '-b', branch, str(path), 'HEAD'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        stat = path.stat()
        atomic_json(path / MARKER, {'version': VERSION, 'id': workspace_id,
                    'state_home': str(state_home()), 'path': str(path), 'device': stat.st_dev, 'inode': stat.st_ino})
        now = time.time()
        entry = {'id': workspace_id, 'repo': repo_id, 'task': task, 'path': str(path), 'branch': branch,
                 'status': 'active', 'owner': None, 'reserved_app': app, 'revoked_sessions': [],
                 'created': now, 'last_used': now}
        registry['workspaces'][workspace_id] = entry
        return dict(entry)


def change_status(path, action):
    with registry_lock() as registry:
        entry = workspace_for_path(registry, path)
        if entry is None:
            raise ValueError('path is not a managed workspace')
        validate_marker(entry)
        with workspace_lock(entry, blocking=False):
            if action == 'release':
                if entry.get('owner'):
                    owner = entry['owner']
                    entry['revoked_sessions'].append(owner['app'] + ':' + owner['session_id'])
                entry['owner'] = None
                entry['reserved_app'] = None
                if entry['status'] != 'finished':
                    entry['status'] = 'idle'
            elif action == 'finish':
                if entry.get('owner') or entry.get('reserved_app'):
                    raise ValueError('workspace has an active session or app reservation; release it first')
                entry['status'] = 'finished'
                entry['reserved_app'] = None
            elif action == 'reopen':
                if entry['status'] != 'finished':
                    raise ValueError('workspace is already open')
                config = registry['repos'][entry['repo']]
                active = sum(other['repo'] == entry['repo'] and other['status'] != 'finished'
                             for other in registry['workspaces'].values())
                if active >= config['limit']:
                    raise ValueError('workspace limit reached')
                entry['status'] = 'active'
            entry['last_used'] = time.time()
            return dict(entry)


def hook(app, payload):
    event = payload.get('hook_event_name')
    session = payload.get('session_id')
    cwd = payload.get('cwd')
    if not isinstance(cwd, str):
        raise ValueError('hook needs cwd')
    with registry_lock() as registry:
        entry = workspace_for_path(registry, cwd)
        if entry is None:
            return
        if not isinstance(session, str) or not session:
            raise ValueError('hook needs session_id')
        validate_marker(entry)
        owner = {'app': app, 'session_id': session}
        if event == 'SessionEnd':
            if entry.get('owner') == owner:
                entry['owner'] = None
                entry['reserved_app'] = None
                if entry['status'] != 'finished':
                    entry['status'] = 'idle'
                entry['last_used'] = time.time()
            return
        if app + ':' + session in entry.get('revoked_sessions', []):
            raise ValueError('session was released; open a new session to use this workspace')
        if entry['status'] == 'finished':
            raise ValueError('workspace task is finished; reopen it explicitly')
        if entry.get('reserved_app') not in (None, app):
            raise ValueError('workspace is reserved for ' + entry['reserved_app'])
        if entry.get('owner') not in (None, owner):
            raise ValueError('workspace has another active session; release it before handoff')
        if event in ('SessionStart', 'UserPromptSubmit', 'PreToolUse'):
            if entry.get('owner') is None:
                with workspace_lock(entry, blocking=False):
                    entry['owner'] = owner
            entry['status'] = 'active'
            entry['reserved_app'] = app
        entry['last_used'] = time.time()


def inherited_lock(entry):
    raw = os.environ.get('CHIBI_WORKSPACE_LOCK_FD')
    if raw is None:
        return None
    try:
        fd = int(raw)
        actual = os.fstat(fd)
        expected = (state_home() / 'locks' / (entry['id'] + '.lock')).stat()
        if actual.st_dev != expected.st_dev or actual.st_ino != expected.st_ino:
            raise ValueError('inherited workspace lock does not match this workspace')
        with (state_home() / 'locks' / (entry['id'] + '.lock')).open('a+') as probe:
            try:
                fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return fd
            raise ValueError('inherited workspace descriptor has no held lock')
    except (OSError, ValueError) as error:
        raise ValueError('invalid inherited workspace lock') from error


def inherited_fds(*extra):
    fds = set(extra)
    match = re.search(r'--jobserver-(?:auth|fds)=(\d+),(\d+)(?:\s|$)', os.environ.get('MAKEFLAGS', ''))
    if match:
        for raw in match.groups():
            fd = int(raw)
            try:
                os.fstat(fd)
            except OSError:
                continue
            fds.add(fd)
    return tuple(sorted(fds))


def run_command(command):
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        raise ValueError('run needs a command')
    with registry_lock() as registry:
        entry = workspace_for_path(registry, Path.cwd())
        if entry:
            if entry['status'] == 'finished':
                raise ValueError('workspace task is finished; reopen it explicitly')
            entry['last_used'] = time.time()
            entry = dict(entry)
    if entry is None:
        return subprocess.call(command, pass_fds=inherited_fds())
    validate_marker(entry)
    inherited = inherited_lock(entry)
    if inherited is not None:
        return subprocess.call(command, pass_fds=inherited_fds(inherited))
    # Wait outside registry lock; finish/retirement never wait while holding it.
    with workspace_lock(entry) as lock:
        with registry_lock() as registry:
            current = registry['workspaces'][entry['id']]
            if current['status'] == 'finished':
                raise ValueError('workspace task is finished; reopen it explicitly')
        env = dict(os.environ, CHIBI_WORKSPACE_LOCK_FD=str(lock.fileno()))
        try:
            result = subprocess.call(command, env=env, pass_fds=inherited_fds(lock.fileno()))
            if result == 0:
                try:
                    from workspace_retirement import capture_workspace
                except ImportError:
                    pass
                else:
                    capture_workspace(entry['path'], lock_fd=lock.fileno())
            return result
        finally:
            with registry_lock() as registry:
                registry['workspaces'][entry['id']]['last_used'] = time.time()


def open_workspace(entry, app):
    if app == 'codex':
        subprocess.run(['codex', 'app', entry['path']], check=True)
        return
    executable = shutil.which('claude')
    if executable is None:
        raise ValueError('Claude CLI is missing; allocated workspace: ' + entry['path'])
    result = subprocess.run([executable, '--help'], capture_output=True, text=True, check=True)
    if '--desktop' not in result.stdout:
        version = subprocess.run([executable, '--version'], capture_output=True, text=True, check=True)
        match = re.search(r'\b(\d+)\.(\d+)\.(\d+)\b', version.stdout)
        if not match or tuple(map(int, match.groups())) < (2, 1, 285):
            raise ValueError('Claude CLI needs --desktop support (2.1.285+); allocated workspace: ' + entry['path'])
    subprocess.run([executable, '--desktop'], cwd=entry['path'], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    init = sub.add_parser('init')
    init.add_argument('--repo', required=True)
    init.add_argument('--limit', type=int, default=4)
    get = sub.add_parser('acquire')
    get.add_argument('task')
    get.add_argument('--repo', required=True)
    get.add_argument('--app', choices=('codex', 'claude'))
    get.add_argument('--open', action='store_true')
    sub.add_parser('list')
    sub.add_parser('validate-lock')
    for action in ('finish', 'release', 'reopen'):
        sub.add_parser(action).add_argument('path')
    hooks = sub.add_parser('hook')
    hooks.add_argument('app', choices=('codex', 'claude'))
    run = sub.add_parser('run')
    run.add_argument('args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        if args.command == 'init':
            if args.limit < 1:
                raise ValueError('limit must be positive')
            with registry_lock() as registry:
                repo_id = init_repository(registry, args.repo, args.limit)
                registry['repos'][repo_id]['limit'] = args.limit
            print(repo_id)
        elif args.command == 'acquire':
            if args.open and not args.app:
                raise ValueError('--open needs --app')
            entry = acquire(args.task, args.repo, args.app)
            print(entry['path'], flush=True)
            if args.open:
                open_workspace(entry, args.app)
        elif args.command == 'list':
            with registry_lock() as registry:
                print(json.dumps(list(registry['workspaces'].values()), indent=2))
        elif args.command in ('finish', 'release', 'reopen'):
            print(change_status(args.path, args.command)['path'])
        elif args.command == 'hook':
            payload = json.load(sys.stdin)
            try:
                hook(args.app, payload)
            except (ValueError, OSError) as error:
                event = payload.get('hook_event_name')
                if event == 'PreToolUse':
                    print(json.dumps({'hookSpecificOutput': {'hookEventName': event,
                          'permissionDecision': 'deny', 'permissionDecisionReason': str(error)}}))
                    return 0
                if event == 'UserPromptSubmit':
                    print(json.dumps({'decision': 'block', 'reason': str(error)}))
                    return 0
                if event == 'SessionStart':
                    print(json.dumps({'continue': False, 'stopReason': str(error)}))
                    return 0
                print(str(error), file=sys.stderr)
                return 1
            print('{}')
        elif args.command == 'validate-lock':
            with registry_lock() as registry:
                entry = workspace_for_path(registry, Path.cwd())
                if entry is None or inherited_lock(entry) is None:
                    raise ValueError('no valid inherited workspace lock')
        elif args.command == 'run':
            return run_command(args.args)
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('workspaces: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
