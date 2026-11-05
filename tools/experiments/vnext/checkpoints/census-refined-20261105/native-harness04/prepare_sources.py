#!/usr/bin/env python3
"""Copy source only. No imports of project generators, builds or native loading.

Future coordinator invokes after v4 generation/review; pins are mandatory but
are identity checks, not proof of admission. This file has not been executed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import stat
import sys
import tempfile

HERE = Path(__file__).resolve().parent
VNEXT = 'tools/experiments/vnext/'
# Exact membership of bridge/prepare_native.py SOURCE_MAP at authoring time.
# A changed closure requires review; do not discover/generate missing packages.
COMPILER = (
    'source_parser/Declarations.chibi', 'source_parser/Expressions.chibi',
    'source_parser/Parser.chibi', 'source_parser/Statements.chibi',
    'source_parser/Tree.chibi', 'source_parser/Types.chibi',
    'source_tokenizer/Tokenizer.chibi', 'syntax_diagnostics/Diagnostics.chibi',
    'json_string/JsonString.chibi', 'lexical_binding/Binding.chibi',
    'named_binding/Binding.chibi', 'signature_reader/Reader.chibi',
    'type_constructors/Constructors.chibi', 'unit_algebra/Units.chibi',
    'package_graph/Graph.chibi')
CORE = ('strings/Strings.chibi', 'os/Os.chibi', 'bytes/Bytes.chibi',
    'bytes/Tests.chibi', 'hash/sha256/Sha256.chibi', 'hash/sha256/Tests.chibi',
    'sys/posix/Posix.chibi', 'target/Target.chibi',
    'sys/windows/Decimal.chibi', 'sys/windows/Windows.chibi')
SOURCE_MAP = [(f'tools/compiler/{n}', f'project/{n}') for n in COMPILER]
SOURCE_MAP += [(f'packages/core/{n}', f'core/{n}') for n in CORE]
SOURCE_MAP += [(VNEXT+f'frontend/typed/{n}', f'project/vnext_typed/{n}')
               for n in ('Model.chibi', 'Produce.chibi', 'Verify.chibi')]
SOURCE_MAP += [(VNEXT+'frontend/bridge/generated/Pack.chibi', 'project/vnext_bridge/Pack.chibi')]

def read(path):
    path = path.absolute()
    if path.resolve() != path or not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError(f'requires regular non-symlink path: {path}')
    return path.read_bytes()

def identity(data):
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

def literal(text):
    # Exact frontend/prepare.py helper, preserved rather than new escaping.
    if any(ord(c) < 32 and c not in "\n\r\t" for c in text):
        raise ValueError("unsupported control byte in source fixture")
    return json.dumps(text, ensure_ascii=False)

def prepare(args):
    root, output = args.root.absolute(), args.output.absolute()
    if (root.resolve() != root or output.resolve() != output or
            output.exists() or output.is_symlink() or not output.parent.is_dir()):
        raise ValueError(f'canonical root and fresh output in existing owned parent required: {output}')
    payload = {target: read(root/source) for source, target in SOURCE_MAP}
    census = read(root/(VNEXT+'source_census/Main.chibi'))
    if census != read(HERE/'Main.chibi'):
        raise ValueError('exact retained Census source changed')
    for name, digest in (('project/vnext_typed/Model.chibi', args.model_sha256),
                         ('project/vnext_bridge/Pack.chibi', args.pack_sha256)):
        if identity(payload[name])['sha256'] != digest:
            raise ValueError(f'approved identity mismatch: {name}')
    if b'pub Version: u32 : 4;' not in payload['project/vnext_bridge/Pack.chibi']:
        raise ValueError('generated v4 Pack required; preparer never generates it')
    # The ABI imports accept plain paths. Hash existing artifacts without loading.
    artifacts = {}
    for name, path in (('backend', args.backend), ('adapter', args.adapter)):
        path = path.absolute()
        if any(c in '\\"' or ord(c) < 32 or ord(c) == 127 for c in str(path)):
            raise ValueError('foreign archive/library needs a plain path')
        data = read(path)
        if not data: raise ValueError(f'empty {name}')
        artifacts[name] = {'path': str(path), **identity(data)}
    sources = [('source_census/Main.chibi', census),
               ('source_census/Capabilities.chibi', read(HERE/'Capabilities.chibi'))]
    rows = []
    for index, (name, source) in enumerate(sources):
        encoded = literal(source.decode('utf-8'))
        if json.loads(encoded).encode('utf-8') != source:
            raise ValueError('source bytes did not round trip')
        payload['inputs/'+name] = source
        rows.append('{path: '+literal(name)+', text: '+encoded+', document: '+str(19+index)+'}')
    template = read(HERE/'CensusMain.chibi.in')
    replacements = {
        b'"LIBRARY_PATH"': literal(artifacts['backend']['path']).encode(),
        b'"ADAPTER_PATH"': literal(artifacts['adapter']['path']).encode(),
        b'FIXTURE_SOURCES': ('[\n'+',\n'.join(rows)+'\n]').encode(),
        b'OUTPUT_PATH': literal(str(output/'run/normal/source-native.o')).encode()}
    for marker in replacements:
        if template.count(marker) != 1: raise ValueError(f'ambiguous marker {marker}')
    payload['entry/Main.chibi'] = re.sub(
        b'"LIBRARY_PATH"|"ADAPTER_PATH"|FIXTURE_SOURCES|OUTPUT_PATH',
        lambda m: replacements[m[0]], template)
    for name in ('native_adapter.c', 'native_adapter.h', 'aot_main.c', 'host-contract.h',
                 'CensusMain.chibi.in', 'Capabilities.chibi', 'reference.py',
                 'prepare_cases.py', 'prepare_sources.py', 'preparation_tests.py',
                 'README.md'):
        payload['inputs/'+name] = read(HERE/name)
    payload['inputs/foreign.h'] = read(root/(VNEXT+'slice_backend/foreign.h'))
    payload['controller/owned_process.py'] = read(root/(VNEXT+'owned_process.py'))
    manifest = {'purpose': 'source-only Census native candidate, not acceptance',
                'artifacts': artifacts,
                'files': {name: identity(data) for name, data in sorted(payload.items())},
                'stages': {name: 'not_run' for name in
                    ('chibi_compile', 'jit_normal', 'perf_failure', 'c_link', 'aot_run')}}
    # Caller exclusively controls output.parent for the entire operation. Portable
    # rename can replace an empty directory; the absence check is not a lock.
    staging = Path(tempfile.mkdtemp(prefix=f'.{output.name}.incomplete-', dir=output.parent))
    try:
        (staging/'run/normal').mkdir(parents=True)
        for name, data in payload.items():
            destination = staging/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open('xb') as stream: stream.write(data)
        with (staging/'source-manifest.json').open('x') as stream:
            stream.write(json.dumps(manifest, indent=2)+'\n')
        if output.exists() or output.is_symlink():
            raise ValueError(f'output appeared before publication: {output}')
        staging.rename(output)
    except BaseException:
        print(f'Preparation did not confirm publication; inspect incomplete staging {staging} '
              f'and final {output}; neither is an acceptance receipt.', file=sys.stderr)
        raise

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'output', 'backend', 'adapter'):
        parser.add_argument('--'+name, required=True, type=Path)
    for name in ('model-sha256', 'pack-sha256'):
        parser.add_argument('--'+name, required=True)
    args = parser.parse_args()
    try: prepare(args)
    except (OSError, ValueError) as error: parser.exit(1, f'Census preparation: {error}\n')
