# Managed pointers: raw receipts at 3c9fdbcc6

These are the unedited logs behind the #695 load, invoke and recovery receipt.
They were not re-run for this handoff except `isolated-recovery.log`, which
loads the checked-in closure without compiling the compiler again (14 s).

Host: Linux x64, clang 18, ASan/UBSan for `build`, Release for
`build-release`. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.

## 807a50bdf (full chain)

`3c9fdbcc6` is `002973c8c` (documentation only) plus #688's `81eb475ce`
(test only) on top of this commit.

| Log | Command |
| --- | --- |
| `807a50bdf/test-build.log` | `make test-build JOBS=6` |
| `807a50bdf/release.log` | `make build-release BUILD_TARGET='chibi_ctl chibi chibi-compiler-host'` |
| `807a50bdf/recovery.log` | `make test-recovery` |
| `807a50bdf/bootstrap.log` | `python3 tools/test_policy.py tool compiler-bootstrap` |
| `807a50bdf/test-native.log` | `make test-native` |
| `807a50bdf/test-materials.log` | `make test-materials` |
| `807a50bdf/test-projects.log` | `make test-projects` |
| `807a50bdf/test-language-all.log` | `make test-language-all` |
| `807a50bdf/test-tools.log` | `make test-tools` |
| `807a50bdf/check-format.log` | `make check-format` |
| `807a50bdf/sdk.log` | `make package-sdk SDK_PREFIX=<temporary prefix>` |

## 3c9fdbcc6 (after the test-only merge)

| Log | Command |
| --- | --- |
| `3c9fdbcc6/test-build.log` | `make test-build JOBS=6` |
| `3c9fdbcc6/execution.log` | `python3 tools/test_policy.py tool language-execution` |
| `3c9fdbcc6/asset-host.log` | `python3 tools/test_policy.py tool language-asset-compiler-host` |
| `3c9fdbcc6/bootstrap.log` | `python3 tools/test_policy.py tool compiler-bootstrap` |
| `3c9fdbcc6/release.log` | `make build-release BUILD_TARGET='chibi_ctl chibi chibi-compiler-host'` |
| `3c9fdbcc6/check-format.log` | `make check-format` |
| `3c9fdbcc6/isolated-recovery.log` | see below |

The isolated recovery extracts `git archive HEAD tools/compiler packages/core`
into an empty directory `$S`, then runs, from `$S`:

```sh
env -i PATH=/usr/bin:/bin build-release/sdk/chibi-compiler-host \
  --bootstrap tools/compiler/bootstrap --compiler $S/tools/compiler \
  --core $S/packages/core --recover $S/recovered.chibip
cmp $S/recovered.chibip tools/compiler/bootstrap/compiler.chibip
```

The recorded revision reads `1109d8e…-dirty` because the manifest keeps the
revision of the seed it was regenerated from; its source entries match
3c9fdbcc6 byte for byte (`compiler-bootstrap`).

`detail/` holds 20 of the 29 per-step logs that the `make` wrappers name as
`.chibi/logs/<id>-<step>.log`; it is not complete. `tools/quiet.py` keeps only
the 20 most recent step logs, so these nine were gone before the handoff:

- `807a50bdf/test-build.log`, `807a50bdf/release.log` and
  `807a50bdf/recovery.log` name eight earlier `configure-native` and `build`
  logs (ids `1792900401…`, `1792900403…`, `1792900601…`, `1792900603…`,
  `1792900606…` twice, `1792900607…`, `1792900609…`). The wrapper logs keep
  each step's status and duration.
- `807a50bdf/test-tools.log` names `1792901598…-graph.log`. That line, and its
  `graph: FAILED`, are the output of `test/tools/test_policy.c`'s quiet-wrapper
  fixture, which fails on purpose in a temporary directory; the run passed
  172 of 172.

`SHA256SUMS` covers every log here.

The `program_borrowing_kernel_results_survive_collection` case and the
169-case `language-program` run at 27d62a8ff were reported in a #695 comment
only; no log of that run is checked in. Later heads carry their own receipts.

## Compiler artifact

`tools/compiler/bootstrap/compiler.chibip` at 3c9fdbcc6 is the downloadable
compiler: 14,224,585 bytes, SHA-256
`faf6bf0a28025ede5782350022ba9c12807697244f6a583813f88d0e405ef4e5`. The other
binaries were built from the same tree and are not checked in:

| File | SHA-256 |
| --- | --- |
| Release `sdk/generated/assets/asset_compiler.chibip` (16,601,165 bytes) | `9018a84a8d356f28ff42a6357c0c567e54e129da2b3b4b63f3e3fddcd77a0276` |
| `build-release/sdk/chibi-compiler-host` | `0fa42889f7d2ea36365e29bf636d97707ba6ae8fc3bf22683d52fd22d5ce19c3` |
| `build-release/bin/chibi_ctl` | `14bb3e31747f2a8eda331ebbade3491203f3622400c77fcb8101508c442de9a2` |
| Sanitizer `build/test/language/test_language` | `8c9a24a495ff52e10ec824b215a0b204965b0f5e320f5dc046ea6f7156825d45` |
