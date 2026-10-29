#!/usr/bin/env python3
"""Package the installed-SDK Studio application and its relocatable resources."""

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import sys

sys.dont_write_bytecode = True

from package_player import ENGINE, MANIFEST, verify_files
from package_sdk import (cache_values, canonical_digest, digest,
                         executable_identity, package_files, package_identity, staging_directory)


def sdk_package(sdk):
    source = json.loads((sdk / MANIFEST).read_text())
    if source.get('manifest_format') != 1 or source.get('kind') != 'sdk':
        raise ValueError('Studio requires a packaged compiler/desktop SDK')
    verify_files(sdk, source.get('files'))
    if source.get('build_id') != package_identity(source):
        raise ValueError('SDK package identity mismatch')
    required = {'Chibi::project_preparation', 'Chibi::ui', 'Chibi::render'}
    if not required.issubset(source.get('targets', [])):
        raise ValueError('SDK is missing Studio authoring/desktop targets')
    engine = json.loads((sdk / ENGINE).read_text())
    if (not engine.get('build_id') or type(engine.get('project_version')) is not int
            or engine['project_version'] < 1
            or source.get('requirements', {}).get('project_format') != engine['project_version']
            or source.get('engine') != {'manifest': ENGINE.as_posix(), 'build_id': engine['build_id']}):
        raise ValueError('SDK engine identity mismatch')
    return source


def validate_package(package):
    record = json.loads((package / MANIFEST).read_text())
    if record.get('manifest_format') != 1 or record.get('kind') != 'studio':
        raise ValueError('Expected a Studio package')
    verify_files(package, record.get('files'))
    if record.get('build_id') != package_identity(record):
        raise ValueError('Studio package identity mismatch')
    products = record.get('products')
    if products not in ({'chibi-studio': 'bin/chibi-dev'}, {'chibi-studio': 'bin/chibi-dev.exe'}):
        raise ValueError('Studio package must contain exactly the Studio product')
    binary = package / products['chibi-studio']
    if not binary.stat().st_size or not os.access(binary, os.X_OK):
        raise ValueError('Studio executable is empty or not executable')
    with binary.open('rb') as executable:
        if executable.read(4) == b'\0asm':
            raise ValueError('Expected a native Studio executable')
    if {path.name for path in (package / 'bin').iterdir()} != {binary.name}:
        raise ValueError('Studio package contains another executable')
    allowed = ('share/chibi/shaders/', 'share/chibi/licenses/', 'share/chibi/ui/', 'share/chibi/studio/')
    if any(name not in (binary.relative_to(package).as_posix(), ENGINE.as_posix())
           and not name.startswith(allowed) for name in record['files']):
        raise ValueError('Studio package contains undeclared SDK/compiler/player content')
    pin = record.get('sdk', {})
    engine = json.loads((package / ENGINE).read_text())
    if (not pin.get('build_id') or not pin.get('package_version')
            or pin.get('engine_build_id') != engine.get('build_id')
            or record.get('engine') != {'manifest': ENGINE.as_posix(), 'build_id': engine.get('build_id')}
            or record.get('requirements', {}).get('project_format') != engine.get('project_version')):
        raise ValueError('Studio engine/SDK pin mismatch')
    return record


def build_identity(build, sdk, source):
    cache = cache_values(build)
    if (cache.get('CHIBI_STUDIO_DISTRIBUTION') != 'ON'
            or cache.get('CMAKE_BUILD_TYPE') not in ('Release', 'RelWithDebInfo')
            or cache.get('CMAKE_GENERATOR') != 'Ninja'):
        raise ValueError('Studio packaging requires a native Ninja distribution Release/RelWithDebInfo build')
    if (Path(cache['Chibi_DIR']).resolve() != sdk / 'lib/cmake/Chibi'
            or cache.get('CHIBI_STUDIO_SDK_BUILD_ID') != source['build_id']
            or cache.get('CHIBI_STUDIO_SDK_MANIFEST_SHA256') != digest(sdk / MANIFEST)):
        raise ValueError('Configured Studio SDK pin differs from the supplied package; reconfigure/rebuild')
    binaries = [build / 'bin' / name for name in ('chibi-dev', 'chibi-dev.exe')]
    binaries = [path for path in binaries if path.is_file()]
    if len(binaries) != 1:
        raise ValueError('Missing or ambiguous Studio executable')
    binary = binaries[0]
    current = subprocess.check_output(['ninja', '-C', str(build), '-n', binary.relative_to(build).as_posix()], text=True)
    if not current.rstrip().endswith('ninja: no work to do.'):
        raise ValueError('Studio build is not current; build it before packaging')
    dependency_text = subprocess.check_output(['ninja', '-C', str(build), '-t', 'deps'], text=True)
    dependencies = {}
    object_name = None
    for line in dependency_text.splitlines():
        if line and not line.startswith(' '):
            object_name = line.split(': #deps ', 1)[0]
            dependencies[object_name] = []
        elif object_name and line.startswith('    '):
            dependencies[object_name].append(line.strip())
    inputs = {}
    objects = {}
    commands = json.loads((build / 'compile_commands.json').read_text())
    for entry in commands:
        directory = Path(entry['directory'])
        arguments = entry.get('arguments') or shlex.split(entry['command'])
        if any('sanitize=' in argument for argument in arguments):
            raise ValueError('Studio distribution must not contain sanitizer instrumentation')
        try:
            object_path = (directory / arguments[arguments.index('-o') + 1]).resolve()
        except (ValueError, IndexError):
            raise ValueError('Studio compile command has no object output') from None
        relative = object_path.relative_to(build).as_posix()
        if not object_path.is_file() or object_path.stat().st_mtime_ns > binary.stat().st_mtime_ns:
            raise ValueError(f'Missing or stale linked object: {relative}')
        names = dependencies.get(relative)
        if not names:
            raise ValueError(f'Missing compiled dependency evidence: {relative}')
        for name in set(names) | {entry['file']}:
            path = (directory / name).resolve()
            if not path.is_file() or path.stat().st_mtime_ns > object_path.stat().st_mtime_ns:
                raise ValueError(f'Compiled dependency changed: {path}')
            inputs[str(path)] = digest(path)
        objects[relative] = digest(object_path)
    if not objects:
        raise ValueError('Studio compile input inventory is empty')
    executable = executable_identity(build, cache, 'chibi-studio',
                                     build / 'CMakeFiles/chibi-studio.dir', binary,
                                     source_root=Path(cache['chibi_studio_SOURCE_DIR']).resolve())
    return {'objects': objects, 'inputs': inputs, 'executable': executable,
            'source_root': cache['chibi_studio_SOURCE_DIR'],
            'build_type': cache['CMAKE_BUILD_TYPE']}


def copy_notices(stage, build):
    for source in (build / '_deps').glob('*-src'):
        for path in source.rglob('*'):
            if (path.is_file() and '.git' not in path.relative_to(source).parts
                    and path.name.lower().startswith(('license', 'copying', 'notice', 'ofl'))):
                target = stage / 'share/chibi/licenses' / source.name / path.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)


def assemble(build, sdk, output):
    source = sdk_package(sdk)
    compiled = build_identity(build, sdk, source)
    root = Path(compiled['source_root']).resolve()
    for owned in (root, build, sdk):
        if output == owned or output in owned.parents or owned in output.parents:
            raise ValueError('Studio output must be outside source/build/SDK ancestry')
    previous_record = validate_package(output) if output.exists() else None
    output.parent.mkdir(parents=True, exist_ok=True)
    with staging_directory(prefix=f'.{output.name}-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'package'
        subprocess.run(['cmake', '--install', str(build), '--prefix', str(stage)], check=True)
        # SDK install metadata describes the SDK, not this Studio product.
        (stage / MANIFEST).unlink(missing_ok=True)
        copy_notices(stage, build)
        product = compiled['executable']['path']
        if digest(stage / product) != compiled['executable']['sha256']:
            raise ValueError('Installed Studio binary differs from the inspected build')
        manifest = {'manifest_format': 1, 'kind': 'studio', 'package_version': source['package_version'],
                    'engine': source['engine'], 'requirements': source['requirements'],
                    'sdk': {'build_id': source['build_id'], 'engine_build_id': source['engine']['build_id'],
                            'package_version': source['package_version']},
                    'products': {'chibi-studio': product}, 'build': compiled,
                    'files': package_files(stage)}
        manifest['build_id'] = package_identity(manifest)
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        validate_package(stage)
        if sdk_package(sdk) != source or build_identity(build, sdk, source) != compiled:
            raise ValueError('SDK or Studio build changed during packaging')
        if (validate_package(output) if output.exists() else None) != previous_record:
            raise ValueError('Existing Studio output changed during packaging')
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
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    try:
        output = args.output.resolve()
        if args.archive and (args.archive.resolve() == output or output in args.archive.resolve().parents):
            raise ValueError('Archive must be outside the package directory')
        manifest = assemble(args.build.resolve(), args.sdk.resolve(), output)
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
        print(f'Packaged Studio {output} ({manifest["build_id"]})')
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'package-studio: {error}\n')


if __name__ == '__main__':
    main()
