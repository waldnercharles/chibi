# Floating conversion handoff receipts

These receipts retain the independent-review fixture correction against production
source commit `83f9977baafe0390c6cee09c80e51eb5c19a24fb`, based on
`7ffa3ef48b204658ba6d9f95f6b2716039fd8798`. Production files are unchanged by
this correction. The typed-float constant dependency remains visibly blocked.
No `Compile.chibi`, `Scalar.chibi`, seed or bootstrap changes are included.

## Pinned supported rejections

Stage zero requires exactly one diagnostic with this exact message:
`Incompatible types: expected float, got double; convert with float(value)`.
Stage one pins the following existing diagnostics. The source strings remain
unchanged; positions are one-based and tied to those fixed fixtures.

| Boundary | Stage-zero location | Stage-one location | Stage-one message |
| --- | --- | --- | --- |
| Declaration | 2:40 | 2:40 | `local value differs from its type` |
| Assignment | 2:61 | 2:61 | `assigned value differs from its local` |
| Return | 2:47 | 2:40 | `returned value differs from the result type` |
| Argument | 3:52 | 3:47 | `argument differs from its parameter type` |

Stage zero also pins typed-constant narrowing at 3:17 and constant return at
3:47. Stage one currently fails earlier at 2:7 with
`constant type is not supported yet`. Those negatives are explicitly blocked,
not credited as narrowing proof. Their future narrowing diagnostic/category
must be allocated with the constant implementation. The positive
`constant-round-even` case still expects acceptance and `[16777216.0]`, and is
still red at 2:8 with the unsupported-type diagnostic.

## Final readable command receipts

Each linked text file contains the exact supervised command and a complete
readable stdout/stderr view with trailing whitespace removed. The `raw_captures`
strings in the linked results JSON preserve the complete original whitespace.
Only the absolute local worktree prefix is replaced with `<worktree>` in both
forms; no diagnostic, expected answer, status or timing is changed.

- [Stage zero](float-conversion/stage-zero.txt)
- [Native/authored type relations](float-conversion/type-relations.txt)
- [Stage one, bytecode compiler](float-conversion/stage-one-bytecode.txt)
- [Stage one, compiled compiler](float-conversion/stage-one-compiled.txt)
- [Admission mutation](float-conversion/mutation-admission.txt)
- [Widening-emission mutation](float-conversion/mutation-emission.txt)
- [Unrelated-rejection mutation](float-conversion/mutation-category.txt)
- [Exit statuses, hashes, source restoration and adaptation record](float-conversion/results.json)

Configuration/build commands were separate from tests. Every build used:

```sh
cmake --build .chibi/float-oracles/current --target test_language --parallel 2
```

The mutation command actually executed was:

```sh
python3 .chibi/float-oracles/run-review-mutations.py
```

The complete driver text as executed is retained below. It changes production
behavior only, never fixture answers; its finalizer restores exact source bytes
and rebuilds the ordinary driver. Baseline import selectors exit 1 because their
separate constant dependency is red. A mutation is credited only when the new
specific rejection or wrong-value witness is present, not merely for exit 1.

```python
import hashlib
import json
import os
import subprocess
from pathlib import Path

root = Path('.chibi/float-oracles')
env = os.environ.copy()
env.update(CHIBI_LANGUAGE_BUILD_DIR='.chibi/float-oracles/current/test/language',
           CHIBI_RELEASE_LANGUAGE_BUILD_DIR='.chibi/float-oracles/current/test/language')
paths = [Path('src/capabilities/catalog.c'), Path('tools/compiler/package_compiler/Steps.chibi')]
original = {path: path.read_bytes() for path in paths}
fixtures = [Path('test/language') / name for name in
            ['float_conversion_cases.h', 'semantic.c', 'packed.c',
             'type_relations_chibi.c', 'package_compiler_chibi.c']]
fixture_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in fixtures}
rows = []

def build(label):
    with (root / (label + '-build.log')).open('w') as log:
        subprocess.run(['cmake', '--build', '.chibi/float-oracles/current',
                        '--target', 'test_language', '--parallel', '2'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)

def check(name, label):
    with (root / (label + '.log')).open('w') as log:
        result = subprocess.run(['python3', 'tools/test_policy.py', 'tool', name],
                                env=env, stdout=log, stderr=subprocess.STDOUT)
    return result.returncode, (root / (label + '.log')).read_text()

try:
    path = paths[0]
    source = original[path].decode()
    target = 'return !(from == PIN_F64 && to == PIN_F32);'
    assert source.count(target) == 1
    path.write_text(source.replace(target, 'return true;', 1))
    build('review-mutation-admission')
    status, log = check('language-semantic', 'review-mutation-admission')
    assert status != 0 and 'floating admission implicit-narrow' in log
    rows.append({'mutation': 'restore broad native floating admission',
                 'exit': status, 'caught': True})
    path.write_bytes(original[path])

    path = paths[1]
    source = original[path].decode()
    target = '''    if (Floating(from) && Floating(to)) {
        return [Instruction(ops.convert, to, slot, input, 0, 0, node)];
    }'''
    replacement = '''    if (Floating(from) && Floating(to)) {
        if (FloatWidens(from, to)) {
            return [ConstantCode(ops, to, 0, slot, node)];
        }
        return [Instruction(ops.convert, to, slot, input, 0, 0, node)];
    }'''
    assert source.count(target) == 1
    source = source.replace(target, replacement, 1)
    target = 'code.append(Instruction(ops.convert, expected.storage, at.slot + length(slots) - 1, argument.slot, 0, 0, node));'
    assert source.count(target) == 1
    source = source.replace(target, 'code.append(ConstantCode(ops, expected.storage, 0, at.slot + length(slots) - 1, node));', 1)
    path.write_text(source)
    build('review-mutation-emission')
    status, log = check('language-package-compiler-chibi-imports', 'review-mutation-emission')
    assert status != 0 and 'package compiler run Fold[1.5]' in log and '[0.0]' in log
    rows.append({'mutation': 'emit typed zero instead of widened values',
                 'exit': status, 'caught': True})
    path.write_bytes(original[path])

    source = original[path].decode()
    target = '"local value differs from its type"'
    assert source.count(target) == 1
    path.write_text(source.replace(target, '"unrelated conversion failure"', 1))
    build('review-mutation-category')
    status, log = check('language-package-compiler-chibi-imports', 'review-mutation-category')
    assert status != 0
    assert 'FLOAT_REJECTION_MISMATCH stage1 implicit-narrow-declaration' in log
    assert 'unrelated conversion failure' in log
    rows.append({'mutation': 'replace narrowing diagnostic with unrelated rejection',
                 'exit': status, 'caught': True})
finally:
    for path, data in original.items():
        path.write_bytes(data)
    build('review-restored-production')
    assert all(path.read_bytes() == data for path, data in original.items())
    assert fixture_hashes == {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in fixtures}
    result = {'controls': rows, 'production_sources_restored': True,
              'fixture_hashes_unchanged': True,
              'production_sha256': {str(path): hashlib.sha256(data).hexdigest()
                                    for path, data in original.items()},
              'fixture_sha256': fixture_hashes}
    (root / 'review-mutation-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
```

## Minimal M2 adaptation

The [three-file patch](float-conversion-m2-ab8c4a58.patch) targets exactly
`ab8c4a58f6cabc8b74e9d94c37fde41cb634c2ac`. It contains 41 inserted and 18 removed
lines across `catalog.c`, `Relations.chibi` and `Steps.chibi`. It was generated
from snapshots, not by checking out M2 or replacing the current `Steps.chibi`.
The M2 `UntypedStorage`/`ConstantStep` integer path, integer modes and all other
M2 changes remain intact. Read-only inspection also confirmed that the call
address `b` has no uses between its old definition and result allocation, so
moving its definition after widening temporaries does not cross an existing use.
Reversing the 18 floating edit blocks restores the three original M2 snapshots
byte-for-byte.

The exact read-only apply check passed with exit 0:

```sh
git apply --check --directory=.chibi/float-oracles/m2-check docs/evidence/float-conversion-m2-ab8c4a58.patch
```

The directory above contained the three unmodified `git show <M2>:<path>`
snapshots. This is textual apply validation only. The combined M2 source was
not compiled or executed, and no competing seed was generated. The coordinator
owns adaptation review, combined validation and the single generation.

Patch SHA-256: `cf38bab05dc06b49c57f3e33f1224bdfa5d4eb5d82562ca2b0f185d4cd87ac8e`.
