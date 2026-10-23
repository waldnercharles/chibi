# Chibi coordination plan

Updated 2026-10-23. Coordinator: `Review Chibi self-hosting work`, chat
`01a16428-9213-7ec1-b984-a2af4ceced41`.

The user authorized coordinating existing chats, creating owner chats when useful,
recovering local work and merging ready changes. Chibi is the primary authoring
language: compiler and build policy, typed assets, materials and shaders belong in
Chibi as their replacements become viable. Authored shaders must support CPU
inspection tied to the same source and generation used for GPU publication.

This file records current decisions. Earlier source/checkpoint detail is retained
in [.coordination/2026-10-22-before-d0-refresh.md](.coordination/2026-10-22-before-d0-refresh.md)
as historical evidence; its old queue, holds and pending statuses are superseded
by this plan. Each owner retains detailed source and validation receipts.

## Verified baseline

- Engine main is `d0de4a84ecf50badcab661411470bdf6f3da5fa2`, tree
  `3d0b4c546201728d27ceffcf602215bd9f9a0f98`. PR653 landed by ordinary merge,
  preserving PR662 and PR666 ancestry; GitHub reconciled both siblings as merged.
  All three source branches remain. The merge tree exactly equals reviewed/tested
  candidate `8f9dbf5e8edb68269c820bc99baf84bf18bc9c17`.
- Final candidate passed all five required local groups: 1,592 cases and
  1,570,365 assertions, zero failures. Navigation, graph SDK and formatting passed.
  Full hosted CI `39891362350` passed every enabled job, including Linux native,
  Linux graph-host SDK packaging, macOS native, normal Studio and the exact native/
  Web Studio pair. Native Windows jobs were intentionally skipped; their absence
  is not compiler-only evidence. No fresh main-triggered CI result is claimed here.
- Separate compiler-only run `39891496704` passed macOS, Windows, Linux and the
  aggregate identity gate at exact8f9. All six stage1/stage2 outputs match the
  canonical 12,098,990-byte compiler, SHA256
  `41905e8bc4afc3998cf48097a98d6c1ca0ce89ff2de6a6dadcffb6fb06f1c71e`.
  Both stages have empty PATH and unavailable CC/CXX, use pinned native Chibi
  runtime executables and copy only native-source-free inputs. The compiler
  rebuild requires neither a C compiler nor CMake. Building/provisioning the native
  runtime itself remains a separate native-toolchain operation.
- Studio main is `df050886dd7721db63b40fdf5435f1cb20ceeb34`, tree
  `a3639ed69dc67a79c225b0f10c961941b7624e39`. PR92 merged after native/Web
  package and startup evidence; exact-tree normal and paired Studio checks passed.
  Browser interaction acceptance is separate.
- Primary engine fast-forwarded to d0 while preserving the existing
  `docs/design-notes.md` bytes. Primary Studio is clean. Original CI pending merge,
  its dirty/untracked files, owner stashes and all branches remain preserved.
  Landing/preservation receipts are under `/tmp/chibi-root-landing-20261022/`.
- The original Claude checkout `chibi/.claude/worktrees/upbeat-spence-90ef8b` is
  clean at `f8b56f020ca371b96c73fda705735fbf0e66faa8`, already in main. Claude
  graph PR655 and recovered follow-ons are also in main; ignored artifacts remain.
- There is no global test wall-clock ceiling or timing-admission quarantine.
  Select checks by behavior and required coverage. Preserve actual operation,
  memory, hardware and frame bounds, explicit current holds and server requirements.
  Retired example/game repositories remain read-only.

## Current local validation queue

Queueing avoids actual shared-machine memory contention. Independent source
preparation/review proceeds in parallel. Elapsed duration is not an admission gate.
Owners seal exact source/binary/log receipts and explicitly release execution.

1. **Task prerequisite completed.** Corrected tree6353f5a382785977b0b9948cfcbcde37facd0d0c
   passed11 cases/101,682 assertions. Root matched21 source hashes, patch,24 logs,
   two final binaries and actual summaries. Earlier driver hashes remain unknown;
   final identities are not retroactive. Docs-only whole-task-lifetime overlayf0fe
   is separate. Common/query composition and bootstrap remain unrun.
2. **Compiler measurement completed; reviewed source repair queued.** Frozen32-file
   rich/Append/prepared-host tree2acb927a94319b4bfff42fa56d5af613e4b664da
   did not run required groups: old host hit layout work, fresh host hit shared
   preparation storage. Root matched32 source hashes and fresh host20722c41.
   LLDB capture failed before useful recovered counters; its one-byte dump is
   quarantined and invalid. Three-file counter instrumentation then established
   the recovered366-function compiler fails finalization reserve:212,975,283 bytes
   requested against210,629,435 remaining, a2,345,848-byte shortfall before service
   binding. Layout work231,747,812 is within its268,435,456 limit. EmitBody's
   conservative estimate is43,209,948 bytes, including31,980,672 for two matrices.
   Source/limits/accounting are unchanged. Production files and host were restored;
   diagnostic native archives are unqualified until normal rebuild.
   Body-only debug-prefix successor bd1414df reduced the initial request by
   2,590,533 bytes to 210,384,750 against 210,494,057 remaining. Service-table
   storage reservation then passed, but definition ownership failed later with
   7,167 bytes remaining. Publication, full load and third-party build were not
   reached. That failed measurement is preserved and execution released.
   A second debug-watch helper extraction is source-only at 1a594100, with
   independent source review clear. It must pass a complete ordinary load before broader
   required/compiler/actions/host gates and fresh resources. Fit is unmeasured.
   Root preflight matched all32 files, exact index/no unstaged tracked diff,
   restored counter sources and diagnostic hostc873cf70. Runner review caught a
   cancellation race during Popen construction before child assignment in both
   profile476ea and ordinary25b84 runners. Their preserved originals may not run.
   Corrected profilebb057adf and ordinary5edd1edb defer cancellation until owning
   the child and PID, with pending checks before launch and before wait. Root
   matched both hashes, exact narrow diffs, AST and unchanged source/output state;
   independent re-review is clear. These successors remain unrun and queued
   behind explicit CI release. Compiler source remains unchanged.
3. **Typed narrow focus completed.** SDK68e625a9 from production0b5, SDK family8/202
   on fixture-only966, Studio335 SDK-values12/163 and model1/261 passed with zero
   frames/renders. Receiptbac49fae retains original7/8 fixture failure. Root matched
   nine logs, five SDK products and actual summaries. Studio local ordinary merge
   466e2cc3 is clean with exact tested335 tree; it is unpublished, with postcommit
   binary currency unclaimed. CI3-file applicationf854 was independently audited;
   its docs-only successor9fa is the protocol9 base; final source6ef is active below.
4. **Collection native focus stopped; reviewed fixture repair queued.** First fresh Release stage-zero build
   passed; types had26 passes/one failure, including all11 new runtime cases passing.
   The failed assertion incorrectly substituted external-retained bytes for arena
   ownership. Reviewed test-only successorf90c191e8be1305f4ea05344e7f01fbf155889c8
   restores string/split-array payload ownership and keeps descriptor/GC checks.
   Rebuilt test_tools passed types: 27 cases/4,861 assertions. Record-runtime
   passed six of eight cases and all three new resolver cases, but two older
   fixtures lack the caller-owned heap required by the new array contract.
   Source repair keeps stable fixture roots and explicitly captures results that
   must survive producer teardown. Frozen successor97c1656f also corrects union
   cost/provenance summaries; runtime/resolver implementation is unchanged.
   Root matched all ten focus hashes, five changed paths, manifest841d2709 and
   patch5efb0004. Independent source review is clear, including actual union
   intrinsics and named scalar metadata; no forced-GC coverage is claimed.
   Execution is released;
   semantic/execution/authored/sharing/bootstrap remain unrun. Source ABI22 and
   artifact ABI21 remain distinct; no current-main/common readiness is claimed.
5. **Active: CI protocol upgrade and required groups.** Source review cleared the
   five-file protocol9 patch 30112d92 and prospective tree6ef6b76e. Older protocol8
   hosts do not guarantee the compiled bootstrap contract; ordinary Make must
   recover before loading authored programs after KDL removal. The compatible
   typed SDK68e also reports8, so numerical version alone is not a capability proof.
   Regression-only e9fc failed exactly the new protocol8 route case against old
   Make; the preceding ten cases passed. Exact6ef is applied with original pending
   ancestry preserved. Real ordinary `make build-release`, without a BUILD_HOST
   override, upgraded the existing release8 host to fresh9 and passed its graph
   build. Root independently matched the red log/tree and final source.
   Green affected selectors passed: build-toolchain11/168 and build-process45/8975.
   First required group test-native passed: 632 cases/236,737 assertions, zero
   failures. Root matched the exit0 receipt, actual75 summary rows, log4a333f20
   and unchanged6ef source. Native sanitizer build and test timings are recorded
   separately. Required test-materials also passed: 110 cases/270,494 assertions,
   zero failures. Root matched its exit0 receipt, nine actual summaries,
   log38585410 and unchanged source/ancestry. Two groups are completed.
   CI owns projects, language and tools next. Typed owns failure repairs.
   After explicit release, the next short proofs are the frozen compiler v3 load,
   then collection97c native focus. No full-gate pass or
   commit is claimed. Required gates and actual source/ancestry/currency
   precede a clean commit and hosted generic no-C proof. Native provisioning,
   runtime pins, paired native/Web, cold Studio and broader acceptance stay separate.
6. **Later scopes:** collection authored/bootstrap, PR407 operation/required checks,
   shader policy closure/facade and material proof, ProjectTools installed proof,
   then remaining renderer/Studio acceptance as dependencies permit. Query/common
   requires eligible compiler/task/pointer composition. PR460 still needs real
   Linux red/green and matching archives; Studio77 needs final SDK/browser acceptance.

## Owners and current checkpoints

| Work | Owning chat | Current assignment |
| --- | --- | --- |
| Rich compiler, Append, prepared host and Project policy | Set up Chibi projects on main — `01a16507-c399-7792-9ec9-1acebd031b34` | Freeze and validate combined rich/Append/host/d0. Project.chibi remains separate source-only; no default flip. |
| Typed assets, KDL removal and Studio migration | Typed Chibi assets and KDL removal — `01a160a6-7daa-74e1-b595-8af541a773eb` | Current-main source integration and compatible SDK/Studio validation, then eligible paired landing. |
| Chibi materials and CPU-debuggable shaders | Chibi materials and CPU-debuggable shaders — `01a165aa-9cba-7700-b8d0-b3e84dab468b` | Flat Facts.chibi schema/consumer source preparation; agree compiler producer before authored material lowering. |
| SDK and build graph | Extend Chibi build graph and SDK packaging — `01a165aa-e67e-75b3-8bf1-5637109599a1` | Typed SDK source review completed; prepare Project inventory/receipt/capability contract with compiler owner. |
| CI and native runtime provisioning | Repair Chibi CI and dependency publication — `01a165ab-2d6d-7e51-937e-5b7466ea2c72` | PR653 complete. Prepare KDL-free runtime compatibility and compiler-only input selection on exact eligible source. |
| Tasks, pointers and common runtime | Plan Odin-inspired packages — `01a15cd2-7126-7cc0-bc72-da5791aae3d5` | Corrected6353 focus passed/released; source composition with compiler/query baselines next. |
| Collections and resolver-aware record seam | Collection owner — `01a160a6-14c8-7393-bae2-5de4ed95921a` | Types passed on f90c; two existing record-runtime fixtures require the new managed-heap lifetime. Source-only harness and union effect-summary repairs are being frozen/reviewed. Semantic/execution and authored/bootstrap remain queued; ABI22/artifact21 remain distinct. |
| Query and reference lifetime | Review system and query syntax — `01a16526-25dc-7040-9202-a0a882f7f29d` | Source-only composition audit; await corrected task/eligible compiler receipts before combined proof. |
| PR407, PR460 and Studio77 | Review and merge open PRs — `01a129ce-181e-7493-b1c4-ac7eb70be726` | Source adaptations applied/reviewed; exact current-source execution and remaining review/platform/browser gates pending. |
| Cross-owner composition and landing | Coordinator | Track source ownership, exact combined evidence, current PR states and ordinary merge identity. |

## Typed asset and runtime compatibility boundary

Typed production integration base is immutable staged tree
`0b5c20a7222a9d171bfcbe35570c184a78f4c8d1`, after reviewed d0 adaptation and
changed-line formatting. Pending original ancestry/index is preserved. SDK owner
independently checked the four-line no-ckdl adaptation, unchanged Graph/native IO
and catalog bindings, strict containment and publication checks. Receipt:
`self-hosted-build-programs/chibi/.chibi/sdk-packaging/typed-integration-0b5-source-audit.json`,
SHA256 `2e524eb99666944a4d89a950bd79f5e7beade41dd9a2407e58df1c90d4b2a08e`.
This is source clearance, not an execution pass.

Current tested successor966dbaccf829cdbb53d2a2988080a38d46aa4a74 changes only
test/tools/build_sdk.c: its relocated explicit-profile oracle checks actual
support-source paths and compiler selection instead of a stale fixed command
count. Narrow SDK/Studio execution results are recorded in the queue above.

The prior generation at tree041c remains unchanged by d0 integration. Root
matched all 19 compiler inputs plus both 37-source asset resource manifests.
Compiler is 12,099,901 bytes/SHA03d7bced; native asset resource14,460,893 bytes/
SHAa0db49cc; Wasm14,458,702 bytes/SHAd6fae379. Generation receiptf9f2a792 retains
strict snapshot, target facts and parity evidence. Focused migrated Studio model
checkpoint521b666/tree19c1 passed one case/261 assertions and meaningful controls.
Studio92 integration passed SDK-values12/163 and the model case on tree33526285;
local ordinary merge466e2cc3 preserves that exact tree. It is clean and unpublished.
Postcommit binary currency and broader acceptance remain unclaimed.

The published .2 native compiler host requires bootstrap.kdl for native
--bootstrap, but its generic --artifact route does not read that contract.
Typed replaces the native contract reader with compiled BootstrapContract and
adds --target-json. Same ABI/protocol numbers do not establish compatibility.
Native recovery and target queries need a matching reviewed release/provenance;
the unchanged generic consumer pin must receive its actual three-platform proof.
Do not restore KDL or weaken contract checks to make old native modes pass.
Compatibility receipt SHA256:
`b317a920d1e79ab367d0ea2b0de5480302b3cc560d056d84c8b1bfc4a34e9266`.

Typed also adds generated asset resource C wrappers inside tools/compiler.
Root verified exact0b5 compiler-self-build.ps1 already copies only bootstrap
resources, runtime pins and manifest-listed .chibi sources, checks source package
membership first and retains the no-native-source guard. The earlier concern
about copying the whole compiler directory does not describe this implementation;
no redundant selector rewrite is assigned. CI also identified artifact calls
missing the CLI's required positive operation timeout; the reviewed correction
is applied source-only, with actual PowerShell execution pending. This is an invocation contract, not a global
test-duration policy. CI/typed owners must reserve workflow files and bind normal
and paired Studio jobs to an immutable compatible Studio source, or prove an
explicit safe merge/publication order.

CI owner sealed three-file compatibility patchaeaa4e46 and clarified
handoff33b450f0 (supersedes retainedff164595), receipt016d3766, under
`.worktrees/ci-sdk-linux-repairs/chibi/.chibi/ci-recovery/typed-host-compatibility/`.
Root matched the sealed hashes, three exact base/candidate files and unchanged
manifest selector/Prepare body. It adds caller-owned invocation supervision,
required generic CLI timeout flags, native recovery/target and source/build
provenance evidence. Runtime pin stays unchanged pending an actual release.
Second independent source review is clear. Exact application966-to-f854 was
audited, and docs-only successor9fa is the protocol9 base. PowerShell/native
execution remains unrun; no source-cleanliness bypass commit is allowed.
Native bootstrap/KDL compatibility and generic artifact-mode ABI compatibility
are distinct: the existing consumer invokes neither --bootstrap nor --target-json.

Local-project migration is prepared source-only at typed
`.chibi/asset-main-integration/local-project-migration/`, source-map SHA256
`11ae2f66c24f23480c279ec31cf17d836e2c6734be0d9fc571e023760746212f`.
Root independently verified all 19 active Terrain Lab non-cache files equal d0
fixture bytes, all typed counterpart hashes, and three unchanged executable
sources. After pair eligibility, create and validate a separate typed candidate;
recheck original hashes and compare evaluated values before replacing anything.
Starter can likewise be recreated separately through public new_project with
its existing name and empty Start scene. Original projects and SDK are preserved.
Modified projects need a separately reviewed old-reader/new-writer bridge with
exact typed values; no retained general legacy converter is claimed. Candidate
creation, value comparisons and launcher/Studio validation remain unrun.

## Compiler, Project and authoring contracts

- Rich catalog/effect/Append changes remain ABI20 work. Real ordered Append
  mutation divergence was reproduced and corrected; focused parity passed.
  Library resolution cases now assert distinguishable runtime values and native
  callback counts. The four focused selectors passed 15 cases/22,837 assertions,
  plus one reversed by-value parameter oracle with 3,611 assertions. Exact restored
  fixture521 and all eight embedded source arrays were verified. Final combined
  gates/resources remain pending.
  This candidate covers the active Starter/Terrain Lab boolean/library/action
  contracts structurally; actual project/default/installed-route proof is still
  required. Authored systems and authored-library named arguments, nested records,
  field defaults, constants, unions and some numeric conversions remain gaps in
  the current rich game path. Authored-export resolution must match the native
  oracle, preserving ambiguous/duplicate export rejection; no new same-library
  overload semantics are implied. The
  active local source inventory has no authored systems, which supplies no
  systems coverage. Existing compiler owner retains those explicit language and
  acceptance follow-ups. Local-project success is separate from general shipped
  default readiness; do not silently narrow the user's usable compiler/library/
  system goal or claim universal completeness. Keep active32/1a594 frozen.
  Future acceptance maps are ignored under compiler
  `.chibi/game-support/combined-d0/future-acceptance/{libraries,systems}.md`.
  Root matched library map044306dc/manifest8db803f2 and all12 cited engine inputs,
  systems map9faba000 and all26 engine source hashes against exact1a594, with
  unchanged index/no unstaged tracked diff. These are proposed unrun cases.
  Exporter source includes systems, but Behavior rejects their roots and Writer
  emits zero query rows; passing the existing negative is refusal evidence.
  Maps preserve native-oracle-first meaningful values, ownership and scheduling
  proof. Final library mapf849c1ca/manifest1fd1ffa0 specifies Sum/Trace bodies.
  Final systems mape36513f8/receipt4bb27dbb incorporates task/query source review:
  zero matching rows differs from manual unqueried attachment, structural loss
  differs from false-guard admission, and actual authored lifetime inference
  requires an after-last-access/wait cancellation/readd case without host binding.
  Private lifetime fixtures are reference seams, not inferred or executed proof.
  Native ABI20 query-header oracles do not import pending first-class query ABI22
  or reconcile independently numbered collection/query wire formats.
- Prepared host accepts source or borrowed closed CHIBICLO, validates artifact
  export and atomic publication, and preserves deadline/cancellation ownership.
  Protocol8 alone proves neither prepared policy support nor additional services.
- Compiler owner owns Project.chibi policy. SDK owner owns declared inventory,
  generated output receipt and staging. Explicit file ownership covers that file;
  directory ownership covers the exact subtree. Preparation receipt binds inputs,
  artifact, entry and actual role/capability witness; it must not depend on the
  final manifest digest or itself. Final compiler-tools.tsv is written last and
  excludes itself. Full SDK unrelated bin/include/lib paths remain permitted.
  SDK owner may implement the separate opt-in ProjectTools adapter in an isolated
  descendant, reserving SDK.chibi/build_sdk.c there; typed live source and current
  CompilerTools remain outside that edit. Concrete draft contract379d9296 is at
  `.worktrees/self-hosted-build-programs/chibi/.chibi/sdk-packaging/project-policy-adapter-59f83.json`.
  Use corrected compiler-owned Project source after its containment repair.
  Current corrected sourcef7987d05 has independent containment review; old59f83 is only
  an interface reference. The isolated adapter may take explicit raw --policy-source
  to bind exact producer input bytes without importing another owner's source.
  Invalid-argument prepared dispatch proves dispatch only; actual readiness,
  compilation and 939/940 capability witnesses remain pending execution gates.
  Future installed-caller draft is frozen in compiler-owned ignored scratch at
  `.chibi/game-support/installed-policy-caller-draft/`, manifest8f9dae13.
  Root matched four draft hashes, exact producerf798 and unchanged compiler32
  source. Project-owner source agreement is clear; six in-memory C cases are
  drafted but uncompiled/unrun. Future caller must reject capture errors, either
  truncation flag and stdout contamination before parsing the complete bounded
  stderr frame. No launcher, cache, default selector or build registration changed.
  Compiler/Project owner also owns the eventual default launcher integration.
  Current typed launcher still identifies its native compiler by resolved path
  and modification time, and doctor reports/runs that executable. After eligible
  compiler and installed ProjectTools proof, update build/run routing, cache
  identity and doctor together against actual Project.Inspect readiness. Keep
  this later edit out of typed's active frozen launcher/tests; no new persisted
  default-selection setting has been adopted.
  A later source audit found an adoption blocker in f798: native assembly writes
  program plus `.resources.json`, while ProjectAssemble publishes only the
  program and leaves its sidecar in scratch. Project owner is preparing a narrow
  successor output/publication contract; f798 is not compile-acceptance evidence.
  Include both outputs in protection, naming, cleanup and previous-result rules.
  Preserve the policy's post-assembly identity check before publication; direct
  final assembly would publish before that rejection as well as before cleanup.
  Native rollback on ordinary error is not atomic multi-file visibility. The
  refined raw pair capability below is approved for ignored source preparation; SDK prep/caller work
  must bind the eventual corrected policy hash and actual complete-output proof.
  Direct-final native assembly proposal was reviewed and declined because it
  publishes before the post-child identity and cleanup failure checks. Keep
  staged assembly and publish both outputs after those checks. Project owner is
  proposing a narrow raw pair transport/rollback contract; commit status,
  postcommit housekeeping, retained backups and cancellation limits must be
  explicit, with semantic policy retained in Chibi. No patch is adopted yet.
  Proposed raw transport assessment is at Project-owned
  `.chibi/project-policy/raw-pair-transport-assessment.md`: one explicit four-path
  integer API with dispositions0 committed/clean,1 uncommitted/restored,
  2 restoration incomplete with retained backups,3 committed/housekeeping warning.
  Independent sharing review is clear. Keep the existing native writer's
  Windows/Web behavior separate from stricter Darwin/Linux host admission and
  exclusive moves; do not infer Node NODERAWFS exclusivity from editor MEMFS.
  The private binding implementation and platform execution proof remain pending.
  Independent refined-seam review is clear; ignored source implementation is
  authorized for conceptual build.publish_pair with those four dispositions.
  Reserve both backup paths/diagnostics before mutation, distinguish empty
  reservations from old-byte backups, and keep owned-placeholder replacement
  separate from no-replace final/restore moves. Status1 restores prior absence
  as well as bytes; status2 includes failed removal with no prior backup.
  Consumed names/ENOENT are normal; committed housekeeping failure is3, never
  rollback. Raw adapter appends reserved diagnostics without another readiness
  call. Implementation is isolated source preparation; live adoption and all
  execution remain pending. Reviewed Project has both staged siblings guard the work namespace before
  and after reservation. Prepublication failure must remove staged siblings
  while holding that reservation, and work last. After attempted publication,
  do no authored cleanup: a consumed staging pair permits another invocation
  to reuse the numbered work name. Source is sealed as below.
  Admission-rejection diagnostics pre-reserve escaped source-pair evidence
  and append it only if the callback
  actually rejects admission and capture permits. Keep retained staging on
  failure and normal successful probe output empty. Pre-entry readiness,
  cancellation and full/truncated capture cannot guarantee recovery paths;
  do not overpromise them. Admitted-kernel status/diagnostic rules are unchanged.
  Early integration review also found CMake-only source registration: new
  support/file_pair.c and host/build_pair.c/private headers must be added to the
  explicit authored graph as well. Typed6ef Targets.chibi SHA39b21289 has explicit
  chibi_support and chibi_build_host source tables. The sealed successor adapts
  that base, private headers and semantic Graph fixtures. Existing StageHeaders
  handles copied private headers without new Configures. sdk/CMakeLists.txt is a cross-owner composition seam:
  preserve the reviewed warm7936 compiler-payload changes while adding native
  pair registration. Exact mixed bases/final combined identities and actual
  graph/native parity remain gates; active32/6ef and SDK51c4 remain untouched.
  Final16-file manifest2b69b942, patch6f4d0366 and importd7dd8403 is source-clear.
  Project522b76ca matches HOST/2 producer; CMake3e6c0a03 preserves warm2a563 and
  Targets0acfafdf mirrors source/private-header/test closure. Root verified all16
  files,21 baseline files plus f798 and both reconstructed patch routes. Root
  policy/build/test review, independent kernel/raw/native-adapter review and SDK
  composition review are clear. Receipt `.coordination/project-pair-source-review-20261023.json`.
  This remains an ignored mixed-base draft: source32 prepared host protocol8 and
  typed6ef protocol9 need explicit eligible combined source. All C/ABI/fault/
  preparedSDK/platform and real artifact/sidecar/export execution remain pending.
  Binding941 is reserved for the proposed private HOST-effect four-string/int
  capability, with all four parameters requiring C strings. Root checked primary
  and available linked build catalogs: existing registrations end at939/940 and
  no941/942 or publish_pair registration was found. Its sealed source is reviewed above.
  SDK and Project owners agreed a separate HOST/2 witness successor with the
  existing ten metadata rows, five checks and a final actual publish-pair check;
  PREPARE/1 remains unchanged. A generated prepared closure must exercise the
  same hash-bound host without source/compiler/core availability. Native C fault
  cases establish error/rollback dispositions separately. Legacy SDK.Verify
  effects stay unchanged. This is source-contract agreement, not a passed proof.
  Full-SDK composition is a separate prerequisite assigned to the SDK owner.
  Frozen ProjectTools47bbe is a fresh-prefix compiler-tools distribution; it does
  not install public bin/include/lib/package.json or copy its --assembly input.
  Current full SDK CMake/package_sdk has no Project artifact/receipt hook. Source
  preparation will keep the existing packager as orchestration and define an
  authored preparation entry over actual unpublished full-SDK stage roles, with
  the actual installed assembly hash. Revalidate owned payload bytes, compiler/
  core closure, witness, receipt and TSV before final package identity. Do not
  invent Graph records for CMake products or blindly overlay another distribution.
  Contract/patch preparation stays in ignored scratch while typed6ef is frozen;
  real composition and public adoption remain unimplemented/unexecuted.
  SDK composition preparation also removes build-only C/.inc from the configured/
  installed semantic compiler payload. Current whole-directory copies include
  native64/wasm32 asset_compiler_blob.inc files of 90,380,720 and 90,367,026 bytes,
  confirmed by root. Trace real source consumers and retain required authored
  modules/bootstrap resources; preserve native build inputs/provenance and keep
  still-needed wrappers available to native builds. Active6ef remains untouched.
  Review caught unsafe warm-cache deletion through an intermediate link; that
  deletion was removed from the scratch draft. Blanket fresh-output-only refusal
  would break legitimate warm builds and is not adopted. SDK owner is assessing
  filtering known native-only cache leftovers during fresh semantic-package copy
  and admission, keeping them inert rather than deleting them. Actual installed
  payload remains strict; parent/link source preservation and ordinary warm-build
  behavior both require coverage. No live cleanup/migration has occurred.
  Warm successor is frozen under `.chibi/full-sdk-composition-warm`:
  selected semantic membership/bytes are compared in source and warm cache,
  exactly four native-only leaves are skipped before stat/hash, and fresh stage
  verification remains unfiltered. New fixture work must preserve both47bbe
  ProjectTools cases and typed's explicit-profile semantic oracleacb833/966.
  Full patch7936ed23, minimal delta2442c355 and manifesta898671b are sealed.
  Root matched all six after-files, exact mixed Git baselines, four predecessor
  delta bases, held predecessor bytes and unchanged47bbe index; both patches
  apply-check. Existing ProjectTools cases and typed semantic command oracle
  are preserved. Independent root interaction review is clear, including exact
  exclusion-before-inspection, strict fresh stage and private test ownership.
  Root receipt is `.coordination/sdk-warm-source-review-20261023.json`.
  All execution remains pending; pair-witness changes are a separate successor.
  HOST/2 SDK successor5313d584/manifest1c26f76d is frozen source-only on that
  reviewed warm baseline. Root matched both exact files and reconstructed them
  from the patch; generated pair calls preserve legacy SDK.Verify effects, with
  parent-observed prepared normal publication in four prior-presence states.
  Independent review found a fixture hold: the HOST/1 control also removed the
  required pair row, conflating version and missing-row rejection. Make that
  control change only the header, preserving all rows, then seal/review a new
  successor. Production probe source has no finding; no runtime witness exists.
  Native941 and corrected Project HOST/2 source/hash remain external gates.
  Header-only successor55c4d44a/manifestab17f31f corrects that one C statement
  (minimal9ee20da1, C89d9cdf7); SDKdd68 remains byte-identical. Root matched and
  reconstructed both patches, preserved predecessors and index; independent
  review confirms strict version and missing-row controls are now distinct.
  Final format-only successor51c42e4a/manifestfbfb7add has C8b7fc47e and unchanged
  SDKdd68. Required clang-format22.1.8 passed by the owner. Root inspected the
  sole formatting delta, reconstructed all three patch routes and matched exact
  final/prior hashes and preserved index. Final source review is clear;
  receipt `.coordination/sdk-pair-source-review-20261023.json`. No actual runtime
  capability witness has been generated and all external execution gates remain.
- Typed owns constructors, source/load/edit/format/serialization and material
  finalization. Shader owner owns flat Facts.chibi schema/consumer preparation;
  compiler owner owns shared semantic-facts producer and nested record support.
  No second resolver, parser, evaluator or semantic catalog is introduced.
  Frozen Facts1f3e1858 and SourceMapbc3e72e1 have source review receipts under
  shader-source/d0-authored-source; SourceMap independent review is clear.
  Compiler-owner semantic agreement for the flat Facts schema is now clear.
  Actual Analyze producer/hooks, packaged closure/facade and runtime-only focus
  remain pending; existing C foundation tests are not Chibi policy coverage.
- First shader fact slice covers imported Float3/TintGain, two calls, alias,
  constants/scalar operations and ranged rejection. Nested records require
  declaration/layout, contextual literals, projection/calls/returns/imports and
  executable-library parity; toggling one declaration guard is insufficient.
- SourceMaterial uses finite exact Float/nominal Float3 defaults, complete bounded
  parameter coverage and closed source identity; initial Opaque opacity is one.
  Chibi lowering feeds existing owned shader IR with provenance. One immutable
  generation binds CPU/GPU/source, and publication preserves the last valid
  snapshot on failure. Native storage/evaluation and GPU publication stay at the
  runtime boundary. Actual authored compilation/render/CPU source inspection and
  Studio interaction remain future acceptance, not facts-schema claims.
- Query/common first composition temporarily retains query's carrier and the
  schema-independent record_valid proof cache, with separate header/schema/
  provenance checks. New collection resolver changes require explicit source
  integration. Its earlier test-permission hold is satisfied by the direct human
  reply, while source/ABI/artifact eligibility remains unproved.
- No pointer/collection ABI transition enters the ABI20 candidate implicitly.
  Latest human lifetime decision in the common-owner chat: a component dependency
  lasts until the whole task finishes, including after its final component access.
  Removing it while the task waits cancels the remaining continuation/completion
  effects, retaining completed effects and without adding try/catch syntax.
  Root verified the four-document overlayf0fe82ef/base6353, patchb5e44aab;
  executable task sources remain identical. It is not executed lifetime proof.
  Future task/rich composition must order synthetic task declarations before
  cached body preparation and include cancellation WRITE in sparse summaries;
  the common owner is preparing that source map, outside the active compiler gate.

## Open PR catalog

GitHub refreshed 2026-10-23: three engine PRs and one Studio PR remain open.
Remote407/c541,460/aa7b and Studio77/854d still report conflicts; their prepared
local integration trees have not been pushed. Remote663/d34 is mergeable but
draft, with Linux and Studio failures on its older head. None is ready from those
badges or source review alone.

| PR | Current source preparation | Remaining decision/gates |
| --- | --- | --- |
| [Engine407](https://github.com/waldnercharles/chibi/pull/407) | d0 source folded into stagedfa24367a; original pending merge preserved. | Current required/operation/exporter/import/Graph/Wasm checks, final artifact/ancestry currency, unresolved review thread/server state. |
| [Engine460](https://github.com/waldnercharles/chibi/pull/460) | Source-only successor3f9ea39c adds three focused Linux workflow/runner/recipe files to unchanged13-path repair3c8deefa on d0. Root matched all16 live/tree/blob hashes and complete patch9fc886e4; independent Linux source review is clear. Historical remote unchanged. | Genuine Linux unpatched-red/patched-green, required current-source groups, matching rebuilt dependency archives/pins and actual clay visual acceptance; typed conversion separate. Static parsing/lint is not an executed Linux proof. |
| [Engine663](https://github.com/waldnercharles/chibi/pull/663) | Native foundation integration e4efa14d is source-clear on d0; 17-file patch7fb61975. Root verified all seven native/test blobs equal reviewed d34, with Facts/SourceMap excluded. | Exact-source focused/required checks and format remain unrun. Foundation can be assessed independently; actual authored material/compiler/CPU-GPU acceptance remains separate later work. |
| [Studio77](https://github.com/waldnercharles/chibi-studio/pull/77) | Studio92 source incorporated in stagedf9b93d7c; original pending merge preserved. | Compatible typed SDK/source pair, actual node reveal/typing/interruption browser acceptance; specifically denied upload/deployment remains prohibited. |

Merged recovery baseline: engine653/662/666 and Studio92. Earlier temporary
validation PRs441 and Studio78–82 were closed without merging; branches/evidence
remain. Do not merge obsolete controls or remove historical branches.

## Landing and preservation rules

Review exact immutable combined source before execution. Run affected checks and
required repository gates, then verify actual committed/merged revision and
artifact identities. Source-tree equality alone does not prove generated revision
metadata is current. Use ordinary merges and expected-head checks; preserve
branches, dirty primary source, owner stashes and pending merge evidence. A clean
CI badge or mergeable status alone does not establish readiness.

The old CI worktree with pending merge at025d is retained unchanged after landing;
do not resume/push that obsolete state without reconciling its owner history.
Replaced C/CMake policy paths should be removed in reviewed increments once their
Chibi replacement passes supported consumers. Native runtime provisioning and
unported native/platform responsibilities remain explicit separate work.
