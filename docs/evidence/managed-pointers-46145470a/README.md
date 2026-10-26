# Managed pointers: raw receipts at 46145470a

These are the unedited logs behind #695's frozen source candidate, 46145470a.
That revision composes the accepted candidate 9ec9399c4 with main at
d913c0a98, which brings #705, #706 and #707. It also has the fixes listed
below. The commit that adds this directory changes nothing else. These logs
replace the receipts for 9ec9399c4, which remain in this branch's history.

Host: Linux x64, 4 CPUs, clang 18. `build` uses ASan/UBSan and `build-release`
is Release. Every command ran with `CC=clang CXX=clang++
TOOLS_C_COMPILER=clang LIBCLANG_ROOT=/usr/lib/llvm-18
BUILD_HOST=/nonexistent/chibi-build-host`, so no external build host was used.
Each focused log starts with its command, the revision and the number of
modified tracked files, and ends with its exit status.

## Revisions

| Commit | Change |
| --- | --- |
| 06de8d47f | Merges main at 922f2945e (#705, #706). See the first table below. |
| 5e23a4d91 | Merges main at d913c0a98 (#707). See the second table below. |
| fee9760a1 | #707's `program_version_33_vectors_take_version_34_names` counts its steps as `CHIBI_PROJECT_VERSION - 33`. A version 33 project now takes two steps: vector names, then ref-call markers. |
| ec8fd1ac1, 1d64db5fb | Regenerated bootstrap, then a clean manifest revision. |
| 3b2c08666 | Formats `Pointers.chibi` and `Steps.chibi` as `chibi-compile --format` writes them. The change is whitespace only. |
| 7c66eecc1, 46145470a | Regenerated bootstrap, then a clean manifest revision. |

06de8d47f does the following:

| Area | Change |
| --- | --- |
| Grammar | Keeps #706's grammar without angle forms, plus the pointer type forms. |
| Migration | Takes #706's angle-form step (step 32). The ref-call step moves to the next step. |
| Signature reader | Re-applies #695's reader changes over main's version. |
| Pointer path | Leaves `e.C = v` and `e.C.f = v` to #705's component writes for any entity handle (`self`, a handle name or a global's keyed handle). Before, the pointer assignment path took those writes. |

5e23a4d91 gives the versions their final numbers:

| Item | Owner |
| --- | --- |
| Project version 34 and step 33 | #707 |
| Project version 35 and step 34 | #695's ref-call step |

The ref-call step parses with the current grammar from version 33 on.
`valid_at` keeps #707's "version 34 kept version 33's grammar", now
extended to version 35. `chibi_migrate_parse` returns `script_parse` for
version 33 and later. The repository's 20 projects were migrated with
`build/bin/chibi_ctl --project-root <dir> --migrate`. Their sources already
carried the markers, so only their manifests changed.

**Manifest revisions.** `make bootstrap-artifact` records `git describe
--always --dirty`. To let stage zero rebuild the closure, its first run
temporarily changes `sdk/CMakeLists.txt` line 708 to `if(FALSE AND TARGET
chibi-build-host`, which disables the asset compiler host. That run records
`<commit>-dirty`, and the edit is reverted before the commit. A second run on
the clean tree recovers from the checked-in bootstrap and reproduces the same
closure, and only `revision` changes. At 46145470a the manifest records
`7c66eecc14188f4864c4e421d815d4a4b925ce3f`.

## CI chain (`chain/`, at 46145470a)

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

The chain first ran once on 1d64db5fb. There, `test-projects` failed
`repository_packages_are_canonical` (`before/compiler-canonical.log`), and
everything else passed. 3b2c08666 fixes that, and the whole chain ran again
on 46145470a. Only that second run's logs are kept here.

## Focused selectors (`focused/`, at 46145470a, 0 modified files)

`make test-build JOBS=4` came first (`focused/test-build.log`, incremental).
Each selector then ran as `python3 tools/test_policy.py tool <selector>` with
`NATIVE_LLVM_BIN` and `CHIBI_LLVM_BIN` set to `/usr/lib/llvm-18/bin`.
`test-language-all` runs only the `language` group, so this list covers the
`compiler` group.

| Selector | Result |
| --- | --- |
| `language-program` | 174 of 174 |
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
| `language-package-compiler-chibi-behaviors` | 1 of 1, 2,859 asserts |
| `language-package-compiler-chibi-actions` | 9 of 9, 4,315 asserts |
| `language-package-compiler-chibi-behaviors-rest` | 2 of 2, 5,742 asserts |
| `language-package-compiler-chibi-libraries` | 13 of 13, 16,207 asserts |
| `language-package-compiler-chibi-sharing` | 1 of 1, 139 asserts |
| `language-package-compiler-chibi-debug-free` | 1 of 1, 2,106 asserts |
| `language-package-compiler-chibi-native-arguments` | 1 of 1, 9,923 asserts |
| `language-package-compiler-chibi-native-arguments-oracle` | 1 of 1, 227 asserts |
| `language-package-compiler-chibi-scalar` | 3 of 3, 11,117 asserts |
| `language-package-compiler-chibi-imports` | 2 of 2, 11,415 asserts |
| `language-package-compiler-chibi-imports-compiled` | 2 of 2, 11,115 asserts |

Every selector exited 0. That covers the whole `compiler` group except
`language-package-compiler-chibi-bootstrap`, the fixed-point check. The
fixed-point check was not run, and no fixed-point result is claimed.

## Failures before each fix (`before/`)

These ran on the uncommitted merge of main at 922f2945e. That is HEAD
c25987b6e plus the merge's changes and the temporary `sdk/CMakeLists.txt`
edit, which is why the modified counts are about 120.

- `other-component-write.log` (120 modified) is #705's
  `package_compiler_compiles_the_other_behaviors` with no pointer-path guard.
  Stage one rejected `globals.Target.Spot.y = moved.x * 2`: "OtherComponent:6:9:
  field assignment needs a local record".
- `keyed-handle-rejection.log` (121 modified) is the same selector with a
  first guard. That guard excluded only owner-typed handles, so the pointer
  path still took `globals.Other.Spot.x = 1` for a handle of another type. The
  rejection then read "Unknown field 'Spot'" instead of main's "field
  assignment needs a local record". The final guard excludes every entity
  handle.
- `vector-migration-steps.log` is `language-program` at 5e23a4d91 (1 modified,
  the `sdk/CMakeLists.txt` edit). The #707 vector test expected one step from
  version 33 (`program.c (8806)`). fee9760a1 fixes the count.
- `compiler-canonical.log` is `make test-projects` at 1d64db5fb. There, the
  guard's continuation lines were not canonical (`chibi_cli.c (723)`).
  3b2c08666 fixes them.

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

`tools/compiler/bootstrap/compiler.chibip` at 46145470a is 17,237,526 bytes,
SHA-256 `0ffcbc63bede4461cca898cfb97efce04809e4b09a264ad6f80068c81d382a61`.
The other binaries are not checked in:

| File | Built at | SHA-256 |
| --- | --- | --- |
| Sanitizer `build/sdk/generated/assets/asset_compiler.chibip` (19,637,463 bytes) | 46145470a | `84b5b854eb5374e20acdb66ac441b2628b7c0dddddbf2b98da7309845a0d73ab` |
| Sanitizer `build/sdk/chibi-compiler-host` | 46145470a | `1072332e200b965ea6f7bf67f51c39a5bd1711f243877487737cf0a500a1d9fb` |
| Sanitizer `build/test/language/test_language` | 46145470a | `18c6424c016563c53a652220cf1de45ddb4beef2568c773a390733443d589db0` |
| Sanitizer `build/test/tools/test_tools` | 46145470a | `4a9ac36065784d3d59311f7b85e6f3f2fee97c9a041015bc5e8c7b5df6f5f1eb` |
| `build-release/sdk/chibi-compiler-host` | 46145470a | `62269fbee5770ff628e9563cab8f9368cbf4cf9eb3abe315368c3bcd1b8a8929` |
| `build-release/bin/chibi_ctl` | 46145470a | `2ab037021b2b1ded7fe8e47ed6172832b8421ee9f88cc86b28c7ac537f5a184a` |
