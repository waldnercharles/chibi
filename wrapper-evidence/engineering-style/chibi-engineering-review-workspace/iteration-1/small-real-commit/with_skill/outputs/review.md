# Review: a1135ab93 "Preserve scoped-write effects in authored function summaries"

Scope: commit `a1135ab93` (parent `7070712e2`) in `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`. Two files changed: `tools/compiler/package_compiler/Steps.chibi` (+4/-2) and `test/language/argument_effects_chibi.c` (+59/-30). I read both files in full at the commit, plus the producers and consumers of the effect summary in `Body.chibi`, `Compile.chibi`, `Pointers.chibi`, `Sequences.chibi`, `References.chibi`, `Maps.chibi`, `Behavior.chibi`, and the C twin in `src/language/semantic_statement.c`, `src/language/callable_headers.c` and `src/execution/native.c`. The checkout's HEAD is `cd428b92e`, a later commit. Line numbers below are at `a1135ab93`. Read only: I didn't build or run anything, because the checkout has no build directory and must not change. Findings 1 and 2 come from tracing the code. I didn't run them; the failing inputs are written out so they can be run.

## Brief

- **Transform:** For each store the body emitter classifies as external, `WriteStep` (Steps.chibi:4900) used to schedule only `RecordSemantic(node, WriteFact, 0)`. That fact feeds the preparation summary `direct_write` (Body.chibi:865) and the `WriteSummaries` fixpoint (Compile.chibi:2041). The commit also schedules `RecordEffects(WriteEffect)`. The body driver ORs that into `semantic_effects` (Body.chibi:2645-2647), which becomes `Body.Emitted.effects`. Assembly starts each function's published mask from it (`direct_effects := body_effects`, Compile.chibi:2440), propagates it over call edges (Compile.chibi:2843-2853) and writes it into each entry's `effects` (Compile.chibi:2855-2860). In: resolved store steps. Out: the per-function effect mask in the compiled module.
- **Counts:** `WriteStep` has 4 call sites (Steps.chibi:795, 906, 910, 913). Each external store now costs one more `Work` item. In the repository there are 0 `p^ =` stores in `tools/compiler/*.chibi`, and 6 fixture files with one each, all behaviors or systems (`git grep -c -E '\^ (=|\+=|-=)'`). The test now has 21 cases × 2 authored-compiler backends × 2 target backends = 84 comparisons (it had 13 × 4 = 52).
- **Consumers:** `execution_module_function_info.effects`. `src/execution/native.c:120` binds a native export only to an authored function with `effects == 0`. The test derives `parallel` from `pure && !effects && behavior_function_hosts_pure` (argument_effects_chibi.c:330), and this purity decides automatic test parallelism. In source mode (Library, Behavior), Compile.chibi:2741-2742 also ORs `Effects(...)` from state stores and written component references.
- **Layer:** hot (a compiler pass). The added cost is one `Work` per external store, negligible at the counts above.
- **Failure classes:** bug only. The compiler published a summary that differs from the C oracle. No external input or environment failure path changes.
- **Twins:** yes. C decides external stores with one predicate, `script_analyzer_writes_external` (semantic_statement.c:630), called at semantic_statement.c:698 and 891 and at semantic_calls.c:2335. Chibi decides them through the `writes` flag that each `WriteStep` caller computes.

Regression check: the new cases fail on the parent commit. `facts` is a `package`, so `source_mode` is false (Compile.chibi:2310-2311) and nothing ORs `Effects()` in. Before the commit, `WriteStep` fed only `direct_write`, so `StoreInt` published 0 against an expected `SCRIPT_EFFECT_WRITE`. The test therefore guards the change it ships with.

## Findings

```
[BUG] tools/compiler/package_compiler/Sequences.chibi:472 — field and element stores through a scoped ^T parameter still publish no WRITE; only whole-value stores (`p^ = v`) reach WriteStep
Cost: debug/run — the most common store through a pointer (`p.x = 9`, as in package_compiler_chibi.c:2525 `Nudge`) gets a different effect mask from the Chibi compiler than from C, and the parity test can't see it
Evidence: ScopedTargets (References.chibi:144-173) rewrites `pointer^.value` to `pointer.value`. A scoped parameter is bound as `place: ReferencePlace, writable: true` (Body.chibi:2072-2073). The body driver checks `ArrayElementAssignment` (Body.chibi:3352) before `StatementStep` (Body.chibi:3354), and that check returns true for any member or index assignment rooted at a writable name or a dereference (Sequences.chibi:371-391). So these stores take ElementAssignStep, which ends in a `ReferenceWriteIntrinsic` sequence call (Sequences.chibi:472) with no RecordSemantic or RecordEffects. Intrinsic hosts have `linked: -1`, so Compile.chibi:2628-2629 adds nothing either. In C, a scoped parameter's symbol carries SCRIPT_TYPE_REFERENCE (callable_headers.c:468-473), so writes_external walks MEMBER to that symbol and returns true (semantic_statement.c:633-646). This is the same rule that gives StoreInt WRITE. Failing case: `pub StoreField :: (pointer: ^Counter) { pointer.value = 2; }` (and the `pointer^.value = 2` spelling), expected { SCRIPT_EFFECT_WRITE, false }. Traced, not run. Same route at HEAD cd428b92e (Body.chibi:3538).
Fix: in ElementAssignStep's last phase, return `WriteStep(CleanupSequenceValues(...), item.node, external)`, where `external` is true when RootOf(destination) is a dereference or a ReferencePlace name. A CellPlace root stays false: that is a copy-on-write local.
```

```
[BUG] tools/compiler/package_compiler/Pointers.chibi:617 — stores through a managed pointer (`p^ = v`, `p^ op= v`) publish neither WRITE nor the preparation WriteFact
Cost: debug/run — the commit message says the public WRITE comes "for successful external stores", and C treats every dereference target as external. This route is outside the fix. The bug predates the commit, but the commit message says the opposite.
Evidence: Steps.chibi:318-320 sends dereference destinations to PointerAssignmentStart. PointerAssignmentStep then returns PointerCompoundStep (Pointers.chibi:614, which ends in PointerWriteStep at :573) or PointerWriteStep (:617). PointerWriteStep (Pointers.chibi:327-355) returns `next: []` with a `PointerWriteIntrinsic` host, `linked: -1`. In C, a DEREFERENCE is neither MEMBER/INDEX nor SYMBOL, so writes_external returns true (semantic_statement.c:640-643). Failing case: `pub StoreManaged :: (pointer: ^int) -> ^int { pointer^ = 2; return pointer; }`. Returning the parameter makes it a managed pointer per References.chibi:197-202. Expected { SCRIPT_EFFECT_WRITE, false }. Traced, not run. Pointers.chibi at HEAD still has no WriteStep or WriteFact (`grep` at cd428b92e).
Fix: wrap the two returns at Pointers.chibi:614 and :617 in `WriteStep(..., item.node, true)`. Leave ManagedAssignStep (Steps.chibi:792) alone: that is a write to the function's own address-taken local, and C gives that SYMBOL no WRITE.
```

```
[DESIGN] tools/compiler/package_compiler/Steps.chibi:4900 — the external-store rule lives in a `writes` flag computed at each WriteStep caller, rather than in one predicate over the destination as in the C twin
Cost: change — every store route must remember to call WriteStep with the right flag. The two BUGs above are routes that don't, and the doc comment at :4897-4899 tells the reader that every external store passes through here.
Evidence: WriteStep's 4 callers decide locally: `binding.place == ReferencePlace` (Steps.chibi:796, 908) and a literal `true` (:911, :914). Store routes with no WriteStep: ElementAssignStep (Sequences.chibi:472), PointerAssignmentStep (Pointers.chibi:614/617) and MapPlaceDeleteStep for `delete p^[k]` (Maps.chibi:279; C covers it at semantic_statement.c:698). The StatePlace store (Steps.chibi:848) records the WriteFact by hand and depends on Compile.chibi:2742 for the public bit. C has one function, script_analyzer_writes_external, with 3 callers.
Fix: one `ExternalDestination(forest, text, names, destination) -> bool` over RootOf(destination): true for a ReferencePlace name, a dereference, a component or a state. Every store route passes it to WriteStep. This is also the fix for both BUGs.
```

```
[TEST] test/language/argument_effects_chibi.c:159-171 — every new store case takes the same route (Steps.chibi:794-796); field stores through a reference and managed-pointer stores are missing
Cost: debug — the commit message says the test verifies "scalar and record stores", but the record case is whole-record replacement only, so a reader will assume record field stores are covered when they still disagree with C
Evidence: ScopedTargets turns StoreInt `pointer^ = 2` and StoreRecord `pointer^ = {value: 2}` into identifier assignments with a ReferencePlace binding, which reach Steps.chibi:795. CallWrite*, OwnWrite* and CallRead only propagate those callees. No case reaches ElementAssignStep or PointerAssignmentStep.
Fix: add `StoreField` and `StoreManaged` from the BUG findings (both expected WRITE, not parallel), and a copy-on-write control `pub LocalField :: () -> int { c: Counter = {value: 1}; c.value = 2; return c.value; }` expected { 0, true }. Run them with the `--argument-effects-chibi` selector (`language-argument-effects-chibi` in tools/test_policy.py:457).
```

```
[NIT] test/language/argument_effects_chibi.c:183 — the new `parallel` column can be derived: in all 21 rows `parallel == (effects == 0)`
Cost: read — a reader cross-checks 21 booleans to find the row where the column matters, and no such row exists
Evidence: rows 185-206. Every 0-effect row is `true`, and every READ or WRITE row is `false`.
Fix: drop the field and compare `parallel != (cases[i].effects == 0)`. Bring the column back when a case needs 0 effects with impure hosts.
```

## What I would delete

- The `parallel` column in the test table (NIT above).
- Probably the FieldPhase write route: the scheduling at Steps.chibi:554-586 and the `WriteStep(EffectsStep(FieldStep(...)))` at Steps.chibi:903-908. Since 7c6ca2f5d (#694, "Collections as values"), Body.chibi:3352 sends every plain `x.f = v` with a writable identifier root to ElementAssignStep before StatementStep runs. The branch at :554 can still succeed only for a `changing` member push, where StatementTarget yields a member expression. Confirm that case first (see below). If it doesn't reach FieldPhase either, the commit's change at :906 is dead code and so is the route.

## What I would measure

- Run the three proposed cases (`StoreField`, `StoreManaged`, `LocalField`) at this commit through `--argument-effects-chibi`. That confirms or refutes both BUGs; I traced them without running them.
- Whether FieldPhase is reachable: add a temporary assert in the `item.phase == FieldPhase` arm, then run the language package groups and the compiler self-build.
- Whether the missing WriteFact on the ElementAssignStep and PointerWriteStep routes makes `ItemWrite` (Compile.chibi:2079) report an update item that contains a store through a pointer as clear. If it does, GenericUpdates (Compile.chibi:2097) lets that item mutate its receiver in place. The input to test: an update item whose body writes `p.x` through a scoped or managed pointer.
- Package-mode component writes. The commit adds WRITE at ComponentPhase and ComponentAssignPhase in every mode, but `Effects()` already covers source mode (Compile.chibi:2741-2742). If a `package` can write `e.C.f` through a handle, its published mask changed with no test. Count such sources, or confirm that HandleComponent can't match in Package mode.
- How often the two "does this function write" summaries disagree. Compare `WriteSummaries` (`summaries[i] == WriteEffect`) with the published `effects[i] & WriteEffect` across the fixtures and the compiler self-build. If they never disagree, one could be derived from the other.

## What I would leave alone

- **The two summary channels:** `direct_write` with the `WriteSummaries` fixpoint, and public `effects` with the `effect_calls` fixpoint. Merging them means reconciling natives lowered to intrinsics: they reach `direct_write` through `DirectiveFacts` (Body.chibi:867-868) but reach public effects only as linked hosts (Compile.chibi:2628-2629). That is a larger change than this fix, and the answer depends on the measurement above.
- **The redundant WRITE on component writes in source mode** (Steps.chibi:910, 913, already covered at Compile.chibi:2742). It costs one `Work` per component write and keeps WriteStep uniform.
- **OwnWriteInt and OwnWriteRecord pinned to WRITE.** A function that writes only its own local is over-approximated as writing, as C does. The goal is parity with the oracle, the test comment at :202 documents the choice, and making it precise is a separate language decision. Note the asymmetry it creates: a scoped read (ScopedRead, CallRead) publishes 0, but a scoped write publishes WRITE.
- **The test staying in C (pico_unit).** Its oracle is the C front end's analysis, which an authored `@test` can't reach.
- **The cost of the extra `Work` item per store.** At the counts in the brief it can't be measured.
