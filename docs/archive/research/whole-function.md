# Whole-function behavior compilation records

Dated records (September 11–12, 2026) of the work that brought every authored
behavior entry under whole-function compilation: one SLJIT, portable bytecode or
generated-C body per event or function, with no node dispatch inside it. Each
record states what was lowered, the parity checks, and SLJIT timings against C
emitted from the same lowered program; the adjacent `.json` and `.txt` files
hold raw trials and catalog audits. Measurements use an Apple M4 Pro. The 1.5x
SLJIT/generated-C ratio was the performance gate. Catalog counts are events out
of 33 at the time of writing. Current behavior is described in
[compiled behaviors](../../compiled-behaviors.md); `test_suite
--whole-function-audit` now enforces full catalog coverage in `make test`.

## Compiler foundation

- [Whole-function performance](whole-function-performance.md): scalar events as one SLJIT body (1.30x generated C) and a scheduler cursor that made waking 4,096 Delays 54x faster; EnkiTS and Flecs were assessed and not adopted.
- [Function calls](whole-function-calls.md): typed argument/return ABI for effectful functions; passes 1.5x.
- [Call inlining](whole-function-inline.md): logical frames, retained Set outputs and guarded straight-path optimization; passes 1.5x after optimization.
- [Pure functions](whole-pure-functions.md): pure bodies published independently of callers; 1.37x.
- [Math calls](whole-math-calls.md): direct platform math calls and helper-free Clamp; 1.12x.
- [Integers](whole-integers.md): native I32/I64 arithmetic, comparisons and saturating casts; passes 1.5x.
- [Loops](whole-loops.md): ForLoop, ForLoopWithBreak and Break; the counted-loop benchmark missed the gate at 2.15x.

## Values and data

- [Aggregates](whole-aggregates.md): fixed-layout structs flattened into typed slots; 1.29x after optimization.
- [Struct aliases](whole-aliases.md): `NODE_JIT("identity")` conversions compile as copies; 1.25x.
- [Components](whole-components.md): direct Get/Has/Set on reflected components with no accessor calls; 1.47x.
- [Persistent stores](whole-stores.md): struct Set Variable into instance-owned storage; passes 1.5x.
- [Managed values](whole-managed.md): nested structs with strings and names; 28/33.
- [String to name](whole-names.md): `To Name` node and edge conversions; 29/33.
- [Array views](whole-arrays.md): borrowed views and ForEach over scalar and struct arrays; 1.21x. Superseded by owned arrays.
- [Owned arrays](whole-owned-arrays.md): defaults, snapshots, returned and persistent arrays.
- [Array operations](whole-array-ops.md): the full array family through named native value adapters.
- [Array search](whole-search.md): typed Contains/Find loops; 15 of 16 workloads pass, and fixed numeric array equality that compared padding.
- [Sets](whole-sets.md): all four Set nodes; SET container kind preserved through frames and watches.
- [Entity queries](whole-queries.md): Find Entities With Component through a typed snapshot ABI; 1.05x.

## Flow state and selection

- [Persistent flow state](whole-state.md): DoOnce, DoN, FlipFlop and Gate in a per-instance state bank.
- [MultiGate](whole-multigate.md): lowered control with an explicit C helper owning the selection mask.
- [Switch](whole-switches.md): Int/Enum/String/Name Switch as comparison chains; short text labels compared inline.

## Suspension and host calls

- [Continuations](whole-suspend.md): explicit suspension and resume positions shared by all three backends.
- [Delay](whole-delay.md): authored Delay as a time comparison plus continuation, with inspection variants.
- [Retriggerable Delay](whole-retrigger.md): deadline retiming through an explicit runtime helper.
- [Spawn and async commands](whole-async.md): Spawn Prototype inside whole bodies; 16/33.
- [Deferred commands](whole-commands.md): Send Event and Destroy Entity as direct C command calls; 1.07x.
- [Motion](whole-motion.md): spring, angle and rounding operations; 17/33, spring still missed the gate.
- [Nearest entity](whole-nearest.md): typed host data call for Find Nearest Entity; 19/33.
- [Material writes](whole-material.md): Set Material Parameter through a typed host call; 21/33.
- [Random](whole-random.md): Random Real/Range/Vector and bytecode operand metadata; 25/33.
- [Particles](whole-particles.md): Emit Particles through typed host entry points; 26/33.
- [Visual reactions](whole-reaction.md): Trigger Visual Reaction expanded into IR; 31/33.
- [Rhythm and reflected calls](whole-rhythm-reflection.md): Scene Rhythm and reflected native calls complete the catalog at 33/33; the audit joins `make test`.
