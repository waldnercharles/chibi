# Managed pointers: raw receipts at 9ec9399c4

These are the unedited logs behind #695's frozen source candidate, 9ec9399c4.
That revision is the composition of the reviewed pointer work with main at
0b4483241 (#407, #697, #702), plus the fixes listed below. The commit that adds
this directory changes nothing else. These logs replace the receipts for
d2f9a0acb, which remain in this branch's history.

Host: Linux x64, 4 CPUs, clang 18. `build` uses ASan/UBSan and `build-release`
is Release. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.
Each focused log starts with its command, the revision and the number of
modified tracked files, and ends with its exit status.

## Revisions

The composition is 9c1715148, "Merge main at 0b4483241". After it come:

| Commit | Change |
| --- | --- |
| 5bb6ec22a | The bootstrap compiles and reads compiler closures with `COMPILER_HOST_MAX_ARTIFACT_BYTES` (`compiler_bootstrap.c` closure compile, seed read and artifact read), and so does the stage-two self-compile test. The composed closure is about 17 MB. |
| 4b5814d8d | Regenerated bootstrap. |
| 7977ba5ad | #697's ordered `Append` sample spells `ref` at its reference call. |
| 9a5c2d0a2 | Callables reject labelled and typed arguments with main's `unsupported syntax '…'` message. |
| aa02a4f86, fb3c2bbf8 | Regenerated bootstrap, then a clean manifest revision. |
| 4c11037bd | A member of a dereferenced pointer reads the pointee's field, never the producing call's named output, matching stage zero. |
| d75b82075, 9ec9399c4 | Regenerated bootstrap, then a clean manifest revision. |

**Manifest revisions.** `make bootstrap-artifact` records `git describe
--always --dirty`. To let stage zero rebuild the closure, its first run
temporarily changes `sdk/CMakeLists.txt` line 707 to `if(FALSE AND TARGET
chibi-build-host`, which disables the asset compiler host. That run records
`<commit>-dirty`, and the edit is reverted before the commit. A second run on
the clean tree recovers from the checked-in bootstrap and reproduces the same
closure, and only `revision` changes. At 9ec9399c4 the manifest records
`d75b820755082ef45a67b8366cfab0005635a3c1`.

## CI chain (`chain/`, at fb3c2bbf8)

| Log | Command | Result |
| --- | --- | --- |
| `test-build.log` | `make test-build JOBS=4` | ok |
| `release.log` | `make build-release JOBS=4 BUILD_TARGET='chibi_ctl chibi chibi-compiler-host'` | ok |
| `test-native.log` | `make test-native` | ok |
| `test-materials.log` | `make test-materials` | ok |
| `test-projects.log` | `make test-projects` | ok |
| `test-language-all.log` | `make test-language-all NATIVE_LLVM_BIN=/usr/lib/llvm-18/bin` | ok |
| `test-tools.log` | `make test-tools` | ok |
| `check-format.log` | `make check-format` | ok |
| `test-recovery.log` | `make test-recovery` | ok |
| `sdk.log` | `make package-sdk SDK_PREFIX=<temporary prefix>` | ok |

The chain ran on fb3c2bbf8, before the review fix. Between fb3c2bbf8 and
9ec9399c4, the only changes are:

- the `Steps.chibi` guard
- two collections samples
- a paragraph in `docs/managed-pointers.md`
- the regenerated bootstrap

None of them changes C runtime source. The broad chain was not repeated for
that fix. The focused selectors below cover it at 9ec9399c4.

## Focused selectors (`focused/`, at 9ec9399c4, 0 modified files)

`make test-build JOBS=4` came first (`focused/test-build.log`, incremental).
Each selector then ran as `python3 tools/test_policy.py tool <selector>` with
`NATIVE_LLVM_BIN` and `CHIBI_LLVM_BIN` set to `/usr/lib/llvm-18/bin`.

| Selector | Result |
| --- | --- |
| `language-program` | 172 of 172 |
| `compiler-artifact` | 16 of 16 |
| `compiler-bootstrap` | 18 of 18 |
| `language-asset-compiler-host` | 11 of 11 |
| `language-operations` | 11 of 11 |
| `native-catalog` | 8 of 8 |
| `language-scalar-native` | 3 of 3, 24,607 asserts |
| `language-package-compiler-chibi` | 3 of 3, 17,298 asserts |
| `language-package-compiler-chibi-compiled` | 1 of 1, 12,201 asserts |
| `language-package-compiler-chibi-types` | 1 of 1, 5,017 asserts |
| `language-package-compiler-chibi-types-compiled` | 1 of 1, 4,417 asserts |
| `language-package-compiler-chibi-collections` | 1 of 1, 5,647 asserts |
| `language-package-compiler-chibi-collections-compiled` | 1 of 1, 5,132 asserts |
| `language-package-compiler-chibi-literals` | 1 of 1, 4,090 asserts |
| `language-package-compiler-chibi-behaviors` | 1 of 1, 2,583 asserts |
| `language-package-compiler-chibi-actions` | 9 of 9, 4,273 asserts |
| `language-package-compiler-chibi-behaviors-rest` | 2 of 2, 4,750 asserts |
| `language-package-compiler-chibi-libraries` | 13 of 13, 16,063 asserts |
| `language-package-compiler-chibi-sharing` | 1 of 1, 139 asserts |
| `language-package-compiler-chibi-debug-free` | 1 of 1, 2,106 asserts |
| `language-package-compiler-chibi-native-arguments` | 1 of 1, 9,923 asserts |
| `language-package-compiler-chibi-native-arguments-oracle` | 1 of 1, 227 asserts |
| `language-package-compiler-chibi-scalar` | 3 of 3, 11,117 asserts |
| `language-package-compiler-chibi-imports` | 2 of 2, 11,415 asserts |
| `language-package-compiler-chibi-imports-compiled` | 2 of 2, 11,115 asserts |
| `make check-format` | ok |

Every selector exited 0. That covers the whole `compiler` group except
`language-package-compiler-chibi-bootstrap`, the fixed-point check. The
fixed-point check was not run, and no fixed-point result is claimed.

## Failures before each fix (`before/`)

- `dereferenced-member-collections.log`: the collections selector with the new
  `managed pointer named result member` sample and without the `Steps.chibi`
  guard. Its two modified files are that sample and the temporary
  `sdk/CMakeLists.txt` edit described above. Stage one selected `get`'s `Item`
  output for `get(nodes, 0).item` and rejected the read: "named result
  differs from its declared type at 3:80".
- `stage-two-closure-bound.log`: the stage-two self-compile before 5bb6ec22a
  failed with "compiler byte result exceeds type, shape or storage limits".
- `bootstrap-artifact-bound-mutation.log`: the diff it records sets the
  artifact read back to `COMPILER_HOST_MAX_OUTPUT_BYTES`, rebuilds only
  `test_tools`, and runs it. The new
  `bootstrap_load_reads_closures_up_to_the_artifact_bound` case fails at
  `compiler_bootstrap.c (455)`.
- `libraries-receiver-sample.log`: the libraries selector on 4b5814d8d.
  `package_compiler_preserves_receiver_before_writing_item` failed because
  stage zero rejected the sample's unmarked reference call, so its syntax and
  analysis had diagnostics (`package_compiler_chibi.c (8032)`). 7977ba5ad
  fixes the sample.

Hosted run 40237588866 failed the scalar-facts case `named_argument` with
"Check gave -4", because the case expects `unsupported syntax
'named_argument'`. 9a5c2d0a2 fixes it, and that log is not kept here.

`SHA256SUMS` covers every log here. Wrapper logs keep each step's status and
duration. Their per-step `.chibi/logs` files are not kept, because
`tools/quiet.py` keeps only the 20 most recent.

## Not covered

- The `compiler-recovery` workflow and the hosted "Compiler self-build" jobs.
  They run the host that `tools/compiler/runtime.json` pins, which is the
  prepared 21 release, unchanged here. That host reads `compiler.chibip` with
  the 16 MiB bound and cannot load prepared 22, so they fail on every platform
  until a runtime built from this source is released. `make test-recovery`
  above recovers with the host built from the tree.
- The fixed-point selector, as above.
- GPU, Studio, Windows and macOS.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at 9ec9399c4 is 17,110,225 bytes,
SHA-256 `9bc36c6721efc543ea88831ff8bb8707cf27ca5f9ba0119e23f885b1437390ef`.
The other binaries are not checked in:

| File | Built at | SHA-256 |
| --- | --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (19,510,163 bytes) | 9ec9399c4 | `62a8714456ade2ab4636f8573ecfa2a8a63d01237d4bc877111fe757291475de` |
| Sanitizer `build/sdk/chibi-compiler-host` | 9ec9399c4 | `7828b0c41f3e72f7de0fc918b33b4cdea4d759952639655d7df7d05af1128f90` |
| Sanitizer `build/test/language/test_language` | 9ec9399c4 | `46ab9f3a9372e9701449430d2c827a6c3ee121b4aacaf6e5517f28fa10b78804` |
| Sanitizer `build/test/tools/test_tools` | 9ec9399c4 | `6237e2cd3a3c648a4257eccf3ef706b907c5bebe138b4dc468d83c0d3b1fab5c` |
| `build-release/sdk/chibi-compiler-host` | fb3c2bbf8 | `0984ed815a8df96d1358406aad83f2df1067ca369dd5cb57c8ec0eb0b538b561` |
| `build-release/bin/chibi_ctl` | fb3c2bbf8 | `1f300c2e14a9ceaac753d350e8f50a38cfc4896dc92ff1dcdd5a97cd0ccf5875` |
