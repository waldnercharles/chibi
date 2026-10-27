# Managed pointers: raw receipts at 4972ddeed

These are the unedited logs behind #695's source candidate, 4972ddeed.
That revision composes the accepted candidate 46145470a with main at
80d533cf0. Main there carries #708's header-type search, #712 and #713
(scalar records and unions), #714's 32 MiB bootstrap artifact bound,
#715 to #718, #720 and the squash merge of #693 (numeric names, project 35).
The commit that adds this directory changes nothing else. These logs replace
the receipts for 46145470a, which remain in this branch's history.

Host: Linux x64, 4 CPUs, clang 18. `build` uses ASan/UBSan and `build-release`
is Release. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.
Each focused log starts with its command, the revision and the number of
modified tracked files, and ends with its exit status.

## Revisions

| Commit | Change |
| --- | --- |
| 57dc1a3bd | Merges #693 at 49e63bcd8. See the first table below. |
| a56c33916 | Merges main at 19655e9fb. See the second table below. |
| 7488c6280 | Migrates the repository's 20 projects to version 36 with `build/bin/chibi_ctl --project-root <dir> --migrate`. Their sources already carried #693's names and the ref markers, so only their manifests changed. |
| b6a9c225c | Formats the lines the two merges resolved (`catalog.c`, `integer_semantics.c`, `main.c`, `semantic.c`). |
| 316be62b9 | Regenerated bootstrap, recorded as `b6a9c225c-dirty`. |
| a1284a1e2 | Removes the second copy of `asset_source_keeps_the_16_mib_source_bound`. #714 added the same case this branch carried, and the merge kept both definitions. |
| e91479eb5 | Records the clean revision a1284a1e2 in `bootstrap.json`. The closure is unchanged. |
| 1f7747fae | #693's cross-file case `generic_version_34_quantity_type_arguments_migrate` counts its steps as `CHIBI_PROJECT_VERSION - 34` and reads the current version back. A version 34 project now takes two steps: number names, then ref-call markers. |
| 38b4f5dfb | Merges #693 at 8bbfd2dcc: its main merge, bootstrap and prepared21 pin. No compiler source changes here, so this branch keeps its own bootstrap. |
| 244bf2fcf | Merges #693's final head 623ac6449, which adds #717 and #718. No compiler source changes. |
| 4972ddeed | Merges main at 80d533cf0. Main's tree is 623ac6449's plus #720. The squash replays #693's hunks, and this branch's resolutions stand for them. The delta to main equals the delta to 623ac6449. |

57dc1a3bd does the following:

| Area | Change |
| --- | --- |
| Versions | #693 keeps project 35 for its number names (step 34). The ref-call step moves to step 35, and the project version to 36. |
| Names | #695's own lines take #693's names: `u64`, `i64`, `f64` and `number`, for example in `Pointers.chibi`, `Reader.chibi` and `Members.chibi`, and in the pointer samples. |
| Tests | Two #693 cases expected one step and version 35. They now count `CHIBI_PROJECT_VERSION - 34` and read the current version back. |

a56c33916 does the following:

| Area | Change |
| --- | --- |
| Bootstrap bound | Takes #714's `compiler_host.h` comment and its `compiler_artifact.c` and `compiler_bootstrap.c` cases, which replace this branch's own copies of the 32 MiB bound. |
| Lookup | `prepared_record.c` keeps main's charged binary search and equality-result reuse (#708). The remaining delta to main is #695's pointer type and hash-work changes. `record_prepared_finds_header_types_by_name` is present and passes in `language-record-prepared`. |
| Names | Main's new casts and samples in #712 and #713 take #693's names: `i64` in `scalar_llvm/Emit.chibi` and in `llvm_native/Emit.chibi`'s `FieldType`, `u64` in `Scalar.chibi`, and `number`, `f64`, `u8` and `i64` in the scalar record and union samples. Comments and LLVM IR strings such as `{ i32, float }` are unchanged. #693's own main merge renamed the same lines the same way. |

**Manifest revisions.** `make bootstrap-artifact` records `git describe
--always --dirty`. To let stage zero rebuild the closure, its first run
temporarily changes `sdk/CMakeLists.txt` to `if(FALSE AND TARGET
chibi-build-host`, which disables the asset compiler host. That run records
`<commit>-dirty`, and the edit is reverted before the commit. A second run on
the clean tree recovers from the checked-in bootstrap and reproduces the same
closure, and only `revision` changes. At 4972ddeed the manifest records
`a1284a1e2df4ecd04295f2f0321f3c36f76bbaea`. No compiler source changed after
that, and `compiler-bootstrap` checks that the recorded sources match the tree.

## CI chain (`chain/`, at 4972ddeed)

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

The chain first ran on e91479eb5. There, `test-language-all` failed the
generic step count (`before/generic-step-count.log`), and everything else
passed. A second full run on 38b4f5dfb passed but was stopped during its
focused selectors when #693 landed. Only the run on 4972ddeed is kept here.

## Focused selectors (`focused/`, at 4972ddeed, 0 modified files)

`make test-build JOBS=4` came first (`focused/test-build.log`, incremental).
Each selector then ran as `python3 tools/test_policy.py tool <selector>` with
`CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin`. `test-language-all` runs only the
`language` group, so this list covers the `compiler` group, plus the
selectors this composition touches that run in no group.

| Selector | Result |
| --- | --- |
| `language-program` | 182 of 182, 12,699 asserts |
| `compiler-artifact` | 17 of 17, 29,522 asserts |
| `compiler-bootstrap` | 18 of 18, 202 asserts |
| `language-asset-compiler-host` | 11 of 11, 99 asserts |
| `language-operations` | 11 of 11, 264 asserts |
| `language-record-prepared` | 14 of 14, 3,633 asserts |
| `language-migration34` | 12 of 12, 271 asserts |
| `language-migration34-generic-repro` | 4 of 4, 124 asserts |
| `language-named-binding-chibi` | 3 of 3, 54,246 asserts |
| `language-signature-reader-chibi` | 4 of 4, 201,417 asserts |
| `native-catalog` | 8 of 8, 386 asserts |
| `language-scalar-native` | 4 of 4, 111,389 asserts |
| `language-package-compiler-chibi` | 3 of 3, 23,242 asserts |
| `language-package-compiler-chibi-compiled` | 1 of 1, 17,467 asserts |
| `language-package-compiler-chibi-types` | 1 of 1, 5,616 asserts |
| `language-package-compiler-chibi-types-compiled` | 1 of 1, 5,144 asserts |
| `language-package-compiler-chibi-collections` | 1 of 1, 6,412 asserts |
| `language-package-compiler-chibi-collections-compiled` | 1 of 1, 5,998 asserts |
| `language-package-compiler-chibi-literals` | 1 of 1, 4,090 asserts |
| `language-package-compiler-chibi-behaviors` | 1 of 1, 2,859 asserts |
| `language-package-compiler-chibi-actions` | 10 of 10, 4,628 asserts |
| `language-package-compiler-chibi-behaviors-rest` | 2 of 2, 5,742 asserts |
| `language-package-compiler-chibi-libraries` | 13 of 13, 17,185 asserts |
| `language-package-compiler-chibi-sharing` | 1 of 1, 139 asserts |
| `language-package-compiler-chibi-debug-free` | 1 of 1, 2,458 asserts |
| `language-package-compiler-chibi-native-arguments` | 1 of 1, 12,094 asserts |
| `language-package-compiler-chibi-native-arguments-oracle` | 2 of 2, 200 asserts |
| `language-package-compiler-chibi-scalar` | 4 of 4, 38,137 asserts |
| `language-package-compiler-chibi-imports` | 2 of 2, 13,323 asserts |
| `language-package-compiler-chibi-imports-compiled` | 2 of 2, 13,098 asserts |

Every selector exited 0. That covers the whole `compiler` group except
`language-package-compiler-chibi-bootstrap`, the fixed-point check. The
fixed-point check was not run, and no fixed-point result is claimed.

## Failures before each fix (`before/`)

- `duplicate-test-case.log` is the clean `make bootstrap-artifact` run at
  316be62b9. Its Release build stopped on "redefinition of
  'asset_source_keeps_the_16_mib_source_bound'" (`asset_compiler_host.c:488`).
  a1284a1e2 removes the second copy.
- `generic-step-count.log` is `make test-language-all` at e91479eb5. #693's
  `generic.c (611)` expected one step from version 34. 1f7747fae fixes the
  count.

`SHA256SUMS` covers every log here. Wrapper logs keep each step's status and
duration. Their per-step `.chibi/logs` files are not kept, because
`tools/quiet.py` keeps only the 20 most recent.

## Not covered

- The `compiler-recovery` workflow and the hosted "Compiler self-build" jobs.
  They run the host that `tools/compiler/runtime.json` pins, which is main's
  prepared 21 release (compiler-runtime-prepared21-19655e9fb), unchanged here.
  That host stops with "Prepared module core:strings: prepared target ABI
  differs", so these jobs fail on every platform until a prepared 22 runtime
  built from this source is released. `make test-recovery` above recovers with
  the host built from the tree.
- The fixed-point selector, as above.
- GPU, Studio, Windows and macOS.

## Artifacts

`tools/compiler/bootstrap/compiler.chibip` at 4972ddeed is 18,764,380 bytes,
SHA-256 `3c68323355191452c0c091b9778730baf5efbc8d4102632fe67626766b9e57ab`.
That is within #714's 32 MiB artifact bound and above the 16 MiB output bound.
The other binaries are not checked in:

| File | Built at | SHA-256 |
| --- | --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (21,164,648 bytes) | 4972ddeed | `68563e29399d2d6888b3429225400b0fdfaf741b8966ca6c9584219b3a1b4576` |
| Sanitizer `build/sdk/chibi-compiler-host` | 4972ddeed | `65a8bbcfa2dd7f8c53dbbef4db36a58c20f8d412fcf2d80ac2239040866a953b` |
| Sanitizer `build/test/language/test_language` | 4972ddeed | `c1b1ea005a99361548c97fbe3fdb9f77beb182c6f823a0e58c0d1e6bef449661` |
| Sanitizer `build/test/tools/test_tools` | 4972ddeed | `033cb5245da4816a19e1886a53c7ae9b5a363907430c6883b331ae6ddda3e7bb` |
| `build-release/sdk/chibi-compiler-host` | 4972ddeed | `ca641d34f0a9494ce076f759f17039d6f5f20c131aaba145862767bc071276c9` |
| `build-release/bin/chibi_ctl` | 4972ddeed | `fe58b63772e19dae9cf5c1fdb781ebd9db688f3dcf9387268466caf8ce041864` |
