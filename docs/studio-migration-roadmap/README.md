# Remaining Studio migration: implementation and PR checklist

Prepared 15 October 2026 from the owner-supplied **Chibi Studio in .chibi, Revision 2**. This is a dependency and coverage ledger, not evidence that its future features are implemented or tested.

## Start here

1. `packages.tsv` is the complete scoped PR/work-package register: 95 packages, with dependency IDs, decision gates, acceptance criteria and plan section references. It includes seven already-owned foundation packages so they are not implemented twice.
2. `roadmap.md` renders that register as a human-readable checklist.
3. `studio-path-coverage.csv` accounts for all 867 paths in the supplied exact-tree inventory. Every product source/header has an explicit primary package; shared responsibilities list additional packages. It retains the original immutable source URL, blob SHA, responsibility, candidate disposition and evidence level.
4. `validation.json` records counts, dependency checks, source fingerprints and mapping completeness. It is document validation only, not an application test result.
5. `source/plan.md` and `source/studio-file-inventory.csv` preserve the complete source evidence (CSV line endings normalized to LF; original fingerprint retained in validation.json). References such as `D6`, `6.6` and `11 Stage 3` mean those headings in the plan.

The package count is a decomposition for coordination, not an obligation to create one PR per package. A package can become a narrow stack when independently reviewable subcontracts require it; several compatible packages can share one PR only if ownership, dependencies and exact-head evidence stay clear. Every package must ultimately link its real PR(s), or carry a reviewed, reasoned not-needed disposition if it is conditional. A draft placeholder is not feature completion.

The `native-reload-audit.md` report verifies that a new native-reload deletion feature is unnecessary at the audited pair. The supplementary `array-storage-design.md` records researched alternatives and external precedents. Its proposed Chibi spellings are not approved syntax or implementation claims.

## Regeneration and maintenance

This is a frozen baseline snapshot, not a live status tracker. The generator does not query GitHub, rerun product tests, or infer current approval. Original source provenance under `source/` stays fixed. `packages.tsv` supplies package definitions; the explicit foundation/status and dependency overrides near the top of `build_ledger.py` are authoritative for this snapshot and are reapplied during regeneration. Update that input block deliberately when recording a coordinator-verified foundation change, rather than editing generated checkboxes or scope text alone. Review both the input edit and generated diff.

From the repository root, the documentation-only regeneration command is:

```sh
python3 docs/studio-migration-roadmap/build_ledger.py
```

It validates identifiers, dependency resolution/acyclicity and baseline path counts while regenerating `packages.tsv`, `roadmap.md`, `studio-path-coverage.csv` and `validation.json`. It also prints a documentation validation summary. It does not build or execute the product and is not a Python product-test framework.

For a clean, unchanged snapshot, verify reproducibility and whitespace:

```sh
python3 docs/studio-migration-roadmap/build_ledger.py
git diff --check
git diff --exit-code -- docs/studio-migration-roadmap/
```

After intentional snapshot edits, inspect the expected diff, run the same generator again, and verify the second run introduces no additional changes. Keep original `source/` fingerprints unchanged. Recheck the 95-package/867-path results and obtain independent documentation review before publication. Product readiness remains governed by its separate admitted C/pico_unit evidence.

The generator refuses before any output write if an existing coverage CSV contains a non-baseline `tracking_status`, a nonempty `implementation_pr` or `exact_head_evidence`, or lacks those tracking columns. This prevents later implementation progress from being silently erased. If it refuses, preserve the recorded fields and first change to a reviewed status-preserving maintenance workflow; do not clear progress fields to bypass the guard. The current empty baseline remains deterministic.

## Scope and status

The committed endpoint is identical Studio product behavior and visuals with portable product data, algorithms, decisions and composition authored in `.chibi`. The SDK remains separate. An old controller hidden behind a new native capability is not a completed migration.

The supplied plan pins engine `02f8453211dd4cc57965ba52c645f08a92e53c5e` and Studio `78cb4d4c47242db9f7af2b9573653d9756050e99`. These are provenance baselines, not an instruction to ignore newer reviewed changes. B00 refreshes and records the actual implementation pair, immutable SDK artifact and baseline environment before new acceptance claims.

At handoff, the coordinator owns:

- [Engine PR #397](https://github.com/waldnercharles/chibi/pull/397), query-variable load IR (R397)
- [Engine PR #398](https://github.com/waldnercharles/chibi/pull/398), scalar component reads (R398)
- [Engine PR #399](https://github.com/waldnercharles/chibi/pull/399), comment-preserving minimizer safety (R399)
- [Engine PR #400](https://github.com/waldnercharles/chibi/pull/400), managed capture retention (RCAP), plus scalar writes (RWRITE), configurable existing-printer candidate (RFMT), and prepared opcode-schema safety (RSCHEMA)

These are ownership reservations. Coordinator verified PR #399 merged as `1de4810d49b3acf4ead7d910d0abd2b042293e18` and PR #400 as `ddc71942aecab39e7b4dd46ad4f8e1ed58f6d1ec` after fresh independent validation; R399 and RCAP are completed foundation work. Schema PR #401 merged as `23cad1c6c58c9e1b3c486ae6ddee1b6291e81deb` after exact-head independent review/tests. Shared formatter SDK foundation PR #402 merged as `64183a1c4ba5b367bf35604ddae45fa616fd3bd0` after independent checks of exact head `3d7a0fed73e0c5630bc0fb71ada4248f8d82d74b`. R397 and R398 explicitly depend on RSCHEMA before landing. This remains a source/dependency inventory. Other current merge/readiness still needs verification. Recheck exact remote heads, reviews, tests and holds before relying on them. The source workspace disappeared during the earlier work. A checksum-verified main checkout has since been recovered. Capture recovery and fresh validation are complete. Scalar-write reconstruction remains coordinator-owned and needs fresh review/tests. The merged formatter SDK emits LF only; configurable line endings remain future F01 scope. This foundation does not complete layout-engine comparison (F00), all caller/configuration integration (F01), or RML/RCSS integration (F02). Do not assume unpublished content or historical test evidence survived intact.

The seven `existing_owned` entries are excluded from new work ownership. Apart from verified PR #399/#400/#401/#402 foundation merges and the completed bounded B06 source/build audit, entries remain uncompleted in this ledger; many may be safely prepared now, but dependent implementation and acceptance must respect the gates below.

## Decisions requiring the owner's choice

The latest request to produce remaining PRs authorizes implementation and stacking. It does not pick an unresolved visible syntax alternative. Four question groups were sent to the owner. The owner approved the exact state/self question and selected inferred captures. Records and storage spelling remain unresolved; the owner requested array-like storage and investigation of automatic layout selection. See decisions.md for verbatim state/self approval and limits. A short answer must not be interpreted as blanket approval of every semantic alternative.

### 1. Records (D1)

- `Row :: struct { key: uint64; label: string; selected: bool = false }` versus `struct Row { ... }`
- Recommended: first spelling, library-contained nominal types, explicit pure defaults with remaining fields required, scalars/handles/strings plus acyclic nested values initially
- Still choose or explicitly document initial managed field kinds and required-versus-implicit defaults. Arrays/maps/unions need full recursive support before admission. No implicit C ABI promise. Opt-in C layout requires a real consumer and separate choice.

### 2. Names, persistence and Format (D10–D11)

- Approved: bare members normally; `self.count` when a local/parameter shadows persistent count; migrate away redundant root-level `state`, retain callable `state` where lifetime differs, and only meaning-preserving Format removal
- In the approved shadowing case, `self.count` selects persistent `count` while a bare `count` selects the local or parameter. Unshadowed persistent members remain bare by default.
- Unresolved extensions: the exact library-instance, system-shared and callable-persistent receiver rules, their precedence and collision diagnostics. These require a scoped specification consistent with the approved core; preserve existing entity `self` behavior and storage identities.
- Root-level redundant `state` removal through migration is approved; callable `state` remains where it selects persistent lifetime. `shared` stays meaningful. The exact implementation/version transition must satisfy D11. Do not reinterpret this approval as adopting every proposed library/shared/callable receiver rule.
- Choose optional named `fn` omission/rejection policy and narrow default Format versus broader explicit Simplify. Anonymous `fn`/capture qualifiers are not redundant named-function tokens.
- Strict changes require parsed source migration and a real affected project; no project version bump for additive syntax or unchanged formatting alone.

### 3. Lambdas (D6)

- Owner selected inferred captures over explicit capture lists. This does not yet choose by-value/ref semantics, callable signature spelling, or retention behavior.
- Recommended safe next design: inferred immutable by-value captures with mutation remaining explicit, subject to the outstanding semantic choice; checked synchronous nonescaping consumers first. Static/borrowed/owned callable categories must remain semantically distinguishable, whether spelled with qualifiers or parameter lifetime effects.
- Initial stored callbacks need a named feature and owner-bound lifetime/reload policy. Recommended: old-owner generation invalidation and re-registration. Do not infer this from a capture-syntax answer.
- Escaping local `ref`, task-producing callable syntax, pinned old-generation closures and direct synchronous host reentry remain separate decisions, not implicit consequences of adding lambdas.

### 4. Packed AoS/SoA storage (D9)

- Earlier alternatives were `buffer<Particle, aos/soa>` and separate `aos<Particle>` / `soa<Particle>` types. The owner instead asked for array-like syntax and whether optimal storage can be inferred. Those alternatives are under investigation, not selected. Logical records should remain storage-neutral.
- Initial plain-field scope and owning model remain to be selected: move-only owner with explicit clone versus typed service-owned handle. The brief user question did not choose ownership.
- Compare boxed arrays, packed AoS and SoA before claiming a benefit. Preserve existing array value semantics. Do not quietly alter ECS component layout, promise contiguous `ref T` for SoA rows, or make SIMD a prerequisite.

### Decisions that should wait for evidence

- D2: `dict<K,V>` versus changing current indexed `map<T>`; builder API only if automatic safe reuse is insufficient. Evidence for a real lookup/index/construction consumer comes first.
- D3: imported aliases versus current qualified library names plus necessary visibility metadata. Prove the existing multi-module contract first.
- D4/D5/D7/D8 do not require speculative new grammar: concrete existing unions, current RML/RCSS/controller composition, inferred optimization and registered typed events/handles are the recommended compatible first path.
- Native exceptions, revised product behavior or parity masks need specific disposition. Numeric budgets need measured baseline and agreed target; they are not syntax choices.

### Current-syntax check

GitHub `main` grammar was reread on 15 October 2026. Blob `ba6b8d5b6bb31dac14c5fa778e3e79a000453531` still has behavior/library/system/component roots; optional root `state`, explicit callable-state storage and optional named `fn`; no authored struct-declaration, lambda-expression or import-alias rule. See [inspected grammar snapshot](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js). The grammar check refers to engine snapshot `02f8453211dd4cc57965ba52c645f08a92e53c5e`, not an automatically updated main link. This establishes that the examples above are proposals at that checked main snapshot, not compiled feature claims. Unpublished branches must still be checked before implementation.

## Work that can proceed while answers are pending

These are runnable scopes once source/toolchain access and required test admission are actually available; they are not a claim that all builds can run in the current environment.

1. **B00–B02:** source/SDK oracle, exhaustive feature/command/ownership matrix and admitted selector map. Baseline source analysis can begin immediately; actual health checks wait for recovered tools.
2. **B03–B04:** differential trace/recording architecture and missing attribution; preserve three-way comparison and single-owner side effects.
3. **L00, L07, L14, L24:** existing semantic conformance, module-scale specimens, lifetime/FFI/string/task/generation retention, and representative game/effect tests. Do not duplicate RCAP.
4. **L16–L18:** verified operation descriptors, bounded typed-call/native/Wasm import design and safe query-region optimization after existing foundation dependencies. No visible syntax needed.
5. **L04, L20:** semantics-preserving collection reuse and proven redundant-snapshot elimination using existing source forms, with scaling/alias/error tests.
6. **F00, F02 and safe parts of F01:** formatter corpus/comparison and shared UI/layout integration; RFMT is a merged LF-only SDK foundation; the comparison and full caller/dialect integration remain open. Safe root-state/self work may use the exact new approval; unresolved receiver scopes and callable/format subcontracts still need resolution. Candidate tool installation needs its applicable permission.
7. **B06 is now complete:** source/build/Studio-host checks confirm native app replacement already removed at the audited snapshots. No deletion PR is needed. Preserve authored reload/publication/callback safety, checkpoint recovery, generated-C dynamic-loading tests and retired-option cleanup. See `native-reload-audit.md`.
8. **K00–K07 design and recording adapters:** concrete existing-union outcomes and narrow typed documents/UI/files/jobs/runtime/platform/collaboration interfaces. Do not create opaque native product-policy wrappers. Production dependencies still apply.

No broad panel translation while waiting. A disposable API/language specimen is Stage 1/2 evidence, not a completed Stage 3 feature.

## Dependency and stacking policy

Core order:

`B00–B05 → relevant Stage 1 contracts/optimizations → K00–K09 → P00–P04 complete Outliner → P05–P11 lower-risk features → P12–P18 document authority → P19–P26 complex editors → P27–P36 runtime/workspace/services → P37–P38 final cutover`

This is a partial order, not a ban on independent design/measurement work. The table declares direct dependencies. Record which concrete data/collection/callable/storage features each consumer actually uses; add dependency edges when source design shows a new need. Conditional feature implementations are not mandatory dependencies merely because a language feature is attractive. Service families are incremental: jobs/collaboration transport need not block the first Outliner-capable synthetic feature if that feature does not consume them; later consumers retain their explicit dependencies.

- Use one integration owner for cross-repository changes. Land the engine capability first, then pin an immutable SDK in Studio. Never select whichever SDK happens to be latest.
- Stack PRs only on eligible, reviewed dependency content; explicit holds must not enter through a base or cherry-pick. An unapproved syntax branch is not a workaround for a decision gate.
- Keep feature-local native/authored switches only as transitional validation; one side owns mutations. Remove each old policy path once that slice's evidence is accepted, preserving rollback oracle outside shipped duplicate product code.
- A stacked PR must identify its parent and incremental diff. Rebase/merge/conflict changes invalidate affected prior exact-head evidence. Independently review/test the actual integrated candidate.
- Owner-authorized autonomous merging requires coordinating-worker confirmation that required independent review, addressed material findings, admitted tests, applicable builds and server checks pass for the exact head with clean integration. The phrase “all of three comments” is not a three-comment cap and never permits ignoring unresolved findings. Retain branches. No protection bypass or release of unrelated holds.
- PR status is not feature readiness. An open draft that honestly records blockers satisfies publication progress only; completion requires the package and stage gates.

## Acceptance gates applied to every package

**G0: Source/provenance.** Exact base/head/tree and dependency pair, current holds, immutable SDK/assets/toolchain and actual backend/target identified. Relevant original outputs captured before engine changes. Reconcile touched native functions and imports, not just filenames.

**G1: Review.** Independent review covers the exact candidate and affected contract interactions. Address every material finding or obtain its explicit accepted disposition. An approval on an earlier head is insufficient for changed semantics. Update PR scope/tests/blockers accurately.

**G2: Tests/builds.** Only admitted C/pico_unit selectors through the unchanged hard 60-second production supervisor. Build/configure time separate. No aggregate/quarantined/native/render/Studio suite reconstruction from chunks. Required unavailable affected-contract or server coverage blocks landing unless an admitted replacement/explicit scoped decision is valid. Distinguish passed/failed/not-run. Language work compares reference bytecode with actual SLJIT and actual Wasm execution, not an inspection fallback.

**G3: Semantic/lifetime.** Positive and negative/fallback cases, exact error/evaluation order, numerics/IDs, ownership/snapshots/ref aliases, effects/threading, structural invalidation/reacquisition, owner close/cancel/reload, immutable generation compatibility and last-valid-draft isolation.

**G4: Product parity.** Stable state/document/history/action traces plus actual platform input/focus/capture/order and exact deterministic same-platform pixels. Freeze fonts/time/seed/DPI/backend; approve any nondeterministic mask. Fake external effects in shadow paths and test real persistence separately with one owner.

**G5: Performance.** Representative real small/typical/stress distributions, cold preparation versus warm execution, allocations/copied/retained/peak bytes and source attribution, host calls/UI work, no idle regression or unexplained asymptotic/memory-retention regression. Numeric budgets follow measured evidence; no unsupported whole-app speedup inference.

**G6: Tooling/recovery.** Parser/analyzer/lowering/formatter/graph/debug/source maps/serialization/reload agree. One editor undo, atomic writes, invalid draft handling, compatible schema identity, recoverable staging and truthful errors. All authored examples compile under admitted checks.

**G7: Cutover.** Remove touched old C policy and duplicate routes; provisional native labels require function-level justification. Preserve a reproducible oracle and recovery route. Final default switch requires exact shipping-pair acceptance and platform coverage; unavailable coverage is never silently passed.

## Full measurement matrix

Every workload in plan 7.2 must have a claimed scope/evidence or explicit blocked/not-supported disposition:

- Starter and real-project cold load/compile/link/startup
- Idle shell; visible versus hidden panels
- Large Outliner filter/select/reorder/scroll
- Assets grid/list/search/thumbnail arrival
- Multi-select Properties and continuous scrub
- Code typing/completion/diagnostics
- Library edit with many dependents; old/new generation peak and reload pause
- Large graph pan/zoom/select/live debug
- Designer/Inspector live edit/invalidation
- Play/pause/step/stop/keep-changes and world/task lifetime
- Save/reload/import/export/failure/retry and exact bytes
- Long repeated edit/open/close/reload reclamation
- Sparse/dense moving-entity queries
- Shared-target projectiles and ordered damage/reduction
- AI/task swarm with cancellation/despawn
- Spawn/despawn/hierarchy churn and stale references
- Hot strings/arrays/records/dictionaries and algorithmic scaling

Long-session or platform proof that cannot run under current admission remains a readiness blocker to resolve; this checklist does not authorize a long test or quarantined capture.

## Product edge-case matrix

Plan 12 is normative coverage, not a set of optional extras. Feature packages must preserve: shell/menu/palette first-open and disabled/check states/remaps/focus; Outliner filtered ranges/hidden-lock-spawn/rename-path/cycles/reorder/duplicate views; asset modifiers/virtualization/stale thumbnails/import failures/batch undo; Properties exact mixed/computed fields/scrub/typing/cancel/overrides; code IME/Unicode/Vim/invalid drafts/typing groups; graph key/pointer/search/wires/refactor/hidden input; debugger real breakpoints/alias IDs/paused edits; Designer/Inspector one history/slots/styles/live stale targets; runtime camera/gizmo/collider/nonuniform transforms/keep-changes; persistence in-flight newer saves/retry/quit/exact export/revision isolation; collaboration rejection/replay/stale revision/resync/checkpoints/concurrent close; lifecycle mount/reload/replacement/guest/long tasks/shutdown/disposal.

## Conditional scope and explicit non-goals

**Conditional, requiring consumer/evidence and sometimes owner syntax choice:** dictionaries, explicit builders, alias/import extensions, full authored generic specialization, owned stored callbacks, task-producing closures/direct host reentry, intermodule scheduling extension, SIMD, optional formatter/editor adapters. Once a dependent real feature proves the need, complete the relevant language work rather than burying it in a C facade.

**Requested but measured and not a universal Studio prerequisite:** packed AoS and opt-in SoA. They can proceed alongside later Studio work; no unrelated panel must wait for optional SIMD or storage refinement. Source choice and owning contract remain gated.

**Outside the committed roadmap unless a new concrete requirement is approved:** LLVM/new compiler backend, package registry, JSX/virtual DOM/hooks framework, arbitrary OS threading or arbitrary-loop automatic parallelization, inheritance, cyclic object heap, unrestricted pointer arithmetic/FFI, exceptions, dynamic plugins, general operator/trait/macro expansion, field-SoA replacement of ECS internals, recursive SoA flattening/AoSoA, generic C-layout promise, live-C hot replacement, universal AOT packaging. Binary buffers/batched physics-render-audio-network/serialization evolution/AOT/cost assertions are workload-driven capability backlog, not silently scheduled platform projects.

Existing named functions, unions, tasks, ECS queries/effects/reductions, bytecode/SLJIT/Wasm, staged publication and source tooling should be strengthened, not redesigned. The four whole-file native candidates do not preapprove retention of the surrounding product logic.

## Completion accounting

- Every committed/committed-measured package has a real PR or stack and evidence; every conditional package has a named consumer implementation or a reasoned not-needed disposition.
- Every original path is still present, migrated/deleted, retained as a supporting test/build/resource, or covered by a reviewed exact native exception. Current-tree additions must be appended; the 867-path baseline is not permanently exhaustive after implementation grows the tree.
- Every native product function has one current authority; no stale `studio_sources`/node-call or opaque SDK facade retains old policy.
- All seven documentation deliverables from plan 9 exist: friendly Learn Chibi/current capability index; normative language/change log; Build a Studio feature; Write fast game code; generated native API reference; tooling/debugging guide; list/inspector/cancellable job/ordered combat/small-game cookbook. Generated docs drift and examples are checked with C dispatchers.
- Studio cutover (P38) is separate from finishing every wider language workstream. Optional/unconsumed storage refinements cannot arbitrarily delay a proven product cutover, but the user's request for all remaining features still requires their PR/disposition accounting.

No product code was edited, product test suite run, benchmark executed, PR published or merge performed while generating this ledger. Documentation-only validation and commit are separate from product readiness.
