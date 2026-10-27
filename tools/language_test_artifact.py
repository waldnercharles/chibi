#!/usr/bin/env python3
"""Stage and verify same-run language test binaries with a fixed file allowlist."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import subprocess
import tempfile
import tarfile

import test_policy

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    'build/test/language/test_language',
    'build/test/language/corpus.txt',
    'build/test/tools/test_tools',
    'build/sdk/chibi-compiler-host',
)


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def regular(path):
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        raise ValueError(f'not a regular file: {path}')


def identity():
    subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--'], cwd=ROOT, check=True)
    compiler = os.environ.get('CC', 'clang')
    resource = Path(command(compiler, '--print-resource-dir')).resolve()
    runtimes = {}
    for pattern in ('**/*asan*.so*', '**/*ubsan*.so*', '**/*asan*.dylib', '**/*ubsan*.dylib'):
        for path in sorted((resource / 'lib').glob(pattern)):
            if path.is_file():
                runtimes[str(path.relative_to(resource))] = digest(path)
    manifest = test_policy.language_manifest()
    return {
        'version': 1,
        'commit': command('git', 'rev-parse', 'HEAD'),
        'source_root': str(ROOT),
        'system': platform.system(),
        'machine': platform.machine(),
        'image': [os.environ.get('ImageOS', ''), os.environ.get('ImageVersion', '')],
        'run': [os.environ.get('GITHUB_RUN_ID', ''), os.environ.get('GITHUB_RUN_ATTEMPT', '')],
        'compiler': command(compiler, '--version'),
        'compiler_resource': str(resource),
        'sanitizer_runtimes': runtimes,
        'language_manifest': manifest,
        'language_manifest_sha256': hashlib.sha256(
            json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
    }


def corpus(path):
    lines = path.read_text().splitlines()
    if not lines:
        raise ValueError('empty language corpus')
    for line in lines:
        source = Path(line)
        if not source.is_absolute() or not source.resolve().is_relative_to(ROOT):
            raise ValueError(f'corpus source outside checkout: {line}')
        regular(source)


def parent_directories(path, root):
    for parent in path.parents:
        if parent == root:
            return
        if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
            raise ValueError(f'unsafe parent directory: {parent}')
    raise ValueError('path outside expected root')


def pack(directory):
    metadata = identity()
    directory.mkdir(parents=True, exist_ok=False)
    records = {}
    for name in FILES:
        source = ROOT / name
        parent_directories(source, ROOT)
        regular(source)
        executable = name != 'build/test/language/corpus.txt'
        if executable and not os.access(source, os.X_OK):
            raise ValueError(f'not executable: {source}')
        destination = directory / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        mode = 0o755 if executable else 0o644
        destination.chmod(mode)
        records[name] = {'sha256': digest(destination), 'mode': mode}
    corpus(directory / 'build/test/language/corpus.txt')
    metadata['files'] = records
    manifest_path = directory / 'manifest.json'
    manifest_path.write_text(json.dumps(metadata, sort_keys=True, indent=2) + '\n')
    manifest_path.chmod(0o644)


def verify(directory):
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('artifact stage must be a directory, not a symlink')
    expected = set(FILES) | {'manifest.json'}
    actual = set()
    for path in directory.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'artifact symlink: {path}')
        if path.is_dir():
            if not any(name.startswith(str(path.relative_to(directory)) + '/') for name in FILES):
                raise ValueError(f'unexpected artifact directory: {path}')
            continue
        regular(path)
        actual.add(str(path.relative_to(directory)))
    if actual != expected:
        raise ValueError('artifact file inventory differs from runtime allowlist')
    metadata = json.loads((directory / 'manifest.json').read_text())
    records = metadata.pop('files')
    if metadata != identity() or set(records) != set(FILES):
        raise ValueError('artifact source, platform, toolchain, run or selector identity mismatch')
    for name in FILES:
        path = directory / name
        mode = 0o644 if name.endswith('/corpus.txt') else 0o755
        if records[name] != {'sha256': digest(path), 'mode': mode} or stat.S_IMODE(path.stat().st_mode) != mode:
            raise ValueError(f'artifact hash or mode mismatch: {name}')
    corpus(directory / 'build/test/language/corpus.txt')


def restore(directory):
    verify(directory)
    for name in FILES:
        destination = ROOT / name
        parent_directories(destination, ROOT)
        if destination.exists() or destination.is_symlink():
            regular(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as temporary:
            scratch = Path(temporary.name)
        try:
            shutil.copyfile(directory / name, scratch)
            scratch.chmod(0o644 if name.endswith('/corpus.txt') else 0o755)
            scratch.replace(destination)
        finally:
            scratch.unlink(missing_ok=True)


def unpack(archive, directory):
    regular(archive)
    if directory.exists() or directory.is_symlink():
        raise ValueError('artifact destination must not exist')
    parent_directories(directory, Path(directory.anchor))
    expected = set(FILES) | {'manifest.json'}
    with tarfile.open(archive, mode='r:') as package:
        members = package.getmembers()
        if len(members) != len(expected) or {member.name for member in members} != expected:
            raise ValueError('archive member inventory differs from runtime allowlist')
        for member in members:
            mode = 0o755 if member.name in FILES and not member.name.endswith('/corpus.txt') else 0o644
            if member.type not in (tarfile.REGTYPE, tarfile.AREGTYPE) or member.sparse is not None or member.mode != mode:
                raise ValueError(f'archive member is not an ordinary file with the required mode: {member.name}')
        # No writes occur until all names, types and modes have been checked.
        directory.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='language-unpack-', dir=directory.parent) as temporary:
            stage = Path(temporary)
            for member in members:
                destination = stage / member.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                with package.extractfile(member) as source, destination.open('xb') as output:
                    shutil.copyfileobj(source, output)
                destination.chmod(member.mode)
            verify(stage)
            if directory.exists() or directory.is_symlink():
                raise ValueError('artifact destination appeared during verification')
            stage.rename(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('pack', 'verify', 'restore', 'unpack'))
    parser.add_argument('path', type=Path)
    parser.add_argument('destination', type=Path, nargs='?')
    args = parser.parse_args()
    if (args.action == 'unpack') != (args.destination is not None):
        parser.error('unpack requires ARCHIVE DIRECTORY; other actions require DIRECTORY only')
    path = args.path.absolute()
    parent_directories(path, Path(path.anchor))
    if args.action == 'unpack':
        unpack(path, args.destination.absolute())
    else:
        globals()[args.action](path)
    print(f'Language artifact {args.action}: verified file inventory and identity')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'Language artifact: {error}')
