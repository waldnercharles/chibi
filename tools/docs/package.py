#!/usr/bin/env python3
"""Compose the documentation and the real browser formatter."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FORMATTER_FILES = ('app.css', 'app.js', 'state.js', 'worker.js')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def sha(value, length):
    return isinstance(value, str) and re.fullmatch(f'[0-9a-f]{{{length}}}', value)


def append_panel(page, panel, marker):
    content = page.read_text()
    require(content.count('</main>') == 1 and marker not in content,
            'Unexpected documentation guide structure')
    page.write_text(content.replace('</main>', panel + '\n</main>'))


def inventory(root):
    require(root.is_dir() and not root.is_symlink(), 'Expected a regular input directory')
    files = {}
    for path in root.rglob('*'):
        require(not path.is_symlink() and (path.is_file() or path.is_dir()),
                'Website inputs cannot contain symlinks or special files')
        if path.is_file():
            files[path.relative_to(root).as_posix()] = digest(path)
    return files


def formatter(source, target, revision):
    files = inventory(source)
    require(set(files) == {'engine.mjs', 'engine.wasm', 'engine.json'},
            'Expected the complete formatter build')
    record = json.loads((source / 'engine.json').read_text())
    require(record.get('format') == 1 and record.get('api') == 1 and
            record.get('revision') == revision and sha(revision, 40) and
            record.get('max_source_bytes') == 256 * 1024, 'Formatter identity does not match the docs')
    require(record.get('files') == {name: files[name] for name in ('engine.mjs', 'engine.wasm')},
            'Formatter artifact checksum mismatch')
    shutil.copytree(source, target)
    require(inventory(source) == files and inventory(target) == files,
            'Formatter build changed during packaging')
    for name in FORMATTER_FILES:
        shutil.copyfile(ROOT / 'tools/docs/formatter' / name, target / name)
    return record


def package(docs, engine, revision, output):
    require(not output.exists() and not output.is_symlink() and output.parent.is_dir(),
            'Output must be a new directory')
    for source in (docs, engine):
        require(not output.resolve().is_relative_to(source.resolve()), 'Output overlaps an input')
    before = inventory(docs)
    require({'index.html', 'formatter.html', 'style.css'} <= set(before),
            'Build the docs manifest, including the formatter guide, first')
    require('website.json' not in before and
            not any(name.startswith('formatter/') for name in before),
            'Input is already packaged; rebuild the documentation first')
    with tempfile.TemporaryDirectory(prefix='docs-site-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'site'
        shutil.copytree(docs, stage)
        panel = (ROOT / 'tools/docs/formatter/panel.html').read_text()
        append_panel(stage / 'formatter.html', panel, 'id="formatter-app"')
        formatter_record = formatter(engine, stage / 'formatter', revision)
        require(inventory(docs) == before, 'Documentation input changed during packaging')
        after = inventory(stage)
        require(all(after[name] == checksum for name, checksum in before.items()
                    if name != 'formatter.html'),
                'Packaging changed another documentation page or its theme')
        record = dict(format=1, revision=revision, formatter=formatter_record, files=after)
        (stage / 'website.json').write_text(json.dumps(record, indent=2) + '\n')
        (stage / '.nojekyll').touch()
        stage.rename(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    compose = commands.add_parser('compose')
    for name in ('docs', 'formatter', 'output'):
        compose.add_argument('--' + name, type=Path, required=True)
    compose.add_argument('--revision', required=True)
    arguments = parser.parse_args()
    package(arguments.docs, arguments.formatter, arguments.revision, arguments.output)
