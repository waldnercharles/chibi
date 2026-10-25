# Managed pointers: raw receipts at bd84d6e49

These are the unedited logs of the CI check chain on bd84d6e49, run once on that
clean tree. bd84d6e49 holds #688's head 468ac12d7 and main at e18299d12 (after
M2 #687, integer reports #696 and their storage budget #699), with prepared
ABI 22 and a bootstrap regenerated from stage zero. On top of bfbb66116 it
widens float operands in pointer compounds and keeps the 16 MiB byte result
default, with a 32 MiB artifact bound for the command-line host's file outputs
only. The commit that adds this directory changes nothing else. They replace
the receipts for bfbb66116 and 8eb31f228, which are in this branch's history.

Host: Linux x64, 4 CPUs, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

| Log | Command | Result |
| --- | --- | --- |
| `test-build.log` | `make test-build JOBS=4` (10 s, incremental) | ok |
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

`language-program.log` was run after the chain, against the same build, so the
172-case result, `program_borrowing_kernel_results_survive_collection`
included, has its own log. The same cases also appear in
`test-language-all.log`.

`detail/` holds 20 of the 24 per-step logs that the wrappers name as
`.chibi/logs/<id>-<step>.log`. `tools/quiet.py` keeps only the 20 most recent,
so these four were gone when the chain finished:

- `test-build.log`: `1792923637431288016-24150-configure-native.log` and
  `1792923639841153928-24180-build.log`.
- `release.log`: `1792923650043285814-24226-configure-native.log`.
- `test-tools.log`: `1792924753598848988-481-graph.log`, which is the
  quiet-wrapper fixture in `test/tools/test_policy.c` failing on purpose in a
  temporary directory.

The wrapper logs keep each step's status and duration. `SHA256SUMS` covers
every log here.

## Not covered

- The `compiler-recovery` workflow. It downloads the native compiler host that
  `tools/compiler/runtime.json` pins, which is still M2's prepared 21 runtime
  release, unchanged here. That host cannot load a prepared 22 bootstrap, so
  the workflow needs a new release from
  `.github/workflows/compiler-runtime-seed.yml` first. `make test-recovery`
  above recovers with the compiler host built from this tree.
- GPU, Studio, Windows, macOS and the LLVM native path.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at bd84d6e49 is the downloadable
compiler: 15,300,353 bytes, SHA-256
`aee0a7611b4bd9f9e9c8c098b928bc91d2b8e733649bbb12ed5a70852f7a59ac`. The other
binaries were built from the same tree and are not checked in. The sanitized
build that compiled this asset compiler took 246 s, just before the chain:

| File | SHA-256 |
| --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (17,670,518 bytes) | `a8f509128233eb8d54fe9c1a2f63822d9f1872e16c1263dd5c449d32cc699220` |
| `build-release/sdk/chibi-compiler-host` | `55321a064a3ec58dbfa378a393359cd5c18ac95b2e8afe4a93c8de75e31cf8f9` |
| `build-release/bin/chibi_ctl` | `f031f0f506ab7c9159455070abf76b53f6f621de3152ba4a20e0b39dba395e31` |
| Sanitizer `build/test/language/test_language` | `45774d8dd90ce2cc315f8b6f13e22f04b52ccbd341ae96dd114ea5ca18f3d1c2` |
