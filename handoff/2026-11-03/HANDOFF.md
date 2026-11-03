# Continuation details

## Stop state

The user cleared the ongoing goal, then requested only a WIP commit/push with a
handoff and task list. This checkpoint performs no new implementation or tests.
Subagents stopped with usage-limit errors; their source edits are preserved.
A record-view test-generation turn separately encountered an automated content
flag. It was not retried; the owner continued source/schema inspection and
module linking only. Do not infer any new host/unload test passed from that work.

Root repository: `/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi`.
Integration branch before checkpoint: `codex/native-runtime-completion-20261102`.
Root source commit: `85b963c4171ff46c9cdfb193c45e85daa5d3f9da`.
Published PR: https://github.com/waldnercharles/chibi/pull/1331.
Last verified published PR head: `6b46f9261e6c0cf6499a528d5b288520ee08bf53`;
recheck rather than assuming it is current. This checkpoint pushes a separate WIP branch.

The archival checkpoint has integration as first parent and lane checkpoint
commits as additional parents. Its root tree intentionally keeps the integrated
source. That is an archive relationship, not proof of resolved source conflicts.
All referenced parent commits are reachable after fetching this branch.
`LANES.json` identifies the exact original base, worktree, branch and WIP commit.
`uncommitted-work.patch` is precisely the newly checkpointed dirty delta against
that lane's previous HEAD; `committed-lane.patch` preserves its earlier branch
work since the shared integration ancestor. Do not apply an entire historical
lane diff blindly: some earlier changes are already cherry-picked into root.
Use the WIP commit or inspect the saved patches and reconcile the specific hunks.
The held retirement patch is deliberately separate.

## Validation already completed

All times are validation under shared load, not controlled benchmarks.

| Source | Evidence | Result |
| --- | --- | --- |
| 7b05b3eca | evidence/chibi-7b05-composed-check/receipt.json | Candidate 32.70s; 551 units 26.24s; task owners 24.54s; map search 20.60s; 3,563 source blobs/modes unchanged. Single candidate only. |
| a73081162 | evidence/chibi-a730-composed-proof/receipt.json and evidence/chibi-a730-validation-continuation/receipt.json | Stages2/3 35.64/35.30s raw identical; 546 units 28.87s; tasks21.27s. Managed checks completed by explicit fixture-only continuation with original failed attempts retained. Maps131.76s; generations O0/O2 62.30/64.83s. All3,552 original blobs/modes unchanged. |
| 6c492a88f | evidence/chibi-6c49-linux-proof/receipt.json | Stages2/3/4 93.816/94.505/94.365s; original2/3 unequal,3/4 byte-identical. 551 unique tests pass; eight focused O0/O2 executions pass. All3,562 blobs/modes unchanged. |
| 1d5386d9f | evidence/chibi-imported-composed-proof and evidence/chibi-final-nine-preparation | Raw macOS fixed point,530 units; exact nine-family inventory456/456 with no missing/unexpected tests;705 sample sources unchanged. |
| 37e483824 (integrated85b963c41) | evidence/chibi-task-method | Method removal runtime/source/callback suite26.71s; canonical task+reload suite27.33s; scalar tests14.38s. 128 misses allocate0; first shared match detaches1 backing; next unique match0. |
| Held retirement7aa6f842b | evidence/chibi-retirement-preparation | Native command/runner/graph checks, Release build C disabled56.66s; installed SDK91.79s; C facade7.75s. Later d570 reconciliation was source-only. |

Preferred accepted local macOS builder:
`/private/tmp/chibi-7b05-composed-check/candidate/chibi`, SHA256
`50a78622e3012ac3d8b06ac058bf48301536e6d4cc3326490ca16edc394efcb8`.
Accepted a730 fixed-point builder:
`/private/tmp/chibi-a730-composed-proof/stage3/chibi`, SHA256
`15f480276b0b0a20304a87b05f03035ca114ed0e6ed1863d429c3b6807459130`.
Accepted Linux6c49 stage4:
`/private/tmp/chibi-6c49-linux-proof/stage4/chibi`, SHA256
`3ac6dd4a086936b9ef4f60f18efc07dbb11f0576119e7ba11af3d2b02641490e`.
These are intermediate source-bound proofs, not final product/seed acceptance.

The Linux proof completed while the agent was interrupted. Its existing terminal
receipt was read for this handoff: status pass, Docker exit0,551/551 tests, eight
focused executions, final source/hash verification clean. No test was rerun.
Nested-pointer existing logs end with554 compiler tests and a passing managed
suite, but lack a reconciled full terminal receipt for the remaining reload
work; retain that distinction.

## Shared importer design and ownership seams

The unresolved problem is **borrowed Text bytes crossing code-image lifetime**.
Ordinary counted ValueCopy preserves CoW ownership but may leave a literal's
null owner unchanged. An old cell's layout lease protects the old image, not
new literal bytes written by another image. Both ViewSet and whole-array cell
replacement are affected; constructors may receive values from another image.

Use one internal consuming boundary transform after existing once-only value
acquisition and before publication. Keep ordinary counted copies O(1). Text
with a counted owner passes through; borrowed bytes acquire ownership via the
existing text importer. Inline records/unions traverse only Text-bearing fields.
Arrays detach shared backing before rewriting nested descriptors; transfer or
retain other owners exactly once. Preserve roots across allocator callbacks and
resolve canonical addresses after calls that can replace/grow values.

First read existing SnapshotMemo, SnapshotQueue and SnapshotValues. They already
provide typed source-address/length/type memo keys, a work queue and clone
machinery; do not add a duplicate value graph or recursive copier. Prepare
Text reachability in O(types+fields). Runtime work must be bounded by distinct
visited backing edges plus copied bytes, avoiding recursion overflow or repeated
expansion of a shared DAG. Writer-only modules must demand text copy/share,
allocation support and guarded managed/family behavior even without ManagedNew.
A closed mode on an existing private operation must validate compatible types.

Producer's temporary flat-only gate/importer was removed before checkpoint; its
patch is evidence only. Broad collector-copyable view/cell admission remains
held. Existing rootless ValueValid shapes must keep working; the generalized
importer is needed for their confirmed nested Text cases too. Nested maps with
Text must be admitted only where source behavior is established and the owning
map graph is supported. Do not silently widen retained snapshots.

Owned-record WIP changes private APIs: ManagedPayload/ManagedTarget remove the
fields argument; ManagedInstruction takes record ownership flags instead of
fields. Preserve its zero-field-tag distinction between physical Optional Text
and authored same-layout `{int,string}` records (Tagged code37 control).
Coordinate these calls with nested-pointer and producer edits before integration.

Nested-pointer WIP preserves the24-byte descriptor and adds one88-byte cell
(64-byte header+24-byte payload) per escaped pointer variable, one trace.pointer
edge, full semantic identity, exact offset/size/storage validation and image
lease. Reads allocate nothing. Scoped ReferencePlace relays remain scoped;
managed descriptors borrow their actual payload rather than the local descriptor.

Nested-array audit found existing physical outer Array<Record{PArray}> ownership
and tracers reusable; source admission unconditionally excludes PointerStorage
CollectionDecl payloads. No implementation began. Nested-map design requires
collector-aware owner classification and demanded typed tracers, not admission
alone. Existing40-byte map backing prefix has epoch/heap slots reusable before
final ValueDrain; verify rather than assume the proposed metadata contract.

## Integration invariants

- Native opcodes106–112 retain their existing meanings; Count113. No new opcode
  was introduced by the integrated task/map/method work.
- Preserve ArrayRange28 and ArrayAssign29; indexed foreign loan mode4.
- Direct Task.record is a callable ordinal, not a source type index. Preserve
  all storage guards and exact Task WithoutItem identity exception.
- TaskArrayRuntime owns8-byte observer handles; copies/reads retain appropriately,
  invalid-index Set does not retain, and task promises/root callbacks stay typed.
- Preserve canonical RefArrayGC waiting-slot checks, boxed argument TrackSlot
  before constructors, and the `!boxed` waiting-array acquisition guard.
- SnapshotMap5 is the sole map comparison graph shape; retained roots stay
  map-free. Map equality has no same-backing shortcut because NaN must remain unequal.
- Full volatile coroutine publication, typed failure propagation, original-image
  task completion, parent-child cancellation and image-close audits are required.
- Source/schema tags use full semantic identity and reachable type definitions;
  do not invent PointerDecl schema nodes or depend on declaration ordinals.
- Preserve all current fixture registrations and the Windows entry override;
  obsolete negative fixtures are migrated only after actual ordinary/native proof.

## Bootstrap and platform state

c5 package publication and two pins are already complete. Approved14f7 package:
`/private/tmp/chibi-native-seed-14f7-release`, tag
`native-seed-14f7a46a047ab92acfe58398b3cdac25ab7d0312`.
Linux SHA256 `1dd9bf1518645f50f06d4ff1912159945b967e6ad9c7b1f3228fbb32e5d68ab9`;
macOS SHA256 `7b60b3ef3f556f8621b09ed85deb202091c60e022bc3aa6bc062052588e11d38`.
Both accepted raw pairs and485 tests are in the reviewed package. User explicitly
approved publishing that package and advancing its two pins. The package's
original PROVENANCE still says unpublished/unapproved because it is immutable
preapproval evidence; this handoff records the subsequent authorization.
Checkpoint-only instruction prevented execution. Windows remains excluded.

Temporary producer branch `codex/native-windows-producer-20261103`, exact source
`36506138dab6e97ccd9ebc99cd36e8b5eb96dcb3`, Actions run41627603178:
https://github.com/waldnercharles/chibi/actions/runs/41627603178.
Last communicated state was active. Local actual-c5 admission timed out at its
original90-minute bound after controller success8.394s, with no module/output;
source/seed unchanged. That is incomplete, not source rejection. The local
modern-builder6c49 Linux proof does not substitute for published-seed admission.

Before Windows dispatch require whole producer job/run success, unique unexpired
artifact, actual run R/source P/extracted Windows PE hash H. Freeze consumer W
from tested source and use the fully pinned manual ci mode. Do not use a macOS
cross-linked PE hash or accept a partial producer run. Windows fixed point,
complete compiler tests, current matrix, products and actual FreeLibrary audit
are still required. Keep complete source fences, normal CI phases and existing
120-minute Windows bound; do not invent test-duration exclusions.

## Local resources and coordination

Heavy work wrapper: `/Users/cwaldner/Personal/chibi/handoffs/heavy.sh` (six slots).
macOS LLVM22: `/private/tmp/chibi-bba9-proof/llvm22`.
Linux image: `chibi-native:llvm22-amd64`, immutable ID
`sha256:c2936c6e9f67a2bb3bc5869b07e36d7f6cbe87b0af413c1b3f33844ae5722128`,
network none, `/usr/lib/llvm-22/bin`, jobs2, CC/CXX false.
Freeze Git archive+blob/mode manifest before proofs; recheck original sources at
exit. Compiler output basename stays `chibi`; never normalize binaries.
Formal performance benchmarking was held while correctness work used the host.

Related chats: engineering review `01a19d9c-8faf-7780-87f2-b8d6bb5b9023`;
perf/CI `01a19cba-8db8-7041-9a3e-cb5293f1603b`;
root `01a199ec-f55f-7cc0-ab24-fe521b2bbe54`.
The perf/CI chat was told to stop new work/polling and preserve handoff evidence.
Its clone is `/Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/chibi`;
Release reuse commitbc6d971d3 is already integrated as56b3c0368.

Evidence copies are indexed with original paths and SHA256 in evidence/FILES.json.
Source snapshots/build binaries not copied into Git remain at those local paths;
tracked source is recoverable from exact commits. No unrelated workspace,
retired standalone examples/game repository or pre-existing untracked product
proof directory was changed for this handoff.

## Late-arriving performance/CI checkpoint

The coordination chat completed its own checkpoint after the initial root push.
Its exact commit `fb6515433e6c26c16c2ce12a764ba04cbb79d6b7` is retained as an
additional archival parent of this branch, without changing root production code.
Its handoff is available at
https://github.com/waldnercharles/chibi/tree/codex/wip-compile-ci-performance-20261103/handoffs/2026-11-03-compile-ci-performance
or via `git show fb6515433e:handoffs/2026-11-03-compile-ci-performance/`.

That archive retains all21 investigation source tips, readable principal reports
and a SHA-inventoried three-part102.97MB archive of43,968 textual evidence files.
Large generated binaries/IR/process captures remain locally inventoried under
`/Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/evidence`.
Its first-parent production source remains bc6d971d. Last saved hosted producer
snapshot was2026-11-03 12:35:24UTC: run41627603178/job125888813580 in progress.
No later polling, cancellation, dispatch, release, pin changes or bridge trial
occurred. The chat reports no work continuing. The14f approval remains recorded
but unexecuted under the checkpoint-only instruction.
