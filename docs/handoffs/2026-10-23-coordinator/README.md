# WIP Chibi delivery coordination handoff — 2026-10-23

This is a coordination-record checkpoint for cloud takeover. It contains no
engine, compiler, runtime, SDK, asset or Studio implementation changes. It is not
merge, release, provisioning, default-selection or acceptance approval.

Owning chat: **Review Chibi self-hosting work**,
`01a16428-9213-7ec1-b984-a2af4ceced41`.
Handoff requester: **Locate Chibi pointer owner**,
`01a16638-a000-7680-9c95-56053daf001b`, relaying Charles's checkpoint request.

Repository: `https://github.com/waldnercharles/chibi.git`.
Retained development branch: `codex/chibi-coordinator-handoff-20261023`.
Base commit: `d0de4a84ecf50badcab661411470bdf6f3da5fa2`.
The checkpoint commit is the branch tip verified in the handoff response;
`git rev-parse HEAD` after fetching it supplies its identity.

## What to read

- [Owner handoffs](lanes.md): named threads, branches, distinct recovery candidates,
  test status, dependencies and next deliverables.
- [Live inventory](live-inventory.json): full local HEAD, parent, MERGE_HEAD, index
  tree, staged/unstaged/untracked paths and remote branch observations. Captured
  sequentially on 2026-10-23 around 15:24 UTC; this is not an atomic snapshot of
  every owner's working directory. Later owner handoff receipts take precedence.
- [Workspace plan snapshot](workspace-plan.snapshot.md): the coordinator's prior
  detailed source/review/acceptance ledger, preserved verbatim. Its active CI
  queue and compiler "uncommitted" wording are superseded by the corrections below.
- `evidence/*-source-review-20261023.json`: four coordinator-owned review receipts.
  These establish reviewed source identities only; their referenced owner-local
  scratch must be checkpointed by those owners.
- Three `evidence/required-test-*.json` receipts: native/materials passes and the
  projects failure on exact typed tree6ef. Their full logs are owner-local.
- `manifest.json`: bytes and SHA256 for every file in this package except itself.

## Current corrections and execution status

1. The compiler owner has already published **aec822eb5d98cbdf987f81766e33cf02e2b33308**
   on `codex/selfhost-game-support-20261022`, parent d0, exact source tree
   **1a59410022d128108e7744cf2b5641047a44d713**. Do not create a duplicate source
   checkpoint. The owner was separately preparing its evidence/document checkpoint
   and Project draft during this inventory. Verify that owner's final remote tip.
   This source has not passed its v3 load or required checks.
2. Typed tree **6ef6b76e5c7e83b328e2973cc15c42fc79804043**:
   `test-native` passed632 cases/236,737 assertions; `test-materials` passed110
   cases/270,494 assertions. `test-projects` completed with exit2 and one failure:
   `public_asset_constructors_reach_native_readers`, at
   `test/tools/validate_project.c:279`; Constructor scenario reported
   "scene has no entities". Its final selector summary was27 pass/1 fail/329
   assertions. Other negative diagnostic output in the log is not the identified
   failure. `test-language-all` and `test-tools` receipts are absent/unrun.
   Projects log SHA256:
   `88e66e3227f4a5c0b0f86005abe8d209c794493fd3bbbcd885ca51df0cc35695`.
3. CI owner chat separately failed with a **model-capacity system error**. That is
   not the cause of the observed test assertion. The last process inspection found
   no active Chibi test driver or build/compiler host. No owner execution release
   or new lane grant has been recorded; the parent must reconcile ownership before
   another load/build. No new execution is granted by this document.
4. There is no global test wall-clock ceiling or elapsed-time admission rule.
   Preserve real operation, memory, instruction, hardware and frame limits.
   Do not weaken limits, change assertions, or stop tests solely to shorten runs;
   do not infer readiness from duration. No new build or test was launched for
   this documentation checkpoint.
5. Engine main remains d0; Studio main remains
   `df050886dd7721db63b40fdf5435f1cb20ceeb34`. Their prior successful evidence
   applies to those recorded baseline trees, not the pending candidates.
   Compiler-only CI39891496704 rebuilt the compiler on macOS, Windows and Linux
   without a C compiler or CMake, using pinned prebuilt native runtime executables.
   Rebuilding/provisioning those runtime executables remains a separate native
   toolchain task. Do not confuse these two claims.

## Preserved holds

All previous explicit holds, independent review requirements, repository checks
and server protection apply. A WIP checkpoint satisfies preservation only.
The direct request to checkpoint failed/unrun work does not grant acceptance.

- Preserve the dirty primary `docs/design-notes.md`, all owner source/index
  states, stashes, branches, worktrees and pending merge ancestry.
- Do not treat an index tree as the full dirty candidate. Values, collections and
  queries have substantial unstaged/untracked work; their reviewed full-tree
  recovery snapshots differ from the live index.
- Keep compiler v3 load, ordinary host rebuild, bootstrap/resource reproduction,
  five required groups and actual compiler/host parity as distinct gates.
- Keep prepared ABI20, value ABI21 and differing collection/query ABI22 source
  formats distinct. Regenerate and validate a coherent combined target, codec,
  artifacts and resources before claiming compatibility.
- Project/native-pair/SDK drafts are mixed-base source-only preparations. Binding941,
  HOST/2 and matching SDK witness are not present or proven by main's version
  number. Preserve strict failure/rollback/recovery and sidecar publication rules.
- Preserve native default selection. No LLVM default, runtime, pointer semantics,
  foreign ABI, ownership or new effect behavior is approved by this checkpoint.
- Engine407 has unresolved review/server holds. Engine460 needs real Linux proof,
  matching rebuilt dependency archives/pins and visual acceptance. Engine663 remains
  a draft with unrun exact-current-source gates. Studio77 needs compatible SDK and
  actual browser interaction acceptance; its specifically denied upload/deployment
  remains prohibited.
- Retired examples/game repositories remain read-only. Do not restore KDL compatibility
  paths or remove native responsibilities before replacements pass their consumers.

## What is still local or missing

This package deliberately preserves only coordinator-owned records and three
status receipts. It does not publish another owner's implementation or substitute
a hash-only ledger for that implementation. The parent must obtain named owner
handoffs in `lanes.md` before cloud implementation relies on those candidates.

Owner-local dependencies include ignored `.chibi` draft source, patches, manifests,
full logs, runtime/SDK/native/Web binaries, generated resources and caches; historical
preservation under `/tmp/chibi-root-landing-20261022/`; owner stashes; diagnostic
compiler host and native archives that require a normal rebuild; and old semantic
SDK installations. Large native asset compiler include payloads remain native
build inputs, not semantic compiler-package payload.

The compiler owner is exporting its own runners/profile evidence and Project
mixed-base draft. SDK owner must export final warm/HOST2/full-SDK compositions.
Typed owner must export integrated engine source, resources, tests and unpublished
Studio merge. Values/collection/query owners must export the whole working
candidate, not only an older commit or current index. PR owner must export three
adapted candidates with their original holds. No archives were uploaded or
presented as verified remote source by this coordinator.

## LLVM continuation context, not a new implementation assignment

Cloud already has pushed scalar emitter and tool-driver branches (see `lanes.md`).
The diagnostic flat integer transport is not a public semantic contract.
A compiler-owned, versioned scalar projection should use the shared semantic
vocabulary while keeping private Body Code/Slot and lowering bookkeeping private.
The actual producer and invocation-lifetime adapter are unimplemented.

A useful first provider slice is scalar parameters/constants/locals, explicit
types and result identities, arithmetic/comparisons/conversions, calls with known
binding/effects, blocks/branches/returns, immutable source origins, and precise
wrap/check/trap/budget rules. Admit only a coherent supported subset and reject
unsupported refs, tasks, containers, native/host effects and unknown effects.
Unknown is not pure. Do not invent public memory/FFI semantics to make it fit.

Keep the contract in a leaf module so the consumer need not import
`package_compiler`. In one process, prepare frontend, produce an owned result,
release frontend preparation/schema/cache resources, prepare backend and consume
that retained result. This lifecycle is a proposed adapter, not a passed proof.

Measure three things separately:

- **Aggregate prepared-closure admission/storage peak**: retained owned result,
  copies, decoder/scratch/caches and overlapping frontend/backend allocations
  count under unchanged limits. The earlier combined closure failed during
  preparation, before ordinary semantic execution. Two separately fitting
  closures do not establish same-process fit.
- **Owned-result lifetime**: no borrowed frontend storage survives retirement;
  results retain exact type/target/source identity and remain valid through
  backend consumption and cleanup. Verify success, rejection, cancellation and
  repeated invocations.
- **Process RSS**: operating-system resident memory is separate telemetry, not
  the preparation budget, ownership proof, or justification for changing limits.

Cloud owns the emitter/driver consumer and object validation. Parent must name
the cloud provider taking over compiler production/lifetime adaptation; the local
compiler owner's historical file ownership is not a new implementation commitment.
No producer work, scheduling, broader runtime or default change starts here.

## Concise takeover sequence

1. Verify this remote commit and manifest; confirm receipt in the originating
   conversation. This coordinator will make no overlapping implementation changes.
2. Obtain the remaining named owner WIP commits and portable evidence. Preserve
   pending merge parents, staged/unstaged/untracked source and all failed/unrun status.
3. Reconcile the CI failure and execution ownership. Repair the Constructor case
   at its actual source cause, then run affected and required checks on the new
   exact candidate. Compiler v3 and collections focus retain their recorded gates.
4. Integrate one reviewed dependency slice at a time; reproduce matching bootstrap,
   runtime/SDK/native/Web resources and platform evidence before any merge/release.
   Assign the LLVM provider in cloud, agree the leaf contract with the consumer,
   and prove the owned-result lifecycle before broadening its scalar subset.
