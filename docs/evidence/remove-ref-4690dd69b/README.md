# Remove authored ref: raw receipts at 4690dd69b

These are the unedited logs of the CI check chain and the focused selectors on
4690dd69b, run once on that clean tree (each focused log records the revision
and that no tracked file was modified). 4690dd69b is:

- 20d15f41c, the review 3 repair in stage zero (`callable_headers.c`,
  `semantic_calls.c`) and the matching `ScopedForward` change in
  `References.chibi`: a typed argument's value and a parenthesized argument
  forward a scoped pointer, and `(p^)` lends p's target as `p^` does.
- A merge of #695 at beb2045e0 (main through #688, the named native argument
  fix), then a regenerated bootstrap (27f8aeaab).
- b157708f9, the same repair in the self-hosted compiler: `CallArgument` sees
  through parentheses, and `ReferenceArgumentStep` forwards `(p)` as `p` and
  lends `(p^)` as `p^`, keeping the written node's expression point.
- A regenerated bootstrap (4690dd69b).

The commit that adds this directory changes nothing else. These replace the
d70f1c317 receipts, which are in this branch's history. No earlier #698 result
is carried over; this is its own run.

Host: Linux x64, 4 CPUs, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

## The two review 3 shapes

| Shape | Stage zero, bytecode and compiled backends | Self-hosted compiler |
| --- | --- | --- |
| `Bump(value: ^double = p)` | `program_typed_and_parenthesized_forwarding_keep_pointer_scoped`: `a` is literal 5 | Rejects typed arguments ("typed arguments are not supported yet"), as before |
| `Bump((p))` | same case: `b` is literal 5 | `scoped pointer relays` sample, `Parenthesized` returns 3 |
| `Bump((p^))` | same case: `c` is literal 5 | same sample and call |

The program case loads the behavior on `BEHAVIOR_BYTECODE` and the compiled
backend, dispatches `Impulse` through `execution_instance_dispatch`, and reads
`a`, `b` and `c` from the instance. The package compiler sample runs through
both collections selectors: `collections` compares stage zero with the
self-hosted compiler, and `collections-compiled` runs the compiled closure.

`before/` holds the failing runs that preceded each repair:

- `stage-zero-language-program.log` and
  `stage-zero-language-package-compiler-chibi-collections.log`: ddf7dbdbc with
  only the two test files changed (the "2 modified" in their headers). The
  program case failed to compile ("Incompatible types: expected double, got
  pointer"), and the collections sample was rejected by stage zero.
- `stage-one-language-package-compiler-chibi-collections.log` and
  `stage-one-language-package-compiler-chibi-collections-compiled.log`:
  27f8aeaab, after the stage zero repair and before b157708f9. Stage one
  rejected `Bump((p))` with "Pointer parameter requires &value or a pointer at
  11:29".

## CI chain

| Log | Command | Result |
| --- | --- | --- |
| `test-build.log` | `make test-build JOBS=4` | ok |
| `release.log` | `make build-release JOBS=4 BUILD_TARGET='chibi_ctl chibi chibi-compiler-host'` | ok |
| `test-native.log` | `make test-native` | ok |
| `test-materials.log` | `make test-materials` | ok |
| `test-projects.log` | `make test-projects` | ok |
| `test-language-all.log` | `make test-language-all` | ok |
| `test-tools.log` | `make test-tools` | ok |
| `check-format.log` | `make check-format` | ok |
| `test-recovery.log` | `make test-recovery` | ok |
| `sdk.log` | `make package-sdk SDK_PREFIX=<temporary prefix>` | ok |

`test-language-all` runs the `language` policy group. It does not include the
`compiler` group's package compiler selectors, which CI does not run either;
the focused logs below cover them.

## Focused selectors (`focused/`)

`make test-build JOBS=4` first (`focused/test-build.log`, incremental), then
`python3 tools/test_policy.py tool <selector>` for each:

| Selector | Result |
| --- | --- |
| `language-program` | 175 of 175 |
| `compiler-artifact` | 16 of 16 |
| `language-asset-compiler-host` | 11 of 11 |
| `language-package-compiler-chibi` | 3 of 3, 17,290 asserts |
| `language-package-compiler-chibi-compiled` | 1 of 1, 12,193 asserts |
| `language-package-compiler-chibi-types` | 1 of 1, 5,017 asserts |
| `language-package-compiler-chibi-types-compiled` | 1 of 1, 4,417 asserts |
| `language-package-compiler-chibi-collections` | 1 of 1, 5,290 asserts |
| `language-package-compiler-chibi-collections-compiled` | 1 of 1, 4,723 asserts |
| `language-package-compiler-chibi-literals` | 1 of 1, 4,090 asserts |
| `language-package-compiler-chibi-behaviors` | 1 of 1, 21,830 asserts |
| `language-package-compiler-chibi-behaviors-rest` | 2 of 2, 36,848 asserts |
| `language-package-compiler-chibi-sharing` | 1 of 1, 139 asserts |
| `language-package-compiler-chibi-debug-free` | 1 of 1, 2,106 asserts |
| `language-package-compiler-chibi-native-arguments` | 1 of 1, 9,923 asserts |
| `language-package-compiler-chibi-native-arguments-oracle` | 1 of 1, 227 asserts |
| `language-package-compiler-chibi-scalar` | 3 of 3, 11,117 asserts |
| `language-package-compiler-chibi-imports` | 2 of 2, 11,326 asserts |
| `language-package-compiler-chibi-imports-compiled` | 2 of 2, 11,028 asserts |

Every selector exited 0. That is the whole `compiler` group except
`language-package-compiler-chibi-bootstrap`, the fixed-point check, which was
not run; no fixed-point result is claimed.

## Step logs

`detail/` holds 19 of the 25 per-step logs that the wrappers name as
`.chibi/logs/<id>-<step>.log`. `tools/quiet.py` keeps only the 20 most recent,
so these six were gone when the runs finished:

- `test-build.log`: `1792949739635567478-19238-configure-native.log` and
  `1792949742158041083-19269-build.log`.
- `release.log`: `1792949991380893178-19412-configure-native.log` and
  `1792949993855158163-19443-build.log`.
- `test-native.log`: `1792949996807755173-19477-configure-native.log`.
- `test-tools.log`: `1792951151533521111-28059-graph.log`, which is the
  quiet-wrapper fixture in `test/tools/test_policy.c` failing on purpose in a
  temporary directory.

The wrapper logs keep each step's status and duration. `SHA256SUMS` covers
every log here. All of this is local evidence; no hosted check ran on this
revision.

## Not covered

- The `compiler-recovery` workflow. It downloads the native compiler host that
  `tools/compiler/runtime.json` pins, which is still M2's prepared 21 runtime
  release, unchanged here. That host cannot load a prepared 22 bootstrap, so
  the workflow needs a new release from
  `.github/workflows/compiler-runtime-seed.yml` first. `make test-recovery`
  above recovers with the compiler host built from this tree.
- Imported forwarding, which is checked by source inspection only.
- The typed shape in the self-hosted compiler, which rejects typed arguments.
- The fixed-point selector, as above.
- GPU, Studio, Windows, macOS and the LLVM native path.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at 4690dd69b is the downloadable
compiler: 15,533,481 bytes, SHA-256
`8fc30ea74e3ec8db6c18a538f07e6ba0ac5454a4de54db0e5dc9e27241625137`. The other
binaries were built from the same tree and are not checked in:

| File | SHA-256 |
| --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (17,903,644 bytes) | `74b39a3f8e99369b0a65a83741aa101c400c3ee5dd175df2667699dfa2663757` |
| Sanitizer `build/sdk/chibi-compiler-host` | `33f03794ad08e604c8b7c6a70e1e1bb3d712419592375e860a82daff5c3bde9c` |
| Sanitizer `build/test/language/test_language` | `cf4c09607f17576eb6313e8aa91829f29c34b496ab5fe2badac03d784fb2f009` |
| Sanitizer `build/test/tools/test_tools` | `50e36690c6571afeae780662a5bf839144944eca9d98f3299a87503b092bbb50` |
| `build-release/sdk/chibi-compiler-host` | `da16b1e9f39763727b96f375a0f77d6b937c234e752587b3014fe62fb67ef004` |
| `build-release/bin/chibi_ctl` | `c414a3a21fa38c68e3723246d7ce331cc5e426e60d95c936c95db593af90f50a` |
