#!/usr/bin/env python3
"""Package the installed-SDK Studio application and its relocatable resources."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import sys

sys.dont_write_bytecode = True

from package_player import ENGINE, MANIFEST, verify_files
from package_sdk import (cache_values, digest,
                         package_files, package_identity, staging_directory)


def sdk_package(sdk):
    source = json.loads((sdk / MANIFEST).read_text())
    if source.get('manifest_format') != 1 or source.get('kind') != 'sdk':
        raise ValueError('Studio requires a packaged compiler/desktop SDK')
    verify_files(sdk, source.get('files'))
    if source.get('build_id') != package_identity(source):
        raise ValueError('SDK package identity mismatch')
    required = {'Chibi::authoring_host', 'Chibi::workspace_catalog', 'Chibi::ui', 'Chibi::render'}
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
    products = record.get('products', {})
    launcher = products.get('chibi-studio')
    if launcher not in ('bin/chibi-dev', 'bin/chibi-dev.cmd'):
        raise ValueError('Studio package must contain an authored application launcher')
    suffix = '.exe' if launcher.endswith('.cmd') else ''
    expected = {'chibi-studio': launcher, **{
        name: f'bin/{name}{suffix}' for name in ('chibi-author', 'chibi-compile', 'chibi-player', 'chibi')}}
    if products != expected:
        raise ValueError('Studio package products differ from its generic SDK hosts')
    if {path.name for path in (package / 'bin').iterdir()} != {Path(path).name for path in products.values()}:
        raise ValueError('Studio package contains undeclared executables')
    for name, path in products.items():
        binary = package / path
        if not binary.stat().st_size or (suffix == '' and not os.access(binary, os.X_OK)):
            raise ValueError(f'Studio product is empty or not executable: {name}')
    allowed = ('bin/', 'share/chibi/sdk/', 'share/chibi/studio/',
               'share/chibi/shaders/', 'share/chibi/licenses/')
    if any(name != ENGINE.as_posix() and not name.startswith(allowed) for name in record['files']):
        raise ValueError('Studio package contains undeclared content')
    artifact = package / 'share/chibi/studio/game.chibip'
    if not artifact.is_file() or not artifact.stat().st_size:
        raise ValueError('Studio authored artifact is missing')
    nested_path = package / 'share/chibi/sdk/package.json'
    nested = json.loads(nested_path.read_text())
    pin = record.get('sdk', {})
    if (nested.get('kind') != 'sdk' or nested.get('build_id') != package_identity(nested)
            or pin.get('build_id') != nested.get('build_id')
            or pin.get('manifest_sha256') != digest(nested_path)
            or pin.get('manifest') != 'share/chibi/sdk/package.json'
            or pin.get('package_version') != nested.get('package_version')):
        raise ValueError('Studio exact SDK manifest pin mismatch')
    engine = json.loads((package / 'share/chibi/sdk/engine.json').read_text())
    if (pin.get('engine_build_id') != engine.get('build_id')
            or record.get('engine') != {'manifest': 'share/chibi/sdk/engine.json', 'build_id': engine.get('build_id')}
            or record.get('requirements', {}).get('project_format') != engine.get('project_version')):
        raise ValueError('Studio engine pin mismatch')
    for name in ('chibi-author', 'chibi-compile', 'chibi-player', 'chibi'):
        original = nested.get('files', {}).get(nested.get('products', {}).get(name), {})
        if original.get('sha256') != digest(package / products[name]):
            raise ValueError(f'Studio generic host differs from the pinned SDK: {name}')
    return record


def authored_build_identity(build, source_root=None, sdk=None):
    """Verify the actual authored build inputs and copies, without invoking a build."""
    build = Path(build).resolve()
    cache = cache_values(build)
    if cache.get('CHIBI_STUDIO_AUTHORED') != 'ON':
        raise ValueError('Expected an authored Studio build')
    root = Path(cache['chibi_studio_SOURCE_DIR']).resolve()
    if source_root is not None and root != Path(source_root).resolve():
        raise ValueError('Studio source differs from its configured build')
    configured_sdk = Path(cache['Chibi_DIR']).resolve().parents[2]
    sdk = configured_sdk if sdk is None else Path(sdk).resolve()
    if sdk != configured_sdk:
        raise ValueError('Configured Studio SDK differs from supplied package')
    manifest_path = sdk / MANIFEST
    source = json.loads(manifest_path.read_text())
    if (source.get('manifest_format') != 1 or source.get('kind') != 'sdk'
            or source.get('build_id') != package_identity(source)
            or cache.get('CHIBI_STUDIO_SDK_BUILD_ID') != source['build_id']
            or cache.get('CHIBI_STUDIO_SDK_MANIFEST_SHA256') != digest(manifest_path)):
        raise ValueError('Configured Studio SDK pin changed; reconfigure/rebuild')
    compiler_key = source['products']['chibi-compile']
    compiler = sdk / compiler_key
    if digest(compiler) != source['files'][compiler_key]['sha256']:
        raise ValueError('SDK compiler differs from its package identity')
    artifact = Path(cache['CHIBI_STUDIO_ARTIFACT']).resolve()
    launcher = Path(cache['CHIBI_STUDIO_LAUNCHER']).resolve()
    if not artifact.is_relative_to(build) or not launcher.is_relative_to(build):
        raise ValueError('Studio authored outputs must belong to its build directory')
    outputs = (artifact, Path(str(artifact) + '.resources.json'), launcher)
    if any(not path.is_file() or not path.stat().st_size for path in outputs):
        raise ValueError('Build the Studio authored artifact and launcher first')
    project = root / 'projects/studio'
    copied = build / 'share/chibi/studio'
    def files(directory):
        result = {}
        for path in sorted(directory.rglob('*')):
            if path.is_symlink() or not (path.is_file() or path.is_dir()):
                raise ValueError(f'Studio inputs must be regular files: {path}')
            if path.is_file():
                result[path.relative_to(directory).as_posix()] = digest(path)
        return result
    inputs = files(project)
    installed = files(copied)
    for name, original in zip(('game.chibip', 'game.chibip.resources.json'), outputs[:2]):
        if installed.pop(name, None) != digest(original):
            raise ValueError('Studio artifact or resource manifest copy differs; rebuild')
    if not inputs or inputs != installed:
        raise ValueError('Studio source membership or bytes differ from built resources; rebuild')
    if digest(build / 'share/chibi/sdk/package.json') != digest(manifest_path):
        raise ValueError('Copied Studio SDK manifest differs from configured package; rebuild')
    source_inventory = build / 'studio-inputs.txt'
    entries = source_inventory.read_text().strip().split(';')
    if any(not Path(entry).is_absolute() for entry in entries):
        raise ValueError('Configured Studio source inventory must use absolute paths')
    members = [Path(entry).resolve(strict=True) for entry in entries]
    expected = {project / name for name in inputs}
    if len(members) != len(set(members)) or set(members) != expected:
        raise ValueError('Configured Studio source membership changed; reconfigure/rebuild')
    dependencies = [project / name for name in inputs]
    dependencies += [root / 'CMakeLists.txt', source_inventory, manifest_path, compiler]
    for path in dependencies:
        if not path.is_file() or path.stat().st_mtime_ns > artifact.stat().st_mtime_ns:
            raise ValueError(f'Studio authored input changed; rebuild: {path}')
    return {'kind': 'authored-application',
            'inputs': {'projects/studio/' + name: value for name, value in inputs.items()},
            'artifact': {'path': artifact.relative_to(build).as_posix(), 'sha256': digest(artifact)},
            'resource_manifest_sha256': digest(outputs[1]),
            'launcher': {'path': launcher.relative_to(build).as_posix(), 'sha256': digest(launcher)},
            'source_root': str(root), 'build_type': cache['CMAKE_BUILD_TYPE']}


def build_identity(build, sdk, source):
    cache = cache_values(build)
    if (cache.get('CHIBI_STUDIO_AUTHORED') != 'ON'
            or cache.get('CHIBI_STUDIO_DISTRIBUTION') != 'ON'
            or cache.get('CMAKE_BUILD_TYPE') not in ('Release', 'RelWithDebInfo')
            or cache.get('CMAKE_GENERATOR') != 'Ninja'):
        raise ValueError('Studio packaging requires a source-only Ninja distribution Release/RelWithDebInfo build')
    if (Path(cache['Chibi_DIR']).resolve() != sdk / 'lib/cmake/Chibi'
            or cache.get('CHIBI_STUDIO_SDK_BUILD_ID') != source['build_id']
            or cache.get('CHIBI_STUDIO_SDK_MANIFEST_SHA256') != digest(sdk / MANIFEST)):
        raise ValueError('Configured Studio SDK pin differs from the supplied package; reconfigure/rebuild')
    return authored_build_identity(build, sdk=sdk)


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
        product = compiled['launcher']['path']
        if digest(stage / product) != compiled['launcher']['sha256']:
            raise ValueError('Installed Studio launcher differs from the inspected build')
        if digest(stage / 'share/chibi/studio/game.chibip') != compiled['artifact']['sha256']:
            raise ValueError('Installed Studio authored artifact differs from the inspected build')
        if digest(stage / 'share/chibi/sdk/package.json') != digest(sdk / MANIFEST):
            raise ValueError('Installed Studio SDK identity differs from the inspected package')
        manifest = {'manifest_format': 1, 'kind': 'studio', 'package_version': source['package_version'],
                    'engine': {'manifest': 'share/chibi/sdk/engine.json', 'build_id': source['engine']['build_id']},
                    'requirements': source['requirements'],
                    'sdk': {'build_id': source['build_id'], 'engine_build_id': source['engine']['build_id'],
                            'package_version': source['package_version'],
                            'manifest': 'share/chibi/sdk/package.json',
                            'manifest_sha256': digest(sdk / MANIFEST),
                            'identity_scope': 'Exact installed SDK manifest bytes; this distribution contains selected SDK products and resources, not the complete SDK'},
                    'products': {'chibi-studio': product, **source['products']}, 'build': compiled,
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
