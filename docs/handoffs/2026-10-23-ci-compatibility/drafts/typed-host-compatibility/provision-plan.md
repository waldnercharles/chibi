# Typed compiler host provisioning: independent source plan

Source-only, 2026-10-22. Inspecting `/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi` (T): HEAD `30cd287d1b86250d13b42c3716a94d747fa87a9b`, staged tree exactly `0b5c20a7222a9d171bfcbe35570c184a78f4c8d1`, no unstaged tracked changes observed. This staged combination needs a final reviewed immutable commit before hosted provisioning. No tracked edits/staging, native execution, builds/tests, downloads, dispatch or publication occurred. The existing `.2` release and pins remain preserved.

The previous Studio artifact audits are finished: script SHA256 `fc641a4ea493abc5175489970ec395ee1591b05c2b414a2c2b55f07c63e03c02`; default receipt `0d8975ab14f87bfecc1ac3336df14e0faf55e8f67680f68f0ee2309a2528de72`; paired receipt `5a05ace5d9971010b0e307ce475993d78861acd983fc42c609a9940fdae48b2e`. No further artifact controls were added.

## Findings

1. **The shared script omits an argument required by both hosts.** Its [stage and contract calls](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/.github/scripts/compiler-self-build.ps1:235) omit `--timeout-ms`; current [compiler_main.c:295](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/src/toolchain/compiler_main.c:295) returns usage without a positive value. Pinned `.2` source `5c28191b3ea53afa232c34e37b14fd9b01024aef` has the same requirement at lines 276–282. Target query returns before this check, so it can pass while stage one fails. Fixing only the future native host cannot repair the preserved `.2` consumer.

   Add explicit caller-owned positive `InvocationTimeoutMilliseconds`, proposed default `1800000` ms, and forward it to **every generic artifact invocation**, including compiled contract calls. This matches the existing [Makefile BUILD_TIMEOUT_MS](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/Makefile:31) operation default; GRAPH_TIMEOUT_MS is separately 3600000. Retain selected timeout, per-child duration and existing cumulative/configure/build timings. Each invocation has its own deadline; no total-proof elapsed admission rule or INT_MAX sentinel. Extend the existing parent child-process supervision/cleanup to own cancellation too.

   The [native bootstrap CLI](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/src/toolchain/compiler_main.c:287) rejects artifact-only timeout flags. Omit `--timeout-ms` there and supervise that child in the parent with the caller's operation setting. Do not append runtime flags indiscriminately to target-query/bootstrap commands.

2. **Generic self-reproduction bypasses native source recovery.** Current Prove calls `CompileClosure` through `--artifact --entry --output-artifact`, executes each output's `BootstrapContract`, promotes metadata and compares stage bytes. It does not call native `validated()/recover()` or `compiler_bootstrap_load`. This intentionally permits the old `.2` runtime's generic route. Its native `--bootstrap` still parses `bootstrap.kdl`; matching ABI 20/native signatures do not repair that source-policy dependency. Provision a new matching host and prove its actual KDL-free `--bootstrap/--recover` path separately.

3. **Provisioning receipts need ordinary build attribution and complete proof outputs.** [Runtime-Provenance](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/.github/scripts/compiler-self-build.ps1:85) uses only GITHUB_SHA for a new host, without checking the actual clean checkout/full tree; the seed workflow supplies no Platform. Its upload omits both `stage*-contract.*.txt` logs, and its aggregate compares only final compiler/hash files. Supply Platform, record/check clean source SHA/tree before and after, retain contract/native-route logs, and reuse normal CI's existing checks binding both stage files to evidence before cross-platform equality.

## Roots, flags and proof boundaries

The native SDK route is already appropriate. [sdk/CMakeLists.txt](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/sdk/CMakeLists.txt:37) resolves CHIBI_SDK_ROOT to T and forces compiler ON/native OFF/desktop OFF with `CHIBI_SDK_COMPILER_HOST_ONLY=ON`. It bypasses asset resources, fonts, model/image facades and full SDK packaging. [src/language/CMakeLists.txt](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/src/language/CMakeLists.txt:10) returns before the C semantic frontend. Preserve current support ownership, including `src/support/text_edit.c`; no SDK/CMake ownership change is needed.

[third_party/CMakeLists.txt](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/third_party/CMakeLists.txt:66) selects source-only SLJIT `a0020dd49a21020434ff838fc308626c48cd8f05` plus arm64-immediate-negation.patch and Tree-sitter `da6fe9beb4f7f67beb75914ca8e0d48ae48d6406`, then returns before desktop/prebuilt platform dependencies. Keep the existing hash-pinned CMake 4.4.3/Ninja 1.13.2 setup and record actual compiler/tool versions. Provisioning needs a C compiler; normal consumption does not.

Source-File correctly maps compiler-relative paths to `T/tools/compiler`, and `/sdk/core/strings/Strings.chibi` to `T/packages/core/strings/Strings.chibi`. All 19 current lengths/hashes match the manifest: 18 compiler sources including Bootstrap.chibi, plus core strings. Initial isolated inventory is 23 files: those 19, two bootstrap files, runtime.json and the host. Preserve order/package completeness and exclude generated asset snapshots. Do not substitute asset_compiler/CompilerSources.txt.

The twelve-argument request preserves virtual roots `/project` and `/sdk/core` and all six target facts. [compiler_host_compiler_target_json](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/src/toolchain/compiler_host.c:147) uses the same pure native catalog as loading. Keep lossless JsonNode comparison of layout, prepared ABI, instruction names, natives, ports and native identities/signature ABIs; ABI 20 alone is insufficient. No rich-schema/catalog expansion is needed.

[compiled_contract](/Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/src/toolchain/compiler_bootstrap.c:145) invokes `BootstrapContract` (`0x3e6062160494dc34`, `[]`, budget 10000), currently `[5,2]`. Matching-source native validation requires exact metadata/compiled contract equality; recovery must reproduce the seed bytes. Changed-source recovery instead enforces the recovered minimum and exact recovered self-parity. The API directly loads matching sources and recovers changed sources without writing; existing C fixtures own those API semantics.

## Minimal implementation sequence and reservations

1. **CI owner:** reserve `.github/scripts/compiler-self-build.ps1`, `.github/workflows/compiler-runtime-seed.yml`, `.github/workflows/compiler-recovery.yml` and the three-platform section of `docs/self-hosting-build.md`. Add the invocation setting and a separate proposed `-NativeRecovery` opt-in. Leave `.2` normal CI on generic execution without target-query/native-recovery flags until a fresh pin is available. Keep existing jobs, conditions and all-platform requirements.

2. **Manual seed workflow:** add matrix Platform strings matching normal CI, and pass explicit `-Repository $env:GITHUB_WORKSPACE` to Prepare/Prove. Move runtime-build to `$RUNNER_TEMP/compiler-runtime-build` before introducing strict before/after source checks: current `runtime-build/` is unignored. Keep proof/products outside source instead of expanding ignores. Record actual clean revision/tree plus script/workflow, dependency/build-tool and native compiler identities.

   The proposed native recipe, not executed here:

   ```powershell
   $repository = $env:GITHUB_WORKSPACE
   $runtimeBuild = Join-Path $env:RUNNER_TEMP 'compiler-runtime-build'
   cmake -S (Join-Path $repository 'sdk') -B $runtimeBuild -G Ninja `
     -DCMAKE_BUILD_TYPE=Release -DCHIBI_SDK_COMPILER_HOST_ONLY=ON `
     -DCHIBI_ASSET_COMPILER_RECOVERY=OFF -DCMAKE_C_COMPILER=clang
   cmake --build $runtimeBuild --target chibi-compiler-host --parallel 4
   ```

   Prepare must copy that exact `chibi-compiler-host[.exe]`. Prove uses Platform, QueryTarget, proposed NativeRecovery and explicit `InvocationTimeoutMilliseconds 1800000`. Check command exits as today. No full SDK installation or asset generation is required.

3. **Shared proof:** after generic stage equality, opt-in provisioning invokes the following through the same cleared-environment runner, under parent supervision:

   ```text
   chibi-compiler-host --bootstrap tools/compiler/bootstrap --compiler tools/compiler --core packages/core --recover native-recovered.chibip
   ```

   Bind output length/SHA256 to generic stage two and retain command/logs/elapsed/output identity. For this all-19-source matching snapshot the native CLI also requires exact checked-in seed equality; do not turn that into a universal old-seed equality requirement for changed-source recovery. Retain both stage manifests/contracts, target JSON, input records and runtime byte length/SHA256. Extend seed comparison using normal CI's existing both-stage/evidence checks plus this native result. Keep the newly built host's source provenance separate from the old runtime.json pin copied as an input.

4. **Compiler/typed owner:** retain runtime/bootstrap source, the 19 inputs/artifact/manifest and focused `compiler-bootstrap`/`bootstrap-recovery` evidence. The latter exports HEAD, so a run before the typed commit covers HEAD30cd, not staged0b5. Final native/API recovery evidence needs the actual committed candidate. No C host contract change, new probe executable or ABI bump is proposed here. SDK/Graph owners retain SDK/CMake/generation/dependency work; no overlapping edit is requested.

5. **Coordinator:** after final source validation and separate execution authorization, provision all three native platforms at the immutable candidate and require complete matrix/comparison success. Uploads do not publish a release. Publication and later `tools/compiler/runtime.json` changes are separate approved steps: fresh release name, actual source commit/tree/run, exact three asset filenames/lengths/hashes. Preserve `.2`; do not relabel its unchanged binaries as typed-native-compatible. No future identity can be supplied from this source-only assessment.

6. **Consumer transition:** enable QueryTarget only atomically with the verified fresh pin; `.2` has no such interface. Normal CI remains download-only with generic stages/contracts and cross-platform equality. NativeRecovery can remain a producer-only proof bound to the exact published host bytes; if the consumer is intended to claim direct native-route execution too, enable that opt-in in the same pin/consumer transition and retain its receipt. State the chosen scope. Update docs' stale `--timeout-ms REMAINING` example and old-host description accordingly. `docs/package-compiler.md` has no seed/pin recipe needing a change.

## Consolidated PLAN.md review

The consolidated plan's per-call 1800000 setting, parent-supervised native route, source ownership, six-fact target comparison and delayed target-query enablement are correct. Correct lifecycle step 6's **“pin-only descendant”** wording: enabling QueryTarget changes the workflow too, so this is a **pin plus consumer enablement** descendant. State Platform, external runtime build directory, both contract-log uploads and both-stage aggregate binding explicitly. Its native output==generic stage bytes requirement is correct with the matching-vs-changed-source distinction above. Producer-only native proof versus optional direct no-C consumer native proof should remain explicit.

Frozen source identities were read and agree with the owning receipt: shared script `60af9ae7dcd63ed926d8530db70520c5731e8dbf47848fd785000d9f2259b764`; seed workflow `5eb2bb4c04c536a6982ed831e0f0a695d639b2aecf02ee098659d8818e35372e`; normal workflow `60d42ec076ed0e87bf86331eb8587a8762898f142793e4125107b0a74c59178d`. Bootstrap artifact is 12,099,901 bytes/SHA256 `03d7bced2fb9fae810d16faf14b934b6c51a271b6c494f8df02157dc4642497c`; raw manifest SHA256 `1328526b992a05385292a7d54b98908e91cf6871cee4a3da5725a99fb0938ad8`. Its explanatory revision string is not future runtime build provenance. These observations and the proposed sequence provide no new runtime compatibility pass or release authorization.
