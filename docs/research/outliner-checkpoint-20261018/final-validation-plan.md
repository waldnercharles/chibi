# Final validation and landing plan — 2026-10-18

Prepared by `/root/outliner_contract` from read-only inspection of both working
trees, their `AGENTS.md`, engine `docs/testing.md` (especially CUT62), the current
supervisor routes, `/workspace/outliner-build/STATUS.md`, recorded logs, and the
request-fuel integration and its preserved draft provenance. Only this planning
artifact was written. The latest update reads native24, serial-Wasm and final-ABI
census logs; it supersedes older pending entries in STATUS.md. No build,
test, source edit, fetch, publication, or server-state query was performed for
this plan. This is an affected-contract checklist, not landing approval.

## Candidate and non-negotiable boundaries

- Engine: `/workspace/chibi-outliner-m3`, branch
  `integration/outliner-m3-20261018`, HEAD
  `ff6d06c888fe660894776255ec7f9fc507db6393`; pending resolved M3 merge input
  `26e0821419e753f42041383bbe4f25db77b62121`. Staged integration, unstaged changes,
  and untracked source/fixture files all matter. HEAD alone identifies none of
  the current combined implementation.
- Studio: `/workspace/chibi-studio-outliner`, branch
  `codex/live-outliner-20261018`, HEAD
  `95ca95bc3ca2da5b899ec06596982ade26d2d94f`, with uncommitted Outliner work.
- Engine request fuel is integrated and has native/serial-Wasm/generated-C
  measurements. Studio production arithmetic/account integration, K249 evidence,
  and the three live-fuel fixture files are now applied after exact BASE/FILES
  checks. Their standalone arithmetic/live-fuel routes are admitted, but have
  not run against the new matching installed SDK. Preserved draft directories
  are provenance, not the current source of truth.
- Preserve explicit holds: engine **#407**, Studio **#78/#79/#80**. Preserve
  the other worker's ownership/landing sequence for engine **#420** and Studio
  **#71, then #77**. Do not mutate those branches, publish their work on their
  behalf, or import held work through dependencies/copies. Inclusion and landing
  eligibility require the integration owner's current authorization check.
  This plan does not assert these server items are currently open, merged,
  approved, or released; no live server lookup was made.
- **M4 is excluded.** M3 package support is not a completed self-hosted compiler.
- Every test invocation, including an aggregate, keeps one existing hard
  **60-second production supervisor deadline** including setup and cleanup.
  Configure/build/package time remains separate. Run selected groups with
  builds idle; do not split a timed-out aggregate, increase its deadline, or
  recreate quarantined suites through repeated small launches.
- Full native/render/Studio suites and historical CUT61 captures remain
  **disabled, not passed**. CUT62 permits reviewed affected-contract checks;
  an uncovered affected contract or required server check still blocks landing.

## Current evidence: useful baselines, not final-tree passes

`STATUS.md` is a chronological ledger; its newest corrections supersede old
pending statements. Preserve failures alongside later fixes. In particular:

| Contract | Existing evidence | Remaining implication |
| --- | --- | --- |
| Runtime authority / final source guard | `test-authority-2.log`: 9 cases/904 assertions; `test-source-guard-2.log`: 3/62 | Rerun after final fuel/context integration. |
| Producer proof / text helper | `test-outliner-contract-8.log`: 4/142; `test-text-format-1.log`: 4/80 | Earlier generation; final ABI and catalog must be covered. |
| Record lookup/publication | `test-record-publication-2.log`: 7/231 | Retain same-ID positive and distinct-ID negative assertions. |
| Build host / orchestration | `test-build-process-2.log`: 19/2327; `test-build-toolchain-1.log`: 6/109 | Final installed host/environment/toolchain generation still required. |
| Native/language/material/tooling groups | Recorded passes: native40.536s, materials48.289s, language27.869s, tools25.611s | Older combinations; language's earlier60.123s timeout remains a failure record. Projects final evidence is pending. |
| Installed SDK65ed Studio | Live12 scenarios: 1/421; guards1/202; projection1/19040; helpers1/1849; ASan+UBSan | These precede new scalar append and request fuel. |
| SDK identity invalidation | `test-studio-sdk-identity-1.log`: 1/48,1.676s | Real consumer CMake regression passed; final explicit configure/relink still necessary. |
| Scalar append | Native21 build PASS2.2s; `test-scalar-append-5.log`: PASS6/1717,.189s | Reader-name correction resolves the prior activation failure. Retain earlier failures; final fuel/backend integration remains to validate. |
| Canonical census | `test-outliner-work-3.log`: PASS1/393,.475s,K249, Project2plans | Final fuel ABI independently qualified; preserved source/lowerer/context/prepared identities and durable Studio evidence. Requalify only if affected pinned inputs change; final clean-candidate identity still required. |
| Request fuel native/generated C | native24 `test-request-work-3.log`: PASS5/815,.216s; native22 `test-request-work-c-1.log`: PASS1/107,.313s | Actual VM/SLJIT and fourteen VM/generated-C calls. Preserve earlier fixture-lifetime failures; production accounting was not relaxed. |
| Serial Wasm scalar/request fuel | `test-wasm-scalar-append-1.log`: PASS6/1716,.256s; `test-wasm-request-work-1.log`: PASS5/815,.216s | Actual generated Wasm, Release/assertions enabled, CHIBI_WEB_THREADS=OFF. No pthread or browser pass implied. |
| Applied Studio arithmetic/live fuel | Exact source and final supervisor/docs reviewed; see `live-fuel-admission-review.md` | Both first measurements pending SDK10 and explicitly reconfigured/relinked consumer. |
| Scale diagnostic | SDK9 `test-studio-scale-2.log`: PASS1/151739,.966s; each backend uses589760 packed-copy bytes and2788966 arena bytes | Scalar linear-copy behavior passes; old10000 budgets exhaust and diagnostic1000000 completes. Does not establish mounted request fuel or Release/UI latency. |
| Native startup | Two frames, valid1920x1080 PNG, exit0,14.065s | **Failed UBSan** in Cute; image/exit0 do not override diagnostics. Rerun only after reviewed dependency fixes on a new exact package. |
| Web Studio | Earlier web package/build succeeded; browser fixture source admitted | No current browser interaction pass. Build success is not UI verification. |

SDK65ed was actually instrumented; earlier SDK/Studio Debug results were not.
Verify flags from compile commands and link commands, not the ignored standalone
SDK `CHIBI_ENABLE_SANITIZERS` cache option. Keep those profiles clearly labeled.
SDK9 completed with package identity
`e3eb0a5e4510e1c15b97f5e22a2117cdf8c2ea56c07cf325382b083c5ae0bdf1`;
its source snapshot was
`655c6e8dc7cee0267ae70aa0acc4c3a228236e63958350be25887733882f299c`.
Its1562 SDK and194 Studio compile commands were instrumented. It predates the
integrated fuel ABI. SDK10 is being prepared; no SDK10 package/consumer pass is
claimed by this update.

Latest directly read log identities:

| Log | SHA-256 |
| --- | --- |
| `test-request-work-3.log` | `ce516c3cc65621ea24958edd1cafd32a4796844f3cedb1941deb009f0c42bb29` |
| `test-wasm-scalar-append-1.log` | `429ed76d720b531b6d1bedb7e0541f91d891c1ea93d0c2ccd8d18f73d517b089` |
| `test-wasm-request-work-1.log` | `c733aa6fa4cfa663098a5e908ad8a814ba51959fa22bc1f3ae90f21c275a062f` |
| `test-request-work-c-1.log` | `57abaa8ab2af1fe3f119bd741c71443cc738ccf35adca0b7433ab9eb318c6b96` |
| `test-outliner-work-3.log` | `530ceb49ea65144694a986b57b26829d430725c24dab3f7663617388f80ecc70` |
| `test-studio-scale-2.log` | `fe59e5250e933ab0938fe421eaafe043b764d7ecef1a3d4eef6689b2e6901b6d` |

These are checkpoint passes on recorded in-progress sources, not a claim that
all final clean-candidate requirements have been fulfilled. No new run is needed
merely to refresh a status label. At final freeze, map identities to the exact
candidate and execute the selected final matrix as required by CUT62; do not
repeat individual group members once that same candidate's group passes.

## Required finite checks and affected builds

For the current combined scope, retain the five admitted AGENTS groups:
`make test-native`, `make test-materials`, `make test-projects`,
`make test-language-all`, and `make test-tools`. Each is a separate existing
shared-deadline invocation; no multi-goal shortcut. Record the actual selected
build directories and controller paths. Do not repeat a focused check merely
because it is already covered by a passing exact-candidate group, unless the
focused selector is outside that group or supplies a distinct installed/backend
contract. New Outliner/scalar/request-work selectors remain outside aggregates.

Use the existing form `python3 tools/test_policy.py tool <name>` with explicit
`CHIBI_LANGUAGE_BUILD_DIR`, `CHIBI_TOOLS_BUILD_DIR`, `CHIBI_CTL`, and build-host
paths as applicable. The following is the additional affected-contract map.
Registration alone is not admission; preserve the exact reviewed specimen and
bounds, and re-review any workload change before execution.

| Area | Selected routes / build evidence | Contract and backend requirement |
| --- | --- | --- |
| M3 packages and merge resolutions | `language-package-graph`, `language-package-syntax`, `language-package-resolution`, `language-package-execution`, `language-package-imports`, `language-package-files`, `language-package-core` | Existing finite graph/syntax/resolver/filesystem/ownership cases; executable routes must prove bytecode and actual SLJIT. Preserve unsupported Windows disk-loader boundary. |
| Private build host / CLI | `language-build-workflow`, `build-process`, `build-toolchain`, `chibi-cli`; applicable `package`, `package-ownership`, `desktop-dependencies` | Recording/real-child parity, declared nongeneric entry, argv/cwd/allowlisted environment including loader path, bounded child output/deadlines, packaged host and orchestration. Host remains private to build tooling; no credential/environment broadening. |
| SDK ownership and installed M3 | Rebuild/install native and web SDKs; relocated installed public-header/archive/core-source consumer checks using existing bounded package routes; owning-target/configure checks | Verify exported inventories, target dependencies, core source and native/web package identities. Old M3 branch's installed results do not establish current combined tree. Studio must consume installed public SDK only. |
| Catalog and producer | `language-record-publication`, `language-outliner-contract`, `language-source-guard`, `text-format` | Nominal identity remains strict; callback/ABI/schema/state/event/callee validation and fail-closed aggregate provenance; final source-set commit guard; exact entity decimal and byte primitives. Contract4cases/20finalizations/40validator calls executes no authored body. |
| Authority and lifecycle | `language-authority`; existing language program/publication/modules/activation-cache/borrow checks in the selected language group | Tagged origin, fan-out ownership, retained event payloads, cancellation, nested revocation, source replacement and borrowed lifetimes; bytecode plus actual compiled entries. |
| Scalar append | `language-scalar-append`; existing `language-row-builder`, `language-row-array`, `language-borrow` (group membership may satisfy these once) | Exact derived plans, callback/shape rejection, immutable prefixes and snapshots, GC roots, rebind/inspection and same-address arena reuse, linear copies; bytecode and actual SLJIT. Six cases/eight preparations/24 top calls, max128 iterations,20000/frame. Latest reviewed delta makes14 static reproofs, no malformed execution. |
| Scalar Wasm / prepared derivation | Existing admitted `wasm` group plus standalone `wasm-scalar-append` | Serial scalar first measurement passed6/1716. Final-candidate group supplies execution/program/publication/module/row/borrow interactions without separately repeating its members; scalar remains standalone. Prepared decoding must rederive rather than trust serialized optimization metadata. Worker checks below are a distinct profile. |
| Request fuel | `language-request-work`, `wasm-request-work`, `request-work-c` | Integrated and measured: native5/815, serialWasm5/815, generatedC1/107. Final-candidate checks retain both actual native backends, emitted Wasm, and actual generated-C compile/load; fourteen C/VM calls and one capped compiler child. All remain standalone. |
| Canonical coefficient | `language-outliner-work` with explicit absolute `CHIBI_OUTLINER_CONTENT` | Recensus exact final shipped sources and prepared ABI:1case,2fixed reads,7overlays,1preparation/2modules,22compile-time constant evaluations,20declarations/14reachable bodies,0runtime calls. Retain log and final generation identity. |
| Studio pure/model integration | `studio-outliner-row-policies`, `studio-outliner-projection`, `studio-outliner-live`, `studio-outliner-live-guards` | Installed SDK current generation; pure routes VM+actualSLJIT, all row/header values and ownership; live retained mount/model/world/source/request authority, atomic model commit, stale rejection and ordered controls. Keep all12 live scenarios and guard assertions. |
| Studio arithmetic and production scale | `studio-outliner-work`, `studio-outliner-scale`, `studio-outliner-live-fuel` | All applied and source/wiring admitted. Arithmetic4cases/38measurements/43limits/max8actors and livefuel1case/8collections/8modules/44constants/10sends/4Begin need first measurements on SDK10. Livefuel executes VM+actualSLJIT,4096actors/32rows, finite loops<=32767, <=8615 scoped entries/backend, before/after-Publish retention. SDK9 direct scale passes but is not mounted fuel evidence. |
| Consumer invalidation | `studio-sdk-identity` and actual explicitly reconfigured Studio app/acceptance links | Exact SDK build identity must invalidate every consuming target despite older installed file timestamps. Record both consumer compile definitions and actual linked package identity. |
| UI/render integration | Two-frame instrumented native startup; `studio-outliner-browser`; `studio-outliner-move`; `studio-scene-move` | See the bounded rendering decision below. These complement model assertions; they do not replace them. |

Native Debug with verified ASan+UBSan covers ownership/proof/model checks.
Build applicable native Release controller/player/CLI paths and validate the
Release-dependent project group; a Debug result is not Release evidence.
`make web` with the actual Studio checkout and final installed web SDK is required
for UI changes. Native/Wasm/generated-C context ABI changes require rebuilding
their actual consumers and prepared images, not merely relinking one app.

Existing Wasm aggregate/browser routes remain bounded by their own admissions.
The minimal serial CPU coverage is the admitted `wasm` group plus its two new
standalone scalar/fuel routes; do not repeat the group's row/borrow/module
members separately. The real Studio browser route supplies the changed mounted
browser path. A separate full `wasm-browser` CPU run is optional unless final
review identifies a browser-specific bridge contract not covered by these; no
WebGPU or worker-rendering pass is claimed.

### Concrete worker profile, with a narrow exclusion

Current measured `/workspace/outliner-build/wasm-language/CMakeCache.txt` has
`CHIBI_WEB_THREADS=OFF`. A serial pass cannot close worker configuration coverage.
Use the existing language target in a separate directory, for example:

- Build separately: `make wasm-language-build WEB_THREADS=1
  WASM_BUILD_DIR=/workspace/outliner-build/wasm-language-workers`.
- Set `CHIBI_WASM_BUILD_DIR=/workspace/outliner-build/wasm-language-workers` and
  run existing standalone `tool wasm-wasm`. Its pthread-only ninth case runs
  two generations, two independent worker programs per generation, and two
  dispatches per worker, joins all workers, and checks actual emitted-entry
  compilation/execution/retirement. The eight existing fixed cases also retain
  their bounds; no new fixture or aggregate split is proposed.
- Run the unchanged `tool wasm-scalar-append` and `tool wasm-request-work` against
  that same worker-configured dispatcher. This covers generated code/shared
  memory/TLS configuration, not simultaneous execution of their individual cases.
  Keep each existing route's shared60s limit and disclose that distinction.

The finite worker `wasm-wasm` specimen is already documented/admitted for
WEB_THREADS=1 in engine docs/testing.md. Review the final worker build flags and
actual worker-case result; absence of the pthread case is a failure to cover the
profile. Request tokens deliberately require serialized use with their programs
(`src/execution/authority.h:8`); token refcounts and balances are not a concurrent
sharing API. Therefore parallel mutation of one token is excluded by contract,
not waived because a test is missing. Independent program execution and worker
entry lifetimes remain applicable checks above. This does not require a second
threaded Studio render/browser run when the delivered Studio profile remains
serial, and does not claim one.

## Fuel qualification and remaining coverage blockers

1. **Scalar functionality passes natively and in actual serial Wasm.** The
   canonical index-name correction retains strict private-array/result shapes
   and custom-alias rejection. Native scalar5 passes6/1717; Wasm scalar1 passes
   6/1716, with actual emitted entries. SDK9 scale2 verifies both Project plans'
   linear copies. Final clean-candidate and worker-profile mapping remain.
2. **Fuel integration is applied and its backend fixtures now pass.** Native24
   requestwork3 passes5/815 after fixture-only host-reference/update-epoch fixes;
   serialWasm1 passes5/815; generatedC1 passes1/107. Original assertions and
   earlier failures remain preserved. This closes the earlier failing-runtime
   diagnosis, not the pending installed Studio mounted execution or final-tree
   validation. No limit/deadline/assertion was weakened.
3. **K249 is qualified for the final fuel ABI.** Census3 counts fourteen actual
   reachable canonical bodies, 20 declared functions, 22 compile-time constants,
   zero runtime calls and Project2plans. Sum: Refresh6+Reveal7+Depth9+Icon17+Drop49+
   SelectState4+FoldByte7+ContainsFolded21+LastIndex21+Title13+IntRange9+
   BehaviorCount12+Page21+Project53 =249. The full qualification is in
   `census-final-abi-review.md`. The applied Studio constant cites durable
   `docs/evidence/outliner-request-work/{census.md,census.log,engine-fuel.sha256}`.
   Authored controller SHA is
   `510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450`;
   library SHA is
   `e2c727396b2a5004327fcf17cd3e1a8194629c928db75c4927ef6f4cae9c28a5`.
   Lowerer2658fc23, function.hfe1e9b69 and prepared.ccfec73fa are pinned in the
   evidence. Source hashes document the bound and never grant authority.
   Final formatting of unrelated files does not invalidate this sum; changes to
   pinned source/lowering/checkpoint/ABI inputs require explicit requalification.
4. **The source formula is reviewed:**
   `W=8+3N+8V+T+3R+D+2Cv+2Fv+Av+l+min(l,127)` and
   `total=10000+K*W`, with checked arithmetic and no renewal. Two FoldByte calls
   explain3R; repeated prototype spans requireV*cmax; disjoint fields justify
   min(F,V*fmax), with multiplication checked before min; Page extrema and
   full-path Title scanning are covered. Safe edited code may exhaust normally.
   This is neither a native-operation nor wall-time bound nor a new scene cap.
5. **Actual live fuel behavior needs evidence.** Pure arithmetic, preparation
   census and direct high-budget Project calls do not test mounted account
   grant/exhaustion. Preserve or add only the owner's separately reviewed finite
   coverage for pre-Publish refusal/old model retention and post-Publish failure
   retaining the already-valid committed model, with genuine production-sized
   fixed4096 input completion under the new account. No guessed larger limit.
6. **Optional targeted additions, with static-only evidence disclosed:** scalar pause/resume and cross-arena resume
   have code review but no dedicated new execution case. The request-work fixture
   does not directly exercise grant exactly at the initial limit or grant after
   nested dispatch (authority tests cover nested proof revocation separately).
   These are not automatic extra-test or landing blockers: final independent
   review can accept the existing lifecycle/authority tests plus the specific
   static argument, or request a bounded delta if it finds an unresolved defect.
   Never describe them as directly tested. Actual serial Wasm scalar activation now passes. Final installed live request
   completion and failures on both sides of publication remain mandatory; their
   applied, admitted route is ready for its first matching-SDK measurement. It
   does not observe exact token spend, which the engine fixtures cover.

## Minimal sufficient rendered route; preserve input/hierarchy assertions

The two-frame startup and16-frame browser route are useful focused replacements
for the affected startup/render/filter portions of a quarantined full suite.
They are **not** sufficient alone for this entire UI change:

- `studio_ui.c` now routes every Outliner-mount event through the new origin and
  retained-payload adapter. `Outliner.rml`'s RowClicked, RowDropped and ListDropped
  consequently reach unchanged OutlinerInput through a changed dispatch path.
- Current live/guards exercise ShowRows/Refresh/Reveal/fold/toggle controls, not
  actual RowDropped/ListDropped pointer fan-out. Browser16 exercises filter text,
  not hierarchy movement. Pure row equality proves data, not interaction wiring.
- Keep existing admitted **`studio-outliner-move` (21 frames)**: three actors,
  before/inside/after/root drop zones, grouped selection, cycle rejection,
  Alt-arrow ordering/reparenting, stable identities/transforms and indentation.
  Keep **`studio-scene-move` (two frames)** for the associated hierarchy move,
  history/collaboration and model/checkpoint contract. Parent explicitly agreed
  to retain these instead of inventing replacement fixtures.

Use a separate **ordinary Debug, assertions-on, unsanitized** installed SDK and
Studio consumer for rendered acceptance selectors if needed. Current ASan+UBSan
acceptance has roughly284MiB allocated text/data and cannot meet the existing
64MiB admission; stripping debug sections cannot solve that. Preserve the cap,
assertions, sandbox and loadable sections. A reviewed debug-only stripped COPY
of a suitable ordinary Debug executable may remove nonloadable debug sections;
prove identical loadable content and refresh the complete binary/SDK/compile/
EXTRACTION/acceptance-source/UI-resource evidence. Never strip or overwrite the
instrumented validation package and call it equivalent coverage.

Separately, rebuild the reviewed two-frame **instrumented** startup copy with
the corrected Cute providers. Refresh its matching disposable project/resource
manifests, isolated preferences/cache/runtime paths and renderer provenance in a
new evidence directory; preserve the old failed directory. The documented
startup is its own admitted `run_commands(seconds=60)` route, not a bypass for
running a size-rejected acceptance selector. Require exit0, clean sanitizer and
Studio logs, decoded1920x1080 PNG, manual panel/text/icon review and no remaining
children. Record software Vulkan honestly; no hardware/Release timing claim.

The existing `studio-outliner-browser` route keeps16 frames,52CDP commands,
three PNGs, one browser/profile/tab and its55s internal/shared60s deadlines.
Use final web assets and exact current content. Manually verify43 initial rows
with Settings/Data/Theme, one Settings match, then zero matches and the exact
notice, in addition to its error/pixel assertions. Retain review.txt, browser.log
and all images. No sandbox overrides, fallback renderer or extra frames.

At this planning checkpoint the parent reports7.9GiB free. Build scheduling or
removal of owned passed obsolete scratch may recover space after preserving
provenance; that does not authorize deleting shared caches, other workers'
outputs, failed evidence or the instrumented profile. Resource pressure does
not waive affected interaction coverage.

## Concrete remaining run checklist

All commands below are a plan, not execution by this reviewer. Configure/build
first, freeze inputs, then run each selected invocation separately with builds
idle. Use absolute paths and record final artifact hashes; the example directories
are existing task-owned locations, not assertions that they are already current.

1. **Mandatory five groups on the final candidate.** Keep all five; neither the
   small new fixtures nor old group logs replace them. The existing make targets
   and their test-only equivalents are:

   | Required target | Existing supervisor invocation after its build prerequisites |
   | --- | --- |
   | `make test-native` | `python3 tools/test_policy.py group native` |
   | `make test-materials` | `python3 tools/test_policy.py group materials` |
   | `make test-projects` | `python3 tools/test_policy.py group projects` |
   | `make test-language-all` | `python3 tools/test_policy.py group language` |
   | `make test-tools` | `python3 tools/test_policy.py short /workspace/outliner-build/tools-only/test_tools` |

   For the four named groups use
   `CHIBI_TOOLS_BUILD_DIR=/workspace/outliner-build/native/test/tools`,
   `CHIBI_LANGUAGE_BUILD_DIR=/workspace/outliner-build/native/test/language`, and
   `CHIBI_CTL=/workspace/outliner-build/release/bin/chibi_ctl` for projects.
   The fifth target intentionally uses the separately configured runtime-only
   tooling binary. Its complete group already runs package.c and execution-policy
   cases; do not add duplicate standalone `package` or `unit` invocations.
   Language-group text-format/borrow/row-array/row-builder/program/publication/
   modules likewise need no duplicate standalone native run.

2. **Focused native contracts outside those groups.** With the same native
   language/tools directories, run each applicable `tool <name>`:
   `language-package-graph`, `language-package-syntax`,
   `language-package-resolution`, `language-package-execution`,
   `language-package-imports`, `language-package-files`, `language-package-core`,
   `language-build-workflow`, `language-record-publication`,
   `language-authority`, `language-source-guard`, `language-outliner-contract`,
   `language-scalar-append`, `language-request-work`, `request-work-c`,
   `package-ownership`, `desktop-dependencies`, `studio-sdk-identity`.
   Keep `build-process`, `build-toolchain`, and `chibi-cli` separate: they are
   explicitly excluded from runtime-only test-tools. Supply
   `CHIBI_BUILD_HOST=/workspace/outliner-build/native/sdk/chibi-build-host` and
   `CHIBI_CLI=/workspace/outliner-build/native/sdk/chibi`; their policy_worker
   comes from CHIBI_TOOLS_BUILD_DIR. Current measured scalar/fuel/C/census passes
   are valid checkpoint evidence; the final clean-candidate ledger must identify
   the source/artifact actually exercised, not silently relabel old packages.

3. **Census and installed SDK.** If the final candidate changes no pinned census
   input, retain qualified K249 and link its evidence to the clean candidate.
   The bounded final-candidate census, if needed for the final ledger, is
   `tool language-outliner-work` with
   `CHIBI_OUTLINER_CONTENT=/workspace/chibi-studio-outliner/projects/studio/content`.
   Build/package SDK10 and final web SDK, verify target/export ownership and
   package source identity, and use the existing installed language consumer
   configuration `-DCHIBI_INSTALLED_SDK=ON -DChibi_DIR=<installed>/lib/cmake/Chibi`
   for installed core/import/catalog checks (not the in-tree library).
   Run the selected package-core/imports/record-publication routes against that
   consumer using its own CHIBI_LANGUAGE_BUILD_DIR. Preserve relocation and
   runtime-only public-dependency checks from the existing package qualification.

4. **Wasm backends.** Serial final language build:
   `CHIBI_WASM_BUILD_DIR=/workspace/outliner-build/wasm-language`, then existing
   `group wasm`, `tool wasm-scalar-append`, and `tool wasm-request-work`, each
   separately. Current standalone serial passes are recorded above; do not
   re-run solely because the status document changed. The distinct worker build
   uses `/workspace/outliner-build/wasm-language-workers` with WEB_THREADS=1,
   then `wasm-wasm`, `wasm-scalar-append`, and `wasm-request-work` as described
   above. No threaded Studio render build is implied by these CPU checks.

5. **Installed Studio first measurements and regressions.** Explicitly configure
   and relink app plus acceptance after SDK10 promotion. Set
   `CHIBI_STUDIO_BUILD=/workspace/outliner-build/studio-native`,
   `CHIBI_OUTLINER_CONTENT=/workspace/chibi-studio-outliner/projects/studio/content`,
   and `CHIBI_OUTLINER_SOURCE=<that content>/libraries/OutlinerRowPolicies.chibi`.
   Run `studio-outliner-work` and `studio-outliner-live-fuel` first measurements,
   plus final-generation `studio-outliner-row-policies`,
   `studio-outliner-projection`, `studio-outliner-live`,
   `studio-outliner-live-guards`, and `studio-outliner-scale` as separate routes.
   Match actual compile SDK identity and link generation; a successful app relink
   alone does not prove the acceptance binary was relinked.

6. **Rendered affected behavior.** The already admitted two-frame instrumented
   startup must be rerun after Cute fixes, followed by manual PNG/log review.
   Final serial web SDK and Studio assets feed `tool studio-outliner-browser`
   with `CHIBI_TOOLS_BUILD_DIR` above,
   `CHIBI_STUDIO_WEB_ROOT=/workspace/outliner-build/studio-web/bin`, and
   `CHIBI_STUDIO_SOURCE=/workspace/chibi-studio-outliner`; retain all three PNGs
   and manual semantic review. Keep `studio-outliner-move` and `studio-scene-move`
   on the separately built ordinary-Debug assertions-on render profile,
   supplying its absolute CHIBI_STUDIO_BUILD and a fresh route-specific
   `CHIBI_STUDIO_ACCEPTANCE_EVIDENCE` with matching SDK/EXTRACTION/resources.
   Preserve the existing64MiB preflight and exact loadable sections for any
   approved debug-only stripped copy. These two interactions remain required.

No request here expands an aggregate, deadline, fixture input or frame count.
Optional static-only gaps are listed above; new tests are needed only if final
review identifies a concrete uncovered defect. Final holds and server-required
checks remain separate blockers even if every local invocation passes.

## Freeze, execute, and publish checklist

1. Resolve implementation failures; integrate only reviewed final fuel/scalar
   deltas; reconcile both repositories' manifests and `EXTRACTION.json` for every
   edited extracted file. Keep new Studio-only files outside extraction records.
   Feature-coverage uses must exist or have truthful reasoned gap entries.
2. Independently review the whole combined diff and merge resolutions, public
   ABI/export ownership, generated/installed paths, failure cleanup, and this
   selected matrix. No project-version bump is justified merely by additive
   capabilities/runtime behavior; identify an actually incompatible stored
   project before proposing migration. Preserve declared numeric semantics.
3. Owner refreshes main and eligible source revisions through authorized read
   operations, checks current holds/approvals/protections, and makes a clean
   candidate with every intended tracked/untracked input included. No merge is
   authorized by this plan. Capture both commit/tree IDs, parents and source
   revisions; hashes of dirty files alone cannot substitute for the final tree.
4. Format changed C with clang-format22 and hand-edited authored assets through
   the authorized `chibi_ctl --format` workflow; review resulting diffs. Check
   whitespace, no conflict markers, generated/private headers not exported,
   and no accidental source-tree linkage in Studio. Any format/source change
   precedes the final source census/package freeze and selected checks.
5. Build final native instrumented, native Release, web and required ordinary
   Debug render profiles, plus installed/relocated SDK consumers. Explicitly
   configure Studio **after** each SDK promotion. Verify compile/link flags,
   SDK identity definitions, actual link outputs and bundled authored sources;
   stale acceptance link evidence must not recur. Record configuration/build
   commands, durations and exits separately from tests.
6. Execute the admitted matrix on those exact artifacts, with builds idle and
   one production deadline per invocation. Record command/environment paths,
   backend-entry counters, exit/status, logs and complete source/package IDs.
   Final census qualifies K before production activation. Any route/source
   delta needs its own review/admission; preserve failing evidence and explain
   its fix instead of repeated unreviewed attempts.
7. Complete manual native/browser image reviews and current-tree sanitizer,
   child-cleanup, installed ownership and source/resource identity checks.
   Ledger each contract as PASS, FAIL, BLOCKED or UNEXECUTED with exact scope;
   source admission, a build, and a historical pass are different evidence.
8. Before publication, recheck both local trees and remote main/source heads,
   holds and required approvals/checks. Respect engine407, Studio78/79/80 and
   the other worker's420/71→77 sequence. CUT62 allows local evidence when CI is
   disabled only if actual protections do not require those checks. An absent
   job is not a pass. Any changed base/tree/source/dependency/validation flags
   requires renewed combined review and affected validation.
9. Land only the complete tested combination through the separately authorized
   workflow. Preserve SDK/Studio dependency ordering so no published consumer
   requires an unavailable engine API. A server merge/squash ID may differ only
   with proved identical tested tree, unchanged inputs and satisfied current-head
   requirements. Intermediate trees have no inherited validation or authority.

The immediate publication blockers are final clean-candidate validation, matching
installed SDK/consumer evidence, first Studio arithmetic/live-fuel measurements,
worker-profile checks, and affected native/browser interaction checks. Engine
fuel/backend functionality and final-ABI K qualification now have passing evidence.
Unrelated quarantined full suites do not become mandatory merely because this
is a mixed change; the explicit holds and uncovered affected contracts remain.
