# Stage 0 baseline evidence, 15 October 2026

This is a verified source/registry and evidence baseline for B00–B02. It does not
establish a completed Studio conversion, a passing three-way oracle or execution
results for the listed selectors. No runtime workload was executed for this report.

## Result and readiness

- Engine source is locally recovered at
  `64183a1c4ba5b367bf35604ddae45fa616fd3bd0`, tree
  `754c4ce0d63195bd77842f0dc5e0eaa3c7b65782`.
- Studio source identity is
  `78cb4d4c47242db9f7af2b9573653d9756050e99`, tree
  `a91bebf8c9226c4ef974e8fd061db34082ac0755`. Its complete Git tree metadata
  matches all 867 supplied inventory paths, blob identities and byte counts.
  The complete clean checkout is now recovered; all 867 local blob contents
  match, and `git fsck --full` passes. SDK/dependency availability and a
  successful Studio build are separate, still-unmet gates.
- Build declarations reconcile 151 library translation units plus four host
  translation units to exactly the inventory's 155 product sources. There are
  62 product headers. The Zep `.cpp` pass-through is one of those 155, not an
  additional source. This verifies build membership, not function ownership.
- The live registry contains **44 panel sources and 123 commands**. Commands
  cover every enum member except the `STUDIO_COMMAND_COUNT` sentinel, without
  duplicate IDs. The manifests retain exact callbacks, ordering, shortcut
  defaults, contexts, repeat/index/phase flags and immutable source links.
- Studio's dispatcher contains 26 acceptance selectors. The engine lists
  eleven relevant bounded routes; fifteen have no configured engine route at this
  snapshot. One listed route has an exact-source frame mismatch described below.
- B00 remains blocked on executable original/candidate SDK pairs and health
  evidence. B01 has complete path/build/registry accounting, but not the full
  function-body/ownership audit. B02 has a concrete existing-route map;
  missing or mismatched acceptance remains a gate. None is marked complete.

The seven already-owned foundations and B06 native-reload audit remain with
their existing owners. This report changes no engine or Studio implementation,
test execution, project version, generated assets, or runtime behavior.

## Files and evidence meanings

| File | What it establishes |
| --- | --- |
| `oracle.json` | Exact source trees, distinct SDK legs, published package digest metadata, asset roots, local toolchain and missing execution prerequisites |
| `reconciliation.json` | All 867 path/blob/size matches, source/build counts and complete registry counts |
| `panel-sources.csv` | All 44 registrations in synchronization order; exact add/sync/action symbols and mount gate |
| `commands.csv` | All 123 command identities and defaults, including macro-expanded declarations and in-memory enum ordinal |
| `ownership.md` | Source-backed shared/instance state, command/input dispatch, document/history and asynchronous ownership; explicit audit gaps |
| [the archived selectors snapshot](https://github.com/waldnercharles/chibi/blob/77717143059b344bb41123104b5012396ca63eb8/docs/studio-migration-roadmap/baseline/selectors.csv) | All 26 native Studio selectors and 25 relevant engine prerequisites; historical route status versus execution and excluded coverage |
| `source-provenance.csv` | Exact fetched/source-audited Git blobs, byte counts, SHA-256 and immutable URLs |
| `engine-fonts.csv` | All 28 files in the candidate engine font tree, including licenses/readmes; source byte fingerprints, not installed-font or rendered-output proof |
| `toolchain-files.json` | Local compiler/build-tool executable identities; not a complete SDK/dependency closure |

`source-provenance.csv` covers 26 deliberately selected Studio files and nine
engine files. Registry/header inspection does not mean every body in those
files was independently audited. The supplied 867-path inventory remains the
full path ledger, recorded at roadmap candidate
`1117f2de55bb4ceaa8f3b16a0e0fb7dee7dc4881` with its path and SHA-256 in
`reconciliation.json`. Its proposed ownership/dispositions do not grant native
exceptions or establish function-level audit completion.

## Three distinct SDK baselines

### 1. Original native Studio evidence

[CI run 38173785891, job 114571745914](https://github.com/waldnercharles/chibi-studio/actions/runs/38173785891/job/114571745914)
on 11 October checked out the exact Studio commit above and engine
`c159b6cd67d1623f3a7d5cc3a434b04fb849e808`. Its log records the downloaded native
SDK checksum check passing. The
[engine release](https://github.com/waldnercharles/chibi/releases/tag/engine-c159b6cd67d1623f3a7d5cc3a434b04fb849e808)
metadata observed on 15 October identifies `chibi-sdk-linux-x64.tar.gz`, asset
`631770591`, as 42,299,930
bytes with SHA-256
`d97794738ebc19cd5bfb97a28c726db4c52e21676be6a7902c11cbe7972e92ec`.

That job passed the native build, two hidden starter frames, two hidden frames
of Studio's own project, installed starter startup and game export. These are
historical results at that pair. They do not establish the complete acceptance
suite, exact pixels, full project validation or current-engine compatibility.
The SDK bytes/package inventory have not been independently downloaded and
verified locally in this task. A release tag alone is not an immutable artifact:
preserve and verify the asset ID, size and digest before reuse.

### 2. Candidate engine with the same native Studio

The candidate engine is the recovered `64183a1c` snapshot. No installed SDK
manifest, package build identity, Studio build or acceptance evidence exists
for this leg in this report. The release lookup for
`engine-64183a1c4ba5b367bf35604ddae45fa616fd3bd0` returned 404 on 15 October.
Building an installed SDK from the exact engine is a valid later route; selecting
a different available release is not equivalent.

Record SDK package/configuration/inventory hashes, actual compiler flags and
dependency identities before comparing with leg 1. Any adapter needed to build
unchanged native Studio is separately reviewed drift. Source-only engine passes
do not establish this consumer leg.

### 3. Authored Studio with the candidate SDK

Not implemented. Compare it with leg 2 only after leg 1 versus leg 2 engine
drift is explained and accepted. The same fixture, actions, environment and
single side-effect owner must be used across all three legs.

### Published browser editor is a separate pair

[The actual 14 October editor build](https://github.com/waldnercharles/chibi-studio/actions/runs/38524309666/job/115720961424)
used Studio `78cb4d4c` and engine `37718e5eb58a468fb4503ca956dad5aa2a426ec1`,
Emscripten 6.0.10 and Node 24.19.0. Its
[release web SDK](https://github.com/waldnercharles/chibi/releases/tag/engine-37718e5eb58a468fb4503ca956dad5aa2a426ec1)
is asset `638770630`, 15,091,029 bytes, SHA-256
`f3bc60412b41909a426209afe744f569a37b79d2f244701190fd7df2851fb932`.
This is build/deployment evidence, not browser interaction acceptance.

[The 15 October successful run](https://github.com/waldnercharles/chibi-studio/actions/runs/38694580824)
ran only version selection; its editor and deploy jobs were skipped. A green
publish run cannot establish a new engine/Studio build. The checked-in
[publisher](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/.github/workflows/publish.yml)
selects the newest complete engine release, while
[native CI](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/.github/workflows/ci.yml)
checks out engine `main`. Neither policy supplies a fixed cross-repository oracle
without recording the resolved pair and exact artifacts.

## Health and environment gates

1. The historical font-variable/fallback preparation report is not a verified
   current failure. Original CI startup above passed and logged font-face loads,
   but full current-pair project validation has not run. Reproduce the exact
   preparation path with the selected SDK/resources, then resolve the defect or
   obtain explicit oracle disposition. Do not silently correct a golden fixture.
2. Local tooling is Debian 13.6/x86-64, native clang 19.1.7, CMake 4.4.3,
   Ninja 1.13.2, Python 3.12.14, clang-format 22.1.1, Emscripten 6.0.10 and Node
   24.19.0. Historical CI used Ubuntu 24.04 and Studio's clang-18 environment.
   Version similarity is not toolchain parity; local and historical measurements
   must remain separately identified.
3. `python3 tools/agent.py doctor` reports no device nodes, no hardware device,
   Vulkan instance result `-9`, status `unavailable`. No renderer was run. This
   does not authorize software-rendering substitution or a platform pixel claim.
4. Source asset trees and candidate font hashes are pinned in the manifests.
   Installed resources, runtime font selection, window/DPI, driver, timing/seed,
   power mode, background load and deterministic pixel/action goldens remain
   unrecorded. There are no performance measurements or numeric budgets here.

## Route defects and missing evidence

### Collider selector mismatch: reconcile the source and wrapper

The engine's
[exact acceptance tuple](https://github.com/waldnercharles/chibi/blob/64183a1c4ba5b367bf35604ddae45fa616fd3bd0/tools/agent.py#L86-L98)
requires 26 frames and the literal success string ending in `frames=26` for
`--studio-scene-collider3d`. Exact Studio source
[defines 28 frames](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/scene_collider3d.h#L41)
and prints that constant at completion. The README also says 28.

This is a source-proven incompatibility in the baseline, not an observed runtime
failure. Review the actual workload and its bounds independently; then either
update the wrapper to match the source or select a separately pinned earlier
oracle. Changing a frame number or expected success string alone does not prove
runtime coverage.

### Documentation is not execution evidence

`--code-keys` has no configured engine route here. Its
[source constant](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/main.c#L816)
is 38 frames, while its README description says 21. Use actual source bounds
for future review. A short/no-GPU selector such as `--scene-cursors` still needs
its owning executable, configuration and an actual result.

The ten ordinary Studio agent routes require absolute source/build/evidence
paths, matching binary and extraction hashes, build/configuration evidence,
acceptance-source hashes, compile-commands identity, SDK config identity, the
three-file disposable fixture and bounded resources. Those missing generated
records must be recreated from actual builds, never filled in as passes.
`studio-panel-idle` is a separate fixed workload and has its own setup; its
results do not establish a full acceptance-suite pass.

Read [the archived selectors snapshot](https://github.com/waldnercharles/chibi/blob/77717143059b344bb41123104b5012396ca63eb8/docs/studio-migration-roadmap/baseline/selectors.csv) as a historical per-feature coverage map. Select current checks for the
affected contracts, preserve their input bounds, and report actual results.
Build/configure time is separate. Actual native/SLJIT and actual Wasm execution
must be identified; inspected IR, compilation or native execution of a Wasm-named
case is insufficient. Node Wasm results do not establish browser UI behavior.

Missing Stage 0 evidence includes deterministic state/document/history/action
traces; exact same-platform pixels; all input/IME/focus/capture states; complete
save/import/export/failure/retry behavior; owner close/cancel/reload reclamation;
large/typical/stress distributions and long-session retention. Held engine
oracles remain held. Fifteen native selectors without configured engine routes,
browser interaction coverage and soak/capture coverage remain unverified.

## Source recovery and remaining implementation dependencies

The complete Studio Git checkout at `78cb4d4c` is recovered from credential-free
public Git. Its clean worktree, tree identity and every blob's bytes were checked
against the supplied 867-path inventory. `CLAUDE.md` is a symlink: its Git blob
is the link target text, not the contents of `AGENTS.md`. `git fsck --full` passes.
The local complete-history bundle `chibi-studio-main.bundle` is 4,174,683 bytes,
passes `git bundle verify`, and pins `refs/heads/main` to the exact Studio commit.
Its SHA-256 is
`e8f166b06ff6aabbc33f3d0ae03343285ada23eef059f4ca6a133fd0566542d3`.
This closes source access only; it does not establish dependency, build or
runtime health.

Earlier artifact checks found empty artifact lists for six runs (IDs in
`oracle.json`), no Studio releases, and no source bundle in the publisher's
one-day site artifact. Those failed routes are historical, not a current source
blocker. The proposed source-bundle workflow was never published or run and is
not needed for recovery.

No installed immutable SDK, downloaded SDK archive or current Studio build has
been established for either native leg. Partial language/test build-tree
`ChibiConfig.cmake` files are not installed compiler/desktop SDKs. Keep the
original source/assets and each SDK generation separate from candidate outputs;
never point Studio at engine source/private headers or mix SDK generations.

### Bounded build plan for coordinator review, not executed

1. **Verify the original installed SDK.** Acquire the exact leg-1 archive above,
   verify its recorded size and SHA-256, and unpack to a dedicated prefix. Check
   `share/chibi/package.json` identity, source/build provenance, complete file
   inventory, imported targets, `lib/cmake/Chibi/ChibiConfig.cmake`, tools and
   resources. Record the actual compiler, flags, backend and dependency closure;
   historical CI cache names do not prove an SDK's build configuration.
2. **Produce the candidate installed SDK.** Use only engine `64183a1c` through
   its existing `make package-sdk` route, with separate explicit SDK build,
   reflection build and output directories. Agree the compiler/dependencies and
   SDK configuration before starting. The Makefile defaults to Debug with
   sanitizers off; those defaults are not historical parity evidence. Preserve
   the resulting package manifest/hash, source/build identities and file
   inventory. Packaging must complete before any Studio consumer configuration.
3. **Build unchanged Studio twice.** Configure the same clean `78cb4d4c` source
   into distinct output directories, each with `Chibi_DIR` pointing only at its
   verified installed SDK's `lib/cmake/Chibi`. Select identical Studio flags for
   the comparison and record differences in the SDKs. Its CMake checks all
   required imported packages before fetching pinned Zep
   `7aa7ed940d5c889dc6b27a2529a740c0bb2ad6af` plus the checked-in patches; those
   dependency sources are an additional prerequisite, not recovered SDK content.
   Build the native acceptance target only with
   `CHIBI_STUDIO_BUILD_ACCEPTANCE=ON`. A distribution build is a separate claim:
   `CHIBI_STUDIO_DISTRIBUTION=ON` requires a packaged native SDK and Release or
   RelWithDebInfo. Do not relabel the historical Debug startup as distribution
   acceptance or silently modify Studio to make either leg compile.
4. **Capture real build evidence.** Preserve configuration/build exit codes,
   CMake cache and compiler commands, source/EXTRACTION/acceptance-source hashes,
   SDK config/package hashes, binary hashes and copied resource manifests for
   each leg. Verify no private engine source include escaped into the consumer.
   Populate the engine's external-Studio evidence and per-selector records only
   from those actual outputs. In particular, never synthesize successful
   configure/build exits to satisfy a preflight.
5. **Review capabilities and run affected checks.** Select the health/acceptance
   checks with the coordinator; use the exact source workload, disposable
   fixture, isolated preferences/cache and required renderer. Separate
   build/configure timing from execution. Reconcile the 26/28 collider mismatch
   before using that wrapper, and identify the owning executable for routes
   absent from the engine map. A two-frame startup is not UI acceptance.
6. **Resolve drift before migration.** Reproduce/dispose of the font-health
   report, compare original-versus-candidate SDK effects, and capture approved
   original outputs. Complete feature-local state/effect/history/async ownership
   before conversion. B03/B04 tracing and the Outliner vertical slice depend on
   those outputs and owners. This baseline authorizes no broad translation.

Build-contract sources: [Studio CMake](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/CMakeLists.txt),
[Zep pin and patches](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/zep_dependencies.cmake),
[engine build wrappers](https://github.com/waldnercharles/chibi/blob/64183a1c4ba5b367bf35604ddae45fa616fd3bd0/Makefile#L110-L171),
[SDK identity/inventory](https://github.com/waldnercharles/chibi/blob/64183a1c4ba5b367bf35604ddae45fa616fd3bd0/tools/package_sdk.py),
and [acceptance preflight](https://github.com/waldnercharles/chibi/blob/64183a1c4ba5b367bf35604ddae45fa616fd3bd0/tools/agent.py#L190-L258).

## Reproducing the source-only audit

No new test program or execution result was added. Verification uses Git object
identities, existing inventory/registry declarations and metadata reads:

- Verify the local Studio commit/tree, clean worktree and `git fsck --full`.
  Compare `git ls-tree -rl` and all local file bytes (link-target bytes for a
  symlink) with the complete recursive Git tree at the immutable URLs in
  `reconciliation.json`: require `truncated=false`, exactly 867 blobs and exact
  path/blob/size equality with the supplied inventory fingerprint recorded there.
  Verify the complete-history bundle separately by SHA-256 and `git bundle verify`.
- For each selected file in `source-provenance.csv`, compare
  `git hash-object FILE`, `sha256sum FILE` and byte length with the row. Do not
  normalize newlines or otherwise edit the source before checking.
- Reconcile `cmake/studio_sources.cmake`'s 151 source paths plus the four
  executable sources in `CMakeLists.txt` against the 155 product-source paths.
- Read the complete `studio_panel_sources` initializer and all designated
  `COMMANDS` entries. Expand its three macros (`NEW_ASSET`, `SCENE_TOOL`,
  `CODE_COMMAND`) with a C preprocessor, not by guessing defaults. Compare IDs,
  enum ordinals and flags with the CSVs; exclude the count sentinel.
- Inspect the 26 literal selector names in Studio's `main` dispatcher and
  compare the engine's `STUDIO_ACCEPTANCE`, `TOOLS` and `NATIVE` definitions.
  `make test-list` prints the command inventory without executing tests.
- Read workflow jobs/logs and release-asset metadata using the exact run, job,
  release and asset IDs above. Keep historical executed evidence separate from
  this report's source-only findings.

The documentation/metadata checks above passed locally; the prior registry,
selector and font ledgers reproduced byte-for-byte before these recovery/build-plan
updates. All selected provenance files and eight toolchain executable/library
hashes were rechecked. Builds, runtime tests, Studio launch, screenshots and
performance checks were not run for this documentation-only report. No CI,
source-bundle workflow or remote publication was started.
