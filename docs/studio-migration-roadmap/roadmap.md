# Dependency-ordered PR checklist

Only coordinator-verified merged R399/RCAP/RSCHEMA/RFMT foundations and the bounded read-only B06 audit are checked. F00/F01/F02 remain open. Other existing-owned work is reserved to its coordinator; current PR/head/evidence status must be rechecked. Dependencies include preparation and readiness gates, not just code imports. Refer to README.md for universal G0–G7 gates, decisions and policy.

## [x] RSCHEMA: Prepared opcode schema identity

Stage: 1A · Repository: chibi · Scope: existing_owned
Dependencies: none (preparation can start)
Decision gate: none beyond existing contracts and applicable admission

PR #401 merged as 23cad1c6c58c9e1b3c486ae6ddee1b6291e81deb after exact-head review/tests verified by the coordinator. Schema-safety foundation is complete; R397 and R398 retain explicit RSCHEMA dependencies and still require their own refreshed exact-head integration evidence.

Acceptance: Prepared producer/consumer mismatch rejection, unchanged existing ordinals, all backend/serialization checks on exact integrated head.

Plan evidence: 6.6 publication safeguards
PR #401 / merged integration head 23cad1c6c58c9e1b3c486ae6ddee1b6291e81deb / exact-head independent review/tests verified by coordinator; consult PR evidence.

## [ ] R397: Checked query-variable load

Stage: 1A · Repository: chibi · Scope: existing_owned
Dependencies: RSCHEMA
Decision gate: none beyond existing contracts and applicable admission

Existing PR #397; reconcile publication and exact-head evidence with foundation coordinator. Do not reimplement.

Acceptance: Current review/tests/merge status independently verified; preserve fallbacks and bytecode/native/Wasm parity.

Plan evidence: 6.6;11 Stage 1A
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] R398: Checked scalar component reads

Stage: 1A · Repository: chibi · Scope: existing_owned
Dependencies: R397,RSCHEMA
Decision gate: none beyond existing contracts and applicable admission

Existing PR #398; reconcile publication and exact-head evidence with foundation coordinator. Do not reimplement.

Acceptance: Missing/stale entity, layout, effects, escapes and suspension preserve checked-path behavior; actual backend execution.

Plan evidence: 6.6;11 Stage 1A
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [x] R399: Comment-preserving minimizer safety

Stage: 1B · Repository: chibi · Scope: existing_owned
Dependencies: none (preparation can start)
Decision gate: none beyond existing contracts and applicable admission

PR #399 merged as 1de4810d49b3acf4ead7d910d0abd2b042293e18, verified by coordinating worker. Its safety fix is complete; broader Format policy/integration remain separate packages.

Acceptance: Exact-head admitted native/Wasm/tooling tests and independent review; no broad Format policy inferred.

Plan evidence: 8;D10
PR #399 / merged integration head 1de4810d49b3acf4ead7d910d0abd2b042293e18 / merge verified by coordinator; consult PR evidence for exact validation.

## [x] RCAP: Managed capture storage reclamation

Stage: 1B · Repository: chibi · Scope: existing_owned
Dependencies: none (preparation can start)
Decision gate: none beyond existing contracts and applicable admission

PR #400 merged as ddc71942aecab39e7b4dd46ad4f8e1ed58f6d1ec, verified by coordinating worker after fresh independent validation. Capture-retention fix is complete; do not reimplement.

Acceptance: Repetitive managed overwrite, task-owner release, cancellation and snapshot preservation; bounded retained bytes.

Plan evidence: 6.1D;6.4;11 Stage 1B
PR #400 / merged integration head ddc71942aecab39e7b4dd46ad4f8e1ed58f6d1ec / merge and fresh independent validation verified by coordinator; consult PR evidence.

## [ ] RWRITE: Checked scalar component writes

Stage: 1A · Repository: chibi · Scope: existing_owned
Dependencies: R398,RSCHEMA
Decision gate: none beyond existing contracts and applicable admission

Prior unpublished source is not currently recovered, only metadata. Coordinator owns reconstruction after schema identity; fresh exact-head reviews/tests required.

Acceptance: Same-value, range/type, dirty/version/ON_SET, stale/missing entity and compound evaluation/error ordering; all backends.

Plan evidence: 6.6;11 Stage 1A
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [x] RFMT: Configurable current-printer candidate

Stage: 1B · Repository: chibi · Scope: existing_owned
Dependencies: none (preparation can start)
Decision gate: none beyond existing contracts and applicable admission

Shared formatter SDK foundation PR #402 merged as 64183a1c4ba5b367bf35604ddae45fa616fd3bd0 after independent checks of exact head 3d7a0fed73e0c5630bc0fb71ada4248f8d82d74b, verified by the coordinator. Output is LF only; configurable line endings are not implemented. This foundation does not complete F00 comparison or full F01/F02 caller/dialect integration.

Acceptance: CST/comments/determinism and supported width/indent configuration verified for the merged SDK slice; output remains LF only. Future line-ending configuration remains F01 scope, and corpus/layout-engine choice and remaining caller integration stay separate.

Plan evidence: 8.1
PR #402 / merged integration head 64183a1c4ba5b367bf35604ddae45fa616fd3bd0 / independently tested head 3d7a0fed73e0c5630bc0fb71ada4248f8d82d74b / LF-only foundation; F00/F01/F02 remain open.

## [ ] B00: Freeze source and SDK oracle

Stage: 0 · Repository: both · Scope: committed
Dependencies: none (preparation can start)
Decision gate: none beyond existing contracts and applicable admission

Record refreshed exact engine/Studio/source trees, SDK checksum, assets/fonts/toolchain/platforms; identify baseline health including historical font-variable report; preserve historical branches.

Acceptance: Frozen original SDK/native Studio works reproducibly or exact defect has owner disposition; no unapproved oracle correction.

Plan evidence: 2;3;7.1;11 Stage 0;Appendix B
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] B01: Reconcile responsibility and parity manifest

Stage: 0 · Repository: studio · Scope: committed
Dependencies: B00
Decision gate: none beyond existing contracts and applicable admission

Reconcile 155 product sources, 62 headers and all support paths; inventory 44 panel-source contracts, commands, input states, document/history/async flows and actual shared-vs-instance view state.

Acceptance: Every path has a function-level owner before conversion; source/build counts reconcile; provisional native labels are not exceptions.

Plan evidence: 3;4;5;11 Stage 0;Appendix A
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] B02: Admit bounded evidence selectors

Stage: 0 · Repository: both · Scope: committed
Dependencies: B00
Decision gate: none beyond existing contracts and applicable admission

Map each acceptance need to independently meaningful C/pico_unit selectors and real native/Wasm execution; request required admission without recreating a quarantined aggregate.

Acceptance: Unchanged 60-second supervisor and repository policy; held-oracle assertions reused only through admitted routes; unavailable capture/soak remains explicit gate.

Plan evidence: 7.3;12;Appendix A tests
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] B03: Build three-way parity harness

Stage: 0-2 · Repository: both · Scope: committed
Dependencies: B00,B02
Decision gate: none beyond existing contracts and applicable admission

Frozen native/original SDK versus native/candidate SDK versus authored/candidate SDK; record stable IDs, bytes, states, errors, event ordering and real input plus deterministic per-platform captures.

Acceptance: Only one external-effect owner; candidate side uses recording/fake files/jobs/network/dialogs; deterministic time/seed/font/DPI and approved masks only.

Plan evidence: 3;7.1;11 Stage 2;12
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] B04: Complete performance attribution

Stage: 0-2 · Repository: both · Scope: committed
Dependencies: B00,B02
Decision gate: none beyond existing contracts and applicable admission

Measure startup/idle/interactive/cold compile and warm runtime; add missing crossings/copies/retained captures, strings, generations and UI phase attribution; preserve existing profiler meanings.

Acceptance: Raw distributions/provenance, actual compiled-backend assertion, paired uninstrumented headline runs; budgets based on observed native behavior, no invented percentage.

Plan evidence: 7.1-7.2;6.6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] B05: Maintain native exception and cutover ledger

Stage: 0-8 · Repository: studio · Scope: committed
Dependencies: B01
Decision gate: none beyond existing contracts and applicable admission

Function-level authored attempts, language improvements considered, irreducible mechanism justification, effects/ownership/threading and reconsideration criteria for 29 SPLIT and four candidate native files plus extracted adapters.

Acceptance: No unexplained product-policy native retain; built manifest and compiled imports agree with allowlist; evidence updated for every slice.

Plan evidence: 5;Appendix A completion gate
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L00: Specify core semantics and conformance

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: B02
Decision gate: none beyond existing contracts and applicable admission

Versioned normative lexical/names/types/ownership/execution/task/ECS/effects/ABI/reload/tooling rules; fix stale reducer/reload/project-version docs using actual callers.

Acceptance: Positive/negative cross-backend examples; 64-bit/float/overflow/bounds/order semantics; docs distinguish implemented from proposed and current from historical.

Plan evidence: 9;6.2
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L01: Implement plain nominal records

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: B02,L00
Decision gate: D1

After approval, implement library-contained declaration, construction/defaults, field-wise equality, ref mutation, acyclic nominal identities, natural target alignment and plain field descriptors.

Acceptance: No new native C registration per record; required/default evaluation order, nominal mismatch/cycle rejection, native/Wasm/bytecode parity and Studio plus game specimen.

Plan evidence: 6.1A;D1
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L02: Complete managed record ownership

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: L01,RCAP
Decision gate: D1 managed scope

Selected strings/names and acyclic nested records with recursive snapshots, argument/return/event/task ownership; containers/unions only if their complete paths are in approved initial scope.

Acceptance: Producer mutation/destruction, document replacement, alias/ref, cancellation and reload cannot change or dangle saved values; unsupported fields diagnosed.

Plan evidence: 6.4;D1
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L03: Integrate record authoring and persistence

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: L01,L02
Decision gate: D1 schema/default contract

Catalog/reflection, completion/hover/rename/references, graph, debugger/source maps, field/range/unit tools, schema-based serialization/checkpoints and compatible staged state migration.

Acceptance: Generation-scoped descriptors; no raw bytes/padding/pointers saved; add/remove/rename/type/default evolution disposition; incompatible publish preserves last valid generation.

Plan evidence: D1;9;11 Stage 1B
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L04: Optimize ordinary collection operations

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: B04,L00
Decision gate: none beyond existing contracts and applicable admission

Array/set construction and mutation via checked uniqueness/COW/reuse; stable indexes, sorting/searching/queues/cache algorithms using existing syntax first.

Acceptance: Aliases/event/task snapshots, retained managed values, invalid indices/error order, debug/reload preserved; amortized scaling measured and copied bytes explained.

Plan evidence: 6.1B;6.4;D2
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L05: Add keyed dictionaries when needed

Stage: 1B · Repository: chibi · Scope: conditional
Dependencies: L01,L04,B04
Decision gate: D2 dictionary

Use real Studio lookup/index consumer; choose new dict<K,V> versus explicit map contract revision; define key/equality/hash, missing/duplicate keys, deterministic iteration and nested ownership.

Acceptance: No silent change to indexed map<T>; collision/rehash/delete/alias/serialization cases and scaling with meaningful consumer.

Plan evidence: 6.1B;D2
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L06: Add bounded construction builders

Stage: 1B · Repository: chibi · Scope: conditional
Dependencies: L04,B04
Decision gate: D2 builder

Only if safe automatic reuse cannot satisfy measured construction needs; explicit capacity/finish/owner region and scoped views.

Acceptance: No accidental escape, suspension or dangling view; published values independent; failure and capacity overflow semantics; compare against ordinary value code.

Plan evidence: D2;6.4
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L07: Prove module-scale composition

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: B02,L00
Decision gate: none beyond existing contracts and applicable admission

Existing qualified libraries, discovery/namespace rules, private state ownership, visibility, unique names, cycles, incremental dependency invalidation and catalog isolation.

Acceptance: Multi-module specimen resolves deterministically; same-spelled names/cycles diagnose; imported state/reload identity correct; no hidden runtime service lookup.

Plan evidence: 4.2;6.1C;D3
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L08: Extend module visibility or aliases

Stage: 1B · Repository: chibi · Scope: conditional
Dependencies: L07
Decision gate: D3 aliases/visibility

Only demonstrated name/ergonomic issue warrants approved import aliases or missing export/private metadata; keep package-manager redesign out.

Acceptance: Ambiguities never resolve by registration order; dependency and source/debug/rename identities preserved across aliases.

Plan evidence: D3;6.1C
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L09: Specialize generic algorithms

Stage: 1B-2 · Repository: chibi · Scope: conditional
Dependencies: L07,L01
Decision gate: none beyond existing contracts and applicable admission

Implement bounded concrete specialization if real module/collection consumers need it rather than unmaintainable typed duplication.

Acceptance: Code-growth bounds, recursion diagnostics, ABI/effects, native/Wasm lowering, source/debug mappings, invalidation and no unsupported-generic false claims.

Plan evidence: 6.1C;6.5;11 Stage 1B
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L10: Implement synchronous capturing lambdas

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: L00,L01,L07
Decision gate: Inferred capture selected; callable grammar, capture/retention/ref contract still need specification

Owner selected inferred captures. Define exact safe by-value/ref and callable lifetime contract before grammar; checked nonescaping authored-to-authored predicate/comparator/visitor APIs first, with complete owner/service effects.

Acceptance: Inferred captures and costs inspectable; mutation must not silently change capture/alias lifetime; no borrowed escape or suspension; all owner/service effects tracked; zero environment allocation proved only on the chosen hot path.

Plan evidence: D6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L11: Complete callable tooling and compatibility

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: L10
Decision gate: D6 identity/reload

Stable callable/source/environment identities; capture type/mode/value/owner/cost inspection; all backend calls, stepping, traces, breakpoint/graph mapping and imported noescape verification.

Acceptance: Formatting does not change durable IDs; effects/alias/capture mode part of compatibility; stale or incompatible environment never invoked.

Plan evidence: D6;9
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L16: Verify engine operation descriptors

Stage: 1A-2 · Repository: chibi · Scope: committed
Dependencies: RSCHEMA,L00
Decision gate: none beyond existing contracts and applicable admission

Single-source typed definitions gain checked borrow/retain/alias/allocation/copy/thread/reentrancy/suspension/lowering metadata, generated bindings and ABI semantic fingerprints.

Acceptance: Contracts verified against implementation/test, negative mismatches rejected; unknown effects conservative; no unverifiable pure whitelist or editor capability leakage.

Plan evidence: 5;6.6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K00: Define typed editor capability contracts

Stage: 2 · Repository: chibi · Scope: committed
Dependencies: L00,L07,L16
Decision gate: none beyond existing contracts and applicable admission

Generated/type-checked nominal handles/event schemas, concrete Saved/Failed/Cancelled outcomes using existing unions, cost/effects/ownership and capability pack isolation.

Acceptance: Exact integers/IDs never stringified lossy; no raw graph/component pointers; game builds cannot implicitly acquire document authority; errors preserve state/source context.

Plan evidence: 4.1;5;D4;D8
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K04: Expose owner-bound job mechanisms

Stage: 2 · Repository: both · Scope: committed
Dependencies: K00
Decision gate: none beyond existing contracts and applicable admission

Start/poll/cancel export/thumbnail/index work, typed progress/success/failure, owner-generation tokens, limits and explicit concurrency.

Acceptance: Stale completion after close/reload/replacement discarded; cancel/teardown deterministic; queue admission bounded and errors visible.

Plan evidence: 5 jobs;6.4
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L12: Add owner-bound stored callbacks

Stage: 1B-2 · Repository: chibi · Scope: conditional
Dependencies: L10,L11,K04
Decision gate: D6 owned contract/reload

Named production consumer only; owned value captures, registrations/cancellation and queued invocation at approved execution boundary, chosen owner-generation reload policy.

Acceptance: Close/reload during queued invocation; no escaping local refs, cycles/unbounded retired generation retention; handle existence revalidated; copies preserve value semantics.

Plan evidence: D6;11 Stage 1B
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L13: Extend task or direct host callbacks

Stage: later · Repository: chibi · Scope: conditional
Dependencies: L12
Decision gate: D6 task callable/reentrancy

Only a named requirement; task-producing callable spelling/owned capture contract or separately approved synchronous native-to-authored reentrancy.

Acceptance: No borrowed environments across suspension; task results/cancel owner; host thread/context/activation/storage protection and unregistration verified. Current host no-reentry rule otherwise remains.

Plan evidence: D6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L14: Close remaining lifetime gaps

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: B04,RCAP
Decision gate: none beyond existing contracts and applicable admission

World string interning, task result snapshots/roots, activation/state arenas, retained old modules and owning FFI records; measure beyond coordinator-owned capture-box fix.

Acceptance: Repeated edits and return to same live state reclaim to a bound; producer destruction, owner close, reload and cancellation adversarial checks; no descriptor-only saved struct copies.

Plan evidence: 6.1D;6.4;7.2
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L15: Specify owner cleanup and admission

Stage: 1B-2 · Repository: chibi · Scope: committed
Dependencies: L00,K00
Decision gate: none beyond existing contracts and applicable admission

Workspace/document/component registrations/jobs/resources deterministic teardown, bounded queues/payload/task work admission and truthful allocation-failure contract.

Acceptance: Cancel/dispose order, queue overflow/reentrancy and stale completion behavior; do not promise recoverable OutOfMemory while allocator aborts.

Plan evidence: 6.4;5 host families
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L17: Lower typed engine calls across backends

Stage: 1A-2 · Repository: chibi · Scope: committed
Dependencies: L16,R397,R398,RWRITE
Decision gate: none beyond existing contracts and applicable admission

Bounded worthwhile helper with native scalar/unboxed calling convention and Wasm typed imports/linker; simple semantics direct shared IR, stable service binding and generic checked fallback.

Acceptance: Actual bytecode/SLJIT/Wasm semantics/cost comparison; call preparation eliminated where claimed; immutable definitions and realm-local Wasm table lifetime correct.

Plan evidence: 6.6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L18: Optimize safe query hot regions

Stage: 1A-2 · Repository: chibi · Scope: committed
Dependencies: R397,R398,RWRITE,L16,B04
Decision gate: none beyond existing contracts and applicable admission

Hoist stable metadata/validation and specialize row bodies after narrow access operations; preserve logical references outside uninterrupted proven regions.

Acceptance: Reacquire after structural changes, unknown effects, callbacks, yield and publication; counters show whole-path improvement; no claim existing authored code emits BF_ROW/BF_COLUMN loops.

Plan evidence: 6.6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L19: Extend intermodule execution proof

Stage: 1B-2 · Repository: chibi · Scope: conditional
Dependencies: L07,L16,B04
Decision gate: none beyond existing contracts and applicable admission

Inline/local/imported representative game update first; implement verified import summaries only when workload proves needless module serialization.

Acceptance: Real worker participation, same ordered fold/effects results; import generation/ABI invalidation and unknown native/callable fallback; no forced source flattening.

Plan evidence: 6.5;11 Stage 1B
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L20: Eliminate proved redundant snapshots

Stage: 1B-2 · Repository: chibi · Scope: committed
Dependencies: L02,L04,L16,B04
Decision gate: none beyond existing contracts and applicable admission

Use lifetime/uniqueness for record/collection hot helpers; retain owned values at async/stored boundaries and checked fallbacks for managed/opaque cases.

Acceptance: Alias/escape/suspension/error/debug invariants; copied bytes and allocations measured; optimization never turns value assignment into shared mutation.

Plan evidence: 6.4;6.6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L21: Implement packed typed AoS

Stage: 1B-later · Repository: chibi · Scope: committed
Dependencies: L01,L03,B04
Decision gate: D9 array-like spelling and possible storage inference under investigation; ownership unresolved

After approval, implement explicit narrow owner-backed storage of plain records, checked arithmetic/alignment/count/capacity, record snapshots, typed span/FFI contract and logical schema serialization.

Acceptance: Boxed-versus-packed paired Studio/game measurements; clone/share/move explicit; alias/resize/reorder/destruction/suspension borrow rules; no change to current array semantics.

Plan evidence: D9
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L22: Add opt-in field-wise SoA

Stage: later · Repository: chibi · Scope: committed_measured
Dependencies: L21,B04
Decision gate: D9 array-like spelling and possible storage inference under investigation; ownership unresolved

Once selected, implement the explicit layout family; top-level field columns, synchronized structural ops, gather/scatter snapshots, explicit AoS conversion, separate order-preserving versus swap removal.

Acceptance: Three-way boxed/AoS/SoA end-to-end crossover including conversions/materialization; no contiguous ref T for row proxy; bounds/alias/failure/lifetime and FFI coverage.

Plan evidence: D9
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L23: Vectorize worthwhile buffer kernels

Stage: later · Repository: chibi · Scope: conditional
Dependencies: L22,B04
Decision gate: none beyond existing contracts and applicable admission

Only measurable target-specific benefit after safe packed storage; feature detection, bounds/tails, alignment, disjointness/effects and scalar fallback.

Acceptance: Identical floats/reduction order unless separately approved; reproducible total-operation improvement outside uncertainty, not only isolated kernel timing.

Plan evidence: D9 SIMD gate
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] L24: Prove representative game capabilities

Stage: 1B-later · Repository: chibi · Scope: committed
Dependencies: B02,B04,L00
Decision gate: none beyond existing contracts and applicable admission

Dense/sparse/change queries, spawn/despawn/hierarchy, fixed-step/event order, shared-target ordered reductions, AI/task cancel swarm, exact numeric/resource ABI and inline/local/imported cases.

Acceptance: Actual worker participation where claimed, deterministic serial comparison, stable handles on churn, bounded tasks/memory and honest backend selection.

Plan evidence: 6.5;7.2;9
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] F00: Compare formatter layout engines

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: B02,RFMT
Decision gate: none beyond existing contracts and applicable admission

Current configurable printer versus real Prettier plugin/Topiary feasibility and bounded corpus evidence; one authoritative chosen engine, maintenance/configuration/native/browser implications.

Acceptance: No thin wrapper claims width reflow; no unapproved install; all dialect/comment/source-map/invalid-draft tests; decision recorded independently of candidate implementation.

Plan evidence: 8;8.1
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] F01: Unify layout configuration and callers

Stage: 2 · Repository: chibi · Scope: committed
Dependencies: F00,R399
Decision gate: none beyond existing contracts and applicable admission

Versioned options/config resolution for indent/tabs/width/line endings, project versus explicit precedence, diagnostics; one Studio/CLI/check/save-authoring service path.

Acceptance: Identical outputs; check non-writing, write atomic, editor one undo/caret mapping; unknown options explicit; invalid or contextless source safe.

Plan evidence: 8.1;D10
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] F02: Integrate RML and RCSS formatting

Stage: 2 · Repository: both · Scope: committed
Dependencies: F00,B02
Decision gate: none beyond existing contracts and applicable admission

Full 81 RML/102 RCSS Studio assets plus mixed/Unicode/text/entities/comments/templates/bindings/use/slots/nesting/imports/colors/custom values corpus; CLI includes UI assets.

Acceptance: Parse/idempotence/significant bytes/style/order/bindings unchanged, app pixels/input match, incomplete drafts non-destructive; retain pins and no CSS normalization.

Plan evidence: 8
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] F03: Add narrow semantic canonicalizer

Stage: 1B-2 · Repository: chibi · Scope: committed
Dependencies: R399,L00
Decision gate: Exact state/self/meaning-preserving Format approval in decisions.md; full library/shared/callable self table and named-fn rejection/default policy not selected

Separate node/range-based allowlisted root state and named fn token elision from layout and broader Simplify; retain comments/types/contracts, use catalog/document identity.

Acceptance: Normalized tree plus typed bindings/storage/evaluation/defaults/units/ref/effects equality; idempotence, runtime/source-graph identity, undo and incomplete-source behavior.

Plan evidence: D10 proof contract
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] F04: Implement approved necessary-only self resolver

Stage: 1B · Repository: chibi · Scope: committed
Dependencies: L00,L07
Decision gate: Exact state/self/meaning-preserving Format approval in decisions.md; full library/shared/callable self table and named-fn rejection/default policy not selected

Lexical shadowing/member/static/import lookup and owner scope rules exactly as approved; preserve entity self and shared/callable/library storage identities; canonical qualifier elision only if identical binding.

Acceptance: Scope collision/ambiguity, nested capture, refs, new imports/member rename and reload tests; local shadowing legal and necessary qualifier retained; no implicit whole-owner capture.

Plan evidence: D10
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] F05: Migrate strict canonical grammar safely

Stage: 1B-2 · Repository: both · Scope: committed
Dependencies: F03,F04
Decision gate: Exact state/self/meaning-preserving Format approval in decisions.md; full library/shared/callable self table and named-fn rejection/default policy not selected

Inventory parsed occurrences across engine/Studio/Celeste/templates; historical parser rewrite; coordinated then-current project bump only if used contract breaks; migrate engine fixtures atomically.

Acceptance: Named actual affected version-24 Weapon fixture; manifest last, recovery/idempotence, semantic metadata; preserve required callable state/shared; no version-only downstream PRs or implicit Format upgrade.

Plan evidence: D11
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] F06: Add optional editor or Prettier adapter

Stage: later · Repository: chibi · Scope: conditional
Dependencies: F01
Decision gate: none beyond existing contracts and applicable admission

Only useful integrations around authoritative engine API, without second formatting policy or printer.

Acceptance: Actual option forwarding and identical outputs; native/Wasm package correctness; adapters do not imply canonical grammar approval.

Plan evidence: 8.1
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K01: Expose document and authoring mechanisms

Stage: 2 · Repository: chibi · Scope: committed
Dependencies: K00,L03
Decision gate: none beyond existing contracts and applicable admission

Immutable revision snapshots, stable symbol/node IDs, bounded text edits, prepare/stage/publish, reflection/canonical serialization and generic owned storage.

Acceptance: No old product-policy facade; source draft/applied/saved/durable states separate; staged failure atomic and old generation safe.

Plan evidence: 5 documents;11 Stage 2
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K02: Expose UI instances and batched publication

Stage: 2 · Repository: both · Scope: committed
Dependencies: K00,L15
Decision gate: none beyond existing contracts and applicable admission

Generation-safe mount/element handles; typed patches, focus/capture/popover/list/geometry/text primitives; explicit revisions, coherent publish, attach/detach cleanup and bounded settling.

Acceptance: Duplicate views preserve frozen shared/independent behavior; stale callbacks/handles rejected; same-frame event trace and cycles/defer policy; hidden work suppressed.

Plan evidence: 4.3-4.5;5 UI;6.1F
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K03: Expose scoped files and durable persistence

Stage: 2 · Repository: both · Scope: committed
Dependencies: K00
Decision gate: none beyond existing contracts and applicable admission

Project-root reads, atomic writes, change events, import/export streams, browser in-memory versus IndexedDB durability, completion/retry with revision/generation ownership.

Acceptance: Out-of-order/newer saves, failure/retry/quit, path isolation/no symlink-follow semantics; one owner external effects; exact bytes.

Plan evidence: 5 files;12 persistence
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K05: Expose runtime and renderer resources

Stage: 2 · Repository: chibi · Scope: committed
Dependencies: K00,L16
Decision gate: none beyond existing contracts and applicable admission

Create/dispose worlds, immutable prepared generations, playback inspection/control, viewport submissions and retained GPU resources as typed mechanisms.

Acceptance: No component pointer retained over structural change or yield; cleanup/cancel/reload order, isolated preview worlds and actual native/browser paths.

Plan evidence: 5 runtime;6.6
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K06: Expose normalized platform mechanisms

Stage: 2 · Repository: both · Scope: committed
Dependencies: K00
Decision gate: none beyond existing contracts and applicable admission

Window/input events, clipboard/dialogs/cursor/time/scale/accessibility and text surface foreign adapter, callbacks return to designated app thread.

Acceptance: IME/Unicode/focus/capture/touch/trackpad and shortcut routing contracts; reentrancy/thread checks; surrounding product commands stay authored.

Plan evidence: 3;5 platform
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K07: Expose collaboration transport mechanisms

Stage: 2 · Repository: both · Scope: committed
Dependencies: K00,K03,K04
Decision gate: none beyond existing contracts and applicable admission

Authenticated socket/browser I/O, bounded send/receive/close and codec if justified; stable identity/revision/outcome schema and fake service for parity.

Acceptance: Domain authorization/order/replay remains authored; cancellation/stale owner behavior; no replay double-effects or accidental authority expansion.

Plan evidence: 5 collaboration
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K08: Prove synthetic feature lifecycle

Stage: 2 · Repository: both · Scope: committed
Dependencies: K01,K02,K03,K05,K06,F01,F02,L14
Decision gate: none beyond existing contracts and applicable admission

Complete disposable state/services/publication/failure/cancel/reload feature with existing/approved primitives; record evidence without calling it Outliner production migration.

Acceptance: No leaks/corruption/editor service leakage; actual native+browser compiled packages, invalid draft retains applied version; all relevant Stage 1 data/lifetime gates satisfied.

Plan evidence: 11 Stage 2
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] K09: Pin and package candidate SDK

Stage: 2 · Repository: both · Scope: committed
Dependencies: K08,RSCHEMA
Decision gate: none beyond existing contracts and applicable admission

Land reviewed engine capability pair then pin exact Studio SDK release/checksum; clean installed-SDK builds native/browser; honor public API/extraction manifest.

Acceptance: No private engine includes/latest-version selection; integrated pair re-reviewed/tested after conflicts or base changes; predecessor evidence not reused.

Plan evidence: 2;11 parallel work;13
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P00: Establish authored module composition

Stage: 3-8 · Repository: studio · Scope: committed
Dependencies: B01,K09
Decision gate: none beyond existing contracts and applicable admission

App/domain/feature/UI/service dependency direction, command registry and explicit module-local selectors; incrementally dismantle studio_sources.c/global internals/native node facade as each feature moves.

Acceptance: No replacement giant registry/global Studio object; UI-to-domain direction; exactly one authority per migrated function; remaining crosscut pieces accounted at final audit.

Plan evidence: 4;5;11
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P01: Author Outliner scene commands

Stage: 3 · Repository: studio · Scope: committed
Dependencies: P00,K01,K05,L04
Decision gate: none beyond existing contracts and applicable admission

Selection/toggle/range anchors, filtered order, rename/path references, visibility/lock/isolate, add/drop/reparent/reorder and permission validation.

Acceptance: All target set validates before commit; cycles/world-transform/path/hidden/locked/spawned cases; adapters use engine primitives not old select_object/set_view/drop/rename/isolate/add controller calls.

Plan evidence: 11 Stage 3;12 Outliner
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P02: Author Outliner edit transactions

Stage: 3 · Repository: studio · Scope: committed
Dependencies: P01,K01
Decision gate: none beyond existing contracts and applicable admission

Minimum common scene history and snapshot policy, live edit input/change/cancel, undo/redo grouping and redo retention after no-ops.

Acceptance: Failed replay leaves values/stacks untouched; one-step multi-target transaction; reload/close/cancel retains exact IDs and values.

Plan evidence: 4.4;11 Stage 3
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P03: Complete idiomatic Outliner behavior

Stage: 3 · Repository: studio · Scope: committed
Dependencies: P02,K02,F01
Decision gate: none beyond existing contracts and applicable admission

Header/filter/rows/virtualization/selection/fold/reveal/rename/menu/drag-reorder controllers, typed models, per-view state and cleanup using approved syntax.

Acceptance: Two mounts shared/independent state parity, repeated/interrupted gestures, focus/scroll, invalid edits/reload and large-list scale; source semantic comparison before simplification.

Plan evidence: 2 existing migration;11 Stage 3
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P04: Close complete Outliner parity gate

Stage: 3 gate · Repository: both · Scope: committed
Dependencies: P03,B03,B04,B05
Decision gate: none beyond existing contracts and applicable admission

Functional/input/exact-pixel and performance comparison plus native policy removal/allowlist; baseline defect choices separated.

Acceptance: All Stage 0-2 dependencies satisfied; no hidden old controller call-through; retained memory bound; this gate alone opens broad production panel conversion.

Plan evidence: 11 Stage 3;12;13
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P05: Author shared UI components

Stage: 4 · Repository: studio · Scope: committed
Dependencies: P04,K02
Decision gate: none beyond existing contracts and applicable admission

Typed list/tree/field/menu/dialog controls; drag, popup/focus/dismiss/submenu/placement, slot/mount keys, motion/reduced-motion, number commit/scrub, morph text and toast lifecycle.

Acceptance: First-open freshness, cancellation/focus return, multi-view, one-step edits, notification dedup, hidden/closed idle suppression; exact existing RML/RCSS.

Plan evidence: 4.5;11 Stage 4;12
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P06: Migrate Members editing

Stage: 4 · Repository: studio · Scope: committed
Dependencies: P05,K01
Decision gate: none beyond existing contracts and applicable admission

Member list/declaration/type/property choices, commands and reusable fields with exact source identities.

Acceptance: Declaration/edit/source-graph/undo/diagnostic behavior and pointer/keyboard parity; no C member policy remains.

Plan evidence: 11 Stage 4;Appendix A studio_members
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P07: Migrate Output views

Stage: 4 · Repository: studio · Scope: committed
Dependencies: P05,K01
Decision gate: none beyond existing contracts and applicable admission

Records/diagnostic/call projections, filters and navigation; retain bounded storage and source identity.

Acceptance: Incoming messages while hidden, focus/navigation/reload, dedup/order and idle parity; no C output policy remains.

Plan evidence: 11 Stage 4;Appendix A studio_output
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P08: Migrate settings preferences keymaps

Stage: 4 · Repository: studio · Scope: committed
Dependencies: P05,K03,K06
Decision gate: none beyond existing contracts and applicable admission

Schema/defaults/migrations, unknown supported keys, workspace layouts, user remaps/chords and settings control behavior.

Acceptance: Saved/reloaded values, remapped disabled/check commands, platform/reduced-motion overrides and exact shortcut/focus routing.

Plan evidence: 11 Stage 4;12 shell
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P09: Migrate shell commands and dialogs

Stage: 4 · Repository: studio · Scope: committed
Dependencies: P05,P08,P00
Decision gate: none beyond existing contracts and applicable admission

Header/chrome/tabs/area-panel registry, menus, prompts and shared command availability/check state for menu/shortcut/palette/buttons.

Acceptance: First-open nested popovers, Escape/outside, focus return, duplicate areas, disabled commands; no duplicate native route or poll/rebuild regression.

Plan evidence: 4;11 Stage 4;12 shell
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P10: Migrate palettes and pickers

Stage: 4 · Repository: studio · Scope: committed
Dependencies: P05,P09
Decision gate: none beyond existing contracts and applicable admission

Palette/command matching, reusable choice/label/icon presentation and named picker actions with stable typed IDs.

Acceptance: Keyboard/pointer/shortcut parity, filter/order/stable selection, no lossy IDs, cancellation and closed-state work suppression.

Plan evidence: 11 Stage 4;Appendix A choices/labels/palette
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P11: Migrate diagnostics presentation

Stage: 4 · Repository: studio · Scope: committed
Dependencies: P05,B04
Decision gate: none beyond existing contracts and applicable admission

Output-independent performance windows/labels/frame ring and CSV projection; retain only generic clock/profiler/export ABI.

Acceptance: No inclusive nested CPU double counting, correct time origin, bounded ring/cache memory and profiling overhead separation.

Plan evidence: 7 instrumentation;Appendix A diagnostics
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P12: Unify document authority and histories

Stage: 5 · Repository: studio · Scope: committed
Dependencies: P04,K01,L03
Decision gate: none beyond existing contracts and applicable admission

All document types, saved/working/applied identities, scene/prototype collections, command/mutation/navigation coordination and retention; extend Stage 3 transaction foundation.

Acceptance: No duplicate native authority; multi-document exact IDs/bytes, no-op redo, atomic multi-object/file edits, unknown fields and stale revision rejection.

Plan evidence: 4.3-4.4;11 Stage 5
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P13: Migrate save conflicts and close flows

Stage: 5 · Repository: studio · Scope: committed
Dependencies: P12,K03,P09
Decision gate: none beyond existing contracts and applicable admission

Tabs/dirty/revert/reload/save/unsaved prompts, disk notifications/watch policy/self-write suppression and source-draft/applied/saved/durable state machine.

Acceptance: New save during flush, retry/quit/close/reopen, external change, cancellation and project revision isolation; exact file bytes and no symlink-follow change.

Plan evidence: 11 Stage 5;12 persistence/code
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P14: Migrate source and UI buffer authority

Stage: 5 · Repository: studio · Scope: committed
Dependencies: P12,K01,F01,F02
Decision gate: none beyond existing contracts and applicable admission

Asset/UI text buffer state/revisions, edit/apply and preview policy, one source truth with shared SDK parse/compile/reflection mechanism.

Acceptance: Unicode/IME/source-byte identity, typing undo groups, format one undo/caret, malformed drafts/last-valid-generation, source-graph equivalence.

Plan evidence: 11 Stage 5;12 code/designer
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P15: Migrate checkpoints and recovery

Stage: 5 · Repository: studio · Scope: committed
Dependencies: P12,P13,K03,K07
Decision gate: none beyond existing contracts and applicable admission

Scene/workspace checkpoint schema/composition, exact IDs/value types/retention/remapping, staging restore and collaboration-compatible state.

Acceptance: Failure leaves existing documents/worlds intact; restore and replay exact state/bytes; recovery tested before native deletion.

Plan evidence: 11 Stage 5;12 persistence/collab
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P16: Migrate asset browser and index policy

Stage: 5 · Repository: studio · Scope: committed
Dependencies: P05,P12,L04,K04
Decision gate: none beyond existing contracts and applicable admission

Saved plus unsaved/open overlays, search/sort/filter/selection grid/list/virtualization, gallery and thumbnail result invalidation.

Acceptance: Modifier selection/filter retention, stable keys, async after close/filter, bounds/large scaling and native parity.

Plan evidence: 11 Stage 5;12 assets
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P17: Migrate asset actions and refactoring

Stage: 5 · Repository: studio · Scope: committed
Dependencies: P12,P16,K01,K03
Decision gate: none beyond existing contracts and applicable admission

Create/delete/import/drop and batch undo, semantic rename/reference planning/apply orchestration, atomic multi-file changes through SDK primitives.

Acceptance: Path/references preserved, failures all-or-nothing, exact integers/unknown fields, source identity and collaboration replay parity.

Plan evidence: 11 Stage 5;Appendix A refactor/drop
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P18: Migrate reflected Properties edits

Stage: 5 · Repository: studio · Scope: committed
Dependencies: P05,P12,K01,L03
Decision gate: none beyond existing contracts and applicable admission

Fields/model projection, scalar/vector/color/enum/reference, exact 64-bit/mixed/computed/read-only, multi-selection/live sessions/overrides.

Acceptance: Scrub/type/cancel/focus-loss/hide, preview/change grouping, one undo, all-target validation and field revision-driven work.

Plan evidence: 11 Stage 5;12 Properties
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P19: Migrate code editor product policy

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P14,P05,K06
Decision gate: none beyond existing contracts and applicable admission

Completion/indentation/commands/diagnostics/find/apply-save/text presentation/history arbitration; preserve foreign Zep/render/text mechanism only.

Acceptance: Standard/Vim/Unicode/IME, tab lifecycle, diagnostic ranges, editor shortcuts/undo/input/caret and invalid-draft behavior.

Plan evidence: 11 Stage 6;12 code;Appendix A kit_code/zep
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P20: Migrate source navigation tools

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P14,P19,K01
Decision gate: none beyond existing contracts and applicable admission

Call hierarchy/folds, definition/context, project Find/search, refactor UI confirmation/application and navigation history.

Acceptance: Stable source/symbol IDs across formatting/rename/edit, exact locations/back-forward/filter semantics, source-graph aliases.

Plan evidence: 11 Stage 6;Appendix A navigation
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P21: Migrate debugger controls and views

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P14,K05,P19
Decision gate: none beyond existing contracts and applicable admission

Selected target, breakpoints/watch/step/resume/stop, source/graph live values and error presentation with immutable generation ownership.

Acceptance: Actual breakpoints, invalid/valid edits while paused, old/new generation lifetimes, source alias IDs and runtime failure locations.

Plan evidence: 11 Stage 6;12 debugger
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P22: Migrate graph domain and edit policy

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P12,P14,K01
Decision gate: none beyond existing contracts and applicable admission

Flow/document/input-binding/pin/selection/search/value semantics, comments/literals/reflected properties and source synchronization through one authority.

Acceptance: Round-trip supported graph shapes, explicit unsupported diagnostics, source/edit transaction and refactor identities; C policy removed.

Plan evidence: 11 Stage 6;12 graph
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P23: Migrate graph interaction and geometry

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P22,P05,K02
Decision gate: none beyond existing contracts and applicable admission

Pointer/key gestures, pan/zoom/selection/navigation/menus, wires/pin connection/routing, authored geometry where possible over narrow batched draw mechanisms.

Acceptance: Repeated/cancel/hidden-panel input relinquishment, pointer capture, exact geometry/pixels; retained tessellation needs actual kernel justification.

Plan evidence: 11 Stage 6;12 graph;Appendix A kit_wires/kit_wire_shape
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P24: Migrate graph layout and live presentation

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P22,P23,P21
Decision gate: none beyond existing contracts and applicable admission

Node/control composition, arrange/layout algorithms, headers/targets, details, flow visuals, eval projection and live highlight/debug models.

Acceptance: Large graph scale, source/value fidelity, stable ordering/IDs, measured native/authored geometry/layout costs and no unexplained retained growth.

Plan evidence: 11 Stage 6;7.2 graph
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P25: Migrate Canvas and Designer

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P14,P18,P05,K02
Decision gate: none beyond existing contracts and applicable admission

Preview sizing/backdrop/usage navigation, hierarchy/slots/library insert, type-aware details/style edits and gesture transactions in same text history.

Acceptance: Text and visual editing share one truth; live DOM/source mappings, scroll/focus, undo grouping, stale generation/element rejection.

Plan evidence: 11 Stage 6;12 Designer
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P26: Migrate Inspector views and overlays

Stage: 6 · Repository: studio · Scope: committed
Dependencies: P25,P05,K02
Decision gate: none beyond existing contracts and applicable admission

Selected live DOM target, box model/paths/style properties, inspect overlay projection and actions.

Acceptance: Live tree mutation/target deletion, stale handles, layout/source mapping, scrolling and exact inspection view parity.

Plan evidence: 11 Stage 6;12 Inspector
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P27: Complete scene authoring commands

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P12,P15,P18,K05
Decision gate: none beyond existing contracts and applicable admission

Remaining actor/prototype/scene document authoring/drop/hierarchy/path operations beyond Outliner foundation, permission and multi-target transactions.

Acceptance: Hierarchy/world transforms, stable IDs/entity paths, undo/replay/prototype overrides, cancellation and stale generations.

Plan evidence: 11 Stage 7;12 scene
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P28: Migrate scene camera viewport input

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P27,K05,K06,P09
Decision gate: none beyond existing contracts and applicable admission

2D/3D view/camera/pick/overlay/header/grid/zoom policies, normalized input ownership/pinch/wheel/pan and viewport sizing/dirty/presentation.

Acceptance: Focus loss/hidden scene/touch/trackpad, multi-area/game input separation, singular/nonuniform parents, exact pixels and idle suppression.

Plan evidence: 11 Stage 7;12 runtime
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P29: Migrate gizmos and colliders

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P27,P28,P18
Decision gate: none beyond existing contracts and applicable admission

2D/3D constraints/snapping/target selection/handle geometry and collider controls/gesture history.

Acceptance: World/local/singular/nonuniform transforms, cancellation/focus loss, exact one undo, multi-selection, pointer capture and visual parity.

Plan evidence: 11 Stage 7;12 runtime
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P30: Migrate playback and game previews

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P27,P15,K05,K04
Decision gate: none beyond existing contracts and applicable admission

Play/pause/step/stop/restart/target/spawn-break/keep-changes, scenario follow, physics reset, game input/presentation and preview-world lifecycle.

Acceptance: Deterministic world/physics/input state, pause/debug generation safety, isolated previews, task cancellation/retained memory and kept changes parity.

Plan evidence: 11 Stage 7;12 runtime/lifecycle
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P31: Migrate application and workspace orchestration

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P13,P15,P30,P09,K06
Decision gate: none beyond existing contracts and applicable admission

Startup/CLI/project intents, candidate workspace replacement/recovery/room/guest Studio routing, preferences/idle deadlines/frame coordination/module composition; retain entry/event-pump ABI only.

Acceptance: Failed replacement retains old workspace and user work; guest/multi-window routing, teardown order, idle no new work and native/browser launch.

Plan evidence: 11 Stage 7;12 lifecycle;Appendix A application/host
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P32: Migrate model preview and bake policy

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P30,K05,P18
Decision gate: none beyond existing contracts and applicable admission

Fixture composition, rejection state, view/reference-image options, bake controls and results; generic renderer/bake/download mechanism only.

Acceptance: Preview isolation, failure/cancel/close, exact material/visual state, correct resource disposal and native/browser output.

Plan evidence: 11 Stage 7;Appendix A model_preview/studio_model
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P33: Migrate thumbnail scheduling and caches

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P16,K04,K05,K03
Decision gate: none beyond existing contracts and applicable admission

Dependency/content recipes, priority/invalidation/LRU/bounded cache/failure policy, async generation completion; generic file/PNG/GPU operations.

Acceptance: Completion after close/filter/reload, eviction/memory plateau, stale requests discarded, exact visuals and browser persistence.

Plan evidence: 11 Stage 7;7.2 assets
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P34: Migrate project and export workflows

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P13,P15,P31,K03,K04
Decision gate: none beyond existing contracts and applicable admission

Project chooser/new/open/import/export/save-first/name/progress/toast/reveal orchestration; process/file/download primitives only.

Acceptance: Exact exported/imported bytes, retry/cancel/unsaved work, browser durability, errors retain previous workspace and correctly scoped jobs.

Plan evidence: 11 Stage 7;12 persistence
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P35: Migrate collaboration policy and replay

Stage: 7 · Repository: studio · Scope: committed
Dependencies: P12,P15,P27,P22,K07,P31
Decision gate: none beyond existing contracts and applicable admission

Schemas/protocol validation, action authorization/gates, scene/graph dispatch through common command transactions, room/order/queue/presence/resync/checkpoint/replay and UI controls.

Acceptance: Accepted/rejected/stale/replayed actions, reconnect/resync, checkpoint identity and concurrent close; no duplicated external effects; bounded queues.

Plan evidence: 11 Stage 7;12 collaboration
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P36: Extract remaining browser product policy

Stage: 7-8 · Repository: studio · Scope: committed
Dependencies: P31,P34,P35,K03,K06,K07
Decision gate: none beyond existing contracts and applicable admission

Audit all ten web host files for lifecycle/recovery/room policy; retain only browser boot/storage/network/download/isolation machinery with exact interface.

Acceptance: Native/browser service consistency, in-memory versus durable distinction, threaded target only if supported, packaging and startup failure/retry semantics.

Plan evidence: Appendix A browser boundaries;11 Stages 7-8
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P37: Resolve native mechanism exceptions

Stage: 8 · Repository: studio · Scope: committed
Dependencies: P31,P34,P35,P36,B05
Decision gate: none beyond existing contracts and applicable admission

Exact functions in state_file, stacktrace, web_trace, zep bridge plus every extracted adapter/generated binding; authored attempts or concrete irreducible ABI/crash justification.

Acceptance: All 155 original units accounted as migrated/deleted or reviewed generic mechanisms; no hidden portable policy and no unsupported performance exception.

Plan evidence: 5;11 Stage 8;Appendix A
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [ ] P38: Cut over packaging and remove transitional C

Stage: 8 · Repository: both · Scope: committed
Dependencies: P06,P07,P08,P09,P10,P11,P13,P14,P15,P16,P17,P18,P19,P20,P21,P22,P23,P24,P25,P26,P27,P28,P29,P30,P31,P32,P33,P34,P35,P36,P37,F01,F02,F05,L03,L24
Decision gate: none beyond existing contracts and applicable admission

Final source/import allowlist, remove migration adapters/runtime switches/native controller duplicates; retain pinned oracle artifact/branch; installed-SDK native/browser packages, release evidence, docs/cookbooks and recovery proof.

Acceptance: Full agreed exact shipping-pair state/input/pixel/performance/platform matrix, budgets and memory; independent reviews/tests and exceptions signed off; default switch only then. Conditional features require fulfilled implementation or explicit not-needed disposition.

Plan evidence: 11 Stage 8;13;14
PR / exact head / review / tests / remaining blocker: **unrecorded**

## [x] B06: Audit residual native reload dependencies

Stage: 0-2 · Repository: both · Scope: completed_audit
Dependencies: none (preparation can start)
Decision gate: none beyond existing contracts and applicable admission

Read-only native-reload source/build audit completed at engine 02f84532 (unchanged relevant paths in 1de4810d) and Studio 78cb4d4c. Native application replacement is already removed; see native-reload-audit.md. Retain retired-option cleanup, generated-C dlopen test and authored publication/checkpoint mechanisms.

Acceptance: Exact source/build/host references and surviving dynamic-loader uses inspected; no native deletion needed at these snapshots. Reopen only on evidence of a live path, preserving .chibi state/callback/generation safety and workspace recovery. No runtime test claimed.

Plan evidence: Owner clarification 2026-10-15 13:55;6.6 lifetime safeguards;D6;9;13;docs/reload.md
Read-only audit complete; no removal PR needed. Exact snapshot, source links and limits in native-reload-audit.md; no product runtime test claimed.

## Immutable source reference index

- [E-architecture](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/architecture.md#L9-L114)
- [E-borrow](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/borrow.h#L14-L118)
- [E-format-cli](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/automation/controller.c#L6105-L6131)
- [E-generics](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L1158-L1188)
- [E-index](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/arrays.c#L200-L336)
- [E-language](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/code-language.md)
- [E-migration](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/studio-rml-migration.md#L1-L81)
- [E-policy](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/AGENTS.md#L13-L18)
- [E-ui-format](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/ui_source_format.c#L1-L277)
- [P147](https://github.com/waldnercharles/chibi/pull/147)
- [P169](https://github.com/waldnercharles/chibi/pull/169)
- [P68](https://github.com/waldnercharles/chibi-studio/pull/68)
- [S-acceptance](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/README.md)
- [S-app](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/application.c#L153-L267)
- [S-browser](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/BROWSER.md#L48-L90)
- [S-buffer](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_buffer.h#L118-L131)
- [S-build](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/CMakeLists.txt#L64-L142)
- [S-history](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_history.h#L19-L177)
- [S-node-calls](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_node_calls.c#L108-L352)
- [S-outliner](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/projects/studio/content/behaviors/OutlinerRows.chibi#L1-L90)
- [S-source-sync](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_sources.c#L1332-L1442)
- [S-source-table](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_sources.c#L2382-L2456)
- [S-sources](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/studio_sources.cmake#L1-L153)
- [S-ui](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_ui.c#L148-L262)
- [S-ui-events](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_ui.c#L542-L618)
- [S-ui-mount](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_ui.c#L794-L874)
- [s-array-header](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.h#L10-L57)
- [s-canonical](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/automation/controller.c#L5868-L5916)
- [s-component](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/component.c#L177-L230)
- [s-continuation](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L5360-L5436)
- [s-copy](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L274-L333)
- [s-declared](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/declared_components.c#L95-L244)
- [s-ecs-columns](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/ecs.c#L116-L128)
- [s-ecs-growth](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/ecs.c#L235-L256)
- [s-ecs-view](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/execution.h#L7-L23)
- [s-equality](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L336-L384)
- [s-fixture-manifest](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/fixtures/studio-workspace/project.kdl#L1-L4)
- [s-fixture-source](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/fixtures/studio-workspace/content/behaviors/Weapon.chibi#L1-L6)
- [s-format](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_format.c#L158-L310)
- [s-format-tests](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/format.c#L27-L39)
- [s-functions](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L274-L301)
- [s-generics](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L1158-L1188)
- [s-grammar-roots](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L124-L206)
- [s-host](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/host.h#L15-L55)
- [s-ir](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic.h#L8-L89)
- [s-library-self](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/program.c#L251-L261)
- [s-library-state](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/program.c#L1290-L1328)
- [s-literal](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_expression.c#L832-L879)
- [s-lookup](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_context.c#L77-L114)
- [s-migrate-staging](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/project_migrate.c#L4126-L4143)
- [s-minimize](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_minimize.c#L1164-L1242)
- [s-minimize-compare](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_minimize.c#L51-L114)
- [s-minimize-tests](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/format.c#L407-L444)
- [s-no-capture](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_expression.c#L555-L575)
- [s-old-parser](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/project_migrate.c#L23-L61)
- [s-plain](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/type.c#L386-L409)
- [s-reload](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L2553-L2815)
- [s-rewrites](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_minimize.c#L1042-L1124)
- [s-rules](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/AGENTS.md)
- [s-scanner](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/src/scanner.c#L33-L104)
- [s-self](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic.c#L94-L108)
- [s-self-members](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_calls.c#L368-L411)
- [s-snapshot](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L150-L226)
- [s-state-grammar](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L186-L233)
- [s-state-qualifier](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_calls.c#L209-L229)
- [s-state-semantics](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_declaration.c#L30-L54)
- [s-symbol-ids](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_context.c#L143-L160)
- [s-task-test](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/program.c#L6514-L6556)
- [s-type](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/type.h#L11-L74)
- [s-value](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/value.h#L10-L63)
- [s-version](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/project.h#L11-L17)
