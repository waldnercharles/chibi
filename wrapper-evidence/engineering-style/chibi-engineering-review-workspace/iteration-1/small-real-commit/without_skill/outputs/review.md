# Review: a1135ab93 "Preserve scoped-write effects in authored function summaries"

- Repository: `chibi` (checkout `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`, read only)
- Parent: `7070712e2`. Landed on `main` through PR #831 (merge `f2f57ec66`).
- Files: `tools/compiler/package_compiler/Steps.chibi` (+4/-2), `test/language/argument_effects_chibi.c` (+59/-30)

## Verdict

**Changes requested.** Under this repo's merge-first policy, the fixes can come in a follow-up commit.

The one-line compiler change is correct for the paths it touches, and the new test table improves on the old one. But the fix covers only one of stage one's two store-through-pointer lowerings. The commit message claims more than the code does ("add the public WRITE effect only for successful external stores"). The test also leaves several paths that this diff changes without a case.

## What the change does

`WriteStep` (Steps.chibi:4897-4912) used to schedule only `RecordSemantic(node, WriteFact, 0)`. That fact feeds the preparation write summary (`DirectiveFacts`, Body.chibi:865; `summaries.append(direct_write)`, Compile.chibi:2051), which is used for in-place update proofs. It never reached the published function effects. Those come from `semantic_effects`, which only `EffectsPhase` items fill (Body.chibi:2645-2648 → `body_effects` → `direct_effects`, Compile.chibi:2440 and 2740). As a result, stage one published `0` for `StoreInt :: (pointer: ^int) { pointer^ = 2; }`, while stage zero publishes WRITE. The commit adds `RecordEffects(WriteEffect)` next to the fact. Callers then inherit the bit through the effect-call fixed point (Compile.chibi:2838-2853).

## Findings

### 1. Major: stores through non-scoped pointers still publish no WRITE

`PointerWriteStep` (Pointers.chibi:327-355) emits neither `WriteFact` nor `WriteEffect`. Its read twin `PointerReadStep` wraps its result in `EffectsStep(..., ReadEffect)` (Pointers.chibi:180-182). That is why the existing `Dereference` case reads READ in both compilers.

Every assignment whose target is a `dereference_expression`, or a member of a pointer-typed or managed binding, goes to `PointerAssignmentStart`/`PointerAssignmentStep` (Steps.chibi:318-321, Pointers.chibi:672-742, 578-618). Compound assignment goes there too, via `PointerCompoundStep` (Pointers.chibi:572-574). None of these route through `WriteStep`. Only scoped parameters avoid this path, because `ScopedTargets` rewrites `p^` to `p`.

Stage zero treats every dereference store as external. `script_analyzer_writes_external` (src/language/semantic_statement.c:630-651) returns true for any expression that is not a member, index or plain symbol. docs/package-compiler.md:548-550 requires each function to carry "stage zero's ... effects".

Expected divergences (each one is the write mirror of a case already in the table):

```chibi
pub DereferenceWrite :: () -> int { value: int = 1; pointer := &value; pointer^ = 2; return value; }
// A ^T parameter that escapes is managed, not scoped, so it takes the PointerWriteStep path.
pub KeepWrite :: (pointer: ^int) -> ^int { pointer^ = 2; return pointer; }
```

The managed-pointer case is the one that matters. A function that writes caller-visible heap memory still publishes `0`, so schedulers and purity gates still treat it as pure. This is the same bug class the commit set out to fix, on a path that is arguably more dangerous. With no `WriteFact`, the preparation-side in-place proof (docs/package-compiler.md:110-115) also cannot see these writes.

Suggested fix: wrap the `PointerWriteStep` results that `PointerAssignmentStep` returns in `WriteStep(..., item.node, true)`. Then add the two cases above to the table with `SCRIPT_EFFECT_WRITE, false`.

I reached this by reading the code and have not executed it. Adding the two rows would confirm it within one test run.

### 2. Moderate: the diff changes five call sites, but the test covers one shape

`WriteStep` is shared, so the new WRITE now also applies to:

- Steps.chibi:795: whole-place stores, compound included (`PlaceAssignStep` handles `operator`, References.chibi:496). Only plain `pointer^ = v` is tested. There is no `pointer^ += 1` case.
- Steps.chibi:906-908: field stores through a scoped reference (`pointer.value = 2` / `pointer^.value = 2`). There is no case. `StoreRecord` replaces the whole record.
- Steps.chibi:910-914: `ComponentWriteStep` and `ComponentAssignStep`, with `writes` hard-coded to `true`. In source mode these were already WRITE through `Effects` (Behavior.chibi:177-193). In package and library mode they were WRITE-free until now. This change in behavior is not tested and the commit message does not mention it.

Please add `StoreField` and `IncrementInt` rows. For the component paths, either add a package-mode component write case or state in the message that package mode cannot reach them.

### 3. Moderate (design): `OwnWriteInt`/`OwnWriteRecord` lock in an over-approximation as expected behavior

Effect propagation is unconditional: caller effects are OR-ed with callee effects (Compile.chibi:2838-2853) and carry no argument provenance. So `OwnWriteInt` is WRITE and not parallel, even though it only mutates its own local. Meanwhile `LocalBox`, which stores into the same kind of box directly, is pure. It matches stage zero, so parity is satisfied.

The problem is the test comment ("A scoped write retains its callee's WRITE even for an owned local"). It states the behavior as intended without saying why. With that wording, a later precision fix reads as a regression. The over-approximation is also observable now: for example, `library_function` (src/execution/native.c:114-123) refuses to bind any authored function whose `effects` is nonzero, so a pure scalar export that uses an out-parameter helper on a local stops binding.

Reword the comment so it says this is conservative stage-zero parity, not the target semantics. Also record the gap where language decisions live, in `docs/language-friction.md` next to the "scoped pointer parameters" entry (around line 558), citing Zig and Odin, where `&local` arguments do not escape the caller. The repo's rules ask for exactly that kind of entry.

### 4. Minor: two channels now carry the same fact

`WriteStep` schedules a `WriteFact` (→ `direct_write`) and a separate `EffectsPhase` item (→ `semantic_effects`) for one event. This bug happened because the two channels drifted apart. The `StatePlace` path (Steps.chibi:848) and finding 1 show they can still drift.

A single source of truth would be simpler: in `Assemble`, set `direct_effects |= WriteEffect` when `body_write == WriteEffect` (Compile.chibi:2439-2440). Compare with `==`, not `&`, because `UnknownWrite` is `-1` (Body.chibi:114). Every current and future `WriteFact` site would then reach the public summary automatically. This is optional, but it is the kind of simplification the repo's rules ask for.

### 5. Minor: the commit message says what, not why

The body describes the mechanics ("keep the existing preparation write fact and add the public WRITE effect"). It should state the defect and its consequence: stage one published no WRITE for functions that store through scoped pointers, so callers and schedulers saw writers as pure. It should also mention the component-write behavior change from finding 2. The claim "add the public WRITE effect only for successful external stores" is not true while `PointerWriteStep` stores, which are external, still get nothing.

### 6. Nit: the `parallel` column adds no signal for WRITE rows

`parallel = pure && !actual.effects && hosts_pure(...)`, so any nonzero expected effect forces `false`. Keeping the column for uniformity is fine. Just don't count those rows as parallelism coverage.

## Questions

- Sequence mutation through a scoped place (`MutationStep` → `SequencePlaceStep` → `NamePlaceStep`, which accepts `ReferencePlace` at Sequences.chibi:764) and `ElementAssignStep` (Sequences.chibi:394-475) never call `WriteStep`. docs/language-friction.md:558-575 says stage one refuses `v[i] = x` through a scoped parameter, but I found no matching refusal for mutating methods such as `append`. If `pointer^.append(x)` or `append(pointer^, x)` compiles, it publishes no WRITE, while stage zero publishes WRITE (semantic_calls.c:2333-2337). Is it refused somewhere I missed?

## What is good

- The fix sits in the right place, is minimal, and leaves the `.Failed` path and the existing preparation fact untouched.
- The table refactor removes `expected_parallel = i == 0 || i == 1 || i == 6 || i == 8 || i == 12`, which silently broke on any reordering. Each case now carries its own expectations, and the old 13 expectations are preserved exactly.
- The new cases cover direct stores, transitive callers, owned-local callers, and negative controls (`LocalBox` guards the `BoxPlace` → `writes=false` rule; `CallRead` guards against READ leaking). They run on both compilers and both targets, with fixed masks that also guard the stage-zero oracle.
- A C harness is justified here: the oracle is stage zero's analysis, which an authored `@test` cannot express. No other new C was added.

## Verification performed

- Read the diff, all five `WriteStep` call sites, `EffectsStep`, `PointerReadStep`/`PointerWriteStep`, the effect assembly and propagation in `Compile.chibi`, and stage zero's `script_analyzer_writes_external`.
- Ran clang-format 22 (`/opt/homebrew/opt/llvm@22/bin/clang-format`, repo `.clang-format`) on `test/language/argument_effects_chibi.c` at the commit: no changes.
- Did not build or run anything, to keep the checkout read only. The PR #831 description reports `language-argument-effects-chibi` passing with 3,469 assertions on the PR head, which includes later commits. So the new rows do hold in both compilers there.
- Pre-fix failure, by reading only: `WriteFact` reached only preparation summaries, so stage one published `0` for the new WRITE rows, and they would have failed before the fix. Not executed.
- Checked whether the change alters the bootstrap artifact through self-compilation: the compiler sources contain no stores through pointers, so their own effect summaries are unchanged.
