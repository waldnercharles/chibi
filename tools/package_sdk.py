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
                'asset_values', 'value_document', 'capabilities', 'native_capabilities', 'native_prepared', 'core_capabilities', 'array_capabilities', 'field_capabilities', 'state_capabilities', 'selection_capabilities', 'set_capabilities', 'comparison_capabilities', 'conversion_capabilities', 'text_capabilities', 'fmt', 'host_native_catalog', 'host_clock', 'scene_runtime', 'scheduling', 'fields', 'field_catalog', 'tasks', 'shader_runtime', 'model_runtime', 'ui_documents', 'input_action'}
COMPILER_TARGETS = {'program', 'image_preparation', 'builtin_fonts', 'asset_json', 'source_catalog', 'syntax_runtime', 'language_syntax', 'language', 'json', 'compiler', 'authoring', 'studio_capabilities', 'component_presentation', 'project_document', 'input_map', 'prototype_document', 'scene_document', 'reference_index', 'asset_text', 'asset_kdl', 'text_source', 'ui_document', 'ui_authoring', 'ui_syntax'}
NATIVE_TARGETS = {'visual_catalog', 'json', 'native_types', 'declared_components', 'native_bindings', 'tilemap_runtime', 'spatial_runtime', 'entity_runtime', 'audio_runtime', 'url_runtime', 'particle_runtime', 'save_runtime', 'ui_runtime', 'input_runtime', 'host_native_default', 'visibility_runtime', 'engine', 'player', 'material_runtime', 'physics_runtime', 'physics3d_runtime', 'task_pool_runtime', 'box2d', 'box3d'}
VALUE_TARGETS = {'math', 'vector_capabilities', 'math_capabilities', 'arithmetic_capabilities', 'mesh_runtime', 'model_bake'}
DESKTOP_TARGETS = {'builtin_fonts', 'platform', 'ui', 'render'}
NATIVE_AUTHORING_TARGETS = {'compiler_syntax', 'compiler_json', 'shader_preparation', 'project_compilation', 'scene_authoring', 'asset_authoring', 'scenario_document'}
DESKTOP_AUTHORING_TARGETS = {'project_preparation'}


# Private tools retain their compiled identity. Build-host implementation objects
# link directly; the compiler host consumes its separately installed runtime.
PRIVATE_TOOL_PATHS = {
    'chibi-build-host': 'libexec/chibi/chibi-build-host',
    'chibi-compiler-host': 'libexec/chibi/chibi-compiler-host',
}
PRIVATE_OBJECT_OWNERS = {
    'chibi_build_host': 'chibi-build-host',
}
PRODUCT_OBJECT_OWNERS = {
    'chibi_application': ('chibi',),
}
PRIVATE_ARCHIVE_OWNERS = {
    'chibi_compiler_host': 'chibi-compiler-host',
}


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
    if products == {'chibi-player', 'chibi-compile'}:
        products.add('chibi')
    return products


def private_tool_targets(cache, system=None):
    system = system or cache.get('CMAKE_SYSTEM_NAME')
    if cache.get('CHIBI_SDK_BUILD_COMPILER') != 'ON':
        return {}
    if system in ('Darwin', 'Linux'):
        return dict(PRIVATE_TOOL_PATHS)
    if system == 'Windows':
        return {'chibi-compiler-host': PRIVATE_TOOL_PATHS['chibi-compiler-host'] + '.exe'}
    return {}


def installed_private_tools(compiled, files, stage):
    installed = {}
    for name, evidence in compiled.get('private_executables', {}).items():
        destination = evidence['install_path']
        if (destination not in files or files[destination]['sha256'] != evidence['sha256']
                or not os.access(stage / destination, os.X_OK)):
            raise ValueError(f'Installed private executable differs from compiled evidence: {destination}')
        installed[name] = destination
    actual = {path.relative_to(stage).as_posix() for path in (stage / 'libexec').rglob('*')
              if path.is_file()}
    if actual != set(installed.values()):
        raise ValueError('Installed private executables differ from the configured tool contract')
    return installed


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


def ninja_input_edges(build, names):
    """Actual explicit/implicit inputs, never scheduling-only ownership."""
    output = subprocess.check_output(['ninja', '-C', str(build), '-t', 'query', *names], text=True)
    edges = {}
    current = None
    reading = False
    for line in output.splitlines():
        if line and not line.startswith(' '):
            current = line.removesuffix(':')
            if current in edges:
                raise ValueError(f'Duplicate Ninja input record: {current}')
            edges[current] = []
            reading = False
        elif line.startswith('  input:'):
            reading = True
        elif line.startswith('  ') and not line.startswith('    '):
            reading = False
        elif current is not None and reading and line.startswith('    '):
            name = line[4:]
            if not name.startswith(('|| ', '|@ ')):
                edges[current].append(name.removeprefix('| '))
    if set(edges) != set(names):
        raise ValueError('Ninja input records differ from requested outputs')
    return edges


def configured_package_targets(build, cache):
    path = build / ('chibi_package_targets-' + cache.get('CMAKE_BUILD_TYPE', '') + '.txt')
    lines = path.read_text().splitlines()
    if not lines or lines[0] != '1':
        raise ValueError('Invalid configured package target manifest; reconfigure first')
    targets = {}
    outputs = set()
    for line in lines[1:]:
        fields = line.split('\t')
        if len(fields) != 3 or not fields[1] or fields[0] not in ('STATIC_LIBRARY', 'EXECUTABLE', 'OBJECT_LIBRARY'):
            raise ValueError('Invalid configured package target record')
        kind, target, name = fields
        if kind == 'OBJECT_LIBRARY':
            if name:
                raise ValueError(f'Object target has an artifact path: {target}')
            relative = None
        else:
            output = Path(name)
            if not output.is_absolute() or not output.resolve().is_relative_to(build):
                raise ValueError(f'Package target output is outside its build: {target}')
            relative = output.resolve().relative_to(build).as_posix()
        if target in targets or (relative is not None and relative in outputs):
            raise ValueError(f'Duplicate package target or output: {target}')
        targets[target] = {'type': kind, 'path': relative}
        if relative is not None:
            outputs.add(relative)
    return targets


def ninja_artifact_identity(build, target, output, dependencies, source_root):
    path = build / output
    if not path.is_file():
        raise ValueError(f'Build is incomplete: {output}')
    inputs = {}
    for name in sorted(set(dependencies)):
        dependency = (build / name).resolve()
        if not dependency.is_file():
            raise ValueError(f'Artifact link dependency is missing: {name}')
        if dependency.stat().st_mtime_ns > path.stat().st_mtime_ns:
            raise ValueError(f'Artifact {target} predates a link dependency; relink first')
        checksum = digest(dependency)
        key = dependency.relative_to(build).as_posix() if dependency.is_relative_to(build) else 'external/' + dependency.name + '-' + checksum[:16]
        inputs[key] = checksum
    command = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', '-s', output], text=True)
    lines = command.splitlines()
    if len(lines) != 1:
        raise ValueError(f'Expected one configured artifact command: {target}')
    arguments = [argument.replace(str(build), '@build').replace(str(source_root), '@source')
                 for argument in shlex.split(lines[0])]
    return {'path': output, 'sha256': digest(path), 'arguments': arguments,
            'link_command_sha256': canonical_digest(arguments), 'link_inputs': inputs}


def ninja_package_artifacts(build, cache, objects, allowed_products):
    targets = configured_package_targets(build, cache)
    for record in objects.values():
        if record['target'] not in targets:
            raise ValueError(f'Compiled target has no configured type: {record["target"]}')
    outputs = {record['path']: target for target, record in targets.items() if record['path'] is not None}
    edges = ninja_input_edges(build, sorted(outputs) + sorted(objects))
    artifacts = {}
    archives = {}
    archive_targets = {}
    archive_objects = {}
    executables = {}
    auxiliary = {}
    memberships = {name: [] for name in objects}
    # CMake's Windows resource compiler emits .res files. They require the
    # same recorded compile provenance and real artifact edge as object files.
    ordinary = {name for name in objects if Path(name).suffix in ('.o', '.obj', '.res')}
    for output, target in sorted(outputs.items()):
        inputs = [(build / name).resolve() for name in edges[output]]
        for path in inputs:
            if path.suffix in ('.o', '.obj', '.res') and (not path.is_relative_to(build) or path.relative_to(build).as_posix() not in ordinary):
                raise ValueError(f'Artifact has an unaccounted object input: {path.name}')
        members = sorted(path.relative_to(build).as_posix() for path in inputs
                         if path.is_relative_to(build) and path.relative_to(build).as_posix() in ordinary)
        if target in ('chibi', 'chibi-player', 'chibi-compile') and target not in allowed_products:
            raise ValueError(f'Executable {target} is outside the configured product contract')
        if not members:
            if (build / output).exists() or any(record['target'] == target for record in objects.values()):
                raise ValueError(f'Artifact has no compiled object input: {target}')
            # Only an absent auxiliary target with no compiled inputs is unbuilt.
            # Required product targets are checked exactly below.
            continue
        evidence = ninja_artifact_identity(build, target, output, edges[output], ROOT)
        artifacts[output] = evidence
        for name in members:
            memberships[name].append(output)
        if targets[target]['type'] == 'STATIC_LIBRARY':
            archives[output] = evidence['sha256']
            archive_targets[output] = target
            archive_objects[output] = members
        elif target in allowed_products:
            executables[target] = evidence
        else:
            auxiliary[target] = evidence
    for name, record in objects.items():
        if name in ordinary:
            if not memberships[name]:
                raise ValueError(f'Compiled output has no linked artifact: {name}')
            record['artifacts'] = sorted(memberships[name])
            continue
        if Path(name).suffix not in ('.pch', '.gch'):
            raise ValueError(f'Unsupported compiler output: {name}')
        consumers = sorted(output for output in ordinary if any(
            (build / dependency).resolve() == build / name for dependency in edges[output]))
        if not consumers:
            raise ValueError(f'Precompiled header has no owned consumer: {name}')
        for consumer in consumers:
            if (build / name).stat().st_mtime_ns > (build / consumer).stat().st_mtime_ns:
                raise ValueError(f'Object predates its precompiled header: {consumer}')
        record['consumers'] = consumers
    if set(executables) != allowed_products:
        raise ValueError(f'Built executables differ from product contract: {sorted(executables)}')
    return {'archives': archives, 'archive_targets': archive_targets, 'archive_objects': archive_objects,
            'artifacts': artifacts, 'executables': executables, 'auxiliary_executables': auxiliary,
            'configured_targets': targets, 'configured_targets_sha256': canonical_digest(targets)}


def installed_archive_owners(compiled, files):
    installed = {}
    pending = []
    for name, checksum in compiled['archives'].items():
        destination = 'lib/' + Path(name).name
        if destination not in files:
            pending.append(name)
            continue
        if destination not in files or files[destination]['sha256'] != checksum:
            raise ValueError(f'Installed archive differs from compiled evidence: {destination}')
        installed[name] = {'path': destination}
    # A STATIC library may be used solely through TARGET_OBJECTS. Retain its
    # archive evidence and require every object to have a real installed owner.
    members = compiled.get('archive_objects', {})
    direct = set(installed)
    for name in pending:
        covered = set()
        if not members.get(name):
            raise ValueError(f'Built archive has no installed owner: {name}')
        for object_name in members[name]:
            owners = {owner for owner in direct if object_name in members.get(owner, [])}
            if not owners:
                raise ValueError(f'Built archive has no installed owner: {name}')
            covered.update(owners)
        installed[name] = {'folded_into': sorted(covered)}
    return installed


def build_identity(build, cache):
    metadata_path = build / 'chibi_engine_metadata.json'
    if not metadata_path.is_file():
        raise ValueError('Packaging requires configured engine metadata')
    metadata_bytes = metadata_path.read_bytes()
    metadata = json.loads(metadata_bytes)
    version = metadata.get('project_version')
    if type(version) is not int or version < 1:
        raise ValueError('Configured engine project version is invalid')
    sdk_version = metadata.get('sdk_version')
    if type(sdk_version) is not str or not sdk_version:
        raise ValueError('Configured SDK version is missing; reconfigure first')
    formats = metadata.get('artifact_formats')
    if not isinstance(formats, dict) or 'execution' not in formats:
        raise ValueError('Configured artifact format requirements are missing; reconfigure first')
    configured = {'document': metadata, 'sdk_version': sdk_version, 'project_version': version,
                  'artifact_formats': formats, 'sha256': hashlib.sha256(metadata_bytes).hexdigest()}
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
    private_executables = {}
    system = cmake_metadata(build, 'CMakeSystem.cmake').get('CMAKE_SYSTEM_NAME')
    allowed_products = product_targets(cache, system)
    allowed_tools = private_tool_targets(cache, system)
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
        if cache.get('CMAKE_GENERATOR') != 'Ninja':
            if target in ('chibi', 'chibi-player', 'chibi-compile') or target in PRIVATE_TOOL_PATHS:
                if target not in allowed_products and target not in allowed_tools:
                    raise ValueError(f'Executable {target} is outside the configured product contract')
                identities = private_executables if target in allowed_tools else executables
                if target not in identities:
                    identities[target] = executable_identity(build, cache, target, target_directory, source_root=ROOT)
                executable = build / identities[target]['path']
                if executable.stat().st_mtime_ns < object_path.stat().st_mtime_ns:
                    raise ValueError(f'Executable {target} predates a rebuilt object; relink first')
            elif (PRIVATE_OBJECT_OWNERS.get(target) in allowed_tools
                  or any(owner in allowed_products for owner in PRODUCT_OBJECT_OWNERS.get(target, ()))):
                # Application host objects link directly into installed tools.
                pass
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
        if cache.get('CMAKE_GENERATOR') == 'Ninja' and object_path.suffix == '.res':
            # Resource payloads can be declared with OBJECT_DEPENDS without
            # appearing in the compiler's preprocessor dependency log.
            dependencies.update(str((build / name).resolve())
                                for name in ninja_input_edges(build, [relative])[relative])
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
    def owned(name, record):
        if (not isinstance(record, dict) or not isinstance(record.get('source'), str)
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
    for name, record in formats.items():
        owned(name, record)
        if type(record.get('version')) is not int or record['version'] < 1:
            raise ValueError(f'Configured artifact format is invalid: {name}')
        if 'codec' in record:
            owned(name, record['codec'])
    artifact_records = {}
    if cache.get('CMAKE_GENERATOR') == 'Ninja':
        artifact_records = ninja_package_artifacts(build, cache, objects, allowed_products)
        archives = artifact_records['archives']
        archive_targets = artifact_records['archive_targets']
        executables = artifact_records['executables']
        auxiliary = artifact_records['auxiliary_executables']
        private_executables = {name: auxiliary.pop(name) for name in allowed_tools if name in auxiliary}
    if set(executables) != allowed_products:
        raise ValueError(f'Built executables differ from product contract: {sorted(executables)}')
    if set(private_executables) != set(allowed_tools):
        raise ValueError(f'Built private executables differ from tool contract: {sorted(private_executables)}')
    for name, evidence in private_executables.items():
        evidence['install_path'] = allowed_tools[name]
    for name, evidence in objects.items():
        for owner in PRODUCT_OBJECT_OWNERS.get(evidence['target'], ()):
            if owner in allowed_products and executables[owner]['link_inputs'].get(name) != evidence['sha256']:
                raise ValueError(f'Application host object has no installed executable owner: {name}')
        owner = PRIVATE_OBJECT_OWNERS.get(evidence['target'])
        if owner in allowed_tools:
            linked = private_executables[owner]['link_inputs']
            if linked.get(name) != evidence['sha256']:
                raise ValueError(f'Private host object has no installed executable owner: {name}')
    for target, owner in PRIVATE_ARCHIVE_OWNERS.items():
        if owner not in allowed_tools:
            continue
        owned = [name for name, name_target in archive_targets.items() if name_target == target]
        if len(owned) != 1:
            raise ValueError(f'Private tool has no unique runtime archive: {owner}')
        name = owned[0]
        if private_executables[owner]['link_inputs'].get(name) != archives[name]:
            raise ValueError(f'Compiler runtime archive has no installed executable consumer: {name}')
    return {'configured_engine': configured,
            'object_count': len(objects), 'objects': objects, 'archives': archives,
            'private_executables': private_executables,
            'executables': executables, 'archive_targets': archive_targets,
            'inputs_sha256': canonical_digest(inputs), 'inputs': inputs, **artifact_records}


def engine_manifest(stage, compiled):
    # The configured build identity document, with the engine identity, which
    # describes the compiled inputs and archives. Package identity is computed
    # later from installed files, including this manifest.
    manifest = {**compiled['configured_engine']['document'], 'manifest_format': 1,
                'build_id': canonical_digest(compiled)}
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


def host_compiler_components(kind, cache, system):
    if kind != 'sdk' or cache.get('CHIBI_SDK_BUILD_COMPILER') != 'ON':
        return set()
    components = {'compiler_host'} if system in ('Linux', 'Darwin', 'Windows') else set()
    if cache.get('CHIBI_SDK_HAS_ASSET_COMPILER_HOST') == 'ON':
        if system not in ('Linux', 'Darwin'):
            raise ValueError(f'Asset compiler host is unavailable on {system}')
        components.add('asset_compiler_host')
    return components


def check_exports(stage, kind, cache, build):
    cmake = stage / 'lib/cmake/Chibi'
    config = cmake / 'ChibiConfig.cmake'
    targets_file = cmake / 'ChibiTargets.cmake'
    if not config.is_file() or not targets_file.is_file():
        raise ValueError('Installation has no Chibi CMake package exports')
    target_text = targets_file.read_text()
    targets = set(re.findall(r'add_library\(Chibi::([A-Za-z_0-9]+)\s', target_text))
    system = cmake_metadata(build, 'CMakeSystem.cmake').get('CMAKE_SYSTEM_NAME')
    host_components = host_compiler_components(kind, cache, system)
    expected = CORE_TARGETS | (COMPILER_TARGETS if kind == 'sdk' else set()) | host_components
    if kind == 'sdk' or cache.get('CHIBI_SDK_BUILD_NATIVE') == 'ON':
        expected |= VALUE_TARGETS
    if cache.get('CHIBI_SDK_BUILD_NATIVE') == 'ON':
        expected |= NATIVE_TARGETS
        if system != 'Emscripten' or cache.get('CHIBI_WEB_THREADS') == 'ON':
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
    header_owners = CORE_TARGETS | (COMPILER_TARGETS if kind == 'sdk' else set()) | host_components
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
    # Prebuilt third-party libraries the SDK links install their headers here.
    header_owners |= {'vendor'}
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
        for header in ('language_syntax/language/syntax.h', 'compiler/language/lower.h'):
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
        engine = engine_manifest(stage, compiled)
        files = package_files(stage)
        archive_installation = installed_archive_owners(compiled, files)
        for name, evidence in compiled['executables'].items():
            installed = 'bin/' + Path(evidence['path']).name
            binary = stage / installed
            if installed not in files or files[installed]['sha256'] != evidence['sha256'] or not os.access(binary, os.X_OK):
                raise ValueError(f'Installed executable differs from compiled evidence: {installed}')
        installed_products = {path.name for path in (stage / 'bin').glob('*') if path.is_file()}
        expected_products = {Path(value['path']).name for value in compiled['executables'].values()}
        if installed_products != expected_products:
            raise ValueError('Installed executables differ from the configured product contract')
        private_tools = installed_private_tools(compiled, files, stage)
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
            'private_tools': private_tools,
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
                'archive_installation': archive_installation,
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
