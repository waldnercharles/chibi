#!/usr/bin/env python3
"""Build the real Chibi formatter for a browser worker; never install tools."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TREE_SITTER = 'da6fe9beb4f7f67beb75914ca8e0d48ae48d6406'
EMSDK = '6.0.10'
SOURCES = ('tools/docs/formatter/api.c', 'src/authoring/source_format.c',
           'src/language/syntax.c', 'src/language/src/parser.c',
           'src/language/src/scanner.c', 'src/support/xalloc.c')
EXPORTS = ('_malloc', '_free', '_chibi_format_web', '_chibi_format_web_error',
           '_chibi_format_web_revision', '_chibi_format_web_api')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(root, *arguments):
    return subprocess.check_output(['git', '-C', str(root), *arguments], text=True).strip()


def build(runtime, output):
    runtime = runtime.resolve()
    require(not output.exists(), 'Formatter output must be a new directory')
    revision = git(ROOT, 'rev-parse', 'HEAD')
    require(re.fullmatch('[0-9a-f]{40}', revision), 'Expected an exact source revision')
    require(not git(ROOT, 'status', '--porcelain'), 'Formatter source must be clean and committed')
    pin = re.search(r'chibi_third_party\(tree_sitter\s+\S+\s+([0-9a-f]{40})\)',
                    (ROOT / 'third_party/CMakeLists.txt').read_text())
    require(pin and pin.group(1) == TREE_SITTER, 'Update the formatter tree-sitter pin with the engine')
    require(git(runtime, 'rev-parse', 'HEAD') == TREE_SITTER, 'Unexpected tree-sitter revision')
    require(not git(runtime, 'status', '--porcelain'), 'Tree-sitter source must be clean')
    version = subprocess.check_output(['emcc', '--version'], text=True)
    require(re.search(r'\b' + re.escape(EMSDK) + r'\b', version.splitlines()[0]),
            f'This formatter build requires Emscripten {EMSDK}')
    sources = [ROOT / name for name in SOURCES] + [runtime / 'lib/src/lib.c']
    headers = [ROOT / name for name in ('tools/docs/formatter/api.h',
               'src/authoring/source_format.h', 'src/language/syntax.h',
               'src/support/array.h', 'src/support/xalloc.h')]
    headers += list((ROOT / 'src/language/src').rglob('*.h'))
    headers += list((runtime / 'lib').rglob('*.h'))
    # Tree-sitter's unity lib.c includes its implementation .c files directly.
    runtime_sources = list((runtime / 'lib/src').rglob('*.c'))
    inputs = {str(path): digest(path) for path in sorted(set(sources + headers + runtime_sources))}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='formatter-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'formatter'
        stage.mkdir()
        command = ['emcc', '-std=gnu11', '-O2', '-Wno-multichar', '-D_DEFAULT_SOURCE',
                   f'-DCHIBI_FORMAT_REVISION="{revision}"', '-I' + str(ROOT / 'src'),
                   '-I' + str(ROOT / 'src/language/src'), '-I' + str(runtime / 'lib/include'),
                   '-I' + str(runtime / 'lib/src'), '-sMODULARIZE=1', '-sEXPORT_ES6=1',
                   '-sENVIRONMENT=worker', '-sFILESYSTEM=0', '-sALLOW_MEMORY_GROWTH=1',
                   '-sMAXIMUM_MEMORY=268435456', '-sSTACK_SIZE=1048576',
                   '-sDYNAMIC_EXECUTION=0', '-sEXPORTED_FUNCTIONS=' + json.dumps(EXPORTS),
                   '-sEXPORTED_RUNTIME_METHODS=["HEAPU8"]', '-Wl,--gc-sections',
                   '--no-entry', *map(str, sources), '-o', str(stage / 'engine.mjs')]
        subprocess.run(command, check=True)
        require(all(digest(Path(path)) == checksum for path, checksum in inputs.items()),
                'Formatter inputs changed during compilation')
        require(not git(ROOT, 'status', '--porcelain') and
                git(ROOT, 'rev-parse', 'HEAD') == revision, 'Source changed during compilation')
        require(not git(runtime, 'status', '--porcelain') and
                git(runtime, 'rev-parse', 'HEAD') == TREE_SITTER,
                'Tree-sitter source changed during compilation')
        require({path.name for path in stage.iterdir()} == {'engine.mjs', 'engine.wasm'},
                'Unexpected formatter build outputs')
        metadata = dict(format=1, api=1, revision=revision, tree_sitter=TREE_SITTER,
                        emsdk=EMSDK, max_source_bytes=256 * 1024,
                        files={name: digest(stage / name) for name in ('engine.mjs', 'engine.wasm')})
        (stage / 'engine.json').write_text(json.dumps(metadata, indent=2) + '\n')
        stage.rename(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    build(arguments.runtime, arguments.output)
