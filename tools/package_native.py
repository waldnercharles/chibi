#!/usr/bin/env python3
"""Assemble a native authoring package from a distribution build."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MCP_MODULES = (
    'server.mjs', 'authoring.mjs', 'controller.mjs', 'tool-catalog.mjs',
    'engine-package.mjs', 'package-launch.mjs', 'setup-codex.mjs',
)


def cache_values(build):
    values = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        match = re.match(r'([^#/:][^:]*):[^=]+=(.*)', line)
        if match:
            values[match[1]] = match[2]
    return values


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_notices(stage, build, cache):
    notices = stage / 'share/chibi/licenses'
    sources = {'third_party': ROOT / 'third_party'}
    for source in (build / '_deps').glob('*-src'):
        sources[source.name.removesuffix('-src')] = source
    for key, value in cache.items():
        if value and key.endswith('_SOURCE_DIR') and key != 'chibi_SOURCE_DIR':
            sources[key.removesuffix('_SOURCE_DIR')] = Path(value)
    package = cache.get('CHIBI_DEPENDENCY_PACKAGE')
    if package:
        for source in (Path(package).parent / '_deps').glob('*-src'):
            sources[source.name.removesuffix('-src')] = source
    for name, source in sources.items():
        resolved = source.resolve()
        if (not source.is_dir() or resolved == ROOT or resolved == stage.resolve()
                or resolved in stage.resolve().parents):
            continue
        for path in source.rglob('*'):
            if (path.is_file() and '.git' not in path.relative_to(source).parts
                    and path.suffix.lower() in ('', '.txt', '.md', '.rst')
                    and path.name.lower().startswith(('license', 'copying', 'notice', 'ofl'))):
                copy_file(path, notices / name / path.relative_to(source))
        for relative in ('docs/FTL.TXT', 'docs/GPLv2.TXT', 'libraries/imgui/LICENSE.txt'):
            path = source / relative
            if path.is_file():
                copy_file(path, notices / name / relative)


def source_identity():
    def git(*args):
        return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()
    try:
        return git('rev-parse', 'HEAD'), bool(git('status', '--porcelain', '--untracked-files=normal'))
    except (OSError, subprocess.CalledProcessError):
        return None, None


def assemble(build, output):
    if output == ROOT or output in ROOT.parents or output == build or output in build.parents:
        raise ValueError('Package output must not replace the source or build directory')
    if output.exists() and not (output / 'share/chibi/engine.json').is_file():
        raise ValueError('Existing output is not an engine package; choose a new directory')
    cache = cache_values(build)
    for key, expected in (('CHIBI_DISTRIBUTION', 'ON'),
                          ('CHIBI_DEVELOPMENT_RELOAD', 'OFF'),
                          ('CHIBI_ENABLE_SANITIZERS', 'OFF')):
        if cache.get(key) != expected:
            raise ValueError(f'Packaging requires {key}={expected}; run make package-native')
    for name in ('chibi', 'chibi_ctl'):
        if not (build / name).is_file() or not os.access(build / name, os.X_OK):
            raise ValueError(f'Missing executable {build / name}; build both native targets first')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f'.{output.name}-', dir=output.parent) as temporary:
        stage = Path(temporary) / output.name
        share = stage / 'share/chibi'
        for name in ('chibi', 'chibi_ctl'):
            copy_file(build / name, stage / 'bin' / name)
        copy_file(ROOT / 'tools/mcp/launch-packaged.sh', stage / 'bin/chibi-mcp')
        (stage / 'bin/chibi-mcp').chmod(0o755)
        for name in MCP_MODULES:
            copy_file(ROOT / 'tools/mcp' / name, share / 'mcp' / name)
        for source, name in (('src/render/shaders', 'shaders'),
                             ('src/editor/rmlui/resources', 'ui')):
            shutil.copytree(ROOT / source, share / name)
        copy_file(ROOT / 'projects/studio/project.yaml', share / 'studio/project.yaml')
        shutil.copytree(ROOT / 'projects/studio/content', share / 'studio/content')
        for name in ('native-package.md', 'mcp.md', 'authoring-navigation.md',
                     'component-nodes.md', 'graph-optimizer.md', 'neon-gpu-benchmarks.md'):
            copy_file(ROOT / 'docs' / name, share / 'docs' / name)
        copy_notices(stage, build, cache)
        version_source = (ROOT / 'src/assets/project.h').read_text()
        project_version = int(re.search(r'#define CHIBI_PROJECT_VERSION (\d+)', version_source)[1])
        files = {path.relative_to(stage).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in sorted(stage.rglob('*')) if path.is_file()}
        build_id = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        revision, dirty = source_identity()
        manifest = {'format': 1, 'build_id': build_id, 'project_version': project_version,
                    'platform': platform.system().lower(), 'arch': platform.machine(),
                    'source_revision': revision, 'source_dirty': dirty, 'files': files}
        (share / 'engine.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
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
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', type=Path, help='also write a gzip tar archive')
    args = parser.parse_args()
    try:
        output = args.output.resolve()
        if args.archive and (args.archive.resolve() == output or output in args.archive.resolve().parents):
            raise ValueError('Archive must be outside the package directory')
        manifest = assemble(args.build.resolve(), output)
        if args.archive:
            archive = args.archive.resolve()
            archive.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=archive.parent, delete=False) as temporary:
                temporary_path = Path(temporary.name)
            try:
                with tarfile.open(temporary_path, 'w:gz') as tar:
                    tar.add(output, arcname=output.name)
                temporary_path.replace(archive)
            finally:
                temporary_path.unlink(missing_ok=True)
        print(f'Packaged {output} (engine {manifest["build_id"]})')
    except (OSError, ValueError) as error:
        parser.exit(1, f'package-native: {error}\n')


if __name__ == '__main__':
    main()
