#!/usr/bin/env python3
"""Reclaim disk from idle worktrees: clean idle build outputs and remove finished worktrees."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from workspace_retirement import process_idle

DAY = 86400
BUILD_IDLE_DAYS = 2
WORKTREE_IDLE_DAYS = 3


def run(*command, cwd=None):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, timeout=120)


def worktrees(repo):
    result = run('git', '-C', str(repo), 'worktree', 'list', '--porcelain')
    if result.returncode:
        raise ValueError(result.stderr.strip())
    entries = []
    for block in result.stdout.split('\n\n'):
        fields = dict(line.partition(' ')[::2] for line in block.splitlines())
        if 'worktree' in fields:
            entries.append({'path': Path(fields['worktree']), 'prunable': 'prunable' in fields,
                            'locked': 'locked' in fields})
    return entries


def age_days(path):
    try:
        return (time.time() - path.stat().st_mtime) / DAY
    except OSError:
        return 0


def ninja_outputs(build):
    result = run('ninja', '-C', str(build), '-t', 'targets', 'all')
    if result.returncode:
        raise ValueError(result.stderr.strip())
    outputs = []
    for line in result.stdout.splitlines():
        name, separator, rule = line.rpartition(': ')
        path = build / name
        if separator and rule != 'phony' and path.is_file() and not path.is_symlink():
            outputs.append(path)
    return outputs


def idle_builds(worktree, idle_days):
    for build in sorted(worktree.glob('build*')):
        log = build / '.ninja_log'
        if (build.is_dir() and not build.is_symlink() and (build / 'build.ninja').is_file()
                and log.is_file() and age_days(log) >= idle_days):
            yield build


def clean_build(build, apply):
    outputs = ninja_outputs(build)
    size = sum(path.stat().st_size for path in outputs)
    if apply and outputs:
        if not process_idle(build):
            return 0
        # Only files that Ninja itself produced are removed; the next build recreates them.
        result = run('ninja', '-C', str(build), '-t', 'clean')
        if result.returncode:
            raise ValueError(result.stderr.strip())
    return size


def removable(worktree, idle_days):
    """Why a worktree cannot be removed, or None when it holds nothing worth keeping."""
    path = worktree['path']
    if worktree['locked']:
        return 'locked'
    git_dir = Path(run('git', 'rev-parse', '--absolute-git-dir', cwd=path).stdout.strip())
    # The HEAD reflog records every commit and checkout; reading status must not count as use.
    if age_days(git_dir / 'logs/HEAD') < idle_days:
        return 'recently used'
    if any(age_days(build / '.ninja_log') < idle_days for build in path.glob('build*') if (build / '.ninja_log').is_file()):
        return 'recent build'
    if run('git', '--no-optional-locks', 'status', '--porcelain', cwd=path).stdout.strip():
        return 'uncommitted changes'
    if not run('git', 'branch', '-r', '--contains', 'HEAD', cwd=path).stdout.strip():
        return 'commits not on any remote branch'
    if not process_idle(path):
        return 'in use'
    return None


def reap(repo, apply=False, build_idle_days=BUILD_IDLE_DAYS, worktree_idle_days=WORKTREE_IDLE_DAYS):
    report = {'builds': [], 'worktrees': [], 'kept': []}
    entries = worktrees(repo)
    primary = entries[0]['path']
    for entry in entries:
        path = entry['path']
        if not path.is_dir():
            continue
        if path != primary:
            reason = removable(entry, worktree_idle_days)
            if reason is None:
                size = int(run('du', '-sk', str(path)).stdout.split()[0]) * 1024
                if apply:
                    result = run('git', '-C', str(primary), 'worktree', 'remove', str(path))
                    if result.returncode:
                        report['kept'].append({'path': str(path), 'reason': result.stderr.strip()})
                        continue
                report['worktrees'].append({'path': str(path), 'bytes': size, 'applied': apply})
                continue
            report['kept'].append({'path': str(path), 'reason': reason})
        for build in idle_builds(path, build_idle_days):
            try:
                size = clean_build(build, apply)
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                report['kept'].append({'path': str(build), 'reason': str(error)})
                continue
            if size:
                report['builds'].append({'path': str(build), 'bytes': size, 'applied': apply})
    return report


def repositories():
    import workspaces
    with workspaces.registry_lock() as registry:
        return sorted({Path(repo['root']) for repo in registry['repos'].values() if Path(repo['root']).is_dir()})


def show(repo, report, apply):
    total = sum(item['bytes'] for key in ('builds', 'worktrees') for item in report[key])
    print(json.dumps({'repo': str(repo), 'reclaimed_bytes' if apply else 'reclaimable_bytes': total, **report}, indent=1))


def reap_registered(apply):
    for repo in repositories():
        report = reap(repo, apply)
        if report['builds'] or report['worktrees']:
            show(repo, report, apply)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, action='append', help='repository; default: registered repositories')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--build-idle-days', type=float, default=BUILD_IDLE_DAYS)
    parser.add_argument('--worktree-idle-days', type=float, default=WORKTREE_IDLE_DAYS)
    args = parser.parse_args()
    for repo in args.repo or repositories():
        report = reap(repo, args.apply, args.build_idle_days, args.worktree_idle_days)
        show(repo, report, args.apply)


if __name__ == '__main__':
    sys.exit(main())
