#!/usr/bin/env python3
"""Read-only validation for exact engine release packages and publication state."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile

SHA = re.compile(r'[0-9a-f]{40}\Z')
ARCHIVES = ('chibi-export-linux-x64.tar.gz', 'chibi-sdk-linux-x64.tar.gz',
            'chibi-web-sdk.tar.gz')
MEMBERS = {*ARCHIVES, 'build.json'}
ASSETS = MEMBERS | {'SHA256SUMS'}


def api(repository, path):
    result = subprocess.run(['gh', 'api', '--method', 'GET', f'repos/{repository}/{path}'],
                            text=True, capture_output=True, check=False, timeout=120)
    if result.returncode:
        if '(HTTP 404)' in result.stderr:
            return None
        raise ValueError(f'GitHub GET failed: {path}')
    return json.loads(result.stdout)


def inspect(repository, engine):
    tag = 'engine-' + engine
    release = api(repository, 'releases/tags/' + tag)
    ref = api(repository, 'git/ref/tags/' + tag)
    if ref and (ref['object']['type'] != 'commit' or ref['object']['sha'] != engine):
        raise ValueError('Engine tag does not point directly at the selected commit')
    complete = False
    if release:
        if release['tag_name'] != tag:
            raise ValueError('Unexpected engine release tag')
        names = [a['name'] for a in release['assets']]
        if len(names) != len(set(names)) or not set(names) <= ASSETS:
            raise ValueError('Engine release has unknown or duplicate assets; manual review required')
        if not release['draft']:
            assets = release['assets']
            if (not ref or len(assets) != len(ASSETS)
                    or {a['name'] for a in assets} != ASSETS
                    or any(a['state'] != 'uploaded' or a['size'] <= 0 for a in assets)):
                raise ValueError('Published engine release is incomplete; refusing to overwrite it')
            complete = True
    return {'complete': complete, 'tag_exists': ref is not None,
            'release_exists': release is not None}


def verify_draft(repository, directory, engine, emsdk):
    validate(directory, engine, emsdk)
    tag = 'engine-' + engine
    record = api(repository, 'releases/tags/' + tag)
    ref = api(repository, 'git/ref/tags/' + tag)
    if (not record or not record['draft'] or record['tag_name'] != tag
            or not ref or ref['object']['type'] != 'commit' or ref['object']['sha'] != engine):
        raise ValueError('Expected the selected engine draft and direct commit tag')
    assets = record['assets']
    if len(assets) != len(ASSETS) or {a['name'] for a in assets} != ASSETS:
        raise ValueError('Draft must contain exactly the five release assets')
    for asset in assets:
        path = directory / asset['name']
        with path.open('rb') as source:
            digest = 'sha256:' + hashlib.file_digest(source, 'sha256').hexdigest()
        if (asset['state'] != 'uploaded' or asset['size'] != path.stat().st_size
                or asset.get('digest') != digest):
            raise ValueError('Uploaded draft asset differs from verified package: ' + asset['name'])


def validate(directory, engine, emsdk):
    if {p.name for p in directory.iterdir()} != ASSETS:
        raise ValueError('Package directory must contain exactly the five release assets')
    if any(p.is_symlink() or not p.is_file() or p.stat().st_size <= 0 for p in directory.iterdir()):
        raise ValueError('Release assets must be nonempty regular files')
    checksums = {}
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([a-zA-Z0-9.-]+)', line)
        if not match or match[2] in checksums:
            raise ValueError('Invalid or duplicate checksum entry')
        checksums[match[2]] = match[1]
    if set(checksums) != MEMBERS:
        raise ValueError('Checksums must cover exactly the four package members')
    for name, expected in checksums.items():
        with (directory / name).open('rb') as source:
            actual = hashlib.file_digest(source, 'sha256').hexdigest()
        if actual != expected:
            raise ValueError('Checksum mismatch: ' + name)
    record = json.loads((directory / 'build.json').read_text())
    if (record.get('format') != 1 or record.get('engine_sha') != engine
            or record.get('emsdk_version') != emsdk or record.get('web_threads') is not False
            or record.get('platform') != 'ubuntu-24.04-x64'):
        raise ValueError('Build record does not identify the selected serial engine package')
    for name in ARCHIVES:
        expected = ('sdk/' if name == ARCHIVES[0] else '') + 'share/chibi/package.json'
        with tarfile.open(directory / name, 'r:gz') as archive:
            manifests = [m for m in archive.getmembers() if m.name.removeprefix('./') == expected]
            if len(manifests) != 1 or not manifests[0].isfile():
                raise ValueError('Archive must contain one regular SDK manifest: ' + name)
            source = json.load(archive.extractfile(manifests[0]))['source']
        if source.get('dirty') is not False or source.get('revision') != engine:
            raise ValueError('Archive SDK does not identify the clean selected engine: ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('inspect', 'validate', 'verify-draft'))
    parser.add_argument('--engine', required=True)
    parser.add_argument('--repository', default='waldnercharles/chibi')
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--emsdk')
    args = parser.parse_args()
    if not SHA.fullmatch(args.engine):
        parser.error('engine must be a full lowercase commit SHA')
    if args.command in ('validate', 'verify-draft'):
        if args.directory is None or args.emsdk is None:
            parser.error('validate requires directory and emsdk')
        if args.command == 'validate':
            validate(args.directory, args.engine, args.emsdk)
        else:
            verify_draft(args.repository, args.directory, args.engine, args.emsdk)
        return
    values = inspect(args.repository, args.engine)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        for key, value in values.items():
            print(f'{key}={str(value).lower()}', file=output)


if __name__ == '__main__':
    main()
