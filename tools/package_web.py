#!/usr/bin/env python3
"""Assemble a static site with a serial app and an optional threaded app."""
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
    for name in required:
        shutil.copy2(build / name, destination / ('index.html' if name == 'chibi.html' else name))
    if (build / 'chibi.worker.js').is_file():
        shutil.copy2(build / 'chibi.worker.js', destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', type=Path, default=Path('build-web'))
    parser.add_argument('--threads', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build-web-site'))
    args = parser.parse_args()
    copy_build(args.serial, args.output)
    if args.threads:
        copy_build(args.threads, args.output / 'threads', threaded=True)
    (args.output / '.nojekyll').touch()


if __name__ == '__main__':
    main()
