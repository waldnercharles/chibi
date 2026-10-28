#!/usr/bin/env python3
"""Build only Chibi's shared syntax formatter; no engine, downloads or installs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PIN = "da6fe9beb4f7f67beb75914ca8e0d48ae48d6406"


def build(runtime=None, output=None):
    expected = re.search(r'chibi_third_party\(tree_sitter\s+\S+\s+([0-9a-f]{40})\)',
                         (ROOT / 'third_party/CMakeLists.txt').read_text()).group(1)
    if expected != PIN:
        raise ValueError('Update formatter tree-sitter pin alongside third_party/CMakeLists.txt')
    candidates = [runtime or os.environ.get('CHIBI_TREE_SITTER_SOURCE'),
                  ROOT / '.chibi/tree-sitter',
                  ROOT / 'build/_deps/chibi_language_tree_sitter-src',
                  ROOT / 'build-debug/_deps/chibi_language_tree_sitter-src']
    runtime = next((Path(p).resolve() for p in candidates if p and (Path(p) / 'lib/src/lib.c').is_file()), None)
    if runtime is None:
        raise ValueError('Set CHIBI_TREE_SITTER_SOURCE to the already provisioned tree-sitter '
                         f'source at {PIN}; this command never downloads or installs tools.')
    # An explicit existing source copy is supported, but its exact upstream
    # revision must be verifiable rather than accidentally using system headers.
    head = subprocess.check_output(['git', '-C', str(runtime), 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        raise ValueError(f'tree-sitter source must be pinned to {PIN}, found {head}')
    if subprocess.check_output(['git', '-C', str(runtime), 'status', '--porcelain', '--', 'lib']):
        raise ValueError('tree-sitter runtime source has local changes')
    requested_output = output
    output = Path(output or ROOT / ('.chibi/format/chibi-format.exe' if os.name == 'nt' else '.chibi/format/chibi-format')).resolve()
    sources = [ROOT / p for p in ('tools/format/main.c', 'src/authoring/source_format.c',
               'src/authoring/source_format_file.c', 'src/language/syntax.c',
               'src/language/src/parser.c', 'src/language/src/scanner.c', 'src/support/xalloc.c')]
    sources.append(runtime / 'lib/src/lib.c')
    cc = shlex.split(os.environ.get('CC', 'clang' if os.name == 'nt' else 'cc'))
    command = cc + ['-std=gnu11', '-O2', '-Wno-multichar', '-D_DEFAULT_SOURCE', '-D_DARWIN_C_SOURCE',
                    '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'src/language/src'),
                    '-I' + str(runtime / 'lib/include'), '-I' + str(runtime / 'lib/src')]
    if os.name == 'nt':
        sources.append(ROOT / 'src/support/win32/posix.c')
        command += ['-I' + str(ROOT / 'src/support/win32'), '-include', str(ROOT / 'src/support/win32/posix.h')]
    inputs = sources + list((runtime / 'lib').rglob('*.h'))
    if os.name == 'nt':
        inputs += list((ROOT / 'src/support/win32').rglob('*.h'))
    inputs += list((ROOT / 'src/language/src').rglob('*.h'))
    inputs += [ROOT / p for p in ('src/authoring/source_format.h', 'src/authoring/source_format_file.h',
                                  'src/language/syntax.h', 'src/support/array.h', 'src/support/xalloc.h')]
    identity = {'runtime_root': str(runtime), 'command': command, 'compiler': subprocess.check_output(cc + ['--version'], text=True),
                'inputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}}
    if requested_output is None:
        key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:24]
        output = output.parent / key / output.name
    output.parent.mkdir(parents=True, exist_ok=True)
    stamp = output.with_suffix('.json')
    if output.exists() and stamp.exists():
        try:
            cached = json.loads(stamp.read_text())
            digest = cached.pop('binary_sha256', None)
            if cached == identity and hashlib.sha256(output.read_bytes()).hexdigest() == digest:
                return output
        except (OSError, ValueError):
            pass
    descriptor, temporary_name = tempfile.mkstemp(prefix=output.name + '-', suffix='.tmp', dir=output.parent)
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        subprocess.run(command + [str(p) for p in sources] + ['-o', str(temporary)], check=True)
        # Do not publish a cache identity for a mixture changed during compile.
        if any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != digest
               for p, digest in identity['inputs'].items()):
            raise ValueError('formatter input changed during build; retry the exact candidate')
        identity['binary_sha256'] = hashlib.sha256(temporary.read_bytes()).hexdigest()
        temporary.chmod(0o755)
        temporary.replace(output)
        # Atomic metadata plus unique compiler output permit concurrent hooks/CI
        # without sharing a .tmp path. A missing/mismatched stamp only rebuilds.
        with tempfile.NamedTemporaryFile(mode='w', dir=output.parent, prefix='stamp-', delete=False) as stream:
            stamp_temporary = Path(stream.name)
            json.dump(identity, stream, indent=2)
            stream.write('\n')
        stamp_temporary.replace(stamp)
    finally:
        temporary.unlink(missing_ok=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        print(build(args.runtime, args.output))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'format build: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
