# Final combined Studio source review — 2026-10-18

## Conclusion and scope

No blocking correctness finding in the reviewed Studio working tree. This is
independent code review of the complete combined delta against HEAD, including
all new files; it is **not landing approval** or a claim that remaining final
validation has passed. No builds, tests, source preparations, network operations
or shared-source edits were performed by this reviewer. Only this report and its
companion file manifest were written outside the repositories.

Reviewed repository: `/workspace/chibi-studio-outliner`, HEAD
`95ca95bc3ca2da5b899ec06596982ade26d2d94f`. Scope is 12 modified tracked files
plus 13 untracked files, exactly the 25 entries below. The manifest includes the
parent's final two fixture-comment corrections. Supporting engine API/provider
code was read to verify the Studio contract, without editing or claiming a new
whole-engine review. Independent assistance from
`/root/outliner_m3_integration/audit_package_fixtures` checked CMake, extraction,
fixture lifetime and dispatch wiring; no blocker was found there either.

## Findings and resolved review corrections

- **No unresolved blocking source finding.** Public APIs and owning SDK targets
  are used; there is no private engine source include, schema impersonation,
  source-hash authority check, generic model-write escape, or added asynchronous
  publication path in this delta.
- **Resolved nonblocking documentation drift.** The live and guard fixture
  headers described every runtime request as the old per-frame budget. The
  parent corrected those comments to distinguish ordinary controls from shared
  Refresh/Reveal fuel. Reversing only those comments in memory exactly reproduces
  prior reviewed hashes `0da0e553...` and `7137dcda...`. No C token, assertion,
  workload or fixture input changed; prior behavioral measurements remain valid
  for that comment-only delta. Final hashes are in the manifest.
- **Preserved earlier failure.** Live-fuel run1 failed because the fixture treated
  the UI send boolean as execution success. `render/ui.h` and `ui.c` define it as
  payload carryability; dispatch budget failures still return true. The reviewed
  one-assertion correction `94a224d5...` preserves all precise execution-failure
  and model-retention checks. My original fixture review missed that semantic
  distinction. Both the failure and subsequent pass remain evidence below.

## Production review

### Complete publication and model ownership

`studio_outliner_projection.c:563` first verifies the exact retained request,
current runtime proof, same protected state, current mount/controller/source
module, live scene/world/session, and canonical capture scope. It validates the
exact authored Projection and Row layouts and concrete non-null strings,
canonical booleans, and nonnegative integer payloads (revealRow permits -1).
All 12 Row fields are preserved: key, label, icon, prototype, drop, depth,
behaviors, active, marked, hidden, locked and renaming. Projection supplies ten
model fields plus the reveal-row command field.

The full header and row array are converted into one temporary owned document
before any model write. Required values are resolved and authority/currentness
is rechecked immediately before the ten fixed header writes, rows replacement,
and optional reveal command. The fixed paths and prepared values satisfy the
nonrejecting conditions in `ui_set_data`; `ui_reveal_row` uses a fixed nonempty
selector and checked nonnegative row. These functions copy values and do not
call model observers. Thus no recoverable failure can expose a partially
prepared model. Allocation failure follows the existing abort contract, rather
than introducing a partial-success return. The temporary document is freed only
after UI-owned copies exist. Duplicate publication is refused; the read scope
closes before return. An error in the authored tail after accepted Publish keeps
that already-valid model, as required and measured by live-fuel run2.

### Context authority, provider lifetime and reload

Capture obtains authority from the actual runtime-owned live context, resolves
its retained dispatch token and original event/controller, and permits only one
capture. It snapshots exactly the four protected scalar states. Actual applied
module identity, generation, source registry world, entity liveness, mount
origin, edited scene/world pointer and scene-session epoch all participate in
currentness. Source-text hashes never grant permission.

The existing raw provider synchronizes its view cache before borrowing text,
validates full earlier-parent hierarchy, and owns copied facts/strings after
capture. Arrays are copied into activation-owned snapshots. The Scene output
keeps its runtime host-bank backing pointer; only its managed strings are
snapshotted into activation storage, avoiding the previously diagnosed invalid
arena trace of a non-arena record. Runtime tracing visits that host-bank value's
managed fields. Currentness is independent of historical snapshot readability.

All dispatch, tick, settle, unmount, invalidation and retirement boundaries close
read authority. Pure finalized-producer restrictions prevent a callback or
unsafe operation from mutating the scene between Capture and Publish. Nested
runtime entry revokes uninterrupted proof, and pause/resume cannot reconstitute
it. Owned data remains readable after revocation; publication permission does
not. The current same-address scene/world reuse defense includes the scene
session counter, rather than relying only on allocation addresses.

The source guard installs before initial publication and validates the actual
final module set. Initially absent OutlinerRows is permitted; removing an
already-applied producer is rejected. The immutable module validator is applied
to every candidate producer. Edited scene registries install the same guard
with a stable denying service; they receive no mounted publication authority.
The published callback retains the actual committed module. No-op/revert
publication preserves actual retained-module identity, while replacement
advances generation and discards deferred native reveal intent. Guard callbacks
perform no registry mutation or dispatch.

Retirement occurs before mount/world destruction; the adapter and its embedded
service/guard user outlive the source registry. `free_world` retires first,
frees mounts and the preview registry, then releases the adapter. `studio_free`
invalidates while Studio/scene state is still live. Capture snapshots, module
references, request argument arenas and dispatch references have explicit
teardown. The runtime owns pending queue/activation references; completed
requests are reaped only when their token reports no pending execution.

### Events, cancellation and deferred native reveal

Existing UI payload conversion and original host callback contexts are retained.
A stale nonzero mount stamp is discarded rather than reinterpreted against a
new controller. Controls keep typed copied payloads and their order. Only
host-marked internal Refresh requests coalesce/cancel by retained request token;
user controls are not canceled by that mechanism. Unmount revokes binding/read
permission even for a retained user request, so later resumption cannot publish.

Hidden native RevealObject retains only the latest owned typed payload and
records source generation plus scene/world/session. It acquires a current mount
origin only when a view becomes available. The deferred intent is removed before
replay, preserving reentrancy and one-shot behavior, and runs before the
following ordinary view event. A stack-local error copy survives the ordinary
dispatch so deferred failure is not silently overwritten. Replacement/retirement
clears stale intent. The adapter faithfully forwards the carryability boolean;
Studio reports execution errors through the host failure channel.

### Authored policy, dimensions and fuel handoff

The shipped behavior keeps exact state/default/event contracts and synchronous
Capture -> Project -> Publish order; ShownOpen changes only after acceptance.
Reveal forces Collapsed=false before Capture. The library has no mutable state
or effectful host dependency. Its 12 pure helper/body definitions own filtering,
parent depth, icon/drop policy, row flags, byte-based title handling, viewport
arithmetic, behavior counts and exact entity-decimal keys. Array reads use the
checked get capability. All 12 row fields and 11 projection fields match the
schema. Page guards signed extrema before subtraction; actor count leaves room
for the virtual root. Filtering preserves source order and hierarchy ancestors.

The work measurer validates nonnegative dimensions, representable byte lengths,
complete/disjoint field and component spans, valid prototype indices, and
strictly earlier parents. It derives maximum depth in O(N) without recursive
traversal. V uses the monotone shown=N upper bound of exact Page arithmetic and
zeroes when collapsed. The checked formula remains:

```
W = 8 + 3N + 8V + T + 3R + D + 2Cv + 2Fv + Av + l + min(l,127)
total = 10000 + 249W
```

T/R include both actor and prototype text; 3R includes both FoldByte calls.
Repeated prototype use requires V*cmax; disjoint fields justify min(F,V*fmax).
Every intermediate multiply/add is checked, including V*fmax before the min,
K multiplication and bootstrap addition. Failure leaves output untouched and
occurs before Project/Publish. No hypothetical scene-size ceiling is introduced.
The bound concerns authored checkpoints, not native allocation/copying or wall
latency. The frozen K249 sum, complete canonical callee closure and final ABI
identities are durably recorded beside the constant; safe edited policies can
exhaust the finite allowance normally and are not hash-rejected.

Only Refresh/Reveal tokens opt into work. Capture computes the bound from its
owned facts, original reveal payload and protected state, prepares owned outputs,
then rechecks authority/currentness/state/provider before one absolute grant.
The original entry debit is preserved. A second Capture cannot renew the grant.
The same retained token follows source dispatch, helpers and tail execution;
ordinary controls keep their existing frame budgets. Engine tests separately
cover exact spent/grant semantics. Live Studio tests deliberately do not claim
to observe token spend.

## Build, extraction and fixture review

CMake continues to consume installed public Chibi packages. New production
sources are registered once. SDK identity compile definitions cover the studio
library, app and acceptance target, including separately compiled host/registry
units. A replacement package therefore invalidates consumer compilation after
explicit reconfigure even when installed mtimes are older. This is not a claim
that bare cmake --build detects an arbitrarily backdated manifest; explicit
configure after SDK promotion remains required. The acceptance-only native
catalog dependency is explicit. New type-pack registration avoids duplicating
the same pack obtained through the graph catalog.

All four modified files tracked by EXTRACTION.json match their final
consumer_sha256: OutlinerRows, scene_document.c, studio_host.c and studio_ui.c.
Other new/changed files are Studio-owned and correctly unlisted. Producer
historical extraction identities were not rewritten. `git diff --check` was
clean during this review. No claim that the final tree has been committed or
that all formatting/build tests ran is inferred from this source check.

New standalone dispatch branches return before existing scenario dispatch and
add no aggregate workload. The old four-helper fixture retains its calls and
assertions while compiling the enlarged twelve-function library. Pure projection
checks original output storage after module/input retirement; it does not repair
ownership with a test-side copy. Scale scopes end before assertion exits, and
its linear-copy bound remains exact. Live teardown keeps the Rml listener alive
until backend context event detachment. Fuel failure snapshots contain inline
owned diagnostic data; their copies survive clear/cleanup. Each backend is
fully cleaned before the next. All finite admission inventories remain those
previously reviewed, with no signal diagnostics or new failure suppression.

## Observed owner-run evidence and limits

The reviewer read these existing logs; this reviewer executed none of them:

| Log | Result | SHA-256 |
| --- | --- | --- |
| test-studio-work-1.log | PASS 4 cases / 204 assertions / 0.250s | 17082655ad3b55efda12566532cb5f1b946c1996e35738d5971d45d985f2df44 |
| test-studio-live-11.log | PASS 1 case, 12 scenarios / 423 assertions / 2.331s | 8e8ab9686825673249c4a0bbdd972876fbb1891206463ad06b863623334b965d |
| test-studio-live-fuel-1.log | FAIL carryability assertion / 4598 assertions / 3.032s | d501d5275cd9e4e37453a7b057d91d69b6198f3bcd5b35c5906532d5e9c3d2a5 |
| test-studio-live-fuel-2.log | PASS 1 case / 10184 assertions / 7.138s | ebe25c31d45445c115b3ff578b26fb23cb8ec69b3ac974e1454b5deef57ce2e5 |

Live-fuel2 shows actual BYTECODE and SLJIT, each with 8587 backend entries for the
shipped 4096-actor/32-row request, then exact -104 budget failures before Project
and after accepted Publish at matching source ranges. The fixture requires old
model retention at revision11 and valid newly committed model retention at15.
It reports shared_spent=not_observed. All invocations retain the existing60s cap.
Current installed package metadata identifies SDK10 build
`778b78b7b14216ea6f3445b4b3ff23abe0efa609e7ee9d717b88c03c2a52fa27`,
source snapshot `8cf9fcb246917d70fbd688b891ba6880378078cba5a5668a4ff989ffeb96abdf`,
with dirty engine revision ff6d06c8. This is checkpoint provenance, not a final
clean combined-tree identity. Root owns complete binary/link/sanitizer currency.

Final clean-candidate groups, installed/relocated ownership, applicable worker
checks, native rendered startup, browser interaction/manual images and retained
pointer/hierarchy move routes remain separate validation obligations. Their
current status belongs in the root ledger, not a pass implied by this report.
Historical/quarantined full suites remain disabled, not passed.

## Dependency and landing boundary

This review is against the old PR71-derived local HEAD above. Parent's current
remote-state snapshot reports engine420 still open at ff6d06c8 and Studio71 now
draft at `62962adca667f436924548c26c7864b19fa8e860`, with its owner repairing the
439 bridge. That newer work is not absorbed into this review. Parent reports
main unchanged. No remote query or eligibility approval was performed here.
The read local remote-state JSON hash is
`21e02ec122e5268b5b8225f94cbec398fe80882893974afe3b7b423cf317cc9f`.

Landing remains blocked on the separately owned dependency repair/eligibility
and final tested combination. Preserve engine407 and Studio78/79/80 holds,
engine420/Studio71 then77 ownership/order, and exclusion of M4 f328. New parent
code or conflict resolutions require combined review and affected validation;
this report does not transfer approval across that change. Legacy native row
producer retirement is outside this reviewed delta and has not been silently
claimed complete.

## Exact reviewed changed/new file manifest

Companion: `final-studio-files.sha256`, SHA-256
`414ee4d777ab873859f1f1952ed8681f7daccd058d4e8e632ed468e0dbef0b07`.
Paths below are relative to the Studio worktree. These hashes identify all
modified tracked and nonignored new files against the stated HEAD, including
post-format extraction metadata and the final comment-only corrections.

```text
d1955a00a83a0a59a66042533b58aa5ea0073f995d064f77c284fe1c74346258  CMakeLists.txt
165666a640ca20334a2b7c3ef1ee5a72bb42c2d23ab69ad20c5cd5e9dbad880a  EXTRACTION.json
9f43374d9d87088d6c6a171bf530aa5a63e171e791ce23796f8dd447199cb488  cmake/studio_sources.cmake
530ceb49ea65144694a986b57b26829d430725c24dab3f7663617388f80ecc70  docs/evidence/outliner-request-work/census.log
53be43cc40c4730e29ec9987223849d0ab8c526033eb1472920fb53e5087468f  docs/evidence/outliner-request-work/census.md
95af69dc75b23505bae0ebec0d31c3860f47aa8e1c6a8f269c32bf5be7b3a8b1  docs/evidence/outliner-request-work/engine-fuel.sha256
510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450  projects/studio/content/behaviors/OutlinerRows.chibi
e2c727396b2a5004327fcf17cd3e1a8194629c928db75c4927ef6f4cae9c28a5  projects/studio/content/libraries/OutlinerRowPolicies.chibi
3b9b4038626d53c50ec0425304553ade615da0e0cb1e43f31b3bb8bc7b709243  src/editor/documents/scene_document.c
12ccc912c1b73dd8752926b95e801c5f545ca8da397d9800ff999df6e56b5703  src/editor/studio/studio_host.c
f343e1771d312f80f89b74212f287da878860088de7684752178e908a2bb063e  src/editor/studio/studio_outliner_projection.c
6dfe6c50cdce1dfd179c65346f11485c64086116ca11a3f0bfa4d6f5bd12ef47  src/editor/studio/studio_outliner_projection.h
3609c1a1272a9bf0c931e178956f930019819f8ec20e29df9941bdeec0acbdb2  src/editor/studio/studio_outliner_work.c
a5b72db602763b7a7babcec793fcdcac49ae0dcfc169eeaf78c8d23ff96956f6  src/editor/studio/studio_outliner_work.h
bf73ac7f7baf4253601b5e61b3d4ddebb6ba880801b755dc5c61ec4998b69787  src/editor/studio/studio_ui.c
b7681bcf692074d60347c51b23d3ae5a5a5b0c2cc3b549fe29324e6c5ded028a  src/studio_registry.c
231feca8b6b8db16cf4bbe90d3a2d8330a031ad273237ce7d237022f02c370bd  tests/acceptance/README.md
d4fb3fc108f1e3017d936ed77c6aec0bd3736b21e07306991de73daa6c2465c2  tests/acceptance/main.c
55cb80d1e37f1b0ff86a46a7867b3030f8ad4c0c5533effc1d3dd39649be4cfa  tests/acceptance/outliner_live.h
94a224d56f318a65dd9dbdbfc9b75e4367883c442ad039f75e2301f1224dc479  tests/acceptance/outliner_live_fuel.h
b00da912e379095f3b49dbd6c902a12eab59a6bdeb37fa773568d6eb90c68adf  tests/acceptance/outliner_live_guards.h
cbd43bb3087be27f141b14dfdea579cda08d10c5192e8feae01f436de4f8baa3  tests/acceptance/outliner_projection.h
6267c8432f193a93d93c6bd56699fa5052acea9550259de9dcb76892916a7082  tests/acceptance/outliner_row_policies.h
8b3e8b9499ad1a7af1b4c8d13c99b57e75a144e99d6223b3904bdcd3107927d8  tests/acceptance/outliner_scale.h
86e4ba47b7335ba126f66470e94e9613c071337acd7e08951ce4fb772202a874  tests/acceptance/outliner_work.h
```
