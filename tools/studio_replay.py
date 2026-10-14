#!/usr/bin/env python3
"""Prepare and run a pinned authored replay inside test_policy's shared deadline."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

# This module lives in tools after promotion. No builds or independent deadlines.
from package_sdk import cache_values, digest
from package_studio import authored_build_identity

ROOT = Path(__file__).resolve().parents[1]
REQUESTS = {
    'home': ('b5228ab91dd50c99084e8971d8faeb44608c68fe0b6ed841428518a0dec88cea', 24),
    'material': ('ddbefa0643fb3a1dec71b78a7320cefc62cc89261d6e88aa7212e2458e12378f', 60),
}

MATERIAL_FILES = {'content/behaviors/Counter.chibi': 'ab072105e163873d9d414fdf1cbc35f48a28a8b874139531ea61c3c581ac57cd', 'content/scenes/Start.kdl': '4512130a3539013e0ec2e50c0c0427cb9f027f9f997d95379b6a47950aee745b', 'project.kdl': '44e344537da3df0ff16ad29300c12583ddd267723e3fba2bd2e01d7e2d6a33ad'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inventory(root):
    files = {}
    for path in sorted(root.rglob('*')):
        require(not path.is_symlink(), f'Symlink in replay input: {path}')
        if path.is_file():
            files[path.relative_to(root).as_posix()] = digest(path)
    return files


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def run(kind):
    require('CHIBI_TEST_DEADLINE' in os.environ, 'Run through tools/test_policy.py tool studio-authored-' + kind)
    build = Path(os.environ.get('CHIBI_STUDIO_BUILD_DIR', ROOT / 'build-studio')).resolve()
    source = Path(os.environ.get('CHIBI_STUDIO_SOURCE', ROOT.parent / 'chibi-studio')).resolve()
    output = ROOT / 'build/test/studio'
    output.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix=kind + '-', dir=output))
    print(f'Studio replay evidence: {evidence}', flush=True)
    receipt = {'scope': 'authored UI replay; setup and host share production supervisor deadline',
               'kind': kind, 'build': str(build), 'source': str(source),
               'platform': sys.platform, 'requested_backend': 'sljit'}
    try:
        identity = authored_build_identity(build, source_root=source)
        cache = cache_values(build)
        artifact = Path(cache['CHIBI_STUDIO_ARTIFACT']).resolve()
        installed = build / 'share/chibi/studio'
        sdk = build / 'share/chibi/sdk'
        host = build / 'bin' / ('chibi-author.exe' if os.name == 'nt' else 'chibi-author')
        manifest = json.loads((sdk / 'package.json').read_text())
        require(digest(host) == manifest['files'][manifest['products']['chibi-author']]['sha256'], 'Studio host differs from pinned SDK product')
        for path in sorted(sdk.rglob('*')):
            if path.is_file() and path.name != 'package.json':
                key = 'share/chibi/' + path.relative_to(sdk).as_posix()
                require(key in manifest['files'] and digest(path) == manifest['files'][key]['sha256'], f'Studio SDK resource changed: {key}')
        source_files = inventory(source / 'projects/studio')
        write(evidence / 'build-identity.json', identity)
        request = source / 'tests/acceptance' / f'studio_{kind}_replay.json'
        expected, frames = REQUESTS[kind]
        require(digest(request) == expected, 'Replay request changed; review admission before updating its pin')
        shutil.copyfile(request, evidence / 'request.json')
        write(evidence / 'source.json', source_files)
        shutil.copyfile(sdk / 'package.json', evidence / 'sdk-package.json')
        shutil.copyfile(sdk / 'engine.json', evidence / 'engine.json')
        (evidence / 'save.json').write_text('{}\n')
        command = [str(host), str(artifact), '--resource-root', str(installed),
                   '--sdk-resource-root', str(sdk), '--backend', 'sljit', '--hidden',
                   '--frames', str(frames), '--fixed-dt', '0.016666666666666666', '--size', '640x360',
                   '--ui-replay', str(evidence / 'request.json'), '--ui-replay-result', str(evidence / 'result.json'),
                   '--capture', str(evidence / 'capture.png'), '--save', str(evidence / 'save.json')]
        baseline = {}
        if kind == 'material':
            fixture = source / 'tests/acceptance/project'
            baseline = inventory(fixture)
            require(baseline == MATERIAL_FILES, 'Material fixture changed; review its three-file admission')
            workspace = evidence / 'workspace'
            shutil.copytree(fixture, workspace)
            write(evidence / 'workspace-before.json', baseline)
            command += ['--workspace-root', str(workspace)]
        receipt.update(command=command, request_sha256=expected, sdk_build_id=manifest['build_id'],
                       sdk_manifest_sha256=digest(sdk / 'package.json'), artifact_sha256=digest(artifact),
                       resource_manifest_sha256=digest(Path(str(artifact) + '.resources.json')), host_sha256=digest(host))
        write(evidence / 'receipt.json', receipt)
        with (evidence / 'host.log').open('w') as log:
            result = subprocess.run(command, cwd=evidence, stdout=log, stderr=subprocess.STDOUT)
        receipt['exit_code'] = result.returncode
        require(inventory(source / 'projects/studio') == source_files and
                digest(artifact) == receipt['artifact_sha256'] and
                digest(sdk / 'package.json') == receipt['sdk_manifest_sha256'],
                'Replay inputs changed during the run')
        if kind == 'material':
            after = inventory(evidence / 'workspace')
            write(evidence / 'workspace-after.json', after)
            receipt['created_or_changed_sources'] = {name: value for name, value in after.items() if baseline.get(name) != value}
        receipt['outputs'] = {name: digest(evidence / name) for name in ('result.json', 'capture.png', 'host.log') if (evidence / name).is_file()}
        write(evidence / 'receipt.json', receipt)
        return result.returncode
    except (OSError, ValueError, KeyError) as error:
        receipt['error'] = str(error)
        write(evidence / 'receipt.json', receipt)
        print(f'Studio replay refused: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    if len(sys.argv) != 2 or sys.argv[1] not in REQUESTS:
        raise SystemExit('Usage: studio_replay.py home|material')
    raise SystemExit(run(sys.argv[1]))
