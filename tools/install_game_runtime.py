#!/usr/bin/env python3
"""Refresh registered game runtimes from an already built native package."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from contextlib import ExitStack
from pathlib import Path, PurePosixPath

import kdl_asset

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path('share/chibi/engine.json')


def validate_package(package):
    manifest = json.loads((package / MANIFEST).read_text())
    files = manifest.get('files')
    if (manifest.get('format') != 1 or not isinstance(files, dict) or not files
            or type(manifest.get('project_version')) is not int):
        raise ValueError('Invalid runtime package manifest')
    identity = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if manifest.get('build_id') != identity:
        raise ValueError('Runtime package build ID does not match its file manifest')
    paths = list(package.rglob('*'))
    if any(path.is_symlink() for path in paths):
        raise ValueError('Runtime package must contain regular files, not symlinks')
    actual = {path.relative_to(package).as_posix() for path in paths if path.is_file()}
    if actual != set(files) | {MANIFEST.as_posix()}:
        raise ValueError('Runtime package file list does not match its manifest')
    for name, expected in files.items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'Invalid runtime package path: {name}')
        if hashlib.sha256((package / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f'Runtime package checksum mismatch: {name}')
    for name in ('chibi', 'chibi_ctl', 'chibi-mcp'):
        binary = package / 'bin' / name
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise ValueError(f'Missing native runtime executable: {name}')
        if not binary.stat().st_size or binary.read_bytes().startswith(b'\0asm'):
            raise ValueError(f'Invalid native runtime executable: {name}')
    if not (package / 'share/chibi/mcp/setup-codex.mjs').is_file():
        raise ValueError('Missing packaged MCP setup module')
    return manifest


def absolute_path(value):
    path = Path(value)
    if not path.is_absolute():
        raise ValueError(f'Runtime registration paths must be absolute: {value}')
    return path


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as file:
        staged = Path(file.name)
        file.write(text)
    try:
        staged.replace(path)
    finally:
        staged.unlink(missing_ok=True)


def game_config(game, setup):
    path = game / '.codex/config.toml'
    config = path.read_text() if path.exists() else ''
    registration = '\n'.join((
        '[mcp_servers.chibi]', 'command = "sh"',
        f'args = [{json.dumps(str(game / ".chibi/runtime/bin/chibi-mcp"))}, {json.dumps(str(game))}]',
        f'cwd = {json.dumps(str(game))}', 'enabled = true',
        'startup_timeout_sec = 120', 'tool_timeout_sec = 120'))
    script = ('import {registerChibi} from ' + json.dumps(setup.as_uri()) + ';'
              'import {readFileSync} from "node:fs";'
              'const value=JSON.parse(readFileSync(0,"utf8"));'
              'process.stdout.write(registerChibi(value.config,value.registration));')
    return subprocess.run(['node', '--input-type=module', '-e', script],
                          input=json.dumps({'config': config, 'registration': registration}),
                          text=True, capture_output=True, check=True).stdout


def install(package, config):
    if not config.is_file():
        return None
    registration = json.loads(config.read_text())
    games = [absolute_path(value) for value in registration['games']]
    manifest = validate_package(package)
    prepared = []
    for game in games:
        project = kdl_asset.loads((game / 'project.kdl').read_text())
        if (not isinstance(project, dict) or type(project.get('version')) is not int
                or project['version'] != manifest['project_version']):
            raise ValueError(f'Project version mismatch: {game}')
        runtime = game / '.chibi/runtime'
        for path in (game / '.chibi', runtime, game / '.codex', game / '.codex/config.toml',
                     game / '.chibi-engine.json'):
            if path.is_symlink():
                raise ValueError(f'Runtime paths must stay inside the game: {path}')
        if runtime.exists():
            if not (runtime / MANIFEST).is_file():
                raise ValueError(f'Existing runtime directory is not an engine package: {runtime}')
            validate_package(runtime)
        originals = {path: path.read_bytes() if path.exists() else None
                     for path in (game / '.codex/config.toml', game / '.chibi-engine.json')}
        prepared.append((game, game_config(game, package / 'share/chibi/mcp/setup-codex.mjs'), originals))
    pin = json.dumps({'format': 1, 'build_id': manifest['build_id'],
                      'project_version': manifest['project_version']}, indent=2) + '\n'
    with ExitStack() as copies:
        staged_games = []
        for game, codex, originals in prepared:
            (game / '.chibi').mkdir(exist_ok=True)
            directory = Path(copies.enter_context(tempfile.TemporaryDirectory(
                prefix='.runtime-stage-', dir=game / '.chibi')))
            staged = directory / 'package'
            shutil.copytree(package, staged)
            validate_package(staged)
            staged_games.append((game, staged, codex, originals))
        activated = []
        try:
            for game, staged, codex, originals in staged_games:
                runtime = game / '.chibi/runtime'
                previous = staged.parent / 'previous'
                if runtime.exists():
                    runtime.rename(previous)
                try:
                    staged.rename(runtime)
                except OSError:
                    if previous.exists():
                        previous.rename(runtime)
                    raise
                activated.append((runtime, previous, originals))
                atomic_write(game / '.codex/config.toml', codex)
                atomic_write(game / '.chibi-engine.json', pin)
        except OSError:
            for runtime, previous, originals in reversed(activated):
                shutil.rmtree(runtime)
                if previous.exists():
                    previous.rename(runtime)
                for path, value in originals.items():
                    if value is None:
                        path.unlink(missing_ok=True)
                    else:
                        atomic_write(path, value.decode())
            raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--config', type=Path, default=ROOT / '.chibi/game-runtime.json')
    args = parser.parse_args()
    try:
        manifest = install(args.package.resolve(), args.config)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'install-game-runtime: {error}\n')
    if manifest:
        print(f'Installed game runtime {manifest["build_id"]}')


if __name__ == '__main__':
    main()
