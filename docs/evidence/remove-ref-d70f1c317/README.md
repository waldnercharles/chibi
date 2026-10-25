# Remove authored ref: raw receipts at d70f1c317

These are the unedited logs of the CI check chain on d70f1c317, run once on that
clean tree. d70f1c317 is this branch's round 2 fixes (848517630) merged with the
managed-pointer branch at 76e6e7cbb, which holds #688's head, main through #699,
pointer compound float widening and the scoped 32 MiB artifact bound, followed
by a compiler bootstrap regenerated from that merge. The commit that adds this
directory changes nothing else. They replace the 999beda46 receipts, which are
in this branch's history. None of the frozen
#698 review results at 2a14a3abb are carried over; this is its own run.

Host: Linux x64, 4 CPUs, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

| Log | Command | Result |
| --- | --- | --- |
| `test-build.log` | `make test-build JOBS=4` (254 s, asset compiler rebuilt) | ok |
| `release.log` | `make build-release JOBS=4 BUILD_TARGET='chibi_ctl chibi chibi-compiler-host'` | ok |
| `test-native.log` | `make test-native` | ok |
| `test-materials.log` | `make test-materials` | ok |
| `test-projects.log` | `make test-projects` | ok |
| `test-language-all.log` | `make test-language-all` | ok |
| `test-tools.log` | `make test-tools` | ok |
| `check-format.log` | `make check-format` | ok |
| `test-recovery.log` | `make test-recovery` | ok |
| `sdk.log` | `make package-sdk SDK_PREFIX=<temporary prefix>` | ok |
| `language-program.log` | `python3 tools/test_policy.py tool language-program` | 174 of 174 |

`language-program.log` was run after the chain, against the same build. It
names the round 2 regressions `program_version_33_ref_becomes_pointers`,
`program_migration_retry_resumes_after_finished_steps` and
`program_nested_forwarding_keeps_pointer_scoped`. The package compiler samples
`scoped pointer relays` and `scoped pointer spans` run in
`test-language-all.log`, which prints sample failures only.

`detail/` holds 20 of the 24 per-step logs that the wrappers name as
`.chibi/logs/<id>-<step>.log`. `tools/quiet.py` keeps only the 20 most recent,
so these four were gone when the chain finished:

- `test-build.log`: `1792925435912364923-2129-configure-native.log` and
  `1792925438386860336-2160-build.log`.
- `release.log`: `1792925692116056760-2329-configure-native.log`.
- `test-tools.log`: `1792926876810891514-10973-graph.log`, which is the
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
- Imported forwarding is checked by source inspection only, as in the review.
- GPU, Studio, Windows, macOS and the LLVM native path.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at d70f1c317 is the downloadable
compiler: 15,516,993 bytes, SHA-256
`f22ee6047f5ec7fe5b6e089b3eed0ace2ca6c3b29814c7ce8d58b6d26d18c3b0`. The other
binaries were built from the same tree and are not checked in:

| File | SHA-256 |
| --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (17,887,158 bytes) | `7ac6e47e27861430f755fc6d8085164fc5b9205959d7f73ff3b73eba83399dd3` |
| `build-release/sdk/chibi-compiler-host` | `da16b1e9f39763727b96f375a0f77d6b937c234e752587b3014fe62fb67ef004` |
| `build-release/bin/chibi_ctl` | `db1d95df87e3936a356c08e12d995f45785a6edf33323692cf84fcf1c65febc4` |
| Sanitizer `build/test/language/test_language` | `2c485b46f58a56388963fadcf26a4159e0ab56fa3361abf199b53f7ca95d4865` |
