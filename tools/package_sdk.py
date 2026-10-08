#!/usr/bin/env python3
"""Package a standalone installed Chibi SDK or source-free behavior runtime."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import tarfile
import tempfile
import sys
from contextlib import contextmanager

# Installed helpers must leave the checksum-pinned package unchanged.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path('share/chibi/package.json')
CORE_TARGETS = {'ui_document_values', 'text_search', 'image_catalog', 'support', 'types', 'ecs', 'execution_contract', 'execution', 'behavior_runtime', 'events',
                'asset_values', 'value_document', 'capabilities', 'native_capabilities', 'native_prepared', 'core_capabilities', 'array_capabilities', 'field_capabilities', 'state_capabilities', 'selection_capabilities', 'set_capabilities', 'comparison_capabilities', 'conversion_capabilities', 'text_capabilities', 'fmt', 'host_native_catalog', 'scene_runtime', 'scheduling', 'fields', 'field_catalog', 'tasks', 'shader_runtime', 'model_runtime', 'ui_documents'}
COMPILER_TARGETS = {'image_preparation', 'builtin_fonts', 'graph_catalog', 'studio_graph', 'asset_json', 'graph_compilation', 'source_catalog', 'source_presentation', 'graph_model', 'graph_authoring', 'syntax_runtime', 'language_syntax', 'language', 'json', 'compiler', 'authoring', 'studio_capabilities', 'component_presentation', 'project_document', 'prototype_document', 'scene_document', 'reference_index', 'asset_text', 'asset_kdl', 'text_source', 'ui_document', 'ui_authoring', 'ui_syntax'}
NATIVE_TARGETS = {'visual_catalog', 'json', 'native_types', 'native_bindings', 'spatial_runtime', 'entity_runtime', 'audio_runtime', 'rhythm_runtime', 'particle_runtime', 'save_runtime', 'ui_runtime', 'input_runtime', 'host_native_default', 'visibility_runtime', 'engine', 'player', 'visual_motion_runtime', 'material_runtime', 'physics_runtime', 'task_pool_runtime', 'box2d'}
VALUE_TARGETS = {'math', 'vector_capabilities', 'math_capabilities', 'arithmetic_capabilities', 'mesh_runtime', 'model_bake'}
DESKTOP_TARGETS = {'builtin_fonts', 'platform', 'ui', 'render'}
NATIVE_AUTHORING_TARGETS = {'shader_preparation', 'project_compilation', 'asset_authoring', 'model_authoring', 'scenario_document'}
DESKTOP_AUTHORING_TARGETS = {'project_preparation'}


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def package_identity(manifest):
    # Package identity covers the contract and provenance as well as file checksums.
    return canonical_digest({key: value for key, value in manifest.items() if key != 'build_id'})


@contextmanager
def staging_directory(*, prefix, dir):
    path = Path(tempfile.mkdtemp(prefix=prefix, dir=dir))
    try:
        yield path
    except BaseException:
        if (path / 'previous').exists():
            print(f'Package rollback could not restore its prior generation; recovery retained at {path / "previous"}',
                  file=sys.stderr)
        else:
            shutil.rmtree(path)
        raise
    else:
        shutil.rmtree(path)


def cache_values(build):
    values = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        match = re.match(r'([^#/:][^:]*):[^=]+=(.*)', line)
        if match:
            values[match[1]] = match[2]
    return values


def cmake_metadata(build, filename):
    candidates = sorted((build / 'CMakeFiles').glob(f'*/{filename}'))
    if len(candidates) != 1:
        raise ValueError(f'Expected one configured {filename} in {build}')
    values = {}
    for match in re.finditer(r'set\((CMAKE_[A-Z_]+) "([^"\n]*)"\)', candidates[0].read_text()):
        values[match[1]] = match[2]
    return values


def source_identity():
    def git(*args):
        return subprocess.check_output(['git', '-C', str(ROOT), *args])
    revision = git('rev-parse', 'HEAD').decode().strip()
    dirty = bool(git('status', '--porcelain', '--untracked-files=normal').strip())
    names = set(git('ls-files', '-c', '-o', '--exclude-standard', '-z').decode().split('\0'))
    files = {}
    for name in sorted(names):
        if not name:
            continue
        path = ROOT / name
        if path.is_symlink():
            files[name] = {'symlink': os.readlink(path)}
        elif path.is_file():
            files[name] = digest(path)
    return {'revision': revision, 'dirty': dirty, 'snapshot_sha256': canonical_digest(files)}


def product_targets(cache, system=None):
    if (system or cache.get('CMAKE_SYSTEM_NAME')) == 'Emscripten':
        return set()
    products = set()
    native = cache.get('CHIBI_SDK_BUILD_NATIVE') == 'ON'
    if native and cache.get('CHIBI_SDK_BUILD_DESKTOP') == 'ON':
        products.add('chibi-player')
    if native and cache.get('CHIBI_SDK_BUILD_COMPILER') == 'ON':
        products.add('chibi-compile')
    return products


def executable_identity(build, cache, target, target_directory, output=None, *, source_root=None):
    candidates = ([output] if output is not None else
                  [target_directory.parent.parent / (target + suffix)
                   for suffix in ('', '.exe')])
    outputs = [path for path in candidates if path.is_file()]
    if len(outputs) != 1:
        raise ValueError(f'Missing or ambiguous built executable for {target}')
    output = outputs[0]
    dependencies = []
    if cache.get('CMAKE_GENERATOR') == 'Ninja':
        relative = output.relative_to(build).as_posix()
        query = subprocess.check_output(['ninja', '-C', str(build), '-t', 'query', relative], text=True)
        reading = False
        for line in query.splitlines():
            if line.startswith('  input:'):
                reading = True
            elif line.startswith('  outputs:'):
                reading = False
            elif reading and line.startswith('    '):
                # Order-only target dependencies are scheduling facts, not
                # linker inputs. Explicit and implicit inputs are both files.
                name = line.strip()
                if name.startswith('|| '):
                    continue
                dependencies.append(name.removeprefix('| '))
        commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', relative], text=True)
        lines = commands.splitlines()
        if not lines:
            raise ValueError(f'No executable link command for {target}')
        arguments = shlex.split(lines[-1])
        directory = build
    else:
        link = target_directory / 'link.txt'
        if not link.is_file():
            raise ValueError(f'No executable link evidence for {target}')
        arguments = shlex.split(link.read_text())
        directory = target_directory.parent.parent
        for i, argument in enumerate(arguments):
            if i and arguments[i - 1] == '-o':
                continue
            path = (directory / argument).resolve()
            if not argument.startswith('-') and path.is_file():
                dependencies.append(argument)
    if not dependencies:
        raise ValueError(f'No executable link dependencies for {target}')
    inputs = {}
    object_count = 0
    for name in sorted(set(dependencies)):
        path = (directory / name).resolve()
        if not path.is_file():
            raise ValueError(f'Executable link dependency is missing: {name}')
        if path.stat().st_mtime_ns > output.stat().st_mtime_ns:
            raise ValueError(f'Executable {target} predates a link dependency; relink first')
        checksum = digest(path)
        key = path.relative_to(build).as_posix() if path.is_relative_to(build) else 'external/' + path.name + '-' + checksum[:16]
        inputs[key] = checksum
        object_count += path.suffix in ('.o', '.obj')
    if not object_count:
        raise ValueError(f'No executable object input for {target}')
    normalized = [argument.replace(str(build), '@build') for argument in arguments]
    if source_root is not None:
        normalized = [argument.replace(str(source_root), '@source') for argument in normalized]
    return {'path': output.relative_to(build).as_posix(), 'sha256': digest(output),
            'link_command_sha256': canonical_digest(normalized), 'arguments': normalized,
            'link_inputs': inputs}


def build_identity(build, cache):
    metadata_path = build / 'chibi_engine_metadata.json'
    if not metadata_path.is_file():
        raise ValueError('Packaging requires configured engine metadata')
    metadata_bytes = metadata_path.read_bytes()
    metadata = json.loads(metadata_bytes)
    version = metadata.get('project_version')
    if type(version) is not int or version < 1:
        raise ValueError('Configured engine project version is invalid')
    formats = metadata.get('artifact_formats')
    if not isinstance(formats, dict) or 'execution' not in formats:
        raise ValueError('Configured artifact format requirements are missing; reconfigure first')
    configured = {'project_version': version, 'artifact_formats': formats,
                  'sha256': hashlib.sha256(metadata_bytes).hexdigest()}
    commands_file = build / 'compile_commands.json'
    if not commands_file.is_file():
        raise ValueError('Packaging requires CMAKE_EXPORT_COMPILE_COMMANDS=ON')
    commands = json.loads(commands_file.read_text())
    ninja_dependencies = {}
    if cache.get('CMAKE_GENERATOR') == 'Ninja':
        output = subprocess.check_output(['ninja', '-C', str(build), '-t', 'deps'], text=True)
        current = None
        for line in output.splitlines():
            if line and not line.startswith(' '):
                current = line.split(': #deps ', 1)[0]
                ninja_dependencies[current] = []
            elif current and line.startswith('    '):
                ninja_dependencies[current].append(line.strip())
    inputs = {}
    objects = {}
    archives = {}
    archive_targets = {}
    executables = {}
    allowed_products = product_targets(cache, cmake_metadata(build, 'CMakeSystem.cmake').get('CMAKE_SYSTEM_NAME'))
    for entry in commands:
        directory = Path(entry['directory'])
        arguments = entry.get('arguments') or shlex.split(entry['command'])
        # CMake can record `output` relative to its build root while `-o` is
        # relative to this command's working directory in a subdirectory.
        # The compiler invocation is the authoritative filesystem operation.
        try:
            object_name = arguments[arguments.index('-o') + 1]
        except (ValueError, IndexError):
            object_name = entry.get('output')
            if not object_name:
                raise ValueError('Compile command has no object output') from None
        object_path = (directory / object_name).resolve()
        if not object_path.is_file():
            raise ValueError(f'Build is incomplete: {object_path.name}')
        target_directory = next((parent for parent in object_path.parents
                                 if parent.name.endswith('.dir') and parent.parent.name == 'CMakeFiles'), None)
        if target_directory is None:
            raise ValueError(f'Cannot identify archive owner for {object_path.name}')
        target = target_directory.name.removesuffix('.dir')
        if target in ('chibi-player', 'chibi-compile'):
            if target not in allowed_products:
                raise ValueError(f'Executable {target} is outside the configured product contract')
            if target not in executables:
                executables[target] = executable_identity(build, cache, target, target_directory, source_root=ROOT)
            executable = build / executables[target]['path']
            if executable.stat().st_mtime_ns < object_path.stat().st_mtime_ns:
                raise ValueError(f'Executable {target} predates a rebuilt object; relink first')
        else:
            candidates = [target_directory.parent.parent / name
                          for name in (f'lib{target}.a', f'{target}.lib', f'lib{target}.lib')]
            archive_paths = [path for path in candidates if path.is_file()]
            if len(archive_paths) != 1:
                raise ValueError(f'Missing or ambiguous built archive for {target}')
            archive = archive_paths[0]
            if archive.stat().st_mtime_ns < object_path.stat().st_mtime_ns:
                raise ValueError(f'Archive {archive.name} predates a rebuilt object; relink first')
            archive_key = archive.relative_to(build).as_posix()
            archive_targets[archive_key] = target
            if archive_key not in archives:
                archives[archive_key] = digest(archive)
        normalized = [argument.replace(str(build), '@build').replace(str(ROOT), '@source')
                      for argument in arguments]
        objects[object_path.relative_to(build).as_posix()] = {
            'sha256': digest(object_path), 'target': target,
            'command_sha256': canonical_digest(normalized), 'arguments': normalized,
        }
        if cache.get('CMAKE_GENERATOR') == 'Ninja':
            relative = object_path.relative_to(build).as_posix()
            dependencies = ninja_dependencies.get(relative)
            if not dependencies:
                raise ValueError(f'No Ninja dependency evidence for {relative}')
        else:
            depfile = Path(str(object_path) + '.d')
            if not depfile.is_file():
                raise ValueError(f'No compiler dependency evidence for {object_path.name}')
            text = depfile.read_text().replace('\\\n', ' ')
            dependencies = shlex.split(text.split(':', 1)[1])
        dependencies = set(dependencies) | {entry['file']}
        for name in dependencies:
            path = (directory / name).resolve()
            if not path.is_file():
                raise ValueError(f'Compiled dependency is missing: {path.name}')
            if path.stat().st_mtime_ns > object_path.stat().st_mtime_ns:
                raise ValueError(f'Compiled dependency changed after its object was built: {path.name}; rebuild first')
            checksum = digest(path)
            if path.is_relative_to(build):
                key = 'generated/' + path.relative_to(build).as_posix()
            elif path.is_relative_to(ROOT):
                key = 'source/' + path.relative_to(ROOT).as_posix()
            else:
                key = 'external/' + path.name + '-' + checksum[:16]
            inputs[key] = checksum
    for name, record in formats.items():
        if (not isinstance(record, dict) or type(record.get('version')) is not int
                or record['version'] < 1 or not isinstance(record.get('source'), str)
                or not isinstance(record.get('header'), str)
                or not record['source'].startswith('src/')
                or not record['header'].startswith('src/')):
            raise ValueError(f'Configured artifact format is invalid: {name}')
        source = inputs.get('source/' + record['source'])
        header = record['header'].removeprefix('src/')
        copies = [checksum for path, checksum in inputs.items()
                  if (path.startswith('generated/') and path.endswith('/' + header))
                  or path == 'source/' + record['header']]
        if source != record.get('sha256') or not copies or any(
                checksum != record.get('header_sha256') for checksum in copies):
            raise ValueError(f'Configured artifact format differs from compiled ownership: {name}; reconfigure/rebuild first')
    if set(executables) != allowed_products:
        raise ValueError(f'Built executables differ from product contract: {sorted(executables)}')
    return {'configured_engine': configured,
            'object_count': len(objects), 'objects': objects, 'archives': archives,
            'executables': executables, 'archive_targets': archive_targets,
            'inputs_sha256': canonical_digest(inputs), 'inputs': inputs}


def engine_manifest(stage, compiled):
    # Engine identity describes the compiled inputs and archives. Package
    # identity is computed later from installed files, including this manifest.
    manifest = {'manifest_format': 1, 'build_id': canonical_digest(compiled),
                'project_version': compiled['configured_engine']['project_version']}
    path = stage / 'share/chibi/engine.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    return manifest


def package_files(stage):
    if stage.is_symlink():
        raise ValueError('Installed package root cannot be a symlink')
    files = {}
    for path in sorted(stage.rglob('*')):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f'Installed packages cannot contain symlinks: {path.relative_to(stage)}')
        if path.is_file() and path.relative_to(stage) != MANIFEST:
            files[path.relative_to(stage).as_posix()] = {'sha256': digest(path), 'bytes': path.stat().st_size}
    return files


def previous_package(output, kind):
    manifest = json.loads((output / MANIFEST).read_text())
    if (manifest.get('manifest_format') != 1 or manifest.get('kind') != kind
            or manifest.get('build_id') != package_identity(manifest)
            or manifest.get('files') != package_files(output)):
        raise ValueError('Existing output is not a verified matching managed Chibi package')
    return manifest


def check_exports(stage, kind, cache, build):
    cmake = stage / 'lib/cmake/Chibi'
    config = cmake / 'ChibiConfig.cmake'
    targets_file = cmake / 'ChibiTargets.cmake'
    if not config.is_file() or not targets_file.is_file():
        raise ValueError('Installation has no Chibi CMake package exports')
    target_text = targets_file.read_text()
    targets = set(re.findall(r'add_library\(Chibi::([A-Za-z_0-9]+)\s', target_text))
    expected = CORE_TARGETS | (COMPILER_TARGETS if kind == 'sdk' else set())
    if kind == 'sdk' or cache.get('CHIBI_SDK_BUILD_NATIVE') == 'ON':
        expected |= VALUE_TARGETS
    if cache.get('CHIBI_SDK_BUILD_NATIVE') == 'ON':
        expected |= NATIVE_TARGETS
        if cmake_metadata(build, 'CMakeSystem.cmake').get('CMAKE_SYSTEM_NAME') != 'Emscripten' or cache.get('CHIBI_WEB_THREADS') == 'ON':
            expected |= {'task_scheduler'}
        if kind == 'sdk':
            expected |= {'studio_catalog'} | NATIVE_AUTHORING_TARGETS
    if cache.get('CHIBI_SDK_BUILD_DESKTOP') == 'ON':
        expected |= DESKTOP_TARGETS
        if kind == 'sdk':
            expected |= DESKTOP_AUTHORING_TARGETS
    if cache.get('CHIBI_ENABLE_SJLIT', 'ON') == 'ON':
        expected |= {'execution_sljit'}
    if targets != expected:
        raise ValueError(f'Exported targets differ from {kind} contract: expected {sorted(expected)}, got {sorted(targets)}')
    for path in cmake.glob('*.cmake'):
        text = path.read_text()
        if str(ROOT) in text or str(build) in text:
            raise ValueError(f'Installed CMake file retains a source/build path: {path.name}')
        if kind == 'runtime' and re.search(r'Chibi::(?:syntax_runtime|language\w*|compiler|authoring|asset_text|asset_kdl)\b|tree.sitter', text, re.I):
            raise ValueError(f'Runtime export contains a compiler/parser dependency: {path.name}')
    includes = stage / 'include/chibi'
    header_owners = CORE_TARGETS | (COMPILER_TARGETS if kind == 'sdk' else set())
    if kind == 'sdk' or cache.get('CHIBI_SDK_BUILD_NATIVE') == 'ON':
        header_owners |= VALUE_TARGETS
    if cache.get('CHIBI_SDK_BUILD_NATIVE') == 'ON':
        header_owners |= NATIVE_TARGETS
        if kind == 'sdk':
            header_owners |= {'studio_catalog'} | NATIVE_AUTHORING_TARGETS
    if cache.get('CHIBI_SDK_BUILD_DESKTOP') == 'ON':
        header_owners |= DESKTOP_TARGETS
        if kind == 'sdk':
            header_owners |= DESKTOP_AUTHORING_TARGETS
    if cache.get('CHIBI_SDK_BUILD_DESKTOP') == 'ON':
        header_owners |= {'desktop_vendor'}
    for owner in includes.iterdir():
        if not owner.is_dir() or owner.name not in header_owners:
            raise ValueError(f'Undeclared public header owner: {owner.name}')
    for target, properties in re.findall(r'set_target_properties\(Chibi::(\w+) PROPERTIES\s+(.*?)\n\)', target_text, re.S):
        exported = re.search(r'INTERFACE_INCLUDE_DIRECTORIES "([^"]*)"', properties)
        if exported and exported[1] != '${_IMPORT_PREFIX}/include/chibi/' + target:
            raise ValueError(f'{target} exports headers outside its owned include directory')
    if kind == 'runtime':
        for path in includes.rglob('*'):
            if path.is_dir() and path.name in ('language', 'authoring', 'tree_sitter', 'editor', 'graph'):
                raise ValueError(f'Runtime package unexpectedly exports {path.name} headers')
        for path in (stage / 'lib').glob('*'):
            if path.is_file() and re.search(r'(syntax|language|compiler|authoring|tree.sitter|asset_text|asset_kdl)', path.name, re.I):
                raise ValueError(f'Runtime package unexpectedly contains {path.name}')
    else:
        for header in ('language_syntax/language/syntax.h', 'compiler/language/lower.h', 'authoring/authoring/source_prepare.h'):
            if not (includes / header).is_file():
                raise ValueError(f'SDK package is missing {header}')
    for header in ('execution/execution/prepared.h', 'behavior_runtime/runtime/behaviors/package.h',
                   'capabilities/capabilities/catalog.h',
                   'native_capabilities/runtime/native_capabilities.h',
                   'events/runtime/events.h', 'ecs/ecs/ecs.h', 'tasks/runtime/tasks.h',
                   'scene_runtime/runtime/scenes/scene.h', 'scene_runtime/runtime/scenes/prototypes.h',
                   'scheduling/runtime/systems/scheduler.h', 'fields/runtime/fields/prepared.h',
                   'field_catalog/runtime/material_field.h'):
        if not (includes / header).is_file():
            raise ValueError(f'Package is missing {header}')
    return sorted(f'Chibi::{target}' for target in targets)


def package_version(stage):
    path = stage / 'lib/cmake/Chibi/ChibiConfigVersion.cmake'
    if not path.is_file():
        raise ValueError('Installation has no Chibi package version contract')
    match = re.search(r'set\(PACKAGE_VERSION "([0-9.]+)"\)', path.read_text())
    if not match:
        raise ValueError('Configured Chibi package version was not found')
    return match[1]


def assemble(build, output, kind):
    cache = cache_values(build)
    if Path(cache.get('CMAKE_HOME_DIRECTORY', '')).resolve() != ROOT / 'sdk':
        raise ValueError('Package from an independently configured sdk/ build, not the Studio build')
    expected = 'ON' if kind == 'sdk' else 'OFF'
    if cache.get('CHIBI_SDK_BUILD_COMPILER') != expected:
        raise ValueError(f'{kind} packaging requires CHIBI_SDK_BUILD_COMPILER={expected}')
    if output == ROOT or output in ROOT.parents or output == build or output in build.parents or build in output.parents:
        raise ValueError('Package output must be outside the source/build ancestry and build tree')
    previous_identity = previous_package(output, kind) if output.exists() else None
    compiled = build_identity(build, cache)
    compiler = cmake_metadata(build, 'CMakeCCompiler.cmake')
    system = cmake_metadata(build, 'CMakeSystem.cmake')
    if system.get('CMAKE_SYSTEM_NAME') == 'Emscripten' and cache.get('CHIBI_ENABLE_SJLIT', 'ON') != 'OFF':
        raise ValueError('Emscripten SDK packaging requires CHIBI_ENABLE_SJLIT=OFF')
    output.parent.mkdir(parents=True, exist_ok=True)
    with staging_directory(prefix=f'.{output.name}-', dir=output.parent) as temporary:
        stage = Path(temporary) / output.name
        subprocess.run(['cmake', '--install', str(build), '--prefix', str(stage)], check=True)
        targets = check_exports(stage, kind, cache, build)
        docs = stage / 'share/chibi/docs'
        docs.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'sdk/README.md', docs / 'sdk.md')
        for name in ('LICENSE', 'LICENSE.md', 'LICENSE.txt'):
            if (ROOT / name).is_file():
                licenses = stage / 'share/chibi/licenses/chibi'
                licenses.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, licenses / name)
        engine = engine_manifest(stage, compiled)
        files = package_files(stage)
        vendor = stage / 'lib/cmake/Chibi/ChibiDesktopDependencies.cmake'
        vendor_text = vendor.read_text() if vendor.is_file() else ''
        for name, checksum in compiled['archives'].items():
            installed = 'lib/' + Path(name).name
            if installed not in files:
                target = compiled['archive_targets'][name]
                key = re.sub(r'[^A-Za-z0-9_]', '_', target)
                owner = 'Chibi::desktop_' + key
                declarations = re.findall(r'set_target_properties\(' + re.escape(owner) +
                                          r' PROPERTIES IMPORTED_LOCATION "\$\{_chibi_desktop_prefix\}/([^"\n]+)"\)', vendor_text)
                if cache.get('CHIBI_SDK_BUILD_DESKTOP') != 'ON' or len(declarations) != 1:
                    raise ValueError(f'Built archive has no installed owner: {name}')
                installed = declarations[0]
                if not installed.startswith('lib/chibi-vendor/') or '..' in Path(installed).parts:
                    raise ValueError(f'Invalid desktop archive install path: {installed}')
            if installed not in files or files[installed]['sha256'] != checksum:
                raise ValueError(f'Installed archive differs from compiled evidence: {installed}')
        for name, evidence in compiled['executables'].items():
            installed = 'bin/' + Path(evidence['path']).name
            binary = stage / installed
            if installed not in files or files[installed]['sha256'] != evidence['sha256'] or not os.access(binary, os.X_OK):
                raise ValueError(f'Installed executable differs from compiled evidence: {installed}')
        installed_products = {path.name for path in (stage / 'bin').glob('*') if path.is_file()}
        expected_products = {Path(value['path']).name for value in compiled['executables'].values()}
        if installed_products != expected_products:
            raise ValueError('Installed executables differ from the configured product contract')
        if canonical_digest(build_identity(build, cache)) != canonical_digest(compiled):
            raise ValueError('Build inputs or outputs changed during packaging')
        headers = {name: value['sha256'] for name, value in files.items() if name.startswith('include/')}
        runtime_headers = {name: checksum for name, checksum in headers.items()
                           if name.startswith('include/chibi/') and name.split('/')[2] in CORE_TARGETS}
        formats = compiled['configured_engine']['artifact_formats']
        prepared = formats['execution']['version']
        manifest = {
            'manifest_format': 1, 'kind': kind,
            'package_version': package_version(stage),
            'engine': {'manifest': 'share/chibi/engine.json', 'build_id': engine['build_id']},
            'source': source_identity(), 'files': files, 'targets': targets,
            'products': {name: 'bin/' + Path(evidence['path']).name
                         for name, evidence in compiled['executables'].items()},
            'requirements': {
                'c_standard': 23, 'prepared_format': prepared, 'artifact_formats': formats,
                'project_format': engine['project_version'],
                'system': system.get('CMAKE_SYSTEM_NAME'), 'processor': system.get('CMAKE_SYSTEM_PROCESSOR'),
                'pointer_bits': int(compiler['CMAKE_C_SIZEOF_DATA_PTR']) * 8,
                'byte_order': compiler.get('CMAKE_C_BYTE_ORDER'),
                **({'web_threads': cache.get('CHIBI_WEB_THREADS') == 'ON',
                    'web_workers': int(cache.get('CHIBI_WEB_WORKERS', '4'))}
                   if system.get('CMAKE_SYSTEM_NAME') == 'Emscripten' else {}),
                'public_headers_sha256': canonical_digest(headers),
                'runtime_headers_sha256': canonical_digest(runtime_headers),
            },
            'build': {
                'inputs': compiled,
                'configuration': cache.get('CMAKE_BUILD_TYPE'),
                'compiler_id': compiler.get('CMAKE_C_COMPILER_ID'),
                'compiler_version': compiler.get('CMAKE_C_COMPILER_VERSION'),
                'compiler_target': cache.get('CMAKE_C_COMPILER_TARGET'),
                'c_flags': cache.get('CMAKE_C_FLAGS', ''),
                'configuration_c_flags': cache.get('CMAKE_C_FLAGS_' + cache.get('CMAKE_BUILD_TYPE', '').upper(), ''),
                'native_backend': cache.get('CHIBI_ENABLE_SJLIT', 'ON') == 'ON',
            },
        }
        manifest['build_id'] = package_identity(manifest)
        manifest_path = stage / MANIFEST
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        current_identity = previous_package(output, kind) if output.exists() else None
        if current_identity != previous_identity:
            raise ValueError('Existing SDK output changed during packaging')
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


def archive_package(output, archive):
    if archive == output or output in archive.parents:
        raise ValueError('Archive must be outside the package directory')
    archive.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=archive.parent, delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        with tarfile.open(temporary_path, 'w:gz') as tar:
            tar.add(output, arcname=output.name)
        temporary_path.replace(archive)
    finally:
        temporary_path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kind', choices=('sdk', 'runtime'), required=True)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    try:
        output = args.output.resolve()
        if args.archive and (args.archive.resolve() == output or output in args.archive.resolve().parents):
            raise ValueError('Archive must be outside the package directory')
        manifest = assemble(args.build.resolve(), output, args.kind)
        if args.archive:
            archive_package(output, args.archive.resolve())
        print(f'Packaged {args.kind} {output} ({manifest["build_id"]})')
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'package-sdk: {error}\n')


if __name__ == '__main__':
    main()
