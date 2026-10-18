# Chibi self-hosting handoff — 2026-10-18

## Read this first

Continue `PLAN.md` in the **private** engine repository:
https://github.com/waldnercharles/chibi

The integration target is `codex/toolchain-cleanup-20261017` at
`c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb`, tree
`09d520fe2a5723226126b0879faaee390e87b7b9`. This is the last validated,
landed checkpoint (PR444). Main is `6ea2221c97d266c9bb028443cb67bd445fdb3476`;
main is an ancestor, not a substitute for the target branch.

The owner requested all current work be committed and pushed for handoff.
Implementation and validation stopped at that request. No PR was merged as
part of this handoff. Drafts below deliberately preserve incomplete work.
Do not infer successful tests from a pushed branch, an approved source review,
a synthetic helper check, or a missing GitHub required check.

Goal: an actual Chibi-authored compiler compiling its own complete sources,
then Chibi-driven installed/relocated game build and link without invoking
CMake, Clang or GCC, including hidden or renamed copies. Bootstrap may use
those tools. Component parity, process orchestration and native wrappers do
not satisfy that goal. **M5 and self-hosting are not complete.**

## Exact branches and stack

All engine branches are retained. Read `docs/handoff/2026-10-18/refs.json`
for machine-readable full commit/tree/base identities and final verification.

1. **Merged semantic checkpoint, PR444:**
   https://github.com/waldnercharles/chibi/pull/444
   Target `codex/toolchain-cleanup-20261017` = `c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb`.
   Retained feature `codex/m5-semantic-components-20261018` =
   `b57ac78c53c170139d8f807158c076f971a2202b`. Merge and tested trees match.
2. **Native unit adapter, PR443, draft:**
   https://github.com/waldnercharles/chibi/pull/443
   `codex/m5-unit-native-adapter-20261018` =
   `494691255b9ed22d3e7da02e20aa30e716226107`, tree
   `7f56b35bb34ed0254b7950309f7f591de418f386`.
   Normal parents: `06a5ae206b4daf2a5f58d66969faa008bf9c57f3` and target `c828177e…`.
   Source reviewed; new integrated tree unrun. Prior actual native tooling build
   failed. The diagnostic-only helper update is now committed.
3. **Transport boundary, PR445, draft:**
   https://github.com/waldnercharles/chibi/pull/445
   `codex/m5-compiler-transport-20261018` =
   `e165059d1c17ec7605d909ae3bbf727d9e267a80`, tree
   `8e5ea89dfe428ca7804b68af0c85ba35e5e74b5e`.
   Normal parents: `2f1fecfd104052d7707c21954db405321d6e3aaa` and target `c828177e…`.
   Ordinary configure/build passed; strict product-link audit failed; all runtime
   selectors skipped. The audit cause is still unknown. Diagnostic expansion
   was discussed but **no diagnostic code was implemented** before the freeze.
4. **Snapshot-to-reader composition, PR446, draft:**
   https://github.com/waldnercharles/chibi/pull/446
   `codex/m5-snapshot-reader-state-20261018` = `25be310766829b135097542f737cc56c617f59da`, tree `061957d9fe8902af94897ae80ef4ff01a0616954`.
   Checkpoint parent `70ff5bf54a1125a110be6a26a7e226a4b5d542ba` contains the
   original source stacked on transport `1338151c084c1a633f881153084bc83773df27fa`.
   Source behavior is unchanged by the handoff helper addition. Integrate the
   newer PR445/semantic changes before runtime validation. Helper/control are
   preserved proposals, not independently admitted or executed.
5. **Scalar lowering and managed results, PR447, draft:**
   https://github.com/waldnercharles/chibi/pull/447
   `codex/m5-scalar-lowering-20261018` =
   `5c27f2b00b4cd4c7385b06d118217c6b8abe8ff8`, tree
   `66cb94362a50fc3d4af5913a8941b9982d187580`.
   Parent `bdbb896c48aadd271ee63a6562447ccb95d7ffa1` was based on
   `ace559c9c294a450c95a3a730dcd6d9f1b5ce13c`. The complete scalar source now
   parses after two minimal grammar corrections. Full semantic/backend/runtime
   tests remain unrun; current target integration is unresolved.
6. **Real bounded authored compiler, draft PR449 (https://github.com/waldnercharles/chibi/pull/449):**
   `codex/m5-authored-compiler-handoff-20261018` = `0fba697e169e9484fbf33b349d2c49f9cd28cbb1`, tree `400ff80aafce376526ce17cabc0a074eec610450`.
   Based on target `c828177e…`. Twelve new source/test/contract files are saved
   at their intended paths; historical registration/PLAN hunks are preserved
   as patches, intentionally not applied before dependency integration.
   Needs PR445 → PR446 plus PR447. Read
   `docs/handoff/2026-10-18/authored-compiler/` on this branch.
7. **Compiler-cache closure regressions, draft PR448 (https://github.com/waldnercharles/chibi/pull/448):**
   `codex/m5-package-cache-closure-20261018` = `a461927a646f470a8a0cc94abe9d4fb147530f95`, tree `7ca06d504c86ea902297898c46159ad1a21cfff6`.
   Based on target `c828177e…`; independent of the transport/driver chain.
   Test source and private helper are saved. Source/route reviewed, no product
   runtime. Documentation/supervisor accounting comments still need refresh.
   Read `docs/handoff/2026-10-18/package-cache/` on that branch.

The handoff branch itself only records recovery material and proposals; it is
not a replacement integration branch. Do not merge all drafts blindly or reset
one branch to another. Use normal merges/cherry-picks with explicit conflict
resolution, then validate the resulting exact tree. Preserve branch history.

## What was actually demonstrated

- PR440 unit repair and PR442 type relations previously passed and landed.
- PR444 source `b57ac78c…` passed all five complete semantic selectors:
  union 3 cases, effects 4, records 5, units 3, types 4. Each retained its
  production 60-second supervisor and expected pico summary. Configure/build
  took 3s/15s; selector times were 7/1/1/3/<1s. Run:
  https://github.com/waldnercharles/chibi-studio/actions/runs/39140946564
  Status JSON SHA-256:
  `7d85449277f819cf77949ad6fb2fec52c49923473bbea8aef069f1ff86dea84a`.
- Native PR443's earlier source `06a5ae20…` passed tooling generation (41s),
  then tooling compilation failed after 170s. No native selector started:
  https://github.com/waldnercharles/chibi-studio/actions/runs/39139386112
  Status SHA-256:
  `68e2f90aad4d1e97ae5f7f8617e3a00dec77dfbbdd1e23b51cceb497ca22a0e6`.
  Root cause is unknown. The new helper has finite compile/link/generation
  categories, but has not yet been run on the latest native checkpoint.
- Transport PR445 source `e165059d…` configured in 3s and built the ordinary
  language/tools/compiler-host products in 35s. Its strict audit failed in 2s.
  All seven runtime steps were skipped, not failed executions or passes:
  https://github.com/waldnercharles/chibi-studio/actions/runs/39143405107
  Status SHA-256:
  `87139c7537378989bc691433dbfdba338546d9fa463ef71f55cdc7518251fd4e`.
  Status `identity_unverified` means those executions never began; checkout
  identity gates did pass. No raw hosted diagnostics were exported.
- Exact PR447 Lower.chibi originally failed at a missing union `|` separator
  and reserved `Frame.branch` declaration. The corrected entire 22,452-byte
  source passed 1 C/pico case / 3 assertions in 0.067s; separate GCC build
  0.801s. This proves syntax only. Candidate SHA-256:
  `1778dd60224e4edb46e87d4563e20e0625b170da933976c317f00340eecfd4fd`.
- The driver also fixes two missing union separators. Its complete 18-file,
  179,267-byte parser precheck was independently admitted but **NOT RUN**
  because the owner requested handoff. Substantive compiler review is unfinished;
  native bridge/test workload review and actual preparation are still pending.

Minimal status JSON copies are committed under
`docs/handoff/2026-10-18/evidence/`; hosted artifacts have seven-day retention,
so use these copies after expiry. No executable artifact or raw private log
is included.

## Important source and helper files

- Plan/contracts: `PLAN.md`, `docs/self-hosting-build.md`, `docs/testing.md`,
  `docs/toolchain-inventory.md`, and repository `AGENTS.md`.
- PR443: `src/language/semantic.c`, `src/language/unit_analysis.{c,h}`,
  owning SDK/test-tools CMake language activation, `cmake/tests/desktop_dependencies/languages/`,
  `.github/scripts/native-unit-validation.sh`, `.github/native-unit-dependencies.tsv`.
- PR445: `src/toolchain/compiler_{host,main,syntax}.c`, matching private headers,
  `tools/compiler/compiler_syntax/Syntax.chibi`, `tools/compiler/source_snapshot/Snapshot.chibi`,
  `sdk/execution.cmake`, `test/language/{compiler_artifact_chibi,package_snapshot_chibi}.c`,
  `test/tools/compiler_artifact.c`, `.github/scripts/transport-validation.sh`,
  `.github/scripts/transport-boundary-audit.py`.
- PR446: `tools/compiler/analysis_state/State.chibi`,
  `test/language/signature_reader_chibi.c`, source embedding/dispatcher/policy registrations,
  `.github/scripts/reader-state-validation.sh` and saved unarmed control.
- PR447: `tools/compiler/scalar_lowering/Lower.chibi`,
  `src/execution/scalar_lowering_plan.{c,h}`, `src/language/scalar_lowering_reference.h`,
  `src/language/lower.c`, `test/language/scalar_lowering_chibi.c`,
  `test/language/binding_benchmark.c` and CMake/dispatcher registrations.
- Driver: `tools/compiler/bounded_compiler/{Compile,Expression,Artifact}.chibi`;
  its checkpoint manifest enumerates the exact native bridge/test paths and
  unapplied registrations. It accepts one pure zero-argument int/bool function
  across 1–3 import-free files. Every file/declaration is checked before encoding.
- Cache: `test/language/package_imports.c`,
  `.github/scripts/package-cache-validation.sh`, saved unarmed control and counts.

The driver's current native capability only consumes an authored lowering plan,
finalizes a scalar callable and encodes the inner prepared module. Authored code
constructs the outer CHIBICLO envelope. This transitional native finalizer/inner
encoder is **not** an authored serializer or full self-host compiler. Tests would
use fresh host API instances after producer retirement, not separate processes.
Native analysis/preparation still bootstraps the compiler fixture. SLJIT/runtime,
native verification/optimization/finalization, serialization and build backends
remain dependencies. The existing Build.chibi path still invokes CMake and
transitional helpers. Named-tool independence has not been demonstrated.

## Environment and exact continuation commands

Keep the current setup unless the owner explicitly changes it. No saved cloud
coding environment or user desktop is authorized for this continuation. The
available scratch workspace has parser-precheck tools, including GCC, but no
complete authenticated checkout or full product-build setup;
source work uses the connected repository and the already-established hosted
runner. Do not treat a missing local checkout as permission to use another
computer, generate credentials, change billing, or enable disabled engine CI.

From an authorized authenticated checkout, start with read-only reconciliation:

```sh
git fetch origin
git switch codex/toolchain-cleanup-20261017
git rev-parse HEAD HEAD^{tree}
git show origin/codex/self-host-handoff-20261018:HANDOFF.md
```

Read exact branch files and inspect diffs before integrating. Never substitute
`main`, assume the old `9bf6e275` plan head, or rewrite published branches.

Bootstrap build requirements used by the hosted route: Ubuntu 24.04,
Clang/Clang++ 18, Python3, Git, make, binutils, and hash-pinned CMake/Ninja from
`.github/build-tools.txt`. Official Tree-sitter/CSS/HTML/SLJIT/Cute source commits
are pinned inside the helper; native dependencies use the TSV. Dependencies
must be acquired through the established authorized route. Configuration/build
are separately timed and excluded from test ceilings. Final installed builds
must eventually remove the named bootstrap-tool dependency.

The private helpers contain the complete previously reviewed acquisition,
configure, build, identity and test recipes. They expect the hosted GitHub
identity environment and should not be invoked blindly as standalone scripts.
For a correctly configured local build, replace the path placeholders and use
the existing full selector commands:

```sh
export CHIBI_LANGUAGE_BUILD_DIR="/absolute/path/to/language-build"
export CHIBI_TOOLS_BUILD_DIR="/absolute/path/to/tools-build"
export CHIBI_COMPILER_HOST="$CHIBI_TOOLS_BUILD_DIR/sdk/chibi-compiler-host"
python3 tools/test_policy.py tool language-package-snapshot-chibi
python3 tools/test_policy.py tool compiler-artifact
```

Transport's admitted integration also includes `language-union-members-chibi`,
`language-effect-analysis-chibi`, `language-record-members-chibi`,
`language-unit-analysis-chibi`, and `language-type-relations-chibi`.
Native's focused full selectors are `language-semantic`, `language-incremental`,
`unit-fixtures`, `language-unit-analysis-chibi`, and `desktop-dependencies`.
Cache uses complete `language-package-imports`. Reader's proposal adds
`language-signature-reader-chibi` and `language-snapshot-reader-chibi`. Do not invent spelling or bypass supervisor.

Every test command retains its unchanged 60-second production ceiling, including
fixture work/cleanup. No splitting a failed aggregate into subsets, raising
limits, or treating quarantined checks as passes. Run only the changed-behavior
checks actually required by current repository policy/review. Reuse unchanged
review/evidence; reserve broader checks for actual integration milestones.

## Public Studio runner boundary

Repository: https://github.com/waldnercharles/chibi-studio (public).
Temporary draft https://github.com/waldnercharles/chibi-studio/pull/81,
branch `codex/host-m5-unit-validation-20261018` at
`088d9e37f8f4c5f88cb5ec23e2599618c5f71bb2`, tree
`7755426eb19449a7326204d5395ffdef73ed49be`; base/main
`bee5b54e159cce44ee52ca9439be587d9a95f5d2`.
All runs are terminal at handoff. This is a test-only control: **do not merge or
deploy it**. Retain the branch. The workflow path is
`.github/workflows/toolchain-m5-unit-analysis.yml`; only one admitted pinned
workload should be active at a time. It uses existing private engine checkout
access, shallow checkout and `persist-credentials: false`. Do not add credentials
or broaden permissions. Existing ordinary Studio CI is excluded on this branch.

Only fixed status/timing, commit/tree identity, booleans and reviewed finite
failure categories may leave the runner. Never upload private engine source,
history, binaries, build metadata, unrestricted exception text or raw logs to
this public repository. No private source bundle is present there. Existing
private source-bundle infrastructure is separate; no usable artifact was assumed.
The denied browser ZIP download route remains unavailable.

## Ordered next work

1. Reconcile exact refs, draft stack and applicable holds. This is a short
   integration check, not a general PR-cleanup project. PR407 remains held;
   do not import unrelated PR420/439 or paused Studio work.
2. Finish the bounded fast-feedback step: run the already-admitted 18-file
   parser precheck from the driver checkpoint after verifying its frozen inputs.
   Keep its result distinct from semantic execution. Add only useful closed audit
   failure categories to diagnose PR445's existing failure in one measurement;
   the actual code for that change is not yet written. PR443's committed helper
   already includes broader safe build categories. Avoid an infrastructure project.
3. Resolve PR445's actual product-link audit failure without weakening the
   runtime-only frontend/public-writer boundary. Reuse unchanged evidence and run
   the necessary exact-head focused/integration checks. Land only when material
   failures, independent review, required tests and integration are all clear.
4. Integrate and validate PR446 on the corrected transport/semantic tree.
   Integrate PR447 preserving both transport section flags and scalar registration;
   establish real scalar/native/Wasm behavior, not just syntax. Native PR443 can
   progress independently; it is not a compiler-only transport dependency.
   Cache tests are an independent useful regression lane, not a driver prerequisite.
5. Integrate the driver checkpoint only after its real dependencies. Apply/reanchor
   the saved registration hunks, finish source/bridge/lifetime review, prepare the
   compiler closure and execute its finite complete test corpus. Expand actual
   authored compiler capability from this bounded working slice rather than
   treating unsupported-only acceptance scaffolding as progress.
6. Compile the complete compiler with itself (stage0→stage1→stage2), compare the
   required semantics/diagnostics/ABI/artifacts, and prove installed plus relocated
   game build/link without engine/compiler source trees (game sources remain present) with CMake/Clang/GCC genuinely absent. Explicitly
   document any remaining platform assembler/linker/runtime dependencies.

The saved `proposals/self-host-acceptance.patch` is an **unapplied, unreviewed,
unsupported-only** proposal based on `ace559c9…`. It returns a fixed nonzero
UNSUPPORTED result and contains no self-host execution proof. Preserve it for
its acceptance contract; implementing the real driver is the priority.

## Other work and recovery limits

Separate, already-pushed Outliner work is not part of this self-host stack:
- Engine `integration/outliner-m3-20261018`:
  `e6d4c6dde6551d97bc9348220ff431d9a3b5186f`.
- Studio `codex/live-outliner-20261018`:
  `4805c446ca8215431ffa726f5b2a2dc102a547ce`, with `docs/live-outliner-handoff.md`.
These are M3-based, exclude M4 and newer Studio71 fixes, and were not edited here.

This handoff accounts for the substantive source/proposals/evidence present in
the current self-host workspace and the listed published commits. It does not
claim recovery of every historical local session or unrelated unpublished work.
Immutable reference copies, downloaded bootstrap tools, generated binaries and
redundant synthetic logs are not duplicated in Git; their source identities and
relevant authored scripts/patches are preserved. No required new source is left
only in a scratch path. See `WORK-INVENTORY.md` for the checkpoint mapping.
