#!/usr/bin/env python3
"""Package and verify the browser authoring host and its isolated compilers."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True

from package_player import verify_files
from package_sdk import (ROOT, build_identity, cache_values, canonical_digest,
                         cmake_metadata, digest, package_identity,
                         source_identity, staging_directory)

MANIFEST = Path('package.json')
PRODUCTS = {name: name + '.js' for name in
            ('chibi-author', 'chibi-compile', 'chibi-compile-worker')}
INSTALLED = {'chibi-author': 'chibi-author.js',
             'chibi-compile': 'compiler/chibi-compile.js',
             'chibi-compile-worker': 'compiler/chibi-compile-worker.js'}
REQUIRED = {'index.html', 'engine.json', 'compiler/compiler_worker.js',
            *INSTALLED.values(), *(name.removesuffix('.js') + '.wasm'
                                   for name in INSTALLED.values())}


def validate(package):
    record = json.loads((package / MANIFEST).read_text())
    if (record.get('manifest_format') != 1 or record.get('kind') != 'web-author'
            or record.get('build_id') != package_identity(record)
            or record.get('products') != INSTALLED):
        raise ValueError('Invalid browser authoring package identity')
    verify_files(package, record.get('files'), MANIFEST)
    if set(record['files']) != REQUIRED:
        raise ValueError('Browser authoring product inventory differs from its contract')
    for path in INSTALLED.values():
        if not (package / path).stat().st_size:
            raise ValueError('Empty browser authoring product: ' + path)
        with (package / (path.removesuffix('.js') + '.wasm')).open('rb') as wasm:
            if wasm.read(8) != b'\0asm\x01\0\0\0':
                raise ValueError('Invalid WebAssembly product: ' + path)
    engine = json.loads((package / 'engine.json').read_text())
    if (record.get('engine') != {'manifest': 'engine.json', 'build_id': engine.get('build_id')}
            or engine.get('project_version') != record.get('requirements', {}).get('project_format')):
        raise ValueError('Browser authoring engine identity mismatch')
    return record


def match_sdk(package, sdk_manifest):
    record = validate(package)
    sdk = json.loads(sdk_manifest.read_text())
    if (sdk.get('kind') != 'sdk' or sdk.get('build_id') != package_identity(sdk)
            or sdk.get('source') != record.get('source')
            or sdk.get('package_version') != record.get('package_version')):
        raise ValueError('Browser host and SDK must identify the same engine source snapshot/version')
    for key, value in record['requirements'].items():
        if sdk.get('requirements', {}).get(key) != value:
            raise ValueError('Browser host/SDK requirement differs: ' + key)
    return record


def compiled_identity(build, cache):
    record = build_identity(build, cache, products=PRODUCTS)
    record['webassembly'] = {name: digest(build / path.replace('.js', '.wasm'))
                             for name, path in PRODUCTS.items()}
    return record


def assemble(build, output):
    cache = cache_values(build)
    system = cmake_metadata(build, 'CMakeSystem.cmake')
    compiler = cmake_metadata(build, 'CMakeCCompiler.cmake')
    if (Path(cache.get('CMAKE_HOME_DIRECTORY', '')).resolve() != ROOT / 'sdk'
            or system.get('CMAKE_SYSTEM_NAME') != 'Emscripten'
            or cache.get('CHIBI_WEB_AUTHOR') != 'ON'
            or cache.get('CHIBI_ENABLE_SJLIT') != 'OFF'
            or cache.get('CMAKE_BUILD_TYPE') != 'Release'):
        raise ValueError('Browser authoring packaging requires a Release Emscripten sdk/ author build')
    if output == ROOT or output in ROOT.parents or output == build or output in build.parents or build in output.parents:
        raise ValueError('Browser package output must be outside source/build ancestry')
    previous = validate(output) if output.exists() else None
    source = source_identity()
    compiled = compiled_identity(build, cache)
    version_text = (build / 'ChibiConfigVersion.cmake').read_text()
    version = re.search(r'set\(PACKAGE_VERSION "([0-9.]+)"\)', version_text)
    if not version:
        raise ValueError('Missing configured SDK version')
    output.parent.mkdir(parents=True, exist_ok=True)
    with staging_directory(prefix='.' + output.name + '-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'package'
        subprocess.run(['cmake', '--install', str(build), '--component', 'web_author',
                        '--prefix', str(stage)], check=True)
        engine_id = canonical_digest(compiled)
        (stage / 'engine.json').write_text(json.dumps({
            'manifest_format': 1, 'build_id': engine_id,
            'project_version': compiled['configured_engine']['project_version']}, indent=2) + '\n')
        files = {}
        for path in sorted(stage.rglob('*')):
            if path.is_symlink() or not (path.is_file() or path.is_dir()):
                raise ValueError('Browser packages require regular files')
            if path.is_file():
                files[path.relative_to(stage).as_posix()] = {'sha256': digest(path), 'bytes': path.stat().st_size}
        for target, path in INSTALLED.items():
            if files.get(path, {}).get('sha256') != compiled['executables'][target]['sha256']:
                raise ValueError('Installed browser executable differs from compiled evidence: ' + target)
            built_wasm = build / PRODUCTS[target].replace('.js', '.wasm')
            if files.get(path.replace('.js', '.wasm'), {}).get('sha256') != digest(built_wasm):
                raise ValueError('Installed browser WebAssembly differs: ' + target)
        for installed, original in (('index.html', 'author.html'), ('compiler/compiler_worker.js', 'compiler_worker.js')):
            if files.get(installed, {}).get('sha256') != digest(ROOT / 'src/host/web' / original):
                raise ValueError('Installed browser bootstrap differs from source: ' + installed)
        requirements = {'project_format': compiled['configured_engine']['project_version'],
                        'artifact_formats': compiled['configured_engine']['artifact_formats'],
                        'prepared_format': compiled['configured_engine']['artifact_formats']['execution']['version'],
                        'system': 'Emscripten', 'processor': system.get('CMAKE_SYSTEM_PROCESSOR'),
                        'pointer_bits': int(compiler['CMAKE_C_SIZEOF_DATA_PTR']) * 8,
                        'byte_order': compiler.get('CMAKE_C_BYTE_ORDER'),
                        'web_threads': cache.get('CHIBI_WEB_THREADS') == 'ON',
                        'web_workers': int(cache.get('CHIBI_WEB_WORKERS', '4'))}
        record = {'manifest_format': 1, 'kind': 'web-author', 'package_version': version[1],
                  'engine': {'manifest': 'engine.json', 'build_id': engine_id},
                  'source': source, 'requirements': requirements, 'products': INSTALLED,
                  'build': {'inputs': compiled, 'configuration': cache['CMAKE_BUILD_TYPE'],
                            'compiler_id': compiler.get('CMAKE_C_COMPILER_ID'),
                            'compiler_version': compiler.get('CMAKE_C_COMPILER_VERSION')},
                  'files': files}
        record['build_id'] = package_identity(record)
        manifest_text = json.dumps(record, indent=2, sort_keys=True) + '\n'
        if len(manifest_text.encode()) > 8 * 1024 * 1024:
            raise ValueError('Browser package manifest exceeds the host identity limit')
        (stage / MANIFEST).write_text(manifest_text)
        validate(stage)
        if source_identity() != source or compiled_identity(build, cache) != compiled:
            raise ValueError('Source or build changed during browser packaging')
        if (validate(output) if output.exists() else None) != previous:
            raise ValueError('Existing browser package changed during packaging')
        backup = Path(temporary) / 'previous'
        if output.exists():
            output.rename(backup)
        try:
            stage.rename(output)
        except OSError:
            if backup.exists():
                backup.rename(output)
            raise
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--sdk-manifest', type=Path)
    args = parser.parse_args()
    try:
        output = args.output.resolve()
        if args.verify:
            record = match_sdk(output, args.sdk_manifest.resolve()) if args.sdk_manifest else validate(output)
        else:
            if args.build is None or args.sdk_manifest:
                raise ValueError('Packaging requires --build; SDK matching uses --verify')
            record = assemble(args.build.resolve(), output)
        print(f'Browser authoring package {output} ({record["build_id"]})')
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'package-web-author: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
