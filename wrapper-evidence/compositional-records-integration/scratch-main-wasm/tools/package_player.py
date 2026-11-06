#!/usr/bin/env python3
"""Deploy the actual player from a verified installed runtime SDK."""

import argparse
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile
import sys

# Installed helpers must leave the checksum-pinned package unchanged.
sys.dont_write_bytecode = True

from package_sdk import digest, package_files, package_identity, staging_directory

MANIFEST = Path('share/chibi/package.json')
ENGINE = Path('share/chibi/engine.json')


def valid_path(name):
    if not isinstance(name, str):
        raise ValueError('Package file path must be text')
    relative = PurePosixPath(name)
    if (relative.is_absolute() or '..' in relative.parts or '\\' in name
            or relative.as_posix() != name or name in ('', '.')):
        raise ValueError(f'Invalid package path: {name}')


def verify_files(package, files, manifest=MANIFEST):
    if not isinstance(files, dict) or not files:
        raise ValueError('Package file inventory is missing')
    paths = list(package.rglob('*'))
    if (package.is_symlink() or any(path.is_symlink() or not (path.is_file() or path.is_dir())
                                    for path in paths)):
        raise ValueError('Packages must contain regular files, not symlinks')
    actual = {path.relative_to(package).as_posix() for path in paths if path.is_file()}
    if actual != set(files) | {manifest.as_posix()}:
        raise ValueError('Package file inventory does not match its contents')
    for name, record in files.items():
        valid_path(name)
        if name == manifest.as_posix():
            raise ValueError('Package inventory cannot include its own manifest')
        path = package / name
        if (not isinstance(record, dict) or type(record.get('bytes')) is not int
                or record['bytes'] < 0 or record['bytes'] != path.stat().st_size
                or record.get('sha256') != digest(path)):
            raise ValueError(f'Package checksum/length mismatch: {name}')


def validate_package(package, kind='player'):
    manifest = json.loads((package / MANIFEST).read_text())
    if manifest.get('manifest_format') != 1 or manifest.get('kind') != kind:
        raise ValueError(f'Expected an installed {kind} package')
    files = manifest.get('files')
    verify_files(package, files)
    identity = package_identity(manifest)
    if manifest.get('build_id') != identity:
        raise ValueError('Package build ID does not match its deployment contract')
    products = manifest.get('products')
    if products not in ({'chibi-player': 'bin/chibi-player'}, {'chibi-player': 'bin/chibi-player.exe'}):
        raise ValueError('Package must own exactly the standalone player product')
    binary = package / products['chibi-player']
    if not binary.is_file() or not os.access(binary, os.X_OK) or not binary.stat().st_size:
        raise ValueError('Standalone player executable is missing')
    with binary.open('rb') as executable:
        if executable.read(4) == b'\0asm':
            raise ValueError('Expected a native player executable')
    if kind == 'runtime' and any(re.search(r'Chibi::(?:graph|studio|compiler|authoring|language|syntax|asset_(?:text|kdl))', target)
                                 for target in manifest.get('targets', [])):
        raise ValueError('Runtime SDK contains compiler/Studio exports')
    requirements = manifest.get('requirements', {})
    version = requirements.get('project_format')
    if type(version) is not int or version < 1:
        raise ValueError('Package project format requirement is missing')
    engine = json.loads((package / ENGINE).read_text())
    if (engine.get('project_version') != version
            or manifest.get('engine') != {'manifest': ENGINE.as_posix(), 'build_id': engine.get('build_id')}
            or not engine.get('build_id')):
        raise ValueError('Player engine identity does not match its package')
    if kind == 'player':
        pin = manifest.get('sdk', {})
        if (not pin.get('build_id') or pin.get('engine_build_id') != engine['build_id']
                or not pin.get('package_version')):
            raise ValueError('Player package has no exact source SDK pin')
        allowed = ('bin/', 'share/chibi/shaders/', 'share/chibi/licenses/')
        if any(name != ENGINE.as_posix() and not name.startswith(allowed) for name in files):
            raise ValueError('Player package contains undeclared SDK/Studio content')
        if {path.name for path in (package / 'bin').iterdir()} != {binary.name}:
            raise ValueError('Player package contains an undeclared binary')
    return manifest


def assemble(sdk, output):
    if (output == sdk or output in sdk.parents or sdk in output.parents):
        raise ValueError('Player output must be outside the SDK ancestry')
    source = validate_package(sdk, 'runtime')
    previous_identity = validate_package(output) if output.exists() else None
    output.parent.mkdir(parents=True, exist_ok=True)
    with staging_directory(prefix=f'.{output.name}-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'package'
        names = [source['products']['chibi-player'], ENGINE.as_posix()]
        names += [name for name in source['files']
                  if name.startswith(('share/chibi/shaders/', 'share/chibi/licenses/'))]
        for name in names:
            destination = stage / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(sdk / name, destination)
        files = package_files(stage)
        manifest = {'manifest_format': 1, 'kind': 'player',
                    'package_version': source['package_version'],
                    'engine': source['engine'], 'requirements': source['requirements'],
                    'sdk': {'build_id': source['build_id'], 'engine_build_id': source['engine']['build_id'],
                            'package_version': source['package_version']},
                    'products': source['products'], 'files': files}
        manifest['build_id'] = package_identity(manifest)
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        validate_package(stage)
        if validate_package(sdk, 'runtime') != source:
            raise ValueError('Source SDK changed during player packaging')
        current_identity = validate_package(output) if output.exists() else None
        if current_identity != previous_identity:
            raise ValueError('Existing player output changed during packaging')
        previous = Path(temporary) / 'previous'
        if output.exists():
            output.rename(previous)
        try:
            stage.rename(output)
        except OSError:
            if previous.exists():
                previous.rename(output)
            raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    try:
        output = args.output.resolve()
        if args.archive and (args.archive.resolve() == output or output in args.archive.resolve().parents):
            raise ValueError('Archive must be outside the package directory')
        manifest = assemble(args.sdk.resolve(), output)
        if args.archive:
            archive = args.archive.resolve()
            archive.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=archive.parent, delete=False) as temporary:
                staged = Path(temporary.name)
            try:
                with tarfile.open(staged, 'w:gz') as tar:
                    tar.add(output, arcname=output.name)
                staged.replace(archive)
            finally:
                staged.unlink(missing_ok=True)
        print(f'Packaged player {output} ({manifest["build_id"]})')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'package-player: {error}\n')


if __name__ == '__main__':
    main()
