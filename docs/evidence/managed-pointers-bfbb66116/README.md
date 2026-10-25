# Managed pointers: raw receipts at bfbb66116

These are the unedited logs of the CI check chain on bfbb66116, run once on that
clean tree. bfbb66116 holds #688's head 468ac12d7 and main at e18299d12 (after
M2 #687, integer reports #696 and their storage budget #699), with prepared
ABI 22, a bootstrap regenerated from stage zero and the compiler host's 32 MiB
byte result limit. The commit that adds this directory changes nothing else.
They replace the receipts for 8eb31f228, the same branch before #696 and #699,
which are in that commit's history.

Host: Linux x64, 4 CPUs, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

| Log | Command | Result |
| --- | --- | --- |
| `test-build.log` | `make test-build JOBS=4` (233 s, asset compiler rebuilt) | ok |
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

- `test-build.log`: `1792916314867845063-30960-configure-native.log` and
  `1792916317500125833-30989-build.log`.
- `release.log`: `1792916550734152544-31284-configure-native.log`.
- `test-tools.log`: `1792917658342329958-7785-graph.log`, which is the
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

`tools/compiler/bootstrap/compiler.chibip` at bfbb66116 is the downloadable
compiler: 15,286,759 bytes, SHA-256
`91dd993ed9c3196a48619ea15158886bddd6e982808ab05e152c34746c770fe8`. The other
binaries were built from the same tree and are not checked in:

| File | SHA-256 |
| --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (17,656,923 bytes) | `5d65a56582689275b0de173b91fdc4bf57dd391e11bcf5092c9e31cbbad69b12` |
| `build-release/sdk/chibi-compiler-host` | `9380998643d52b6b1c47dcbde1f697014c7385cc849a8025f0fcfd57c588bab3` |
| `build-release/bin/chibi_ctl` | `f031f0f506ab7c9159455070abf76b53f6f621de3152ba4a20e0b39dba395e31` |
| Sanitizer `build/test/language/test_language` | `da0466ecce37e88432eb0ae3c0cf1f442381d6b7fb13a6d0912cd16479a2547f` |
