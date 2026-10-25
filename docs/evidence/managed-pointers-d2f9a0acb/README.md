# Managed pointers: raw receipts at d2f9a0acb

These are the unedited logs of the CI check chain and the focused selectors on
d2f9a0acb, run once on that clean tree (each focused log records the revision
and that no tracked file was modified). d2f9a0acb is ec64c1e70 plus:

- 714e3f33e, "Accept named native arguments in the
  self-hosted compiler again": `CallStep` in
  `tools/compiler/package_compiler/Steps.chibi` rejected every named argument
  before native parameter matching, a slip from merging the managed-pointer
  commit with main. Only callables without native argument matching reject
  them now. The `native arguments evaluate in source order` sample spells `ref`
  at its reference-parameter call sites, and the compiler host header names
  what each file output writes.
- A merge of main at 1b186cee4 (#688 landed).
- The regenerated compiler bootstrap.

The commit that adds this directory changes nothing else. These replace the
receipts for ec64c1e70, which are in this branch's history.

Host: Linux x64, 4 CPUs, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

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
| `language-program` | 172 of 172 |
| `compiler-artifact` | 16 of 16 |
| `language-asset-compiler-host` | 11 of 11 |
| `language-package-compiler-chibi` | 3 of 3, 17,290 asserts |
| `language-package-compiler-chibi-compiled` | 1 of 1, 12,193 asserts |
| `language-package-compiler-chibi-types` | 1 of 1, 5,017 asserts |
| `language-package-compiler-chibi-types-compiled` | 1 of 1, 4,417 asserts |
| `language-package-compiler-chibi-collections` | 1 of 1, 5,463 asserts |
| `language-package-compiler-chibi-collections-compiled` | 1 of 1, 4,952 asserts |
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

On ec64c1e70, before the fix, the same build failed two of these:
`native-arguments` (12 of 13 named samples on both backends reported "named
arguments are not supported yet") and `native-arguments-oracle` (stage zero
reported "Reference parameter requires an explicit ref argument" for the
source-order sample). Those runs are not kept here.

## Step logs

`detail/` holds 19 of the 25 per-step logs that the wrappers name as
`.chibi/logs/<id>-<step>.log`. `tools/quiet.py` keeps only the 20 most recent,
so these six were gone when the runs finished:

- `test-build.log`: `1792939264817461422-28152-configure-native.log` and
  `1792939267429847780-28182-build.log`.
- `release.log`: `1792939524395301281-28349-configure-native.log` and
  `1792939527060478474-28378-build.log`.
- `test-native.log`: `1792939529903303826-28412-configure-native.log`.
- `test-tools.log`: `1792940675449458950-4532-graph.log`, which is the
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
- The fixed-point selector, as above.
- A byte result over 16 MiB through each in-process route at runtime; the
  asset source route is checked, and the 17.7 MB facade passes the
  command-line `--output-bytes` route on every asset compiler build.
- GPU, Studio, Windows, macOS and the LLVM native path.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at d2f9a0acb is the downloadable
compiler: 15,300,973 bytes, SHA-256
`b5554deef3b8747879110e5eab7713604e3226a7affd18013789c60c37c3682c`. The other
binaries were built from the same tree and are not checked in:

| File | SHA-256 |
| --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (17,671,137 bytes) | `e025f07c25fba493d2f1eb4088a25cc3ff306a4af005fb08f144d83945cbc5e2` |
| Sanitizer `build/sdk/chibi-compiler-host` | `c0b51103bcab8a4e6fbd97b6a3907ba70c606955b3b13af4ffc9a400c6cc4a3f` |
| Sanitizer `build/test/language/test_language` | `8c396e1920f00bf1c554adacfee25d5c73d9293599c3ed5b805baf7d9f00010c` |
| Sanitizer `build/test/tools/test_tools` | `3809ee144a373b2a7977618bdd7a835e1413adb1f9c655f25ffc63839f8e1d79` |
| `build-release/sdk/chibi-compiler-host` | `55321a064a3ec58dbfa378a393359cd5c18ac95b2e8afe4a93c8de75e31cf8f9` |
| `build-release/bin/chibi_ctl` | `f031f0f506ab7c9159455070abf76b53f6f621de3152ba4a20e0b39dba395e31` |

The two Release binaries hash the same as at ec64c1e70: the only C source
change between the two revisions is a comment in `compiler_host.h`.
