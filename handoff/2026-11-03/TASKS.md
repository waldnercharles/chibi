# Task list

## Completed and integrated

- [x] Make the SHA-verified native seed the normal build entry; native failure
  stops the build. The remaining explicit recovery/VM route is held for deletion.
- [x] Implement the authored native compiler/CLI/build graph, installed compiler
  and asset companion, compile-only public facade, native command contract and
  Release compiler reuse. Earlier product proofs are in PLAN.md and evidence.
- [x] Implement program-owned tasks, parent-child cancellation, typed failures,
  task results, task-observer arrays, and original-generation completion after
  Edit and Continue.
- [x] Implement traced managed pointers, pointer-array cells, numeric/bool pointer
  views, boxed argument roots, canonical waiting array references, record arrays,
  optional Text cells, maps and nested map mutation/iteration.
- [x] Integrate structural contains/remove_item for arrays of maps (`7b05b3eca`).
- [x] Integrate method-form remove_item through canonical array ownership
  (`85b963c41`, lane original `37e483824`). Actual source, ownership, callback,
  allocation and generation tests passed in the lane.
- [x] Complete macOS 7b05 candidate checks: 551 compiler tests, full task-owner
  and map-search suites. This was a single candidate, not a repeated fixed point.
- [x] Complete a730 macOS raw fixed point, 546 units, task suite and explicit
  continuation of managed/map/generation checks after obsolete test corrections.
- [x] Complete 6c49 Linux raw stage3=stage4 fixed point, 551 tests and eight O0/O2
  source/entry runs. Preserve the original unequal stage2/stage3 pair.
- [x] Complete the earlier nine-family sample inventory: all 456 tests at 1d538.
- [x] Register 36 Windows source/entry cases and actual typed-failure DLL-close
  audit. Current Windows runtime acceptance remains outstanding.
- [x] Publish the previously approved c5 Linux/macOS package and advance both pins.
- [x] Prepare, audit and receive approval for the 14f7 Linux/macOS seed package.
- [x] Preserve held host/VM/recovery deletion `d570cf7ba` with unchanged 309
  deletion paths, root source/PLAN and all 36 Windows cases.
- [x] Audit registered native rejections against actual ordinary compiler
  behavior; distinguish confirmed live gaps from ordinary-rejected forms.

## Implemented or partly implemented, not integrated

- [ ] Review and integrate **nested managed pointers** checkpoint `294d6297e`.
  Source/data/API/performance review clear; normal/forced/callback checks pass.
  The preserved logs end with 554 compiler tests passed and the managed suite
  passed. Final lifetime review and the task/generation suite's exact terminal
  status still need reconciliation. Do not invent a complete receipt.
- [ ] Finish **nullable and record-target pointer views** checkpoint `d5d556e8e`.
  Frontend/carrier changes and PointerElements fixture are saved. Ordinary used
  controls pass; complete native proof/schema/reload validation is pending.
- [ ] Finish **owned flat record cells** checkpoint `24ee31397`.
  Source, tests, ordinary 1/1 and eight lifetime modes were prepared; final
  source-image/metadata/ownership controls need receipt reconciliation. Integrate
  with the shared importer, not a duplicate permanent flat-only implementation.
- [ ] Finish **collector-bearing record-array views/cells** checkpoint `2efca40ed`.
  Metadata/tracer review clear under an explicit importer hold. Actual source
  O0/O2 modules link, semantic tags distinguish pointee layout changes. New
  image host has not run. Broad admission is not safe to land until import works.

## Designed or audited, implementation still required

- [ ] Implement ONE generalized **boundary importer** for Text ownership before
  publishing a value into a cell or view that may outlive the writer image.
  Actual existing rootless Row{labels:string[]} and Row{leaf:Leaf{label:string}}
  programs expose the gap, as does collector Row{pointer:^int,labels:string[]}.
  Cover element writes, whole-array replacement and constructors receiving values
  from another image. Reuse existing graph/memo/queue machinery. No importer
  production edits were made before the interruption.
- [ ] Support nested pointer arrays (`^int[][]`) using existing collector-copyable
  carrier records and typed roots; source audit complete, implementation not begun.
- [ ] Support nested maps with managed-pointer payloads. Admission alone is unsafe:
  map backing tracing/ownership/root coverage is required. Design only so far.
- [ ] Preserve whole optional backed/pointer/view cells and optional
  collector-bearing view parameters/results. Exact ordinary-positive controls
  are saved in the closure audit.
- [ ] Extend owned record cells with backed fields where confirmed by ordinary
  source, using the same importer and exact nominal/field-tag rules.

## Release, platform and retirement work remaining

- [ ] Execute the **approved 14f7 package publication** to
  `native-seed-14f7a46a047ab92acfe58398b3cdac25ab7d0312`, then advance ONLY its
  Linux x64/macOS arm64 pins. Exact reviewed package remains at
  `/private/tmp/chibi-native-seed-14f7-release`; metadata, verifier and proposed
  pin patch are saved here. Recheck immutable hashes and existing remote state.
  Do not publish Windows or claim final product acceptance from this seed.
- [ ] Recheck Linux producer run 41627603178. Last reported state was active;
  its old c5 local admission timed out after 90 minutes, without source rejection.
  Use only a whole-successful producer job and its unique unexpired artifact.
- [ ] Freeze a tested Windows consumer revision and pin producer run/source/PE
  SHA identities before dispatch. Run raw fixed point, complete current compiler
  tests, all cases, products and real typed-failure FreeLibrary audit.
- [ ] Validate final combined language/ownership changes and native installed
  compiler, CLI, SDK, asset companion and live optional browser facade on supported
  platforms. Earlier receipts prove their named sources only.
- [ ] Reconcile and apply held retirement after parity and platform acceptance.
  Delete obsolete aliases/flags/test callers/recovery seeds/VM hosts together.
- [ ] Prepare and obtain approval for any final additional platform seed package,
  then publish/pin it. The 14f7 approval covers only that exact intermediate package.
- [ ] Publish the final integration to PR #1331 and complete its checks/review.
  This WIP checkpoint does not merge the PR or change its original branch.

## Boundaries that remain unchanged

- [x] LLVM 22 minimum; tasks belong to the program; parent-child cancellation;
  suspended tasks finish in their original generation after Edit and Continue.
- [x] Archived engine/render/game/Studio services stay archived. No obsolete
  C session ABI adapter or Studio restoration is required by this scope.
- [x] Character pointers, audited foreign managed-pointer forms, view fields,
  readonly view mutation, retained addresses and unguarded nullable dereferences
  remain rejected where the ordinary compiler rejects them too.
- [x] No fabricated dangling-pointer execution or forced premature image unload.
  Use ordinary source, valid ownership checks and the existing safe reload APIs.
