# Managed pointers: raw receipts at ec64c1e70

These are the unedited logs of focused selectors and the CI check chain on
ec64c1e70, run once on that clean tree (each focused log records the revision
and that no tracked file was modified). ec64c1e70 is bd84d6e49 plus a test that
pins the asset source route to 16 MiB and wording that names both command-line
outputs taking the 32 MiB artifact bound. The commit that adds this directory
changes nothing else. These replace the receipts for bd84d6e49, bfbb66116 and
8eb31f228, which are in this branch's history.

Host: Linux x64, 4 CPUs, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

## Focused selectors (`focused/`)

`make test-build JOBS=4` first (`focused/test-build.log`, incremental), then:

| Log | Command | Result |
| --- | --- | --- |
| `focused/language-asset-compiler-host.log` | `python3 tools/test_policy.py tool language-asset-compiler-host` | 11 of 11, exit 0 |
| `focused/language-package-compiler-chibi-collections.log` | `python3 tools/test_policy.py tool language-package-compiler-chibi-collections` | 1 case, 5,463 asserts, exit 0 |
| `focused/language-package-compiler-chibi-collections-compiled.log` | `python3 tools/test_policy.py tool language-package-compiler-chibi-collections-compiled` | 1 case, 4,952 asserts, exit 0 |
| `focused/compiler-artifact.log` | `python3 tools/test_policy.py tool compiler-artifact` | 16 of 16, exit 0 |

The two collections selectors run every collections sample, including
`managed pointer compound float widening`, `managed pointer compound narrowing`
and `managed pointer compound mixed integers`; their logs print only failures.
`compiler-artifact` holds `binary_result_limit_is_atomic`. The asset compiler
host selector holds `asset_source_keeps_the_16_mib_source_bound`, which failed
when that route was temporarily switched to the artifact bound. No fixed-point
selector (`language-package-compiler-chibi-bootstrap`) was run, and no
fixed-point result is claimed.

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
| `language-program.log` | `python3 tools/test_policy.py tool language-program` | 172 of 172 |

`test-language-all` runs the `language` policy group, which does not include
the collections or fixed-point selectors; the focused logs above cover the
collections selectors.

`detail/` holds 20 of the 26 per-step logs that the wrappers name as
`.chibi/logs/<id>-<step>.log`. `tools/quiet.py` keeps only the 20 most recent,
so these six were gone when the runs finished:

- `focused/test-build.log`: `1792928388396693884-12853-configure-native.log`
  and `1792928391119813897-12884-build.log`.
- `test-build.log`: `1792928964082863211-13200-configure-native.log` and
  `1792928964232928014-13214-build.log`.
- `release.log`: `1792928964454312392-13229-configure-native.log`.
- `test-tools.log`: `1792930122329546754-21997-graph.log`, which is the
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
- A byte result over 16 MiB through each in-process route at runtime; the
  asset source route is checked, and the 17.7 MB facade passes the
  command-line `--output-bytes` route on every asset compiler build.
- GPU, Studio, Windows, macOS and the LLVM native path.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at ec64c1e70 is the downloadable
compiler: 15,300,353 bytes, SHA-256
`aee0a7611b4bd9f9e9c8c098b928bc91d2b8e733649bbb12ed5a70852f7a59ac`. The other
binaries were built from the same tree and are not checked in:

| File | SHA-256 |
| --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (17,670,518 bytes) | `a8f509128233eb8d54fe9c1a2f63822d9f1872e16c1263dd5c449d32cc699220` |
| Sanitizer `build/sdk/chibi-compiler-host` | `8a26207c9a6375ec4a07fb1f1e3407824cef0c9e8aa57227ce388a598aec3667` |
| Sanitizer `build/test/language/test_language` | `9fbdd24f5c821f1416977785cc91af8941889d5645980bf664fe806c5f6ca85f` |
| Sanitizer `build/test/tools/test_tools` | `b91a26a4ff4ed9a372d5e573b6f76d63423e8e61502571d91e217a95d148bf69` |
| `build-release/sdk/chibi-compiler-host` | `55321a064a3ec58dbfa378a393359cd5c18ac95b2e8afe4a93c8de75e31cf8f9` |
| `build-release/bin/chibi_ctl` | `f031f0f506ab7c9159455070abf76b53f6f621de3152ba4a20e0b39dba395e31` |
