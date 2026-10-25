# Managed pointers: raw receipts at 8eb31f228

These are the unedited logs of the CI check chain on 8eb31f228, run once on that
clean tree. 8eb31f228 is main after #687 (M2) merged into this branch, with
prepared ABI 22, a bootstrap regenerated from stage zero, the compiler host's
32 MiB byte result limit, and the test and documentation fixes on top. The
commit that adds this directory changes nothing else.

Host: Linux x64, 4 CPUs, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

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
| `language-program.log` | `python3 tools/test_policy.py tool language-program` | 169 of 169 |

`language-program.log` was run after the chain, against the same build, so the
169-case result, `program_borrowing_kernel_results_survive_collection`
included, has its own log. The same cases also appear in
`test-language-all.log`.

`detail/` holds 20 of the 24 per-step logs that the wrappers name as
`.chibi/logs/<id>-<step>.log`. `tools/quiet.py` keeps only the 20 most recent,
so these four were gone when the chain finished:

- `test-build.log`: `1792914670711295886-20462-configure-native.log` and
  `1792914673000816989-20492-build.log` (incremental, 10 s).
- `release.log`: `1792914683025192651-20534-configure-native.log`.
- `test-tools.log`: `1792915763024533452-29304-graph.log`, which is the
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

`tools/compiler/bootstrap/compiler.chibip` at 8eb31f228 is the downloadable
compiler: 15,286,759 bytes, SHA-256
`91dd993ed9c3196a48619ea15158886bddd6e982808ab05e152c34746c770fe8`. The other
binaries were built from the same tree and are not checked in:

| File | SHA-256 |
| --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (17,656,923 bytes) | `5d65a56582689275b0de173b91fdc4bf57dd391e11bcf5092c9e31cbbad69b12` |
| `build-release/sdk/chibi-compiler-host` | `73a948e7e3a0e80d82931032d75e1a1e0941ce634935d3098d8a960225f20041` |
| `build-release/bin/chibi_ctl` | `fa77c2aaedf9ac5af2c34c07103e2146b1d4a64a7bcae01f0175d73a1a4ad85c` |
| Sanitizer `build/test/language/test_language` | `8cac27d034153e23dc64e5400f3308168113d90532952dc2f02f8eb7e833617e` |
