#!/usr/bin/env python3
"""Build origin/main in isolation and publish its static bundle to GitHub Pages."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
import time
from pathlib import Path
from urllib.error import URLError
from urllib.request import urlopen


def run(*args, cwd=None, capture=False, env=None):
    result = subprocess.run(args, cwd=cwd, env=env, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def materialize_projects(source, root):
    commits = {}
    for alias in sorted((source / 'projects').glob('*')):
        if not alias.is_symlink():
            continue
        target = Path(os.readlink(alias))
        if target.is_absolute():
            raise RuntimeError(f'Project link must be relative: {alias.relative_to(source)}')
        project = (root / alias.relative_to(source).parent / target).resolve()
        if not project.is_dir():
            raise RuntimeError(f'Missing external project: {project}')
        try:
            repository = Path(run('git', 'rev-parse', '--show-toplevel', cwd=project, capture=True)).resolve()
            commit = run('git', 'rev-parse', 'HEAD', cwd=project, capture=True)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(f'External project must have a committed Git HEAD: {project}') from error
        if repository != project:
            raise RuntimeError(f'External project must be a Git repository root: {project}')
        # Archive committed assets instead of copying the working tree. This
        # deliberately leaves dirty edits and local configuration on disk.
        with tempfile.TemporaryFile() as archive:
            subprocess.run(['git', 'archive', '--format=tar', commit], cwd=project,
                           stdout=archive, check=True)
            archive.seek(0)
            alias.unlink()
            alias.mkdir()
            with tarfile.open(fileobj=archive) as files:
                files.extractall(alias, filter='data')
        commits[alias.relative_to(source).as_posix()] = commit
    return commits


def publish(root, jobs, collab_url):
    remote = run('git', 'remote', 'get-url', 'origin', cwd=root, capture=True)
    repo = run('gh', 'repo', 'view', remote, '--json', 'nameWithOwner',
               '--jq', '.nameWithOwner', capture=True)
    pages = json.loads(run('gh', 'api', f'repos/{repo}/pages', capture=True))
    if pages['build_type'] != 'legacy' or pages['source'] != {'branch': 'gh-pages', 'path': '/'}:
        raise RuntimeError('Pages must publish from the root of the gh-pages branch')

    with tempfile.TemporaryDirectory(prefix='chibi-publish-') as directory:
        source = Path(directory) / 'source'
        site = Path(directory) / 'site'
        bundle = Path(directory) / 'bundle'
        # Share existing objects, but keep refs, checkout and build files isolated.
        run('git', 'clone', '--shared', '--no-checkout', str(root), str(source))
        run('git', 'remote', 'set-url', 'origin', remote, cwd=source)
        run('git', 'fetch', '--no-tags', 'origin',
            '+refs/heads/main:refs/remotes/origin/main', cwd=source)
        commit = run('git', 'rev-parse', 'origin/main', cwd=source, capture=True)
        run('git', 'checkout', '--detach', commit, cwd=source)
        project_commits = materialize_projects(source, root)
        print(f'Building origin/main at {commit}', flush=True)
        env = os.environ.copy()
        # Parent make variables must not leak into the isolated build.
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES'):
            env.pop(key, None)
        env.setdefault('EM_CACHE', str(root / '.cache' / 'emscripten'))
        env['CCACHE_DISABLE'] = '1'
        run('make', 'web', f'JOBS={jobs}', f'CHIBI_COLLAB_URL={collab_url}',
            'BUILD_DIR=build', 'WEB_BUILD_DIR=build-web',
            'WEB_THREADS_BUILD_DIR=build-web-threads', 'WEB_CMAKE_FLAGS=',
            cwd=source, env=env)
        runtime_config = root / '.chibi/game-runtime.json'
        runtime_package = source / 'dist/game-runtime'
        if runtime_config.is_file():
            run('make', 'package-native', f'JOBS={jobs}', 'PACKAGE_OUTPUT=dist/game-runtime',
                'UPDATE_GAME_RUNTIME=0', cwd=source, env=env)
        build = source / 'build-web'
        for name in ('chibi.html', 'chibi.js', 'chibi.wasm', 'chibi.data'):
            if not (build / name).is_file() or (build / name).stat().st_size == 0:
                raise RuntimeError(f'Missing or empty build output: {name}')
        package = source / 'tools/package_web.py'
        if not package.is_file():
            package = root / 'tools/package_web.py'
        arguments = ['python3', str(package), '--serial', str(build),
                     '--output', str(bundle)]
        if (source / 'build-web-threads').is_dir():
            arguments.extend(('--threads', str(source / 'build-web-threads')))
        run(*arguments)
        (bundle / 'BUILD_SOURCES.json').write_text(json.dumps(
            {'engine': commit, 'projects': project_commits}, indent=2, sort_keys=True) + '\n')
        assets = sorted(path.relative_to(bundle).as_posix()
                        for path in bundle.rglob('*') if path.is_file())

        hashes = {}
        for name in assets:
            if name == '.nojekyll':
                continue
            with (bundle / name).open('rb') as stream:
                hashes[name] = hashlib.file_digest(stream, 'sha256').hexdigest()

        run('git', 'clone', '--depth', '1', '--single-branch', '--branch',
            'gh-pages', remote, str(site))
        for name in assets:
            (site / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(bundle / name, site / name)
        for marker in ('BUILD_COMMIT', 'DEPLOYMENT_COMMIT'):
            (site / marker).write_text(commit + '\n')
        run('git', 'add', '--', *assets, 'BUILD_COMMIT',
            'DEPLOYMENT_COMMIT', cwd=site)
        changed = subprocess.run(['git', 'diff', '--cached', '--quiet'], cwd=site).returncode
        if changed not in (0, 1):
            raise RuntimeError('Could not inspect staged deployment')
        latest = run('git', 'ls-remote', 'origin', 'refs/heads/main',
                     cwd=source, capture=True).split()[0]
        if latest != commit:
            raise RuntimeError('origin/main advanced during the build; rerun make publish-web')
        if changed:
            run('git', 'commit', '-m', f'Publish web build {commit}', cwd=site)
            # Normal push rejects competing deployments instead of overwriting them.
            run('git', 'push', 'origin', 'HEAD:refs/heads/gh-pages', cwd=site)
        else:
            print('The same bundle is already on gh-pages', flush=True)
        run('gh', 'api', '--method', 'POST', f'repos/{repo}/pages/builds', capture=True)

        wait_for_deployment(pages['html_url'], commit, hashes)
        if runtime_config.is_file():
            run('python3', str(root / 'tools/install_game_runtime.py'),
                '--package', str(runtime_package), '--config', str(runtime_config))


def wait_for_deployment(page_url, commit, hashes):
    url = page_url.rstrip('/') + '/'
    print(f'Waiting for {url} to serve {commit}', flush=True)
    for attempt in range(60):
        try:
            with urlopen(f'{url}DEPLOYMENT_COMMIT?commit={commit}&attempt={attempt}', timeout=15) as response:
                if response.read().decode().strip() == commit:
                    matches = True
                    for name, expected in hashes.items():
                        with urlopen(f'{url}{name}?commit={commit}&attempt={attempt}', timeout=30) as asset:
                            digest = hashlib.sha256()
                            for chunk in iter(lambda: asset.read(1024 * 1024), b''):
                                digest.update(chunk)
                        if digest.hexdigest() != expected:
                            matches = False
                            break
                    if matches:
                        print(f'Published {commit}: {url}', flush=True)
                        return
        except (URLError, TimeoutError):
            pass
        time.sleep(5)
    raise RuntimeError(f'gh-pages was published, but the live marker or assets did not match: {url}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--collab-url', default=os.environ.get('CHIBI_COLLAB_URL', ''))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    root = Path(run('git', 'rev-parse', '--show-toplevel', capture=True))
    try:
        publish(root, args.jobs, args.collab_url)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'publish-web: {error}\n')


if __name__ == '__main__':
    main()
