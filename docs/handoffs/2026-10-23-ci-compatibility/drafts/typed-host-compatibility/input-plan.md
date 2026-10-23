# Pure compiler self-build input selection

The requested frozen typed state already implements the smallest coherent selector. No whole-directory copy remains in this path, so no further selector repair is needed at this revision. Preserve the existing shared Prepare helper and add focused coverage for the wrapper coexistence case.

Inspected read-only: `/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi`, HEAD `30cd287d1b86250d13b42c3716a94d747fa87a9b`, staged tree `0b5c20a7222a9d171bfcbe35570c184a78f4c8d1`. `git diff --cached --quiet <tree>` returned zero; focused working-tree comparison of the workflow/helper also returned zero. No tracked edit, staging, build, download, dispatch or compiler execution was performed. This file is the only output of this source-only task.

## Owning source and current behavior

- `.github/workflows/compiler-recovery.yml:74` calls `.github/scripts/compiler-self-build.ps1 -Mode Prepare`; line 77 calls Prove with the same proof root/platform and `-PinnedRuntime`.
- `.github/scripts/compiler-self-build.ps1:31` maps each manifest source path: `/sdk/core/...` goes below `packages/core`, other declared paths go below `tools/compiler`.
- Helper lines 44–76 require every declared file and all `.chibi` siblings in each selected package directory. Prepare performs that check against the repository at lines 103–104, copies the two bootstrap resources at 107–109 and runtime pins at 110, verifies the closure at 111, then copies only each declared source at 112–117.
- The native-source guard remains at helper lines 125–126. It still rejects `.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.m`, `.mm` anywhere in the resulting proof root.
- Prove checks the selected inventory/closure again at helper lines 180–182; lines 196–219 construct the compiler request from the declared paths and their actual UTF-8 texts. These compiler inputs do not require asset-bootstrap C wrappers or asset resource inventories.
- `src/toolchain/compiler_bootstrap.c:326` documents the same per-package completeness rule; lines 329–371 implement it. This explains why copying a subset of files from a selected package is incorrect even if a file is not named by an import.

The two generated files exist at `tools/compiler/asset_compiler/bootstrap/native64/asset_compiler_packaged.c` and `tools/compiler/asset_compiler/bootstrap/wasm32/asset_compiler_packaged.c` (162,306 bytes each). Both are outside the selected package set. `tools/compiler/asset_compiler/bootstrap/README.md:3` identifies them as target-specific prepared asset-service/resource-writer output; lines 27–32 describe that separate generator's inputs. They are not ordinary compiler self-build inputs.

## Recommended selector

Use `tools/compiler/bootstrap/bootstrap.json` as the authoritative inventory. Copy exactly:

1. The supplied checksum-pinned runtime executable under `chibi-compiler-host[.exe]`.
2. `tools/compiler/bootstrap/bootstrap.json`, `tools/compiler/bootstrap/compiler.chibip`, and `tools/compiler/runtime.json`.
3. Every manifest `sources[].path`, mapped by the helper's existing Source-File rule, after repository package-completeness checks. Do not discover extra packages recursively or select only sources whose bytes still match an older seed: ordinary compiler source changes are legitimate recovery inputs, recorded by Prove at lines 204–210 and promoted at 258–260.

At this exact snapshot that is **23 initial proof files**: 19 authored sources, three bootstrap/provenance resources, one runtime executable. The 19 source paths are:

```text
tools/compiler/package_compiler/Compile.chibi
tools/compiler/package_compiler/Body.chibi
tools/compiler/package_compiler/Steps.chibi
tools/compiler/package_compiler/Maps.chibi
tools/compiler/package_compiler/References.chibi
tools/compiler/package_compiler/Tree.chibi
tools/compiler/package_compiler/Number.chibi
tools/compiler/package_compiler/Behavior.chibi
tools/compiler/package_compiler/Bootstrap.chibi
tools/compiler/syntax_diagnostics/Diagnostics.chibi
packages/core/strings/Strings.chibi
tools/compiler/source_snapshot/Snapshot.chibi
tools/compiler/compiler_syntax/Syntax.chibi
tools/compiler/package_routes/Routes.chibi
tools/compiler/package_syntax/Extract.chibi
tools/compiler/package_graph/Graph.chibi
tools/compiler/package_paths/Paths.chibi
tools/compiler/lexical_binding/Binding.chibi
tools/compiler/prepared_writer/Writer.chibi
```

Static file reads confirmed all 19 manifest byte counts/SHA256 values match the current source. Every `.chibi` sibling in their 11 selected package directories is recorded, and every explicit project/core import in those files targets a selected package. The list above is a review specimen, not a second hardcoded inventory to implement.

Do not use `tools/compiler/asset_compiler/CompilerSources.txt:1` as the selector: its 18 entries omit `package_compiler/Bootstrap.chibi`, which the current manifest records at line 104. That file is a member of the selected `package_compiler` package and defines the compiled `BootstrapContract` at `tools/compiler/package_compiler/Bootstrap.chibi:7` (`[5, 2]`). An 18-file copy would violate package completeness and lose this contract entry. Do not turn that asset-generator list into another self-build source of truth.

No additional asset compiler `.chibi` package, generated C wrapper, target JSON, asset manifest, CompilerSources.txt, resource inventory, or retired KDL wrapper belongs in this proof inventory. Keep the native-source guard intact rather than excluding offending suffixes from that guard.

## Checks to prepare

These are recommendations; this task executed only the static reads described above.

- Add a focused Prepare-only regression in the existing tooling dispatcher, alongside `test/tools/compiler_bootstrap.c`. Use a small compiler/core fixture with a complete manifest and checksum-matching dummy closure/runtime bytes, plus native64 and wasm32 generated-C sentinels in an unrelated asset-bootstrap directory. Invoke only the shared script's Prepare mode; it copies/hashes the supplied runtime without executing it. Assert the exact selected output files and bytes, both sentinels absent, and the existing native-source gate unchanged. Whole-directory copying fails this specimen while the manifest selector succeeds.
- Add a selected-package membership specimen: an undeclared `Extra.chibi` sibling must refuse preparation; adding it to the manifest must copy it. Removing a declared compiler/core source must refuse preparation. This complements the existing public bootstrap membership/missing-file oracle at `test/tools/compiler_bootstrap.c:560`, rather than merely asserting script text.
- Cover `/sdk/core/strings/Strings.chibi` mapping and the new `package_compiler/Bootstrap.chibi` membership. A valid source-content edit should retain its selected path and appear with its actual bytes/hash in evidence; do not accidentally prohibit normal compiler recovery by requiring every source to retain the seed hash during selection.
- Before any runtime proof, statically compare the manifest closure size/digest, selected path set, package siblings and source/request inventory on the exact frozen candidate. Verify the proof root has the expected 23 initial files and zero native-source suffixes. The current source-only inspection has verified the manifest/package/source half; Prepare execution is still pending in the owning lane.
- Once an exact source commit and execution are authorized, use the existing compiler-only workflow on macOS ARM64, Linux X64 and Windows X64 and require the aggregate. Preserve helper lines 147–155 (cleared environment, empty PATH and unavailable CC/CXX), artifact/entry/input/output-artifact arguments at 235–236, independent compiled contract execution/checks at 242–252, stage promotion at 253–266, and stage2-consumes-stage1/fixed-point checks at 268–277. Preserve workflow cross-platform equality at `compiler-recovery.yml:123`–142. No native provisioning workflow or compiler build is needed to address the input-copy issue.
- Audit original resulting artifacts against the new helper schema: 23 initial input files, 19 source byte/hash records, raw request/target facts, both stage/contract invocations and cleared environments, exact runtime pins, promoted version/minimum/closure/source metadata, and all six nonempty stage binaries matching across platforms. The former 44-file inventory and `--bootstrap ... --recover` evidence expectations from the older main workflow must not be reused unchanged. Current seed input is 12,099,901 bytes / `03d7bced2fb9fae810d16faf14b934b6c51a271b6c494f8df02157dc4642497c`; this static observation is not a claim that a new hosted proof passed.

## Source binding hashes

```text
.github/workflows/compiler-recovery.yml
60d42ec076ed0e87bf86331eb8587a8762898f142793e4125107b0a74c59178d
.github/scripts/compiler-self-build.ps1
60af9ae7dcd63ed926d8530db70520c5731e8dbf47848fd785000d9f2259b764
tools/compiler/bootstrap/bootstrap.json
1328526b992a05385292a7d54b98908e91cf6871cee4a3da5725a99fb0938ad8
tools/compiler/bootstrap/compiler.chibip
03d7bced2fb9fae810d16faf14b934b6c51a271b6c494f8df02157dc4642497c
tools/compiler/runtime.json
36d9837ca1d77e0eea66ceb96d0783bcff3f34baecf4db383370cacbb1eccb09
```
