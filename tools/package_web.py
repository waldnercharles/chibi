#!/usr/bin/env python3
"""Assemble a static site with a serial app and a threaded app."""
import argparse
import shutil
from pathlib import Path


def copy_build(build, destination, threaded=False):
    required = ['chibi.html', 'chibi.js', 'chibi.wasm', 'chibi.data']
    if threaded:
        required.append('chibi-isolation.js')
    for name in required:
        if not (build / name).is_file():
            raise FileNotFoundError(build / name)
    destination.mkdir(parents=True, exist_ok=True)
    if (build / 'chibi-default.js').is_file():
        required.append('chibi-default.js')
    for name in required:
        target = destination / ('index.html' if name == 'chibi.html' else name)
        if (build / name).resolve() != target.resolve():
            shutil.copy2(build / name, target)
    # Both the development and packaged entry retain their URL when switching.
    if build.resolve() != destination.resolve():
        shutil.copy2(build / 'chibi.html', destination / 'chibi.html')
    if (build / 'chibi.worker.js').is_file():
        if build.resolve() != destination.resolve():
            shutil.copy2(build / 'chibi.worker.js', destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', type=Path, default=Path('build-web'))
    parser.add_argument('--threads', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build-web-site'))
    args = parser.parse_args()
    threaded = args.output / 'threads'
    entries = [threaded / name for name in ('index.html', 'chibi.html')]
    for path in (args.output, threaded, *entries):
        if path.is_symlink():
            raise ValueError(f'Package destination must not be a symlink: {path}')
    copy_build(args.serial, args.output)
    if args.threads:
        copy_build(args.threads, threaded, threaded=True)
    else:
        # Old worker binaries may remain, but cannot be selected by the launcher.
        for entry in entries:
            entry.unlink(missing_ok=True)
    (args.output / '.nojekyll').touch()


if __name__ == '__main__':
    main()
