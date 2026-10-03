#!/usr/bin/env python3
"""Retire verified generated outputs of explicitly managed workspace builds."""
import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import time

import workspaces

MARKER = '.chibi-managed-build.json'
SUFFIXES = {'.o', '.obj', '.a', '.so', '.dylib', '.dll', '.exe', '.wasm', '.js'}


def home():
    return Path(os.environ.get('CHIBI_WORKSPACE_HOME',
                              '~/.local/share/chibi-workspaces')).expanduser().resolve()


def workspace(registry, path):
    path = Path(path).resolve()
    candidates = [entry for entry in registry['workspaces'].values()
                  if entry.get('status') != 'retired'
                  and path.is_relative_to(Path(entry['path']).resolve())]
    if not candidates:
        return None
    parent = path
    while not parent.is_dir():
        if parent == parent.parent:
            return None
        parent = parent.parent
    try:
        git = subprocess.run(['git', '-C', str(parent), 'rev-parse', '--show-toplevel'],
                             text=True, capture_output=True, check=True, timeout=20)
        root = Path(git.stdout.strip()).resolve()
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    candidates = [entry for entry in candidates if Path(entry['path']).resolve() == root]
    # Two live identities at one path are ambiguous; never borrow a stale one.
    return candidates[0] if len(candidates) == 1 else None


def ordinary_path(path, root):
    """Do not follow symlinks or accept the source checkout itself."""
    if not path.is_absolute() or path == root or not path.is_relative_to(root):
        return False
    current = root
    for part in path.relative_to(root).parts:
        current /= part
        if current.is_symlink():
            return False
    return True


def signature(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        return None
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    after = path.lstat()
    identity = (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)
    if identity != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        return None
    return [*identity, digest.hexdigest()]


def lease_inherited(entry, lock_fd=None):
    value = os.environ.get('CHIBI_WORKSPACE_LOCK_FD', '') if lock_fd is None else lock_fd
    try:
        fd = int(value)
        actual = os.fstat(fd)
        expected = (home() / 'locks' / (entry['id'] + '.lock')).stat()
        if (actual.st_dev, actual.st_ino) != (expected.st_dev, expected.st_ino):
            return False
        # A separate descriptor cannot acquire the lock if an inherited lease
        # really exists. Merely naming an unlocked descriptor is insufficient.
        with (home() / 'locks' / (entry['id'] + '.lock')).open('a+') as probe:
            try:
                fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return False
            except BlockingIOError:
                # A blocked independent probe only proves somebody holds a
                # lock. The supplied descriptor must itself own that lease.
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return True
    except (ValueError, OSError):
        return False


def register(build_dir):
    path = Path(os.path.abspath(build_dir))
    with workspaces.registry_lock() as registry:
        snapshot = {'workspaces': dict(registry['workspaces'])}
    discovered = workspace(snapshot, path)
    if discovered is None:
        return False
    with workspaces.registry_lock() as registry:
        entry = registry['workspaces'].get(discovered['id'])
        if (entry is None or entry.get('status') == 'retired'
                or entry['path'] != discovered['path'] or not lease_inherited(entry)):
            return False
        if entry.get('lifecycle') == 'app' and not entry.get('build_integration'):
            return False
        workspaces.validate_marker(entry)
        root = Path(entry['path']).resolve()
        if not ordinary_path(path, root):
            return False
        builds = registry.setdefault('builds', {})
        if str(path) in builds:
            previous = builds[str(path)]
            if previous['workspace_id'] == entry['id']:
                return owned(previous, entry)
            old_workspace = registry['workspaces'].get(previous['workspace_id'], {})
            if old_workspace.get('status') != 'retired' or path.exists():
                return False
            registry.setdefault('retired_builds', {})[previous['workspace_id'] + ':' + str(path)] = previous
            del builds[str(path)]
        # Never infer ownership of existing output, even an empty directory.
        if path.exists():
            return False
        path.mkdir(parents=True)
        marker = {'version': 1, 'workspace_id': entry['id'], 'state_home': str(home())}
        (path / MARKER).write_text(json.dumps(marker) + '\n')
        info = path.stat()
        builds[str(path)] = {'path': str(path), 'workspace_id': entry['id'],
                             'device': info.st_dev, 'inode': info.st_ino,
                             'last_used': time.time(), 'outputs': {}}
        return True


def owned(record, entry):
    path = Path(record['path'])
    root = Path(entry['path']).resolve()
    if not ordinary_path(path, root):
        return False
    try:
        workspaces.validate_marker(entry)
        info = path.stat()
        marker = json.loads((path / MARKER).read_text())
        return (info.st_dev == record['device'] and info.st_ino == record['inode']
                and marker == {'version': 1, 'workspace_id': entry['id'],
                               'state_home': str(home())})
    except (OSError, ValueError):
        return False


def inspect_build(record, entry, foreign_references=False):
    path = Path(record['path'])
    root = Path(entry['path']).resolve()
    if not owned(record, entry):
        raise ValueError('build ownership changed')
    cache = path / 'CMakeCache.txt'
    if cache.is_symlink() or (path / 'build.ninja').is_symlink():
        raise ValueError('symlinked configuration')
    values = {}
    for line in cache.read_text().splitlines():
        if ':' in line and '=' in line:
            values[line.split(':', 1)[0]] = line.split('=', 1)[1]
    if Path(values.get('CMAKE_HOME_DIRECTORY', '')).resolve() != root:
        raise ValueError('foreign CMake source directory')
    cache_home = Path(os.environ.get('XDG_CACHE_HOME', '~/.cache')).expanduser()
    cache_roots = [Path(os.environ.get(variable, str(cache_home / name))).expanduser().resolve()
                   for variable, name in [('CHIBI_DEPENDENCY_PACKAGE_CACHE_DIR', 'chibi-dependency-packages'),
                                          ('CHIBI_FETCHCONTENT_CACHE_DIR', 'chibi-fetchcontent'),
                                          ('CCACHE_DIR', 'ccache')]]
    if values.get('CHIBI_DEPENDENCY_PACKAGE'):
        cache_roots.append(Path(values['CHIBI_DEPENDENCY_PACKAGE']).resolve())
    git = subprocess.run(['git', '-C', str(root), 'worktree', 'list', '--porcelain'],
                         text=True, capture_output=True, check=True, timeout=20)
    for line in git.stdout.splitlines():
        if line.startswith('worktree '):
            nested = Path(line[9:]).resolve()
            if nested.is_relative_to(path):
                raise ValueError('contains a registered worktree')
            if foreign_references and nested != root:
                try:
                    nested.stat()
                except FileNotFoundError:
                    # A stale registration has no checkout/cache to consume
                    # these outputs. Other inspection errors remain fatal.
                    continue
                caches = subprocess.run(['rg', '--files', '--hidden', '--no-ignore',
                                         '-g', 'CMakeCache.txt', '-g', '!.git/**', str(nested)],
                                        text=True, capture_output=True, timeout=20)
                if caches.returncode not in (0, 1) or caches.stderr:
                    raise ValueError('cannot inspect foreign CMake references')
                for name in caches.stdout.splitlines():
                    content = Path(name).read_text()
                    if str(path) + '/' in content or '=' + str(path) + '\n' in content:
                        raise ValueError('referenced by another checkout CMake cache')
    # A separate repository may not be registered with the parent repository.
    def scan_error(error):
        raise error
    for current, dirs, files in os.walk(path, followlinks=False, onerror=scan_error):
        if '.git' in dirs or '.git' in files:
            raise ValueError('contains a repository')
    tracked = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--',
                              str(path.relative_to(root))], capture_output=True, check=True, timeout=20)
    tracked_paths = {root / os.fsdecode(name) for name in tracked.stdout.split(b'\0') if name}
    ninja = subprocess.run(['ninja', '-C', str(path), '-t', 'targets', 'all'],
                           text=True, capture_output=True, check=True, timeout=20)
    outputs = set()
    for line in ninja.stdout.splitlines():
        name, separator, rule = line.rpartition(': ')
        if not separator or rule == 'phony':
            continue
        candidate = Path(name)
        if not candidate.is_absolute():
            candidate = path / candidate
        candidate = Path(os.path.abspath(candidate))
        if not ordinary_path(candidate, path) or candidate in tracked_paths:
            continue
        if any(candidate.is_relative_to(cache_root) for cache_root in cache_roots):
            continue
        try:
            mode = candidate.lstat().st_mode
            if candidate.suffix in SUFFIXES or mode & 0o111:
                if stat.S_ISREG(mode):
                    outputs.add(candidate)
        except FileNotFoundError:
            continue
    return outputs


def capture_record(record, entry):
    outputs = inspect_build(record, entry)
    captured = {}
    for path in outputs:
        previous = record.get('outputs', {}).get(str(path.relative_to(record['path'])))
        value = previous if previous is not None and unchanged(path, previous) else signature(path)
        if value is not None:
            captured[str(path.relative_to(record['path']))] = value
    record['outputs'] = captured
    record['last_used'] = time.time()
    return len(captured)


def capture_workspace(path, lock_fd=None):
    with workspaces.registry_lock() as registry:
        snapshot = {'workspaces': dict(registry['workspaces'])}
    discovered = workspace(snapshot, path)
    if discovered is None:
        return 0
    with workspaces.registry_lock() as registry:
        entry = registry['workspaces'].get(discovered['id'])
        if (entry is None or entry.get('status') == 'retired'
                or entry['path'] != discovered['path'] or not lease_inherited(entry, lock_fd)):
            return 0
        entry = dict(entry)
        records = json.loads(json.dumps([record for record in registry.get('builds', {}).values()
                                        if record['workspace_id'] == entry['id']]))
    total = 0
    for record in records:
        try:
            count = capture_record(record, entry)
            with workspaces.registry_lock() as registry:
                current = registry.get('builds', {}).get(record['path'])
                current_entry = registry['workspaces'].get(entry['id'], {})
                if (current is not None and current['workspace_id'] == entry['id']
                        and current_entry.get('status') != 'retired'
                        and (current['device'], current['inode']) == (record['device'], record['inode'])):
                    current.update(outputs=record['outputs'], last_used=record['last_used'])
                    total += count
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            print(f'Build retirement capture skipped: {error}', file=sys.stderr)
    return total


def unchanged(path, expected):
    try:
        info = path.lstat()
        return (stat.S_ISREG(info.st_mode) and info.st_nlink == 1
                and [info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns] == expected[:4])
    except OSError:
        return False


def process_idle(path):
    """No activity is stronger than a successful scan with no foreign PID."""
    if shutil.which('lsof') is None:
        return False
    try:
        result = subprocess.run(['lsof', '-n', '-P', '-F', 'p', '+D', str(path)],
                                capture_output=True, text=True, timeout=20)
        if result.stderr or result.returncode not in (0, 1):
            return False
        pids = {int(line[1:]) for line in result.stdout.splitlines()
                if line.startswith('p')}
        return not (pids - {os.getpid()})
    except (OSError, ValueError, subprocess.SubprocessError):
        return False


def app_activity(entry):
    """All app sessions contribute activity, even when one app has stopped."""
    used = entry.get('last_used', 0)
    sessions = entry.get('sessions', {})
    if not isinstance(sessions, dict):
        return True, float('inf')
    active = False
    for session in sessions.values():
        if not isinstance(session, dict):
            return True, float('inf')
        active |= session.get('active') is not False
        seen = session.get('last_seen', 0)
        if not isinstance(seen, (int, float)) or not math.isfinite(seen):
            return True, float('inf')
        used = max(used, seen)
    return active, used


def eligible(registry, record, keep, idle_days):
    usage = {}
    for build in registry.get('builds', {}).values():
        identifier = build['workspace_id']
        entry = registry['workspaces'].get(identifier)
        if entry is None or entry.get('status') == 'retired':
            continue
        try:
            Path(entry['path']).stat()
        except FileNotFoundError:
            continue
        except OSError:
            # An inaccessible checkout is not evidence that it is absent.
            pass
        _, used = app_activity(entry)
        usage[identifier] = max(usage.get(identifier, 0), build['last_used'], used)
    recent = set(sorted(usage, key=usage.get, reverse=True)[:keep])
    entry = registry['workspaces'].get(record['workspace_id'])
    if entry is None:
        return False
    active, used = app_activity(entry)
    if active or (entry.get('lifecycle') == 'app' and not entry.get('build_integration')):
        return False
    return (entry is not None and entry['id'] not in recent and not entry.get('owner')
            and not entry.get('reserved_app') and entry.get('status') in ('active', 'idle', 'finished')
            and time.time() - max(record['last_used'], used) >= idle_days * 86400)


def sweep(apply=False, keep=3, idle_days=7):
    if keep < 0 or idle_days < 0:
        raise ValueError('retention values must be nonnegative')
    with workspaces.registry_lock() as registry:
        snapshot = json.loads(json.dumps(registry))
    results = []
    for record in snapshot.get('builds', {}).values():
        if not eligible(snapshot, record, keep, idle_days):
            continue
        entry = snapshot['workspaces'][record['workspace_id']]
        lock_path = home() / 'locks' / (entry['id'] + '.lock')
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        with lock_path.open('a+') as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                continue
            try:
                if not process_idle(entry['path']):
                    continue
                outputs = inspect_build(record, entry, foreign_references=True)
                prepared = {}
                for name, expected in record['outputs'].items():
                    path = Path(record['path']) / name
                    if path in outputs and signature(path) == expected:
                        prepared[name] = expected
                # Hashes, Ninja and lsof run outside the global registry mutex.
                # Hold the workspace lease while rechecking current session
                # ownership and performing short stat/unlink operations.
                with workspaces.registry_lock() as registry:
                    current = registry.get('builds', {}).get(record['path'])
                    if current is None or not eligible(registry, current, keep, idle_days):
                        continue
                    if not owned(current, registry['workspaces'][entry['id']]):
                        continue
                    count = size = 0
                    for name, expected in prepared.items():
                        path = Path(record['path']) / name
                        if current['outputs'].get(name) != expected or not unchanged(path, expected):
                            continue
                        size += expected[2]
                        count += 1
                        if apply:
                            path.unlink()
                            del current['outputs'][name]
                    if count:
                        results.append({'path': record['path'], 'files': count,
                                        'bytes': size, 'applied': apply})
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                results.append({'path': record['path'], 'skipped': str(error)})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('register').add_argument('build_dir')
    commands.add_parser('capture').add_argument('workspace', nargs='?', default='.')
    retire = commands.add_parser('sweep')
    retire.add_argument('--apply', action='store_true')
    retire.add_argument('--keep', type=int, default=3)
    retire.add_argument('--idle-days', type=float, default=7)
    args = parser.parse_args()
    if args.command == 'register':
        register(args.build_dir)
    elif args.command == 'capture':
        capture_workspace(args.workspace)
    else:
        results = sweep(args.apply, args.keep, args.idle_days)
        if results:
            print(json.dumps(results))


if __name__ == '__main__':
    main()
