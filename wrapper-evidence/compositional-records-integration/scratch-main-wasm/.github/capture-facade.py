#!/usr/bin/env python3
"""Preserve an existing failed facade workload; never compile or reconstruct it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import stat
import subprocess


PAYLOADS = (
    'sdk/generated/assets/input.json',
    'sdk/generated/assets/compiler.chibip',
    'sdk/generated/assets/native_target.json',
    'sdk/chibi-compiler-host',
)
BUILD_FILES = PAYLOADS + (
    'sdk/chibi-build-host',
    'sdk/chibi-compiler-host.chibi-record',
    'sdk/chibi-build-host.chibi-record',
    'sdk/generated/assets/input-inventory.txt',
    'compile_commands.json',
    'link_commands.json',
)
SOURCE_FILES = (
    'tools/compiler/bootstrap/bootstrap.json',
    'tools/compiler/runtime.json',
    'tools/compiler/asset_compiler/CompilerSources.txt',
    'tools/graph/asset_compiler/Generate.chibi',
    'tools/toolchain/AssetCompiler.chibi',
    'tools/graph/engine/Engine.chibi',
)
# Diagnostic capture bounds do not change compiler or build limits.
FILE_BYTES = 64 * 1024 * 1024
TOTAL_BYTES = 256 * 1024 * 1024
ROLE_BYTES = 8 * 1024 * 1024


def git(root, *arguments):
    return subprocess.check_output(
        ['git', *arguments], cwd=root, text=True, timeout=10
    ).strip()


def identity(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def copy_file(parent, relative, destination, limit):
    """A failed or unstable read is recorded, never promoted to capture evidence."""
    try:
        source = parent
        for part in Path(relative).parts:
            source = source / part
            if stat.S_ISLNK(source.lstat().st_mode):
                raise ValueError('symbolic links are not captured')
        flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
        with os.fdopen(os.open(source, flags), 'rb') as stream:
            before = os.fstat(stream.fileno())
            if source.is_symlink() or not stat.S_ISREG(before.st_mode):
                raise ValueError('expected a regular file, without a symbolic link')
            if before.st_size > limit:
                raise ValueError('capture byte limit exceeded')
            destination.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256()
            size = 0
            with destination.open('xb') as output:
                while data := stream.read(min(65536, limit - size + 1)):
                    size += len(data)
                    if size > limit:
                        raise ValueError('capture byte limit exceeded')
                    digest.update(data)
                    output.write(data)
            after = os.fstat(stream.fileno())
        if identity(before) != identity(after) or identity(after) != identity(source.stat()):
            raise ValueError('file changed while being captured')
        destination.chmod(stat.S_IMODE(before.st_mode))
        return {'status': 'captured', 'bytes': size,
                'sha256': digest.hexdigest(),
                'mode': stat.S_IMODE(before.st_mode)}
    except (OSError, ValueError) as error:
        destination.unlink(missing_ok=True)
        return {'status': 'missing' if isinstance(error, FileNotFoundError) else 'error',
                'error': str(error)}


def collect(root, build, output, expected_revision):
    original_root = root.absolute()
    original_build = build.absolute()
    if not original_build.is_relative_to(original_root):
        raise ValueError('build directory must be inside the source checkout')
    path = original_root
    for part in original_build.relative_to(original_root).parts:
        if part == '..':
            raise ValueError('build directory must not escape through parent components')
        path = path / part
        if path.is_symlink():
            raise ValueError('build directory must not contain symbolic links')
    root = original_root.resolve()
    build = original_build.resolve()
    if not build.is_relative_to(root):
        raise ValueError('build directory must be inside the source checkout')
    output = output.resolve()
    if output.is_relative_to(root):
        raise ValueError('capture output must be outside the source checkout')
    output.mkdir(parents=True, exist_ok=False)
    revision = git(root, 'rev-parse', 'HEAD')
    tree = git(root, 'rev-parse', 'HEAD^{tree}')
    status = git(root, 'status', '--porcelain', '--untracked-files=normal')
    receipt = {
        'revision': revision, 'tree': tree,
        'expected_revision': expected_revision,
        'revision_matches': revision == expected_revision, 'clean': not status,
        'source_root': str(root), 'build_root': str(build),
        'platform': platform.uname()._asdict(),
        'logical_cpus': os.cpu_count(),
        'capture_bounds': {'file_bytes': FILE_BYTES, 'total_bytes': TOTAL_BYTES,
                           'role_parse_bytes': ROLE_BYTES},
        'files': {},
        'child_argv': {'status': 'not captured'},
        'child_phases': {'status': 'not captured; use the separate build timing artifact'},
        'compiler_executed_by_collector': False,
    }
    remaining = TOTAL_BYTES
    for prefix, parent, paths in [('build', build, BUILD_FILES), ('source', root, SOURCE_FILES)]:
        for relative in paths:
            key = f'{prefix}/{relative}'
            captured = copy_file(parent, relative, output / key, min(FILE_BYTES, remaining))
            receipt['files'][key] = captured
            remaining -= captured.get('bytes', 0)
    # Success removes transient inputs. A failed later stage may retain
    # packaging input; distinguish its role and preserve all bytes unchanged.
    receipt['input_role'] = 'unavailable'
    if receipt['files'][f'build/{PAYLOADS[0]}']['status'] == 'captured':
        try:
            path = output / 'build' / PAYLOADS[0]
            if path.stat().st_size > ROLE_BYTES:
                raise ValueError('input role inspection byte limit exceeded; raw input retained')
            request = json.loads(path.read_bytes())
            if isinstance(request, list) and len(request) > 4 and isinstance(request[3], list):
                receipt['input_role'] = ('facade-closure' if request[3] and
                                         request[3][0] == 'asset_compiler/Compile.chibi'
                                         else 'other-closure')
            else:
                receipt['input_role'] = 'other; may be a later packaging request'
        except (ValueError, RecursionError) as error:
            receipt['input_role'] = 'unparsed'
            receipt['input_parse_error'] = str(error)
    receipt['payloads_complete'] = all(
        receipt['files'][f'build/{name}']['status'] == 'captured' for name in PAYLOADS
    )
    receipt['source_after'] = {
        'revision': git(root, 'rev-parse', 'HEAD'),
        'tree': git(root, 'rev-parse', 'HEAD^{tree}'),
        'clean': not git(root, 'status', '--porcelain', '--untracked-files=normal'),
    }
    receipt['source_stable'] = receipt['source_after'] == {
        'revision': revision, 'tree': tree, 'clean': receipt['clean']
    }
    receipt['source_bound_payloads'] = (
        receipt['payloads_complete'] and receipt['revision_matches'] and receipt['clean']
        and receipt['source_stable']
    )
    receipt['replay_admission'] = (
        'This is a byte capture, not replay approval. Verify the manifest, actual host '
        'imports/platform, source binding, flags, argv, and operation limits before replay.'
    )
    (output / 'capture.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expected-revision', required=True)
    args = parser.parse_args()
    receipt = collect(args.source_root, args.build, args.output, args.expected_revision)
    print(json.dumps({key: receipt[key] for key in (
        'revision', 'tree', 'revision_matches', 'clean',
        'payloads_complete', 'source_bound_payloads'
    )}))


if __name__ == '__main__':
    main()
