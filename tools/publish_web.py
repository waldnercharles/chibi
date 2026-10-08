#!/usr/bin/env python3
"""Build origin/main in isolation and publish its static bundle to GitHub Pages."""
import argparse
import fcntl
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import time
from pathlib import Path
from urllib.error import URLError
from urllib.request import urlopen

import dependency_package


def run(*args, cwd=None, capture=False, env=None):
    result = subprocess.run(args, cwd=cwd, env=env, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def materialize_projects(source, root, commits=None, archive=True):
    selected = {}
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
            name = alias.relative_to(source).as_posix()
            commit = commits[name] if commits is not None else run(
                'git', 'rev-parse', 'HEAD', cwd=project, capture=True)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(f'External project must have a committed Git HEAD: {project}') from error
        if repository != project:
            raise RuntimeError(f'External project must be a Git repository root: {project}')
        selected[name] = commit
        if not archive:
            continue
        # Archive committed assets instead of copying the working tree. This
        # deliberately leaves dirty edits and local configuration on disk.
        with tempfile.TemporaryFile() as stream:
            subprocess.run(['git', 'archive', '--format=tar', commit], cwd=project,
                           stdout=stream, check=True)
            stream.seek(0)
            alias.unlink()
            alias.mkdir()
            with tarfile.open(fileobj=stream) as files:
                files.extractall(alias, filter='data')
    return selected


def file_hashes(directory, names):
    hashes = {}
    for name in names:
        with (directory / name).open('rb') as stream:
            hashes[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
    return hashes


def artifact_at(directory, identity):
    marker = directory / 'BUILD_ARTIFACT.json'
    if not marker.is_file():
        return None
    try:
        artifact = json.loads(marker.read_text())
        hashes = artifact['hashes']
        required = {'index.html', 'chibi.html', 'chibi.js', 'chibi.wasm', 'chibi.data',
                    'BUILD_SOURCES.json'}
        if artifact['identity'] != identity or not required.issubset(hashes):
            return None
        for name in hashes:
            path = Path(name)
            if path.is_absolute() or '..' in path.parts:
                return None
        return artifact if file_hashes(directory, hashes) == hashes else None
    except (OSError, ValueError, KeyError, TypeError):
        return None


def build_environment(root):
    # Build inputs are explicit; CI run IDs, session IDs and application state
    # neither reach CMake nor invalidate an otherwise identical publication.
    names = {'PATH', 'HOME', 'USER', 'TMPDIR', 'TMP', 'TEMP', 'SHELL',
             'LANG', 'LC_ALL', 'XDG_CACHE_HOME', 'CC', 'CXX', 'AR', 'RANLIB',
             'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'SDKROOT',
             'MACOSX_DEPLOYMENT_TARGET', 'LIBCLANG_ROOT', 'CMAKE_GENERATOR',
             'CMAKE_PREFIX_PATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH',
             'CPATH', 'LIBRARY_PATH', 'PKG_CONFIG_PATH', 'PKG_CONFIG_SYSROOT_DIR'}
    names.update(dependency_package.ENVIRONMENT)
    env = {key: value for key, value in os.environ.items()
           if key in names or key.startswith(('EM', 'CCACHE_', 'CHIBI_'))}
    # A parent's make options do not belong to this checkout.
    for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'CCACHE_BASEDIR'):
        env.pop(key, None)
    env.setdefault('EM_CACHE', str(root / '.cache' / 'emscripten'))
    env.setdefault('CCACHE_DIR', str(root / '.cache' / 'ccache'))
    return env


def input_identity(path, content=False):
    path = path.resolve()
    stat = path.stat()
    value = [str(path), stat.st_dev, stat.st_ino, stat.st_size,
             stat.st_mtime_ns, stat.st_ctime_ns]
    if content and path.is_file() and stat.st_size <= 1024 * 1024:
        value.append(file_hashes(path.parent, [path.name])[path.name])
    return value


def web_build_inputs(env):
    # Match configure-time discovery, including tools selected by em-config.
    toolchain = dependency_package.web_toolchain()
    emscripten = toolchain.parents[3]
    llvm = Path(run(str(emscripten / 'em-config'), 'LLVM_ROOT', capture=True, env=env))
    binaryen = Path(run(str(emscripten / 'em-config'), 'BINARYEN_ROOT', capture=True, env=env))
    files = {toolchain, emscripten / 'emcc', emscripten / 'em++', emscripten / 'em-config'}
    files.update(emscripten.glob('*.py'))
    roots = [emscripten / name for name in ('cmake', 'tools', 'system', 'src')]
    roots.append(llvm.parent / 'lib/clang')
    for name in ('clang', 'clang++', 'wasm-ld', 'llvm-ar', 'llvm-nm', 'llvm-objcopy', 'llvm-size'):
        files.add(llvm / name)
    for name in ('wasm-opt', 'wasm-metadce', 'wasm-emscripten-finalize'):
        files.add(binaryen / 'bin' / name)
    files.update((llvm.parent / 'lib').glob('libLLVM*'))
    # Reflect selects libclang from these hints. Track only its library and
    # public/resource headers, including newly installed priority candidates.
    hints = [Path('/opt/homebrew/opt/llvm'), Path('/usr/local/opt/llvm')]
    hints.extend(Path('/usr/lib').glob('llvm-*'))
    hints.extend(Path(name) for name in env.get('CMAKE_PREFIX_PATH', '').split(';') if name)
    if env.get('LIBCLANG_ROOT'):
        hints.append(Path(env['LIBCLANG_ROOT']))
    hints.extend((Path('/usr'), Path('/usr/local')))
    for hint in hints:
        roots.extend((hint / 'include/clang-c', hint / 'lib/clang'))
        files.update((hint / 'lib').glob('*clang*'))
    files.update(Path('/usr/lib/x86_64-linux-gnu').glob('*clang*'))
    for root in roots:
        if root.is_dir():
            files.add(root)
            files.update(path for path in root.rglob('*')
                         if path.is_file() and '__pycache__' not in path.parts)
    inventory = [[str(path), input_identity(path)] for path in sorted(files) if path.exists()]
    return {'toolchain': input_identity(toolchain, content=True),
            'llvm': str(llvm), 'binaryen': str(binaryen),
            'files': hashlib.sha256(json.dumps(inventory).encode()).hexdigest()}


def build_identity(commit, projects, collab_url, threads, env):
    # Hash the selected build environment conservatively. Store only
    # the digest, so credentials are never written to deployment metadata.
    environment = json.dumps(env, sort_keys=True).encode()
    inputs = {'version': 1, 'engine': commit, 'projects': projects,
              'collab_url': collab_url, 'threads': threads,
              'platform': platform.platform(),
              'environment': hashlib.sha256(environment).hexdigest(),
              'tools': {}}
    for tool in ('emcc', 'cmake', 'cc', 'c++'):
        command = shlex.split(env.get({'cc': 'CC', 'c++': 'CXX'}.get(tool, ''), tool))
        executable = shutil.which(command[0], path=env.get('PATH'))
        if not executable:
            raise RuntimeError(f'Missing build tool: {command[0]}')
        inputs['tools'][tool] = {'command': command, 'path': executable,
                                'file': input_identity(Path(executable), content=True),
                                'version': run(*command, '--version', capture=True, env=env)}
    if platform.system() == 'Darwin' and shutil.which('xcrun', path=env.get('PATH')):
        inputs['sdk'] = {flag: run('xcrun', flag, capture=True, env=env)
                         for flag in ('--show-sdk-path', '--show-sdk-version')}
        inputs['developer'] = run('xcode-select', '-p', capture=True, env=env)
    configuration = [Path(env.get('EM_CONFIG', str(Path.home() / '.emscripten')))]
    if 'sdk' in inputs:
        configuration.extend(Path(inputs['sdk']['--show-sdk-path']) / name
                             for name in ('SDKSettings.json', 'SDKSettings.plist'))
    if env.get('SDKROOT'):
        configuration.extend(Path(env['SDKROOT']) / name
                             for name in ('SDKSettings.json', 'SDKSettings.plist'))
    inputs['configuration'] = {str(path): file_hashes(path.parent, [path.name])[path.name]
                               if path.is_file() else None for path in configuration}
    inputs['web_inputs'] = web_build_inputs(env)
    identity = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    return identity, inputs


def require_current_main(source, commit):
    latest = run('git', 'ls-remote', 'origin', 'refs/heads/main',
                 cwd=source, capture=True).split()[0]
    if latest != commit:
        raise RuntimeError('origin/main advanced during the build; rerun make publish-web')


def publish(root, jobs, collab_url, threads=False, update_runtime=False, cache_dir=None):
    remote = run('git', 'remote', 'get-url', 'origin', cwd=root, capture=True)
    repo = run('gh', 'repo', 'view', remote, '--json', 'nameWithOwner',
               '--jq', '.nameWithOwner', capture=True)
    pages = json.loads(run('gh', 'api', f'repos/{repo}/pages', capture=True))
    if pages['build_type'] != 'legacy' or pages['source'] != {'branch': 'gh-pages', 'path': '/'}:
        raise RuntimeError('Pages must publish from the root of the gh-pages branch')

    with tempfile.TemporaryDirectory(prefix='chibi-publish-') as directory:
        source = Path(directory) / 'source'
        site = Path(directory) / 'site'
        # Share existing objects, but keep refs, checkout and build files isolated.
        run('git', 'clone', '--shared', '--no-checkout', str(root), str(source))
        run('git', 'remote', 'set-url', 'origin', remote, cwd=source)
        run('git', 'fetch', '--no-tags', 'origin',
            '+refs/heads/main:refs/remotes/origin/main', cwd=source)
        commit = run('git', 'rev-parse', 'origin/main', cwd=source, capture=True)
        run('git', 'checkout', '--detach', commit, cwd=source)
        project_commits = materialize_projects(source, root, archive=False)
        env = build_environment(root)
        identity, inputs = build_identity(commit, project_commits, collab_url, threads, env)
        run('git', 'clone', '--depth', '1', '--single-branch', '--branch',
            'gh-pages', remote, str(site))
        deployed = artifact_at(site, identity)
        cache = (cache_dir or root / '.cache' / 'publish-web') / identity
        cache.mkdir(parents=True, exist_ok=True)
        bundle = cache / 'bundle'
        # A concurrent invocation may reuse this completed bundle, but must not
        # configure or package into the same isolated build at the same time.
        with (cache / 'lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            cached = artifact_at(bundle, identity)
            if not deployed or update_runtime:
                if not cached or (update_runtime and not (cache / 'source').is_dir()):
                    cached_source = cache / 'source'
                    if cached_source.exists():
                        shutil.rmtree(cached_source)
                    shutil.move(source, cached_source)
                    source = cached_source
                    materialize_projects(source, root, commits=project_commits)
                    env['CCACHE_BASEDIR'] = str(source)
                    print(f'Building origin/main at {commit}', flush=True)
                    run('make', 'web-serial', f'JOBS={jobs}', f'CHIBI_COLLAB_URL={collab_url}',
                        'BUILD_DIR=build', 'WEB_BUILD_DIR=build-web', 'WEB_CMAKE_FLAGS=',
                        cwd=source, env=env)
                    if threads:
                        run('make', 'web-threads', f'JOBS={jobs}',
                            f'CHIBI_COLLAB_URL={collab_url}', 'BUILD_DIR=build',
                            'WEB_THREADS_BUILD_DIR=build-web-threads', 'WEB_CMAKE_FLAGS=',
                            'WEB_WORKERS=4', cwd=source, env=env)
                    for build in ('build-web', 'build-web-threads') if threads else ('build-web',):
                        for name in ('chibi.html', 'chibi.js', 'chibi.wasm', 'chibi.data'):
                            output = source / build / name
                            if not output.is_file() or not output.stat().st_size:
                                raise RuntimeError(f'Missing or empty build output: {build}/{name}')
                    if bundle.exists():
                        shutil.rmtree(bundle)
                    arguments = ['python3', str(source / 'tools/package_web.py'),
                                 '--serial', str(source / 'build-web'), '--output', str(bundle)]
                    if threads:
                        arguments.extend(('--threads', str(source / 'build-web-threads')))
                    run(*arguments)
                    (bundle / 'BUILD_SOURCES.json').write_text(json.dumps(
                        {'engine': commit, 'projects': project_commits, 'build': inputs},
                        indent=2, sort_keys=True) + '\n')
                    assets = sorted(path.relative_to(bundle).as_posix()
                                    for path in bundle.rglob('*') if path.is_file())
                    cached = {'identity': identity, 'hashes': file_hashes(bundle, assets)}
                    (bundle / 'BUILD_ARTIFACT.json').write_text(json.dumps(cached, sort_keys=True) + '\n')
                else:
                    print(f'Reusing verified publication artifact {identity}', flush=True)
                    source = cache / 'source'
                    env['CCACHE_BASEDIR'] = str(source)
            runtime_config = root / '.chibi/game-runtime.json'
            runtime_package = source / 'dist/game-runtime'
            if update_runtime:
                if not runtime_config.is_file():
                    raise RuntimeError('--update-game-runtime requires .chibi/game-runtime.json')
                run('make', 'package-player', f'JOBS={jobs}', 'PLAYER_OUTPUT=dist/game-runtime',
                    'UPDATE_GAME_RUNTIME=0', cwd=source, env=env)
        if deployed:
            require_current_main(source, commit)
            print('The same verified artifact is already on gh-pages', flush=True)
            wait_for_deployment(pages['html_url'], commit, deployed['hashes'])
            if update_runtime:
                run('python3', str(root / 'tools/install_game_runtime.py'),
                    '--package', str(runtime_package), '--config', str(runtime_config))
            return
        assets = [*cached['hashes'], 'BUILD_ARTIFACT.json']
        hashes = cached['hashes']
        with (cache / 'lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if artifact_at(bundle, identity) != cached:
                raise RuntimeError('Publication artifact changed before staging the deployment')
            old_marker = site / 'BUILD_ARTIFACT.json'
            if old_marker.is_file():
                old_assets = json.loads(old_marker.read_text())['hashes']
            else:
                # Older paired publications had no artifact manifest.
                old_assets = [f'threads/{name}' for name in (
                    'index.html', 'chibi.html', 'chibi.js', 'chibi.wasm', 'chibi.data',
                    'chibi.worker.js', 'chibi-isolation.js', 'chibi-default.js')]
            removed = []
            for name in old_assets:
                path = Path(name)
                if path.is_absolute() or '..' in path.parts:
                    raise RuntimeError('Invalid previous publication asset path')
                if name not in assets and (site / name).is_file():
                    (site / name).unlink()
                    removed.append(name)
            for name in assets:
                (site / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(bundle / name, site / name)
            if file_hashes(site, hashes) != hashes:
                raise RuntimeError('Publication artifact changed while staging the deployment')
        for marker in ('BUILD_COMMIT', 'DEPLOYMENT_COMMIT'):
            (site / marker).write_text(commit + '\n')
        run('git', 'add', '--', *assets, *removed, 'BUILD_COMMIT',
            'DEPLOYMENT_COMMIT', cwd=site)
        changed = subprocess.run(['git', 'diff', '--cached', '--quiet'], cwd=site).returncode
        if changed not in (0, 1):
            raise RuntimeError('Could not inspect staged deployment')
        require_current_main(source, commit)
        if changed:
            run('git', 'commit', '-m', f'Publish web build {commit}', cwd=site)
            # Normal push rejects competing deployments instead of overwriting them.
            run('git', 'push', 'origin', 'HEAD:refs/heads/gh-pages', cwd=site)
            run('gh', 'api', '--method', 'POST', f'repos/{repo}/pages/builds', capture=True)
        else:
            print('The same bundle is already on gh-pages', flush=True)

        wait_for_deployment(pages['html_url'], commit, hashes)
        if update_runtime:
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
                        if name == '.nojekyll':
                            continue
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
    parser.add_argument('--threads', action='store_true', help='Include the threaded app bundle')
    parser.add_argument('--update-game-runtime', action='store_true', help='Also build and deliver registered native game runtimes')
    parser.add_argument('--cache-dir', type=Path, help='Publication artifact cache (default: .cache/publish-web)')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    root = Path(run('git', 'rev-parse', '--show-toplevel', capture=True))
    try:
        publish(root, args.jobs, args.collab_url, args.threads, args.update_game_runtime,
                args.cache_dir.resolve() if args.cache_dir else None)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'publish-web: {error}\n')


if __name__ == '__main__':
    main()
