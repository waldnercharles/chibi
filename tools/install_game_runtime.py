#!/usr/bin/env python3
"""Install a verified standalone player package into registered games."""
import argparse
import json
import os
import shutil
import tempfile
import sys
from contextlib import ExitStack
from pathlib import Path

# Installed helpers must leave the checksum-pinned package unchanged.
sys.dont_write_bytecode = True

import kdl_asset
from package_player import validate_package, verify_files, valid_path
from package_sdk import canonical_digest, staging_directory

MANIFEST = Path('share/chibi/package.json')


def validate_previous(package):
    if (package / MANIFEST).is_file():
        return validate_package(package)
    # Only the exact formerly managed package may be retired during migration.
    # It is never accepted as a new player deployment input.
    previous = Path('share/chibi/engine.json')
    legacy = json.loads((package / previous).read_text())
    files = legacy.get('files')
    if (legacy.get('format') != 1 or not isinstance(files, dict) or not files
            or type(legacy.get('project_version')) is not int
            or legacy.get('build_id') != canonical_digest(files)):
        raise ValueError('Existing runtime is not a verified managed package')
    for name in files:
        valid_path(name)
    records = {name: {'sha256': checksum, 'bytes': (package / name).stat().st_size}
               for name, checksum in files.items()}
    verify_files(package, records, previous)
    for name in ('chibi', 'chibi_ctl'):
        binary = package / 'bin' / name
        if not binary.is_file() or not os.access(binary, os.X_OK) or not binary.stat().st_size:
            raise ValueError('Existing runtime is not the formerly managed native package')
    return legacy


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


def install(package, config):
    if not config.is_file():
        raise ValueError(f'Runtime registration file is missing: {config}')
    registration = json.loads(config.read_text())
    games = [absolute_path(value) for value in registration['games']]
    if len({game.resolve() for game in games}) != len(games):
        raise ValueError('Runtime registration contains duplicate game directories')
    manifest = validate_package(package)
    prepared = []
    for game in games:
        project = kdl_asset.loads((game / 'project.kdl').read_text())
        if (not isinstance(project, dict) or type(project.get('version')) is not int
                or project['version'] != manifest['requirements']['project_format']):
            raise ValueError(f'Project version mismatch: {game}')
        runtime = game / '.chibi/runtime'
        for path in (game / '.chibi', runtime, game / '.chibi-engine.json'):
            if path.is_symlink():
                raise ValueError(f'Runtime paths must stay inside the game: {path}')
        previous_identity = validate_previous(runtime) if runtime.exists() else None
        originals = {path: path.read_bytes() if path.exists() else None
                     for path in (game / '.chibi-engine.json',)}
        prepared.append((game, originals, previous_identity))
    pin = json.dumps({'format': 2, 'kind': 'player', 'build_id': manifest['build_id'],
                      'engine_build_id': manifest['engine']['build_id'],
                      'sdk_build_id': manifest['sdk']['build_id'],
                      'package_version': manifest['package_version'],
                      'requirements': manifest['requirements']}, indent=2) + '\n'
    with ExitStack() as copies:
        staged_games = []
        for game, originals, previous_identity in prepared:
            (game / '.chibi').mkdir(exist_ok=True)
            directory = Path(copies.enter_context(staging_directory(
                prefix='.runtime-stage-', dir=game / '.chibi')))
            staged = directory / 'package'
            shutil.copytree(package, staged)
            if validate_package(staged) != manifest:
                raise ValueError('Player package changed while it was copied')
            staged_games.append((game, staged, originals, previous_identity))
        activated = []
        try:
            for game, staged, originals, previous_identity in staged_games:
                runtime = game / '.chibi/runtime'
                previous = staged.parent / 'previous'
                if (game / '.chibi').is_symlink() or runtime.is_symlink():
                    raise ValueError(f'Runtime directory changed to a symlink: {runtime}')
                current_identity = validate_previous(runtime) if runtime.exists() else None
                if current_identity != previous_identity:
                    raise ValueError(f'Existing runtime changed during staging: {runtime}')
                for path, value in originals.items():
                    current = path.read_bytes() if path.exists() else None
                    if path.is_symlink() or current != value:
                        raise ValueError(f'Runtime pin changed during staging: {path}')
                if runtime.exists():
                    runtime.rename(previous)
                try:
                    staged.rename(runtime)
                except OSError:
                    if previous.exists():
                        previous.rename(runtime)
                    raise
                activated.append((runtime, previous, originals))
                atomic_write(game / '.chibi-engine.json', pin)
        except (OSError, ValueError):
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
    parser.add_argument('--config', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = install(args.package.resolve(), args.config)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f'install-game-runtime: {error}\n')
    if manifest:
        print(f'Installed game runtime {manifest["build_id"]}')


if __name__ == '__main__':
    main()
