#!/usr/bin/env python3
"""Publish original workflow artifacts without replacing releases, assets or branches."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile

TARGETS = [(target, variant)
           for target in ('linux-x64', 'macos-arm64')
           for variant in ('release', 'address-undefined', 'thread')]
TARGETS += [(target, 'release') for target in ('web', 'web-threads', 'web-player')]
PIN_FILE = 'third_party/archives.tsv'


def run(*args, raw=False):
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{args[0]} {args[1]} failed ({result.returncode}): {result.stderr.strip()}')
    return result.stdout if raw else result.stdout.strip()


def api(path, optional=False):
    result = subprocess.run(['gh', 'api', path], text=True, capture_output=True)
    try:
        value = json.loads(result.stdout)
    except ValueError:
        value = None
    if result.returncode:
        if optional and isinstance(value, dict) and str(value.get('status')) == '404':
            return None
        raise RuntimeError(f'GitHub API read failed: {path}: {result.stderr.strip()}')
    if value is None:
        raise RuntimeError(f'GitHub API returned invalid or null JSON: {path}')
    return value


def validate(release, sha, repository):
    if not re.fullmatch(r'third-party-[A-Za-z0-9][A-Za-z0-9.-]*', release):
        raise ValueError('release must be a third-party- tag containing letters, digits, dots or hyphens')
    run('git', 'check-ref-format', f'refs/tags/{release}')
    run('git', 'check-ref-format', f'refs/heads/pin-{release}')
    if not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise ValueError('expected a full source commit SHA')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('invalid repository')


def validate_archive(path):
    # Inspect headers and the root package description only; never extract or execute.
    roots = 0
    count = total = 0
    try:
        with tarfile.open(path, 'r:gz') as archive:
            for item in archive:
                count += 1
                total += item.size
                if item.size < 0 or count > 100000 or total > 8 * 1024**3:
                    raise ValueError(f'archive inventory exceeds bounds: {path.name}')
                if str(PurePosixPath(item.name)) == 'third-party.cmake':
                    roots += 1
                    if not item.isfile() or item.size <= 0:
                        raise ValueError(f'root third-party.cmake must be a nonempty regular file: {path.name}')
                    with archive.extractfile(item) as description:
                        if not description.read(1):
                            raise ValueError(f'empty package description: {path.name}')
    except (tarfile.TarError, EOFError, OSError) as error:
        raise ValueError(f'invalid gzip tar archive: {path.name}: {error}') from error
    if roots != 1:
        raise ValueError(f'expected exactly one root third-party.cmake: {path.name}')


def archives(directory):
    expected = {f'third-party-{target}-{variant}.tar.gz': (target, variant)
                for target, variant in TARGETS}
    paths = list(directory.iterdir())
    if {p.name for p in paths} != expected.keys():
        raise ValueError('expected exactly the nine third-party archives; missing or extra files')
    result = {}
    for path in sorted(paths):
        if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f'not a nonempty regular archive: {path.name}')
        validate_archive(path)
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        result[path.name] = {'path': path, 'sha256': digest, 'size': path.stat().st_size,
                             'target': expected[path.name][0], 'variant': expected[path.name][1]}
    return result


def tag_sha(release):
    rows = run('git', 'ls-remote', '--tags', 'origin', f'refs/tags/{release}',
               f'refs/tags/{release}^{{}}').splitlines()
    refs = dict(reversed(row.split('\t', 1)) for row in rows)
    return refs.get(f'refs/tags/{release}^{{}}', refs.get(f'refs/tags/{release}'))


def missing_assets(existing, expected):
    seen = set()
    for item in existing:
        name = item['name']
        if name in seen or name not in expected:
            raise ValueError('existing release has duplicate or unexpected assets; refusing to change it')
        seen.add(name)
        wanted = expected[name]
        if (item.get('state') != 'uploaded' or item.get('size') != wanted['size']
                or item.get('digest') != 'sha256:' + wanted['sha256']):
            raise ValueError(f'existing asset differs or has no verified digest: {name}; never overwrite')
    return sorted(set(expected) - seen)


def publish_release(release, sha, repository, expected):
    path = f'repos/{repository}/releases/tags/{release}'
    current = api(path, optional=True)
    actual = tag_sha(release)
    if actual is not None and actual != sha:
        raise ValueError('release tag does not resolve to the workflow source commit')
    if current is not None:
        if actual != sha or current.get('draft') or not current.get('prerelease'):
            raise ValueError('existing release is not the matching published third-party prerelease')
        missing_assets(current['assets'], expected)  # Validate every existing asset before any writes.
    else:
        run('gh', 'release', 'create', release, '--repo', repository, '--target', sha,
            '--title', release, '--prerelease', '--latest=false',
            '--notes', f'Third-party archives built from {sha}.')
    if tag_sha(release) != sha:
        raise ValueError('release tag changed; refusing to upload artifacts')
    current = api(path)
    missing = missing_assets(current['assets'], expected)
    for name in missing:
        # No --clobber: a concurrent upload fails rather than overwriting its bytes.
        run('gh', 'release', 'upload', release, str(expected[name]['path']), '--repo', repository)
    if missing_assets(api(path)['assets'], expected):
        raise ValueError('release is incomplete after upload; rerun this publish job, not the build jobs')
    if tag_sha(release) != sha:
        raise ValueError('release tag changed after upload; refusing to pin artifacts')


def pins(repository, release, expected):
    text = f'# repository {repository}\n# target\tvariant\trelease\tsha256\n'
    for item in expected.values():
        text += f"{item['target']}\t{item['variant']}\t{release}\t{item['sha256']}\n"
    return text


def verify_branch(ref, sha, content):
    parents = run('git', 'show', '-s', '--format=%P', ref).split()
    if parents != [sha]:
        raise ValueError('existing pin branch is not a single commit on this workflow source')
    if run('git', 'diff', '--name-only', sha, ref) != PIN_FILE:
        raise ValueError('existing pin branch contains unrelated changes')
    if run('git', 'show', f'{ref}:{PIN_FILE}', raw=True) != content:
        raise ValueError('existing pin branch has different archive pins; never force-push')


def prepare_pin(release, sha, repository, content):
    branch = f'pin-{release}'
    run('git', 'fetch', '--no-tags', 'origin', 'refs/heads/main')
    main = run('git', 'rev-parse', 'FETCH_HEAD')
    if run('git', 'show', f'{main}:{PIN_FILE}', raw=True) == content:
        return None
    run('git', 'merge-base', '--is-ancestor', sha, main)
    owner = repository.split('/')[0]
    prs = api(f'repos/{repository}/pulls?head={owner}:{branch}&state=all')
    if len(prs) > 1 or any(p['state'] != 'open' or p['base']['ref'] != 'main'
                           or (p['head'].get('repo') or {}).get('full_name') != repository for p in prs):
        raise ValueError('pin PR was closed, merged, retargeted or is ambiguous; no automatic reopening')
    remote = run('git', 'ls-remote', '--heads', 'origin', f'refs/heads/{branch}')
    if remote:
        run('git', 'fetch', '--no-tags', 'origin', f'refs/heads/{branch}')
        head = run('git', 'rev-parse', 'FETCH_HEAD')
        verify_branch(head, sha, content)
    else:
        if prs:
            raise ValueError('existing PR has no matching remote pin branch')
        if run('git', 'status', '--porcelain', '--untracked-files=no') or run('git', 'rev-parse', 'HEAD') != sha:
            raise ValueError('pin creation requires a clean checkout of the workflow source')
        head = None
    if prs and prs[0]['head']['sha'] != head:
        raise ValueError('pin PR head changed during verification')
    return branch, head, prs


def pin_release(release, sha, repository, content):
    # Recheck after uploads too: remote branch/PR state may have changed meanwhile.
    plan = prepare_pin(release, sha, repository, content)
    if plan is None:
        return 'Pins already match main; no branch or PR changes.'
    branch, head, prs = plan
    if head is None:
        Path(PIN_FILE).write_text(content)
        run('git', 'switch', '-c', branch)
        run('git', '-c', 'user.name=github-actions', '-c',
            'user.email=github-actions@users.noreply.github.com', 'commit', '-m',
            f'Pin the {release} third-party archives', '--', PIN_FILE)
        head = run('git', 'rev-parse', 'HEAD')
        verify_branch(head, sha, content)
        run('git', 'push', 'origin', f'HEAD:refs/heads/{branch}')
    if prs:
        if prs[0]['head']['sha'] != head:
            raise ValueError('pin PR head changed during verification')
        return prs[0]['html_url']
    return run('gh', 'pr', 'create', '--repo', repository, '--base', 'main', '--head', branch,
               '--title', f'Pin the {release} third-party archives', '--body',
               f'Archives built from {sha} and published as release {release}.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validate-only', action='store_true')
    parser.add_argument('--archives', type=Path, default=Path('archives'))
    args = parser.parse_args()
    release, sha, repository = (os.environ[key] for key in ('RELEASE', 'GITHUB_SHA', 'GITHUB_REPOSITORY'))
    validate(release, sha, repository)
    if args.validate_only:
        return
    expected = archives(args.archives)
    content = pins(repository, release, expected)
    prepare_pin(release, sha, repository, content)  # Read-only gate before any publication.
    publish_release(release, sha, repository, expected)
    print(pin_release(release, sha, repository, content))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
