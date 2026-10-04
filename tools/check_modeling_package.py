#!/usr/bin/env python3
"""Validate a relocated native engine against a disposable saved modeling project.

Does not build, install registered runtimes, or alter the supplied project/package.
The capture proves packaged launch; visual inspection remains a separate check.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def hashes(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(root.rglob('*')) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scene', default='ModelPropStudy')
    parser.add_argument('--frames', type=int, default=120)
    args = parser.parse_args()
    package, source, output = args.package.resolve(), args.project_root.resolve(), args.output.resolve()
    if output == source or output.is_relative_to(source) or output == package or output.is_relative_to(package):
        raise ValueError('Evidence must be outside the supplied project and package')
    if not (source / 'project.kdl').is_file() or not (source / 'content/models').is_dir():
        raise ValueError('Pass the saved modeling project explicitly')
    if not (source / 'content/scenes' / f'{args.scene}.kdl').is_file():
        raise ValueError(f'Missing requested saved scene {args.scene}')
    if args.frames < 1:
        raise ValueError('Capture frames must be positive')
    baseline = hashes(source)
    package_baseline = hashes(package)
    output.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix='packaged-model-', dir=output))
    relocated = evidence / 'relocated engine with spaces'
    shutil.copytree(package, relocated)
    project = evidence / 'disposable modeling project'
    project.mkdir()
    shutil.copy2(source / 'project.kdl', project / 'project.kdl')
    shutil.copytree(source / 'content', project / 'content')
    assert hashes(source / 'content') == hashes(project / 'content'), 'Project dependencies were not copied faithfully'
    cwd = evidence / 'unrelated working directory'
    cwd.mkdir()
    blocked = evidence / 'blocked build commands'
    blocked.mkdir()
    marker = evidence / 'attempted rebuild'
    for name in ('make', 'cmake', 'ninja', 'cc', 'clang', 'gcc', 'git'):
        command = blocked / name
        command.write_text('#!/bin/sh\nprintf "%s\\n" "$0" >> "$CHIBI_TEST_REBUILD_MARKER"\nexit 99\n')
        command.chmod(0o755)
    environment = {key: value for key, value in os.environ.items() if not key.startswith('CHIBI_')}
    environment.update(PATH=str(blocked) + os.pathsep + environment.get('PATH', ''),
                       CHIBI_TEST_REBUILD_MARKER=str(marker), MallocNanoZone='0')
    results = []

    def run(command, input=None, timeout=900):
        process = subprocess.run([str(arg) for arg in command], cwd=cwd, env=environment,
                                 input=input, text=True, capture_output=True, timeout=timeout)
        results.append(dict(command=[str(arg) for arg in command], returncode=process.returncode,
                            stdout=process.stdout, stderr=process.stderr))
        assert process.returncode == 0, process.stderr or process.stdout
        assert not marker.exists(), 'Packaged execution attempted a source rebuild'
        return process

    def call(tool, arguments, failure=False):
        request = dict(jsonrpc='2.0', id=1, method='tools/call', params=dict(name=tool, arguments=arguments))
        process = run([relocated / 'bin/chibi-mcp', project], json.dumps(request) + '\n')
        response = json.loads(process.stdout.strip())
        failed = bool(response.get('error') or response.get('result', {}).get('isError'))
        assert failed == failure, response
        return response if failure else response['result']['structuredContent']['result']

    try:
        manifest = json.loads((relocated / 'share/chibi/engine.json').read_text())
        payload = hashes(relocated)
        del payload['share/chibi/engine.json']
        assert payload == manifest['files'], 'Relocated engine payload does not match its checksums'
        for binary in ('chibi', 'chibi_ctl'):
            info = json.loads(run([relocated / 'bin' / binary, '--engine-info']).stdout)
            assert info['distribution'] and info['build_id'] == manifest['build_id']
            assert Path(info['resource_root']).resolve() == relocated / 'share/chibi'
        run([relocated / 'bin/chibi-mcp', '--setup', project])
        status = call('getProjectStatus', {})
        assert Path(status['project_root']).resolve() == project
        run([relocated / 'bin/chibi_ctl', '--project-root', project, '--format', '--check'])
        families = [('model', 'models'), ('shader-library', 'shader-libraries'),
                    ('material', 'materials'), ('shape', 'shapes'), ('prototype', 'prototypes'), ('scene', 'scenes')]
        validated = []
        inspections = {}
        for kind, folder in families:
            for path in sorted((project / 'content' / folder).glob('*.kdl')):
                selector = dict(kind=kind, name=path.stem)
                assert call('validateAsset', selector)['valid'], selector
                validated.append(selector)
                if kind == 'model':
                    inspections[path.stem] = call('inspectGraph', selector).get('model_compilation')
        # A saved malformed edit must be diagnosed rather than using an earlier
        # successful generation. Only the disposable copy is edited.
        probe = next(iter(sorted((project / 'content/models').glob('*.kdl'))))
        original = probe.read_bytes()
        invalid = b'domain "model" {\n'
        try:
            probe.write_bytes(invalid)
            rejection = call('validateAsset', dict(kind='model', name=probe.stem), failure=True)
            assert probe.read_bytes() == invalid, 'Rejected source was silently rewritten'
        finally:
            probe.write_bytes(original)
        assert call('validateAsset', dict(kind='model', name=probe.stem))['valid']
        screenshot = evidence / f'{args.scene}-packaged.png'
        run([relocated / 'bin/chibi', '--headless', '--play', '--project-root', project,
             '--scene', f'scenes/{args.scene}.kdl', '--frames', args.frames, '--fixed-dt', 1 / 60,
             '--screenshot', screenshot])
        assert screenshot.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), 'Packaged launch did not produce a PNG'
        report = dict(passed=True, engine_build_id=manifest['build_id'], project=str(project),
                      scene=args.scene, frames=args.frames, screenshot=str(screenshot),
                      visual_inspection='pending', validated=validated, model_compilation=inspections,
                      invalid_saved_source_rejection=rejection, source_dependencies=hashes(source / 'content'))
        (evidence / 'package-validation.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    finally:
        (evidence / 'commands.json').write_text(json.dumps(results, indent=2) + '\n')
        assert hashes(source) == baseline, 'Supplied project changed during isolated validation'
        assert hashes(package) == package_baseline, 'Supplied engine package changed during isolated validation'


if __name__ == '__main__':
    main()
