#!/usr/bin/env python3
"""Configure only this job's ccache store and identify compatible snapshots."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parent.parent


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def compiler_digest(command):
    # Resolve symlinks through read_bytes; version strings alone can alias
    # different compiler builds. Missing/unreadable tools fail before restore.
    path = shutil.which(command)
    if path is None:
        raise FileNotFoundError(command)
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def digest(values):
    return hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()


def identity(root, env, run=output, fingerprint=compiler_digest):
    # Both profiles share storage, not compilation keys: ccache hashes flags,
    # including sanitizers, independently for every object. No sloppy mode.
    values = {
        'runner': [env[key] for key in ('RUNNER_OS', 'RUNNER_ARCH')],
        'image': [env.get(key, '') for key in ('ImageOS', 'ImageVersion')],
        'compilers': [[run(env[key], '--version'), fingerprint(env[key])]
                      for key in ('CC', 'CXX')],
        'ccache': run('ccache', '--version'),
    }
    # Driver bytes alone don't identify dynamically linked LLVM companions.
    # Record installed package revisions too; a failed query stops restoration.
    if env['RUNNER_OS'] == 'Linux':
        values['packages'] = run('dpkg-query', '-W', '-f=${binary:Package}=${Version}\n',
                                 'clang*', 'libclang*', 'libllvm*', 'llvm*')
    if env['RUNNER_OS'] == 'macOS':
        dependencies = run('brew', 'deps', '--installed', 'llvm').splitlines()
        values['packages'] = run('brew', 'list', '--versions', 'llvm', *sorted(dependencies))
        values['sdk'] = [run('xcrun', '--show-sdk-path'),
                         run('xcrun', '--show-sdk-version'),
                         run('xcrun', '--show-sdk-build-version')]
    compatible = f"ccache-v2-{env['RUNNER_OS']}-{env['RUNNER_ARCH']}-{digest(values)}"
    values['profile'] = 'native-debug-address-undefined-release-v1'
    paths = [root / 'Makefile', root / 'CMakeLists.txt',
             root / 'third_party/archives.tsv', root / '.github/build-tools.txt',
             root / '.github/native-ccache.py']
    paths += sorted((root / 'cmake').rglob('*.cmake'))
    paths += sorted((root / 'tools/toolchain').rglob('*.chibi'))
    values['inputs'] = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in paths}
    return f'{compatible}-{digest(values)}', compatible


def configure(env):
    temp = Path(env['RUNNER_TEMP'])
    # Configuration is never restored from the cache or written machine-wide.
    config = temp / 'chibi-ccache.conf'
    config.write_text('max_size = 500M\ncompiler_check = content\nhash_dir = true\nsloppiness =\n')
    settings = {'CCACHE_DIR': str(temp / 'chibi-ccache'),
                'CCACHE_CONFIGPATH': str(config),
                'CCACHE_BASEDIR': env['GITHUB_WORKSPACE']}
    with open(env['GITHUB_ENV'], 'a') as stream:
        for key, value in settings.items():
            stream.write(f'{key}={value}\n')


def main():
    prefix, compatible = identity(ROOT, os.environ)
    configure(os.environ)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
        stream.write(f'prefix={prefix}\ncompatibility-prefix={compatible}\n')
    print(f'Compiler cache primary prefix: {prefix}')
    print(f'Compiler cache toolchain fallback: {compatible}')


if __name__ == '__main__':
    main()
