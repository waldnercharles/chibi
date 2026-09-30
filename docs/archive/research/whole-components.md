# Direct compiled component access

The September 12 component pass adds direct native access for reflected Get, Has,
Set and field Set, including Self, default-Self targets, explicit entity targets
and component input bindings. It builds on `6bfde209` in the isolated
`cwaldner/compiled-behavior-paths` branch.

The native instructions validate the entity's slot, generation, membership,
component presence and storage layout, then load or store typed fields. They
make no C component-accessor calls. Bytecode and generated C implement the same
lowered operations. The ECS owns a compact execution view and refreshes it when
registration reallocates the lookup table or migration changes a column.
Externally supplied layout hashes cannot authorize an incompatible allocation.
The execution hash includes nested reflected layouts on the current target.

Straight control paths reuse validated addresses. Joins and loop edges end that
reuse, and future structural/host opcodes must also end it. Writes never reuse
cached field values: a subsequent read sees prior writes. Stored node outputs
retain snapshots through typed slots, even after the component changes. Unused
snapshot fields disappear before code generation. Local forwarding removes
unnecessary stack stores and reloads while preserving storage at control joins.
Normal and inspected compiled execution now report direct input-binding errors
at the same flow boundary as the oracle.

## Measurements

Apple M4 Pro, Clang 23.1.1, Release `-O3`, sanitizers off, floating contraction off.
Five trials rotate SLJIT, generated C and bytecode order, with two million calls
per trial. The benchmark reads Health, updates a field twice and returns both
execution-time snapshots. All variants use the same checks and result storage.
Compile time and final result allocation are outside these kernel timings.

| Backend | Median | Range |
| --- | ---: | ---: |
| SLJIT | 4.907 ns | 4.873–5.640 ns |
| Generated C | 3.335 ns | 3.231–3.400 ns |
| Bytecode | 143.511 ns | 140.582–145.385 ns |

SLJIT is **1.471× generated C**, passing the unchanged **1.5×** performance gate,
and about **29.2× faster than bytecode** for this kernel. The first implementation
measured about 15 ns against 9 ns for C and failed the gate. Address reuse alone
improved both backends but left native stack traffic behind. Forwarding local
snapshots into registers produced the accepted result.

The existing scalar, argument/return, call, math, integer, aggregate and scheduler
gates also pass. Native/C ratios range from 1.029 to 1.410 for those whole bodies;
aggregate is 1.205. These bounds apply to the measured kernels, not all gameplay.

Five paired Arena runs alternate the baseline and changed executable. Each uses
seed 12345, 30 warmup frames and 150 measured frames with profiling enabled.
Every run verifies firing, restart, 63 behavior instances and zero oracle calls.
The table contains medians of the five per-run percentiles:

| Backend | Behavior p50 before | After | Behavior p95 before | After |
| --- | ---: | ---: | ---: | ---: |
| SLJIT | 49 µs | 46 µs | 78 µs | 76 µs |
| Bytecode | 53 µs | 53 µs | 75 µs | 76 µs |

Native behavior p50 improves **6.1%**. Simulation p50 changes from 55 to 53 µs.
Projectile Tick execution falls from 5.520 to 3.580 µs per frame, about **35%**;
TwinStick Input Tick falls from 0.353 to 0.087 µs. Wave Tick remains around
16 µs per frame. Bytecode shows no overall p50 improvement in this sample.
These are bounded simulation measurements, not presentation frame rates.

Raw trials, executable hashes, exclusions and validation records are in
[whole-components-results.json](whole-components-results.json).

## Coverage and acceptance

Whole bodies now cover **5/33 events**, **3/31 effectful local functions**,
**39/39 pure local functions** and **3/4 library functions**. The four new events
are Surface Pulse Begin, Twin Stick Input Input/Tick and Twin Stick Weapon Begin.
The two new effectful functions are Advance Shard Motion and Face Direction.
The strict catalog audit still returns nonzero while other bodies are excluded.

Parity tests cover component and field snapshots, full writes, explicit targets,
Self, Has behavior, missing worlds/components, invalid and reused entity handles,
queued destruction, field bindings and error ordering at every step budget.
They exercise lookup collisions and reallocation, schema migration with a stale
external hash, and restoration of a compatible schema. Generated C compares
state, snapshots and source errors against both compiled backends. The portable
smoke uses a separate nested component and another target entity, preserves a
64-bit field on WASM, and proves bytecode execution with no SLJIT or oracle.

Verification passed:

- `make test` and `make build`.
- Sanitized `test_suite --behavior-parity`, including Scene/prototype state hashes,
  waits, cancellation and inspection tests.
- `make web`, including the component bytecode smoke.
- `check-behavior-backend` in a native build with SLJIT disabled, rejecting Play
  without an interpreter fallback.
- `tools/bench_components.py --trials 5 --iterations 2000000` and
  `tools/bench_behavior.py --trials 5 --iterations 2000000`.
- Ten paired `test_suite --compiled-arena` runs and `git diff --check`.

Managed component fields and containers remain excluded. A custom component
reader that supplies no ECS execution view uses its general compiled callback
path. Whole-function debugger instrumentation, host effects, resumable frames,
recursive calls and complete gameplay comparison with generated C remain open.
The next control/value blockers are ForLoop, ForEach and persistent aggregate
writes, especially in Wave Tick. Full completion requirements remain in
[behavior-completion.md](behavior-completion.md).
