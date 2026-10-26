#!/usr/bin/env python3
"""Configure only this job's ccache store and identify compatible snapshots."""
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent.parent


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def identity(root, env, run=output):
    # Both profiles share storage, not compilation keys: ccache hashes flags,
    # including sanitizers, independently for every object. No sloppy mode.
    values = {
        'profile': 'native-debug-address-undefined-release-v1',
        'runner': [env[key] for key in ('RUNNER_OS', 'RUNNER_ARCH')],
        'image': [env.get(key, '') for key in ('ImageOS', 'ImageVersion')],
        'compilers': [run(env[key], '--version') for key in ('CC', 'CXX')],
        'ccache': run('ccache', '--version'),
    }
    if env['RUNNER_OS'] == 'macOS':
        values['sdk'] = [run('xcrun', '--show-sdk-path'),
                         run('xcrun', '--show-sdk-version')]
    paths = [root / 'Makefile', root / 'CMakeLists.txt',
             root / 'third_party/archives.tsv', root / '.github/build-tools.txt',
             root / '.github/native-ccache.py']
    paths += sorted((root / 'cmake').rglob('*.cmake'))
    paths += sorted((root / 'tools/toolchain').rglob('*.chibi'))
    values['inputs'] = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in paths}
    digest = hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()
    return f"ccache-v1-{env['RUNNER_OS']}-{env['RUNNER_ARCH']}-{digest}"


def configure(env):
    temp = Path(env['RUNNER_TEMP'])
    # Configuration is never restored from the cache or written machine-wide.
    config = temp / 'chibi-ccache.conf'
    config.write_text('max_size = 500M\ncompiler_check = content\n')
    settings = {'CCACHE_DIR': str(temp / 'chibi-ccache'),
                'CCACHE_CONFIGPATH': str(config),
                'CCACHE_BASEDIR': env['GITHUB_WORKSPACE']}
    with open(env['GITHUB_ENV'], 'a') as stream:
        for key, value in settings.items():
            stream.write(f'{key}={value}\n')


def main():
    prefix = identity(ROOT, os.environ)
    configure(os.environ)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
        stream.write(f'prefix={prefix}\n')
    print(f'Compiler cache compatibility prefix: {prefix}')


if __name__ == '__main__':
    main()
