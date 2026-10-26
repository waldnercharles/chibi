#!/usr/bin/env python3
"""Install CI dependencies using a private copy of the hosted runner's sources.

Only the runner-images three-entry mirror list and single-URI .list/.sources
entries are supported. See ubuntu-apt.md before extending this deliberately
bounded substitution; APT still owns source parsing and signature validation.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


MIRROR_URI = 'mirror+file:/etc/apt/apt-mirrors.txt'
MIRRORS = (
    'http://azure.archive.ubuntu.com/ubuntu/\tpriority:1',
    'https://archive.ubuntu.com/ubuntu/\tpriority:2',
    'https://security.ubuntu.com/ubuntu/\tpriority:3',
)


def prefer_https(text):
    # Reject added mirrors, metadata/filters, duplicates and changed priorities.
    # Never flatten a mirror list whose semantics we have not inspected.
    entries = [line for line in text.splitlines()
               if line.strip() and not line.startswith('#')]
    if entries != list(MIRRORS):
        raise ValueError('unexpected hosted Ubuntu mirror list; review ubuntu-apt.md')
    replacements = {old: old[:-1] + str(priority)
                    for old, priority in zip(MIRRORS, (3, 1, 2))}
    return ''.join(replacements.get(line.rstrip('\r\n'), line.rstrip('\r\n'))
                   + line[len(line.rstrip('\r\n')):] for line in text.splitlines(keepends=True))


def rewrite_source(text, suffix, uri):
    prefix = r'URIs:[ \t]+' if suffix == '.sources' else r'deb(?:-src)?[ \t]+(?:\[[^\]\n]+\][ \t]+)?'
    end = r'(?=[ \t]*\r?\n?$)' if suffix == '.sources' else r'(?=[ \t])'
    pattern = re.compile(r'^(' + prefix + ')' + re.escape(MIRROR_URI) + end)
    result = []
    count = 0
    for line in text.splitlines(keepends=True):
        if line.lstrip().startswith('#'):
            result.append(line)
            continue
        replaced, matches = pattern.subn(lambda match: match[1] + uri, line)
        if not matches and ('apt-mirrors.txt' in line or 'azure.archive.ubuntu.com' in line):
            raise ValueError('unsupported Ubuntu source entry: ' + line.strip())
        result.append(replaced)
        count += matches
    return ''.join(result), count


def prepare_sources(apt, destination):
    mirrors = prefer_https((apt / 'apt-mirrors.txt').read_text())
    parts = destination / 'sources.list.d'
    parts.mkdir()
    (destination / 'lists').mkdir()  # Empty: never reuse indexes for another URI.
    (destination / 'apt-mirrors.txt').write_text(mirrors)
    (destination / 'sources.list').write_text('')
    sources = [apt / 'sources.list'] if (apt / 'sources.list').exists() else []
    sources += sorted(path for path in (apt / 'sources.list.d').iterdir()
                      if re.fullmatch(r'[a-zA-Z0-9_.-]+\.(list|sources)', path.name))
    count = 0
    for path in sources:
        text, changed = rewrite_source(path.read_bytes().decode(), path.suffix,
                                       'mirror+file:' + str(destination / 'apt-mirrors.txt'))
        (destination / path.relative_to(apt)).write_bytes(text.encode())
        count += changed
    if not count:
        raise ValueError('no supported hosted Ubuntu mirror source found')
    # mkdtemp is private by default; APT's _apt user must read the copied files.
    destination.chmod(0o755)
    for path in destination.rglob('*'):
        path.chmod(0o755 if path.is_dir() else 0o644)
    return [option for key, value in (
        ('Dir::Etc::sourcelist', destination / 'sources.list'),
        ('Dir::Etc::sourceparts', parts),
        ('Dir::State::lists', destination / 'lists'),
        ('Dir::Cache::pkgcache', ''),
        ('Dir::Cache::srcpkgcache', ''),
        ('APT::Update::Error-Mode', 'any'),
    ) for option in ('-o', f'{key}={value}')]


def install(options, packages, no_recommends):
    # Both operations see the same sources, mirror priorities and fresh lists.
    subprocess.run(['sudo', 'apt-get', *options, '-o', 'Debug::Acquire::gpgv=true',
                    'update'], check=True)
    subprocess.run(['apt-cache', *options, 'policy', *packages], check=True)
    flags = ['--no-install-recommends'] if no_recommends else []
    subprocess.run(['sudo', 'apt-get', *options, 'install', '-y', *flags, *packages], check=True)
    subprocess.run(['dpkg-query', '-W', '-f=${binary:Package}\t${Version}\n', *packages], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-install-recommends', action='store_true')
    parser.add_argument('packages', nargs='+')
    args = parser.parse_args()
    destination = Path(tempfile.mkdtemp(prefix='chibi-apt-', dir=os.environ['RUNNER_TEMP']))
    options = prepare_sources(Path('/etc/apt'), destination)
    print(f'CI APT sources and fresh lists: {destination}', flush=True)
    install(options, args.packages, args.no_install_recommends)


if __name__ == '__main__':
    main()
